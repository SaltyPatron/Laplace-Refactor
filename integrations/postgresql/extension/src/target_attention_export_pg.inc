#include "laplace/target_attention_safetensors.h"
#include "laplace/target_scope_plan.h"

PG_FUNCTION_INFO_V1(laplace_pg_target_attention_export);
PG_FUNCTION_INFO_V1(laplace_pg_target_attention_export_scoped);

typedef struct laplace_pg_target_attention_scope_slot_bundle {
    laplace_target_scope_slot_spec slot;
} laplace_pg_target_attention_scope_slot_bundle;

static uint32_t* laplace_pg_target_attention_read_scope_relation_families(
    HeapTupleHeader tuple,
    size_t* family_count) {
    ArrayType* array = DatumGetArrayTypeP(
        laplace_pg_required_composite_attribute(
            tuple, 5, "target scope eligible_relation_families"));
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    uint32_t* families;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != INT4OID) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace target scope eligible_relation_families must be an integer array")));
    }
    deconstruct_array(
        array, INT4OID, sizeof(int32), true, TYPALIGN_INT,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target scope eligible_relation_families cannot be empty")));
    }
    families = (uint32_t*)palloc0(sizeof(*families) * (size_t)count);
    for (index = 0; index < count; ++index) {
        const int32 family = nulls[index] ? -1 : DatumGetInt32(values[index]);
        if (family <= 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target scope relation families must be positive")));
        }
        families[index] = (uint32_t)family;
    }
    *family_count = (size_t)count;
    return families;
}

static laplace_pg_target_attention_scope_slot_bundle*
laplace_pg_target_attention_read_scope_slots(
    ArrayType* array,
    size_t* slot_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    laplace_pg_target_attention_scope_slot_bundle* bundles;
    int index;
    values = laplace_pg_target_attention_deconstruct_composite_array(
        array, "target_scope_slot", &count, &nulls);
    bundles = (laplace_pg_target_attention_scope_slot_bundle*)palloc0(
        sizeof(*bundles) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_target_scope_slot_spec* slot;
        size_t family_count = 0u;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace target scope slots cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        slot = &bundles[index].slot;
        slot->target_role = laplace_pg_target_attention_read_u32_attribute(
            tuple, 1, "target scope target_role");
        if (slot->target_role != LAPLACE_TARGET_ROLE_COMPATIBILITY_QK &&
            slot->target_role != LAPLACE_TARGET_ROLE_CONTRIBUTION_VO) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target attention scope accepts only QK compatibility and VO contribution slots")));
        }
        slot->layer_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 2, "target scope layer_index");
        slot->head_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 3, "target scope head_index");
        slot->expert_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 4, "target scope expert_index");
        slot->eligible_relation_families =
            laplace_pg_target_attention_read_scope_relation_families(
                tuple, &family_count);
        slot->eligible_relation_family_count = family_count;
        slot->eligible_source_mask = laplace_pg_target_attention_read_u32_attribute(
            tuple, 6, "target scope eligible_source_mask");
        slot->head_rank = laplace_pg_target_attention_read_u64_attribute(
            tuple, 7, "target scope head_rank");
        slot->flags = laplace_pg_target_attention_read_u32_attribute(
            tuple, 8, "target scope flags");
        slot->reserved = 0u;
        if (slot->head_rank == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target scope head_rank must be positive")));
        }
    }
    *slot_count = (size_t)count;
    return bundles;
}

static uint64_t laplace_pg_target_attention_scope_rank_for_coordinate(
    const laplace_pg_target_attention_scope_slot_bundle* bundles,
    size_t slot_count,
    uint32_t layer_index,
    uint32_t head_index,
    uint32_t expert_index) {
    uint64_t rank = 0u;
    size_t qk_count = 0u;
    size_t vo_count = 0u;
    size_t index;
    for (index = 0u; index < slot_count; ++index) {
        const laplace_target_scope_slot_spec* slot = &bundles[index].slot;
        if (slot->layer_index != layer_index || slot->head_index != head_index ||
            slot->expert_index != expert_index) {
            continue;
        }
        if (rank == 0u) rank = slot->head_rank;
        if (rank != slot->head_rank) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace QK and VO target scope slots for one head must declare the same rank")));
        }
        if (slot->target_role == LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) {
            ++qk_count;
        } else if (slot->target_role == LAPLACE_TARGET_ROLE_CONTRIBUTION_VO) {
            ++vo_count;
        }
    }
    if (qk_count != 1u || vo_count != 1u || rank == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target attention scope requires exactly one QK and one VO slot per target head"),
                 errdetail("layer=%u head=%u expert=%u qk=%zu vo=%zu",
                           layer_index, head_index, expert_index, qk_count, vo_count)));
    }
    return rank;
}

