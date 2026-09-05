#ifndef LAPLACE_COGNITION_OBSERVATION_REQUEST_H
#define LAPLACE_COGNITION_OBSERVATION_REQUEST_H

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
    LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION = 1
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
    LAPLACE_COGNITION_OBSERVATION_REQUEST_EXECUTION_FAILURE = 10
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
