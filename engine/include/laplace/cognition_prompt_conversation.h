#ifndef LAPLACE_COGNITION_PROMPT_CONVERSATION_H
#define LAPLACE_COGNITION_PROMPT_CONVERSATION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_conversation.h"
#include "laplace/cognition_prompt_admission.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION = 2
};

typedef enum laplace_cognition_prompt_conversation_status {
    LAPLACE_COGNITION_PROMPT_CONVERSATION_OK = 0,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_INVALID_VERSION = 2,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_ADMISSION_FAILURE = 3,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_STRUCTURAL_PROVIDER_FAILURE = 4,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_PROVIDER_SET_FAILURE = 5,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_CONVERSATION_FAILURE = 6,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE = 7,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_ENCODING_INVALID = 8,
    LAPLACE_COGNITION_PROMPT_CONVERSATION_WHY_NOT = 9
} laplace_cognition_prompt_conversation_status;

typedef enum laplace_cognition_prompt_orientation_disposition {
    LAPLACE_COGNITION_PROMPT_ORIENTATION_NOT_REQUIRED = 0,
    LAPLACE_COGNITION_PROMPT_ORIENTATION_RESOLVED = 1,
    LAPLACE_COGNITION_PROMPT_ORIENTATION_NO_RESPONSE = 2,
    LAPLACE_COGNITION_PROMPT_ORIENTATION_AMBIGUOUS = 3,
    LAPLACE_COGNITION_PROMPT_ORIENTATION_INCOMPLETE = 4,
    LAPLACE_COGNITION_PROMPT_ORIENTATION_EXHAUSTED = 5,
    LAPLACE_COGNITION_PROMPT_ORIENTATION_FAILED = 6
} laplace_cognition_prompt_orientation_disposition;

/*
 * One response policy downstream of an already-created exact prompt admission.
 * The incoming turn is deliberately absent: it is copied only from the admission
 * view so a caller cannot admit one prompt and execute cognition against another
 * observation identity or occurrence. An explicit goal remains valid typed
 * cognition policy. When GOAL_PRESENT is absent, the prompt route must derive the
 * goal and admissible relation families from witnessed query-relative orientation.
 */
typedef struct laplace_cognition_prompt_conversation_request {
    laplace_cognition_turn_policy cognition_policy;
    laplace_cognition_conversation_realization_policy realization_policy;
    laplace_cognition_materialization_request materialization;
    laplace_cognition_conversation_discourse_roots discourse_roots;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_prompt_conversation_request;

typedef struct laplace_cognition_prompt_conversation_result {
    laplace_digest256 prompt_admission_receipt_id;
    laplace_digest256 prompt_exact_bytes_fingerprint;
    laplace_digest256 composite_cognition_provider_fingerprint;
    /* Present when an unbound prompt executed the sparse typed coupling field. */
    laplace_digest256 orientation_coupling_receipt_id;
    /* Present when coupling responses were lowered into joint interpretation. */
    laplace_digest256 orientation_interpretation_receipt_id;
    laplace_digest256 orientation_response_fingerprint;
    laplace_id128 derived_goal_entity_id;
    laplace_cognition_conversation_result conversation;
    uint32_t orientation_disposition;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_prompt_conversation_result;

/*
 * Executes the admitted prompt through one native conversation chain while
 * composing its exact structural provider with zero or more caller-owned
 * persistent cognition providers. For an unbound prompt the composite provider
 * is first used as a sparse query-relative response field; semantic reactions are
 * solved jointly across their originating prompt occurrences before the cognition
 * turn is compiled. No caller topic, keyword router or model selects that binding.
 *
 * The prompt admission and every additional provider state must remain alive for
 * the duration of this call. The provider set owns descriptor copies only. A
 * typed WHY_NOT publishes zero response/frame bytes and retains the orientation
 * disposition/receipts when orientation itself supplied the reason.
 */
LAPLACE_API laplace_cognition_prompt_conversation_status
laplace_cognition_prompt_conversation_execute(
    laplace_cognition_prompt_admission* admission,
    const laplace_cognition_prompt_conversation_request* request,
    const uint8_t* previous_frame,
    size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* additional_cognition_providers,
    size_t additional_cognition_provider_count,
    const laplace_cognition_realization_provider_v1* realization_provider,
    const laplace_cognition_materialization_provider_v1* materialization_provider,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes,
    uint8_t* next_discourse_frame,
    size_t next_discourse_frame_capacity,
    size_t* next_discourse_frame_bytes,
    laplace_cognition_prompt_conversation_result* result);

LAPLACE_API laplace_cognition_prompt_conversation_status
laplace_cognition_prompt_conversation_execute_encoded(
    laplace_cognition_prompt_admission* admission,
    const laplace_cognition_prompt_conversation_request* request,
    uint32_t output_encoding,
    const uint8_t* previous_frame,
    size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* additional_cognition_providers,
    size_t additional_cognition_provider_count,
    const laplace_cognition_realization_provider_v1* realization_provider,
    const laplace_cognition_materialization_provider_v1* materialization_provider,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes,
    uint8_t* next_discourse_frame,
    size_t next_discourse_frame_capacity,
    size_t* next_discourse_frame_bytes,
    laplace_cognition_prompt_conversation_result* result);

#ifdef __cplusplus
}
#endif

#endif