static bytea* laplace_pg_target_attention_encode_compiled(
    laplace_target_compile_result** compiled,
    const laplace_target_compile_receipt* compile_receipt,
    const laplace_target_attention_head_spec* head_specs,
    size_t head_count,
    uint64_t hidden_width,
    double relative_tolerance,
    bool require_exact,
    laplace_target_attention_projection_receipt* projection_receipt,
    laplace_target_attention_safetensors_receipt* codec_receipt) {
    laplace_target_attention_projection_request projection_request;
    laplace_target_attention_projection_result* projection = NULL;
    bytea* artifact;
    size_t required_bytes = 0u;

    memset(&projection_request, 0, sizeof(projection_request));
    projection_request.heads = head_specs;
    projection_request.head_count = head_count;
    projection_request.hidden_width = hidden_width;
    projection_request.relative_tolerance = relative_tolerance;
    projection_request.flags = require_exact
        ? LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT : 0u;
    projection_request.version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;
    memset(projection_receipt, 0, sizeof(*projection_receipt));
    {
        const laplace_target_attention_projection_status projection_status =
            laplace_target_attention_project(
                *compiled, &projection_request, &projection, projection_receipt);
        if (projection_status != LAPLACE_TARGET_ATTENTION_PROJECTION_OK ||
            projection == NULL) {
            laplace_target_compile_result_destroy(compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target export attention projection failed"),
                     errdetail("status=%d", (int)projection_status)));
        }
    }

    memset(codec_receipt, 0, sizeof(*codec_receipt));
    {
        const laplace_target_attention_safetensors_status measure_status =
            laplace_target_attention_safetensors_encode(
                compile_receipt, projection, projection_receipt,
                NULL, 0u, &required_bytes, codec_receipt);
        if (measure_status !=
                LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT ||
            required_bytes == 0u || required_bytes > (size_t)(MaxAllocSize - VARHDRSZ)) {
            laplace_target_attention_projection_result_destroy(&projection);
            laplace_target_compile_result_destroy(compiled);
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
                compile_receipt, projection, projection_receipt,
                (uint8_t*)VARDATA(artifact), required_bytes,
                &required_bytes, codec_receipt);
        if (codec_status != LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK ||
            codec_receipt->byte_count != (uint64_t)VARSIZE_ANY_EXHDR(artifact)) {
            laplace_target_attention_projection_result_destroy(&projection);
            laplace_target_compile_result_destroy(compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target export SafeTensors encoding failed"),
                     errdetail("status=%d", (int)codec_status)));
        }
    }

    laplace_target_attention_projection_result_destroy(&projection);
    return artifact;
}

