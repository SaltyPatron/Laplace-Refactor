#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "laplace/cognition_packet.h"
#include "laplace/cognition_packet_compile.h"
#include "laplace/cognition_runtime.h"
#include "laplace/contract/isa.h"
#include "laplace/framework.h"
#include "laplace/isa.h"
#include "cognition_isa_pg.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_solve);

static void read_digest_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_digest256* digest,
    const char* field) {
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        digest, field);
}

static void read_id128_attribute(
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

static uint32_t read_u32_attribute(
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

static uint64_t read_u64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static double read_f64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
}

static Datum* deconstruct_exact_composite_array(
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
                 errmsg("Laplace cognition %s input must be a one-dimensional exact composite array",
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
                 errmsg("Laplace cognition %s input cannot be empty", type_name)));
    }
    return values;
}

static uint32_t* read_relation_families(
    HeapTupleHeader tuple,
    size_t* family_count) {
    ArrayType* array = DatumGetArrayTypeP(
        laplace_pg_required_composite_attribute(
            tuple, 6, "cognition operator eligible_relation_families"));
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    uint32_t* families;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != INT4OID) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace cognition eligible_relation_families must be an integer array")));
    }
    deconstruct_array(
        array, INT4OID, sizeof(int32), true, TYPALIGN_INT,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace cognition eligible_relation_families cannot be empty")));
    }
    families = (uint32_t*)palloc0(sizeof(*families) * (size_t)count);
    for (index = 0; index < count; ++index) {
        const int32 family = nulls[index] ? -1 : DatumGetInt32(values[index]);
        if (family <= 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace cognition relation families must be positive")));
        }
        families[index] = (uint32_t)family;
    }
    *family_count = (size_t)count;
    return families;
}

static void read_operator_program(
    HeapTupleHeader tuple,
    laplace_cognition_operator_program* program) {
    size_t family_count = 0u;
    uint32_t* families;
    memset(program, 0, sizeof(*program));
    read_digest_attribute(tuple, 1, &program->program_id,
                          "cognition operator program_id");
    read_digest_attribute(tuple, 2, &program->boundary_id,
                          "cognition operator boundary_id");
    read_digest_attribute(tuple, 3, &program->context_fingerprint,
                          "cognition operator context_fingerprint");
    read_digest_attribute(tuple, 4, &program->evidence_epoch,
                          "cognition operator evidence_epoch");
    read_digest_attribute(tuple, 5, &program->result_contract_fingerprint,
                          "cognition operator result_contract_fingerprint");
    families = read_relation_families(tuple, &family_count);
    program->eligible_relation_families = families;
    program->eligible_relation_family_count = family_count;
    program->eligible_source_mask = read_u32_attribute(
        tuple, 7, "cognition operator eligible_source_mask");
    program->flags = read_u32_attribute(tuple, 8, "cognition operator flags");
    program->numeric_tolerance = read_f64_attribute(
        tuple, 9, "cognition operator numeric_tolerance");
    program->version = read_u32_attribute(tuple, 10, "cognition operator version");
    program->reserved = 0u;
}

static laplace_cognition_operator_field* read_fields(
    ArrayType* array,
    size_t* field_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    laplace_cognition_operator_field* fields;
    int index;
    values = deconstruct_exact_composite_array(
        array, "cognition_operator_field", &count, &nulls);
    fields = (laplace_cognition_operator_field*)palloc0(
        sizeof(*fields) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_cognition_operator_field* field;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace cognition fields cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        field = &fields[index];
        read_digest_attribute(tuple, 1, &field->field_id,
                              "cognition field field_id");
        read_id128_attribute(tuple, 2, &field->entity_id,
                             "cognition field entity_id");
        read_digest_attribute(tuple, 3, &field->physicality_id,
                              "cognition field physicality_id");
        read_digest_attribute(tuple, 4, &field->role_id,
                              "cognition field role_id");
        read_digest_attribute(tuple, 5, &field->recipe_fingerprint,
                              "cognition field recipe_fingerprint");
        field->ordinal = read_u64_attribute(tuple, 6, "cognition field ordinal");
        field->value_dimension = read_u32_attribute(
            tuple, 7, "cognition field value_dimension");
        field->flags = read_u32_attribute(tuple, 8, "cognition field flags");
    }
    *field_count = (size_t)count;
    return fields;
}

