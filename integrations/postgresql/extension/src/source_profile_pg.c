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

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/isa.h"
#include "laplace/source_profile.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"
#include "source_profile_pg.h"

#ifndef LAPLACE_PG_SOURCE_PROFILE_ENTRYPOINT
#define LAPLACE_PG_SOURCE_PROFILE_ENTRYPOINT \
    LAPLACE_PG_SOURCE_PROFILE_VALIDATE_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_SOURCE_PROFILE_ENTRYPOINT);

static void read_id128(Datum datum, laplace_id128* id, const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(id->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace %s must contain exactly 16 bytes", field)));
    }
    memcpy(id->bytes, VARDATA_ANY(value), sizeof(id->bytes));
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

static uint64_t read_u64(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static void read_digest(
    HeapTupleHeader tuple,
    int attribute,
    laplace_digest256* digest,
    const char* field) {
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        digest, field);
}

void laplace_pg_read_source_profile(
    HeapTupleHeader tuple,
    laplace_source_profile_manifest* profile) {
    read_digest(tuple, 1, &profile->profile_id, "source-profile profile_id");
    profile->coordinate.kind = read_u32(tuple, 2, "source-profile coordinate_kind");
    read_id128(
        laplace_pg_required_composite_attribute(
            tuple, 3, "source-profile coordinate_authority"),
        &profile->coordinate.authority, "source-profile coordinate_authority");
    read_id128(
        laplace_pg_required_composite_attribute(
            tuple, 4, "source-profile coordinate_release"),
        &profile->coordinate.release, "source-profile coordinate_release");
    read_id128(
        laplace_pg_required_composite_attribute(
            tuple, 5, "source-profile coordinate_namespace"),
        &profile->coordinate.name_space, "source-profile coordinate_namespace");
    read_id128(
        laplace_pg_required_composite_attribute(
            tuple, 6, "source-profile coordinate_local_identifier"),
        &profile->coordinate.local_identifier,
        "source-profile coordinate_local_identifier");
    profile->coordinate.version = read_u64(
        tuple, 7, "source-profile coordinate_version");
    read_digest(tuple, 8, &profile->authority_release_fingerprint,
                "source-profile authority_release_fingerprint");
    read_digest(tuple, 9, &profile->license_fingerprint,
                "source-profile license_fingerprint");
    read_digest(tuple, 10, &profile->artifact_graph_fingerprint,
                "source-profile artifact_graph_fingerprint");
    read_digest(tuple, 11, &profile->syntax_authority_fingerprint,
                "source-profile syntax_authority_fingerprint");
    read_digest(tuple, 12, &profile->recipe_program_fingerprint,
                "source-profile recipe_program_fingerprint");
    read_digest(tuple, 13, &profile->universal_ast_mapping_fingerprint,
                "source-profile universal_ast_mapping_fingerprint");
    read_digest(tuple, 14, &profile->highway_references_fingerprint,
                "source-profile highway_references_fingerprint");
    read_digest(tuple, 15, &profile->epistemic_witnessing_fingerprint,
                "source-profile epistemic_witnessing_fingerprint");
    read_digest(tuple, 16, &profile->denominator_declaration_fingerprint,
                "source-profile denominator_declaration_fingerprint");
    read_digest(tuple, 17, &profile->conformance_fingerprint,
                "source-profile conformance_fingerprint");
    read_digest(tuple, 18, &profile->completion_law_fingerprint,
                "source-profile completion_law_fingerprint");
    read_digest(tuple, 19, &profile->selected_boundary_fingerprint,
                "source-profile selected_boundary_fingerprint");
    profile->byte_count = read_u64(tuple, 20, "source-profile byte_count");
    profile->container_count = read_u64(tuple, 21, "source-profile container_count");
    profile->member_count = read_u64(tuple, 22, "source-profile member_count");
    profile->file_count = read_u64(tuple, 23, "source-profile file_count");
    profile->record_count = read_u64(tuple, 24, "source-profile record_count");
    profile->field_count = read_u64(tuple, 25, "source-profile field_count");
    profile->syntax_node_count = read_u64(tuple, 26, "source-profile syntax_node_count");
    profile->span_count = read_u64(tuple, 27, "source-profile span_count");
    profile->edge_count = read_u64(tuple, 28, "source-profile edge_count");
    profile->reference_count = read_u64(tuple, 29, "source-profile reference_count");
    profile->occurrence_count = read_u64(tuple, 30, "source-profile occurrence_count");
    profile->claim_count = read_u64(tuple, 31, "source-profile claim_count");
    profile->mapping_count = read_u64(tuple, 32, "source-profile mapping_count");
    profile->error_count = read_u64(tuple, 33, "source-profile error_count");
    profile->unknown_count = read_u64(tuple, 34, "source-profile unknown_count");
    profile->transformation_count = read_u64(tuple, 35, "source-profile transformation_count");
    profile->output_count = read_u64(tuple, 36, "source-profile output_count");
    profile->closure_subject_count = read_u64(tuple, 37, "source-profile closure_subject_count");
    profile->accepted_count = read_u64(tuple, 38, "source-profile accepted_count");
    profile->rejected_count = read_u64(tuple, 39, "source-profile rejected_count");
    profile->duplicate_count = read_u64(tuple, 40, "source-profile duplicate_count");
    profile->reused_count = read_u64(tuple, 41, "source-profile reused_count");
    profile->transformed_count = read_u64(tuple, 42, "source-profile transformed_count");
    profile->lossy_count = read_u64(tuple, 43, "source-profile lossy_count");
    profile->unsupported_count = read_u64(tuple, 44, "source-profile unsupported_count");
    profile->malformed_count = read_u64(tuple, 45, "source-profile malformed_count");
    profile->unresolved_count = read_u64(tuple, 46, "source-profile unresolved_count");
    profile->persisted_count = read_u64(tuple, 47, "source-profile persisted_count");
    profile->derived_count = read_u64(tuple, 48, "source-profile derived_count");
    profile->not_applicable_mask = read_u64(tuple, 49, "source-profile not_applicable_mask");
    profile->reconstruction_class = read_u32(
        tuple, 50, "source-profile reconstruction_class");
    profile->flags = read_u32(tuple, 51, "source-profile flags");
}

static laplace_source_profile_manifest* read_profiles(
    ArrayType* array,
    size_t* profile_count) {
    const Oid type_oid = laplace_pg_composite_type_oid("source_profile_manifest");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_source_profile_manifest* profiles;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace source-profile input must be a one-dimensional exact source_profile_manifest array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace source-profile input cannot be empty")));
    }
    profiles = (laplace_source_profile_manifest*)palloc0(
        sizeof(*profiles) * (size_t)count);
    for (index = 0; index < count; ++index) {
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace source-profile input cannot contain null manifests")));
        }
        laplace_pg_read_source_profile(
            DatumGetHeapTupleHeader(values[index]), &profiles[index]);
    }
    *profile_count = (size_t)count;
    return profiles;
}

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

