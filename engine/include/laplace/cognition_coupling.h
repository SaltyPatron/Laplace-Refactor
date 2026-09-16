#ifndef LAPLACE_COGNITION_COUPLING_H
#define LAPLACE_COGNITION_COUPLING_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_observation_request.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_COUPLING_VERSION = 1,
    LAPLACE_COGNITION_COUPLING_SEED_PRIMARY = UINT32_C(1),
    LAPLACE_COGNITION_COUPLING_SEED_KNOWN_FLAGS =
        LAPLACE_COGNITION_COUPLING_SEED_PRIMARY,
    LAPLACE_COGNITION_COUPLING_RESPONSE_SEMANTIC = UINT32_C(1),
    LAPLACE_COGNITION_COUPLING_RESPONSE_MULTI_SEED = UINT32_C(2),
    LAPLACE_COGNITION_COUPLING_RESPONSE_EVIDENCE = UINT32_C(4),
    LAPLACE_COGNITION_COUPLING_RESPONSE_STANDING = UINT32_C(8),
    LAPLACE_COGNITION_COUPLING_RESPONSE_KNOWN_FLAGS =
        LAPLACE_COGNITION_COUPLING_RESPONSE_SEMANTIC |
        LAPLACE_COGNITION_COUPLING_RESPONSE_MULTI_SEED |
        LAPLACE_COGNITION_COUPLING_RESPONSE_EVIDENCE |
        LAPLACE_COGNITION_COUPLING_RESPONSE_STANDING
};

typedef enum laplace_cognition_coupling_status {
    LAPLACE_COGNITION_COUPLING_OK = 0,
    LAPLACE_COGNITION_COUPLING_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_COUPLING_INVALID_PROVIDER = 2,
    LAPLACE_COGNITION_COUPLING_PROVIDER_FAILURE = 3,
    LAPLACE_COGNITION_COUPLING_PROVIDER_CONTRACT = 4,
    LAPLACE_COGNITION_COUPLING_EXHAUSTED = 5,
    LAPLACE_COGNITION_COUPLING_MEMORY_FAILURE = 6,
    LAPLACE_COGNITION_COUPLING_OVERFLOW = 7,
    LAPLACE_COGNITION_COUPLING_RESULT_RANGE = 8
} laplace_cognition_coupling_status;

typedef enum laplace_cognition_coupling_disposition {
    LAPLACE_COGNITION_COUPLING_COMPLETE = 1,
    LAPLACE_COGNITION_COUPLING_PARTIAL = 2,
    LAPLACE_COGNITION_COUPLING_NO_RESPONSE = 3,
    LAPLACE_COGNITION_COUPLING_UNKNOWN = 4
} laplace_cognition_coupling_disposition;

typedef struct laplace_cognition_coupling_seed {
    laplace_id128 entity_id;
    laplace_digest256 occurrence_id;
    uint64_t logical_ordinal;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_coupling_seed;

typedef struct laplace_cognition_coupling_request {
    const laplace_cognition_coupling_seed* seeds;
    size_t seed_count;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    laplace_digest256 authority_id;
    uint64_t maximum_witnesses;
    uint64_t maximum_frontier_states;
    uint64_t maximum_provider_calls;
    uint64_t maximum_memory_bytes;
    uint32_t maximum_depth;
    uint32_t frontier_batch_width;
    uint32_t relation_mask;
    uint32_t version;
} laplace_cognition_coupling_request;

/* Exact typed crossing caused by one occurrence perturbation. No conversion of
 * evidence uncertainty or Glicko state into a universal relevance score occurs.
 * A standing crossing preserves its complete immutable rating/RD/volatility state. */
typedef struct laplace_cognition_coupling_witness {
    laplace_digest256 witness_id;
    laplace_id128 source_entity_id;
    laplace_id128 target_entity_id;
    laplace_id128 relation_id;
    laplace_digest256 seed_occurrence_id;
    laplace_digest256 observation_fingerprint;
    laplace_digest256 evidence_root_fingerprint;
    laplace_standing_state standing;
    uint64_t seed_index;
    uint64_t source_logical_ordinal;
    uint64_t target_logical_ordinal;
    uint64_t multiplicity;
    uint64_t gap;
    uint64_t evidence_uncertainty_numerator;
    uint64_t evidence_uncertainty_denominator;
    uint32_t depth;
    uint32_t relation_family;
    uint32_t source_layer;
    uint32_t direction;
    uint32_t flags;
} laplace_cognition_coupling_witness;

typedef struct laplace_cognition_coupling_response {
    laplace_id128 entity_id;
    laplace_digest256 response_fingerprint;
    laplace_digest256 seed_root_set_fingerprint;
    laplace_digest256 evidence_root_set_fingerprint;
    uint64_t seed_support_count;
    uint64_t witness_count;
    uint64_t independent_evidence_root_count;
    uint64_t semantic_witness_count;
    uint64_t physicality_witness_count;
    uint64_t testimony_witness_count;
    uint64_t calculation_witness_count;
    uint64_t geometry_witness_count;
    uint64_t standing_witness_count;
    uint64_t total_multiplicity;
    uint64_t total_gap;
    uint64_t minimum_gap;
    uint32_t minimum_depth;
    uint32_t relation_family_mask;
    uint32_t source_layer_mask;
    uint32_t direction_mask;
    uint32_t flags;
} laplace_cognition_coupling_response;

typedef struct laplace_cognition_coupling_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 seed_set_fingerprint;
    laplace_digest256 response_set_fingerprint;
    uint64_t rows_examined;
    uint64_t index_plan_count;
    uint64_t crossing_count;
    uint64_t io_operations;
    uint64_t database_operations;
    uint64_t provider_call_count;
    uint64_t generated_frontier_state_count;
    uint64_t witness_count;
    uint64_t response_count;
    uint32_t disposition;
    uint32_t version;
} laplace_cognition_coupling_receipt;

typedef struct laplace_cognition_coupling_result
    laplace_cognition_coupling_result;

LAPLACE_API laplace_cognition_coupling_status
laplace_cognition_coupling_execute(
    const laplace_cognition_coupling_request* request,
    const laplace_cognition_observation_candidate_provider_v1* provider,
    laplace_cognition_coupling_result** result,
    laplace_cognition_coupling_receipt* receipt);

LAPLACE_API size_t
laplace_cognition_coupling_response_count(
    const laplace_cognition_coupling_result* result);

LAPLACE_API laplace_cognition_coupling_status
laplace_cognition_coupling_response_read(
    const laplace_cognition_coupling_result* result,
    size_t response_index,
    laplace_cognition_coupling_response* response);

LAPLACE_API size_t
laplace_cognition_coupling_witness_count(
    const laplace_cognition_coupling_result* result);

LAPLACE_API laplace_cognition_coupling_status
laplace_cognition_coupling_witness_read(
    const laplace_cognition_coupling_result* result,
    size_t witness_index,
    laplace_cognition_coupling_witness* witness);

LAPLACE_API void
laplace_cognition_coupling_result_destroy(
    laplace_cognition_coupling_result** result);

#ifdef __cplusplus
}
#endif

#endif