static laplace_cognition_operator_constraint* read_constraints(
    ArrayType* array,
    size_t* constraint_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    laplace_cognition_operator_constraint* constraints;
    int index;
    values = deconstruct_exact_composite_array(
        array, "cognition_operator_constraint", &count, &nulls);
    constraints = (laplace_cognition_operator_constraint*)palloc0(
        sizeof(*constraints) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_cognition_operator_constraint* constraint;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace cognition constraints cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        constraint = &constraints[index];
        read_digest_attribute(tuple, 1, &constraint->constraint_id,
                              "cognition constraint constraint_id");
        read_digest_attribute(tuple, 2, &constraint->plane_id,
                              "cognition constraint plane_id");
        read_digest_attribute(tuple, 3, &constraint->law_fingerprint,
                              "cognition constraint law_fingerprint");
        read_digest_attribute(tuple, 4, &constraint->units_fingerprint,
                              "cognition constraint units_fingerprint");
        read_digest_attribute(tuple, 5, &constraint->evidence_root_id,
                              "cognition constraint evidence_root_id");
        read_digest_attribute(tuple, 6, &constraint->calculation_receipt_id,
                              "cognition constraint calculation_receipt_id");
        constraint->source_field_index = read_u64_attribute(
            tuple, 7, "cognition constraint source_field_index");
        constraint->target_field_index = read_u64_attribute(
            tuple, 8, "cognition constraint target_field_index");
        constraint->transport_scale = read_f64_attribute(
            tuple, 9, "cognition constraint transport_scale");
        constraint->transport_offset = read_f64_attribute(
            tuple, 10, "cognition constraint transport_offset");
        constraint->target_value = read_f64_attribute(
            tuple, 11, "cognition constraint target_value");
        constraint->precision = read_f64_attribute(
            tuple, 12, "cognition constraint precision");
        constraint->relation_family = read_u32_attribute(
            tuple, 13, "cognition constraint relation_family");
        constraint->source_class = read_u32_attribute(
            tuple, 14, "cognition constraint source_class");
        constraint->direction = read_u32_attribute(
            tuple, 15, "cognition constraint direction");
        constraint->transport_kind = read_u32_attribute(
            tuple, 16, "cognition constraint transport_kind");
        constraint->flags = read_u32_attribute(
            tuple, 17, "cognition constraint flags");
        constraint->reserved = 0u;
    }
    *constraint_count = (size_t)count;
    return constraints;
}

static void read_solver_program(
    HeapTupleHeader tuple,
    laplace_cognition_solver_program* program) {
    memset(program, 0, sizeof(*program));
    read_digest_attribute(tuple, 1, &program->program_id,
                          "cognition solver program_id");
    read_digest_attribute(tuple, 2, &program->result_contract_fingerprint,
                          "cognition solver result_contract_fingerprint");
    program->max_iterations = read_u64_attribute(
        tuple, 3, "cognition solver max_iterations");
    program->absolute_residual_tolerance = read_f64_attribute(
        tuple, 4, "cognition solver absolute_residual_tolerance");
    program->relative_residual_tolerance = read_f64_attribute(
        tuple, 5, "cognition solver relative_residual_tolerance");
    program->regularization = read_f64_attribute(
        tuple, 6, "cognition solver regularization");
    program->method = read_u32_attribute(tuple, 7, "cognition solver method");
    program->flags = read_u32_attribute(tuple, 8, "cognition solver flags");
    program->version = read_u32_attribute(tuple, 9, "cognition solver version");
    program->reserved = 0u;
}

static double* read_initial_state(
    ArrayType* array,
    size_t expected_count) {
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    double* state;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != FLOAT8OID) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace cognition initial state must be a double precision array")));
    }
    get_typlenbyvalalign(
        FLOAT8OID, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, FLOAT8OID, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0 || (size_t)count != expected_count) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace cognition initial-state cardinality must match the field set"),
                 errdetail("fields=%zu initial_state=%d", expected_count, count)));
    }
    state = (double*)palloc0(sizeof(*state) * expected_count);
    for (index = 0; index < count; ++index) {
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace cognition initial state cannot contain null values")));
        }
        state[index] = DatumGetFloat8(values[index]);
    }
    return state;
}