static ArrayType* profile_ids(
    const laplace_source_profile_manifest* profiles,
    size_t profile_count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * profile_count);
    size_t index;
    for (index = 0u; index < profile_count; ++index) {
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            profiles[index].profile_id.bytes, 32u));
    }
    return construct_array(
        values, (int)profile_count, BYTEAOID, -1, false, TYPALIGN_INT);
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

static ArrayType* profile_records_array(
    const laplace_source_profile_manifest* profiles,
    size_t profile_count,
    Oid* array_oid) {
    laplace_pg_composite_binding binding;
    Datum* rows;
    ArrayType* result;
    size_t index;
    if (profiles == NULL || profile_count == 0u || array_oid == NULL ||
        profile_count > (size_t)INT_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace source-profile native batch is invalid")));
    }
    rows = (Datum*)palloc(sizeof(*rows) * profile_count);
    source_profile_binding_open(&binding);
    for (index = 0u; index < profile_count; ++index) {
        rows[index] = source_profile_record(&binding, &profiles[index]);
    }
    result = laplace_pg_composite_array(
        &binding, rows, (uint64_t)profile_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static void persist_source_profiles(
    Oid input_array_type,
    Datum input_array,
    const laplace_source_profile_receipt* profile_receipt,
    const laplace_isa_receipt* isa_receipt) {
    static const char profiles_sql[] =
        "WITH input AS (SELECT (p).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".source_profile_manifest[]) p),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".source_profile SELECT * FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT NOT EXISTS (SELECT FROM input i WHERE "
        "NOT EXISTS (SELECT FROM written w WHERE w.profile_id=i.profile_id AND ROW(w.*) IS NOT DISTINCT FROM ROW(i.*)) AND "
        "NOT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".source_profile s WHERE s.profile_id=i.profile_id AND ROW(s.*) IS NOT DISTINCT FROM ROW(i.*)))";
    static const char receipt_sql[] =
        "WITH written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".source_profile_receipt(receipt_id,selected_boundary_fingerprint,input_fingerprint,output_fingerprint,isa_receipt_id,profile_count,closure_subject_count,persisted_count,negative_count,exact_reconstruction_count,semantic_reconstruction_count,no_reconstruction_count,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13) ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT EXISTS (SELECT FROM written WHERE receipt_id=$1 AND selected_boundary_fingerprint=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND profile_count=$6 AND closure_subject_count=$7 AND persisted_count=$8 AND negative_count=$9 AND exact_reconstruction_count=$10 AND semantic_reconstruction_count=$11 AND no_reconstruction_count=$12 AND version=$13) OR EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".source_profile_receipt WHERE receipt_id=$1 AND selected_boundary_fingerprint=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND profile_count=$6 AND closure_subject_count=$7 AND persisted_count=$8 AND negative_count=$9 AND exact_reconstruction_count=$10 AND semantic_reconstruction_count=$11 AND no_reconstruction_count=$12 AND version=$13)";
    static const char members_sql[] =
        "WITH input AS (SELECT $1::bytea AS receipt_id,(p).profile_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".source_profile_manifest[]) WITH ORDINALITY p),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".source_profile_receipt_member(receipt_id,profile_id,member_ordinal) SELECT receipt_id,profile_id,member_ordinal FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT (SELECT count(*) FROM written)+(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".source_profile_receipt_member m JOIN input i ON i.receipt_id=m.receipt_id AND i.profile_id=m.profile_id AND i.member_ordinal=m.member_ordinal WHERE m.receipt_id=$1)=(SELECT count(*) FROM input) AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".source_profile_receipt_member m WHERE m.receipt_id=$1)=(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".source_profile_receipt_member m JOIN input i ON i.receipt_id=m.receipt_id AND i.profile_id=m.profile_id AND i.member_ordinal=m.member_ordinal WHERE m.receipt_id=$1)";
    Oid profile_types[1] = {input_array_type};
    Datum profile_values[1] = {input_array};
    Oid receipt_types[13] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT8OID,
        INT4OID};
    Datum receipt_values[13];
    Oid member_types[2] = {BYTEAOID, input_array_type};
    Datum member_values[2];
    int result;
    receipt_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile_receipt->receipt_id.bytes, 32u));
    receipt_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile_receipt->selected_boundary_fingerprint.bytes, 32u));
    receipt_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile_receipt->input_fingerprint.bytes, 32u));
    receipt_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile_receipt->output_fingerprint.bytes, 32u));
    receipt_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt->receipt_id.bytes, 32u));
    receipt_values[5] = Int64GetDatum(laplace_pg_checked_int64(
        profile_receipt->profile_count, "source-profile count"));
    receipt_values[6] = Int64GetDatum(laplace_pg_checked_int64(
        profile_receipt->closure_subject_count, "source-profile closure subject count"));
    receipt_values[7] = Int64GetDatum(laplace_pg_checked_int64(
        profile_receipt->persisted_count, "source-profile persisted count"));
    receipt_values[8] = Int64GetDatum(laplace_pg_checked_int64(
        profile_receipt->negative_count, "source-profile negative count"));
    receipt_values[9] = Int64GetDatum(laplace_pg_checked_int64(
        profile_receipt->exact_reconstruction_count,
        "source-profile exact reconstruction count"));
    receipt_values[10] = Int64GetDatum(laplace_pg_checked_int64(
        profile_receipt->semantic_reconstruction_count,
        "source-profile semantic reconstruction count"));
    receipt_values[11] = Int64GetDatum(laplace_pg_checked_int64(
        profile_receipt->no_reconstruction_count,
        "source-profile no reconstruction count"));
    receipt_values[12] = Int32GetDatum((int32)profile_receipt->version);
    member_values[0] = receipt_values[0];
    member_values[1] = input_array;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace source-profile persistence could not connect to SPI")));
    }
    result = SPI_execute_with_args(
        profiles_sql, 1, profile_types, profile_values, NULL, false, 0);
