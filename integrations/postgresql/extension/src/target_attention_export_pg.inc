#include "laplace/target_attention_safetensors.h"

PG_FUNCTION_INFO_V1(laplace_pg_target_attention_export);

Datum laplace_pg_target_attention_export(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 context_fingerprint;
    laplace_target_compile_request compile_request;
    laplace_target_compile_receipt compile_receipt;
    laplace_target_compile_result* compiled = NULL;
    laplace_target_attention_projection_request projection_request;
    laplace_target_attention_projection_receipt projection_receipt;
    laplace_target_attention_projection_result* projection = NULL;
    laplace_target_attention_safetensors_receipt codec_receipt;
    laplace_pg_target_attention_job_bundle* bundles;
    laplace_target_compile_job* jobs;
    laplace_target_compile_slot_receipt* slots;
    laplace_target_attention_head_spec* head_specs;
    bytea* artifact;
    size_t job_count = 0u;
    size_t slot_count;
    size_t head_count = 0u;
    size_t head_cursor = 0u;
    size_t index;
    size_t required_bytes = 0u;
    uint64_t hidden_width;
    double relative_tolerance;
    bool require_exact;
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

    memset(&compile_request, 0, sizeof(compile_request));
    laplace_pg_read_digest(PG_GETARG_DATUM(1), &compile_request.evidence_boundary,
                           "target export evidence_boundary");
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &compile_request.evidence_epoch,
                           "target export evidence_epoch");
    laplace_pg_read_digest(PG_GETARG_DATUM(3), &compile_request.recipe_fingerprint,
                           "target export recipe_fingerprint");
    laplace_pg_read_digest(PG_GETARG_DATUM(4),
                           &compile_request.target_contract_fingerprint,
                           "target export target_contract_fingerprint");
    bundles = laplace_pg_target_attention_read_jobs(
        PG_GETARG_ARRAYTYPE_P(5), &context_fingerprint, &job_count);
    hidden_width = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(6), "target export hidden_width");
    relative_tolerance = PG_GETARG_FLOAT8(7);
    require_exact = PG_GETARG_BOOL(8);
    if (hidden_width == 0u || !isfinite(relative_tolerance) ||
        relative_tolerance <= 0.0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target export hidden width and tolerance must be positive")));
    }

    jobs = (laplace_target_compile_job*)palloc0(sizeof(*jobs) * job_count);
    for (index = 0u; index < job_count; ++index) jobs[index] = bundles[index].job;
    compile_request.jobs = jobs;
    compile_request.job_count = job_count;
    compile_request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    compile_request.version = LAPLACE_TARGET_COMPILE_VERSION;

    memset(&compile_receipt, 0, sizeof(compile_receipt));
    if (laplace_target_compile_execute(
            &compile_request, &compiled, &compile_receipt) !=
            LAPLACE_TARGET_COMPILE_OK || compiled == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target export operator compilation failed"),
                 errdetail("status=%u", compile_receipt.status)));
    }

    slot_count = laplace_target_compile_result_slot_count(compiled);
    if (slot_count != job_count || slot_count == 0u) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target export slot inventory does not match submitted jobs"),
                 errdetail("jobs=%zu slots=%zu", job_count, slot_count)));
    }
    slots = (laplace_target_compile_slot_receipt*)palloc0(
        sizeof(*slots) * slot_count);
    for (index = 0u; index < slot_count; ++index) {
        if (laplace_target_compile_result_slot_receipt(
                compiled, index, &slots[index]) != LAPLACE_TARGET_COMPILE_OK) {
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target export slot receipt read failed")));
        }
        if (slots[index].target_role == LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) {
            ++head_count;
        }
    }
    if (head_count == 0u || slot_count != head_count * 2u) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target export requires paired QK and VO jobs")));
    }

    head_specs = (laplace_target_attention_head_spec*)palloc0(
        sizeof(*head_specs) * head_count);
    for (index = 0u; index < slot_count; ++index) {
        size_t vo_index;
        if (slots[index].target_role != LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) continue;
        head_specs[head_cursor].qk_slot_index = index;
        head_specs[head_cursor].head_rank =
            laplace_pg_target_attention_rank_for_coordinate(
                bundles, job_count, slots[index].layer_index,
                slots[index].head_index, slots[index].expert_index);
        for (vo_index = 0u; vo_index < slot_count; ++vo_index) {
            if (slots[vo_index].target_role == LAPLACE_TARGET_ROLE_CONTRIBUTION_VO &&
                slots[vo_index].layer_index == slots[index].layer_index &&
                slots[vo_index].head_index == slots[index].head_index &&
                slots[vo_index].expert_index == slots[index].expert_index) {
                head_specs[head_cursor].vo_slot_index = vo_index;
                break;
            }
        }
        if (vo_index == slot_count) {
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target export QK job has no matching VO job")));
        }
        ++head_cursor;
    }
    if (head_cursor != head_count) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target export head pairing failed")));
    }

    memset(&projection_request, 0, sizeof(projection_request));
    projection_request.heads = head_specs;
    projection_request.head_count = head_count;
    projection_request.hidden_width = hidden_width;
    projection_request.relative_tolerance = relative_tolerance;
    projection_request.flags = require_exact
        ? LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT : 0u;
    projection_request.version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;
    memset(&projection_receipt, 0, sizeof(projection_receipt));
    {
        const laplace_target_attention_projection_status projection_status =
            laplace_target_attention_project(
                compiled, &projection_request, &projection, &projection_receipt);
        if (projection_status != LAPLACE_TARGET_ATTENTION_PROJECTION_OK ||
            projection == NULL) {
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target export attention projection failed"),
                     errdetail("status=%d", (int)projection_status)));
        }
    }

    memset(&codec_receipt, 0, sizeof(codec_receipt));
    {
        const laplace_target_attention_safetensors_status measure_status =
            laplace_target_attention_safetensors_encode(
                &compile_receipt, projection, &projection_receipt,
                NULL, 0u, &required_bytes, &codec_receipt);
        if (measure_status !=
                LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT ||
            required_bytes == 0u || required_bytes > (size_t)(MaxAllocSize - VARHDRSZ)) {
            laplace_target_attention_projection_result_destroy(&projection);
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                     errmsg("Laplace target export artifact preflight failed"),
                     errdetail("status=%d required_bytes=%zu",
                               (int)measure_status, required_bytes)));
        }
    }

    artifact = (bytea*)palloc(VARHDRSZ + required_bytes);
    SET_VARSIZE(artifact, VARHDRSZ + required_bytes);
    {
        const laplace_target_attention_safetensors_status codec_status =
            laplace_target_attention_safetensors_encode(
                &compile_receipt, projection, &projection_receipt,
                (uint8_t*)VARDATA(artifact), required_bytes,
                &required_bytes, &codec_receipt);
        if (codec_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK ||
            codec_receipt.byte_count != (uint64_t)VARSIZE_ANY_EXHDR(artifact)) {
            laplace_target_attention_projection_result_destroy(&projection);
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target export SafeTensors encoding failed"),
                     errdetail("status=%d", (int)codec_status)));
        }
    }

    result_values[0] = PointerGetDatum(artifact);
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.artifact_id.bytes, sizeof(codec_receipt.artifact_id.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.compile_receipt_id.bytes,
        sizeof(codec_receipt.compile_receipt_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.compile_request_fingerprint.bytes,
        sizeof(codec_receipt.compile_request_fingerprint.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.projection_id.bytes, sizeof(codec_receipt.projection_id.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.embedding_fingerprint.bytes,
        sizeof(codec_receipt.embedding_fingerprint.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.head_set_fingerprint.bytes,
        sizeof(codec_receipt.head_set_fingerprint.bytes)));
    result_values[7] = laplace_pg_numeric_from_uint64(codec_receipt.byte_count);
    result_values[8] = laplace_pg_numeric_from_uint64(codec_receipt.header_byte_count);
    result_values[9] = laplace_pg_numeric_from_uint64(codec_receipt.data_byte_count);
    result_values[10] = laplace_pg_numeric_from_uint64(codec_receipt.tensor_count);
    result_values[11] = laplace_pg_numeric_from_uint64(codec_receipt.head_count);
    result_values[12] = laplace_pg_numeric_from_uint64(codec_receipt.field_count);
    result_values[13] = laplace_pg_numeric_from_uint64(codec_receipt.hidden_width);
    result_values[14] = laplace_pg_numeric_from_uint64(
        projection_receipt.semantic_basis_rank);
    result_values[15] = laplace_pg_numeric_from_uint64(projection_receipt.null_basis_rank);
    result_values[16] = Float8GetDatum(codec_receipt.max_relative_residual);
    result_values[17] = Int32GetDatum((int32)compile_receipt.status);
    result_values[18] = Int32GetDatum((int32)projection_receipt.status);
    result_values[19] = Int32GetDatum((int32)codec_receipt.status);

    laplace_target_attention_projection_result_destroy(&projection);
    laplace_target_compile_result_destroy(&compiled);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 20);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
