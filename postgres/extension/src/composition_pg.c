#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "blake3.h"
#include "laplace/composition.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace_pg_internal.h"
#include "composition_pg.h"
#include "persistence_rows_pg.h"
#include "persistence_pg.h"
#include "set_pg.h"

#if !defined(LAPLACE_PG_COMPOSITION_ENTRYPOINT)
#define LAPLACE_PG_COMPOSITION_ENTRYPOINT LAPLACE_PG_COMPOSITION_DEPOSIT_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_COMPOSITION_ENTRYPOINT);

static SPIPlanPtr entity_presence_plan = NULL;
static SPIPlanPtr physicality_presence_plan = NULL;

static bool composition_query_boolean(void) {
    bool is_null = false;
    Datum value;
    if (SPI_processed != 1u || SPI_tuptable == NULL) {
        return false;
    }
    value = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    return !is_null && DatumGetBool(value);
}

static int digest_compare(const void* left, const void* right) {
    return memcmp(
        ((const laplace_digest256*)left)->bytes,
        ((const laplace_digest256*)right)->bytes, 32u);
}

static ArrayType* composition_occurrence_ids(
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* input,
    size_t* unique_count) {
    laplace_digest256* identifiers;
    Datum* values;
    size_t input_index;
    size_t output_count = 0u;
    if (execution == NULL || input == NULL || unique_count == NULL ||
        execution->result_count != (size_t)input->request_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace composition occurrence membership input is incomplete")));
    }
    identifiers = (laplace_digest256*)palloc0(
        sizeof(*identifiers) * execution->result_count);
    for (input_index = 0u; input_index < execution->result_count; ++input_index) {
        laplace_persistence_attestation_record occurrence;
        memset(&occurrence, 0, sizeof(occurrence));
        occurrence.entity_id = execution->results[input_index].entity_id;
        occurrence.physicality_id = execution->results[input_index].physicality_id;
        occurrence.source_fingerprint = *input->source_fingerprint;
        occurrence.context_fingerprint =
            input->requests[input_index].occurrence_context_fingerprint;
        occurrence.source_ordinal = input->requests[input_index].source_ordinal;
        occurrence.flags = LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY;
        occurrence.attestation_kind =
            LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
        if (laplace_persistence_attestation_identify(
                &occurrence, &identifiers[input_index]) !=
                LAPLACE_PERSISTENCE_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace composition occurrence membership identity failed")));
        }
    }
    qsort(
        identifiers, execution->result_count, sizeof(*identifiers),
        digest_compare);
    for (input_index = 0u; input_index < execution->result_count; ++input_index) {
        if (output_count == 0u ||
            memcmp(identifiers[input_index].bytes,
                   identifiers[output_count - 1u].bytes, 32u) != 0) {
            identifiers[output_count++] = identifiers[input_index];
        }
    }
    if (output_count != execution->summary.occurrence_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace composition occurrence membership count differs from the canonical working set")));
    }
    values = (Datum*)palloc(sizeof(*values) * output_count);
    for (input_index = 0u; input_index < output_count; ++input_index) {
        values[input_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            identifiers[input_index].bytes, 32u));
    }
    *unique_count = output_count;
    return construct_array(
        values, (int)output_count, BYTEAOID, -1, false, TYPALIGN_INT);
}

