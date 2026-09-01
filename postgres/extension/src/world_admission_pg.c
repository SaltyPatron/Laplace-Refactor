#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "blake3.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/isa.h"
#include "laplace/world_admission.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

#ifndef LAPLACE_PG_WORLD_ADMISSION_ENTRYPOINT
#define LAPLACE_PG_WORLD_ADMISSION_ENTRYPOINT \
    LAPLACE_PG_WORLD_ADMISSION_CLOSE_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_WORLD_ADMISSION_ENTRYPOINT);

static bool query_boolean(void) {
    bool is_null = false;
    Datum value;
    if (SPI_processed != 1u || SPI_tuptable == NULL) {
        return false;
    }
    value = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    return !is_null && DatumGetBool(value);
}

static Datum tuple_value(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    const char* field) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                 errmsg("Laplace durable world-admission field %s is null", field)));
    }
    return value;
}

static void tuple_digest(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    laplace_digest256* digest,
    const char* field) {
    laplace_pg_read_digest(
        tuple_value(tuple, descriptor, column, field), digest, field);
}

static uint64_t tuple_u64(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    const char* field) {
    const Datum value = tuple_value(tuple, descriptor, column, field);
    const Oid type_oid = TupleDescAttr(descriptor, column - 1)->atttypid;
    if (type_oid == NUMERICOID) {
        return laplace_pg_uint64_from_numeric(value, field);
    }
    if (type_oid == INT8OID) {
        const int64 signed_value = DatumGetInt64(value);
        if (signed_value >= 0) {
            return (uint64_t)signed_value;
        }
    } else if (type_oid == INT4OID) {
        const int32 signed_value = DatumGetInt32(value);
        if (signed_value >= 0) {
            return (uint64_t)(uint32_t)signed_value;
        }
    } else {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace durable world-admission field %s has unsupported integer type %u",
                        field, type_oid)));
    }
    ereport(ERROR,
            (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
             errmsg("Laplace durable world-admission field %s is negative", field)));
    pg_unreachable();
}

