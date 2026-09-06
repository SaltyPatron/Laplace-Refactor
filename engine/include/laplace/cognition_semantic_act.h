#ifndef LAPLACE_COGNITION_SEMANTIC_ACT_H
#define LAPLACE_COGNITION_SEMANTIC_ACT_H

#include <stdint.h>

#include "laplace/cognition_observation_request.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT = UINT32_C(1),
    LAPLACE_COGNITION_SEMANTIC_ACT_TERMINAL_OPERATION_PRESENT = UINT32_C(2),
    LAPLACE_COGNITION_SEMANTIC_ACT_KNOWN_FLAGS =
        LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_TERMINAL_OPERATION_PRESENT,
    LAPLACE_COGNITION_SEMANTIC_ACT_VERSION = 1
};

typedef enum laplace_cognition_semantic_act_status {
    LAPLACE_COGNITION_SEMANTIC_ACT_OK = 0,
    LAPLACE_COGNITION_SEMANTIC_ACT_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_SEMANTIC_ACT_INCOMPLETE = 2,
    LAPLACE_COGNITION_SEMANTIC_ACT_FORWARD_RESULT_FAILURE = 3,
    LAPLACE_COGNITION_SEMANTIC_ACT_NO_TERMINAL_OPERATION = 4,
    LAPLACE_COGNITION_SEMANTIC_ACT_NO_ANSWER = 5,
    LAPLACE_COGNITION_SEMANTIC_ACT_REQUEST_FAILURE = 6,
    LAPLACE_COGNITION_SEMANTIC_ACT_RESULT_FAILURE = 7,
    LAPLACE_COGNITION_SEMANTIC_ACT_RANGE = 8
} laplace_cognition_semantic_act_status;

/*
 * Immutable handoff from completed cognition to realization/effect selection.
 *
 * This record is intentionally semantic rather than linguistic. `operation_kind`
 * is the terminal operation chosen by the native cognition scheduler; no prompt
 * bytes, language keywords, endpoint names, or transport-specific intent labels
 * participate in act selection. `primary_answer` is the lowest-rank retained
 * terminal answer while `answer_set_fingerprint` binds every retained answer so
 * realization cannot silently discard or replace the completed result set.
 */
typedef struct laplace_cognition_semantic_act {
    laplace_digest256 act_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 result_contract_fingerprint;
    laplace_digest256 forward_receipt_id;
    laplace_digest256 forward_output_fingerprint;
    laplace_digest256 final_state_id;
    laplace_digest256 answer_set_fingerprint;
    laplace_cognition_observation_answer primary_answer;
    uint64_t answer_count;
    uint32_t operation_kind;
    uint32_t flags;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_semantic_act;

/*
 * Selects one semantic act only after the canonical forward pass has proven
 * semantic completion with zero remaining required obligations. The selected
 * operation is read from the final native forward layer and the answer set is
 * read from the retained observation result; callers cannot inject an act kind
 * or manufacture completion outside cognition.
 */
LAPLACE_API laplace_cognition_semantic_act_status
laplace_cognition_observation_semantic_act_select(
    const laplace_cognition_observation_request* request,
    const laplace_cognition_observation_result* observation_result,
    const laplace_cognition_forward_result* forward_result,
    const laplace_cognition_forward_receipt* forward_receipt,
    laplace_cognition_semantic_act* act);

#ifdef __cplusplus
}
#endif

#endif
