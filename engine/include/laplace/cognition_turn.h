#ifndef LAPLACE_COGNITION_TURN_H
#define LAPLACE_COGNITION_TURN_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_discourse_frame.h"
#include "laplace/cognition_observation_request.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE = UINT32_C(1),
    LAPLACE_COGNITION_TURN_KNOWN_FLAGS =
        LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE,
    LAPLACE_COGNITION_TURN_VERSION = 1,
    LAPLACE_COGNITION_TURN_POLICY_VERSION = 1,
    LAPLACE_COGNITION_TURN_RECEIPT_VERSION = 1
};

typedef enum laplace_cognition_turn_status {
    LAPLACE_COGNITION_TURN_OK = 0,
    LAPLACE_COGNITION_TURN_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_TURN_INVALID_FLAGS = 2,
    LAPLACE_COGNITION_TURN_PREVIOUS_FRAME_INVALID = 3,
    LAPLACE_COGNITION_TURN_DISCOURSE_MISMATCH = 4,
    LAPLACE_COGNITION_TURN_WORLD_MISMATCH = 5,
    LAPLACE_COGNITION_TURN_SEQUENCE_MISMATCH = 6,
    LAPLACE_COGNITION_TURN_REQUEST_INVALID = 7
} laplace_cognition_turn_status;

/*
 * One admitted incoming observation. The transport supplies identities already
 * produced by the universal decomposition/admission path; this compiler never
 * inspects prompt bytes or performs a keyword/intent lookup.
 *
 * `context_fingerprint` is the current external/framework context. The emitted
 * cognition request receives a derived context that additionally binds the
 * discourse identity, exact previous durable state (when present), occurrence,
 * and turn ordinal.
 */
typedef struct laplace_cognition_turn_input {
    laplace_digest256 discourse_id;
    laplace_id128 observation_entity_id;
    laplace_digest256 observation_occurrence_id;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    uint64_t turn_ordinal;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_turn_input;

/*
 * Finite cognition policy for one turn. This is deliberately typed separately
 * from the incoming observation so transports cannot smuggle session state into
 * search semantics. The fields lower directly into the canonical observation
 * request and are validated by that request contract before publication.
 */
typedef struct laplace_cognition_turn_policy {
    laplace_id128 goal_entity_id;
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    laplace_digest256 authority_id;
    laplace_digest256 result_contract_fingerprint;
    laplace_query_search_budget search_budget;
    laplace_cognition_observation_forward_limits forward_limits;
    uint32_t relation_mask;
    uint32_t maximum_results;
    uint32_t request_flags;
    uint32_t version;
} laplace_cognition_turn_policy;

typedef struct laplace_cognition_turn_receipt {
    laplace_digest256 turn_fingerprint;
    laplace_digest256 request_fingerprint;
    laplace_digest256 previous_state_id;
    laplace_digest256 previous_frame_fingerprint;
    uint64_t previous_frame_bytes;
    uint64_t turn_ordinal;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_turn_receipt;

/*
 * Compiles one incoming admitted observation into the next finite native
 * cognition request. If HAS_PREVIOUS_DISCOURSE is set, `previous_frame` must be
 * the canonical durable discourse frame from the preceding turn. It is decoded
 * and identity-validated before any request is emitted. First turns provide no
 * frame and must have ordinal zero.
 *
 * The output request is zeroed on failure. Successful compilation proves that
 * the request itself passes laplace_cognition_observation_request_identify().
 */
LAPLACE_API laplace_cognition_turn_status
laplace_cognition_turn_compile(
    const laplace_cognition_turn_input* input,
    const laplace_cognition_turn_policy* policy,
    const uint8_t* previous_frame,
    size_t previous_frame_bytes,
    laplace_cognition_observation_request* request,
    laplace_cognition_turn_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
