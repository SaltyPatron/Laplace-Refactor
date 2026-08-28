#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
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
#include "laplace/contract/evidence_lineage.h"
#include "laplace/contract/evidence_testimony.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/persistence.h"
#include "laplace/tabular_source.h"
#include "composition_pg.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"
#include "source_profile_pg.h"
#include "unicode_atoms_pg.h"

PG_FUNCTION_INFO_V1(LAPLACE_PG_SOURCE_ADMIT_TABULAR_SYMBOL);

typedef struct laplace_pg_source_stage_receipts {
    laplace_digest256 source_profile;
    laplace_digest256 source_profile_isa;
    laplace_digest256 evidence_lineage;
    laplace_digest256 evidence_lineage_isa;
    laplace_digest256 evidence_testimony;
    laplace_digest256 evidence_testimony_isa;
    laplace_digest256 world_admission_id;
    laplace_digest256 world_admission;
    laplace_digest256 world_admission_isa;
    uint64_t evidence_node_count;
    uint64_t testimony_count;
} laplace_pg_source_stage_receipts;

typedef struct laplace_pg_source_claim {
    laplace_evidence_lineage_record lineage;
    uint32_t outcome_type;
} laplace_pg_source_claim;

static Datum required_tuple_value(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    const char* field) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace source admission returned null %s", field)));
    }
    return value;
}

static void read_exact_bytes(
    Datum datum,
    uint8_t* output,
    size_t expected,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != expected) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace source-admission %s has invalid width", field),
                 errdetail("expected=%zu actual=%zu", expected,
                           (size_t)VARSIZE_ANY_EXHDR(value))));
    }
    memcpy(output, VARDATA_ANY(value), expected);
}

static uint32_t read_u32(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    const int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace %s cannot be negative", field)));
    }
    return (uint32_t)value;
}

static int claim_compare(const void* left, const void* right) {
    const laplace_pg_source_claim* a = (const laplace_pg_source_claim*)left;
    const laplace_pg_source_claim* b = (const laplace_pg_source_claim*)right;
    return memcmp(a->lineage.node_id.bytes, b->lineage.node_id.bytes, 32u);
}

static int testimony_compare(const void* left, const void* right) {
    const laplace_evidence_testimony_record* a =
        (const laplace_evidence_testimony_record*)left;
    const laplace_evidence_testimony_record* b =
        (const laplace_evidence_testimony_record*)right;
    return memcmp(a->testimony_id.bytes, b->testimony_id.bytes, 32u);
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 24u)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static laplace_digest256 finish_hash(blake3_hasher* hasher) {
    laplace_digest256 result;
    blake3_hasher_finalize(hasher, result.bytes, sizeof(result.bytes));
    return result;
}

static laplace_digest256 trust_input_id(
    const laplace_source_profile_manifest* profile) {
    static const char domain[] = "laplace-source-trust-input-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(
        &hasher, profile->authority_release_fingerprint.bytes, 32u);
    blake3_hasher_update(&hasher, profile->license_fingerprint.bytes, 32u);
    blake3_hasher_update(
        &hasher, profile->epistemic_witnessing_fingerprint.bytes, 32u);
    return finish_hash(&hasher);
}

static laplace_digest256 outcome_detail_id(
    const laplace_digest256* source,
    const laplace_digest256* node,
    uint32_t outcome_type) {
    static const char domain[] = "laplace-source-outcome-detail-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(&hasher, source->bytes, 32u);
    blake3_hasher_update(&hasher, node->bytes, 32u);
    hash_u32(&hasher, outcome_type);
    return finish_hash(&hasher);
}

