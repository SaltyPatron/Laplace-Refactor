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
    LAPLACE_COGNITION_RESPONSE_SCAN_VERSION = 1,
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

/*
 * A response scan is the bounded native "tug this strand" execution. It starts
 * from one or more canonical entities, asks the supplied typed provider set for
 * every admissible crossing at the current frontier, preserves every distinct
 * path state, and repeats until max_depth or an exact finite boundary is met.
 *
 * relation_mask is an admissibility envelope for this scan, not a selected
 * natural-language intent. A raw prompt orientation pass is expected to use the
 * complete relation-family mask and let witnessed structure determine which
 * channels actually respond.
 */
typedef struct laplace_cognition_response_scan_program {
    laplace_digest256 program_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    uint64_t max_frontier_states;
    uint64_t max_candidate_records;
    uint64_t max_provider_calls;
    uint32_t max_depth;
    uint32_t relation_mask;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_response_scan_program;

typedef struct laplace_cognition_response_scan_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 field_receipt_id;
    uint64_t expanded_state_count;
    uint64_t emitted_state_count;
    uint64_t candidate_count;
    uint64_t response_count;
    uint64_t provider_call_count;
    uint64_t rows_examined;
    uint64_t index_plan_count;
    uint64_t crossing_count;
    uint32_t relation_family_mask;
    uint32_t source_layer_mask;
    uint32_t limiting_disposition;
    uint32_t version;
    uint32_t status;
    uint32_t reserved;
} laplace_cognition_response_scan_receipt;

typedef enum laplace_cognition_response_status {
    LAPLACE_COGNITION_RESPONSE_OK = 0,
    LAPLACE_COGNITION_RESPONSE_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_RESPONSE_INVALID_CANDIDATE = 2,
    LAPLACE_COGNITION_RESPONSE_RANGE = 3,
    LAPLACE_COGNITION_RESPONSE_OVERFLOW = 4,
    LAPLACE_COGNITION_RESPONSE_MEMORY_FAILURE = 5,
    LAPLACE_COGNITION_RESPONSE_PROVIDER_FAILURE = 6,
    LAPLACE_COGNITION_RESPONSE_PROVIDER_CONTRACT = 7,
    LAPLACE_COGNITION_RESPONSE_INCOMPLETE = 8
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

/*
 * Execute the response field over a live provider set. Successful publication is
 * all-or-nothing: if any provider reports an incomplete boundary, any exact
 * candidate batch does not fit, or any frontier/resource bound is hit before the
 * declared depth closes, no candidate or response prefix is published.
 *
 * Returned candidates retain a scan-global source_state_index, so a later
 * interpretation compiler can reconstruct which independently responding
 * frontier states supported each target. The response entries are the canonical
 * typed fold over that exact candidate estate.
 */
LAPLACE_API laplace_cognition_response_status
laplace_cognition_response_field_scan(
    const laplace_cognition_response_scan_program* program,
    const laplace_id128* initial_entities,
    size_t initial_entity_count,
    const laplace_cognition_observation_candidate_provider_v1* provider,
    laplace_cognition_observation_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_response_entry* responses,
    size_t response_capacity,
    size_t* response_count,
    laplace_cognition_response_scan_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