static void enforce_memory_grant(
    const laplace_framework_context* context,
    size_t field_count,
    size_t constraint_count) {
    uint64_t field_bytes;
    uint64_t constraint_bytes;
    uint64_t state_bytes;
    uint64_t required;
    if (field_count > UINT64_MAX / sizeof(laplace_cognition_operator_field) ||
        constraint_count > UINT64_MAX / sizeof(laplace_cognition_operator_constraint) ||
        field_count > UINT64_MAX / (4u * sizeof(double))) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace cognition working set size overflowed")));
    }
    field_bytes = (uint64_t)field_count * sizeof(laplace_cognition_operator_field);
    constraint_bytes =
        (uint64_t)constraint_count * sizeof(laplace_cognition_operator_constraint);
    state_bytes = (uint64_t)field_count * 4u * sizeof(double);
    if (field_bytes > UINT64_MAX / 2u || constraint_bytes > UINT64_MAX / 2u) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace cognition working set size overflowed")));
    }
    field_bytes *= 2u;
    constraint_bytes *= 2u;
    if (UINT64_MAX - field_bytes < constraint_bytes ||
        UINT64_MAX - (field_bytes + constraint_bytes) < state_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace cognition working set size overflowed")));
    }
    required = field_bytes + constraint_bytes + state_bytes;
    if (required > context->resource_grant.memory_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace cognition working set exceeds its execution-context memory grant"),
                 errdetail("required_bytes=%llu granted_bytes=%llu",
                           (unsigned long long)required,
                           (unsigned long long)context->resource_grant.memory_bytes)));
    }
}

static ArrayType* solution_array(const double* solution, size_t count) {
    Datum* values;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    size_t index;
    if (count > (size_t)INT_MAX ||
        count > SIZE_MAX / sizeof(*values)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace cognition solution exceeds PostgreSQL array cardinality")));
    }
    values = (Datum*)palloc(sizeof(*values) * count);
    for (index = 0u; index < count; ++index) {
        values[index] = Float8GetDatum(solution[index]);
    }
    get_typlenbyvalalign(
        FLOAT8OID, &type_length, &type_by_value, &type_alignment);
    return construct_array(
        values, (int)count, FLOAT8OID,
        type_length, type_by_value, type_alignment);
}