static laplace_tabular_artifact* read_artifacts(
    ArrayType* array,
    size_t* artifact_count) {
    const Oid type_oid = laplace_pg_composite_type_oid("tabular_source_artifact");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_tabular_artifact* artifacts;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace tabular source input must be an exact one-dimensional artifact array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace tabular source requires at least one artifact")));
    }
    artifacts = (laplace_tabular_artifact*)palloc0(
        sizeof(*artifacts) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        bytea* content;
        bytea* name;
        char* exact_name;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace tabular artifact array cannot contain nulls")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 1, "artifact_id"),
            &artifacts[index].artifact_id, "tabular artifact_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 2, "parent_artifact_id"),
            &artifacts[index].parent_artifact_id,
            "tabular parent_artifact_id");
        read_exact_bytes(
            laplace_pg_required_composite_attribute(tuple, 3, "expected_sha256"),
            artifacts[index].expected_sha256,
            sizeof(artifacts[index].expected_sha256), "expected SHA-256");
        content = DatumGetByteaPP(laplace_pg_required_composite_attribute(
            tuple, 4, "artifact content"));
        name = DatumGetByteaPP(laplace_pg_required_composite_attribute(
            tuple, 5, "artifact name"));
        artifacts[index].bytes = (const uint8_t*)VARDATA_ANY(content);
        artifacts[index].byte_count = (uint64_t)VARSIZE_ANY_EXHDR(content);
        artifacts[index].name_byte_count = (uint64_t)VARSIZE_ANY_EXHDR(name);
        if (artifacts[index].name_byte_count == 0u ||
            artifacts[index].name_byte_count > SIZE_MAX - 1u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace tabular artifact name is invalid")));
        }
        exact_name = (char*)palloc((size_t)artifacts[index].name_byte_count + 1u);
        memcpy(exact_name, VARDATA_ANY(name),
               (size_t)artifacts[index].name_byte_count);
        exact_name[artifacts[index].name_byte_count] = '\0';
        artifacts[index].name = exact_name;
        artifacts[index].expected_record_count = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 6, "expected_record_count"),
            "tabular expected_record_count");
        artifacts[index].expected_field_count = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 7, "expected_field_count"),
            "tabular expected_field_count");
        artifacts[index].reference_column_mask = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 8, "reference_column_mask"),
            "tabular reference_column_mask");
        artifacts[index].mode = read_u32(tuple, 9, "tabular mode");
        artifacts[index].delimiter = read_u32(tuple, 10, "tabular delimiter");
        artifacts[index].line_terminator = read_u32(
            tuple, 11, "tabular line_terminator");
        artifacts[index].expected_column_count = read_u32(
            tuple, 12, "tabular expected_column_count");
        artifacts[index].outcome_type = read_u32(
            tuple, 13, "tabular outcome_type");
        artifacts[index].flags = read_u32(tuple, 14, "tabular flags");
    }
    *artifact_count = (size_t)count;
    return artifacts;
}

static void source_profile_binding_open(laplace_pg_composite_binding* binding) {
    Oid types[51];
    int32 typmods[51];
    int index;
    for (index = 0; index < 51; ++index) {
        types[index] = BYTEAOID;
        typmods[index] = LAPLACE_PG_TYPMOD_NONE;
    }
    types[1] = INT4OID;
    types[6] = NUMERICOID;
    typmods[6] = LAPLACE_PG_NUMERIC_TYPMOD(20, 0);
    for (index = 19; index <= 48; ++index) {
        types[index] = NUMERICOID;
        typmods[index] = LAPLACE_PG_NUMERIC_TYPMOD(20, 0);
    }
    types[49] = INT4OID;
    types[50] = INT4OID;
    laplace_pg_composite_binding_open(
        "source_profile_manifest", types, typmods, 51, binding);
}

