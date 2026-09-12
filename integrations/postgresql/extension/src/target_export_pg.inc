#include "laplace/target_package.h"
#include "laplace/target_safetensors.h"
#include "laplace/target_scope_plan.h"

PG_FUNCTION_INFO_V1(laplace_pg_target_safetensors_export_scoped);

typedef struct laplace_pg_target_export_scope_slot_bundle {
    laplace_target_scope_slot_spec slot;
} laplace_pg_target_export_scope_slot_bundle;

static laplace_pg_target_export_scope_slot_bundle*
laplace_pg_target_export_read_scope_slots(
    ArrayType* array,
    size_t* slot_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    laplace_pg_target_export_scope_slot_bundle* bundles;
    int index;

    values = laplace_pg_target_attention_deconstruct_composite_array(
        array, "target_scope_slot", &count, &nulls);
    bundles = (laplace_pg_target_export_scope_slot_bundle*)palloc0(
        sizeof(*bundles) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_target_scope_slot_spec* slot;
        size_t family_count = 0u;
        const bool attention_role =
            !nulls[index] &&
            (laplace_pg_target_attention_read_u32_attribute(
                 DatumGetHeapTupleHeader(values[index]), 1,
                 "target export target_role") ==
                 LAPLACE_TARGET_ROLE_COMPATIBILITY_QK ||
             laplace_pg_target_attention_read_u32_attribute(
                 DatumGetHeapTupleHeader(values[index]), 1,
                 "target export target_role") ==
                 LAPLACE_TARGET_ROLE_CONTRIBUTION_VO);

        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace target export scope slots cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        slot = &bundles[index].slot;
        slot->target_role = laplace_pg_target_attention_read_u32_attribute(
            tuple, 1, "target export target_role");
        if (slot->target_role < LAPLACE_TARGET_ROLE_COMPATIBILITY_QK ||
            slot->target_role > LAPLACE_TARGET_ROLE_OUTPUT) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target export scope contains an unknown target role"),
                     errdetail("role=%u", slot->target_role)));
        }
        slot->layer_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 2, "target export layer_index");
        slot->head_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 3, "target export head_index");
        slot->expert_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 4, "target export expert_index");
        slot->eligible_relation_families =
            laplace_pg_target_attention_read_scope_relation_families(
                tuple, &family_count);
        slot->eligible_relation_family_count = family_count;
        slot->eligible_source_mask = laplace_pg_target_attention_read_u32_attribute(
            tuple, 6, "target export eligible_source_mask");
        slot->head_rank = laplace_pg_target_attention_read_u64_attribute(
            tuple, 7, "target export head_rank");
        slot->flags = laplace_pg_target_attention_read_u32_attribute(
            tuple, 8, "target export flags");
        slot->reserved = 0u;
        if (attention_role && slot->head_rank == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace QK/VO target export slots require positive head_rank")));
        }
        if (!attention_role && slot->head_rank != 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace non-attention target export slots require head_rank=0"),
                     errdetail("role=%u head_rank=%llu",
                               slot->target_role,
                               (unsigned long long)slot->head_rank)));
        }
    }
    *slot_count = (size_t)count;
    return bundles;
}

static bytea* laplace_pg_target_safetensors_encode_compiled(
    laplace_target_compile_result** compiled,
    const laplace_target_compile_receipt* compile_receipt,
    laplace_target_package_receipt* package_receipt,
    laplace_target_safetensors_receipt* codec_receipt) {
    size_t package_bytes = 0u;
    size_t artifact_bytes = 0u;
    uint8_t* package;
    bytea* artifact;
    laplace_target_package_status package_status;
    laplace_target_safetensors_status codec_status;

    package_status = laplace_target_package_measure(
        *compiled, compile_receipt, &package_bytes);
    if (package_status != LAPLACE_TARGET_PACKAGE_OK || package_bytes == 0u ||
        package_bytes > (size_t)MaxAllocSize) {
        laplace_target_compile_result_destroy(compiled);
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace target-neutral package preflight failed"),
                 errdetail("status=%d required_bytes=%zu",
                           (int)package_status, package_bytes)));
    }

    package = (uint8_t*)palloc(package_bytes);
    memset(package_receipt, 0, sizeof(*package_receipt));
    package_status = laplace_target_package_encode(
        *compiled, compile_receipt, package, package_bytes, package_receipt);
    laplace_target_compile_result_destroy(compiled);
    if (package_status != LAPLACE_TARGET_PACKAGE_OK ||
        package_receipt->byte_count != (uint64_t)package_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target-neutral package encoding failed"),
                 errdetail("status=%d", (int)package_status)));
    }

    memset(codec_receipt, 0, sizeof(*codec_receipt));
    codec_status = laplace_target_safetensors_encode(
        package, package_bytes, NULL, 0u, &artifact_bytes, codec_receipt);
    if (codec_status != LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT ||
        artifact_bytes == 0u || artifact_bytes > (size_t)(MaxAllocSize - VARHDRSZ)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace target SafeTensors preflight failed"),
                 errdetail("status=%d required_bytes=%zu",
                           (int)codec_status, artifact_bytes)));
    }

    artifact = (bytea*)palloc(VARHDRSZ + artifact_bytes);
    SET_VARSIZE(artifact, VARHDRSZ + artifact_bytes);
    codec_status = laplace_target_safetensors_encode(
        package, package_bytes,
        (uint8_t*)VARDATA(artifact), artifact_bytes,
        &artifact_bytes, codec_receipt);
    if (codec_status != LAPLACE_TARGET_SAFETENSORS_OK ||
        codec_receipt->byte_count != (uint64_t)VARSIZE_ANY_EXHDR(artifact)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target SafeTensors encoding failed"),
                 errdetail("status=%d", (int)codec_status)));
    }
    pfree(package);
    return artifact;
}

