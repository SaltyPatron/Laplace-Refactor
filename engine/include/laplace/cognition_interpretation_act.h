#ifndef LAPLACE_COGNITION_INTERPRETATION_ACT_H
#define LAPLACE_COGNITION_INTERPRETATION_ACT_H

#include <stdint.h>

#include "laplace/cognition_interpretation_query.h"
#include "laplace/cognition_semantic_act.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_INTERPRETATION_ACT_RULE_VERSION = 1
};

typedef struct laplace_cognition_interpretation_act_rule {
    uint32_t primary_obligation_index;
    uint32_t act_kind;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_interpretation_act_rule;

typedef enum laplace_cognition_interpretation_act_status {
    LAPLACE_COGNITION_INTERPRETATION_ACT_OK = 0,
    LAPLACE_COGNITION_INTERPRETATION_ACT_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_INTERPRETATION_ACT_INCOMPLETE = 2,
    LAPLACE_COGNITION_INTERPRETATION_ACT_AMBIGUOUS = 3,
    LAPLACE_COGNITION_INTERPRETATION_ACT_RESULT_FAILURE = 4,
    LAPLACE_COGNITION_INTERPRETATION_ACT_RANGE = 5
} laplace_cognition_interpretation_act_status;

/*
 * Select the semantic act declared by the interpreted program after every
 * required obligation has reached the forward machine's completion condition.
 * The program, not answer rank, names the primary obligation. Multiple retained
 * paths are allowed only when they name the same primary entity; distinct
 * primary entities remain typed ambiguity and no act is published.
 *
 * answer_set_fingerprint binds every final obligation and every actual retained
 * observation answer across the complete program. request_fingerprint binds the
 * exact per-obligation request fingerprints as a set; there is no fabricated
 * single-query request standing in for a multi-obligation program.
 */
LAPLACE_API laplace_cognition_interpretation_act_status
laplace_cognition_interpretation_semantic_act_select(
    const laplace_cognition_interpretation_program_rule* program_rule,
    const laplace_cognition_interpretation_act_rule* act_rule,
    const laplace_cognition_interpretation_execution_result* execution,
    const laplace_cognition_forward_receipt* receipt,
    laplace_cognition_semantic_act* act);

#ifdef __cplusplus
}
#endif

#endif