static Datum source_profile_record(
    const laplace_pg_composite_binding* binding,
    const laplace_source_profile_manifest* profile) {
    Datum fields[51];
    bool nulls[51] = {false};
    const laplace_digest256* fingerprints[12] = {
        &profile->authority_release_fingerprint,
        &profile->license_fingerprint,
        &profile->artifact_graph_fingerprint,
        &profile->syntax_authority_fingerprint,
        &profile->recipe_program_fingerprint,
        &profile->universal_ast_mapping_fingerprint,
        &profile->highway_references_fingerprint,
        &profile->epistemic_witnessing_fingerprint,
        &profile->denominator_declaration_fingerprint,
        &profile->conformance_fingerprint,
        &profile->completion_law_fingerprint,
        &profile->selected_boundary_fingerprint};
    const uint64_t counts[30] = {
        profile->byte_count, profile->container_count, profile->member_count,
        profile->file_count, profile->record_count, profile->field_count,
        profile->syntax_node_count, profile->span_count, profile->edge_count,
        profile->reference_count, profile->occurrence_count,
        profile->claim_count, profile->mapping_count, profile->error_count,
        profile->unknown_count, profile->transformation_count,
        profile->output_count, profile->closure_subject_count,
        profile->accepted_count, profile->rejected_count,
        profile->duplicate_count, profile->reused_count,
        profile->transformed_count, profile->lossy_count,
        profile->unsupported_count, profile->malformed_count,
        profile->unresolved_count, profile->persisted_count,
        profile->derived_count, profile->not_applicable_mask};
    size_t index;
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->profile_id.bytes, 32u));
    fields[1] = Int32GetDatum((int32)profile->coordinate.kind);
    fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.authority.bytes, 16u));
    fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.release.bytes, 16u));
    fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.name_space.bytes, 16u));
    fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.local_identifier.bytes, 16u));
    fields[6] = laplace_pg_numeric_from_uint64(profile->coordinate.version);
    for (index = 0u; index < 12u; ++index) {
        fields[7u + index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            fingerprints[index]->bytes, 32u));
    }
    for (index = 0u; index < 30u; ++index) {
        fields[19u + index] = laplace_pg_numeric_from_uint64(counts[index]);
    }
    fields[49] = Int32GetDatum((int32)profile->reconstruction_class);
    fields[50] = Int32GetDatum((int32)profile->flags);
    return laplace_pg_composite_record(binding, fields, nulls);
}

static ArrayType* profile_array(
    const laplace_source_profile_manifest* profile,
    Oid* array_oid) {
    laplace_pg_composite_binding binding;
    Datum value;
    ArrayType* result;
    source_profile_binding_open(&binding);
    value = source_profile_record(&binding, profile);
    result = laplace_pg_composite_array(&binding, &value, 1u);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* lineage_array(
    const laplace_pg_source_claim* claims,
    size_t claim_count,
    Oid* array_oid) {
    static const Oid types[11] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, INT4OID, INT4OID, INT4OID, INT4OID};
    static const int32 typmods[11] = {
        -1, -1, -1, -1, -1, -1, LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * claim_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "evidence_lineage_record", types, typmods, 11, &binding);
    for (index = 0u; index < claim_count; ++index) {
        const laplace_evidence_lineage_record* record = &claims[index].lineage;
        Datum fields[11];
        bool nulls[11] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->node_id.bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->proposition_id.bytes, 16u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->occurrence_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->source_id.bytes, 32u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->context_id.bytes, 32u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->parent_node_id.bytes, 32u));
        fields[6] = laplace_pg_numeric_from_uint64(record->source_ordinal);
        fields[7] = Int32GetDatum((int32)record->record_kind);
        fields[8] = Int32GetDatum((int32)record->epistemic_kind);
        fields[9] = Int32GetDatum((int32)record->flags);
        fields[10] = Int32GetDatum((int32)record->reserved);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(&binding, rows, (uint64_t)claim_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* testimony_array(
    const laplace_evidence_testimony_record* records,
    size_t record_count,
    Oid* array_oid) {
    static const Oid types[13] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, NUMERICOID,
        INT4OID, INT4OID, INT4OID, INT4OID};
    static const int32 typmods[13] = {
        -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * record_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "evidence_testimony_record", types, typmods, 13, &binding);
    for (index = 0u; index < record_count; ++index) {
        const laplace_evidence_testimony_record* record = &records[index];
        Datum fields[13];
        bool nulls[13] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->testimony_id.bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->evidence_node_id.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->source_profile_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->recipe_receipt_id.bytes, 32u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->trust_input_id.bytes, 32u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->outcome_detail_id.bytes, 32u));
        fields[6] = laplace_pg_numeric_from_uint64(record->uncertainty_numerator);
        fields[7] = laplace_pg_numeric_from_uint64(record->uncertainty_denominator);
        fields[8] = laplace_pg_numeric_from_uint64(record->sample_count);
        fields[9] = Int32GetDatum((int32)record->source_type);
        fields[10] = Int32GetDatum((int32)record->outcome_type);
        fields[11] = Int32GetDatum((int32)record->disposition);
        fields[12] = Int32GetDatum((int32)record->flags);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(&binding, rows, (uint64_t)record_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* world_request_array(
    const laplace_source_profile_manifest* profile,
    const laplace_pg_composition_execution* execution,
    const laplace_pg_source_stage_receipts* receipts,
    Oid* array_oid) {
    static const Oid types[6] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID};
    static const int32 typmods[6] = {-1, -1, -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum fields[6];
    bool nulls[6] = {false};
    Datum row;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "world_admission_request", types, typmods, 6, &binding);
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile->profile_id.bytes, 32u));
    fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts->source_profile.bytes, 32u));
    fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile->recipe_program_fingerprint.bytes, 32u));
    fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution->summary.receipt_id.bytes, 32u));
    fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts->evidence_lineage.bytes, 32u));
    fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts->evidence_testimony.bytes, 32u));
    row = laplace_pg_composite_record(&binding, fields, nulls);
    result = laplace_pg_composite_array(&binding, &row, 1u);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static void require_spi_result(int result, int columns, const char* stage) {
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        SPI_tuptable == NULL || SPI_tuptable->tupdesc->natts != columns) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace %s did not return one exact receipt", stage)));
    }
}

