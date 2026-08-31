#include "laplace/isa.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "blake3.h"
#include "laplace/framework.h"
#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/standing_calculation.h"
#include "laplace/highway.h"
#include "laplace/reference_mapping.h"
#include "laplace/reference_topology.h"
#include "laplace/source_profile.h"
#include "laplace/trajectory.h"
#include "laplace/world_admission.h"

static const uint8_t PROGRAM_DOMAIN[] = "laplace-isa-program-v1";
static const uint8_t INPUT_DOMAIN[] = "laplace-isa-input-v1";
static const uint8_t OUTPUT_DOMAIN[] = "laplace-isa-output-v1";
static const uint8_t RECEIPT_DOMAIN[] = "laplace-isa-receipt-v1";

typedef laplace_isa_status (*laplace_isa_operation_validate_fn)(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error);

typedef laplace_isa_status (*laplace_isa_operation_execute_fn)(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction);

typedef struct laplace_isa_operation_binding {
    uint32_t opcode;
    uint32_t input_type;
    uint32_t output_type;
    uint16_t instruction_version;
    uint16_t introduced_minor;
    laplace_isa_operation_validate_fn validate;
    laplace_isa_operation_execute_fn execute;
} laplace_isa_operation_binding;

#define DECLARE_OPERATION(symbol, handler, opcode_value, version_value, minor_value, input_value, output_value, module_value) \
    static laplace_isa_status validate_##handler( \
        const laplace_isa_program*, const laplace_isa_instruction*, \
        uint64_t, laplace_isa_error*); \
    static laplace_isa_status execute_##handler( \
        laplace_isa_program*, const laplace_isa_instruction*);

LAPLACE_ISA_OPERATION_REGISTRY(DECLARE_OPERATION)

#undef DECLARE_OPERATION

#define BIND_OPERATION(symbol, handler, opcode_value, version_value, minor_value, input_value, output_value, module_value) \
    {opcode_value, input_value, output_value, version_value, minor_value, \
     validate_##handler, execute_##handler},

static const laplace_isa_operation_binding OPERATION_BINDINGS[] = {
    LAPLACE_ISA_OPERATION_REGISTRY(BIND_OPERATION)
};

#undef BIND_OPERATION

static const laplace_isa_operation_binding* operation_binding_find(uint32_t opcode) {
    size_t low = 0;
    size_t high = sizeof(OPERATION_BINDINGS) / sizeof(OPERATION_BINDINGS[0]);
#if defined(LAPLACE_TEST_DISPATCH_FIRST_OPERATION_ONLY)
    high = 1;
#endif
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        if (OPERATION_BINDINGS[middle].opcode < opcode) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low < sizeof(OPERATION_BINDINGS) / sizeof(OPERATION_BINDINGS[0]) &&
#if defined(LAPLACE_TEST_DISPATCH_FIRST_OPERATION_ONLY)
        low < 1u &&
#endif
        OPERATION_BINDINGS[low].opcode == opcode) {
        return &OPERATION_BINDINGS[low];
    }
    return NULL;
}

static void clear_error(laplace_isa_error* error) {
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
        error->status = LAPLACE_ISA_OK;
        error->instruction_index = UINT64_MAX;
        error->value_index = UINT32_MAX;
    }
}

static laplace_isa_status fail(
    laplace_isa_error* error,
    laplace_isa_status status,
    uint64_t instruction_index,
    uint32_t value_index) {
    if (error != NULL) {
        error->status = status;
        error->instruction_index = instruction_index;
        error->value_index = value_index;
        error->reserved = 0;
    }
    return status;
}

static uint32_t value_element_bytes(uint32_t type) {
    switch (type) {
        case LAPLACE_ISA_VALUE_U32_VECTOR:
            return (uint32_t)sizeof(uint32_t);
        case LAPLACE_ISA_VALUE_ID128_VECTOR:
            return (uint32_t)sizeof(laplace_id128);
        case LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR:
            return (uint32_t)sizeof(laplace_trajectory_carrier);
        case LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR:
            return (uint32_t)sizeof(laplace_composition_occurrence);
        case LAPLACE_ISA_VALUE_HIGHWAY_KEY_VECTOR:
            return (uint32_t)sizeof(laplace_highway_key);
        case LAPLACE_ISA_VALUE_HIGHWAY_COORDINATE_VECTOR:
            return (uint32_t)sizeof(laplace_highway_coordinate);
        case LAPLACE_ISA_VALUE_HIGHWAY_REGISTRY_RECEIPT_VECTOR:
            return (uint32_t)sizeof(laplace_highway_registry_receipt);
        case LAPLACE_ISA_VALUE_EVIDENCE_LINEAGE_RECORD_VECTOR:
            return (uint32_t)sizeof(laplace_evidence_lineage_record);
        case LAPLACE_ISA_VALUE_EVIDENCE_ROOT_RECORD_VECTOR:
            return (uint32_t)sizeof(laplace_evidence_root_record);
        case LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECORD_VECTOR:
            return (uint32_t)sizeof(laplace_evidence_testimony_record);
        case LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECEIPT_VECTOR:
            return (uint32_t)sizeof(laplace_evidence_testimony_receipt);
        case LAPLACE_ISA_VALUE_SOURCE_PROFILE_MANIFEST_VECTOR:
            return (uint32_t)sizeof(laplace_source_profile_manifest);
        case LAPLACE_ISA_VALUE_SOURCE_PROFILE_RECEIPT_VECTOR:
            return (uint32_t)sizeof(laplace_source_profile_receipt);
        case LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECORD_VECTOR:
            return (uint32_t)sizeof(laplace_world_admission_record);
        case LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECEIPT_VECTOR:
            return (uint32_t)sizeof(laplace_world_admission_receipt);
        case LAPLACE_ISA_VALUE_REFERENCE_CANDIDATE_VECTOR:
            return (uint32_t)sizeof(laplace_reference_candidate);
        case LAPLACE_ISA_VALUE_REFERENCE_RECORD_VECTOR:
            return (uint32_t)sizeof(laplace_reference_record);
        case LAPLACE_ISA_VALUE_REFERENCE_MAPPING_CANDIDATE_VECTOR:
            return (uint32_t)sizeof(laplace_reference_mapping_candidate);
        case LAPLACE_ISA_VALUE_REFERENCE_MAPPING_RECORD_VECTOR:
            return (uint32_t)sizeof(laplace_reference_mapping_record);
        case LAPLACE_ISA_VALUE_STANDING_PERIOD_INPUT_VECTOR:
            return (uint32_t)sizeof(laplace_standing_period_input);
        case LAPLACE_ISA_VALUE_STANDING_PERIOD_RESULT_VECTOR:
            return (uint32_t)sizeof(laplace_standing_period_result);
        default:
            return 0;
    }
}

static int value_span(
    const laplace_isa_value_view* value,
    uint64_t count,
    uintptr_t* begin,
    uintptr_t* end) {
    const uint32_t element_bytes = value_element_bytes(value->type);
    uint64_t last_offset;
    uint64_t span;
    uintptr_t address;

    if (begin == NULL || end == NULL || element_bytes == 0) {
        return 0;
    }
    if (count == 0) {
        *begin = (uintptr_t)value->data;
        *end = (uintptr_t)value->data;
        return 1;
    }
    if (value->data == NULL || value->stride_bytes < element_bytes) {
        return 0;
    }
    if (count - 1 > UINT64_MAX / value->stride_bytes) {
        return 0;
    }
    last_offset = (count - 1) * value->stride_bytes;
    if (UINT64_MAX - last_offset < element_bytes) {
        return 0;
    }
    span = last_offset + element_bytes;
    if (span > SIZE_MAX) {
        return 0;
    }
    address = (uintptr_t)value->data;
    if (address > UINTPTR_MAX - (uintptr_t)span) {
        return 0;
    }
    *begin = address;
    *end = address + (uintptr_t)span;
    return 1;
}

static int ranges_overlap(
    uintptr_t left_begin,
    uintptr_t left_end,
    uintptr_t right_begin,
    uintptr_t right_end) {
    if (left_begin == left_end || right_begin == right_end) {
        return 0;
    }
    return left_begin < right_end && right_begin < left_end;
}

static uint8_t* value_element(laplace_isa_value_view* value, uint64_t index) {
    return (uint8_t*)value->data + (size_t)(index * value->stride_bytes);
}

static const uint8_t* const_value_element(
    const laplace_isa_value_view* value,
    uint64_t index) {
    return (const uint8_t*)value->data + (size_t)(index * value->stride_bytes);
}

static void copy_lineage_inputs(
    const laplace_isa_value_view* input,
    laplace_evidence_lineage_record* records) {
    uint64_t index;
    for (index = 0u; index < input->count; ++index) {
        memcpy(&records[(size_t)index], const_value_element(input, index),
               sizeof(records[index]));
    }
}

static void copy_testimony_inputs(
    const laplace_isa_value_view* input,
    laplace_evidence_testimony_record* records) {
    uint64_t index;
    for (index = 0u; index < input->count; ++index) {
        memcpy(&records[(size_t)index], const_value_element(input, index),
               sizeof(records[index]));
    }
}

static void copy_standing_inputs(
    const laplace_isa_value_view* input,
    laplace_standing_period_input* periods) {
    uint64_t index;
    for (index = 0u; index < input->count; ++index) {
        memcpy(&periods[(size_t)index], const_value_element(input, index),
               sizeof(periods[index]));
    }
}

static void copy_source_profile_inputs(
    const laplace_isa_value_view* input,
    laplace_source_profile_manifest* profiles) {
    uint64_t index;
    for (index = 0u; index < input->count; ++index) {
        memcpy(&profiles[(size_t)index], const_value_element(input, index),
               sizeof(profiles[index]));
    }
}

static void copy_world_admission_inputs(
    const laplace_isa_value_view* input,
    laplace_world_admission_record* admissions) {
    uint64_t index;
    for (index = 0u; index < input->count; ++index) {
        memcpy(&admissions[(size_t)index], const_value_element(input, index),
               sizeof(admissions[index]));
    }
}

static void copy_reference_candidates(
    const laplace_isa_value_view* input,
    laplace_reference_candidate* candidates) {
    uint64_t index;
    for (index = 0u; index < input->count; ++index) {
        memcpy(&candidates[(size_t)index], const_value_element(input, index),
               sizeof(candidates[index]));
    }
}