void laplace_pg_persist_composition_execution_receipt(
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* input) {
    static const char receipt_sql[] =
        "WITH written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".composition_execution_receipt(working_set_receipt,presence_semantic_receipt,presence_execution_receipt,presence_candidate_fingerprint,presence_disposition_fingerprint,presence_provider_fingerprint,presence_provider_receipt,producer_receipt,staged_stream_receipt,stream_fingerprint,sink_artifacts_fingerprint,occurrence_count,logical_occurrence_count,stream_record_count) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14) ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT EXISTS (SELECT FROM written WHERE working_set_receipt=$1 AND presence_semantic_receipt=$2 AND presence_execution_receipt=$3 AND presence_candidate_fingerprint=$4 AND presence_disposition_fingerprint=$5 AND presence_provider_fingerprint=$6 AND presence_provider_receipt=$7 AND producer_receipt=$8 AND staged_stream_receipt=$9 AND stream_fingerprint=$10 AND sink_artifacts_fingerprint=$11 AND occurrence_count=$12 AND logical_occurrence_count=$13 AND stream_record_count=$14) OR EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".composition_execution_receipt WHERE working_set_receipt=$1 AND presence_semantic_receipt=$2 AND presence_execution_receipt=$3 AND presence_candidate_fingerprint=$4 AND presence_disposition_fingerprint=$5 AND presence_provider_fingerprint=$6 AND presence_provider_receipt=$7 AND producer_receipt=$8 AND staged_stream_receipt=$9 AND stream_fingerprint=$10 AND sink_artifacts_fingerprint=$11 AND occurrence_count=$12 AND logical_occurrence_count=$13 AND stream_record_count=$14)";
    static const char members_sql[] =
        "WITH input AS (SELECT $1::bytea AS working_set_receipt,occurrence_id,ordinality::numeric AS member_ordinal FROM unnest($2::bytea[]) WITH ORDINALITY occurrence(occurrence_id,ordinality)),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".composition_execution_occurrence_member(working_set_receipt,occurrence_id,member_ordinal) SELECT working_set_receipt,occurrence_id,member_ordinal FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT (SELECT count(*) FROM written)+(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".composition_execution_occurrence_member m JOIN input i ON i.working_set_receipt=m.working_set_receipt AND i.occurrence_id=m.occurrence_id AND i.member_ordinal=m.member_ordinal WHERE m.working_set_receipt=$1)=(SELECT count(*) FROM input) AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".composition_execution_occurrence_member m WHERE m.working_set_receipt=$1)=(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".composition_execution_occurrence_member m JOIN input i ON i.working_set_receipt=m.working_set_receipt AND i.occurrence_id=m.occurrence_id AND i.member_ordinal=m.member_ordinal WHERE m.working_set_receipt=$1)";
    Oid types[14] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID};
    Datum values[14];
    Oid member_types[2] = {BYTEAOID, BYTEAARRAYOID};
    Datum member_values[2];
    size_t occurrence_count = 0u;
    ArrayType* occurrence_ids = composition_occurrence_ids(
        execution, input, &occurrence_count);
    int result;
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->summary.receipt_id.bytes, 32u));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->presence.semantic_receipt_id.bytes, 32u));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->presence.execution_receipt_id.bytes, 32u));
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->presence.candidate_fingerprint.bytes, 32u));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->presence.disposition_fingerprint.bytes, 32u));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->presence.provider_fingerprint.bytes, 32u));
    values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->presence.provider_receipt_id.bytes, 32u));
    values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->persistence.producer.receipt_id.bytes, 32u));
    values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->persistence.producer.stream.receipt_id.bytes, 32u));
    values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->persistence.producer.stream.stream_fingerprint.bytes, 32u));
    values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->persistence.producer.stream.sink_artifacts_fingerprint.bytes, 32u));
    values[11] = Int64GetDatum(laplace_pg_checked_int64(
        occurrence_count, "composition occurrence count"));
    values[12] = Int64GetDatum(laplace_pg_checked_int64(
        execution->summary.logical_occurrence_count,
        "composition logical occurrence count"));
    values[13] = Int64GetDatum(laplace_pg_checked_int64(
        execution->summary.stream_record_count,
        "composition stream record count"));
    member_values[0] = values[0];
    member_values[1] = PointerGetDatum(occurrence_ids);
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace composition receipt persistence could not connect to SPI")));
    }
    result = SPI_execute_with_args(
        receipt_sql, 14, types, values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !composition_query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace composition execution receipt conflicts with durable state")));
    }
    result = SPI_execute_with_args(
        members_sql, 2, member_types, member_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !composition_query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace composition occurrence membership conflicts with durable state")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace composition receipt persistence could not close SPI")));
    }
}

static const char entity_presence_sql[] =
    "SELECT i.ordinality,CASE WHEN s.entity_id IS NULL THEN 0 "
    "WHEN s.identity_witness=i.identity_witness THEN 1 ELSE 2 END "
    "FROM unnest($1::" LAPLACE_PG_SCHEMA
    ".entity_record[]) WITH ORDINALITY "
    "AS i(entity_id,identity_witness,ordinality) LEFT JOIN "
    LAPLACE_PG_SCHEMA ".entity s ON s.entity_id=i.entity_id "
#if defined(LAPLACE_TEST_COMPOSITION_PRESENCE_PARTIAL)
    "ORDER BY i.ordinality LIMIT 1";
#elif defined(LAPLACE_TEST_COMPOSITION_PRESENCE_REORDER)
    "ORDER BY i.ordinality DESC";
#else
    "ORDER BY i.ordinality";