static void call_source_profile(
    Datum context,
    ArrayType* profiles,
    Oid profile_array_oid,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).source_profile_receipt_id,(r).isa_receipt_id "
        "FROM (SELECT " LAPLACE_PG_SCHEMA "." LAPLACE_PG_SOURCE_PROFILE_VALIDATE_SQL
        "($1,$2) AS r) q";
    Oid types[2] = {
        laplace_pg_composite_type_oid("execution_context"), profile_array_oid};
    Datum values[2] = {context, PointerGetDatum(profiles)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace source-profile stage could not connect")));
    }
    result = SPI_execute_with_args(sql, 2, types, values, NULL, false, 0);
    require_spi_result(result, 2, "source-profile stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "source-profile receipt"),
        &receipts->source_profile, "source-profile receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "source-profile ISA receipt"),
        &receipts->source_profile_isa, "source-profile ISA receipt");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace source-profile stage could not close")));
    }
}

static void call_lineage(
    Datum context,
    ArrayType* lineage,
    Oid lineage_array_oid,
    uint64_t root_capacity,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).lineage_receipt_id,(r).isa_receipt_id,(r).node_count "
        "FROM (SELECT " LAPLACE_PG_SCHEMA "." LAPLACE_PG_EVIDENCE_RECORD_SQL
        "($1,$2,$3) AS r) q";
    Oid types[3] = {
        laplace_pg_composite_type_oid("execution_context"),
        lineage_array_oid, NUMERICOID};
    Datum values[3] = {
        context, PointerGetDatum(lineage),
        laplace_pg_numeric_from_uint64(root_capacity)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace evidence-lineage stage could not connect")));
    }
    result = SPI_execute_with_args(sql, 3, types, values, NULL, false, 0);
    require_spi_result(result, 3, "evidence-lineage stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "lineage receipt"),
        &receipts->evidence_lineage, "lineage receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "lineage ISA receipt"),
        &receipts->evidence_lineage_isa, "lineage ISA receipt");
    receipts->evidence_node_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                             "lineage node count"),
        "lineage node count");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace evidence-lineage stage could not close")));
    }
}

static void call_testimony(
    Datum context,
    ArrayType* testimony,
    Oid testimony_array_oid,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).testimony_receipt_id,(r).isa_receipt_id,(r).testimony_count "
        "FROM (SELECT " LAPLACE_PG_SCHEMA "." LAPLACE_PG_EVIDENCE_TESTIMONY_SQL
        "($1,$2) AS r) q";
    Oid types[2] = {
        laplace_pg_composite_type_oid("execution_context"),
        testimony_array_oid};
    Datum values[2] = {context, PointerGetDatum(testimony)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace evidence-testimony stage could not connect")));
    }
    result = SPI_execute_with_args(sql, 2, types, values, NULL, false, 0);
    require_spi_result(result, 3, "evidence-testimony stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "testimony receipt"),
        &receipts->evidence_testimony, "testimony receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "testimony ISA receipt"),
        &receipts->evidence_testimony_isa, "testimony ISA receipt");
    receipts->testimony_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                             "testimony count"),
        "testimony count");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace evidence-testimony stage could not close")));
    }
}

