#ifndef LAPLACE_QUERY_SEARCH_H
#define LAPLACE_QUERY_SEARCH_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/query_search.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_query_search_state {
    laplace_digest256 state_id;
    laplace_digest256 anchor_id;
    laplace_digest256 dominance_key;
    laplace_digest256 binding_fingerprint;
    laplace_digest256 obligation_fingerprint;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_epoch;
    laplace_digest256 boundary_id;
    uint64_t heuristic_cost;
    uint32_t depth;
    uint32_t flags;
} laplace_query_search_state;

typedef struct laplace_query_search_transition {
    laplace_digest256 transition_id;
    laplace_digest256 source_state_id;
    laplace_query_search_state target;
    laplace_digest256 law_fingerprint;
    laplace_digest256 evidence_root_fingerprint;
    laplace_digest256 calculation_receipt;
    uint64_t cost_components[LAPLACE_QUERY_SEARCH_COST_COMPONENT_COUNT];
    uint32_t relation_family;
    uint32_t source_layer;
    uint32_t direction;
    uint32_t flags;
} laplace_query_search_transition;

typedef struct laplace_query_search_budget {
    uint64_t max_expanded_states;
    uint64_t max_transition_records;
    uint64_t max_emitted_states;
    uint64_t max_frontier_states;
    uint64_t max_memory_bytes;
    uint64_t max_io_operations;
    uint64_t max_database_operations;
    uint64_t max_provider_calls;
    uint32_t max_depth;
    uint32_t requested_path_count;
    uint32_t frontier_batch_width;
    uint32_t transition_batch_capacity;
} laplace_query_search_budget;

typedef struct laplace_query_search_program {
    laplace_digest256 program_id;
    laplace_digest256 goal_id;
    laplace_digest256 boundary_id;
    laplace_digest256 evidence_epoch;
    laplace_digest256 heuristic_proof_fingerprint;
    laplace_digest256 result_contract_fingerprint;
    uint64_t cost_weights[LAPLACE_QUERY_SEARCH_COST_COMPONENT_COUNT];
    laplace_query_search_budget budget;
    uint32_t flags;
    uint32_t reserved;
} laplace_query_search_program;

typedef struct laplace_query_search_expansion_receipt {
    laplace_digest256 receipt_id;
    uint64_t frontier_state_count;
    uint64_t rows_examined;
    uint64_t index_plan_count;
    uint64_t crossing_count;
    uint64_t emitted_transition_count;
    uint64_t io_operations;
    uint64_t database_operations;
    uint32_t limiting_disposition;
    uint32_t flags;
} laplace_query_search_expansion_receipt;

typedef int (*laplace_query_search_expand_batch_fn)(
    void* provider_state,
    const laplace_query_search_state* frontier_states,
    const uint64_t* accumulated_costs,
    size_t frontier_state_count,
    laplace_query_search_transition* transitions,
    size_t transition_capacity,
    size_t* transition_count,
    laplace_query_search_expansion_receipt* receipt);

typedef struct laplace_query_search_provider_v1 {
    void* state;
    laplace_query_search_expand_batch_fn expand_batch;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_query_search_provider_v1;

typedef struct laplace_query_search_path_summary {
    laplace_digest256 path_id;
    laplace_digest256 terminal_state_id;
    uint64_t total_cost;
    uint64_t transition_count;
    uint64_t independent_evidence_root_count;
    uint32_t terminal_depth;
    uint32_t rank;
} laplace_query_search_path_summary;

typedef struct laplace_query_search_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 program_fingerprint;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t expanded_state_count;
    uint64_t transition_record_count;
    uint64_t emitted_state_count;
    uint64_t reopened_state_count;
    uint64_t pruned_state_count;
    uint64_t rows_examined;
    uint64_t index_plan_count;
    uint64_t crossing_count;
    uint64_t io_operations;
    uint64_t database_operations;
    uint64_t provider_call_count;
    uint64_t estimated_peak_memory_bytes;
    uint32_t path_count;
    uint32_t disposition;
    uint32_t flags;
    uint32_t version;
    uint32_t status;
    uint32_t reserved;
} laplace_query_search_receipt;

typedef struct laplace_query_search_result laplace_query_search_result;

typedef enum laplace_query_search_status {
    LAPLACE_QUERY_SEARCH_OK = 0,
    LAPLACE_QUERY_SEARCH_INVALID_ARGUMENT = 1,
    LAPLACE_QUERY_SEARCH_PROGRAM_INVALID = 2,
    LAPLACE_QUERY_SEARCH_STATE_INVALID = 3,
    LAPLACE_QUERY_SEARCH_PROVIDER_FAILURE = 4,
    LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT = 5,
    LAPLACE_QUERY_SEARCH_MEMORY_FAILURE = 6,
    LAPLACE_QUERY_SEARCH_OVERFLOW = 7,
    LAPLACE_QUERY_SEARCH_RESULT_RANGE = 8
} laplace_query_search_status;

enum {
    LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR = 0
};

LAPLACE_API laplace_query_search_status
laplace_query_search_execute(
    const laplace_query_search_program* program,
    const laplace_query_search_state* initial_states,
    size_t initial_state_count,
    const laplace_query_search_provider_v1* provider,
    laplace_query_search_result** result,
    laplace_query_search_receipt* receipt);

LAPLACE_API size_t
laplace_query_search_result_path_count(const laplace_query_search_result* result);

LAPLACE_API laplace_query_search_status
laplace_query_search_result_path(
    const laplace_query_search_result* result,
    size_t path_index,
    laplace_query_search_path_summary* summary);

LAPLACE_API laplace_query_search_status
laplace_query_search_result_path_steps(
    const laplace_query_search_result* result,
    size_t path_index,
    laplace_query_search_transition* transitions,
    size_t transition_capacity,
    size_t* transition_count);

LAPLACE_API void
laplace_query_search_result_destroy(laplace_query_search_result** result);

#ifdef __cplusplus
}
#endif

#endif