#endif

static const char physicality_presence_sql[] =
    "SELECT i.ordinality,CASE WHEN s.physicality_id IS NULL THEN 0 WHEN "
    "s.entity_id=i.entity_id AND s.physicality_type=i.physicality_type AND "
    "s.vertex_class=i.vertex_class AND s.recipe_version=i.recipe_version AND "
    "s.structural_form=i.structural_form AND s.dimension_count=i.dimension_count AND "
    "s.flags=i.flags AND s.recipe_fingerprint=i.recipe_fingerprint AND "
    "s.geometry_epoch=i.geometry_epoch AND "
    "s.trajectory_fingerprint=i.trajectory_fingerprint AND "
    "float8send(s.centroid_x)=float8send(i.centroid_x) AND "
    "float8send(s.centroid_y)=float8send(i.centroid_y) AND "
    "float8send(s.centroid_z)=float8send(i.centroid_z) AND "
    "float8send(s.centroid_m)=float8send(i.centroid_m) AND "
#if !defined(LAPLACE_TEST_COMPOSITION_PRESENCE_SEMANTIC_DRIFT)
    "float8send(s.radius)=float8send(i.radius) AND "
#endif
    "s.logical_count=i.logical_count AND s.vertex_count=i.vertex_count "
    "THEN 1 ELSE 2 END FROM unnest($1::" LAPLACE_PG_SCHEMA
    ".physicality_record[]) WITH ORDINALITY AS i("
    "physicality_id,entity_id,physicality_type,vertex_class,recipe_version,"
    "structural_form,dimension_count,flags,recipe_fingerprint,geometry_epoch,"
    "trajectory_fingerprint,centroid_x,centroid_y,centroid_z,centroid_m,radius,"
    "logical_count,vertex_count,ordinality) LEFT JOIN " LAPLACE_PG_SCHEMA
    ".physicality s ON s.physicality_id=i.physicality_id ORDER BY i.ordinality";

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static laplace_digest256 finish_hash(blake3_hasher* hasher) {
    laplace_digest256 result;
    blake3_hasher_finalize(hasher, result.bytes, sizeof(result.bytes));
    return result;
}

static laplace_digest256 provider_fingerprint(void) {
    static const char domain[] = "laplace-postgresql-composition-presence-provider-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(
        &hasher, entity_presence_sql, sizeof(entity_presence_sql) - 1u);
    blake3_hasher_update(
        &hasher, physicality_presence_sql,
        sizeof(physicality_presence_sql) - 1u);
    return finish_hash(&hasher);
}

static uint64_t tuple_uint64(HeapTuple tuple, TupleDesc descriptor, int column) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace presence provider returned a null scalar")));
    }
    return (uint64_t)DatumGetInt64(value);
}

static uint32_t tuple_uint32(HeapTuple tuple, TupleDesc descriptor, int column) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    const int32 scalar = is_null ? -1 : DatumGetInt32(value);
    if (is_null || scalar < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace presence provider returned an invalid disposition")));
    }
    return (uint32_t)scalar;
}

static void read_dispositions(
    uint8_t* dispositions,
    size_t count,
    blake3_hasher* receipt_hasher) {
    size_t index;
    if (SPI_processed != (uint64)count || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace presence provider returned a partial set")));
    }
    for (index = 0; index < count; ++index) {
        HeapTuple tuple = SPI_tuptable->vals[index];
        TupleDesc descriptor = SPI_tuptable->tupdesc;
        const uint64_t ordinal = tuple_uint64(tuple, descriptor, 1);
        const uint32_t disposition = tuple_uint32(tuple, descriptor, 2);
        if (ordinal != index + 1u || disposition > 2u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace presence provider result order or value is invalid")));
        }
        if (disposition == 2u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace presence candidate collides with different canonical fields"),
                     errdetail("candidate ordinal=%llu",
                               (unsigned long long)ordinal)));
        }
        dispositions[index] = (uint8_t)disposition;
        hash_u64(receipt_hasher, ordinal);
        blake3_hasher_update(receipt_hasher, &dispositions[index], 1u);
    }
}