static void laplace_pg_target_attention_export_common_values(
    Datum* result_values,
    bytea* artifact,
    const laplace_target_compile_receipt* compile_receipt,
    const laplace_target_attention_projection_receipt* projection_receipt,
    const laplace_target_attention_safetensors_receipt* codec_receipt) {
    result_values[0] = PointerGetDatum(artifact);
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt->artifact_id.bytes, sizeof(codec_receipt->artifact_id.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt->compile_receipt_id.bytes,
        sizeof(codec_receipt->compile_receipt_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt->compile_request_fingerprint.bytes,
        sizeof(codec_receipt->compile_request_fingerprint.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt->projection_id.bytes, sizeof(codec_receipt->projection_id.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt->embedding_fingerprint.bytes,
        sizeof(codec_receipt->embedding_fingerprint.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt->head_set_fingerprint.bytes,
        sizeof(codec_receipt->head_set_fingerprint.bytes)));
    result_values[7] = laplace_pg_numeric_from_uint64(codec_receipt->byte_count);
    result_values[8] = laplace_pg_numeric_from_uint64(codec_receipt->header_byte_count);
    result_values[9] = laplace_pg_numeric_from_uint64(codec_receipt->data_byte_count);
    result_values[10] = laplace_pg_numeric_from_uint64(codec_receipt->tensor_count);
    result_values[11] = laplace_pg_numeric_from_uint64(codec_receipt->head_count);
    result_values[12] = laplace_pg_numeric_from_uint64(codec_receipt->field_count);
    result_values[13] = laplace_pg_numeric_from_uint64(codec_receipt->hidden_width);
    result_values[14] = laplace_pg_numeric_from_uint64(
        projection_receipt->semantic_basis_rank);
    result_values[15] = laplace_pg_numeric_from_uint64(projection_receipt->null_basis_rank);
    result_values[16] = Float8GetDatum(codec_receipt->max_relative_residual);
    result_values[17] = Int32GetDatum((int32)compile_receipt->status);
    result_values[18] = Int32GetDatum((int32)projection_receipt->status);
    result_values[19] = Int32GetDatum((int32)codec_receipt->status);
}

Datum laplace_pg_target_attention_export(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 context_fingerprint;
    laplace_target_compile_request compile_request;
    laplace_target_compile_receipt compile_receipt;
    laplace_target_compile_result* compiled = NULL;
    laplace_target_attention_projection_receipt projection_receipt;
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

    artifact = laplace_pg_target_attention_encode_compiled(
        &compiled, &compile_receipt, head_specs, head_count, hidden_width,
        relative_tolerance, require_exact, &projection_receipt, &codec_receipt);
    laplace_pg_target_attention_export_common_values(
        result_values, artifact, &compile_receipt, &projection_receipt, &codec_receipt);
    laplace_target_compile_result_destroy(&compiled);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 20);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}

Datum laplace_pg_target_attention_export_scoped(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 context_fingerprint;
    laplace_target_scope_plan_request scope_request;
    laplace_target_scope_plan_receipt scope_receipt;
    laplace_target_compile_receipt compile_receipt;
    laplace_target_compile_result* compiled = NULL;
    laplace_target_attention_projection_receipt projection_receipt;
    laplace_target_attention_safetensors_receipt codec_receipt;
    laplace_pg_target_attention_scope_slot_bundle* scope_bundles;
    laplace_target_compile_slot_receipt* compiled_slots;
    laplace_target_attention_head_spec* head_specs;
    bytea* artifact;
    size_t scope_slot_count = 0u;
    size_t slot_count;
    size_t head_count = 0u;
    size_t head_cursor = 0u;
    size_t index;
    uint64_t hidden_width;
    double relative_tolerance;
    bool require_exact;
    int32 operator_program_flags;
    Datum result_values[26];
    bool result_nulls[26] = {false};
    HeapTuple result_tuple;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if (laplace_framework_context_fingerprint(
            &context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace scoped target export execution context cannot be fingerprinted")));
    }

    memset(&scope_request, 0, sizeof(scope_request));
    laplace_pg_read_digest(PG_GETARG_DATUM(1), &scope_request.evidence_boundary,
                           "scoped target export evidence_boundary");
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &scope_request.evidence_epoch,
                           "scoped target export evidence_epoch");
    laplace_pg_read_digest(PG_GETARG_DATUM(3), &scope_request.recipe_fingerprint,
                           "scoped target export recipe_fingerprint");
    laplace_pg_read_digest(PG_GETARG_DATUM(4),
                           &scope_request.target_contract_fingerprint,
                           "scoped target export target_contract_fingerprint");
    scope_request.context_fingerprint = context_fingerprint;
    scope_request.fields = laplace_pg_target_attention_read_fields(
        PG_GETARG_ARRAYTYPE_P(5), &scope_request.field_count);
    scope_request.constraints = laplace_pg_target_attention_read_constraints(
        PG_GETARG_ARRAYTYPE_P(6), &scope_request.constraint_count);
    scope_bundles = laplace_pg_target_attention_read_scope_slots(
        PG_GETARG_ARRAYTYPE_P(7), &scope_slot_count);
    scope_request.slots = &scope_bundles[0].slot;
    scope_request.slot_count = scope_slot_count;
    scope_request.numeric_tolerance = PG_GETARG_FLOAT8(8);
    operator_program_flags = PG_GETARG_INT32(9);
    if (operator_program_flags < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace scoped target export operator_program_flags cannot be negative")));
    }
    scope_request.operator_program_flags = (uint32_t)operator_program_flags;
    scope_request.target_compile_flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    scope_request.version = LAPLACE_TARGET_SCOPE_PLAN_VERSION;

    hidden_width = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(10), "scoped target export hidden_width");
    relative_tolerance = PG_GETARG_FLOAT8(11);
    require_exact = PG_GETARG_BOOL(12);
    if (!isfinite(scope_request.numeric_tolerance) ||
        scope_request.numeric_tolerance <= 0.0 || hidden_width == 0u ||
        !isfinite(relative_tolerance) || relative_tolerance <= 0.0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace scoped target export tolerances and hidden width must be positive")));
    }

    /* The slot bundle has no fields after slot, so the slot structs are contiguous. */
    memset(&scope_receipt, 0, sizeof(scope_receipt));
    memset(&compile_receipt, 0, sizeof(compile_receipt));
    {
        const laplace_target_scope_plan_status scope_status =
            laplace_target_scope_plan_compile(
                &scope_request, &compiled, &compile_receipt, &scope_receipt);
        if (scope_status != LAPLACE_TARGET_SCOPE_PLAN_OK || compiled == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace scoped target export operator planning failed"),
                     errdetail("scope_status=%d compile_status=%u",
                               (int)scope_status, compile_receipt.status)));
        }
    }

    slot_count = laplace_target_compile_result_slot_count(compiled);
    if (slot_count != scope_slot_count || slot_count == 0u) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace scoped target export slot inventory does not match planned scope"),
                 errdetail("scope_slots=%zu compiled_slots=%zu",
                           scope_slot_count, slot_count)));
    }
    compiled_slots = (laplace_target_compile_slot_receipt*)palloc0(
        sizeof(*compiled_slots) * slot_count);
    for (index = 0u; index < slot_count; ++index) {
        if (laplace_target_compile_result_slot_receipt(
                compiled, index, &compiled_slots[index]) != LAPLACE_TARGET_COMPILE_OK) {
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace scoped target export slot receipt read failed")));
        }
        if (compiled_slots[index].target_role == LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) {
            ++head_count;
        }
    }
    if (head_count == 0u || slot_count != head_count * 2u) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace scoped target export requires paired QK and VO slots")));
    }

    head_specs = (laplace_target_attention_head_spec*)palloc0(
        sizeof(*head_specs) * head_count);
    for (index = 0u; index < slot_count; ++index) {
        size_t vo_index;
        if (compiled_slots[index].target_role != LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) {
            continue;
        }
        head_specs[head_cursor].qk_slot_index = index;
        head_specs[head_cursor].head_rank =
            laplace_pg_target_attention_scope_rank_for_coordinate(
                scope_bundles, scope_slot_count,
                compiled_slots[index].layer_index,
                compiled_slots[index].head_index,
                compiled_slots[index].expert_index);
        for (vo_index = 0u; vo_index < slot_count; ++vo_index) {
            if (compiled_slots[vo_index].target_role == LAPLACE_TARGET_ROLE_CONTRIBUTION_VO &&
                compiled_slots[vo_index].layer_index == compiled_slots[index].layer_index &&
                compiled_slots[vo_index].head_index == compiled_slots[index].head_index &&
                compiled_slots[vo_index].expert_index == compiled_slots[index].expert_index) {
                head_specs[head_cursor].vo_slot_index = vo_index;
                break;
            }
        }
        if (vo_index == slot_count) {
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace scoped target export QK slot has no matching VO slot")));
        }
        ++head_cursor;
    }
    if (head_cursor != head_count) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace scoped target export head pairing failed")));
    }

    artifact = laplace_pg_target_attention_encode_compiled(
        &compiled, &compile_receipt, head_specs, head_count, hidden_width,
        relative_tolerance, require_exact, &projection_receipt, &codec_receipt);

    result_values[0] = PointerGetDatum(artifact);
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.artifact_id.bytes, sizeof(codec_receipt.artifact_id.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        scope_receipt.receipt_id.bytes, sizeof(scope_receipt.receipt_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        scope_receipt.request_fingerprint.bytes,
        sizeof(scope_receipt.request_fingerprint.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        scope_receipt.slot_set_fingerprint.bytes,
        sizeof(scope_receipt.slot_set_fingerprint.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.compile_receipt_id.bytes,
        sizeof(codec_receipt.compile_receipt_id.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.compile_request_fingerprint.bytes,
        sizeof(codec_receipt.compile_request_fingerprint.bytes)));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.projection_id.bytes, sizeof(codec_receipt.projection_id.bytes)));
    result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.embedding_fingerprint.bytes,
        sizeof(codec_receipt.embedding_fingerprint.bytes)));
    result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        codec_receipt.head_set_fingerprint.bytes,
        sizeof(codec_receipt.head_set_fingerprint.bytes)));
    result_values[10] = laplace_pg_numeric_from_uint64(codec_receipt.byte_count);
    result_values[11] = laplace_pg_numeric_from_uint64(codec_receipt.header_byte_count);
    result_values[12] = laplace_pg_numeric_from_uint64(codec_receipt.data_byte_count);
    result_values[13] = laplace_pg_numeric_from_uint64(codec_receipt.tensor_count);
    result_values[14] = laplace_pg_numeric_from_uint64(codec_receipt.head_count);
    result_values[15] = laplace_pg_numeric_from_uint64(codec_receipt.field_count);
    result_values[16] = laplace_pg_numeric_from_uint64(codec_receipt.hidden_width);
    result_values[17] = laplace_pg_numeric_from_uint64(
        projection_receipt.semantic_basis_rank);
    result_values[18] = laplace_pg_numeric_from_uint64(projection_receipt.null_basis_rank);
    result_values[19] = Float8GetDatum(codec_receipt.max_relative_residual);
    result_values[20] = laplace_pg_numeric_from_uint64(
        scope_receipt.selected_constraint_count);
    result_values[21] = Int32GetDatum((int32)scope_receipt.source_mask_union);
    result_values[22] = Int32GetDatum((int32)scope_receipt.status);
    result_values[23] = Int32GetDatum((int32)compile_receipt.status);
    result_values[24] = Int32GetDatum((int32)projection_receipt.status);
    result_values[25] = Int32GetDatum((int32)codec_receipt.status);

    laplace_target_compile_result_destroy(&compiled);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 26);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
