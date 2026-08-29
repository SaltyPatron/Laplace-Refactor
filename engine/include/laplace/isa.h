#ifndef LAPLACE_ISA_H
#define LAPLACE_ISA_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_packet.h"
#include "laplace/contract/isa.h"
#include "laplace/export.h"
#include "laplace/framework.h"
#include "laplace/identity.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef laplace_digest256 laplace_isa_digest256;

typedef struct laplace_isa_value_view {
    void* data;
    uint64_t count;
    uint64_t capacity;
    uint32_t stride_bytes;
    uint32_t type;
    uint32_t flags;
    uint32_t reserved;
} laplace_isa_value_view;

typedef struct laplace_isa_instruction {
    uint32_t opcode;
    uint32_t input_value;
    uint32_t output_value;
    uint16_t version;
    uint16_t flags;
} laplace_isa_instruction;

typedef struct laplace_isa_program {
    laplace_isa_instruction* instructions;
    laplace_isa_value_view* values;
    const laplace_framework_context* context;
    uint64_t instruction_count;
    uint64_t value_count;
    uint16_t major;
    uint16_t minor;
    uint32_t flags;
    uint32_t receipt_detail;
    uint32_t reserved;
} laplace_isa_program;

typedef enum laplace_isa_status {
    LAPLACE_ISA_OK = 0,
    LAPLACE_ISA_INVALID_ARGUMENT = 1,
    LAPLACE_ISA_UNSUPPORTED_VERSION = 2,
    LAPLACE_ISA_EMPTY_PROGRAM = 3,
    LAPLACE_ISA_UNKNOWN_FLAGS = 4,
    LAPLACE_ISA_UNKNOWN_OPCODE = 5,
    LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION = 6,
    LAPLACE_ISA_REGISTER_OUT_OF_RANGE = 7,
    LAPLACE_ISA_VALUE_TYPE_MISMATCH = 8,
    LAPLACE_ISA_VALUE_INVALID = 9,
    LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT = 10,
    LAPLACE_ISA_VALUE_OVERLAP = 11,
    LAPLACE_ISA_INPUT_OUT_OF_RANGE = 12,
    LAPLACE_ISA_EXECUTION_FAILED = 13,
    LAPLACE_ISA_CONTEXT_INVALID = 14,
    LAPLACE_ISA_DEPENDENCE_CYCLE = 15,
    LAPLACE_ISA_RESOURCE_INSUFFICIENT = 16
} laplace_isa_status;

typedef struct laplace_isa_error {
    laplace_isa_status status;
    uint64_t instruction_index;
    uint32_t value_index;
    uint32_t reserved;
} laplace_isa_error;

typedef struct laplace_isa_receipt {
    laplace_isa_digest256 receipt_id;
    laplace_isa_digest256 context_fingerprint;
    laplace_isa_digest256 program_fingerprint;
    laplace_isa_digest256 input_fingerprint;
    laplace_isa_digest256 output_fingerprint;
    uint64_t instruction_count;
    uint64_t executed_instruction_count;
    uint16_t major;
    uint16_t minor;
    uint32_t receipt_detail;
    laplace_isa_status status;
    uint32_t reserved;
} laplace_isa_receipt;

/* The generated ISA registry names handlers from the operation contract.  Keep
 * the cognition bridge here beside the public ISA types so every transport
 * reaches the same canonical packet executor without a second opcode switch. */
static inline laplace_isa_status laplace_isa_cognition_fail(
    laplace_isa_error* error,
    laplace_isa_status status,
    uint64_t instruction_index,
    uint32_t value_index) {
    if (error != NULL) {
        error->status = status;
        error->instruction_index = instruction_index;
        error->value_index = value_index;
        error->reserved = 0u;
    }
    return status;
}

static inline int laplace_isa_cognition_size_fits(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return 1;
#endif
}

static inline laplace_isa_status validate_cognition_solve_packet(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input;
    const laplace_isa_value_view* output;
    size_t required_words = 0u;
    uint64_t request_bytes;
    uint64_t result_bytes;
    uint64_t working_bytes;
    laplace_cognition_packet_status packet_status;
    if (program == NULL || instruction == NULL || program->values == NULL ||
        program->context == NULL) {
        return laplace_isa_cognition_fail(
            error, LAPLACE_ISA_INVALID_ARGUMENT,
            instruction_index, UINT32_MAX);
    }
    input = &program->values[instruction->input_value];
    output = &program->values[instruction->output_value];
    if (input->count == 0u || input->stride_bytes != sizeof(uint32_t) ||
        output->stride_bytes != sizeof(uint32_t) || input->data == NULL ||
        output->data == NULL ||
        !laplace_isa_cognition_size_fits(input->count) ||
        !laplace_isa_cognition_size_fits(output->capacity)) {
        return laplace_isa_cognition_fail(
            error, LAPLACE_ISA_VALUE_INVALID,
            instruction_index, instruction->input_value);
    }
    packet_status = laplace_cognition_packet_required_result_words(
        (const uint32_t*)input->data, (size_t)input->count, &required_words);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK) {
        return laplace_isa_cognition_fail(
            error, LAPLACE_ISA_INPUT_OUT_OF_RANGE,
            instruction_index, instruction->input_value);
    }
    if ((uint64_t)required_words > output->capacity) {
        return laplace_isa_cognition_fail(
            error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
            instruction_index, instruction->output_value);
    }
    if (input->count > UINT64_MAX / sizeof(uint32_t) ||
        (uint64_t)required_words > UINT64_MAX / sizeof(uint32_t)) {
        return laplace_isa_cognition_fail(
            error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
            instruction_index, instruction->output_value);
    }
    request_bytes = input->count * sizeof(uint32_t);
    result_bytes = (uint64_t)required_words * sizeof(uint32_t);
    if (UINT64_MAX - request_bytes < result_bytes) {
        return laplace_isa_cognition_fail(
            error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
            instruction_index, instruction->output_value);
    }
    working_bytes = request_bytes + result_bytes;
    if (working_bytes > program->context->resource_grant.memory_bytes) {
        return laplace_isa_cognition_fail(
            error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
            instruction_index, instruction->output_value);
    }
    return LAPLACE_ISA_OK;
}

static inline laplace_isa_status execute_cognition_solve_packet(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
    laplace_isa_value_view* input;
    laplace_isa_value_view* output;
    size_t result_words = 0u;
    laplace_cognition_packet_status packet_status;
    if (program == NULL || instruction == NULL || program->values == NULL) {
        return LAPLACE_ISA_INVALID_ARGUMENT;
    }
    input = &program->values[instruction->input_value];
    output = &program->values[instruction->output_value];
    packet_status = laplace_cognition_packet_execute_words(
        (const uint32_t*)input->data,
        (size_t)input->count,
        (uint32_t*)output->data,
        (size_t)output->capacity,
        &result_words);
    if (packet_status == LAPLACE_COGNITION_PACKET_RESULT_CAPACITY) {
        return LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT;
    }
    if (packet_status != LAPLACE_COGNITION_PACKET_OK) {
        return LAPLACE_ISA_EXECUTION_FAILED;
    }
    output->count = (uint64_t)result_words;
    return LAPLACE_ISA_OK;
}

LAPLACE_API laplace_isa_status laplace_isa_validate(
    const laplace_isa_program* program,
    laplace_isa_error* error);

LAPLACE_API laplace_isa_status laplace_isa_execute(
    laplace_isa_program* program,
    laplace_isa_receipt* receipt,
    laplace_isa_error* error);

#ifdef __cplusplus
}
#endif

#endif