#ifndef LAPLACE_OBSERVATION_QUERY_H
#define LAPLACE_OBSERVATION_QUERY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_forward_pass.h"
#include "laplace/export.h"
#include "laplace/persistence.h"
#include "laplace/query_search.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_observation_query_index laplace_observation_query_index;

typedef enum laplace_observation_query_relation {
    LAPLACE_OBSERVATION_QUERY_CONTAINER = UINT32_C(1),
    LAPLACE_OBSERVATION_QUERY_CONSTITUENT = UINT32_C(2),
    LAPLACE_OBSERVATION_QUERY_PREDECESSOR = UINT32_C(4),
    LAPLACE_OBSERVATION_QUERY_SUCCESSOR = UINT32_C(8),
    LAPLACE_OBSERVATION_QUERY_COOCCUR = UINT32_C(16)
} laplace_observation_query_relation;

enum {
    LAPLACE_OBSERVATION_QUERY_RELATION_MASK =
        LAPLACE_OBSERVATION_QUERY_CONTAINER |
        LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR |
        LAPLACE_OBSERVATION_QUERY_SUCCESSOR |
        LAPLACE_OBSERVATION_QUERY_COOCCUR,
    LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT = UINT32_C(1),
    LAPLACE_OBSERVATION_QUERY_BINDING_TERMINAL_RESULTS = UINT32_C(2),
    LAPLACE_OBSERVATION_QUERY_BINDING_KNOWN_FLAGS =
        LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT |
        LAPLACE_OBSERVATION_QUERY_BINDING_TERMINAL_RESULTS,
    LAPLACE_OBSERVATION_QUERY_INDEX_ABI_MAJOR = 1,
    LAPLACE_OBSERVATION_QUERY_INDEX_ABI_MINOR = 1,
    LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY = 1,
    LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD = 1,
    LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE = 2,
    LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC = 3
};

typedef struct laplace_observation_query_binding {
    laplace_digest256 binding_fingerprint;
    laplace_id128 anchor_entity_id;
    laplace_id128 goal_entity_id;
    uint32_t relation_mask;
    uint32_t maximum_results;
    uint32_t flags;
    uint32_t reserved;
} laplace_observation_query_binding;

/*
 * Canonical physicality/trajectory index input. This form deliberately contains
 * no query binding: an immutable observation estate must be reusable by requests
 * that did not exist when the index was built.
 */
typedef struct laplace_observation_query_index_base_input {
    const laplace_persistence_physicality_record* physicalities;
    size_t physicality_count;
    const laplace_persistence_trajectory_segment_record* trajectory_segments;
    size_t trajectory_segment_count;
    laplace_digest256 boundary_id;
    laplace_digest256 evidence_epoch;
    uint64_t maximum_candidate_records_per_expansion;
    uint32_t flags;
    uint32_t reserved;
} laplace_observation_query_index_base_input;

/*
 * Compatibility input for provider-owned/predeclared bindings. New runtime
 * request compilation should prefer laplace_observation_query_index_create_base
 * plus a request-bound provider so query intent never becomes index identity.
 */
typedef struct laplace_observation_query_index_input {
    const laplace_persistence_physicality_record* physicalities;
    size_t physicality_count;
    const laplace_persistence_trajectory_segment_record* trajectory_segments;
    size_t trajectory_segment_count;
    const laplace_observation_query_binding* bindings;
    size_t binding_count;
    laplace_digest256 boundary_id;
    laplace_digest256 evidence_epoch;
    uint64_t maximum_candidate_records_per_expansion;
    uint32_t flags;
    uint32_t reserved;
} laplace_observation_query_index_input;

typedef struct laplace_observation_query_index_summary {
    laplace_digest256 index_fingerprint;
    laplace_digest256 boundary_id;
    laplace_digest256 evidence_epoch;
    uint64_t physicality_count;
    uint64_t trajectory_segment_count;
    uint64_t occurrence_run_count;
    uint64_t logical_occurrence_count;
    uint64_t indexed_entity_count;
    uint64_t binding_count;
    uint64_t maximum_candidate_records_per_expansion;
    uint32_t status;
    uint32_t reserved;
} laplace_observation_query_index_summary;

typedef enum laplace_observation_query_status {
    LAPLACE_OBSERVATION_QUERY_OK = 0,
    LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT = 1,
    LAPLACE_OBSERVATION_QUERY_BINDING_INVALID = 2,
    LAPLACE_OBSERVATION_QUERY_PHYSICALITY_INVALID = 3,
    LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID = 4,
    LAPLACE_OBSERVATION_QUERY_REFERENCE_INVALID = 5,
    LAPLACE_OBSERVATION_QUERY_DUPLICATE = 6,
    LAPLACE_OBSERVATION_QUERY_COLLISION = 7,
    LAPLACE_OBSERVATION_QUERY_OVERFLOW = 8,
    LAPLACE_OBSERVATION_QUERY_MEMORY_FAILURE = 9
} laplace_observation_query_status;

/*
 * Produces the typed query coordinate used by the observation provider for one
 * canonical entity. The coordinate is a query/search address only; it does not
 * replace or remint the canonical 128-bit content identity.
 */
LAPLACE_API laplace_observation_query_status
laplace_observation_query_entity_coordinate(
    const laplace_id128* entity_id,
    laplace_digest256* coordinate);

/*
 * Content-identifies a typed observation-query binding. Goal presence is
 * represented only by the explicit GOAL_PRESENT flag; the bit pattern of the
 * goal entity is never interpreted as absence.
 */
LAPLACE_API laplace_observation_query_status
laplace_observation_query_binding_identify(
    const laplace_observation_query_binding* binding,
    laplace_digest256* binding_fingerprint);

/*
 * Creates an immutable, validated observation index from canonical physicality
 * plus packed trajectory state without embedding any future query intent into
 * the index. This is the runtime-request path.
 */
LAPLACE_API laplace_observation_query_status
laplace_observation_query_index_create_base(
    const laplace_observation_query_index_base_input* input,
    laplace_observation_query_index** index);

/*
 * Creates the same immutable physicality/trajectory index while also installing
 * predeclared compatibility bindings. Index construction validates physicality
 * identities, contiguous segment ordinals, packed trajectory decoding,
 * trajectory fingerprints, logical occurrence counts and binding identities.
 */
LAPLACE_API laplace_observation_query_status
laplace_observation_query_index_create(
    const laplace_observation_query_index_input* input,
    laplace_observation_query_index** index);

LAPLACE_API void laplace_observation_query_index_destroy(
    laplace_observation_query_index** index);

LAPLACE_API laplace_observation_query_status
laplace_observation_query_index_summary_get(
    const laplace_observation_query_index* index,
    laplace_observation_query_index_summary* summary);

/*
 * Exposes predeclared compatibility bindings through the common bounded set-wise
 * query-search provider contract. Runtime request bindings should use the
 * request-bound provider API rather than mutating this immutable index.
 */
LAPLACE_API laplace_observation_query_status
laplace_observation_query_search_provider(
    laplace_observation_query_index* index,
    laplace_query_search_provider_v1* provider);

/*
 * Exposes predeclared compatibility bindings through the native cognition
 * forward-pass provider contract. Runtime request execution should use the
 * request-bound provider API so the compiled policy remains request-owned.
 */
LAPLACE_API laplace_observation_query_status
laplace_observation_query_cognition_provider(
    laplace_observation_query_index* index,
    laplace_cognition_forward_provider_v1* provider);

#ifdef __cplusplus
}
#endif

#endif
