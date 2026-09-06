#ifndef LAPLACE_COGNITION_DISCOURSE_H
#define LAPLACE_COGNITION_DISCOURSE_H

#include <stdint.h>

#include "laplace/cognition_semantic_act.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE = UINT32_C(1),
    LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES = UINT32_C(2),
    LAPLACE_COGNITION_DISCOURSE_HAS_PROPOSITIONS = UINT32_C(4),
    LAPLACE_COGNITION_DISCOURSE_HAS_REFERENTS = UINT32_C(8),
    LAPLACE_COGNITION_DISCOURSE_HAS_UNRESOLVED_QUESTIONS = UINT32_C(16),
    LAPLACE_COGNITION_DISCOURSE_HAS_CORRECTIONS = UINT32_C(32),
    LAPLACE_COGNITION_DISCOURSE_HAS_ELLIPSIS_BINDINGS = UINT32_C(64),
    LAPLACE_COGNITION_DISCOURSE_HAS_CROSS_MODAL_ARTIFACTS = UINT32_C(128),
    LAPLACE_COGNITION_DISCOURSE_HAS_REJECTED_INTERPRETATIONS = UINT32_C(256),
    LAPLACE_COGNITION_DISCOURSE_HAS_PRIOR_PROGRAMS = UINT32_C(512),
    LAPLACE_COGNITION_DISCOURSE_HAS_RECEIPTS = UINT32_C(1024),
    LAPLACE_COGNITION_DISCOURSE_KNOWN_FLAGS =
        LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE |
        LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES |
        LAPLACE_COGNITION_DISCOURSE_HAS_PROPOSITIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_REFERENTS |
        LAPLACE_COGNITION_DISCOURSE_HAS_UNRESOLVED_QUESTIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_CORRECTIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_ELLIPSIS_BINDINGS |
        LAPLACE_COGNITION_DISCOURSE_HAS_CROSS_MODAL_ARTIFACTS |
        LAPLACE_COGNITION_DISCOURSE_HAS_REJECTED_INTERPRETATIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_PRIOR_PROGRAMS |
        LAPLACE_COGNITION_DISCOURSE_HAS_RECEIPTS,
    LAPLACE_COGNITION_DISCOURSE_VERSION = 1
};

typedef enum laplace_cognition_discourse_status {
    LAPLACE_COGNITION_DISCOURSE_OK = 0,
    LAPLACE_COGNITION_DISCOURSE_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_DISCOURSE_INVALID_FLAGS = 2,
    LAPLACE_COGNITION_DISCOURSE_INACTIVE_PAYLOAD = 3,
    LAPLACE_COGNITION_DISCOURSE_SEMANTIC_ACT_INVALID = 4,
    LAPLACE_COGNITION_DISCOURSE_IDENTITY_MISMATCH = 5
} laplace_cognition_discourse_status;

/*
 * A completed-turn discourse snapshot is active cognition state, not a recent
 * topic label. The root fingerprints below point to independently retained
 * typed sets; they are deliberately not flattened into one embedding, rank or
 * prose memory blob. Presence is carried by explicit flags so the all-zero
 * 256-bit value remains representable when a set is present.
 */
typedef struct laplace_cognition_discourse_input {
    laplace_digest256 discourse_id;
    laplace_digest256 previous_state_id;
    laplace_id128 observation_entity_id;
    laplace_digest256 observation_occurrence_id;
    laplace_digest256 active_entity_set_fingerprint;
    laplace_digest256 proposition_set_fingerprint;
    laplace_digest256 referent_set_fingerprint;
    laplace_digest256 unresolved_question_set_fingerprint;
    laplace_digest256 correction_set_fingerprint;
    laplace_digest256 ellipsis_binding_set_fingerprint;
    laplace_digest256 cross_modal_artifact_set_fingerprint;
    laplace_digest256 rejected_interpretation_set_fingerprint;
    laplace_digest256 prior_program_set_fingerprint;
    laplace_digest256 receipt_set_fingerprint;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    uint64_t turn_ordinal;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_discourse_input;

typedef struct laplace_cognition_discourse_state {
    laplace_digest256 state_id;
    laplace_digest256 discourse_id;
    laplace_digest256 previous_state_id;
    laplace_id128 observation_entity_id;
    laplace_digest256 observation_occurrence_id;
    laplace_digest256 semantic_act_id;
    laplace_digest256 active_entity_set_fingerprint;
    laplace_digest256 proposition_set_fingerprint;
    laplace_digest256 referent_set_fingerprint;
    laplace_digest256 unresolved_question_set_fingerprint;
    laplace_digest256 correction_set_fingerprint;
    laplace_digest256 ellipsis_binding_set_fingerprint;
    laplace_digest256 cross_modal_artifact_set_fingerprint;
    laplace_digest256 rejected_interpretation_set_fingerprint;
    laplace_digest256 prior_program_set_fingerprint;
    laplace_digest256 receipt_set_fingerprint;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    uint64_t turn_ordinal;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_discourse_state;

/*
 * Publishes a completed-turn immutable discourse snapshot from an already
 * completed semantic act. The semantic act remains a separate object; the
 * snapshot binds its identity and the independently retained discourse roots.
 */
LAPLACE_API laplace_cognition_discourse_status
laplace_cognition_discourse_state_create(
    const laplace_cognition_discourse_input* input,
    const laplace_cognition_semantic_act* semantic_act,
    laplace_cognition_discourse_state* state);

LAPLACE_API laplace_cognition_discourse_status
laplace_cognition_discourse_state_identify(
    const laplace_cognition_discourse_state* state,
    laplace_digest256* state_id);

LAPLACE_API laplace_cognition_discourse_status
laplace_cognition_discourse_state_validate(
    const laplace_cognition_discourse_state* state);

#ifdef __cplusplus
}
#endif

#endif
