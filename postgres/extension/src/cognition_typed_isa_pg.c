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
#include "utils/lsyscache.h"

#include "laplace/cognition_packet_compile.h"
#include "laplace/cognition_runtime.h"
#include "laplace/contract/isa.h"
#include "laplace/framework.h"
#include "laplace/isa.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_solve_isa);

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
        families[(size_t)index] = (uint32_t)family;
    }
    *family_count = (size_t)count;
    return families;
}

static void read_operator_program(
    HeapTupleHeader tuple,
    laplace_cognition_operator_program* program) {
    size_t family_count = 0u;
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
    program->eligible_relation_families = read_relation_families(
        tuple, &family_count);
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
        field = &fields[(size_t)index];
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
        constraint = &constraints[(size_t)index];
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
        state[(size_t)index] = DatumGetFloat8(values[index]);
    }
    return state;
}

static ArrayType* solution_array(const double* solution, size_t count) {
    Datum* values;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    size_t index;
    if (count == 0u || count > (size_t)INT_MAX) {
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

static void execute_cognition_isa(
    const laplace_framework_context* context,
    uint32_t* request_words,
    size_t request_word_count,
    uint32_t* result_words,
    size_t result_word_capacity,
    size_t* result_word_count,
    laplace_isa_receipt* receipt) {
    laplace_isa_value_view views[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_error error;
    laplace_isa_status status;

    memset(views, 0, sizeof(views));
    views[0].data = request_words;
    views[0].count = (uint64_t)request_word_count;
    views[0].capacity = (uint64_t)request_word_count;
    views[0].stride_bytes = (uint32_t)sizeof(*request_words);
    views[0].type = LAPLACE_ISA_VALUE_U32_VECTOR;
    views[1].data = result_words;
    views[1].count = 0u;
    views[1].capacity = (uint64_t)result_word_capacity;
    views[1].stride_bytes = (uint32_t)sizeof(*result_words);
    views[1].type = LAPLACE_ISA_VALUE_U32_VECTOR;

    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_COGNITION_SOLVE_PACKET;
    instruction.input_value = 0u;
    instruction.output_value = 1u;
    instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_COGNITION_SOLVE_PACKET;

    memset(&program, 0, sizeof(program));
    program.instructions = &instruction;
    program.values = views;
    program.context = context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;

    memset(receipt, 0, sizeof(*receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_isa_execute(&program, receipt, &error);
    if (status != LAPLACE_ISA_OK || views[1].count == 0u ||
        views[1].count > views[1].capacity || views[1].count > SIZE_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed cognition ISA execution failed"),
                 errdetail("isa_status=%d instruction=%llu value=%u",
                           (int)status,
                           (unsigned long long)error.instruction_index,
                           error.value_index)));
    }
    *result_word_count = (size_t)views[1].count;
    laplace_pg_persist_execution_receipt(
        receipt, (uint64_t)request_word_count, instruction.opcode);
}

Datum laplace_pg_cognition_solve_isa(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 context_fingerprint;
    laplace_cognition_runtime_request request;
    laplace_cognition_runtime_result decoded;
    laplace_isa_receipt isa_receipt;
    laplace_cognition_packet_status packet_status;
    size_t field_count = 0u;
    size_t constraint_count = 0u;
    size_t request_word_count = 0u;
    size_t encoded_word_count = 0u;
    size_t result_word_capacity = 0u;
    size_t result_word_count = 0u;
    uint32_t* request_words;
    uint32_t* result_words;
    double* solution;
    Datum result_values[18];
    bool result_nulls[18] = {false};
    HeapTuple result_tuple;

    memset(&request, 0, sizeof(request));
    memset(&decoded, 0, sizeof(decoded));
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if (laplace_framework_context_fingerprint(
            &context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace cognition execution context cannot be fingerprinted")));
    }

    read_operator_program(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &request.operator_program);
    if (memcmp(
            context_fingerprint.bytes,
            request.operator_program.context_fingerprint.bytes,
            sizeof(context_fingerprint.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace cognition operator program is not bound to the supplied execution context")));
    }
    request.fields = read_fields(PG_GETARG_ARRAYTYPE_P(2), &field_count);
    request.constraints = read_constraints(
        PG_GETARG_ARRAYTYPE_P(3), &constraint_count);
    read_solver_program(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(4)), &request.solver_program);
    request.initial_state = read_initial_state(
        PG_GETARG_ARRAYTYPE_P(5), field_count);
    request.field_count = (uint64_t)field_count;
    request.constraint_count = (uint64_t)constraint_count;
    request.initial_state_count = (uint64_t)field_count;

    packet_status = laplace_cognition_packet_request_required_words(
        &request, &request_word_count);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK || request_word_count == 0u ||
        request_word_count > SIZE_MAX / sizeof(*request_words)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace typed cognition request cannot be compiled to canonical ISA words"),
                 errdetail("packet_status=%d", (int)packet_status)));
    }
    request_words = (uint32_t*)palloc(sizeof(*request_words) * request_word_count);
    packet_status = laplace_cognition_packet_encode_request_words(
        &request, request_words, request_word_count, &encoded_word_count);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK ||
        encoded_word_count != request_word_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed cognition request compilation diverged from preflight"),
                 errdetail("packet_status=%d expected_words=%zu encoded_words=%zu",
                           (int)packet_status, request_word_count, encoded_word_count)));
    }
    packet_status = laplace_cognition_packet_required_result_words(
        request_words, request_word_count, &result_word_capacity);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK || result_word_capacity == 0u ||
        result_word_capacity > SIZE_MAX / sizeof(*result_words)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed cognition result capacity cannot be derived"),
                 errdetail("packet_status=%d", (int)packet_status)));
    }
    result_words = (uint32_t*)palloc0(sizeof(*result_words) * result_word_capacity);
    execute_cognition_isa(
        &context, request_words, request_word_count,
        result_words, result_word_capacity, &result_word_count, &isa_receipt);

    solution = (double*)palloc0(sizeof(*solution) * field_count);
    decoded.solution = solution;
    decoded.solution_capacity = (uint64_t)field_count;
    packet_status = laplace_cognition_packet_decode_result_words(
        result_words, result_word_count, &decoded);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK ||
        decoded.status != LAPLACE_COGNITION_RUNTIME_OK ||
        decoded.solution_count != (uint64_t)field_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed cognition ISA result failed canonical decode"),
                 errdetail("packet_status=%d runtime_status=%u solution_count=%llu",
                           (int)packet_status, decoded.status,
                           (unsigned long long)decoded.solution_count)));
    }

    result_values[0] = PointerGetDatum(solution_array(
        solution, (size_t)decoded.solution_count));
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