static ArrayType* entity_group_array(
    const laplace_composition_entity_candidate* candidates,
    const size_t* indexes,
    size_t count) {
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * count);
    ArrayType* result;
    size_t index;
    laplace_pg_entity_binding_open(&binding);
    for (index = 0; index < count; ++index) {
        rows[index] = laplace_pg_entity_record(
            &binding, &candidates[indexes[index]].entity);
    }
    result = laplace_pg_composite_array(&binding, rows, count);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* physicality_array(
    const laplace_persistence_physicality_record* candidates,
    size_t count) {
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * count);
    ArrayType* result;
    size_t index;
    laplace_pg_physicality_binding_open(&binding);
    for (index = 0; index < count; ++index) {
        rows[index] = laplace_pg_physicality_record(&binding, &candidates[index]);
    }
    result = laplace_pg_composite_array(&binding, rows, count);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static laplace_composition_status resolve_presence(
    void* state,
    const laplace_composition_entity_candidate* entity_candidates,
    size_t entity_candidate_count,
    const laplace_persistence_physicality_record* physicality_candidates,
    size_t physicality_candidate_count,
    uint8_t* entity_dispositions,
    uint8_t* physicality_dispositions,
    laplace_composition_presence_provider_result* result) {
    static const char receipt_domain[] =
        "laplace-postgresql-composition-presence-execution-v1";
    Oid entity_types[1];
    Oid physicality_types[1];
    blake3_hasher receipt_hasher;
    size_t tier;
    (void)state;
    if (entity_candidates == NULL || entity_candidate_count == 0u ||
        entity_dispositions == NULL || result == NULL ||
        (physicality_candidate_count != 0u &&
         (physicality_candidates == NULL || physicality_dispositions == NULL))) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    memset(result, 0, sizeof(*result));
    result->provider_fingerprint = provider_fingerprint();
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, receipt_domain, sizeof(receipt_domain) - 1u);
    blake3_hasher_update(
        &receipt_hasher, result->provider_fingerprint.bytes,
        sizeof(result->provider_fingerprint.bytes));
    hash_u64(&receipt_hasher, entity_candidate_count);
    hash_u64(&receipt_hasher, physicality_candidate_count);

#if defined(LAPLACE_TEST_COMPOSITION_PRESENCE_BLIND)
    memset(entity_dispositions, LAPLACE_COMPOSITION_NOVEL,
           entity_candidate_count);
    if (physicality_candidate_count != 0u) {
        memset(physicality_dispositions, LAPLACE_COMPOSITION_NOVEL,
               physicality_candidate_count);
    }
    result->returned_entity_count = entity_candidate_count;
    result->returned_physicality_count = physicality_candidate_count;
    result->provider_receipt_id = finish_hash(&receipt_hasher);
    return LAPLACE_COMPOSITION_OK;
#endif

    if (SPI_connect() != SPI_OK_CONNECT) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    entity_types[0] = laplace_pg_composite_array_oid("entity_record");
    physicality_types[0] = laplace_pg_composite_array_oid("physicality_record");
    laplace_pg_keep_plan(
        &entity_presence_plan, entity_presence_sql, 1, entity_types);
    laplace_pg_keep_plan(
        &physicality_presence_plan, physicality_presence_sql, 1,
        physicality_types);

    for (tier = LAPLACE_COMPOSITION_TIER_MINIMUM;
         tier <= LAPLACE_COMPOSITION_TIER_MAXIMUM; ++tier) {
        size_t* indexes;
        size_t count = 0u;
        size_t index;
        ArrayType* rows;
        Datum values[1];
        int query_result;
        for (index = 0; index < entity_candidate_count; ++index) {
            if (entity_candidates[index].tier_floor == tier) {
                ++count;
            }
        }
        if (count == 0u) {
            continue;
        }
        indexes = (size_t*)palloc(sizeof(*indexes) * count);
        count = 0u;
        for (index = 0; index < entity_candidate_count; ++index) {
            if (entity_candidates[index].tier_floor == tier) {
                indexes[count++] = index;
            }
        }
        rows = entity_group_array(entity_candidates, indexes, count);
#if defined(LAPLACE_TEST_COMPOSITION_PRESENCE_PER_ROW)
        (void)rows;
        for (index = 0u; index < count; ++index) {
            uint8_t disposition;
            rows = entity_group_array(
                entity_candidates, &indexes[index], 1u);
            values[0] = PointerGetDatum(rows);
            query_result = SPI_execute_plan(
                entity_presence_plan, values, NULL, true, 0);
            if (query_result != SPI_OK_SELECT) {
                ereport(ERROR,
                        (errcode(ERRCODE_INTERNAL_ERROR),
                         errmsg("Laplace entity presence row operation failed")));
            }
            read_dispositions(&disposition, 1u, &receipt_hasher);
            entity_dispositions[indexes[index]] = disposition;
            ++result->entity_round_count;
        }
#else
        values[0] = PointerGetDatum(rows);
        query_result = SPI_execute_plan(
            entity_presence_plan, values, NULL, true, 0);
        if (query_result != SPI_OK_SELECT) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Laplace entity presence set operation failed")));
        }
        {
            uint8_t* group_dispositions =
                (uint8_t*)palloc(sizeof(*group_dispositions) * count);
            read_dispositions(group_dispositions, count, &receipt_hasher);
            for (index = 0; index < count; ++index) {
                entity_dispositions[indexes[index]] = group_dispositions[index];
            }
        }
        ++result->entity_round_count;