Datum laplace_pg_target_safetensors_export_scoped(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 context_fingerprint;
    laplace_target_scope_plan_request scope_request;
    laplace_target_scope_plan_receipt scope_receipt;
    laplace_target_compile_receipt compile_receipt;
    laplace_target_compile_result* compiled = NULL;
    laplace_target_package_receipt package_receipt;
    laplace_target_safetensors_receipt codec_receipt;
    laplace_pg_target_export_scope_slot_bundle* scope_bundles;
    bytea* artifact;
    size_t scope_slot_count = 0u;
    int32 operator_program_flags;
    int32 target_compile_flags;
    Datum result_values[20];
    bool result_nulls[20] = {false};
    HeapTuple result_tuple;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if (laplace_framework_context_fingerprint(
            &context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target export execution context cannot be fingerprinted")));
    }

    memset(&scope_request, 0, sizeof(scope_request));
    laplace_pg_read_digest(PG_GETARG_DATUM(1), &scope_request.evidence_boundary,
                           "target export evidence_boundary");
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &scope_request.evidence_epoch,
                           "target export evidence_epoch");
    laplace_pg_read_digest(PG_GETARG_DATUM(3), &scope_request.recipe_fingerprint,
                           "target export recipe_fingerprint");
    laplace_pg_read_digest(PG_GETARG_DATUM(4),
                           &scope_request.target_contract_fingerprint,
                           "target export target_contract_fingerprint");
    scope_request.context_fingerprint = context_fingerprint;
    scope_request.fields = laplace_pg_target_attention_read_fields(
        PG_GETARG_ARRAYTYPE_P(5), &scope_request.field_count);
    scope_request.constraints = laplace_pg_target_attention_read_constraints(
        PG_GETARG_ARRAYTYPE_P(6), &scope_request.constraint_count);
    scope_bundles = laplace_pg_target_export_read_scope_slots(
        PG_GETARG_ARRAYTYPE_P(7), &scope_slot_count);
    scope_request.slots = &scope_bundles[0].slot;
    scope_request.slot_count = scope_slot_count;
    scope_request.numeric_tolerance = PG_GETARG_FLOAT8(8);
    operator_program_flags = PG_GETARG_INT32(9);
    target_compile_flags = PG_GETARG_INT32(10);
    if (!isfinite(scope_request.numeric_tolerance) ||
        scope_request.numeric_tolerance <= 0.0 ||
        operator_program_flags < 0 || target_compile_flags < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target export tolerance/flags are invalid")));
    }
    scope_request.operator_program_flags = (uint32_t)operator_program_flags;
    scope_request.target_compile_flags = (uint32_t)target_compile_flags;
    scope_request.version = LAPLACE_TARGET_SCOPE_PLAN_VERSION;

    memset(&scope_receipt, 0, sizeof(scope_receipt));
    memset(&compile_receipt, 0, sizeof(compile_receipt));
    {
        const laplace_target_scope_plan_status scope_status =
            laplace_target_scope_plan_compile(
                &scope_request, &compiled, &compile_receipt, &scope_receipt);
        if (scope_status != LAPLACE_TARGET_SCOPE_PLAN_OK || compiled == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target export operator planning failed"),
                     errdetail("scope_status=%d compile_status=%u",
                               (int)scope_status, scope_receipt.compile_status)));
        }
    }

    artifact = laplace_pg_target_safetensors_encode_compiled(
        &compiled, &compile_receipt, &package_receipt, &codec_receipt);

    result_values[0] = PointerGetDatum(artifact);
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.artifact_id.bytes, sizeof(codec_receipt.artifact_id.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.target_package_id.bytes,
        sizeof(codec_receipt.target_package_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        scope_receipt.receipt_id.bytes, sizeof(scope_receipt.receipt_id.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        scope_receipt.request_fingerprint.bytes,
        sizeof(scope_receipt.request_fingerprint.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        scope_receipt.slot_set_fingerprint.bytes,
        sizeof(scope_receipt.slot_set_fingerprint.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.compile_receipt_id.bytes,
        sizeof(codec_receipt.compile_receipt_id.bytes)));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.request_fingerprint.bytes,
        sizeof(codec_receipt.request_fingerprint.bytes)));
    result_values[8] = laplace_pg_numeric_from_uint64(codec_receipt.byte_count);
    result_values[9] = laplace_pg_numeric_from_uint64(codec_receipt.header_byte_count);
    result_values[10] = laplace_pg_numeric_from_uint64(codec_receipt.data_byte_count);
    result_values[11] = laplace_pg_numeric_from_uint64(codec_receipt.tensor_count);
    result_values[12] = laplace_pg_numeric_from_uint64(codec_receipt.slot_count);
    result_values[13] = laplace_pg_numeric_from_uint64(scope_receipt.field_count);
    result_values[14] = laplace_pg_numeric_from_uint64(
        scope_receipt.selected_constraint_count);
    result_values[15] = Int32GetDatum((int32)scope_receipt.source_mask_union);
    result_values[16] = Int32GetDatum((int32)scope_receipt.status);
    result_values[17] = Int32GetDatum((int32)compile_receipt.status);
    result_values[18] = Int32GetDatum((int32)package_receipt.status);
    result_values[19] = Int32GetDatum((int32)codec_receipt.status);

    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 20);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
