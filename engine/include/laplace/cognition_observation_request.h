#ifndef LAPLACE_COGNITION_OBSERVATION_REQUEST_H
#define LAPLACE_COGNITION_OBSERVATION_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_forward_pass.h"
#include "laplace/export.h"
#include "laplace/identity.h"
#include "laplace/observation_query.h"
#include "laplace/query_search.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT = UINT32_C(1),
    LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS = UINT32_C(2),
    LAPLACE_COGNITION_OBSERVATION_REQUEST_ALLOW_TYPED_UNRESOLVED = UINT32_C(4),
    LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE = UINT32_C(8),
    LAPLACE_COGNITION_OBSERVATION_REQUEST_KNOWN_FLAGS =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_ALLOW_TYPED_UNRESOLVED |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION = 1,
    LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR = 0
};

typedef struct laplace_cognition_observation_forward_limits {
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
} laplace_cognition_observation_forward_limits;

typedef struct laplace_cognition_observation_request {
    laplace_id128 anchor_entity_id;
    laplace_id128 goal_entity_id;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    laplace_digest256 authority_id;
    laplace_digest256 result_contract_fingerprint;
    laplace_query_search_budget search_budget;
    laplace_cognition_observation_forward_limits forward_limits;
    uint32_t relation_mask;
    uint32_t maximum_results;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_observation_request;

typedef struct laplace_cognition_observation_compiled_request {
    laplace_digest256 request_fingerprint;
    laplace_observation_query_binding binding;
    laplace_query_search_state initial_search_state;
    laplace_query_search_program search_program;
    laplace_cognition_forward_program forward_program;
    laplace_cognition_guidance_state* guidance_state;
    uint32_t status;
    uint32_t reserved;
} laplace_cognition_observation_compiled_request;

typedef struct laplace_cognition_observation_request_provider
    laplace_cognition_observation_request_provider;

/*
 * A persistence/query backend returns only typed observation candidates. It does
 * not construct search states, transitions, cognition operations, resolutions,
 * guidance, completion decisions, or forward receipts. `source_state_index`
 * identifies which source in the supplied frontier produced the candidate.
 * `observation_fingerprint` identifies the durable structural/semantic record
 * that justifies the crossing. `evidence_root_fingerprint` is independently
 * optional: zero means the crossing is calculated from structure alone, while a
 * nonzero value identifies an independent testimonial/evidence root. The source
 * layer remains explicit so storage adapters do not flatten those two meanings.
 */
typedef struct laplace_cognition_observation_candidate {
    laplace_id128 target_entity_id;
    laplace_digest256 observation_fingerprint;
    laplace_digest256 evidence_root_fingerprint;
    uint64_t source_state_index;
    uint64_t source_logical_ordinal;
    uint64_t target_logical_ordinal;
    uint64_t multiplicity;
    uint64_t gap;
    uint32_t relation;
    uint32_t source_layer;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_observation_candidate;

typedef struct laplace_cognition_observation_candidate_usage {
    uint64_t rows_examined;
    uint64_t index_plan_count;
    uint64_t crossing_count;
    uint64_t io_operations;
    uint64_t database_operations;
    uint32_t limiting_disposition;
    uint32_t flags;
} laplace_cognition_observation_candidate_usage;

typedef int (*laplace_cognition_observation_enumerate_candidates_fn)(
    void* provider_state,
    const laplace_observation_query_binding* binding,
    const laplace_id128* source_entity_ids,
    const laplace_query_search_state* frontier_states,
    const uint64_t* accumulated_costs,
    size_t frontier_state_count,
    laplace_cognition_observation_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_observation_candidate_usage* usage);

typedef struct laplace_cognition_observation_candidate_provider_v1 {
    void* state;
    laplace_digest256 provider_fingerprint;
    uint64_t maximum_candidate_records_per_expansion;
    laplace_cognition_observation_enumerate_candidates_fn enumerate_candidates;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_observation_candidate_provider_v1;

/*
 * Native provider composition keeps physical candidate providers separate while
 * presenting one bounded candidate surface to the canonical cognition engine.
 * The set owns copies of provider descriptors, not their opaque states: every
 * child provider state must remain alive until the set is destroyed. Child
 * providers still enumerate candidates only; this composition layer does not
 * acquire search, guidance, completion, realization, or receipt authority.
 */
typedef struct laplace_cognition_observation_candidate_provider_set
    laplace_cognition_observation_candidate_provider_set;

typedef enum laplace_cognition_observation_provider_set_status {
    LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK = 0,
    LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_INVALID_PROVIDER = 2,
    LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_DUPLICATE_PROVIDER = 3,
    LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OVERFLOW = 4,
    LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_MEMORY_FAILURE = 5
} laplace_cognition_observation_provider_set_status;

LAPLACE_API laplace_cognition_observation_provider_set_status
laplace_cognition_observation_candidate_provider_set_create(
    const laplace_cognition_observation_candidate_provider_v1* providers,
    size_t provider_count,
    laplace_cognition_observation_candidate_provider_set** provider_set,
    laplace_cognition_observation_candidate_provider_v1* composite_provider);

LAPLACE_API void
laplace_cognition_observation_candidate_provider_set_destroy(
    laplace_cognition_observation_candidate_provider_set** provider_set);

/*
 * Terminal answer record retained from the canonical query-search result before
 * that internal result is destroyed.  `entity_id` is the actual selected target
 * entity and is therefore directly consumable by a realization transport.  Path
 * identity/cost and the final crossing remain attached so realization does not
 * erase how the answer was obtained.
 */
typedef struct laplace_cognition_observation_answer {
    laplace_id128 entity_id;
    laplace_digest256 path_id;
    laplace_digest256 terminal_state_id;
    uint64_t total_cost;
    uint64_t transition_count;
    uint64_t independent_evidence_root_count;
    uint32_t relation_family;
    uint32_t source_layer;
    uint32_t direction;
    uint32_t rank;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_observation_answer;

typedef struct laplace_cognition_observation_result
    laplace_cognition_observation_result;

typedef enum laplace_cognition_observation_request_status {
    LAPLACE_COGNITION_OBSERVATION_REQUEST_OK = 0,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_FLAGS = 2,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_RELATION = 3,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_LIMITS = 4,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_BINDING_FAILURE = 5,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_GUIDANCE_FAILURE = 6,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_COORDINATE_FAILURE = 7,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_PROVIDER_FAILURE = 8,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_MEMORY_FAILURE = 9,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_EXECUTION_FAILURE = 10,
    LAPLACE_COGNITION_OBSERVATION_REQUEST_RESULT_RANGE = 11
} laplace_cognition_observation_request_status;

/*
 * Identifies the complete typed request. Absence of a goal is represented by the
 * explicit GOAL_PRESENT flag; goal bytes never decide presence. Resource limits
 * are part of request identity because a bounded program is not the same program
 * as an unbounded or differently bounded one.
 */
LAPLACE_API laplace_cognition_observation_request_status
laplace_cognition_observation_request_identify(
    const laplace_cognition_observation_request* request,
    laplace_digest256* request_fingerprint);

/*
 * Compiles one exact typed observation request into the existing canonical
 * guidance, forward-pass and typed-search contracts. This is request lowering,
 * not a private query engine: execution still goes through the shared cognition
 * forward provider and query-search provider surfaces.
 *
 * The caller owns `compiled->guidance_state` after success and releases it with
 * laplace_cognition_observation_compiled_request_destroy().
 */
LAPLACE_API laplace_cognition_observation_request_status
laplace_cognition_observation_request_compile(
    const laplace_cognition_observation_request* request,
    laplace_cognition_observation_compiled_request* compiled);

LAPLACE_API void
laplace_cognition_observation_compiled_request_destroy(
    laplace_cognition_observation_compiled_request* compiled);

/*
 * Binds the exact compiled search program and initial search state to a cognition
 * provider over one immutable observation index. Unlike the compatibility
 * observation provider, this provider does not synthesize a private one-hop
 * search policy: forward execution consumes the request's own finite search
 * budget, goal semantics and boundary declaration.
 *
 * The immutable index must outlive the returned provider handle. The compiled
 * request need only remain valid for this creation call: the provider copies the
 * binding and execution policy/state it consumes later. Release the provider with
 * laplace_cognition_observation_request_provider_destroy().
 */
LAPLACE_API laplace_cognition_observation_request_status
laplace_cognition_observation_request_cognition_provider(
    laplace_observation_query_index* index,
    const laplace_cognition_observation_compiled_request* compiled,
    laplace_cognition_observation_request_provider** provider_state,
    laplace_cognition_forward_provider_v1* provider);

LAPLACE_API void
laplace_cognition_observation_request_provider_destroy(
    laplace_cognition_observation_request_provider** provider_state);

/*
 * Executes one complete typed request over an external observation estate while
 * retaining semantic ownership in the native engine. The backend enumerates only
 * typed observation candidates. Native code derives search transitions, executes
 * the compiled bounded search, constructs/selects cognition operations, applies
 * resolutions, decides completion, and receipts the full forward pass. The
 * terminal entity/path records are retained in `observation_result` for the
 * realization layer; callers do not need to rerun or reverse a result hash.
 */
LAPLACE_API laplace_cognition_observation_request_status
laplace_cognition_observation_request_execute_with_candidate_provider(
    const laplace_cognition_observation_request* request,
    const laplace_cognition_observation_candidate_provider_v1* provider,
    laplace_cognition_observation_result** observation_result,
    laplace_cognition_forward_result** forward_result,
    laplace_cognition_forward_receipt* receipt);

LAPLACE_API size_t
laplace_cognition_observation_result_answer_count(
    const laplace_cognition_observation_result* result);

LAPLACE_API laplace_cognition_observation_request_status
laplace_cognition_observation_result_answer(
    const laplace_cognition_observation_result* result,
    size_t answer_index,
    laplace_cognition_observation_answer* answer);

LAPLACE_API void
laplace_cognition_observation_result_destroy(
    laplace_cognition_observation_result** result);

/*
 * Executes one complete typed request through the canonical compile, provider,
 * and bounded forward-pass surfaces. This is the public native boundary for a
 * caller that has an admitted immutable observation index; it does not expose
 * hand-built guidance or private search state to transports.
 */
LAPLACE_API laplace_cognition_observation_request_status
laplace_cognition_observation_request_execute(
    laplace_observation_query_index* index,
    const laplace_cognition_observation_request* request,
    laplace_cognition_forward_result** result,
    laplace_cognition_forward_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