#endif
    }

    if (physicality_candidate_count != 0u) {
        ArrayType* rows = physicality_array(
            physicality_candidates, physicality_candidate_count);
        Datum values[1] = {PointerGetDatum(rows)};
        const int query_result = SPI_execute_plan(
            physicality_presence_plan, values, NULL, true, 0);
        if (query_result != SPI_OK_SELECT) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Laplace physicality presence set operation failed")));
        }
        read_dispositions(
            physicality_dispositions, physicality_candidate_count,
            &receipt_hasher);
        result->physicality_round_count = 1u;
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace presence provider could not close SPI")));
    }
    result->returned_entity_count = entity_candidate_count;
    result->returned_physicality_count = physicality_candidate_count;
    hash_u64(&receipt_hasher, result->entity_round_count);
    hash_u64(&receipt_hasher, result->physicality_round_count);
    result->provider_receipt_id = finish_hash(&receipt_hasher);
    return LAPLACE_COMPOSITION_OK;
}

static void laplace_pg_composition_presence_provider(
    laplace_composition_presence_provider_v1* provider) {
    if (provider == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace composition presence provider output is null")));
    }
    memset(provider, 0, sizeof(*provider));
    provider->resolve = resolve_presence;
    provider->abi_major = LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI;
    provider->abi_minor = LAPLACE_COMPOSITION_ABI_MINOR;
}

static void read_id128(Datum datum, laplace_id128* identifier, const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(identifier->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("%s must contain exactly %zu bytes",
                        field, sizeof(identifier->bytes))));
    }
    memcpy(identifier->bytes, VARDATA_ANY(value), sizeof(identifier->bytes));
}

static void deconstruct_composite_array(
    ArrayType* array,
    const char* type_name,
    Datum** values,
    bool** nulls,
    int* count) {
    const Oid type_oid = laplace_pg_composite_type_oid(type_name);
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace %s input must be a one-dimensional exact record array",
                        type_name)));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        values, nulls, count);
    if (*count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace %s input cannot be empty", type_name)));
    }
}

static laplace_composition_known_entity* read_known_entities(
    ArrayType* array,
    uint64_t* count) {
    Datum* values = NULL;
    bool* nulls = NULL;
    int item_count = 0;
    int index;
    laplace_composition_known_entity* result;
    deconstruct_composite_array(
        array, "composition_known_entity_record",
        &values, &nulls, &item_count);
    result = (laplace_composition_known_entity*)palloc0(
        sizeof(*result) * (size_t)item_count);
    for (index = 0; index < item_count; ++index) {
        HeapTupleHeader tuple;
        int64 atom;
        int16 tier;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace known-entity input cannot contain null")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        read_id128(
            laplace_pg_required_composite_attribute(tuple, 1, "entity_id"),
            &result[index].entity_id, "known entity_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 2, "identity_witness"),
            &result[index].identity_witness, "known identity_witness");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 3, "physicality_id"),
            &result[index].physicality_id, "known physicality_id");
        result[index].centroid.component[0] = DatumGetFloat8(
            laplace_pg_required_composite_attribute(tuple, 4, "centroid_x"));
        result[index].centroid.component[1] = DatumGetFloat8(
            laplace_pg_required_composite_attribute(tuple, 5, "centroid_y"));
        result[index].centroid.component[2] = DatumGetFloat8(
            laplace_pg_required_composite_attribute(tuple, 6, "centroid_z"));
        result[index].centroid.component[3] = DatumGetFloat8(
            laplace_pg_required_composite_attribute(tuple, 7, "centroid_m"));
        atom = DatumGetInt64(
            laplace_pg_required_composite_attribute(tuple, 8, "atom"));
        tier = DatumGetInt16(
            laplace_pg_required_composite_attribute(tuple, 9, "tier_floor"));
        if (atom < 0 || atom > (int64)UINT32_MAX || tier < 0 ||
            tier > LAPLACE_COMPOSITION_TIER_MAXIMUM) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace known-entity scalar is out of range")));
        }
        result[index].atom = (uint32_t)atom;
        result[index].tier_floor = (uint8_t)tier;
        result[index].has_atom = DatumGetBool(
            laplace_pg_required_composite_attribute(tuple, 10, "has_atom"))
            ? 1u : 0u;
    }
    *count = (uint64_t)item_count;
    return result;
}

