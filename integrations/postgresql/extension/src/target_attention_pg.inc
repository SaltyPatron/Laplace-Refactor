#include "laplace/target_attention_projection.h"
#include "laplace/target_compile.h"

#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/lsyscache.h"

#include "set_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_target_attention_compile);

typedef struct laplace_pg_target_attention_job_bundle {
    laplace_target_compile_job job;
    uint64_t head_rank;
} laplace_pg_target_attention_job_bundle;

static void laplace_pg_target_attention_read_digest_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_digest256* digest,
    const char* field) {
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        digest, field);
}

static void laplace_pg_target_attention_read_id128_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_id128* id,
    const char* field) {
    bytea* value = DatumGetByteaPP(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(id->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly 16 bytes", field)));
    }
    memcpy(id->bytes, VARDATA_ANY(value), sizeof(id->bytes));
}

static uint32_t laplace_pg_target_attention_read_u32_attribute(
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

static uint64_t laplace_pg_target_attention_read_u64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static double laplace_pg_target_attention_read_f64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
}

static Datum* laplace_pg_target_attention_deconstruct_composite_array(
    ArrayType* array,
    const char* type_name,
    int* count,
    bool** nulls) {
    const Oid type_oid = laplace_pg_composite_type_oid(type_name);
    Datum* values = NULL;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace target attention %s input must be a one-dimensional exact composite array",
                        type_name)));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, nulls, count);
    if (*count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target attention %s input cannot be empty", type_name)));
    }
    return values;
}

static uint32_t* laplace_pg_target_attention_read_relation_families(
    HeapTupleHeader tuple,
    size_t* family_count) {
    ArrayType* array = DatumGetArrayTypeP(
        laplace_pg_required_composite_attribute(
            tuple, 6, "target operator eligible_relation_families"));
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    uint32_t* families;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != INT4OID) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace target operator eligible_relation_families must be an integer array")));
    }
    deconstruct_array(
        array, INT4OID, sizeof(int32), true, TYPALIGN_INT,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target operator eligible_relation_families cannot be empty")));
    }
    families = (uint32_t*)palloc0(sizeof(*families) * (size_t)count);
    for (index = 0; index < count; ++index) {
        const int32 family = nulls[index] ? -1 : DatumGetInt32(values[index]);
        if (family <= 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target operator relation families must be positive")));
        }
        families[index] = (uint32_t)family;
    }
    *family_count = (size_t)count;
    return families;
}

static void laplace_pg_target_attention_read_operator_program(
    HeapTupleHeader tuple,
    laplace_cognition_operator_program* program) {
    size_t family_count = 0u;
    uint32_t* families;
    memset(program, 0, sizeof(*program));
    laplace_pg_target_attention_read_digest_attribute(
        tuple, 1, &program->program_id, "target operator program_id");
    laplace_pg_target_attention_read_digest_attribute(
        tuple, 2, &program->boundary_id, "target operator boundary_id");
    laplace_pg_target_attention_read_digest_attribute(
        tuple, 3, &program->context_fingerprint, "target operator context_fingerprint");
    laplace_pg_target_attention_read_digest_attribute(
        tuple, 4, &program->evidence_epoch, "target operator evidence_epoch");
    laplace_pg_target_attention_read_digest_attribute(
        tuple, 5, &program->result_contract_fingerprint,
        "target operator result_contract_fingerprint");
    families = laplace_pg_target_attention_read_relation_families(tuple, &family_count);
    program->eligible_relation_families = families;
    program->eligible_relation_family_count = family_count;
    program->eligible_source_mask = laplace_pg_target_attention_read_u32_attribute(
        tuple, 7, "target operator eligible_source_mask");
    program->flags = laplace_pg_target_attention_read_u32_attribute(
        tuple, 8, "target operator flags");
    program->numeric_tolerance = laplace_pg_target_attention_read_f64_attribute(
        tuple, 9, "target operator numeric_tolerance");
    program->version = laplace_pg_target_attention_read_u32_attribute(
        tuple, 10, "target operator version");
    program->reserved = 0u;
}