static uint32_t tuple_u32(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    const char* field) {
    const int32 value = DatumGetInt32(
        tuple_value(tuple, descriptor, column, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace durable world-admission field %s is negative", field)));
    }
    return (uint32_t)value;
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static laplace_digest256 readback_fingerprint(
    const laplace_world_admission_record* record) {
    const laplace_digest256* digests[] = {
        &record->source_profile_id,
        &record->selected_boundary_fingerprint,
        &record->source_profile_receipt_id,
        &record->recipe_receipt_id,
        &record->composition_working_set_receipt_id,
        &record->composition_presence_receipt_id,
        &record->composition_producer_receipt_id,
        &record->composition_stream_receipt_id,
        &record->evidence_lineage_receipt_id,
        &record->evidence_testimony_receipt_id};
    laplace_digest256 result;
    blake3_hasher hasher;
    size_t index;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_WORLD_ADMISSION_READBACK_DOMAIN,
        sizeof(LAPLACE_WORLD_ADMISSION_READBACK_DOMAIN) - 1u);
    for (index = 0u; index < 10u; ++index) {
        blake3_hasher_update(&hasher, digests[index]->bytes, 32u);
    }
    hash_u64(&hasher, record->profile_occurrence_count);
    hash_u64(&hasher, record->composition_occurrence_count);
    hash_u64(&hasher, record->profile_claim_count);
    hash_u64(&hasher, record->evidence_node_count);
    hash_u64(&hasher, record->testimony_count);
    hash_u64(&hasher, record->profile_bound_testimony_count);
    hash_u64(&hasher, record->recipe_bound_testimony_count);
    hash_u64(&hasher, record->lineage_bound_testimony_count);
    hash_u64(&hasher, record->closure_subject_count);
    hash_u64(&hasher, record->closed_subject_count);
    hash_u32(&hasher, record->reconstruction_class);
    hash_u32(&hasher, record->flags);
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

static size_t validate_request_array(ArrayType* requests) {
    const Oid type_oid = laplace_pg_composite_type_oid("world_admission_request");
    const int count = ArrayGetNItems(ARR_NDIM(requests), ARR_DIMS(requests));
    if (ARR_NDIM(requests) != 1 || ARR_ELEMTYPE(requests) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace world-admission input must be a one-dimensional exact world_admission_request array")));
    }
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace world-admission input cannot be empty")));
    }
    return (size_t)count;
}

static laplace_world_admission_record* derive_durable_records(
    Oid request_array_type,
    Datum request_array,
    size_t request_count) {
    static const char derive_sql[] =
        "WITH input AS (SELECT r.source_profile_id,r.source_profile_receipt_id,r.recipe_receipt_id,r.composition_working_set_receipt_id,r.evidence_lineage_receipt_id,r.evidence_testimony_receipt_id,r.ordinality::numeric AS ordinality FROM unnest($1::" LAPLACE_PG_SCHEMA ".world_admission_request[]) WITH ORDINALITY r) "
        "SELECT i.ordinality,p.profile_id,p.selected_boundary_fingerprint,spr.receipt_id,i.recipe_receipt_id,ce.working_set_receipt,ce.presence_semantic_receipt,ce.producer_receipt,ce.staged_stream_receipt,lr.receipt_id,tr.receipt_id,"
        "p.occurrence_count,ce.occurrence_count,p.claim_count,lr.node_count,tr.testimony_count,"
        "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt_member tm JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony t ON t.testimony_id=tm.testimony_id WHERE tm.receipt_id=tr.receipt_id AND t.source_profile_id=p.profile_id),"
        "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt_member tm JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony t ON t.testimony_id=tm.testimony_id WHERE tm.receipt_id=tr.receipt_id AND t.recipe_receipt_id=i.recipe_receipt_id),"
        "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt_member tm JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony t ON t.testimony_id=tm.testimony_id JOIN " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt_member lm ON lm.receipt_id=lr.receipt_id AND lm.node_id=t.evidence_node_id JOIN " LAPLACE_PG_SCHEMA ".evidence_node n ON n.node_id=t.evidence_node_id JOIN " LAPLACE_PG_SCHEMA ".composition_execution_occurrence_member cm ON cm.working_set_receipt=ce.working_set_receipt AND cm.occurrence_id=n.occurrence_id WHERE tm.receipt_id=tr.receipt_id),"
        "p.closure_subject_count,p.closure_subject_count,p.reconstruction_class,0::integer "
        "FROM input i "
        "JOIN " LAPLACE_PG_SCHEMA ".source_profile p ON p.profile_id=i.source_profile_id "
        "JOIN " LAPLACE_PG_SCHEMA ".source_profile_receipt spr ON spr.receipt_id=i.source_profile_receipt_id AND spr.selected_boundary_fingerprint=p.selected_boundary_fingerprint "
        "JOIN " LAPLACE_PG_SCHEMA ".source_profile_receipt_member sprm ON sprm.receipt_id=spr.receipt_id AND sprm.profile_id=p.profile_id "
        "JOIN " LAPLACE_PG_SCHEMA ".composition_execution_receipt ce ON ce.working_set_receipt=i.composition_working_set_receipt_id "
        "JOIN " LAPLACE_PG_SCHEMA ".canonical_deposit_receipt cd ON cd.receipt_id=ce.staged_stream_receipt AND cd.recipe_fingerprint=i.recipe_receipt_id "
        "JOIN " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt lr ON lr.receipt_id=i.evidence_lineage_receipt_id "
        "JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt tr ON tr.receipt_id=i.evidence_testimony_receipt_id AND tr.source_profile_id=p.profile_id "
        "WHERE p.recipe_program_fingerprint=i.recipe_receipt_id "
        "AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".source_profile_receipt_member m WHERE m.receipt_id=spr.receipt_id)=spr.profile_count "
        "AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt_member m WHERE m.receipt_id=lr.receipt_id)=lr.node_count "
        "AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt_member m WHERE m.receipt_id=tr.receipt_id)=tr.testimony_count "
        "AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".composition_execution_occurrence_member m WHERE m.working_set_receipt=ce.working_set_receipt)=ce.occurrence_count "
        "ORDER BY i.ordinality";
    Oid types[1] = {request_array_type};
    Datum values[1] = {request_array};
    laplace_world_admission_record* records =
        (laplace_world_admission_record*)palloc0(
            sizeof(*records) * request_count);
    size_t index;
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace world-admission derivation could not connect to SPI")));
    }
    result = SPI_execute_with_args(
        derive_sql, 1, types, values, NULL, true, 0);
    if (result != SPI_OK_SELECT || SPI_tuptable == NULL ||
        SPI_processed != (uint64)request_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace world-admission requests do not resolve to one exact durable component set")));
    }
    for (index = 0u; index < request_count; ++index) {
        HeapTuple tuple = SPI_tuptable->vals[index];
        TupleDesc descriptor = SPI_tuptable->tupdesc;
        const uint64_t ordinal = tuple_u64(
            tuple, descriptor, 1, "request ordinal");
        laplace_digest256 expected_id;
        if (ordinal != index + 1u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace world-admission durable derivation reordered requests")));
        }
        tuple_digest(tuple, descriptor, 2, &records[index].source_profile_id, "source profile");
        tuple_digest(tuple, descriptor, 3, &records[index].selected_boundary_fingerprint, "selected boundary");
        tuple_digest(tuple, descriptor, 4, &records[index].source_profile_receipt_id, "source-profile receipt");
        tuple_digest(tuple, descriptor, 5, &records[index].recipe_receipt_id, "recipe receipt");
        tuple_digest(tuple, descriptor, 6, &records[index].composition_working_set_receipt_id, "composition working-set receipt");
        tuple_digest(tuple, descriptor, 7, &records[index].composition_presence_receipt_id, "composition presence receipt");
        tuple_digest(tuple, descriptor, 8, &records[index].composition_producer_receipt_id, "composition producer receipt");
        tuple_digest(tuple, descriptor, 9, &records[index].composition_stream_receipt_id, "composition stream receipt");
        tuple_digest(tuple, descriptor, 10, &records[index].evidence_lineage_receipt_id, "evidence lineage receipt");
        tuple_digest(tuple, descriptor, 11, &records[index].evidence_testimony_receipt_id, "evidence testimony receipt");
        records[index].profile_occurrence_count = tuple_u64(tuple, descriptor, 12, "profile occurrence count");
        records[index].composition_occurrence_count = tuple_u64(tuple, descriptor, 13, "composition occurrence count");
        records[index].profile_claim_count = tuple_u64(tuple, descriptor, 14, "profile claim count");
        records[index].evidence_node_count = tuple_u64(tuple, descriptor, 15, "evidence node count");
        records[index].testimony_count = tuple_u64(tuple, descriptor, 16, "testimony count");
        records[index].profile_bound_testimony_count = tuple_u64(tuple, descriptor, 17, "profile-bound testimony count");
        records[index].recipe_bound_testimony_count = tuple_u64(tuple, descriptor, 18, "recipe-bound testimony count");
        records[index].lineage_bound_testimony_count = tuple_u64(tuple, descriptor, 19, "lineage-bound testimony count");
        records[index].closure_subject_count = tuple_u64(tuple, descriptor, 20, "closure subject count");
        records[index].closed_subject_count = tuple_u64(tuple, descriptor, 21, "closed subject count");
        records[index].reconstruction_class = tuple_u32(tuple, descriptor, 22, "reconstruction class");
        records[index].flags = tuple_u32(tuple, descriptor, 23, "flags");
        records[index].readback_fingerprint = readback_fingerprint(&records[index]);
        if (laplace_world_admission_identify(
                &records[index], &expected_id) != LAPLACE_WORLD_ADMISSION_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace durable world-admission components do not close"),
                     errdetail("admission ordinal=%llu",
                               (unsigned long long)ordinal)));
        }
        records[index].admission_id = expected_id;
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace world-admission derivation could not close SPI")));
    }
    return records;
}

