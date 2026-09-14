#ifndef LAPLACE_COGNITION_RESPONSE_FIELD_H
#define LAPLACE_COGNITION_RESPONSE_FIELD_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_observation_request.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_RESPONSE_FIELD_VERSION = 1,
    LAPLACE_COGNITION_RESPONSE_RELATION_CHANNEL_COUNT = 6,
    LAPLACE_COGNITION_RESPONSE_SOURCE_CHANNEL_COUNT = 5
};

typedef struct laplace_cognition_response_entry {
    laplace_id128 entity_id;
    laplace_digest256 response_fingerprint;
    uint64_t source_state_count;
    uint64_t candidate_count;
    uint64_t multiplicity_mass;
    uint64_t minimum_gap;
    uint64_t independent_evidence_root_count;
    uint64_t relation_channel_mass[LAPLACE_COGNITION_RESPONSE_RELATION_CHANNEL_COUNT];
    uint64_t source_channel_mass[LAPLACE_COGNITION_RESPONSE_SOURCE_CHANNEL_COUNT];
    uint32_t relation_family_mask;
    uint32_t source_layer_mask;
    uint32_t direction_mask;
    uint32_t flags;
} laplace_cognition_response_entry;

typedef struct laplace_cognition_response_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t candidate_count;
    uint64_t response_count;
    uint64_t distinct_source_state_count;
    uint64_t independent_evidence_root_count;
    uint32_t version;
    uint32_t status;
} laplace_cognition_response_receipt;

typedef enum laplace_cognition_response_status {
    LAPLACE_COGNITION_RESPONSE_OK = 0,
    LAPLACE_COGNITION_RESPONSE_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_RESPONSE_INVALID_CANDIDATE = 2,
    LAPLACE_COGNITION_RESPONSE_RANGE = 3,
    LAPLACE_COGNITION_RESPONSE_OVERFLOW = 4,
    LAPLACE_COGNITION_RESPONSE_MEMORY_FAILURE = 5
} laplace_cognition_response_status;

/*
 * Fold one bounded set of already-enumerated typed crossings into the
 * query-relative response field. The fold deliberately does not flatten the
 * response to a universal relevance score. Relation families, provider/source
 * planes, direction, multiplicity, gap, source-state incidence and independent
 * evidence roots remain separately readable.
 *
 * A target that is reached from several distinct frontier states therefore
 * records that joint back-reaction explicitly through source_state_count. The
 * caller may use that typed field to build interpretation factors, hard gates,
 * search guidance or operator inputs without pretending all channels are
 * commensurate scalar costs.
 */
LAPLACE_API laplace_cognition_response_status
laplace_cognition_response_field_measure(
    const laplace_cognition_observation_candidate* candidates,
    size_t candidate_count,
    laplace_cognition_response_entry* responses,
    size_t response_capacity,
    size_t* response_count,
    laplace_cognition_response_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