static laplace_cognition_operator_field* laplace_pg_target_attention_read_fields(
    ArrayType* array,
    size_t* field_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    laplace_cognition_operator_field* fields;
    int index;
    values = laplace_pg_target_attention_deconstruct_composite_array(
        array, "cognition_operator_field", &count, &nulls);
    fields = (laplace_cognition_operator_field*)palloc0(
        sizeof(*fields) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_cognition_operator_field* field;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace target attention fields cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        field = &fields[index];
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 1, &field->field_id, "target field field_id");
        laplace_pg_target_attention_read_id128_attribute(
            tuple, 2, &field->entity_id, "target field entity_id");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 3, &field->physicality_id, "target field physicality_id");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 4, &field->role_id, "target field role_id");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 5, &field->recipe_fingerprint, "target field recipe_fingerprint");
        field->ordinal = laplace_pg_target_attention_read_u64_attribute(
            tuple, 6, "target field ordinal");
        field->value_dimension = laplace_pg_target_attention_read_u32_attribute(
            tuple, 7, "target field value_dimension");
        field->flags = laplace_pg_target_attention_read_u32_attribute(
            tuple, 8, "target field flags");
    }
    *field_count = (size_t)count;
    return fields;
}

static laplace_cognition_operator_constraint*
laplace_pg_target_attention_read_constraints(
    ArrayType* array,
    size_t* constraint_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    laplace_cognition_operator_constraint* constraints;
    int index;
    values = laplace_pg_target_attention_deconstruct_composite_array(
        array, "cognition_operator_constraint", &count, &nulls);
    constraints = (laplace_cognition_operator_constraint*)palloc0(
        sizeof(*constraints) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_cognition_operator_constraint* constraint;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace target attention constraints cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        constraint = &constraints[index];
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 1, &constraint->constraint_id, "target constraint constraint_id");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 2, &constraint->plane_id, "target constraint plane_id");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 3, &constraint->law_fingerprint, "target constraint law_fingerprint");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 4, &constraint->units_fingerprint,
            "target constraint units_fingerprint");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 5, &constraint->evidence_root_id,
            "target constraint evidence_root_id");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 6, &constraint->calculation_receipt_id,
            "target constraint calculation_receipt_id");
        constraint->source_field_index = laplace_pg_target_attention_read_u64_attribute(
            tuple, 7, "target constraint source_field_index");
        constraint->target_field_index = laplace_pg_target_attention_read_u64_attribute(
            tuple, 8, "target constraint target_field_index");
        constraint->transport_scale = laplace_pg_target_attention_read_f64_attribute(
            tuple, 9, "target constraint transport_scale");
        constraint->transport_offset = laplace_pg_target_attention_read_f64_attribute(
            tuple, 10, "target constraint transport_offset");
        constraint->target_value = laplace_pg_target_attention_read_f64_attribute(
            tuple, 11, "target constraint target_value");
        constraint->precision = laplace_pg_target_attention_read_f64_attribute(
            tuple, 12, "target constraint precision");
        constraint->relation_family = laplace_pg_target_attention_read_u32_attribute(
            tuple, 13, "target constraint relation_family");
        constraint->source_class = laplace_pg_target_attention_read_u32_attribute(
            tuple, 14, "target constraint source_class");
        constraint->direction = laplace_pg_target_attention_read_u32_attribute(
            tuple, 15, "target constraint direction");
        constraint->transport_kind = laplace_pg_target_attention_read_u32_attribute(
            tuple, 16, "target constraint transport_kind");
        constraint->flags = laplace_pg_target_attention_read_u32_attribute(
            tuple, 17, "target constraint flags");
        constraint->reserved = 0u;
    }
    *constraint_count = (size_t)count;
    return constraints;
}