static ArrayType* admission_record_array(
    const laplace_world_admission_record* records,
    size_t record_count) {
    Oid attribute_types[24];
    int32 attribute_typmods[24];
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * record_count);
    size_t index;
    int attribute;
    for (attribute = 0; attribute < 24; ++attribute) {
        attribute_types[attribute] = attribute < 12 ? BYTEAOID :
            attribute < 22 ? NUMERICOID : INT4OID;
        attribute_typmods[attribute] = attribute < 12 || attribute >= 22 ?
            LAPLACE_PG_TYPMOD_NONE : LAPLACE_PG_NUMERIC_TYPMOD(20, 0);
    }
    laplace_pg_composite_binding_open(
        "world_admission_record", attribute_types, attribute_typmods,
        24, &binding);
    for (index = 0u; index < record_count; ++index) {
        const laplace_world_admission_record* record = &records[index];
        const laplace_digest256* digests[] = {
            &record->admission_id,
            &record->source_profile_id,
            &record->selected_boundary_fingerprint,
            &record->source_profile_receipt_id,
            &record->recipe_receipt_id,
            &record->composition_working_set_receipt_id,
            &record->composition_presence_receipt_id,
            &record->composition_producer_receipt_id,
            &record->composition_stream_receipt_id,
            &record->evidence_lineage_receipt_id,
            &record->evidence_testimony_receipt_id,
            &record->readback_fingerprint};
        const uint64_t counts[] = {
            record->profile_occurrence_count,
            record->composition_occurrence_count,
            record->profile_claim_count,
            record->evidence_node_count,
            record->testimony_count,
            record->profile_bound_testimony_count,
            record->recipe_bound_testimony_count,
            record->lineage_bound_testimony_count,
            record->closure_subject_count,
            record->closed_subject_count};
        Datum values[24];
        bool nulls[24] = {false};
        size_t field;
        for (field = 0u; field < 12u; ++field) {
            values[field] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                digests[field]->bytes, 32u));
        }
        for (field = 0u; field < 10u; ++field) {
            values[12u + field] = laplace_pg_numeric_from_uint64(counts[field]);
        }
        values[22] = Int32GetDatum((int32)record->reconstruction_class);
        values[23] = Int32GetDatum((int32)record->flags);
        rows[index] = laplace_pg_composite_record(&binding, values, nulls);
    }
    {
        ArrayType* result = laplace_pg_composite_array(
            &binding, rows, (uint64_t)record_count);
        laplace_pg_composite_binding_close(&binding);
        return result;
    }
}

