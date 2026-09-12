#ifndef LAPLACE_TARGET_OBSERVATION_COMPILE_H
#define LAPLACE_TARGET_OBSERVATION_COMPILE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_observation_request.h"
#include "laplace/export.h"
#include "laplace/target_scope_plan.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_OBSERVATION_COMPILE_VERSION = 1
};

/* Target-specific slots are selected over the exact typed operator estate that
 * live observation cognition already generated and executed. The caller cannot
 * replace fields, constraints, evidence roots, source classes, relation planes,
 * uncertainty precision, operator flags or numeric tolerance on this path. */
typedef struct laplace_target_observation_compile_request {
    const laplace_cognition_observation_result* observation_result;
    size_t answer_index;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 target_contract_fingerprint;
    const laplace_target_scope_slot_spec* slots;
    size_t slot_count;
    uint32_t target_compile_flags;
    uint32_t version;
    uint32_t reserved;
} laplace_target_observation_compile_request;

LAPLACE_API laplace_target_scope_plan_status
laplace_target_observation_compile(
    const laplace_target_observation_compile_request* request,
    laplace_target_compile_result** result,
    laplace_target_compile_receipt* compile_receipt,
    laplace_target_scope_plan_receipt* scope_receipt);

#ifdef __cplusplus
}
#endif

#endif