static laplace_pg_target_attention_job_bundle*
laplace_pg_target_attention_read_jobs(
    ArrayType* array,
    const laplace_digest256* context_fingerprint,
    size_t* job_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    laplace_pg_target_attention_job_bundle* bundles;
    int index;
    values = laplace_pg_target_attention_deconstruct_composite_array(
        array, "target_operator_job", &count, &nulls);
    bundles = (laplace_pg_target_attention_job_bundle*)palloc0(
        sizeof(*bundles) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_pg_target_attention_job_bundle* bundle;
        Datum program_datum;
        Datum fields_datum;
        Datum constraints_datum;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace target operator jobs cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        bundle = &bundles[index];
        bundle->job.target_role = laplace_pg_target_attention_read_u32_attribute(
            tuple, 1, "target job target_role");
        if (bundle->job.target_role != LAPLACE_TARGET_ROLE_COMPATIBILITY_QK &&
            bundle->job.target_role != LAPLACE_TARGET_ROLE_CONTRIBUTION_VO) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target attention accepts only QK compatibility and VO contribution jobs")));
        }
        bundle->job.layer_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 2, "target job layer_index");
        bundle->job.head_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 3, "target job head_index");
        bundle->job.expert_index = laplace_pg_target_attention_read_u32_attribute(
            tuple, 4, "target job expert_index");
        laplace_pg_target_attention_read_digest_attribute(
            tuple, 5, &bundle->job.role_fingerprint,
            "target job role_fingerprint");
        program_datum = laplace_pg_required_composite_attribute(
            tuple, 6, "target job operator_program");
        laplace_pg_target_attention_read_operator_program(
            DatumGetHeapTupleHeader(program_datum), &bundle->job.operator_program);
        if (memcmp(
                bundle->job.operator_program.context_fingerprint.bytes,
                context_fingerprint->bytes, sizeof(context_fingerprint->bytes)) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target operator program is not bound to the supplied execution context")));
        }
        fields_datum = laplace_pg_required_composite_attribute(
            tuple, 7, "target job fields");
        bundle->job.fields = laplace_pg_target_attention_read_fields(
            DatumGetArrayTypeP(fields_datum), &bundle->job.field_count);
        constraints_datum = laplace_pg_required_composite_attribute(
            tuple, 8, "target job constraints");
        bundle->job.constraints = laplace_pg_target_attention_read_constraints(
            DatumGetArrayTypeP(constraints_datum), &bundle->job.constraint_count);
        bundle->head_rank = laplace_pg_target_attention_read_u64_attribute(
            tuple, 9, "target job head_rank");
        if (bundle->head_rank == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace target job head_rank must be positive")));
        }
        bundle->job.flags = 0u;
        bundle->job.reserved = 0u;
    }
    *job_count = (size_t)count;
    return bundles;
}

static uint64_t laplace_pg_target_attention_rank_for_coordinate(
    const laplace_pg_target_attention_job_bundle* bundles,
    size_t job_count,
    uint32_t layer_index,
    uint32_t head_index,
    uint32_t expert_index) {
    uint64_t rank = 0u;
    size_t matches = 0u;
    size_t index;
    for (index = 0u; index < job_count; ++index) {
        const laplace_target_compile_job* job = &bundles[index].job;
        if (job->layer_index == layer_index &&
            job->head_index == head_index &&
            job->expert_index == expert_index) {
            if (rank == 0u) rank = bundles[index].head_rank;
            if (rank != bundles[index].head_rank) {
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("Laplace QK and VO jobs for one target head must declare the same rank")));
            }
            ++matches;
        }
    }
    if (matches != 2u || rank == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target attention requires exactly one QK and one VO job per target head"),
                 errdetail("layer=%u head=%u expert=%u matching_jobs=%zu",
                           layer_index, head_index, expert_index, matches)));
    }
    return rank;
}

static ArrayType* laplace_pg_target_attention_float8_array(
    const double* values,
    size_t count,
    const char* field) {
    Datum* datums;
    size_t index;
    if (count == 0u || count > (size_t)INT_MAX ||
        count > SIZE_MAX / sizeof(*datums)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace target attention %s exceeds PostgreSQL array cardinality",
                        field)));
    }
    datums = (Datum*)palloc(sizeof(*datums) * count);
    for (index = 0u; index < count; ++index) {
        datums[index] = Float8GetDatum(values[index]);
    }
    return construct_array(
        datums, (int)count, FLOAT8OID, sizeof(float8), true, TYPALIGN_DOUBLE);
}

