#ifndef LAPLACE_ISA_H
#define LAPLACE_ISA_H

#include <stdint.h>

#include "laplace/contract/isa.h"
#include "laplace/export.h"
#include "laplace/identity.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct laplace_framework_context;

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
    const struct laplace_framework_context* context;
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
    LAPLACE_ISA_CONTEXT_INVALID = 14
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
