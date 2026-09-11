#ifndef LAPLACE_TARGET_SCOPE_PLAN_H
#define LAPLACE_TARGET_SCOPE_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_operator.h"
#include "laplace/export.h"
#include "laplace/target_compile.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_SCOPE_PLAN_VERSION = 1
};

typedef struct laplace_target_scope_slot_spec {
    uint32_t target_role;
    uint32_t layer_index;
    uint32_t head_index;
    uint32_t expert_index;
    const uint32_t* eligible_relation_families;
    size_t eligible_relation_family_count;
    uint32_t eligible_source_mask;
    uint64_t head_rank;
    uint32_t flags;
    uint32_t reserved;
} laplace_target_scope_slot_spec;

typedef struct laplace_target_scope_plan_request {
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 target_contract_fingerprint;
    laplace_digest256 context_fingerprint;
    const laplace_cognition_operator_field* fields;
    size_t field_count;
    const laplace_cognition_operator_constraint* constraints;
    size_t constraint_count;
    const laplace_target_scope_slot_spec* slots;
    size_t slot_count;
    double numeric_tolerance;
    uint32_t operator_program_flags;
    uint32_t target_compile_flags;
    uint32_t version;
    uint32_t reserved;
} laplace_target_scope_plan_request;

typedef struct laplace_target_scope_plan_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 slot_set_fingerprint;
    laplace_digest256 compile_receipt_id;
    laplace_digest256 compile_request_fingerprint;
    laplace_digest256 context_fingerprint;
    uint64_t slot_count;
    uint64_t field_count;
    uint64_t constraint_count;
    uint64_t selected_constraint_count;
    uint32_t source_mask_union;
    uint32_t compile_status;
    uint32_t status;
    uint32_t version;
} laplace_target_scope_plan_receipt;

typedef enum laplace_target_scope_plan_status {
    LAPLACE_TARGET_SCOPE_PLAN_OK = 0,
    LAPLACE_TARGET_SCOPE_PLAN_INVALID_ARGUMENT = 1,
    LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST = 2,
    LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT = 3,
    LAPLACE_TARGET_SCOPE_PLAN_NO_MATCHING_CONSTRAINTS = 4,
    LAPLACE_TARGET_SCOPE_PLAN_TARGET_COMPILE_FAILURE = 5,
    LAPLACE_TARGET_SCOPE_PLAN_MEMORY_FAILURE = 6
} laplace_target_scope_plan_status;

/*
 * Generate target jobs from one typed operator estate and declarative slot
 * selectors.  The caller supplies the substrate fields/constraints once.  Native
 * code canonicalizes each relation-family set, derives the consumer-role and
 * operator-program identities, binds the common evidence/context/recipe scope,
 * and delegates the actual operator construction to laplace_target_compile_execute.
 *
 * Head rank is target-consumer capacity.  It participates in the scope-plan
 * receipt but not in the generated native operator identity, so changing target
 * rank cannot silently rewrite substrate semantics.
 */
LAPLACE_API laplace_target_scope_plan_status
laplace_target_scope_plan_compile(
    const laplace_target_scope_plan_request* request,
    laplace_target_compile_result** result,
    laplace_target_compile_receipt* compile_receipt,
    laplace_target_scope_plan_receipt* scope_receipt);

#ifdef __cplusplus
}
#endif

#endif