static ArrayType* laplace_pg_target_attention_u64_array(
    const uint64_t* values,
    size_t count,
    const char* field) {
    Datum* datums;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    size_t index;
    if (count == 0u || count > (size_t)INT_MAX ||
        count > SIZE_MAX / sizeof(*datums)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace target attention %s exceeds PostgreSQL array cardinality",
                        field)));
    }
    datums = (Datum*)palloc(sizeof(*datums) * count);
    for (index = 0u; index < count; ++index) {
        datums[index] = laplace_pg_numeric_from_uint64(values[index]);
    }
    get_typlenbyvalalign(NUMERICOID, &type_length, &type_by_value, &type_alignment);
    return construct_array(
        datums, (int)count, NUMERICOID,
        type_length, type_by_value, type_alignment);
}

static ArrayType* laplace_pg_target_attention_u32_array(
    const uint32_t* values,
    size_t count,
    const char* field) {
    Datum* datums;
    size_t index;
    if (count == 0u || count > (size_t)INT_MAX ||
        count > SIZE_MAX / sizeof(*datums)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace target attention %s exceeds PostgreSQL array cardinality",
                        field)));
    }
    datums = (Datum*)palloc(sizeof(*datums) * count);
    for (index = 0u; index < count; ++index) {
        if (values[index] > (uint32_t)INT32_MAX) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace target attention %s value exceeds PostgreSQL integer range",
                            field)));
        }
        datums[index] = Int32GetDatum((int32)values[index]);
    }
    return construct_array(
        datums, (int)count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

static ArrayType* laplace_pg_target_attention_digest_array(
    const laplace_digest256* values,
    size_t count,
    const char* field) {
    Datum* datums;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    size_t index;
    if (count == 0u || count > (size_t)INT_MAX ||
        count > SIZE_MAX / sizeof(*datums)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace target attention %s exceeds PostgreSQL array cardinality",
                        field)));
    }
    datums = (Datum*)palloc(sizeof(*datums) * count);
    for (index = 0u; index < count; ++index) {
        datums[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            values[index].bytes, sizeof(values[index].bytes)));
    }
    get_typlenbyvalalign(BYTEAOID, &type_length, &type_by_value, &type_alignment);
    return construct_array(
        datums, (int)count, BYTEAOID,
        type_length, type_by_value, type_alignment);
}

static void laplace_pg_target_attention_read_projection_vector(
    const laplace_target_attention_projection_result* projection,
    size_t head_index,
    laplace_target_attention_projection_status (*reader)(
        const laplace_target_attention_projection_result*,
        size_t, double*, size_t, size_t*),
    double* destination,
    size_t destination_capacity,
    size_t* written) {
    size_t required = 0u;
    laplace_target_attention_projection_status status = reader(
        projection, head_index, NULL, 0u, &required);
    if (status != LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT ||
        required == 0u || required > destination_capacity) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target attention projection tensor preflight failed"),
                 errdetail("head=%zu status=%d required=%zu capacity=%zu",
                           head_index, (int)status, required, destination_capacity)));
    }
    status = reader(
        projection, head_index, destination, destination_capacity, &required);
    if (status != LAPLACE_TARGET_ATTENTION_PROJECTION_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target attention projection tensor read failed"),
                 errdetail("head=%zu status=%d", head_index, (int)status)));
    }
    *written = required;
}

