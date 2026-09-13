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
 * Run every OPEN interpreted obligation through the existing guidance/forward
 * processor and the existing typed candidate-provider search engine. One query
 * recipe exists per declared obligation. The joint interpretation owns the
 * operand values; the query program owns how those operands are projected into
 * anchor/goal search bindings. The provider supplies observed crossings only.
 *
 * `turn` and `policy` supply current finite execution bounds and independently
 * admitted scope. They must match the interpretation; caller-supplied goals are
 * rejected by laplace_cognition_interpretation_prepare_turn().
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