static laplace_composition_operand* read_operands(
    ArrayType* array,
    uint64_t* count) {
    Datum* values = NULL;
    bool* nulls = NULL;
    int item_count = 0;
    int index;
    laplace_composition_operand* result;
    deconstruct_composite_array(
        array, "composition_operand_record", &values, &nulls, &item_count);
    result = (laplace_composition_operand*)palloc0(
        sizeof(*result) * (size_t)item_count);
    for (index = 0; index < item_count; ++index) {
        HeapTupleHeader tuple;
        int64 metadata;
        int32 reference_kind;
        int32 flags;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace operand input cannot contain null")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        result[index].reference_index = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 1, "reference_index"),
            "composition reference_index");
        result[index].multiplicity = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 2, "multiplicity"),
            "composition multiplicity");
        metadata = DatumGetInt64(
            laplace_pg_required_composite_attribute(
                tuple, 3, "relationship_metadata"));
        reference_kind = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 4, "reference_kind"));
        flags = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 5, "flags"));
        if (metadata < 0 || reference_kind < 0 || flags < 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace composition operand scalar is out of range")));
        }
        result[index].relationship_metadata = (uint64_t)metadata;
        result[index].reference_kind = (uint32_t)reference_kind;
        result[index].flags = (uint32_t)flags;
    }
    *count = (uint64_t)item_count;
    return result;
}

static laplace_composition_request* read_requests(
    ArrayType* array,
    uint64_t* count) {
    Datum* values = NULL;
    bool* nulls = NULL;
    int item_count = 0;
    int index;
    laplace_composition_request* result;
    deconstruct_composite_array(
        array, "composition_request_record", &values, &nulls, &item_count);
    result = (laplace_composition_request*)palloc0(
        sizeof(*result) * (size_t)item_count);
    for (index = 0; index < item_count; ++index) {
        HeapTupleHeader tuple;
        int32 recipe_version;
        int32 flags;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace composition request input cannot contain null")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        result[index].first_operand = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 1, "first_operand"),
            "composition first_operand");
        result[index].operand_count = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 2, "operand_count"),
            "composition operand_count");
        result[index].source_ordinal = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 3, "source_ordinal"),
            "composition source_ordinal");
        recipe_version = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 4, "recipe_version"));
        flags = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 5, "flags"));
        if (recipe_version < 0 || flags < 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace composition request scalar is out of range")));
        }
        result[index].recipe_version = (uint32_t)recipe_version;
        result[index].flags = (uint32_t)flags;
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 6, "recipe_fingerprint"),
            &result[index].recipe_fingerprint, "composition recipe_fingerprint");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 7, "geometry_epoch"),
            &result[index].geometry_epoch, "composition geometry_epoch");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 8, "occurrence_context_fingerprint"),
            &result[index].occurrence_context_fingerprint,
            "composition occurrence_context_fingerprint");
    }
    *count = (uint64_t)item_count;
    return result;
}

static ArrayType* result_id_array(
    const laplace_composition_result* results,
    size_t count,
    bool physicality) {
    Datum* values = (Datum*)palloc(sizeof(*values) * count);
    size_t index;
    for (index = 0; index < count; ++index) {
        if (physicality) {
            values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                results[index].physicality_id.bytes,
                sizeof(results[index].physicality_id.bytes)));
        } else {
            values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                results[index].entity_id.bytes,
                sizeof(results[index].entity_id.bytes)));
        }
    }
    return construct_array(values, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static ArrayType* result_tier_array(
    const laplace_composition_result* results,
    size_t count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * count);
    size_t index;
    for (index = 0; index < count; ++index) {
        values[index] = Int16GetDatum((int16)results[index].tier_floor);
    }
    return construct_array(values, (int)count, INT2OID, 2, true, TYPALIGN_SHORT);
}