static void call_world_admission(
    Datum context,
    ArrayType* request,
    Oid request_array_oid,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT ((r).admission_ids)[1],(r).world_admission_receipt_id,"
        "(r).isa_receipt_id FROM (SELECT " LAPLACE_PG_SCHEMA "."
        LAPLACE_PG_WORLD_ADMISSION_CLOSE_SQL "($1,$2) AS r) q";
    Oid types[2] = {
        laplace_pg_composite_type_oid("execution_context"), request_array_oid};
    Datum values[2] = {context, PointerGetDatum(request)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace world-admission stage could not connect")));
    }
    result = SPI_execute_with_args(sql, 2, types, values, NULL, false, 0);
    require_spi_result(result, 3, "world-admission stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "world admission id"),
        &receipts->world_admission_id, "world admission id");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "world-admission receipt"),
        &receipts->world_admission, "world-admission receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                             "world-admission ISA receipt"),
        &receipts->world_admission_isa, "world-admission ISA receipt");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace world-admission stage could not close")));
    }
}

static laplace_pg_source_claim* build_claims(
    const laplace_tabular_source_plan_view* plan,
    const laplace_pg_composition_execution* execution) {
    laplace_pg_source_claim* claims;
    size_t index;
    if (plan->claim_count == 0u || plan->claim_count > SIZE_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace source profile has no witnessed claims")));
    }
    claims = (laplace_pg_source_claim*)palloc0(
        sizeof(*claims) * (size_t)plan->claim_count);
    for (index = 0u; index < (size_t)plan->claim_count; ++index) {
        const uint64_t result_index = plan->claim_result_indexes[index];
        laplace_persistence_occurrence_record occurrence;
        if (result_index >= execution->result_count ||
            result_index >= plan->request_count ||
            plan->claim_source_ordinals[index] == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source claim does not bind a canonical occurrence")));
        }
        memset(&occurrence, 0, sizeof(occurrence));
        occurrence.entity_id = execution->results[result_index].entity_id;
        occurrence.physicality_id = execution->results[result_index].physicality_id;
        occurrence.source_fingerprint = plan->source_fingerprint;
        occurrence.context_fingerprint =
            plan->requests[result_index].occurrence_context_fingerprint;
        occurrence.source_ordinal = plan->claim_source_ordinals[index];
        occurrence.flags = LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY;
        if (laplace_persistence_occurrence_identify(
                &occurrence, &claims[index].lineage.occurrence_id) !=
            LAPLACE_PERSISTENCE_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source claim occurrence identity failed")));
        }
        claims[index].lineage.proposition_id =
            execution->results[result_index].entity_id;
        claims[index].lineage.source_id = plan->source_fingerprint;
        claims[index].lineage.context_id =
            plan->requests[result_index].occurrence_context_fingerprint;
        claims[index].lineage.source_ordinal =
            plan->claim_source_ordinals[index];
        claims[index].lineage.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
        claims[index].lineage.epistemic_kind =
            LAPLACE_EVIDENCE_KIND_TESTIMONY;
        claims[index].outcome_type = plan->claim_outcome_types[index];
        if (laplace_evidence_node_identify(
                &claims[index].lineage,
                &claims[index].lineage.node_id) !=
            LAPLACE_EVIDENCE_LINEAGE_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source claim evidence identity failed")));
        }
    }
    qsort(claims, (size_t)plan->claim_count, sizeof(*claims), claim_compare);
    return claims;
}

static laplace_evidence_testimony_record* build_testimony(
    const laplace_pg_source_claim* claims,
    size_t claim_count,
    const laplace_source_profile_manifest* profile,
    const laplace_digest256* source_fingerprint) {
    laplace_evidence_testimony_record* records =
        (laplace_evidence_testimony_record*)palloc0(
            sizeof(*records) * claim_count);
    const laplace_digest256 trust = trust_input_id(profile);
    size_t index;
    for (index = 0u; index < claim_count; ++index) {
        records[index].evidence_node_id = claims[index].lineage.node_id;
        records[index].source_profile_id = profile->profile_id;
        records[index].recipe_receipt_id = profile->recipe_program_fingerprint;
        records[index].trust_input_id = trust;
        records[index].outcome_detail_id = outcome_detail_id(
            source_fingerprint, &claims[index].lineage.node_id,
            claims[index].outcome_type);
        records[index].uncertainty_denominator = 1u;
        records[index].sample_count = 1u;
        records[index].source_type = LAPLACE_EVIDENCE_SOURCE_STANDARD;
        records[index].outcome_type = claims[index].outcome_type;
        records[index].disposition = LAPLACE_EVIDENCE_DISPOSITION_PERSISTED;
        if (laplace_evidence_testimony_identify(
                &records[index], &records[index].testimony_id) !=
            LAPLACE_EVIDENCE_TESTIMONY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source testimony identity failed")));
        }
    }
    qsort(records, claim_count, sizeof(*records), testimony_compare);
    return records;
}