static void persist_admissions(
    ArrayType* record_array,
    const laplace_world_admission_receipt* receipt,
    const laplace_isa_receipt* isa_receipt) {
    static const char records_sql[] =
        "WITH input AS (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".world_admission_record[]) r),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".world_admission SELECT * FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT NOT EXISTS (SELECT FROM input i WHERE NOT EXISTS (SELECT FROM written w WHERE w.admission_id=i.admission_id AND ROW(w.*) IS NOT DISTINCT FROM ROW(i.*)) AND NOT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".world_admission a WHERE a.admission_id=i.admission_id AND ROW(a.*) IS NOT DISTINCT FROM ROW(i.*)))";
    static const char receipt_sql[] =
        "WITH written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".world_admission_receipt(receipt_id,selected_boundary_fingerprint,input_fingerprint,output_fingerprint,isa_receipt_id,admission_count,occurrence_count,claim_count,evidence_node_count,testimony_count,closure_subject_count,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12) ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT EXISTS (SELECT FROM written WHERE receipt_id=$1 AND selected_boundary_fingerprint=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND admission_count=$6 AND occurrence_count=$7 AND claim_count=$8 AND evidence_node_count=$9 AND testimony_count=$10 AND closure_subject_count=$11 AND version=$12) OR EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".world_admission_receipt WHERE receipt_id=$1 AND selected_boundary_fingerprint=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND admission_count=$6 AND occurrence_count=$7 AND claim_count=$8 AND evidence_node_count=$9 AND testimony_count=$10 AND closure_subject_count=$11 AND version=$12)";
    static const char members_sql[] =
        "WITH input AS (SELECT $1::bytea AS receipt_id,(r).admission_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".world_admission_record[]) WITH ORDINALITY r),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".world_admission_receipt_member(receipt_id,admission_id,member_ordinal) SELECT receipt_id,admission_id,member_ordinal FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT (SELECT count(*) FROM written)+(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".world_admission_receipt_member m JOIN input i ON i.receipt_id=m.receipt_id AND i.admission_id=m.admission_id AND i.member_ordinal=m.member_ordinal WHERE m.receipt_id=$1)=(SELECT count(*) FROM input) AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".world_admission_receipt_member m WHERE m.receipt_id=$1)=(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".world_admission_receipt_member m JOIN input i ON i.receipt_id=m.receipt_id AND i.admission_id=m.admission_id AND i.member_ordinal=m.member_ordinal WHERE m.receipt_id=$1)";
    const Oid array_type = get_array_type(ARR_ELEMTYPE(record_array));
    Oid record_types[1] = {array_type};
    Datum record_values[1] = {PointerGetDatum(record_array)};
    Oid receipt_types[12] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT4OID};
    Datum receipt_values[12];
    Oid member_types[2] = {BYTEAOID, array_type};
    Datum member_values[2];
    int result;
    receipt_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->receipt_id.bytes, 32u));
    receipt_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->selected_boundary_fingerprint.bytes, 32u));
    receipt_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->input_fingerprint.bytes, 32u));
    receipt_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->output_fingerprint.bytes, 32u));
    receipt_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(isa_receipt->receipt_id.bytes, 32u));
    receipt_values[5] = Int64GetDatum(laplace_pg_checked_int64(receipt->admission_count, "world admission count"));
    receipt_values[6] = Int64GetDatum(laplace_pg_checked_int64(receipt->occurrence_count, "world occurrence count"));
    receipt_values[7] = Int64GetDatum(laplace_pg_checked_int64(receipt->claim_count, "world claim count"));
    receipt_values[8] = Int64GetDatum(laplace_pg_checked_int64(receipt->evidence_node_count, "world evidence-node count"));
    receipt_values[9] = Int64GetDatum(laplace_pg_checked_int64(receipt->testimony_count, "world testimony count"));
    receipt_values[10] = Int64GetDatum(laplace_pg_checked_int64(receipt->closure_subject_count, "world closure-subject count"));
    receipt_values[11] = Int32GetDatum((int32)receipt->version);
    member_values[0] = receipt_values[0];
    member_values[1] = PointerGetDatum(record_array);
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE), errmsg("Laplace world-admission persistence could not connect to SPI")));
    }
    result = SPI_execute_with_args(records_sql, 1, record_types, record_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace world-admission replay conflicts with durable state")));
    }
    result = SPI_execute_with_args(receipt_sql, 12, receipt_types, receipt_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace world-admission receipt identity collides with durable state")));
    }
    result = SPI_execute_with_args(members_sql, 2, member_types, member_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace world-admission receipt membership conflicts with durable state")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg("Laplace world-admission persistence could not close SPI")));
    }
}

