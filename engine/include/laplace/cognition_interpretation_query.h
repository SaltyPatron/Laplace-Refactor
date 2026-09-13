#ifndef LAPLACE_COGNITION_INTERPRETATION_QUERY_H
#define LAPLACE_COGNITION_INTERPRETATION_QUERY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_interpretation.h"
#include "laplace/cognition_observation_request.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_INTERPRETATION_QUERY_PROGRAM_VERSION = 1,
    LAPLACE_COGNITION_INTERPRETATION_QUERY_NO_OPERAND = UINT32_MAX
};

/*
 * Program-owned execution mapping for one interpreted obligation. Operand
 * indices address the obligation rule's exact typed operand_slots. Runtime text,
 * token rank, geometry, provider order, and first-row selection cannot choose an
 * anchor or goal. GOAL_PRESENT/TERMINAL_RESULTS are derived from goal_operand;
 * request_flags may carry only independent boundary/unresolved policy.
 */
typedef struct laplace_cognition_interpretation_query_rule {
    uint32_t obligation_index;
    uint32_t anchor_operand_index;
    uint32_t goal_operand_index;
    uint32_t relation_mask;
    uint32_t maximum_results;
    uint32_t request_flags;
    uint32_t reserved0;
    uint32_t reserved1;
} laplace_cognition_interpretation_query_rule;

typedef struct laplace_cognition_interpretation_query_program {
    const laplace_cognition_interpretation_query_rule* queries;
    size_t query_count;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_interpretation_query_program;

typedef struct laplace_cognition_interpretation_execution_result
    laplace_cognition_interpretation_execution_result;

/*
 * Canonical identity of the executable query recipe. A program_rule that is
 * executed by the candidate-provider path must carry this exact identity in its
 * recipe_id, so changing operand roles, relation families, result multiplicity,
 * or boundary policy changes the selected cognition program.
 */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_query_program_identify(
    const laplace_cognition_interpretation_query_program* program,
    laplace_digest256* identity);

/*
 * Execute every OPEN interpreted obligation through the existing native
 * guidance/forward processor and retain the actual canonical observation answers
 * produced for each obligation. Resolution.value_id remains the 256-bit result
 * fingerprint used by guidance; it is never truncated or reinterpreted as a
 * 128-bit entity. The retained answer rows are the authoritative entity/result
 * surface for later semantic-act selection.
 */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_program_execute_with_candidate_provider(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_cognition_interpretation_program_rule* rule,
    const laplace_cognition_interpretation_query_program* query_program,
    const laplace_cognition_turn_input* turn,
    const laplace_cognition_turn_policy* policy,
    const laplace_cognition_observation_candidate_provider_v1* provider,
    laplace_cognition_interpretation_execution_result** result,
    laplace_cognition_forward_receipt* receipt);

LAPLACE_API size_t
laplace_cognition_interpretation_execution_obligation_count(
    const laplace_cognition_interpretation_execution_result* result);

LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_execution_obligation_id(
    const laplace_cognition_interpretation_execution_result* result,
    size_t obligation_index,
    laplace_digest256* obligation_id);

LAPLACE_API size_t
laplace_cognition_interpretation_execution_answer_count(
    const laplace_cognition_interpretation_execution_result* result,
    size_t obligation_index);

LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_execution_answer(
    const laplace_cognition_interpretation_execution_result* result,
    size_t obligation_index,
    size_t answer_index,
    laplace_cognition_observation_answer* answer);

LAPLACE_API void
laplace_cognition_interpretation_execution_destroy(
    laplace_cognition_interpretation_execution_result** result);

/*
 * Compatibility surface for callers that need only the native forward result.
 * It delegates to the retained execution API and detaches the forward result;
 * no alternative execution path or different semantics are introduced.
 */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_forward_execute_with_candidate_provider(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_cognition_interpretation_program_rule* rule,
    const laplace_cognition_interpretation_query_program* query_program,
    const laplace_cognition_turn_input* turn,
    const laplace_cognition_turn_policy* policy,
    const laplace_cognition_observation_candidate_provider_v1* provider,
    laplace_cognition_forward_result** result,
    laplace_cognition_forward_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
