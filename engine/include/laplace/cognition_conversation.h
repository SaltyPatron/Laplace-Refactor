#ifndef LAPLACE_COGNITION_CONVERSATION_H
#define LAPLACE_COGNITION_CONVERSATION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_discourse_frame.h"
#include "laplace/cognition_materialization.h"
#include "laplace/cognition_realization.h"
#include "laplace/cognition_turn.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_CONVERSATION_VERSION = 1,
    LAPLACE_COGNITION_CONVERSATION_REALIZATION_POLICY_VERSION = 1,
    LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_VERSION = 1,
    LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_KNOWN_FLAGS =
        LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES |
        LAPLACE_COGNITION_DISCOURSE_HAS_PROPOSITIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_REFERENTS |
        LAPLACE_COGNITION_DISCOURSE_HAS_UNRESOLVED_QUESTIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_CORRECTIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_ELLIPSIS_BINDINGS |
        LAPLACE_COGNITION_DISCOURSE_HAS_CROSS_MODAL_ARTIFACTS |
        LAPLACE_COGNITION_DISCOURSE_HAS_REJECTED_INTERPRETATIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_PRIOR_PROGRAMS |
        LAPLACE_COGNITION_DISCOURSE_HAS_RECEIPTS
};

typedef enum laplace_cognition_conversation_status {
    LAPLACE_COGNITION_CONVERSATION_OK = 0,
    LAPLACE_COGNITION_CONVERSATION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_CONVERSATION_INVALID_VERSION = 2,
    LAPLACE_COGNITION_CONVERSATION_INVALID_DISCOURSE_ROOTS = 3,
    LAPLACE_COGNITION_CONVERSATION_TURN_FAILURE = 4,
    LAPLACE_COGNITION_CONVERSATION_COGNITION_FAILURE = 5,
    LAPLACE_COGNITION_CONVERSATION_SEMANTIC_ACT_FAILURE = 6,
    LAPLACE_COGNITION_CONVERSATION_REALIZATION_FAILURE = 7,
    LAPLACE_COGNITION_CONVERSATION_MATERIALIZATION_FAILURE = 8,
    LAPLACE_COGNITION_CONVERSATION_DISCOURSE_FAILURE = 9,
    LAPLACE_COGNITION_CONVERSATION_FRAME_FAILURE = 10,
    LAPLACE_COGNITION_CONVERSATION_CAPACITY = 11,
    LAPLACE_COGNITION_CONVERSATION_MEMORY_FAILURE = 12
} laplace_cognition_conversation_status;

/*
 * Realization policy owned by the conversation caller. Evidence epoch and active
 * context are deliberately absent: the transport derives those from the exact
 * compiled cognition turn so realization cannot drift to another evidence or
 * discourse boundary between cognition and surface selection.
 */
typedef struct laplace_cognition_conversation_realization_policy {
    laplace_id128 modality_id;
    laplace_id128 language_id;
    laplace_id128 register_id;
    laplace_digest256 realization_recipe_epoch;
    uint32_t maximum_candidates;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_conversation_realization_policy;

/*
 * Independently retained discourse-set roots for the completed turn. Previous
 * state presence is not caller-controlled here: it is derived only from the
 * validated incoming turn and exact predecessor frame.
 */
typedef struct laplace_cognition_conversation_discourse_roots {
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
    uint32_t flags;
    uint32_t version;
} laplace_cognition_conversation_discourse_roots;

typedef struct laplace_cognition_conversation_request {
    laplace_cognition_turn_input turn;
    laplace_cognition_turn_policy cognition_policy;
    laplace_cognition_conversation_realization_policy realization_policy;
    laplace_cognition_materialization_request materialization;
    laplace_cognition_conversation_discourse_roots discourse_roots;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_conversation_request;

typedef struct laplace_cognition_conversation_result {
    laplace_digest256 conversation_id;
    laplace_cognition_observation_request cognition_request;
    laplace_cognition_turn_receipt turn_receipt;
    laplace_cognition_forward_receipt forward_receipt;
    laplace_cognition_semantic_act semantic_act;
    laplace_cognition_realization_request realization_request;
    laplace_cognition_realization_result realization;
    laplace_cognition_realization_receipt realization_receipt;
    laplace_cognition_materialization_receipt materialization_receipt;
    laplace_cognition_discourse_state discourse_state;
    laplace_cognition_discourse_frame_receipt discourse_frame_receipt;
    uint64_t output_bytes;
    uint64_t discourse_frame_bytes;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_conversation_result;

/*
 * Executes one admitted observation through the complete public native response
 * chain:
 *
 *   admitted observation -> finite turn -> cognition -> semantic act
 *   -> evidence-bound realization -> exact Unicode materialization
 *   -> immutable next discourse state/frame.
 *
 * The candidate provider can enumerate typed observations only. The realization
 * provider can enumerate exact content candidates only. The materialization
 * provider can read exact immutable content only. None can acquire the adjacent
 * stage's authority.
 *
 * `previous_frame` is required exactly when the turn declares
 * HAS_PREVIOUS_DISCOURSE. The caller's output and next-frame buffers are not
 * modified until every stage succeeds, including durable frame encoding. On any
 * failure both published byte counts remain zero.
 */
LAPLACE_API laplace_cognition_conversation_status
laplace_cognition_conversation_execute(
    const laplace_cognition_conversation_request* request,
    const uint8_t* previous_frame,
    size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* cognition_provider,
    const laplace_cognition_realization_provider_v1* realization_provider,
    const laplace_cognition_materialization_provider_v1* materialization_provider,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes,
    uint8_t* next_discourse_frame,
    size_t next_discourse_frame_capacity,
    size_t* next_discourse_frame_bytes,
    laplace_cognition_conversation_result* result);

#ifdef __cplusplus
}
#endif

#endif
