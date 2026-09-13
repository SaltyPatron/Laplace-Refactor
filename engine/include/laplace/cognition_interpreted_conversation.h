#ifndef LAPLACE_COGNITION_INTERPRETED_CONVERSATION_H
#define LAPLACE_COGNITION_INTERPRETED_CONVERSATION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_conversation.h"
#include "laplace/cognition_interpretation.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_INTERPRETED_CONVERSATION_VERSION = 1
};

typedef enum laplace_cognition_interpreted_conversation_status {
    LAPLACE_COGNITION_INTERPRETED_CONVERSATION_OK = 0,
    LAPLACE_COGNITION_INTERPRETED_CONVERSATION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_INTERPRETED_CONVERSATION_INTERPRETATION_FAILURE = 2,
    LAPLACE_COGNITION_INTERPRETED_CONVERSATION_CONVERSATION_FAILURE = 3
} laplace_cognition_interpreted_conversation_status;

/*
 * Exact bridge between the whole-observation interpretation result and the
 * existing complete conversation lifecycle. The interpretation result must be
 * UNIQUE within its supplied evidence boundary. The declared goal slot is then
 * bound through laplace_cognition_interpretation_prepare_turn(); no caller may
 * substitute a goal entity or treat a retrieval rank as the interpretation.
 *
 * `interpretation_context_fingerprint` is the inferred turn context produced by
 * the interpretation engine. It cryptographically binds the interpretation
 * receipt and goal-slot identity before the ordinary turn compiler adds durable
 * discourse state. The original interpretation receipt remains the receipt
 * returned by laplace_cognition_interpretation_execute().
 */
typedef struct laplace_cognition_interpreted_conversation_result {
    laplace_digest256 goal_slot_id;
    laplace_digest256 interpretation_context_fingerprint;
    laplace_cognition_conversation_result conversation;
    uint32_t interpretation_status;
    uint32_t conversation_status;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_interpreted_conversation_result;

/*
 * Executes only after a unique joint interpretation has bound the goal slot.
 * Ambiguous, incompatible, exhausted, or scope-mismatched interpretation never
 * falls through to the non-interpreted conversation path. Output and discourse
 * byte counts remain zero until the complete delegated conversation succeeds.
 */
LAPLACE_API laplace_cognition_interpreted_conversation_status
laplace_cognition_interpreted_conversation_execute_encoded(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* goal_slot_id,
    const laplace_cognition_conversation_request* request,
    uint32_t output_encoding,
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
    laplace_cognition_interpreted_conversation_result* result);

LAPLACE_API laplace_cognition_interpreted_conversation_status
laplace_cognition_interpreted_conversation_execute(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* goal_slot_id,
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
    laplace_cognition_interpreted_conversation_result* result);

#ifdef __cplusplus
}
#endif

#endif