Datum laplace_pg_cognition_solve(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 context_fingerprint;
    laplace_cognition_operator_program operator_program;
    laplace_cognition_operator_field* fields;
    laplace_cognition_operator_constraint* constraints;
    laplace_cognition_solver_program solver_program;
    laplace_cognition_runtime_request request;
    laplace_cognition_runtime_result decoded;
    laplace_isa_receipt isa_receipt;
    laplace_cognition_packet_status packet_status;
    size_t field_count = 0u;
    size_t constraint_count = 0u;
    size_t request_word_capacity = 0u;
    size_t request_word_count = 0u;
    size_t result_word_capacity = 0u;
    size_t result_word_count = 0u;
    uint32_t* request_words;
    uint32_t* result_words;
    double* initial_state;
    double* solution;
    Datum result_values[18];
    bool result_nulls[18] = {false};
    HeapTuple result_tuple;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if (laplace_framework_context_fingerprint(
            &context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace cognition execution context cannot be fingerprinted")));
    }

    read_operator_program(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &operator_program);
    if (memcmp(
            context_fingerprint.bytes,
            operator_program.context_fingerprint.bytes,
            sizeof(context_fingerprint.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace cognition operator program is not bound to the supplied execution context")));
    }

    fields = read_fields(PG_GETARG_ARRAYTYPE_P(2), &field_count);
    constraints = read_constraints(PG_GETARG_ARRAYTYPE_P(3), &constraint_count);
    read_solver_program(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(4)), &solver_program);
    initial_state = read_initial_state(PG_GETARG_ARRAYTYPE_P(5), field_count);
    enforce_memory_grant(&context, field_count, constraint_count);

    memset(&request, 0, sizeof(request));
    request.operator_program = operator_program;
    request.fields = fields;
    request.constraints = constraints;
    request.initial_state = initial_state;
    request.field_count = (uint64_t)field_count;
    request.constraint_count = (uint64_t)constraint_count;
    request.initial_state_count = (uint64_t)field_count;
    request.solver_program = solver_program;
    packet_status = laplace_cognition_packet_request_required_words(
        &request, &request_word_capacity);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK ||
        request_word_capacity == 0u ||
        request_word_capacity > SIZE_MAX / sizeof(*request_words)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed cognition request could not be lowered to the canonical packet"),
                 errdetail("packet_status=%d", (int)packet_status)));
    }
    request_words = (uint32_t*)palloc0(
        sizeof(*request_words) * request_word_capacity);
    packet_status = laplace_cognition_packet_encode_request_words(
        &request, request_words, request_word_capacity, &request_word_count);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK ||
        request_word_count != request_word_capacity) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed cognition request packet encoding failed"),
                 errdetail("packet_status=%d words=%zu expected=%zu",
                           (int)packet_status, request_word_count,
                           request_word_capacity)));
    }
    packet_status = laplace_cognition_packet_required_result_words(
        request_words, request_word_count, &result_word_capacity);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK ||
        result_word_capacity == 0u ||
        result_word_capacity > SIZE_MAX / sizeof(*result_words)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed cognition result packet preflight failed"),
                 errdetail("packet_status=%d", (int)packet_status)));
    }
    result_words = (uint32_t*)palloc0(
        sizeof(*result_words) * result_word_capacity);
    laplace_pg_cognition_execute_words(
        &context, request_words, request_word_count,
        result_words, result_word_capacity, &result_word_count, &isa_receipt);

    solution = (double*)palloc0(sizeof(*solution) * field_count);
    memset(&decoded, 0, sizeof(decoded));
    decoded.solution = solution;
    decoded.solution_capacity = (uint64_t)field_count;
    packet_status = laplace_cognition_packet_decode_result_words(
        result_words, result_word_count, &decoded);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK ||
        decoded.status != LAPLACE_COGNITION_RUNTIME_OK ||
        decoded.solution_count != (uint64_t)field_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace cognition ISA result did not decode to the typed result contract"),
                 errdetail("packet_status=%d runtime_status=%u solution_count=%llu expected=%zu",
                           (int)packet_status, decoded.status,
                           (unsigned long long)decoded.solution_count,
                           field_count)));
    }

    result_values[0] = PointerGetDatum(solution_array(solution, field_count));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.operator_receipt.receipt_id.bytes,
        sizeof(decoded.operator_receipt.receipt_id.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.operator_receipt.operator_id.bytes,
        sizeof(decoded.operator_receipt.operator_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.operator_receipt.program_fingerprint.bytes,
        sizeof(decoded.operator_receipt.program_fingerprint.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.operator_receipt.field_set_fingerprint.bytes,
        sizeof(decoded.operator_receipt.field_set_fingerprint.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.operator_receipt.constraint_set_fingerprint.bytes,
        sizeof(decoded.operator_receipt.constraint_set_fingerprint.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.solver_receipt.receipt_id.bytes,
        sizeof(decoded.solver_receipt.receipt_id.bytes)));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.solver_receipt.program_fingerprint.bytes,
        sizeof(decoded.solver_receipt.program_fingerprint.bytes)));
    result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.solver_receipt.input_fingerprint.bytes,
        sizeof(decoded.solver_receipt.input_fingerprint.bytes)));
    result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.solver_receipt.iteration_trace_fingerprint.bytes,
        sizeof(decoded.solver_receipt.iteration_trace_fingerprint.bytes)));
    result_values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        decoded.solver_receipt.output_fingerprint.bytes,
        sizeof(decoded.solver_receipt.output_fingerprint.bytes)));
    result_values[11] = laplace_pg_numeric_from_uint64(
        decoded.solver_receipt.iteration_count);
    result_values[12] = Float8GetDatum(decoded.solver_receipt.initial_residual_l2);
    result_values[13] = Float8GetDatum(decoded.solver_receipt.final_residual_l2);
    result_values[14] = Float8GetDatum(decoded.solver_receipt.final_energy);
    result_values[15] = Int32GetDatum((int32)decoded.solver_receipt.disposition);
    result_values[16] = Int32GetDatum((int32)decoded.solver_receipt.status);
    result_values[17] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt.receipt_id.bytes, sizeof(isa_receipt.receipt_id.bytes)));
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 18);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
