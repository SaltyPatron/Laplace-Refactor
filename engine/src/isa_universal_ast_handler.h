#ifndef LAPLACE_ISA_UNIVERSAL_AST_HANDLER_H
#define LAPLACE_ISA_UNIVERSAL_AST_HANDLER_H

#include "laplace/isa.h"
#include "laplace/universal_ast.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define LAPLACE_UNIVERSAL_AST_RECEIPT_WORDS 66u

static laplace_isa_status laplace_isa_ast_fail(
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

static int laplace_isa_ast_size_fits(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return 1;
#endif
}

#if !defined(LAPLACE_TEST_ISA_REJECT_RESULT_ALIAS) && \
    !defined(LAPLACE_TEST_ISA_STANDING_ORDER_BYPASS)
static void laplace_isa_ast_write_u64(uint32_t** cursor, uint64_t value) {
    *(*cursor)++ = (uint32_t)value;
    *(*cursor)++ = (uint32_t)(value >> 32u);
}

static void laplace_isa_ast_write_digest(
    uint32_t** cursor,
    const laplace_digest256* digest) {
    size_t word;
    for (word = 0u; word < 8u; ++word) {
        const size_t offset = word * 4u;
        *(*cursor)++ =
            (uint32_t)digest->bytes[offset] |
            ((uint32_t)digest->bytes[offset + 1u] << 8u) |
            ((uint32_t)digest->bytes[offset + 2u] << 16u) |
            ((uint32_t)digest->bytes[offset + 3u] << 24u);
    }
}

static void laplace_isa_ast_encode_receipt(
    const laplace_universal_ast_packet_receipt* receipt,
    uint32_t* output) {
    uint32_t* cursor = output;
    laplace_isa_ast_write_digest(&cursor, &receipt->receipt_id);
    laplace_isa_ast_write_digest(&cursor, &receipt->grammar_registry_fingerprint);
    laplace_isa_ast_write_digest(&cursor, &receipt->recipe_fingerprint);
    laplace_isa_ast_write_digest(&cursor, &receipt->plan_fingerprint);
    laplace_isa_ast_write_digest(&cursor, &receipt->witness_fingerprint);
    laplace_isa_ast_write_digest(&cursor, &receipt->composition_trace_fingerprint);
    laplace_isa_ast_write_u64(&cursor, receipt->binding_count);
    laplace_isa_ast_write_u64(&cursor, receipt->preserved_count);
    laplace_isa_ast_write_u64(&cursor, receipt->declared_loss_count);
    laplace_isa_ast_write_u64(&cursor, receipt->content_binding_count);
    laplace_isa_ast_write_u64(&cursor, receipt->missing_binding_count);
    laplace_isa_ast_write_u64(&cursor, receipt->error_binding_count);
    laplace_isa_ast_write_u64(&cursor, receipt->atom_count);
    laplace_isa_ast_write_u64(&cursor, receipt->request_count);
    *cursor++ = receipt->version;
    *cursor++ = receipt->status;
}
#endif

static laplace_isa_status validate_universal_ast_apply_packet(
    const laplace_isa_program* program,
    const laplace_isa_instruction* instruction,
    uint64_t instruction_index,
    laplace_isa_error* error) {
    const laplace_isa_value_view* input;
    const laplace_isa_value_view* output;
    uint64_t request_bytes;
    uint64_t result_bytes;

    if (program == NULL || instruction == NULL || program->values == NULL ||
        program->context == NULL) {
        return laplace_isa_ast_fail(
            error, LAPLACE_ISA_INVALID_ARGUMENT,
            instruction_index, UINT32_MAX);
    }
    input = &program->values[instruction->input_value];
    output = &program->values[instruction->output_value];
    if (input->count == 0u || input->data == NULL || output->data == NULL ||
        input->stride_bytes != sizeof(uint32_t) ||
        output->stride_bytes != sizeof(uint32_t) ||
        !laplace_isa_ast_size_fits(input->count) ||
        !laplace_isa_ast_size_fits(output->capacity)) {
        return laplace_isa_ast_fail(
            error, LAPLACE_ISA_VALUE_INVALID,
            instruction_index, instruction->input_value);
    }
    if (output->capacity < LAPLACE_UNIVERSAL_AST_RECEIPT_WORDS) {
        return laplace_isa_ast_fail(
            error, LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT,
            instruction_index, instruction->output_value);
    }
    if (input->count > UINT64_MAX / sizeof(uint32_t)) {
        return laplace_isa_ast_fail(
            error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
            instruction_index, instruction->input_value);
    }
    request_bytes = input->count * sizeof(uint32_t);
    result_bytes = (uint64_t)LAPLACE_UNIVERSAL_AST_RECEIPT_WORDS * sizeof(uint32_t);
    if (request_bytes > UINT64_MAX - result_bytes ||
        request_bytes + result_bytes > program->context->resource_grant.memory_bytes) {
        return laplace_isa_ast_fail(
            error, LAPLACE_ISA_RESOURCE_INSUFFICIENT,
            instruction_index, instruction->output_value);
    }
    return LAPLACE_ISA_OK;
}

static laplace_isa_status execute_universal_ast_apply_packet(
    laplace_isa_program* program,
    const laplace_isa_instruction* instruction) {
#if defined(LAPLACE_TEST_ISA_REJECT_RESULT_ALIAS) || \
    defined(LAPLACE_TEST_ISA_STANDING_ORDER_BYPASS)
    /* These mutation libraries recompile isa.c in isolation to prove unrelated
     * ISA defects. They deliberately do not link the canonical engine. Keep the
     * new AST opcode present in the generated registry but fail it closed rather
     * than importing or duplicating AST semantics into those mutation binaries. */
    (void)program;
    (void)instruction;
    return LAPLACE_ISA_INPUT_OUT_OF_RANGE;
#else
    laplace_isa_value_view* input;
    laplace_isa_value_view* output;
    laplace_universal_ast_packet_receipt receipt;
    laplace_universal_ast_status status;

    if (program == NULL || instruction == NULL || program->values == NULL) {
        return LAPLACE_ISA_INVALID_ARGUMENT;
    }
    input = &program->values[instruction->input_value];
    output = &program->values[instruction->output_value];
    status = laplace_universal_ast_packet_validate_words(
        (const uint32_t*)input->data,
        (size_t)input->count,
        &receipt);
    if (status == LAPLACE_UNIVERSAL_AST_MEMORY_FAILURE) {
        return LAPLACE_ISA_RESOURCE_INSUFFICIENT;
    }
    if (status != LAPLACE_UNIVERSAL_AST_OK) {
        return LAPLACE_ISA_INPUT_OUT_OF_RANGE;
    }
    laplace_isa_ast_encode_receipt(&receipt, (uint32_t*)output->data);
    output->count = LAPLACE_UNIVERSAL_AST_RECEIPT_WORDS;
    return LAPLACE_ISA_OK;
#endif
}

#endif