static ArrayType* admission_ids(
    const laplace_world_admission_record* records,
    size_t record_count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * record_count);
    size_t index;
    for (index = 0u; index < record_count; ++index) {
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            records[index].admission_id.bytes, 32u));
    }
    return construct_array(
        values, (int)record_count, BYTEAOID, -1, false, TYPALIGN_INT);
}

Datum LAPLACE_PG_WORLD_ADMISSION_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* request_array = PG_GETARG_ARRAYTYPE_P(1);
    const Oid request_array_type = get_array_type(ARR_ELEMTYPE(request_array));
    const size_t record_count = validate_request_array(request_array);
    laplace_world_admission_record* records;
    ArrayType* record_array;
    laplace_world_admission_receipt semantic_receipt;
    laplace_world_admission_receipt output_receipt;
    laplace_world_admission_error semantic_error;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt isa_receipt;
    laplace_isa_error isa_error;
    Datum result_values[12];
    bool result_nulls[12] = {false};
    HeapTuple result_tuple;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    records = derive_durable_records(
        request_array_type, PointerGetDatum(request_array), record_count);
    record_array = admission_record_array(records, record_count);
    memset(&output_receipt, 0, sizeof(output_receipt));
    memset(values, 0, sizeof(values));
    values[0].data = records;
    values[0].count = (uint64_t)record_count;
    values[0].capacity = (uint64_t)record_count;
    values[0].stride_bytes = sizeof(*records);
    values[0].type = LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECORD_VECTOR;
    values[1].data = &output_receipt;
    values[1].capacity = 1u;
    values[1].stride_bytes = sizeof(output_receipt);
    values[1].type = LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECEIPT_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_WORLD_ADMISSION_CLOSE_BATCH;
    instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_WORLD_ADMISSION_CLOSE_BATCH;
    instruction.output_value = 1u;
    memset(&program, 0, sizeof(program));
    program.instructions = &instruction;
    program.values = values;
    program.context = &context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    memset(&isa_receipt, 0, sizeof(isa_receipt));
    memset(&isa_error, 0, sizeof(isa_error));
    if (laplace_isa_execute(&program, &isa_receipt, &isa_error) != LAPLACE_ISA_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace world-admission ISA execution failed"),
                 errdetail("status=%d instruction=%llu", (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }
    memset(&semantic_receipt, 0, sizeof(semantic_receipt));
    memset(&semantic_error, 0, sizeof(semantic_error));
    if (laplace_world_admission_close_batch(
            records, record_count, &semantic_receipt, &semantic_error) !=
            LAPLACE_WORLD_ADMISSION_OK ||
        memcmp(&semantic_receipt, &output_receipt, sizeof(semantic_receipt)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace world-admission semantic receipt reconstruction failed")));
    }
    laplace_pg_persist_execution_receipt(
        &isa_receipt, record_count, instruction.opcode);
    persist_admissions(record_array, &semantic_receipt, &isa_receipt);
    result_values[0] = PointerGetDatum(admission_ids(records, record_count));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(semantic_receipt.receipt_id.bytes, 32u));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(semantic_receipt.selected_boundary_fingerprint.bytes, 32u));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(semantic_receipt.input_fingerprint.bytes, 32u));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(semantic_receipt.output_fingerprint.bytes, 32u));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(isa_receipt.receipt_id.bytes, 32u));
    result_values[6] = laplace_pg_numeric_from_uint64(semantic_receipt.admission_count);
    result_values[7] = laplace_pg_numeric_from_uint64(semantic_receipt.occurrence_count);
    result_values[8] = laplace_pg_numeric_from_uint64(semantic_receipt.claim_count);
    result_values[9] = laplace_pg_numeric_from_uint64(semantic_receipt.evidence_node_count);
    result_values[10] = laplace_pg_numeric_from_uint64(semantic_receipt.testimony_count);
    result_values[11] = laplace_pg_numeric_from_uint64(semantic_receipt.closure_subject_count);
    result_tuple = laplace_pg_form_result_tuple(fcinfo, result_values, result_nulls, 12);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