static void copy_reference_mapping_candidates(
    const laplace_isa_value_view* input,
    laplace_reference_mapping_candidate* candidates) {
    uint64_t index;
    for (index = 0u; index < input->count; ++index) {
        memcpy(&candidates[(size_t)index], const_value_element(input, index),
               sizeof(candidates[index]));
    }
}

static laplace_isa_status validate_value(
    const laplace_isa_value_view* value,
    uint32_t value_index,
    laplace_isa_error* error) {
    uintptr_t begin;
    uintptr_t end;

    if (value_element_bytes(value->type) == 0) {
        return fail(error, LAPLACE_ISA_VALUE_TYPE_MISMATCH, UINT64_MAX, value_index);
    }
    if (value->flags != LAPLACE_ISA_KNOWN_VALUE_FLAGS || value->reserved != 0 ||
        value->count > value->capacity) {
        return fail(error, LAPLACE_ISA_VALUE_INVALID, UINT64_MAX, value_index);
    }
    if (!value_span(value, value->capacity, &begin, &end)) {
        return fail(error, LAPLACE_ISA_VALUE_INVALID, UINT64_MAX, value_index);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_instruction(
    const laplace_isa_program* program,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_instruction* instruction =
        &program->instructions[(size_t)instruction_index];
    laplace_isa_value_view* input;
    laplace_isa_value_view* output;
    const laplace_isa_operation_binding* binding;

    if (instruction->flags != LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS) {
        return fail(error, LAPLACE_ISA_UNKNOWN_FLAGS, instruction_index, UINT32_MAX);
    }
    if ((uint64_t)instruction->input_value >= program->value_count) {
        return fail(error, LAPLACE_ISA_REGISTER_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    if ((uint64_t)instruction->output_value >= program->value_count) {
        return fail(error, LAPLACE_ISA_REGISTER_OUT_OF_RANGE,
                    instruction_index, instruction->output_value);
    }
    if (instruction->input_value == instruction->output_value) {
        return fail(error, LAPLACE_ISA_VALUE_OVERLAP,
                    instruction_index, instruction->output_value);
    }

    input = &program->values[instruction->input_value];
    output = &program->values[instruction->output_value];
    binding = operation_binding_find(instruction->opcode);
    if (binding == NULL) {
        return fail(error, LAPLACE_ISA_UNKNOWN_OPCODE,
                    instruction_index, UINT32_MAX);
    }
    if (program->minor < binding->introduced_minor ||
        instruction->version != binding->instruction_version) {
        return fail(error, LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION,
                    instruction_index, UINT32_MAX);
    }
    if (input->type != binding->input_type) {
        return fail(error, LAPLACE_ISA_VALUE_TYPE_MISMATCH,
                    instruction_index, instruction->input_value);
    }
    if (output->type != binding->output_type) {
        return fail(error, LAPLACE_ISA_VALUE_TYPE_MISMATCH,
                    instruction_index, instruction->output_value);
    }
    return binding->validate(program, instruction, instruction_index, error);
}

static laplace_isa_status validate_equal_cardinality_capacity(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input =
        &program->values[instruction->input_value];
    const laplace_isa_value_view* output =
        &program->values[instruction->output_value];
    if (output->capacity < input->count) {
        return fail(error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_identity_codepoint_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input =
        &program->values[instruction->input_value];
    uint64_t index;
    laplace_isa_status status = validate_equal_cardinality_capacity(
        program, instruction, instruction_index, error);
    if (status != LAPLACE_ISA_OK) {
        return status;
    }
    for (index = 0; index < input->count; ++index) {
        uint32_t position;
        memcpy(&position, const_value_element(input, index), sizeof(position));
        if (position > LAPLACE_UNICODE_POSITION_MAXIMUM) {
            return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                        instruction_index, instruction->input_value);
        }
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_trajectory_composition_decode_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input =
        &program->values[instruction->input_value];
    uint64_t logical_ordinal = 1;
    uint64_t index;
    laplace_isa_status status = validate_equal_cardinality_capacity(
        program, instruction, instruction_index, error);
    if (status != LAPLACE_ISA_OK) {
        return status;
    }
    for (index = 0; index < input->count; ++index) {
        laplace_trajectory_carrier carrier;
        laplace_composition_occurrence occurrence;
        laplace_trajectory_status trajectory_status;
        memcpy(&carrier, const_value_element(input, index), sizeof(carrier));
        trajectory_status = laplace_trajectory_composition_decode_one(
            &carrier, logical_ordinal, &occurrence);
        if (trajectory_status != LAPLACE_TRAJECTORY_OK ||
            UINT64_MAX - logical_ordinal < occurrence.run_length) {
            return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                        instruction_index, instruction->input_value);
        }
        logical_ordinal += occurrence.run_length;
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_highway_coordinate_calculate_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input =
        &program->values[instruction->input_value];
    laplace_isa_status status = validate_equal_cardinality_capacity(
        program, instruction, instruction_index, error);
    uint64_t index;
    if (status != LAPLACE_ISA_OK) {
        return status;
    }
    for (index = 0; index < input->count; ++index) {
        laplace_highway_key key;
        laplace_highway_coordinate coordinate;
        memcpy(&key, const_value_element(input, index), sizeof(key));
        if (laplace_highway_coordinate_calculate(&key, &coordinate) !=
            LAPLACE_HIGHWAY_OK) {
            return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                        instruction_index, instruction->input_value);
        }
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_highway_registry_materialize_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input =
        &program->values[instruction->input_value];
    laplace_isa_status status = validate_equal_cardinality_capacity(
        program, instruction, instruction_index, error);
    uint64_t index;
    if (status != LAPLACE_ISA_OK) {
        return status;
    }
    for (index = 0; index < input->count; ++index) {
        uint32_t requested_version;
        memcpy(&requested_version, const_value_element(input, index),
               sizeof(requested_version));
        if (requested_version != LAPLACE_HIGHWAY_REGISTRY_VERSION) {
            return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                        instruction_index, instruction->input_value);
        }
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_evidence_record_lineage_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input = &program->values[instruction->input_value];
    const laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_evidence_root_record* temporary;
    laplace_evidence_lineage_record* contiguous_input = NULL;
    const laplace_evidence_lineage_record* lineage_input;
    laplace_evidence_lineage_receipt receipt;
    laplace_evidence_lineage_error lineage_error;
    size_t count = 0u;
    laplace_evidence_lineage_status status;
    uint64_t temporary_bytes;
    if (input->count > SIZE_MAX || output->capacity > SIZE_MAX ||
        output->capacity == 0u) {
        return fail(error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    if (output->capacity > UINT64_MAX / sizeof(*temporary) ||
        input->count > UINT64_MAX / sizeof(*contiguous_input)) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    temporary_bytes = output->capacity * sizeof(*temporary);
    lineage_input = (const laplace_evidence_lineage_record*)input->data;
    if (input->stride_bytes != sizeof(*contiguous_input) ||
        (uintptr_t)input->data % _Alignof(laplace_evidence_lineage_record) != 0u) {
        if (UINT64_MAX - temporary_bytes < input->count * sizeof(*contiguous_input)) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        temporary_bytes += input->count * sizeof(*contiguous_input);
        if (temporary_bytes > program->context->resource_grant.memory_bytes) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        contiguous_input = (laplace_evidence_lineage_record*)calloc(
            (size_t)input->count, sizeof(*contiguous_input));
        if (contiguous_input == NULL) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        copy_lineage_inputs(input, contiguous_input);
        lineage_input = contiguous_input;
    } else if (temporary_bytes > program->context->resource_grant.memory_bytes) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    temporary = (laplace_evidence_root_record*)calloc(
        (size_t)output->capacity, sizeof(*temporary));
    if (temporary == NULL) {
        free(contiguous_input);
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&lineage_error, 0, sizeof(lineage_error));
    status = laplace_evidence_record_lineage_batch(
        lineage_input,
        (size_t)input->count,
        program->context->resource_grant.memory_bytes - temporary_bytes,
        temporary, (size_t)output->capacity, &count, &receipt, &lineage_error);
    free(temporary);
    free(contiguous_input);
    if (status == LAPLACE_EVIDENCE_LINEAGE_CYCLE) {
        return fail(error, LAPLACE_ISA_DEPENDENCE_CYCLE,
                    instruction_index, instruction->input_value);
    }
    if (status == LAPLACE_EVIDENCE_LINEAGE_RESOURCE_INSUFFICIENT) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    if (status == LAPLACE_EVIDENCE_LINEAGE_CAPACITY_INSUFFICIENT) {
        return fail(error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    if (status != LAPLACE_EVIDENCE_LINEAGE_OK) {
        return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_evidence_record_testimony_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input = &program->values[instruction->input_value];
    const laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_evidence_testimony_record* contiguous = NULL;
    const laplace_evidence_testimony_record* testimony_input;
    laplace_evidence_testimony_receipt receipt;
    laplace_evidence_testimony_error testimony_error;
    laplace_evidence_testimony_status status;
    uint64_t temporary_bytes = 0u;
    if (input->count == 0u || input->count > SIZE_MAX || output->capacity < 1u) {
        return fail(error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    testimony_input = (const laplace_evidence_testimony_record*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_evidence_testimony_record) != 0u) {
        if (input->count > UINT64_MAX / sizeof(*contiguous)) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        temporary_bytes = input->count * sizeof(*contiguous);
        if (temporary_bytes > program->context->resource_grant.memory_bytes) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        contiguous = (laplace_evidence_testimony_record*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        copy_testimony_inputs(input, contiguous);
        testimony_input = contiguous;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&testimony_error, 0, sizeof(testimony_error));
    status = laplace_evidence_record_testimony_batch(
        testimony_input, (size_t)input->count, &receipt, &testimony_error);
    free(contiguous);
    if (status != LAPLACE_EVIDENCE_TESTIMONY_OK) {
        return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_evidence_calculate_standing_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input = &program->values[instruction->input_value];
    const laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_standing_period_input* contiguous = NULL;
    const laplace_standing_period_input* standing_input;
    laplace_standing_period_result result;
    laplace_standing_error standing_error;
    uint64_t required_bytes;
    laplace_standing_status status;
    if (input->count == 0u || input->count > SIZE_MAX || output->capacity < 1u ||
        input->count > UINT64_MAX /
            (sizeof(laplace_standing_period_input) +
             3u * sizeof(laplace_standing_event) + sizeof(laplace_digest256))) {
        return fail(error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    required_bytes = input->count *
        (sizeof(laplace_standing_period_input) +
         3u * sizeof(laplace_standing_event) + sizeof(laplace_digest256));
    if (required_bytes > program->context->resource_grant.memory_bytes) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    standing_input = (const laplace_standing_period_input*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_standing_period_input) != 0u) {
        contiguous = (laplace_standing_period_input*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        copy_standing_inputs(input, contiguous);
        standing_input = contiguous;
    }
    memset(&result, 0, sizeof(result));
    memset(&standing_error, 0, sizeof(standing_error));
    status = laplace_standing_calculate_period_batch(
        standing_input, (size_t)input->count, &result, &standing_error);
    free(contiguous);
    if (status == LAPLACE_STANDING_RESOURCE_INSUFFICIENT ||
        status == LAPLACE_STANDING_OVERFLOW) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    if (status != LAPLACE_STANDING_OK) {
        return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_source_profile_validate_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input = &program->values[instruction->input_value];
    const laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_source_profile_manifest* contiguous = NULL;
    const laplace_source_profile_manifest* profile_input;
    laplace_source_profile_receipt receipt;
    laplace_source_profile_error profile_error;
    laplace_source_profile_status status;
    uint64_t temporary_bytes = 0u;
    if (input->count == 0u || input->count > SIZE_MAX || output->capacity < 1u) {
        return fail(error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    profile_input = (const laplace_source_profile_manifest*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_source_profile_manifest) != 0u) {
        if (input->count > UINT64_MAX / sizeof(*contiguous)) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        temporary_bytes = input->count * sizeof(*contiguous);
        if (temporary_bytes > program->context->resource_grant.memory_bytes) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        contiguous = (laplace_source_profile_manifest*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        copy_source_profile_inputs(input, contiguous);
        profile_input = contiguous;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&profile_error, 0, sizeof(profile_error));
    status = laplace_source_profile_validate_batch(
        profile_input, (size_t)input->count, &receipt, &profile_error);
    free(contiguous);
    if (status != LAPLACE_SOURCE_PROFILE_OK) {
        return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_world_admission_close_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input = &program->values[instruction->input_value];
    const laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_world_admission_record* contiguous = NULL;
    const laplace_world_admission_record* admission_input;
    laplace_world_admission_receipt receipt;
    laplace_world_admission_error admission_error;
    laplace_world_admission_status status;
    uint64_t temporary_bytes = 0u;
    if (input->count == 0u || input->count > SIZE_MAX || output->capacity < 1u) {
        return fail(error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    admission_input = (const laplace_world_admission_record*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_world_admission_record) != 0u) {
        if (input->count > UINT64_MAX / sizeof(*contiguous)) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        temporary_bytes = input->count * sizeof(*contiguous);
        if (temporary_bytes > program->context->resource_grant.memory_bytes) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        contiguous = (laplace_world_admission_record*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        copy_world_admission_inputs(input, contiguous);
        admission_input = contiguous;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&admission_error, 0, sizeof(admission_error));
    status = laplace_world_admission_close_batch(
        admission_input, (size_t)input->count, &receipt, &admission_error);
    free(contiguous);
    if (status != LAPLACE_WORLD_ADMISSION_OK) {
        return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_reference_topology_resolve_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_reference_candidate* contiguous = NULL;
    const laplace_reference_candidate* candidates;
    laplace_reference_record* records;
    laplace_reference_topology_receipt receipt;
    laplace_reference_topology_error topology_error;
    laplace_reference_topology_status topology_status;
    uint64_t temporary_bytes;
    laplace_isa_status status = validate_equal_cardinality_capacity(
        program, instruction, instruction_index, error);
    if (status != LAPLACE_ISA_OK) {
        return status;
    }
    if (input->count == 0u || input->count > SIZE_MAX ||
        input->count > UINT64_MAX / sizeof(*records)) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    temporary_bytes = input->count * sizeof(*records);
    candidates = (const laplace_reference_candidate*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_reference_candidate) != 0u) {
        if (input->count > UINT64_MAX / sizeof(*contiguous) ||
            UINT64_MAX - temporary_bytes < input->count * sizeof(*contiguous)) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        temporary_bytes += input->count * sizeof(*contiguous);
        contiguous = (laplace_reference_candidate*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        copy_reference_candidates(input, contiguous);
        candidates = contiguous;
    }
    if (temporary_bytes > program->context->resource_grant.memory_bytes) {
        free(contiguous);
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    records = (laplace_reference_record*)calloc(
        (size_t)input->count, sizeof(*records));
    if (records == NULL) {
        free(contiguous);
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&topology_error, 0, sizeof(topology_error));
    topology_status = laplace_reference_topology_resolve_batch(
        candidates, (size_t)input->count, records, &receipt, &topology_error);
    free(records);
    free(contiguous);
    if (topology_status == LAPLACE_REFERENCE_TOPOLOGY_MEMORY_FAILURE) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    if (topology_status != LAPLACE_REFERENCE_TOPOLOGY_OK) {
        return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_reference_mapping_resolve_batch(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_reference_mapping_candidate* contiguous = NULL;
    const laplace_reference_mapping_candidate* candidates;
    laplace_reference_mapping_record* records;
    laplace_reference_mapping_receipt receipt;
    laplace_reference_mapping_error mapping_error;
    laplace_reference_mapping_status mapping_status;
    uint64_t temporary_bytes;
    laplace_isa_status status = validate_equal_cardinality_capacity(
        program, instruction, instruction_index, error);
    if (status != LAPLACE_ISA_OK) {
        return status;
    }
    if (input->count == 0u || input->count > SIZE_MAX ||
        input->count > UINT64_MAX / sizeof(*records)) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    temporary_bytes = input->count * sizeof(*records);
    candidates = (const laplace_reference_mapping_candidate*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data %
            _Alignof(laplace_reference_mapping_candidate) != 0u) {
        if (input->count > UINT64_MAX / sizeof(*contiguous) ||
            UINT64_MAX - temporary_bytes < input->count * sizeof(*contiguous)) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        temporary_bytes += input->count * sizeof(*contiguous);
        contiguous = (laplace_reference_mapping_candidate*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                        instruction_index, instruction->input_value);
        }
        copy_reference_mapping_candidates(input, contiguous);
        candidates = contiguous;
    }
    if (temporary_bytes > program->context->resource_grant.memory_bytes) {
        free(contiguous);
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    records = (laplace_reference_mapping_record*)calloc(
        (size_t)input->count, sizeof(*records));
    if (records == NULL) {
        free(contiguous);
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->output_value);
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&mapping_error, 0, sizeof(mapping_error));
    mapping_status = laplace_reference_mapping_resolve_batch(
        candidates, (size_t)input->count, records, &receipt, &mapping_error);
    free(records);
    free(contiguous);
    if (mapping_status == LAPLACE_REFERENCE_MAPPING_MEMORY_FAILURE) {
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    instruction_index, instruction->input_value);
    }
    if (mapping_status != LAPLACE_REFERENCE_MAPPING_OK) {
        return fail(error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
                    instruction_index, instruction->input_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status validate_global_ranges(
    const laplace_isa_program* program,
    laplace_isa_error* error) {
    uint64_t left_index;

    for (left_index = 0; left_index < program->instruction_count; ++left_index) {
        const laplace_isa_instruction* left =
            &program->instructions[(size_t)left_index];
        const laplace_isa_value_view* left_output =
            &program->values[left->output_value];
        const laplace_isa_value_view* left_input =
            &program->values[left->input_value];
        uintptr_t left_output_begin;
        uintptr_t left_output_end;
        uintptr_t left_input_begin;
        uintptr_t left_input_end;
        uint64_t right_index;

        if (!value_span(left_output, left_output->capacity,
                        &left_output_begin, &left_output_end) ||
            !value_span(left_input, left_input->count,
                        &left_input_begin, &left_input_end)) {
            return fail(error, LAPLACE_ISA_VALUE_INVALID,
                        left_index, left->output_value);
        }
        if (ranges_overlap(left_output_begin, left_output_end,
                           left_input_begin, left_input_end)) {
            return fail(error, LAPLACE_ISA_VALUE_OVERLAP,
                        left_index, left->output_value);
        }

        for (right_index = left_index + 1;
             right_index < program->instruction_count;
             ++right_index) {
            const laplace_isa_instruction* right =
                &program->instructions[(size_t)right_index];
            const laplace_isa_value_view* right_output =
                &program->values[right->output_value];
            const laplace_isa_value_view* right_input =
                &program->values[right->input_value];
            uintptr_t right_output_begin;
            uintptr_t right_output_end;
            uintptr_t right_input_begin;
            uintptr_t right_input_end;

            if (!value_span(right_output, right_output->capacity,
                            &right_output_begin, &right_output_end) ||
                !value_span(right_input, right_input->count,
                            &right_input_begin, &right_input_end)) {
                return fail(error, LAPLACE_ISA_VALUE_INVALID,
                            right_index, right->output_value);
            }
            if (ranges_overlap(left_output_begin, left_output_end,
                               right_output_begin, right_output_end) ||
                ranges_overlap(left_output_begin, left_output_end,
                               right_input_begin, right_input_end) ||
                ranges_overlap(right_output_begin, right_output_end,
                               left_input_begin, left_input_end)) {
                return fail(error, LAPLACE_ISA_VALUE_OVERLAP,
                            right_index, right->output_value);
            }
        }
    }
    return LAPLACE_ISA_OK;
}

laplace_isa_status laplace_isa_validate(
    const laplace_isa_program* program,
    laplace_isa_error* error) {
    uint64_t value_index;
    uint64_t instruction_index;
    uint64_t instruction_validation_count;
    laplace_isa_status status;

    clear_error(error);
    if (program == NULL ||
        (program->instructions == NULL && program->instruction_count != 0) ||
        (program->values == NULL && program->value_count != 0)) {
        return fail(error, LAPLACE_ISA_INVALID_ARGUMENT, UINT64_MAX, UINT32_MAX);
    }
    if (laplace_framework_context_validate(program->context) !=
        LAPLACE_FRAMEWORK_OK) {
        return fail(error, LAPLACE_ISA_CONTEXT_INVALID, UINT64_MAX, UINT32_MAX);
    }
    if (program->major != LAPLACE_ISA_MAJOR || program->minor > LAPLACE_ISA_MINOR) {
        return fail(error, LAPLACE_ISA_UNSUPPORTED_VERSION, UINT64_MAX, UINT32_MAX);
    }
    if (program->flags != LAPLACE_ISA_KNOWN_PROGRAM_FLAGS || program->reserved != 0 ||
        program->receipt_detail != LAPLACE_ISA_RECEIPT_DETAIL_FULL) {
        return fail(error, LAPLACE_ISA_UNKNOWN_FLAGS, UINT64_MAX, UINT32_MAX);
    }
    if (program->instruction_count == 0) {
        return fail(error, LAPLACE_ISA_EMPTY_PROGRAM, UINT64_MAX, UINT32_MAX);
    }
    if (program->instruction_count > SIZE_MAX / sizeof(*program->instructions) ||
        program->value_count > SIZE_MAX / sizeof(*program->values) ||
        program->value_count > UINT32_MAX) {
        return fail(error, LAPLACE_ISA_VALUE_INVALID, UINT64_MAX, UINT32_MAX);
    }

    for (value_index = 0; value_index < program->value_count; ++value_index) {
        status = validate_value(&program->values[(size_t)value_index],
                                (uint32_t)value_index, error);
        if (status != LAPLACE_ISA_OK) {
            return status;
        }
    }
#if defined(LAPLACE_TEST_VALIDATE_FIRST_INSTRUCTION_ONLY)
    instruction_validation_count = 1;
#else
    instruction_validation_count = program->instruction_count;
#endif
    for (instruction_index = 0;
         instruction_index < instruction_validation_count;
         ++instruction_index) {
        status = validate_instruction(program, instruction_index, error);
        if (status != LAPLACE_ISA_OK) {
            return status;
        }
    }
    return validate_global_ranges(program, error);
}

static void hash_u16(blake3_hasher* hasher, uint16_t value) {
    const uint8_t bytes[2] = {
        (uint8_t)(value & UINT16_C(0xff)),
        (uint8_t)(value >> 8)
    };
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value & UINT32_C(0xff)),
        (uint8_t)((value >> 8) & UINT32_C(0xff)),
        (uint8_t)((value >> 16) & UINT32_C(0xff)),
        (uint8_t)(value >> 24)
    };
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_binary64(blake3_hasher* hasher, double value) {
    uint64_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    hash_u64(hasher, bits);
}

static void hash_standing_state(
    blake3_hasher* hasher, const laplace_standing_state* state) {
    blake3_hasher_update(hasher, state->state_id.bytes, 32u);
    blake3_hasher_update(hasher, state->coordinate_id.bytes, 32u);
    blake3_hasher_update(hasher, state->arena_scope_id.bytes, 32u);
    blake3_hasher_update(hasher, state->prior_state_id.bytes, 32u);
    blake3_hasher_update(hasher, state->epoch_id.bytes, 32u);
    blake3_hasher_update(hasher, state->rating_recipe_id.bytes, 32u);
    hash_binary64(hasher, state->rating);
    hash_binary64(hasher, state->rating_deviation);
    hash_binary64(hasher, state->volatility);
    hash_u64(hasher, state->eligible_match_count);
    hash_u64(hasher, state->period_ordinal);
    hash_u32(hasher, state->rating_recipe_version);
    hash_u32(hasher, state->flags);
}

static void hash_standing_event(
    blake3_hasher* hasher, const laplace_standing_event* event) {
    blake3_hasher_update(hasher, event->event_id.bytes, 32u);
    blake3_hasher_update(hasher, event->participant_coordinate_id.bytes, 32u);
    blake3_hasher_update(hasher, event->participant_prior_state_id.bytes, 32u);
    hash_standing_state(hasher, &event->opponent_prior_state);
    blake3_hasher_update(hasher, event->period_id.bytes, 32u);
    blake3_hasher_update(hasher, event->eligible_root_id.bytes, 32u);
    blake3_hasher_update(hasher, event->outcome_mapping_id.bytes, 32u);
    blake3_hasher_update(hasher, event->context_id.bytes, 32u);
    blake3_hasher_update(hasher, event->valid_time_id.bytes, 32u);
    hash_u64(hasher, event->score_numerator);
    hash_u64(hasher, event->score_denominator);
    hash_u32(hasher, event->outcome_kind);
    hash_u32(hasher, event->flags);
}

static void hash_standing_input(
    blake3_hasher* hasher,
    const laplace_standing_period_input* input) {
    hash_standing_state(hasher, &input->prior_state);
    hash_standing_event(hasher, &input->event);
    hash_binary64(hasher, input->volatility_constraint);
    hash_binary64(hasher, input->convergence_tolerance);
}

#if !defined(LAPLACE_TEST_STANDING_INPUT_ORDER_RECEIPT)
static int compare_standing_input_identity(const void* left, const void* right) {
    const laplace_standing_period_input* first =
        (const laplace_standing_period_input*)left;
    const laplace_standing_period_input* second =
        (const laplace_standing_period_input*)right;
    return memcmp(first->event.event_id.bytes, second->event.event_id.bytes, 32u);
}
#endif

static void hash_standing_receipt(
    blake3_hasher* hasher, const laplace_standing_period_receipt* receipt) {
    blake3_hasher_update(hasher, receipt->receipt_id.bytes, 32u);
    blake3_hasher_update(hasher, receipt->prior_state_id.bytes, 32u);
    blake3_hasher_update(hasher, receipt->successor_state_id.bytes, 32u);
    blake3_hasher_update(hasher, receipt->period_id.bytes, 32u);
    blake3_hasher_update(hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(hasher, receipt->eligible_event_count);
    hash_u64(hasher, receipt->prior_match_count);
    hash_u64(hasher, receipt->successor_match_count);
    hash_u32(hasher, receipt->volatility_iterations);
    hash_u32(hasher, receipt->version);
    hash_u32(hasher, receipt->status);
    hash_u32(hasher, receipt->flags);
}

static void hash_reference_mapping_candidate(
    blake3_hasher* hasher,
    const laplace_reference_mapping_candidate* candidate) {
    const laplace_highway_coordinate* coordinates[2] = {
        &candidate->left_coordinate, &candidate->right_coordinate};
    size_t coordinate_index;
    blake3_hasher_update(hasher, candidate->boundary_id.bytes, 32u);
    blake3_hasher_update(hasher, candidate->source_profile_id.bytes, 32u);
    blake3_hasher_update(hasher, candidate->left_reference_id.bytes, 32u);
    blake3_hasher_update(hasher, candidate->right_reference_id.bytes, 32u);
    for (coordinate_index = 0u; coordinate_index < 2u; ++coordinate_index) {
        const laplace_highway_coordinate* coordinate =
            coordinates[coordinate_index];
        blake3_hasher_update(hasher, coordinate->coordinate.bytes, 16u);
        blake3_hasher_update(
            hasher, coordinate->collision_fingerprint.bytes, 32u);
        hash_u32(hasher, coordinate->kind);
        hash_u32(hasher, coordinate->reserved);
        hash_u64(hasher, coordinate->version);
    }
    blake3_hasher_update(hasher, candidate->relation_id.bytes, 16u);
    blake3_hasher_update(hasher, candidate->row_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, candidate->left_field_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, candidate->left_value_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, candidate->right_field_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, candidate->right_value_entity_id.bytes, 16u);
    hash_u64(hasher, candidate->source_ordinal);
    hash_u64(hasher, candidate->artifact_ordinal);
    hash_u64(hasher, candidate->row_ordinal);
    hash_u64(hasher, candidate->relation_version);
    hash_u32(hasher, candidate->relation_kind);
    hash_u32(hasher, candidate->flags);
    hash_u32(hasher, candidate->left_disposition);
    hash_u32(hasher, candidate->right_disposition);
}

static void finish_digest(blake3_hasher* hasher, laplace_isa_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

static void hash_program(
    const laplace_isa_program* program,
    laplace_isa_digest256* digest) {
    blake3_hasher hasher;
    uint64_t index;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, PROGRAM_DOMAIN, sizeof(PROGRAM_DOMAIN) - 1u);
    hash_u16(&hasher, program->major);
    hash_u16(&hasher, program->minor);
    hash_u32(&hasher, program->flags);
    hash_u32(&hasher, program->receipt_detail);
#if !defined(LAPLACE_TEST_OMIT_CONTEXT_FROM_ISA_RECEIPT)
    {
        laplace_digest256 context_fingerprint;
        if (laplace_framework_context_fingerprint(
                program->context, &context_fingerprint) == LAPLACE_FRAMEWORK_OK) {
            blake3_hasher_update(
                &hasher, context_fingerprint.bytes,
                sizeof(context_fingerprint.bytes));
        }
    }
#endif
    hash_u64(&hasher, program->instruction_count);
    hash_u64(&hasher, program->value_count);
    for (index = 0; index < program->instruction_count; ++index) {
        const laplace_isa_instruction* instruction =
            &program->instructions[(size_t)index];
        hash_u32(&hasher, instruction->opcode);
        hash_u16(&hasher, instruction->version);
        hash_u16(&hasher, instruction->flags);
        hash_u32(&hasher, instruction->input_value);
        hash_u32(&hasher, instruction->output_value);
    }
    for (index = 0; index < program->value_count; ++index) {
        const laplace_isa_value_view* value = &program->values[(size_t)index];
        hash_u32(&hasher, value->type);
        hash_u32(&hasher, value->flags);
        hash_u32(&hasher, value->stride_bytes);
        hash_u64(&hasher, value->capacity);
    }
    finish_digest(&hasher, digest);
}

static bool hash_value_vector(
    blake3_hasher* hasher,
    const laplace_isa_value_view* value) {
    uint64_t index;
#if !defined(LAPLACE_TEST_STANDING_INPUT_ORDER_RECEIPT)
    if (value->type == LAPLACE_ISA_VALUE_STANDING_PERIOD_INPUT_VECTOR) {
        laplace_standing_period_input* ordered;
        if (value->count > SIZE_MAX / sizeof(*ordered)) {
            return false;
        }
        ordered = (laplace_standing_period_input*)malloc(
            (size_t)value->count * sizeof(*ordered));
        if (ordered == NULL) {
            return false;
        }
        copy_standing_inputs(value, ordered);
        qsort(ordered, (size_t)value->count, sizeof(*ordered),
              compare_standing_input_identity);
        for (index = 0u; index < value->count; ++index) {
            hash_standing_input(hasher, &ordered[(size_t)index]);
        }
        free(ordered);
        return true;
    }
#endif
    for (index = 0; index < value->count; ++index) {
        const uint8_t* item = const_value_element(value, index);
        switch (value->type) {
            case LAPLACE_ISA_VALUE_U32_VECTOR: {
                uint32_t number;
                memcpy(&number, item, sizeof(number));
                hash_u32(hasher, number);
                break;
            }
            case LAPLACE_ISA_VALUE_ID128_VECTOR:
                blake3_hasher_update(hasher, item, sizeof(laplace_id128));
                break;
            case LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR: {
                laplace_trajectory_carrier carrier;
                size_t slot;
                memcpy(&carrier, item, sizeof(carrier));
                for (slot = 0; slot < LAPLACE_TRAJECTORY_SLOT_COUNT; ++slot) {
                    uint64_t bits;
                    memcpy(&bits, &carrier.slots[slot], sizeof(bits));
                    hash_u64(hasher, bits);
                }
                break;
            }
            case LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR: {
                laplace_composition_occurrence occurrence;
                memcpy(&occurrence, item, sizeof(occurrence));
                blake3_hasher_update(hasher, occurrence.entity_id.bytes,
                                     sizeof(occurrence.entity_id.bytes));
                hash_u64(hasher, occurrence.logical_ordinal);
                hash_u64(hasher, occurrence.metadata);
                hash_u32(hasher, occurrence.atom);
                hash_u16(hasher, occurrence.packed_ordinal);
                hash_u16(hasher, occurrence.run_length);
                blake3_hasher_update(hasher, &occurrence.tier,
                                     sizeof(occurrence.tier));
                blake3_hasher_update(hasher, &occurrence.has_atom,
                                     sizeof(occurrence.has_atom));
                break;
            }
            case LAPLACE_ISA_VALUE_HIGHWAY_KEY_VECTOR: {
                laplace_highway_key key;
                memcpy(&key, item, sizeof(key));
                hash_u32(hasher, key.kind);
                hash_u32(hasher, key.reserved);
                blake3_hasher_update(
                    hasher, key.authority.bytes, sizeof(key.authority.bytes));
                blake3_hasher_update(
                    hasher, key.release.bytes, sizeof(key.release.bytes));
                blake3_hasher_update(
                    hasher, key.name_space.bytes, sizeof(key.name_space.bytes));
                blake3_hasher_update(
                    hasher, key.local_identifier.bytes,
                    sizeof(key.local_identifier.bytes));
                hash_u64(hasher, key.version);
                break;
            }
            case LAPLACE_ISA_VALUE_HIGHWAY_COORDINATE_VECTOR: {
                laplace_highway_coordinate coordinate;
                memcpy(&coordinate, item, sizeof(coordinate));
                blake3_hasher_update(
                    hasher, coordinate.coordinate.bytes,
                    sizeof(coordinate.coordinate.bytes));
                blake3_hasher_update(
                    hasher, coordinate.collision_fingerprint.bytes,
                    sizeof(coordinate.collision_fingerprint.bytes));
                hash_u32(hasher, coordinate.kind);
                hash_u32(hasher, coordinate.reserved);
                hash_u64(hasher, coordinate.version);
                break;
            }
            case LAPLACE_ISA_VALUE_HIGHWAY_REGISTRY_RECEIPT_VECTOR: {
                laplace_highway_registry_receipt receipt;
                memcpy(&receipt, item, sizeof(receipt));
                blake3_hasher_update(
                    hasher, receipt.receipt_id.bytes,
                    sizeof(receipt.receipt_id.bytes));
                blake3_hasher_update(
                    hasher, receipt.context_fingerprint.bytes,
                    sizeof(receipt.context_fingerprint.bytes));
                blake3_hasher_update(
                    hasher, receipt.registry_fingerprint.bytes,
                    sizeof(receipt.registry_fingerprint.bytes));
                blake3_hasher_update(
                    hasher, receipt.activation_epoch_id.bytes,
                    sizeof(receipt.activation_epoch_id.bytes));
                blake3_hasher_update(
                    hasher, receipt.activation_epoch_fingerprint.bytes,
                    sizeof(receipt.activation_epoch_fingerprint.bytes));
                hash_u64(hasher, receipt.registry_version);
                hash_u64(hasher, receipt.kind_count);
                hash_u64(hasher, receipt.alias_count);
                hash_u64(hasher, receipt.disposition_count);
                hash_u32(hasher, receipt.status);
                hash_u32(hasher, receipt.reserved);
                break;
            }
            case LAPLACE_ISA_VALUE_EVIDENCE_LINEAGE_RECORD_VECTOR: {
                laplace_evidence_lineage_record record;
                memcpy(&record, item, sizeof(record));
                blake3_hasher_update(hasher, record.node_id.bytes, 32u);
                blake3_hasher_update(hasher, record.proposition_id.bytes, 16u);
                blake3_hasher_update(hasher, record.occurrence_id.bytes, 32u);
                blake3_hasher_update(hasher, record.source_id.bytes, 32u);
                blake3_hasher_update(hasher, record.context_id.bytes, 32u);
                blake3_hasher_update(hasher, record.parent_node_id.bytes, 32u);
                hash_u64(hasher, record.source_ordinal);
                hash_u32(hasher, record.record_kind);
                hash_u32(hasher, record.epistemic_kind);
                hash_u32(hasher, record.flags);
                hash_u32(hasher, record.reserved);
                break;
            }
            case LAPLACE_ISA_VALUE_EVIDENCE_ROOT_RECORD_VECTOR: {
                laplace_evidence_root_record record;
                memcpy(&record, item, sizeof(record));
                blake3_hasher_update(hasher, record.node_id.bytes, 32u);
                blake3_hasher_update(hasher, record.root_node_id.bytes, 32u);
                blake3_hasher_update(hasher, record.proposition_id.bytes, 16u);
                hash_u64(hasher, record.path_depth);
                hash_u32(hasher, record.root_epistemic_kind);
                hash_u32(hasher, record.flags);
                break;
            }
            case LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECORD_VECTOR: {
                laplace_evidence_testimony_record record;
                memcpy(&record, item, sizeof(record));
                blake3_hasher_update(hasher, record.testimony_id.bytes, 32u);
                blake3_hasher_update(hasher, record.evidence_node_id.bytes, 32u);
                blake3_hasher_update(hasher, record.source_profile_id.bytes, 32u);
                blake3_hasher_update(hasher, record.recipe_receipt_id.bytes, 32u);
                blake3_hasher_update(hasher, record.trust_input_id.bytes, 32u);
                blake3_hasher_update(hasher, record.outcome_detail_id.bytes, 32u);
                hash_u64(hasher, record.uncertainty_numerator);
                hash_u64(hasher, record.uncertainty_denominator);
                hash_u64(hasher, record.sample_count);
                hash_u32(hasher, record.source_type);
                hash_u32(hasher, record.outcome_type);
                hash_u32(hasher, record.disposition);
                hash_u32(hasher, record.flags);
                break;
            }
            case LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECEIPT_VECTOR: {
                laplace_evidence_testimony_receipt receipt;
                memcpy(&receipt, item, sizeof(receipt));
                blake3_hasher_update(hasher, receipt.receipt_id.bytes, 32u);
                blake3_hasher_update(hasher, receipt.source_profile_id.bytes, 32u);
                blake3_hasher_update(hasher, receipt.input_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, receipt.output_fingerprint.bytes, 32u);
                hash_u64(hasher, receipt.testimony_count);
                hash_u64(hasher, receipt.sample_count);
                hash_u64(hasher, receipt.uncertain_count);
                hash_u64(hasher, receipt.negative_disposition_count);
                hash_u32(hasher, receipt.version);
                hash_u32(hasher, receipt.status);
                break;
            }
            case LAPLACE_ISA_VALUE_STANDING_PERIOD_INPUT_VECTOR: {
                laplace_standing_period_input input;
                memcpy(&input, item, sizeof(input));
                hash_standing_input(hasher, &input);
                break;
            }
            case LAPLACE_ISA_VALUE_STANDING_PERIOD_RESULT_VECTOR: {
                laplace_standing_period_result result;
                memcpy(&result, item, sizeof(result));
                hash_standing_state(hasher, &result.successor_state);
                hash_standing_receipt(hasher, &result.receipt);
                break;
            }
            case LAPLACE_ISA_VALUE_SOURCE_PROFILE_MANIFEST_VECTOR: {
                laplace_source_profile_manifest profile;
                memcpy(&profile, item, sizeof(profile));
                blake3_hasher_update(hasher, profile.profile_id.bytes, 32u);
                hash_u32(hasher, profile.coordinate.kind);
                hash_u32(hasher, profile.coordinate.reserved);
                blake3_hasher_update(hasher, profile.coordinate.authority.bytes, 16u);
                blake3_hasher_update(hasher, profile.coordinate.release.bytes, 16u);
                blake3_hasher_update(hasher, profile.coordinate.name_space.bytes, 16u);
                blake3_hasher_update(
                    hasher, profile.coordinate.local_identifier.bytes, 16u);
                hash_u64(hasher, profile.coordinate.version);
                blake3_hasher_update(hasher, profile.authority_release_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.license_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.artifact_graph_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.syntax_authority_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.recipe_program_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.universal_ast_mapping_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.highway_references_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.epistemic_witnessing_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.denominator_declaration_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.conformance_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.completion_law_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, profile.selected_boundary_fingerprint.bytes, 32u);
                hash_u64(hasher, profile.byte_count);
                hash_u64(hasher, profile.container_count);
                hash_u64(hasher, profile.member_count);
                hash_u64(hasher, profile.file_count);
                hash_u64(hasher, profile.record_count);
                hash_u64(hasher, profile.field_count);
                hash_u64(hasher, profile.syntax_node_count);
                hash_u64(hasher, profile.span_count);
                hash_u64(hasher, profile.edge_count);
                hash_u64(hasher, profile.reference_count);
                hash_u64(hasher, profile.occurrence_count);
                hash_u64(hasher, profile.claim_count);
                hash_u64(hasher, profile.mapping_count);
                hash_u64(hasher, profile.error_count);
                hash_u64(hasher, profile.unknown_count);
                hash_u64(hasher, profile.transformation_count);
                hash_u64(hasher, profile.output_count);
                hash_u64(hasher, profile.closure_subject_count);
                hash_u64(hasher, profile.accepted_count);
                hash_u64(hasher, profile.rejected_count);
                hash_u64(hasher, profile.duplicate_count);
                hash_u64(hasher, profile.reused_count);
                hash_u64(hasher, profile.transformed_count);
                hash_u64(hasher, profile.lossy_count);
                hash_u64(hasher, profile.unsupported_count);
                hash_u64(hasher, profile.malformed_count);
                hash_u64(hasher, profile.unresolved_count);
                hash_u64(hasher, profile.persisted_count);
                hash_u64(hasher, profile.derived_count);
                hash_u64(hasher, profile.not_applicable_mask);
                hash_u32(hasher, profile.reconstruction_class);
                hash_u32(hasher, profile.flags);
                break;
            }
            case LAPLACE_ISA_VALUE_SOURCE_PROFILE_RECEIPT_VECTOR: {
                laplace_source_profile_receipt receipt;
                memcpy(&receipt, item, sizeof(receipt));
                blake3_hasher_update(hasher, receipt.receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, receipt.selected_boundary_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, receipt.input_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, receipt.output_fingerprint.bytes, 32u);
                hash_u64(hasher, receipt.profile_count);
                hash_u64(hasher, receipt.closure_subject_count);
                hash_u64(hasher, receipt.persisted_count);
                hash_u64(hasher, receipt.negative_count);
                hash_u64(hasher, receipt.exact_reconstruction_count);
                hash_u64(hasher, receipt.semantic_reconstruction_count);
                hash_u64(hasher, receipt.no_reconstruction_count);
                hash_u32(hasher, receipt.version);
                hash_u32(hasher, receipt.status);
                break;
            }
            case LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECORD_VECTOR: {
                laplace_world_admission_record admission;
                memcpy(&admission, item, sizeof(admission));
                blake3_hasher_update(hasher, admission.admission_id.bytes, 32u);
                blake3_hasher_update(hasher, admission.source_profile_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.selected_boundary_fingerprint.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.source_profile_receipt_id.bytes, 32u);
                blake3_hasher_update(hasher, admission.recipe_receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.composition_working_set_receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.composition_presence_receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.composition_producer_receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.composition_stream_receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.evidence_lineage_receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, admission.evidence_testimony_receipt_id.bytes, 32u);
                blake3_hasher_update(hasher, admission.readback_fingerprint.bytes, 32u);
                hash_u64(hasher, admission.profile_occurrence_count);
                hash_u64(hasher, admission.composition_occurrence_count);
                hash_u64(hasher, admission.profile_claim_count);
                hash_u64(hasher, admission.evidence_node_count);
                hash_u64(hasher, admission.testimony_count);
                hash_u64(hasher, admission.profile_bound_testimony_count);
                hash_u64(hasher, admission.recipe_bound_testimony_count);
                hash_u64(hasher, admission.lineage_bound_testimony_count);
                hash_u64(hasher, admission.closure_subject_count);
                hash_u64(hasher, admission.closed_subject_count);
                hash_u32(hasher, admission.reconstruction_class);
                hash_u32(hasher, admission.flags);
                break;
            }
            case LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECEIPT_VECTOR: {
                laplace_world_admission_receipt receipt;
                memcpy(&receipt, item, sizeof(receipt));
                blake3_hasher_update(hasher, receipt.receipt_id.bytes, 32u);
                blake3_hasher_update(
                    hasher, receipt.selected_boundary_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, receipt.input_fingerprint.bytes, 32u);
                blake3_hasher_update(hasher, receipt.output_fingerprint.bytes, 32u);
                hash_u64(hasher, receipt.admission_count);
                hash_u64(hasher, receipt.occurrence_count);
                hash_u64(hasher, receipt.claim_count);
                hash_u64(hasher, receipt.evidence_node_count);
                hash_u64(hasher, receipt.testimony_count);
                hash_u64(hasher, receipt.closure_subject_count);
                hash_u32(hasher, receipt.version);
                hash_u32(hasher, receipt.status);
                break;
            }
            case LAPLACE_ISA_VALUE_REFERENCE_CANDIDATE_VECTOR: {
                laplace_reference_candidate candidate;
                memcpy(&candidate, item, sizeof(candidate));
                blake3_hasher_update(
                    hasher, candidate.source_profile_id.bytes, 32u);
                hash_u32(hasher, candidate.key.kind);
                hash_u32(hasher, candidate.key.reserved);
                blake3_hasher_update(hasher, candidate.key.authority.bytes, 16u);
                blake3_hasher_update(hasher, candidate.key.release.bytes, 16u);
                blake3_hasher_update(hasher, candidate.key.name_space.bytes, 16u);
                blake3_hasher_update(
                    hasher, candidate.key.local_identifier.bytes, 16u);
                hash_u64(hasher, candidate.key.version);
                blake3_hasher_update(hasher, candidate.row_entity_id.bytes, 16u);
                blake3_hasher_update(hasher, candidate.field_entity_id.bytes, 16u);
                blake3_hasher_update(hasher, candidate.value_entity_id.bytes, 16u);
                hash_u64(hasher, candidate.source_ordinal);
                hash_u64(hasher, candidate.artifact_ordinal);
                hash_u64(hasher, candidate.row_ordinal);
                hash_u64(hasher, candidate.column_ordinal);
                hash_u32(hasher, candidate.rule_flags);
                hash_u32(hasher, candidate.reserved);
                break;
            }
            case LAPLACE_ISA_VALUE_REFERENCE_RECORD_VECTOR: {
                laplace_reference_record record;
                memcpy(&record, item, sizeof(record));
                blake3_hasher_update(
                    hasher, record.candidate.source_profile_id.bytes, 32u);
                hash_u32(hasher, record.candidate.key.kind);
                hash_u32(hasher, record.candidate.key.reserved);
                blake3_hasher_update(
                    hasher, record.candidate.key.authority.bytes, 16u);
                blake3_hasher_update(
                    hasher, record.candidate.key.release.bytes, 16u);
                blake3_hasher_update(
                    hasher, record.candidate.key.name_space.bytes, 16u);
                blake3_hasher_update(
                    hasher, record.candidate.key.local_identifier.bytes, 16u);
                hash_u64(hasher, record.candidate.key.version);
                blake3_hasher_update(
                    hasher, record.candidate.row_entity_id.bytes, 16u);
                blake3_hasher_update(
                    hasher, record.candidate.field_entity_id.bytes, 16u);
                blake3_hasher_update(
                    hasher, record.candidate.value_entity_id.bytes, 16u);
                hash_u64(hasher, record.candidate.source_ordinal);
                hash_u64(hasher, record.candidate.artifact_ordinal);
                hash_u64(hasher, record.candidate.row_ordinal);
                hash_u64(hasher, record.candidate.column_ordinal);
                hash_u32(hasher, record.candidate.rule_flags);
                hash_u32(hasher, record.candidate.reserved);
                blake3_hasher_update(
                    hasher, record.coordinate.coordinate.bytes, 16u);
                blake3_hasher_update(
                    hasher, record.coordinate.collision_fingerprint.bytes, 32u);
                hash_u32(hasher, record.coordinate.kind);
                hash_u32(hasher, record.coordinate.reserved);
                hash_u64(hasher, record.coordinate.version);
                blake3_hasher_update(hasher, record.occurrence_id.bytes, 32u);
                blake3_hasher_update(hasher, record.reference_id.bytes, 32u);
                hash_u32(hasher, record.disposition);
                hash_u32(hasher, record.reserved);
                break;
            }
            case LAPLACE_ISA_VALUE_REFERENCE_MAPPING_CANDIDATE_VECTOR: {
                laplace_reference_mapping_candidate candidate;
                memcpy(&candidate, item, sizeof(candidate));
                hash_reference_mapping_candidate(hasher, &candidate);
                break;
            }
            case LAPLACE_ISA_VALUE_REFERENCE_MAPPING_RECORD_VECTOR: {
                laplace_reference_mapping_record record;
                memcpy(&record, item, sizeof(record));
                hash_reference_mapping_candidate(hasher, &record.candidate);
                blake3_hasher_update(hasher, record.proposition_id.bytes, 32u);
                blake3_hasher_update(hasher, record.occurrence_id.bytes, 32u);
                blake3_hasher_update(hasher, record.mapping_id.bytes, 32u);
                hash_u32(hasher, record.disposition);
                hash_u32(hasher, record.reserved);
                break;
            }
            default:
                break;
        }
    }
    return true;
}

static bool hash_inputs(
    const laplace_isa_program* program,
    laplace_isa_digest256* digest) {
    blake3_hasher hasher;
    uint64_t index;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, INPUT_DOMAIN, sizeof(INPUT_DOMAIN) - 1u);
    for (index = 0; index < program->instruction_count; ++index) {
        const laplace_isa_instruction* instruction =
            &program->instructions[(size_t)index];
        const laplace_isa_value_view* value =
            &program->values[instruction->input_value];
        hash_u64(&hasher, index);
        hash_u32(&hasher, instruction->input_value);
        hash_u32(&hasher, value->type);
        hash_u64(&hasher, value->count);
        if (!hash_value_vector(&hasher, value)) {
            return false;
        }
    }
    finish_digest(&hasher, digest);
    return true;
}

static bool hash_outputs(
    const laplace_isa_program* program,
    laplace_isa_digest256* digest) {
    blake3_hasher hasher;
    uint64_t index;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, OUTPUT_DOMAIN, sizeof(OUTPUT_DOMAIN) - 1u);
    for (index = 0; index < program->instruction_count; ++index) {
        const laplace_isa_instruction* instruction =
            &program->instructions[(size_t)index];
        const laplace_isa_value_view* value =
            &program->values[instruction->output_value];
        hash_u64(&hasher, index);
        hash_u32(&hasher, instruction->output_value);
        hash_u32(&hasher, value->type);
        hash_u64(&hasher, value->count);
        if (!hash_value_vector(&hasher, value)) {
            return false;
        }
    }
    finish_digest(&hasher, digest);
    return true;
}

static void hash_receipt(laplace_isa_receipt* receipt) {
    blake3_hasher hasher;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, RECEIPT_DOMAIN, sizeof(RECEIPT_DOMAIN) - 1u);
    hash_u16(&hasher, receipt->major);
    hash_u16(&hasher, receipt->minor);
    hash_u32(&hasher, receipt->receipt_detail);
    hash_u32(&hasher, (uint32_t)receipt->status);
    hash_u64(&hasher, receipt->instruction_count);
    hash_u64(&hasher, receipt->executed_instruction_count);
    blake3_hasher_update(&hasher, receipt->program_fingerprint.bytes,
                         sizeof(receipt->program_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->context_fingerprint.bytes,
                         sizeof(receipt->context_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->input_fingerprint.bytes,
                         sizeof(receipt->input_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt->output_fingerprint.bytes,
                         sizeof(receipt->output_fingerprint.bytes));
    finish_digest(&hasher, &receipt->receipt_id);
}

static laplace_isa_status execute_identity_codepoint_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    uint64_t index;

    if (input->stride_bytes == (uint32_t)sizeof(uint32_t) &&
        output->stride_bytes == (uint32_t)sizeof(laplace_id128) &&
        (uintptr_t)input->data % _Alignof(uint32_t) == 0 &&
        (uintptr_t)output->data % _Alignof(laplace_id128) == 0 &&
        input->count <= SIZE_MAX) {
        const laplace_identity_status status = laplace_identity_codepoint_batch(
            (const uint32_t*)input->data,
            (size_t)input->count,
            (laplace_id128*)output->data);
        if (status != LAPLACE_IDENTITY_OK) {
            return LAPLACE_ISA_EXECUTION_FAILED;
        }
    } else {
        for (index = 0; index < input->count; ++index) {
            uint32_t position;
            laplace_id128 identity;
            laplace_identity_status status;
            memcpy(&position, const_value_element(input, index), sizeof(position));
            status = laplace_identity_codepoint(position, &identity);
            if (status != LAPLACE_IDENTITY_OK) {
                return LAPLACE_ISA_EXECUTION_FAILED;
            }
            memcpy(value_element(output, index), &identity, sizeof(identity));
        }
    }
    output->count = input->count;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_trajectory_composition_decode_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    uint64_t logical_ordinal = 1;
    uint64_t index;

    for (index = 0; index < input->count; ++index) {
        laplace_trajectory_carrier carrier;
        laplace_composition_occurrence occurrence;
        laplace_trajectory_status trajectory_status;
        memcpy(&carrier, const_value_element(input, index), sizeof(carrier));
        trajectory_status = laplace_trajectory_composition_decode_one(
            &carrier, logical_ordinal, &occurrence);
        if (trajectory_status != LAPLACE_TRAJECTORY_OK) {
            return LAPLACE_ISA_EXECUTION_FAILED;
        }
        memcpy(value_element(output, index), &occurrence, sizeof(occurrence));
        logical_ordinal += occurrence.run_length;
    }
    output->count = input->count;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_highway_coordinate_calculate_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    uint64_t index;

    if (input->stride_bytes == (uint32_t)sizeof(laplace_highway_key) &&
        output->stride_bytes == (uint32_t)sizeof(laplace_highway_coordinate) &&
        (uintptr_t)input->data % _Alignof(laplace_highway_key) == 0 &&
        (uintptr_t)output->data % _Alignof(laplace_highway_coordinate) == 0 &&
        input->count <= SIZE_MAX) {
        const laplace_highway_status status =
            laplace_highway_coordinate_calculate_batch(
                (const laplace_highway_key*)input->data,
                (size_t)input->count,
                (laplace_highway_coordinate*)output->data);
        if (status != LAPLACE_HIGHWAY_OK) {
            return LAPLACE_ISA_EXECUTION_FAILED;
        }
    } else {
        for (index = 0; index < input->count; ++index) {
            laplace_highway_key key;
            laplace_highway_coordinate coordinate;
            memcpy(&key, const_value_element(input, index), sizeof(key));
            if (laplace_highway_coordinate_calculate(&key, &coordinate) !=
                LAPLACE_HIGHWAY_OK) {
                return LAPLACE_ISA_EXECUTION_FAILED;
            }
            memcpy(value_element(output, index), &coordinate, sizeof(coordinate));
        }
    }
    output->count = input->count;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_highway_registry_materialize_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    uint64_t index;

    for (index = 0; index < input->count; ++index) {
        laplace_highway_registry_receipt receipt;
        if (laplace_highway_registry_materialize(
                program->context, &receipt) != LAPLACE_HIGHWAY_OK) {
            return LAPLACE_ISA_EXECUTION_FAILED;
        }
        memcpy(value_element(output, index), &receipt, sizeof(receipt));
    }
    output->count = input->count;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_evidence_record_lineage_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_evidence_lineage_receipt receipt;
    laplace_evidence_lineage_error error;
    size_t count = 0u;
    laplace_evidence_lineage_status status;
    laplace_evidence_lineage_record* contiguous_input = NULL;
    laplace_evidence_root_record* contiguous_output = NULL;
    const laplace_evidence_lineage_record* lineage_input;
    laplace_evidence_root_record* lineage_output;
    uint64_t temporary_bytes = 0u;
    uint64_t index;
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    lineage_input = (const laplace_evidence_lineage_record*)input->data;
    lineage_output = (laplace_evidence_root_record*)output->data;
    if (input->stride_bytes != sizeof(*contiguous_input) ||
        (uintptr_t)input->data % _Alignof(laplace_evidence_lineage_record) != 0u) {
        temporary_bytes = input->count * sizeof(*contiguous_input);
        contiguous_input = (laplace_evidence_lineage_record*)calloc(
            (size_t)input->count, sizeof(*contiguous_input));
        if (contiguous_input == NULL) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        copy_lineage_inputs(input, contiguous_input);
        lineage_input = contiguous_input;
    }
    if (output->stride_bytes != sizeof(*contiguous_output) ||
        (uintptr_t)output->data % _Alignof(laplace_evidence_root_record) != 0u) {
        temporary_bytes += output->capacity * sizeof(*contiguous_output);
        contiguous_output = (laplace_evidence_root_record*)calloc(
            (size_t)output->capacity, sizeof(*contiguous_output));
        if (contiguous_output == NULL) {
            free(contiguous_input);
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        lineage_output = contiguous_output;
    }
    if (temporary_bytes > program->context->resource_grant.memory_bytes) {
        free(contiguous_output);
        free(contiguous_input);
        return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
    }
    status = laplace_evidence_record_lineage_batch(
        lineage_input,
        (size_t)input->count,
        program->context->resource_grant.memory_bytes - temporary_bytes,
        lineage_output,
        (size_t)output->capacity, &count, &receipt, &error);
    if (status != LAPLACE_EVIDENCE_LINEAGE_OK) {
        free(contiguous_output);
        free(contiguous_input);
        return status == LAPLACE_EVIDENCE_LINEAGE_CYCLE
            ? LAPLACE_ISA_DEPENDENCE_CYCLE
            : status == LAPLACE_EVIDENCE_LINEAGE_RESOURCE_INSUFFICIENT
                ? LAPLACE_ISA_RESOURCE_INSUFFICIENT
                : LAPLACE_ISA_EXECUTION_FAILED;
    }
    if (contiguous_output != NULL) {
        for (index = 0u; index < (uint64_t)count; ++index) {
            memcpy(value_element(output, index), &contiguous_output[(size_t)index],
                   sizeof(contiguous_output[index]));
        }
    }
    free(contiguous_output);
    free(contiguous_input);
    output->count = (uint64_t)count;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_evidence_record_testimony_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_evidence_testimony_record* contiguous = NULL;
    const laplace_evidence_testimony_record* testimony_input;
    laplace_evidence_testimony_receipt receipt;
    laplace_evidence_testimony_error error;
    laplace_evidence_testimony_status status;
    uint64_t temporary_bytes = 0u;
    testimony_input = (const laplace_evidence_testimony_record*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_evidence_testimony_record) != 0u) {
        temporary_bytes = input->count * sizeof(*contiguous);
        if (temporary_bytes > program->context->resource_grant.memory_bytes) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        contiguous = (laplace_evidence_testimony_record*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        copy_testimony_inputs(input, contiguous);
        testimony_input = contiguous;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_evidence_record_testimony_batch(
        testimony_input, (size_t)input->count, &receipt, &error);
    free(contiguous);
    if (status != LAPLACE_EVIDENCE_TESTIMONY_OK) {
        return LAPLACE_ISA_EXECUTION_FAILED;
    }
    memcpy(value_element(output, 0u), &receipt, sizeof(receipt));
    output->count = 1u;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_evidence_calculate_standing_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_standing_period_input* contiguous = NULL;
    const laplace_standing_period_input* standing_input;
    laplace_standing_period_result result;
    laplace_standing_error error;
    laplace_standing_status status;
    standing_input = (const laplace_standing_period_input*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_standing_period_input) != 0u) {
        contiguous = (laplace_standing_period_input*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        copy_standing_inputs(input, contiguous);
        standing_input = contiguous;
    }
    memset(&result, 0, sizeof(result));
    memset(&error, 0, sizeof(error));
    status = laplace_standing_calculate_period_batch(
        standing_input, (size_t)input->count, &result, &error);
    free(contiguous);
    if (status == LAPLACE_STANDING_RESOURCE_INSUFFICIENT ||
        status == LAPLACE_STANDING_OVERFLOW) {
        return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
    }
    if (status != LAPLACE_STANDING_OK) {
        return LAPLACE_ISA_EXECUTION_FAILED;
    }
    memcpy(value_element(output, 0u), &result, sizeof(result));
    output->count = 1u;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_source_profile_validate_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_source_profile_manifest* contiguous = NULL;
    const laplace_source_profile_manifest* profile_input;
    laplace_source_profile_receipt receipt;
    laplace_source_profile_error error;
    laplace_source_profile_status status;
    uint64_t temporary_bytes = 0u;
    profile_input = (const laplace_source_profile_manifest*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_source_profile_manifest) != 0u) {
        temporary_bytes = input->count * sizeof(*contiguous);
        if (temporary_bytes > program->context->resource_grant.memory_bytes) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        contiguous = (laplace_source_profile_manifest*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        copy_source_profile_inputs(input, contiguous);
        profile_input = contiguous;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_source_profile_validate_batch(
        profile_input, (size_t)input->count, &receipt, &error);
    free(contiguous);
    if (status != LAPLACE_SOURCE_PROFILE_OK) {
        return LAPLACE_ISA_EXECUTION_FAILED;
    }
    memcpy(value_element(output, 0u), &receipt, sizeof(receipt));
    output->count = 1u;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_world_admission_close_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_world_admission_record* contiguous = NULL;
    const laplace_world_admission_record* admission_input;
    laplace_world_admission_receipt receipt;
    laplace_world_admission_error error;
    laplace_world_admission_status status;
    uint64_t temporary_bytes = 0u;
    admission_input = (const laplace_world_admission_record*)input->data;
    if (input->stride_bytes != sizeof(*contiguous) ||
        (uintptr_t)input->data % _Alignof(laplace_world_admission_record) != 0u) {
        temporary_bytes = input->count * sizeof(*contiguous);
        if (temporary_bytes > program->context->resource_grant.memory_bytes) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        contiguous = (laplace_world_admission_record*)calloc(
            (size_t)input->count, sizeof(*contiguous));
        if (contiguous == NULL) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        copy_world_admission_inputs(input, contiguous);
        admission_input = contiguous;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_world_admission_close_batch(
        admission_input, (size_t)input->count, &receipt, &error);
    free(contiguous);
    if (status != LAPLACE_WORLD_ADMISSION_OK) {
        return LAPLACE_ISA_EXECUTION_FAILED;
    }
    memcpy(value_element(output, 0u), &receipt, sizeof(receipt));
    output->count = 1u;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_reference_topology_resolve_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_reference_candidate* contiguous_input = NULL;
    laplace_reference_record* contiguous_output = NULL;
    const laplace_reference_candidate* candidates;
    laplace_reference_record* records;
    laplace_reference_topology_receipt receipt;
    laplace_reference_topology_error error;
    laplace_reference_topology_status status;
    uint64_t temporary_bytes = 0u;
    uint64_t index;
    candidates = (const laplace_reference_candidate*)input->data;
    records = (laplace_reference_record*)output->data;
    if (input->stride_bytes != sizeof(*contiguous_input) ||
        (uintptr_t)input->data % _Alignof(laplace_reference_candidate) != 0u) {
        temporary_bytes = input->count * sizeof(*contiguous_input);
        contiguous_input = (laplace_reference_candidate*)calloc(
            (size_t)input->count, sizeof(*contiguous_input));
        if (contiguous_input == NULL) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        copy_reference_candidates(input, contiguous_input);
        candidates = contiguous_input;
    }
    if (output->stride_bytes != sizeof(*contiguous_output) ||
        (uintptr_t)output->data % _Alignof(laplace_reference_record) != 0u) {
        if (UINT64_MAX - temporary_bytes <
            input->count * sizeof(*contiguous_output)) {
            free(contiguous_input);
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        temporary_bytes += input->count * sizeof(*contiguous_output);
        contiguous_output = (laplace_reference_record*)calloc(
            (size_t)input->count, sizeof(*contiguous_output));
        if (contiguous_output == NULL) {
            free(contiguous_input);
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        records = contiguous_output;
    }
    if (temporary_bytes > program->context->resource_grant.memory_bytes) {
        free(contiguous_output);
        free(contiguous_input);
        return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_reference_topology_resolve_batch(
        candidates, (size_t)input->count, records, &receipt, &error);
    if (status != LAPLACE_REFERENCE_TOPOLOGY_OK) {
        free(contiguous_output);
        free(contiguous_input);
        return status == LAPLACE_REFERENCE_TOPOLOGY_MEMORY_FAILURE
            ? LAPLACE_ISA_RESOURCE_INSUFFICIENT
            : LAPLACE_ISA_EXECUTION_FAILED;
    }
    if (contiguous_output != NULL) {
        for (index = 0u; index < input->count; ++index) {
            memcpy(value_element(output, index),
                   &contiguous_output[(size_t)index],
                   sizeof(contiguous_output[index]));
        }
    }
    free(contiguous_output);
    free(contiguous_input);
    output->count = input->count;
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_reference_mapping_resolve_batch(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input = &program->values[instruction->input_value];
    laplace_isa_value_view* output = &program->values[instruction->output_value];
    laplace_reference_mapping_candidate* contiguous_input = NULL;
    laplace_reference_mapping_record* contiguous_output = NULL;
    const laplace_reference_mapping_candidate* candidates;
    laplace_reference_mapping_record* records;
    laplace_reference_mapping_receipt receipt;
    laplace_reference_mapping_error error;
    laplace_reference_mapping_status status;
    uint64_t temporary_bytes = 0u;
    uint64_t index;
    candidates = (const laplace_reference_mapping_candidate*)input->data;
    records = (laplace_reference_mapping_record*)output->data;
    if (input->stride_bytes != sizeof(*contiguous_input) ||
        (uintptr_t)input->data %
            _Alignof(laplace_reference_mapping_candidate) != 0u) {
        temporary_bytes = input->count * sizeof(*contiguous_input);
        contiguous_input = (laplace_reference_mapping_candidate*)calloc(
            (size_t)input->count, sizeof(*contiguous_input));
        if (contiguous_input == NULL) {
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        copy_reference_mapping_candidates(input, contiguous_input);
        candidates = contiguous_input;
    }
    if (output->stride_bytes != sizeof(*contiguous_output) ||
        (uintptr_t)output->data %
            _Alignof(laplace_reference_mapping_record) != 0u) {
        if (UINT64_MAX - temporary_bytes <
            input->count * sizeof(*contiguous_output)) {
            free(contiguous_input);
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        temporary_bytes += input->count * sizeof(*contiguous_output);
        contiguous_output = (laplace_reference_mapping_record*)calloc(
            (size_t)input->count, sizeof(*contiguous_output));
        if (contiguous_output == NULL) {
            free(contiguous_input);
            return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        }
        records = contiguous_output;
    }
    if (temporary_bytes > program->context->resource_grant.memory_bytes) {
        free(contiguous_output);
        free(contiguous_input);
        return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
    }
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_reference_mapping_resolve_batch(
        candidates, (size_t)input->count, records, &receipt, &error);
    if (status != LAPLACE_REFERENCE_MAPPING_OK) {
        free(contiguous_output);
        free(contiguous_input);
        return status == LAPLACE_REFERENCE_MAPPING_MEMORY_FAILURE
            ? LAPLACE_ISA_RESOURCE_INSUFFICIENT
            : LAPLACE_ISA_EXECUTION_FAILED;
    }
    if (contiguous_output != NULL) {
        for (index = 0u; index < input->count; ++index) {
            memcpy(value_element(output, index),
                   &contiguous_output[(size_t)index],
                   sizeof(contiguous_output[index]));
        }
    }
    free(contiguous_output);
    free(contiguous_input);
    output->count = input->count;
    return LAPLACE_ISA_OK;
}

laplace_isa_status laplace_isa_execute(
    laplace_isa_program* program,
    laplace_isa_receipt* receipt,
    laplace_isa_error* error) {
    laplace_isa_status status;
    uint64_t instruction_index;

    if (receipt == NULL) {
        return fail(error, LAPLACE_ISA_INVALID_ARGUMENT, UINT64_MAX, UINT32_MAX);
    }
    memset(receipt, 0, sizeof(*receipt));
    if (program != NULL) {
        receipt->major = program->major;
        receipt->minor = program->minor;
        receipt->receipt_detail = program->receipt_detail;
        receipt->instruction_count = program->instruction_count;
    }

    status = laplace_isa_validate(program, error);
    if (status != LAPLACE_ISA_OK) {
        receipt->status = status;
        return status;
    }

#if defined(LAPLACE_TEST_OMIT_CONTEXT_FROM_ISA_RECEIPT)
    memset(&receipt->context_fingerprint, 0, sizeof(receipt->context_fingerprint));
#else
    if (laplace_framework_context_fingerprint(
            program->context, &receipt->context_fingerprint) !=
        LAPLACE_FRAMEWORK_OK) {
        receipt->status = LAPLACE_ISA_CONTEXT_INVALID;
        return fail(error, LAPLACE_ISA_CONTEXT_INVALID, UINT64_MAX, UINT32_MAX);
    }
#endif
    hash_program(program, &receipt->program_fingerprint);
    if (!hash_inputs(program, &receipt->input_fingerprint)) {
        receipt->status = LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    UINT64_MAX, UINT32_MAX);
    }

    for (instruction_index = 0;
         instruction_index < program->instruction_count;
         ++instruction_index) {
        const laplace_isa_instruction* instruction =
            &program->instructions[(size_t)instruction_index];
        const laplace_isa_operation_binding* binding =
            operation_binding_find(instruction->opcode);
        status = binding == NULL
            ? LAPLACE_ISA_EXECUTION_FAILED
            : binding->execute(program, instruction);
        if (status != LAPLACE_ISA_OK) {
            receipt->status = status;
            receipt->executed_instruction_count = instruction_index;
            return fail(error, status, instruction_index, instruction->output_value);
        }
        receipt->executed_instruction_count = instruction_index + 1;
    }

    if (!hash_outputs(program, &receipt->output_fingerprint)) {
        receipt->status = LAPLACE_ISA_RESOURCE_INSUFFICIENT;
        return fail(error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
                    UINT64_MAX, UINT32_MAX);
    }
    receipt->status = LAPLACE_ISA_OK;
    hash_receipt(receipt);
    clear_error(error);
    return LAPLACE_ISA_OK;
}