static ArrayType* disposition_array(const uint8_t* dispositions, size_t count) {
    Datum* values;
    size_t index;
    if (count == 0u) {
        return construct_empty_array(INT2OID);
    }
    if (dispositions == NULL || count > INT_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace composition disposition view is invalid")));
    }
    values = (Datum*)palloc(sizeof(*values) * count);
    for (index = 0; index < count; ++index) {
        values[index] = Int16GetDatum((int16)dispositions[index]);
    }
    return construct_array(values, (int)count, INT2OID, 2, true, TYPALIGN_SHORT);
}

void LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(
    laplace_pg_composition_execution* execution) {
    if (execution == NULL) {
        return;
    }
    laplace_composition_working_set_destroy(&execution->working_set);
    memset(execution, 0, sizeof(*execution));
}

void LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution) {
    laplace_composition_presence_provider_v1 presence_provider;
    laplace_framework_producer_v1 producer;
    laplace_composition_status status;
    if (input == NULL || execution == NULL || input->context == NULL ||
        input->source_fingerprint == NULL ||
        input->calculation_recipe_fingerprint == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace composition execution input is incomplete")));
    }
    memset(execution, 0, sizeof(*execution));
    status = laplace_composition_working_set_create(
        input, &execution->working_set);
    if (status != LAPLACE_COMPOSITION_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace composition working-set construction failed"),
                 errdetail("status=%d", (int)status)));
    }
    PG_TRY();
    {
        laplace_pg_composition_presence_provider(&presence_provider);
        status = laplace_composition_working_set_resolve_presence(
            execution->working_set, &presence_provider, &execution->presence);
        if (status != LAPLACE_COMPOSITION_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace PostgreSQL composition presence failed"),
                     errdetail("status=%d", (int)status)));
        }
        status = laplace_composition_working_set_producer(
            execution->working_set, &producer);
        if (status != LAPLACE_COMPOSITION_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace composition producer publication failed"),
                     errdetail("status=%d", (int)status)));
        }
        LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL(
            input->context, input->source_fingerprint,
            input->calculation_recipe_fingerprint, &producer,
            &execution->persistence);
        if (laplace_composition_working_set_summary_get(
                execution->working_set, &execution->summary) !=
            LAPLACE_COMPOSITION_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Laplace composition summary is unavailable")));
        }
        execution->results = laplace_composition_working_set_results(
            execution->working_set, &execution->result_count);
        execution->entity_dispositions =
            laplace_composition_working_set_entity_dispositions(
                execution->working_set,
                &execution->entity_disposition_count);
        execution->physicality_dispositions =
            laplace_composition_working_set_physicality_dispositions(
                execution->working_set,
                &execution->physicality_disposition_count);
        if (execution->results == NULL ||
            execution->result_count != input->request_count ||
            execution->result_count > INT_MAX ||
            execution->entity_dispositions == NULL ||
            execution->entity_disposition_count !=
                execution->summary.unique_entity_count ||
            execution->physicality_disposition_count !=
                execution->summary.unique_physicality_count ||
            (execution->physicality_disposition_count != 0u &&
             execution->physicality_dispositions == NULL)) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace composition execution result is incomplete")));
        }
    }
    PG_CATCH();
    {
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(execution);
        PG_RE_THROW();
    }
    PG_END_TRY();
}