#ifdef LAPLACE_TEST_SOURCE_PROFILE_REPLAY_VERIFY_BYPASS
    if (result != SPI_OK_SELECT) {
#else
    if (result != SPI_OK_SELECT || !query_boolean()) {
#endif
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace source-profile replay conflicts with durable state")));
    }
    result = SPI_execute_with_args(
        receipt_sql, 13, receipt_types, receipt_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace source-profile receipt identity collides with durable state")));
    }
    result = SPI_execute_with_args(
        members_sql, 2, member_types, member_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace source-profile receipt membership conflicts with durable state")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace source-profile persistence could not close SPI")));
    }
}

void laplace_pg_source_profile_execute_and_persist(
    const laplace_framework_context* context,
    const laplace_source_profile_manifest* profiles,
    size_t profile_count,
    laplace_pg_source_profile_execution* result) {
    laplace_source_profile_receipt semantic_receipt;
    laplace_source_profile_receipt output_receipt;
    laplace_source_profile_error semantic_error;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_error isa_error;
    ArrayType* durable_profiles;
    Oid durable_array_oid;

    if (context == NULL || profiles == NULL || profile_count == 0u ||
        result == NULL ||
        laplace_framework_context_validate(context) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace native source-profile execution input is invalid")));
    }

    memset(result, 0, sizeof(*result));
    memset(&output_receipt, 0, sizeof(output_receipt));
    memset(values, 0, sizeof(values));
    values[0].data = (void*)profiles;
    values[0].count = (uint64_t)profile_count;
    values[0].capacity = (uint64_t)profile_count;
    values[0].stride_bytes = sizeof(*profiles);
    values[0].type = LAPLACE_ISA_VALUE_SOURCE_PROFILE_MANIFEST_VECTOR;
    values[1].data = &output_receipt;
    values[1].capacity = 1u;
    values[1].stride_bytes = sizeof(output_receipt);
    values[1].type = LAPLACE_ISA_VALUE_SOURCE_PROFILE_RECEIPT_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_SOURCE_PROFILE_VALIDATE_BATCH;
    instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_SOURCE_PROFILE_VALIDATE_BATCH;
    instruction.output_value = 1u;
    memset(&program, 0, sizeof(program));
    program.instructions = &instruction;
    program.values = values;
    program.context = context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    memset(&result->isa_receipt, 0, sizeof(result->isa_receipt));
    memset(&isa_error, 0, sizeof(isa_error));
    if (laplace_isa_execute(
            &program, &result->isa_receipt, &isa_error) != LAPLACE_ISA_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace source-profile ISA execution failed"),
                 errdetail("status=%d instruction=%llu",
                           (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }

    memset(&semantic_receipt, 0, sizeof(semantic_receipt));
    memset(&semantic_error, 0, sizeof(semantic_error));
    if (laplace_source_profile_validate_batch(
            profiles, profile_count, &semantic_receipt, &semantic_error) !=
            LAPLACE_SOURCE_PROFILE_OK ||
        memcmp(&semantic_receipt, &output_receipt, sizeof(semantic_receipt)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace source-profile semantic receipt reconstruction failed")));
    }
    result->semantic_receipt = semantic_receipt;

    laplace_pg_persist_execution_receipt(
        &result->isa_receipt, profile_count, instruction.opcode);
    durable_profiles = profile_records_array(
        profiles, profile_count, &durable_array_oid);
    persist_source_profiles(
        durable_array_oid, PointerGetDatum(durable_profiles),
        &result->semantic_receipt, &result->isa_receipt);
}

Datum LAPLACE_PG_SOURCE_PROFILE_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    laplace_source_profile_manifest* profiles;
    size_t profile_count = 0u;
    laplace_pg_source_profile_execution execution;
    Datum result_values[13];
    bool result_nulls[13] = {false};
    HeapTuple result_tuple;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    profiles = read_profiles(input_array, &profile_count);
    memset(&execution, 0, sizeof(execution));
    laplace_pg_source_profile_execute_and_persist(
        &context, profiles, profile_count, &execution);

    result_values[0] = PointerGetDatum(profile_ids(profiles, profile_count));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.receipt_id.bytes, 32u));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.selected_boundary_fingerprint.bytes, 32u));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.input_fingerprint.bytes, 32u));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.output_fingerprint.bytes, 32u));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.isa_receipt.receipt_id.bytes, 32u));
    result_values[6] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.profile_count);
    result_values[7] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.closure_subject_count);
    result_values[8] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.persisted_count);
    result_values[9] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.negative_count);
    result_values[10] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.exact_reconstruction_count);
    result_values[11] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.semantic_reconstruction_count);
    result_values[12] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.no_reconstruction_count);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 13);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
