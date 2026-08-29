#ifndef LAPLACE_COGNITION_FORWARD_PASS_H
#define LAPLACE_COGNITION_FORWARD_PASS_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_guidance.h"
#include "laplace/contract/cognition_forward_pass.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_cognition_forward_program {
    laplace_digest256 program_id;
    laplace_digest256 result_contract_fingerprint;
    uint64_t max_layers;
    uint64_t max_provider_calls;
    uint64_t max_projected_queries;
    uint64_t max_candidate_operations;
    uint64_t max_resolutions;
    uint64_t max_resource_cost;
    uint64_t max_io_operations;
    uint64_t max_database_operations;
    uint32_t candidate_operation_capacity;
    uint32_t resolution_capacity;
    uint32_t flags;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_forward_program;

typedef struct laplace_cognition_forward_enumeration_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 state_id;
    laplace_digest256 projection_fingerprint;
    uint64_t projected_query_count;
    uint64_t candidate_operation_count;
    uint64_t resource_cost;
    uint64_t io_operations;
    uint64_t database_operations;
    uint32_t status;
    uint32_t flags;
} laplace_cognition_forward_enumeration_receipt;

typedef struct laplace_cognition_forward_execution_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 state_id;
    laplace_digest256 operation_id;
    laplace_digest256 result_fingerprint;
    uint64_t resolution_count;
    uint64_t resource_cost;
    uint64_t io_operations;
    uint64_t database_operations;
    uint32_t status;
    uint32_t flags;
} laplace_cognition_forward_execution_receipt;

typedef int (*laplace_cognition_forward_enumerate_fn)(
    void* provider_state,
    const laplace_cognition_guidance_state* state,
    const laplace_cognition_query_projection* projections,
    size_t projection_count,
    laplace_cognition_guidance_operation* operations,
    size_t operation_capacity,
    size_t* operation_count,
    laplace_cognition_forward_enumeration_receipt* receipt);

typedef int (*laplace_cognition_forward_execute_fn)(
    void* provider_state,
    const laplace_cognition_guidance_state* state,
    const laplace_cognition_guidance_operation* operation,
    const laplace_cognition_query_projection* projections,
    size_t projection_count,
    laplace_cognition_resolution* resolutions,
    size_t resolution_capacity,
    size_t* resolution_count,
    laplace_cognition_forward_execution_receipt* receipt);

typedef struct laplace_cognition_forward_provider_v1 {
    void* state;
    laplace_cognition_forward_enumerate_fn enumerate;
    laplace_cognition_forward_execute_fn execute;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_forward_provider_v1;

typedef struct laplace_cognition_forward_layer_receipt {
    laplace_digest256 layer_receipt_id;
    laplace_digest256 prior_state_id;
    laplace_digest256 projection_fingerprint;
    laplace_digest256 enumeration_receipt_id;
    laplace_digest256 decision_receipt_id;
    laplace_digest256 selected_operation_id;
    laplace_digest256 execution_receipt_id;
    laplace_digest256 resolution_set_fingerprint;
    laplace_digest256 next_state_id;
    uint64_t projected_query_count;
    uint64_t candidate_operation_count;
    uint64_t eligible_operation_count;
    uint64_t resolution_count;
    uint64_t remaining_open_count;
    uint64_t resource_cost;
    uint64_t io_operations;
    uint64_t database_operations;
    uint32_t selected_kind;
    uint32_t completion;
    uint32_t status;
    uint32_t flags;
} laplace_cognition_forward_layer_receipt;

typedef struct laplace_cognition_forward_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 program_fingerprint;
    laplace_digest256 initial_state_id;
    laplace_digest256 final_state_id;
    laplace_digest256 layer_trace_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t layer_count;
    uint64_t provider_call_count;
    uint64_t projected_query_count;
    uint64_t candidate_operation_count;
    uint64_t resolution_count;
    uint64_t resource_cost;
    uint64_t io_operations;
    uint64_t database_operations;
    uint64_t final_remaining_required_count;
    uint32_t final_completion;
    uint32_t disposition;
    uint32_t status;
    uint32_t version;
    uint32_t flags;
} laplace_cognition_forward_receipt;

typedef struct laplace_cognition_forward_result laplace_cognition_forward_result;

typedef enum laplace_cognition_forward_status {
    LAPLACE_COGNITION_FORWARD_OK = 0,
    LAPLACE_COGNITION_FORWARD_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_FORWARD_PROGRAM_INVALID = 2,
    LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE = 3,
    LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT = 4,
    LAPLACE_COGNITION_FORWARD_MEMORY_FAILURE = 5,
    LAPLACE_COGNITION_FORWARD_OVERFLOW = 6,
    LAPLACE_COGNITION_FORWARD_RANGE = 7
} laplace_cognition_forward_status;

enum {
    LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR = 0
};

LAPLACE_API laplace_cognition_forward_status
laplace_cognition_forward_pass_execute(
    const laplace_cognition_forward_program* program,
    const laplace_cognition_guidance_state* initial_state,
    const laplace_cognition_forward_provider_v1* provider,
    laplace_cognition_forward_result** result,
    laplace_cognition_forward_receipt* receipt);

LAPLACE_API void
laplace_cognition_forward_result_destroy(laplace_cognition_forward_result** result);

LAPLACE_API size_t
laplace_cognition_forward_result_layer_count(
    const laplace_cognition_forward_result* result);

LAPLACE_API laplace_cognition_forward_status
laplace_cognition_forward_result_layer(
    const laplace_cognition_forward_result* result,
    size_t layer_index,
    laplace_cognition_forward_layer_receipt* layer);

LAPLACE_API laplace_cognition_forward_status
laplace_cognition_forward_result_final_state_clone(
    const laplace_cognition_forward_result* result,
    laplace_cognition_guidance_state** state);

#ifdef __cplusplus
}
#endif

#endif
