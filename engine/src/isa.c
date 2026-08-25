#include "laplace/isa.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake3.h"
#include "laplace/framework.h"
#include "laplace/trajectory.h"

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

        if (!value_span(left_output, left_input->count,
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

            if (!value_span(right_output, right_input->count,
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

static void hash_value_vector(
    blake3_hasher* hasher,
    const laplace_isa_value_view* value) {
    uint64_t index;
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
            default:
                break;
        }
    }
}

static void hash_inputs(
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
        hash_value_vector(&hasher, value);
    }
    finish_digest(&hasher, digest);
}

static void hash_outputs(
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
        hash_value_vector(&hasher, value);
    }
    finish_digest(&hasher, digest);
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
    hash_inputs(program, &receipt->input_fingerprint);

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

    hash_outputs(program, &receipt->output_fingerprint);
    receipt->status = LAPLACE_ISA_OK;
    hash_receipt(receipt);
    clear_error(error);
    return LAPLACE_ISA_OK;
}
