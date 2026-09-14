#ifndef LAPLACE_COGNITION_INTERPRETATION_CANDIDATES_H
#define LAPLACE_COGNITION_INTERPRETATION_CANDIDATES_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_interpretation.h"
#include "laplace/cognition_observation_request.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_INTERPRETATION_FACTOR_PROGRAM_VERSION = 1,
    LAPLACE_COGNITION_INTERPRETATION_FACTOR_SOURCE_OBSERVATION = UINT32_MAX
};

/*
 * One admitted relation law projected into interpretation slots. source_slot may
 * be SOURCE_OBSERVATION to use the exact whole observation root without making
 * that root a variable. Otherwise it names a slot populated by an earlier
 * declared factor. target_slot always names an interpretation slot.
 *
 * law_id is the exact typed law fingerprint produced from relation identity,
 * family, source plane, direction, and flags by the canonical candidate path.
 * relation_mask is only the finite provider filter; it cannot replace law_id.
 */
typedef struct laplace_cognition_interpretation_factor_rule {
    laplace_digest256 factor_id;
    laplace_digest256 law_id;
    uint32_t source_slot;
    uint32_t target_slot;
    uint32_t relation_mask;
    uint32_t maximum_rows;
    uint32_t reserved0;
    uint32_t reserved1;
} laplace_cognition_interpretation_factor_rule;

typedef struct laplace_cognition_interpretation_factor_program {
    const laplace_cognition_interpretation_factor_rule* factors;
    size_t factor_count;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_interpretation_factor_program;

LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_factor_program_identify(
    const laplace_cognition_interpretation_factor_program* program,
    laplace_digest256* identity);

/*
 * Materialize the declared factor relations from observed typed provider
 * crossings and execute the existing joint interpreter. Provider enumeration is
 * never allowed to elect a row. Each emitted crossing is validated through the
 * same candidate/transition law used by ordinary cognition; only rows whose
 * generated law fingerprint equals the declared factor law are admitted.
 * Structural rows retain their generated calculation receipt, while witnessed
 * semantic rows retain both evidence root and calculation receipt.
 *
 * Factor declarations form a materialization DAG: a non-observation source slot
 * must already have a domain populated by an earlier rule. The final factor set
 * is sorted by factor identity before entering the order-independent joint
 * interpreter. Ambiguity therefore remains a joint result, not a first-row or
 * highest-score decision.
 */
LAPLACE_API laplace_cognition_interpretation_status
laplace_cognition_interpretation_execute_with_candidate_provider(
    const laplace_cognition_interpretation_program* interpretation_program,
    const laplace_cognition_interpretation_factor_program* factor_program,
    const laplace_cognition_turn_input* turn,
    const laplace_cognition_turn_policy* policy,
    const laplace_cognition_observation_candidate_provider_v1* provider,
    laplace_cognition_interpretation_result** result,
    laplace_cognition_interpretation_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