Datum laplace_pg_target_attention_compile(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 context_fingerprint;
    laplace_target_compile_request compile_request;
    laplace_target_compile_receipt compile_receipt;
    laplace_target_compile_result* compiled = NULL;
    laplace_target_attention_projection_request projection_request;
    laplace_target_attention_projection_receipt projection_receipt;
    laplace_target_attention_projection_result* projection = NULL;
    laplace_pg_target_attention_job_bundle* bundles;
    laplace_target_compile_job* jobs;
    laplace_target_compile_slot_receipt* slots;
    laplace_target_attention_head_spec* head_specs;
    laplace_target_attention_head_receipt* head_receipts;
    laplace_digest256* head_receipt_ids;
    laplace_digest256* qk_factorization_ids;
    laplace_digest256* vo_factorization_ids;
    uint64_t* head_offsets;
    uint64_t* head_ranks;
    uint32_t* layer_indices;
    uint32_t* head_indices;
    uint32_t* expert_indices;
    double* q_values;
    double* k_values;
    double* v_values;
    double* o_values;
    double* qk_residuals;
    double* vo_residuals;
    double* embedding_values;
    size_t job_count = 0u;
    size_t slot_count;
    size_t head_count = 0u;
    size_t embedding_count = 0u;
    size_t tensor_count = 0u;
    size_t index;
    size_t head_cursor = 0u;
    size_t value_cursor = 0u;
    uint64_t hidden_width;
    double relative_tolerance;
    bool require_exact;
    Datum result_values[27];
    bool result_nulls[27] = {false};
    HeapTuple result_tuple;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if (laplace_framework_context_fingerprint(
            &context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target attention execution context cannot be fingerprinted")));
    }

    memset(&compile_request, 0, sizeof(compile_request));
    laplace_pg_read_digest(PG_GETARG_DATUM(1), &compile_request.evidence_boundary,
                           "target compile evidence_boundary");
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &compile_request.evidence_epoch,
                           "target compile evidence_epoch");
    laplace_pg_read_digest(PG_GETARG_DATUM(3), &compile_request.recipe_fingerprint,
                           "target compile recipe_fingerprint");
    laplace_pg_read_digest(PG_GETARG_DATUM(4),
                           &compile_request.target_contract_fingerprint,
                           "target compile target_contract_fingerprint");
    bundles = laplace_pg_target_attention_read_jobs(
        PG_GETARG_ARRAYTYPE_P(5), &context_fingerprint, &job_count);
    hidden_width = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(6), "target attention hidden_width");
    relative_tolerance = PG_GETARG_FLOAT8(7);
    require_exact = PG_GETARG_BOOL(8);
    if (hidden_width == 0u || !isfinite(relative_tolerance) ||
        relative_tolerance <= 0.0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target attention hidden width and tolerance must be positive")));
    }

    jobs = (laplace_target_compile_job*)palloc0(sizeof(*jobs) * job_count);
    for (index = 0u; index < job_count; ++index) jobs[index] = bundles[index].job;
    compile_request.jobs = jobs;
    compile_request.job_count = job_count;
    compile_request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    compile_request.version = LAPLACE_TARGET_COMPILE_VERSION;

    memset(&compile_receipt, 0, sizeof(compile_receipt));
    if (laplace_target_compile_execute(
            &compile_request, &compiled, &compile_receipt) != LAPLACE_TARGET_COMPILE_OK ||
        compiled == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target operator compilation failed"),
                 errdetail("status=%u", compile_receipt.status)));
    }

    slot_count = laplace_target_compile_result_slot_count(compiled);
    if (slot_count != job_count || slot_count == 0u) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target compile slot inventory does not match submitted jobs"),
                 errdetail("jobs=%zu slots=%zu", job_count, slot_count)));
    }
    slots = (laplace_target_compile_slot_receipt*)palloc0(sizeof(*slots) * slot_count);
    for (index = 0u; index < slot_count; ++index) {
        if (laplace_target_compile_result_slot_receipt(
                compiled, index, &slots[index]) != LAPLACE_TARGET_COMPILE_OK) {
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target compile slot receipt read failed")));
        }
        if (slots[index].target_role == LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) ++head_count;
    }
    if (head_count == 0u || slot_count != head_count * 2u) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace target attention requires paired QK and VO jobs")));
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
                     errmsg("Laplace target attention QK job has no matching VO job")));
        }
        ++head_cursor;
    }
    if (head_cursor != head_count) {
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target attention head pairing failed")));
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
                     errmsg("Laplace target attention projection failed"),
                     errdetail("status=%d", (int)projection_status)));
        }
    }

    if (laplace_target_attention_projection_embedding(
            projection, NULL, 0u, &embedding_count) !=
            LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT ||
        embedding_count == 0u || embedding_count > SIZE_MAX / sizeof(*embedding_values)) {
        laplace_target_attention_projection_result_destroy(&projection);
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target attention embedding preflight failed")));
    }
    embedding_values = (double*)palloc(sizeof(*embedding_values) * embedding_count);
    if (laplace_target_attention_projection_embedding(
            projection, embedding_values, embedding_count, &embedding_count) !=
            LAPLACE_TARGET_ATTENTION_PROJECTION_OK) {
        laplace_target_attention_projection_result_destroy(&projection);
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target attention embedding read failed")));
    }

    head_receipts = (laplace_target_attention_head_receipt*)palloc0(
        sizeof(*head_receipts) * head_count);
    head_offsets = (uint64_t*)palloc0(sizeof(*head_offsets) * (head_count + 1u));
    head_ranks = (uint64_t*)palloc0(sizeof(*head_ranks) * head_count);
    layer_indices = (uint32_t*)palloc0(sizeof(*layer_indices) * head_count);
    head_indices = (uint32_t*)palloc0(sizeof(*head_indices) * head_count);
    expert_indices = (uint32_t*)palloc0(sizeof(*expert_indices) * head_count);
    head_receipt_ids = (laplace_digest256*)palloc0(sizeof(*head_receipt_ids) * head_count);
    qk_factorization_ids = (laplace_digest256*)palloc0(
        sizeof(*qk_factorization_ids) * head_count);
    vo_factorization_ids = (laplace_digest256*)palloc0(
        sizeof(*vo_factorization_ids) * head_count);
    qk_residuals = (double*)palloc0(sizeof(*qk_residuals) * head_count);
    vo_residuals = (double*)palloc0(sizeof(*vo_residuals) * head_count);

    for (index = 0u; index < head_count; ++index) {
        uint64_t values_for_head;
        if (laplace_target_attention_projection_head_receipt(
                projection, index, &head_receipts[index]) !=
                LAPLACE_TARGET_ATTENTION_PROJECTION_OK) {
            laplace_target_attention_projection_result_destroy(&projection);
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target attention head receipt read failed")));
        }
        if (head_receipts[index].head_rank > UINT64_MAX / hidden_width) {
            laplace_target_attention_projection_result_destroy(&projection);
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                     errmsg("Laplace target attention tensor cardinality overflowed")));
        }
        values_for_head = head_receipts[index].head_rank * hidden_width;
        if (head_offsets[index] > UINT64_MAX - values_for_head) {
            laplace_target_attention_projection_result_destroy(&projection);
            laplace_target_compile_result_destroy(&compiled);
            ereport(ERROR,
                    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                     errmsg("Laplace target attention tensor offset overflowed")));
        }
        head_offsets[index + 1u] = head_offsets[index] + values_for_head;
        head_ranks[index] = head_receipts[index].head_rank;
        layer_indices[index] = head_receipts[index].layer_index;
        head_indices[index] = head_receipts[index].head_index;
        expert_indices[index] = head_receipts[index].expert_index;
        head_receipt_ids[index] = head_receipts[index].head_projection_id;
        qk_factorization_ids[index] = head_receipts[index].qk_factorization_id;
        vo_factorization_ids[index] = head_receipts[index].vo_factorization_id;
        qk_residuals[index] = head_receipts[index].qk_relative_residual;
        vo_residuals[index] = head_receipts[index].vo_relative_residual;
    }
    if (head_offsets[head_count] > (uint64_t)SIZE_MAX ||
        head_offsets[head_count] > (uint64_t)INT_MAX) {
        laplace_target_attention_projection_result_destroy(&projection);
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace target attention concatenated tensors exceed PostgreSQL array capacity")));
    }
    tensor_count = (size_t)head_offsets[head_count];
    q_values = (double*)palloc(sizeof(*q_values) * tensor_count);
    k_values = (double*)palloc(sizeof(*k_values) * tensor_count);
    v_values = (double*)palloc(sizeof(*v_values) * tensor_count);
    o_values = (double*)palloc(sizeof(*o_values) * tensor_count);
    for (index = 0u; index < head_count; ++index) {
        size_t written = 0u;
        const size_t capacity = tensor_count - value_cursor;
        laplace_pg_target_attention_read_projection_vector(
            projection, index, laplace_target_attention_projection_q,
            q_values + value_cursor, capacity, &written);
        if ((uint64_t)written != head_offsets[index + 1u] - head_offsets[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace target attention Q tensor cardinality drifted")));
        }
        laplace_pg_target_attention_read_projection_vector(
            projection, index, laplace_target_attention_projection_k,
            k_values + value_cursor, capacity, &written);
        laplace_pg_target_attention_read_projection_vector(
            projection, index, laplace_target_attention_projection_v,
            v_values + value_cursor, capacity, &written);
        laplace_pg_target_attention_read_projection_vector(
            projection, index, laplace_target_attention_projection_o,
            o_values + value_cursor, capacity, &written);
        value_cursor = (size_t)head_offsets[index + 1u];
    }
    if (value_cursor != tensor_count) {
        laplace_target_attention_projection_result_destroy(&projection);
        laplace_target_compile_result_destroy(&compiled);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace target attention tensor concatenation did not complete")));
    }

    result_values[0] = PointerGetDatum(laplace_pg_target_attention_float8_array(
        embedding_values, embedding_count, "embedding"));
    result_values[1] = PointerGetDatum(laplace_pg_target_attention_float8_array(
        q_values, tensor_count, "q"));
    result_values[2] = PointerGetDatum(laplace_pg_target_attention_float8_array(
        k_values, tensor_count, "k"));
    result_values[3] = PointerGetDatum(laplace_pg_target_attention_float8_array(
        v_values, tensor_count, "v"));
    result_values[4] = PointerGetDatum(laplace_pg_target_attention_float8_array(
        o_values, tensor_count, "o"));
    result_values[5] = PointerGetDatum(laplace_pg_target_attention_u64_array(
        head_offsets, head_count + 1u, "head_offsets"));
    result_values[6] = PointerGetDatum(laplace_pg_target_attention_u64_array(
        head_ranks, head_count, "head_ranks"));
    result_values[7] = PointerGetDatum(laplace_pg_target_attention_u32_array(
        layer_indices, head_count, "layer_indices"));
    result_values[8] = PointerGetDatum(laplace_pg_target_attention_u32_array(
        head_indices, head_count, "head_indices"));
    result_values[9] = PointerGetDatum(laplace_pg_target_attention_u32_array(
        expert_indices, head_count, "expert_indices"));
    result_values[10] = PointerGetDatum(laplace_pg_target_attention_digest_array(
        head_receipt_ids, head_count, "head_receipt_ids"));
    result_values[11] = PointerGetDatum(laplace_pg_target_attention_digest_array(
        qk_factorization_ids, head_count, "qk_factorization_ids"));
    result_values[12] = PointerGetDatum(laplace_pg_target_attention_digest_array(
        vo_factorization_ids, head_count, "vo_factorization_ids"));
    result_values[13] = PointerGetDatum(laplace_pg_target_attention_float8_array(
        qk_residuals, head_count, "qk_relative_residuals"));
    result_values[14] = PointerGetDatum(laplace_pg_target_attention_float8_array(
        vo_residuals, head_count, "vo_relative_residuals"));
    result_values[15] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        compile_receipt.receipt_id.bytes, sizeof(compile_receipt.receipt_id.bytes)));
    result_values[16] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        compile_receipt.request_fingerprint.bytes,
        sizeof(compile_receipt.request_fingerprint.bytes)));
    result_values[17] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        projection_receipt.projection_id.bytes,
        sizeof(projection_receipt.projection_id.bytes)));
    result_values[18] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        projection_receipt.embedding_fingerprint.bytes,
        sizeof(projection_receipt.embedding_fingerprint.bytes)));
    result_values[19] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        projection_receipt.head_set_fingerprint.bytes,
        sizeof(projection_receipt.head_set_fingerprint.bytes)));
    result_values[20] = laplace_pg_numeric_from_uint64(projection_receipt.field_count);
    result_values[21] = laplace_pg_numeric_from_uint64(projection_receipt.hidden_width);
    result_values[22] = laplace_pg_numeric_from_uint64(
        projection_receipt.semantic_basis_rank);
    result_values[23] = laplace_pg_numeric_from_uint64(
        projection_receipt.null_basis_rank);
    result_values[24] = Float8GetDatum(projection_receipt.max_relative_residual);
    result_values[25] = Int32GetDatum((int32)compile_receipt.status);
    result_values[26] = Int32GetDatum((int32)projection_receipt.status);

    laplace_target_attention_projection_result_destroy(&projection);
    laplace_target_compile_result_destroy(&compiled);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 27);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