Datum LAPLACE_PG_SOURCE_ADMIT_TABULAR_SYMBOL(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_source_profile_manifest profile_declaration;
    laplace_source_profile_manifest profile;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context;
    laplace_tabular_artifact* artifacts;
    size_t artifact_count = 0u;
    laplace_tabular_source_input source_input;
    laplace_tabular_source_plan* source_plan = NULL;
    laplace_tabular_source_plan_view plan;
    laplace_composition_known_entity* known = NULL;
    laplace_pg_active_unicode_root active_unicode;
    laplace_composition_working_set_input composition_input;
    laplace_pg_composition_execution execution;
    laplace_pg_source_claim* claims = NULL;
    laplace_evidence_testimony_record* testimonies = NULL;
    laplace_pg_source_stage_receipts receipts;
    ArrayType* profiles;
    ArrayType* lineage;
    ArrayType* testimony;
    ArrayType* world_request;
    Oid profile_array_oid;
    Oid lineage_array_oid;
    Oid testimony_array_oid;
    Oid world_request_array_oid;
    const laplace_composition_result* root;
    Datum result_values[28];
    bool result_nulls[28] = {false};
    HeapTuple result_tuple;
    const Datum context_datum = PG_GETARG_DATUM(0);
    const uint64_t preferred_batch_bytes = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(5), "source preferred_batch_bytes");
    laplace_tabular_source_status source_status;

    memset(&profile_declaration, 0, sizeof(profile_declaration));
    memset(&profile, 0, sizeof(profile));
    memset(&source_input, 0, sizeof(source_input));
    memset(&plan, 0, sizeof(plan));
    memset(&active_unicode, 0, sizeof(active_unicode));
    memset(&composition_input, 0, sizeof(composition_input));
    memset(&execution, 0, sizeof(execution));
    memset(&receipts, 0, sizeof(receipts));
    laplace_pg_read_execution_context(context_datum, &context);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("Laplace source admission requires a writable execution context")));
    }
    laplace_pg_read_source_profile(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &profile_declaration);
    laplace_pg_read_digest(
        PG_GETARG_DATUM(2), &geometry_epoch, "source geometry_epoch");
    laplace_pg_read_digest(
        PG_GETARG_DATUM(3), &occurrence_context,
        "source occurrence_context_fingerprint");
    artifacts = read_artifacts(PG_GETARG_ARRAYTYPE_P(4), &artifact_count);
    source_input.profile_declaration = profile_declaration;
    source_input.geometry_epoch = geometry_epoch;
    source_input.occurrence_context_fingerprint = occurrence_context;
    source_input.artifacts = artifacts;
    source_input.artifact_count = (uint64_t)artifact_count;
    source_input.preferred_batch_bytes = preferred_batch_bytes;
    source_status = laplace_tabular_source_plan_create(
        &source_input, &source_plan);
    if (source_status != LAPLACE_TABULAR_SOURCE_OK || source_plan == NULL ||
        laplace_tabular_source_plan_view_get(source_plan, &plan) !=
            LAPLACE_TABULAR_SOURCE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace tabular source compilation failed"),
                 errdetail("status=%d", (int)source_status)));
    }
    if (plan.atom_count == 0u || plan.atom_count > SIZE_MAX ||
        plan.request_count == 0u || plan.claim_count == 0u ||
        plan.root_result_index >= plan.request_count) {
        laplace_tabular_source_plan_destroy(&source_plan);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace tabular source plan is incomplete")));
    }

    PG_TRY();
    {
        known = (laplace_composition_known_entity*)palloc0(
            sizeof(*known) * (size_t)plan.atom_count);
        laplace_pg_resolve_active_unicode_atoms(
            &context, plan.atom_positions, (size_t)plan.atom_count,
            known, &active_unicode);
        composition_input.context = &context;
        composition_input.source_fingerprint = &plan.source_fingerprint;
        composition_input.calculation_recipe_fingerprint =
            &plan.profile.recipe_program_fingerprint;
        composition_input.known_entities = known;
        composition_input.known_entity_count = plan.atom_count;
        composition_input.operands = plan.operands;
        composition_input.operand_count = plan.operand_count;
        composition_input.requests = plan.requests;
        composition_input.request_count = plan.request_count;
        composition_input.preferred_batch_bytes = preferred_batch_bytes;
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(&composition_input, &execution);
        laplace_pg_persist_composition_execution_receipt(
            &execution, &composition_input);
        if (laplace_tabular_source_profile_finalize(
                source_plan, &execution.summary, &profile) !=
            LAPLACE_TABULAR_SOURCE_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace tabular source profile did not close over deposition")));
        }
        profiles = profile_array(&profile, &profile_array_oid);
        call_source_profile(
            context_datum, profiles, profile_array_oid, &receipts);
        claims = build_claims(&plan, &execution);
        lineage = lineage_array(
            claims, (size_t)plan.claim_count, &lineage_array_oid);
        call_lineage(
            context_datum, lineage, lineage_array_oid,
            plan.claim_count, &receipts);
        testimonies = build_testimony(
            claims, (size_t)plan.claim_count, &profile,
            &plan.source_fingerprint);
        testimony = testimony_array(
            testimonies, (size_t)plan.claim_count, &testimony_array_oid);
        call_testimony(
            context_datum, testimony, testimony_array_oid, &receipts);
        world_request = world_request_array(
            &profile, &execution, &receipts, &world_request_array_oid);
        call_world_admission(
            context_datum, world_request, world_request_array_oid, &receipts);
        if (receipts.evidence_node_count != plan.claim_count ||
            receipts.testimony_count != plan.claim_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source evidence closure changed claim cardinality")));
        }
        root = &execution.results[plan.root_result_index];
        result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile.profile_id.bytes, 32u));
        result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.source_profile.bytes, 32u));
        result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.source_profile_isa.bytes, 32u));
        result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(plan.source_fingerprint.bytes, 32u));
        result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(plan.reconstruction_fingerprint.bytes, 32u));
        result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(active_unicode.root_receipt.bytes, 32u));
        result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.summary.receipt_id.bytes, 32u));
        result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.presence.semantic_receipt_id.bytes, 32u));
        result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.persistence.producer.receipt_id.bytes, 32u));
        result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.persistence.producer.stream.receipt_id.bytes, 32u));
        result_values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_lineage.bytes, 32u));
        result_values[11] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_lineage_isa.bytes, 32u));
        result_values[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_testimony.bytes, 32u));
        result_values[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_testimony_isa.bytes, 32u));
        result_values[14] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.world_admission_id.bytes, 32u));
        result_values[15] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.world_admission.bytes, 32u));
        result_values[16] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.world_admission_isa.bytes, 32u));
        result_values[17] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->entity_id.bytes, 16u));
        result_values[18] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->physicality_id.bytes, 32u));
        result_values[19] = laplace_pg_numeric_from_uint64(plan.artifact_count);
        result_values[20] = laplace_pg_numeric_from_uint64(plan.claim_count);
        result_values[21] = laplace_pg_numeric_from_uint64(plan.request_count);
        result_values[22] = laplace_pg_numeric_from_uint64(execution.summary.occurrence_count);
        result_values[23] = laplace_pg_numeric_from_uint64(execution.summary.logical_occurrence_count);
        result_values[24] = laplace_pg_numeric_from_uint64(receipts.evidence_node_count);
        result_values[25] = laplace_pg_numeric_from_uint64(receipts.testimony_count);
        result_values[26] = laplace_pg_numeric_from_uint64(execution.summary.stream_record_count);
        result_values[27] = Int32GetDatum((int32)LAPLACE_TABULAR_SOURCE_OK);
        result_tuple = laplace_pg_form_result_tuple(
            fcinfo, result_values, result_nulls, 28);
    }
    PG_CATCH();
    {
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
        laplace_tabular_source_plan_destroy(&source_plan);
        PG_RE_THROW();
    }
    PG_END_TRY();
    LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
    laplace_tabular_source_plan_destroy(&source_plan);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