Datum LAPLACE_PG_COMPOSITION_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 source_fingerprint;
    laplace_digest256 calculation_recipe_fingerprint;
    laplace_composition_working_set_input input;
    laplace_pg_composition_execution execution;
    const laplace_composition_result* results;
    size_t result_count = 0u;
    const uint8_t* entity_dispositions;
    const uint8_t* physicality_dispositions;
    size_t entity_disposition_count = 0u;
    size_t physicality_disposition_count = 0u;
    Datum result_values[37];
    bool result_nulls[37] = {false};
    HeapTuple result_tuple;

    memset(&input, 0, sizeof(input));
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    laplace_pg_read_digest(
        PG_GETARG_DATUM(1), &source_fingerprint,
        "composition source_fingerprint");
    laplace_pg_read_digest(
        PG_GETARG_DATUM(2), &calculation_recipe_fingerprint,
        "composition calculation_recipe_fingerprint");
    input.context = &context;
    input.source_fingerprint = &source_fingerprint;
    input.calculation_recipe_fingerprint = &calculation_recipe_fingerprint;
    input.known_entities = read_known_entities(
        PG_GETARG_ARRAYTYPE_P(3), &input.known_entity_count);
    input.operands = read_operands(
        PG_GETARG_ARRAYTYPE_P(4), &input.operand_count);
    input.requests = read_requests(
        PG_GETARG_ARRAYTYPE_P(5), &input.request_count);
    input.preferred_batch_bytes = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(6), "composition preferred_batch_bytes");
    memset(&execution, 0, sizeof(execution));
    PG_TRY();
    {
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(&input, &execution);
        laplace_pg_persist_composition_execution_receipt(&execution, &input);
        results = execution.results;
        result_count = execution.result_count;
        entity_dispositions = execution.entity_dispositions;
        entity_disposition_count = execution.entity_disposition_count;
        physicality_dispositions = execution.physicality_dispositions;
        physicality_disposition_count = execution.physicality_disposition_count;

        result_values[0] = PointerGetDatum(result_id_array(
            results, result_count, false));
        result_values[1] = PointerGetDatum(result_id_array(
            results, result_count, true));
        result_values[2] = PointerGetDatum(result_tier_array(results, result_count));
        result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.summary.receipt_id.bytes,
            sizeof(execution.summary.receipt_id.bytes)));
        result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.semantic_receipt_id.bytes,
            sizeof(execution.presence.semantic_receipt_id.bytes)));
        result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.execution_receipt_id.bytes,
            sizeof(execution.presence.execution_receipt_id.bytes)));
        result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.candidate_fingerprint.bytes, 32u));
        result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.disposition_fingerprint.bytes, 32u));
        result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.provider_fingerprint.bytes, 32u));
        result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.provider_receipt_id.bytes, 32u));
        result_values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.receipt_id.bytes, 32u));
        result_values[11] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.stream.receipt_id.bytes, 32u));
        result_values[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.stream.stream_fingerprint.bytes, 32u));
        result_values[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.stream.sink_artifacts_fingerprint.bytes, 32u));
        result_values[14] = laplace_pg_numeric_from_uint64(execution.summary.unique_entity_count);
        result_values[15] = laplace_pg_numeric_from_uint64(execution.summary.unique_physicality_count);
        result_values[16] = laplace_pg_numeric_from_uint64(execution.summary.novel_entity_count);
        result_values[17] = laplace_pg_numeric_from_uint64(execution.summary.novel_physicality_count);
        result_values[18] = laplace_pg_numeric_from_uint64(execution.summary.trajectory_vertex_count);
        result_values[19] = laplace_pg_numeric_from_uint64(execution.summary.novel_trajectory_vertex_count);
        result_values[20] = laplace_pg_numeric_from_uint64(execution.summary.occurrence_count);
        result_values[21] = laplace_pg_numeric_from_uint64(execution.summary.logical_occurrence_count);
        result_values[22] = laplace_pg_numeric_from_uint64(execution.summary.batch_count);
        result_values[23] = laplace_pg_numeric_from_uint64(execution.summary.stream_record_count);
        result_values[24] = laplace_pg_numeric_from_uint64(execution.summary.stream_byte_count);
        result_values[25] = laplace_pg_numeric_from_uint64(execution.persistence.inserted[0]);
        result_values[26] = laplace_pg_numeric_from_uint64(execution.persistence.inserted[1]);
        result_values[27] = laplace_pg_numeric_from_uint64(execution.persistence.inserted[2]);
        result_values[28] = laplace_pg_numeric_from_uint64(execution.persistence.inserted[3]);
        result_values[29] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.plan_sequence_fingerprint.bytes, 32u));
        result_values[30] = Int32GetDatum((int32)execution.persistence.plan_count);
        result_values[31] = laplace_pg_numeric_from_uint64(
            execution.presence.entity_round_count);
        result_values[32] = laplace_pg_numeric_from_uint64(
            execution.presence.physicality_round_count);
        result_values[33] = PointerGetDatum(disposition_array(
            entity_dispositions, entity_disposition_count));
        result_values[34] = PointerGetDatum(disposition_array(
            physicality_dispositions, physicality_disposition_count));
        result_values[35] = laplace_pg_numeric_from_uint64(
            execution.summary.estimated_peak_working_bytes);
        result_values[36] = Int32GetDatum((int32)LAPLACE_COMPOSITION_OK);
        result_tuple = laplace_pg_form_result_tuple(
            fcinfo, result_values, result_nulls, 37);
    }
    PG_CATCH();
    {
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
        PG_RE_THROW();
    }
    PG_END_TRY();
    LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
