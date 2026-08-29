#ifndef LAPLACE_COGNITION_GUIDANCE_H
#define LAPLACE_COGNITION_GUIDANCE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/cognition_guidance.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_cognition_guidance_header {
    laplace_digest256 program_id;
    laplace_digest256 goal_id;
    laplace_digest256 bindings_fingerprint;
    laplace_digest256 scope_fingerprint;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_epoch;
    laplace_digest256 authority_id;
    laplace_digest256 result_contract_fingerprint;
    uint32_t version;
    uint32_t flags;
} laplace_cognition_guidance_header;

typedef struct laplace_cognition_obligation {
    laplace_digest256 obligation_id;
    laplace_digest256 value_id;
    laplace_digest256 binding_fingerprint;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_boundary;
    laplace_digest256 authority_id;
    laplace_digest256 result_contract_fingerprint;
    laplace_digest256 resolution_receipt_id;
    uint32_t kind;
    uint32_t disposition;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_obligation;

typedef struct laplace_cognition_query_projection {
    laplace_digest256 projection_id;
    laplace_digest256 obligation_id;
    laplace_digest256 binding_fingerprint;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_boundary;
    laplace_digest256 authority_id;
    laplace_digest256 result_contract_fingerprint;
    uint32_t query_kind;
    uint32_t obligation_flags;
} laplace_cognition_query_projection;

typedef struct laplace_cognition_resolution {
    laplace_digest256 obligation_id;
    laplace_digest256 value_id;
    laplace_digest256 receipt_id;
    uint32_t disposition;
    uint32_t flags;
} laplace_cognition_resolution;

typedef struct laplace_cognition_evidence_observation {
    laplace_digest256 observation_id;
    laplace_digest256 proposition_id;
    laplace_digest256 dependence_root_id;
    laplace_digest256 source_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_epoch;
    laplace_digest256 uncertainty_fingerprint;
    laplace_digest256 standing_fingerprint;
    int32_t polarity;
    uint32_t flags;
} laplace_cognition_evidence_observation;

typedef struct laplace_cognition_evidence_fold_summary {
    laplace_digest256 fold_receipt_id;
    laplace_digest256 proposition_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_epoch;
    laplace_digest256 root_set_fingerprint;
    laplace_digest256 uncertainty_fingerprint;
    laplace_digest256 standing_fingerprint;
    uint64_t raw_observation_count;
    uint64_t independent_root_count;
    uint64_t positive_root_count;
    uint64_t negative_root_count;
    uint64_t contradictory_root_count;
    uint32_t flags;
    uint32_t status;
} laplace_cognition_evidence_fold_summary;

typedef struct laplace_cognition_guidance_operation {
    laplace_digest256 operation_id;
    laplace_digest256 target_obligation_id;
    laplace_digest256 operands_fingerprint;
    laplace_digest256 preconditions_fingerprint;
    laplace_digest256 predicted_effect_fingerprint;
    laplace_digest256 authority_id;
    laplace_digest256 receipt_contract_fingerprint;
    uint64_t expected_obligation_reduction;
    uint64_t information_value;
    uint64_t resource_cost;
    uint64_t novelty;
    uint32_t kind;
    uint32_t flags;
} laplace_cognition_guidance_operation;

typedef struct laplace_cognition_guidance_decision {
    laplace_digest256 decision_receipt_id;
    laplace_digest256 selected_operation_id;
    laplace_digest256 state_id;
    uint64_t eligible_operation_count;
    uint64_t rejected_operation_count;
    uint64_t selected_expected_obligation_reduction;
    uint64_t selected_information_value;
    uint64_t selected_resource_cost;
    uint32_t selected_kind;
    uint32_t status;
} laplace_cognition_guidance_decision;

typedef struct laplace_cognition_guidance_transition_receipt {
    laplace_digest256 transition_receipt_id;
    laplace_digest256 prior_state_id;
    laplace_digest256 next_state_id;
    laplace_digest256 resolution_set_fingerprint;
    uint64_t applied_resolution_count;
    uint64_t remaining_open_count;
    uint32_t completion;
    uint32_t status;
} laplace_cognition_guidance_transition_receipt;

typedef struct laplace_cognition_guidance_state laplace_cognition_guidance_state;

typedef enum laplace_cognition_guidance_status {
    LAPLACE_COGNITION_GUIDANCE_OK = 0,
    LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_GUIDANCE_STATE_INVALID = 2,
    LAPLACE_COGNITION_GUIDANCE_DUPLICATE = 3,
    LAPLACE_COGNITION_GUIDANCE_CONFLICT = 4,
    LAPLACE_COGNITION_GUIDANCE_RANGE = 5,
    LAPLACE_COGNITION_GUIDANCE_MEMORY_FAILURE = 6,
    LAPLACE_COGNITION_GUIDANCE_NO_OPERATION = 7
} laplace_cognition_guidance_status;

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_guidance_state_create(
    const laplace_cognition_guidance_header* header,
    const laplace_cognition_obligation* obligations,
    size_t obligation_count,
    laplace_cognition_guidance_state** state);

LAPLACE_API void
laplace_cognition_guidance_state_destroy(laplace_cognition_guidance_state** state);

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_guidance_state_identify(
    const laplace_cognition_guidance_state* state,
    laplace_digest256* state_id);

LAPLACE_API size_t
laplace_cognition_guidance_state_obligation_count(
    const laplace_cognition_guidance_state* state);

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_guidance_state_obligation(
    const laplace_cognition_guidance_state* state,
    size_t index,
    laplace_cognition_obligation* obligation);

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_guidance_completion(
    const laplace_cognition_guidance_state* state,
    uint32_t* completion,
    uint64_t* remaining_required_count);

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_guidance_project_queries(
    const laplace_cognition_guidance_state* state,
    laplace_cognition_query_projection* projections,
    size_t projection_capacity,
    size_t* projection_count);

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_guidance_apply_resolutions(
    const laplace_cognition_guidance_state* state,
    const laplace_cognition_resolution* resolutions,
    size_t resolution_count,
    laplace_cognition_guidance_state** next_state,
    laplace_cognition_guidance_transition_receipt* receipt);

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_evidence_fold(
    const laplace_cognition_evidence_observation* observations,
    size_t observation_count,
    laplace_cognition_evidence_fold_summary* summary);

LAPLACE_API laplace_cognition_guidance_status
laplace_cognition_guidance_select_operation(
    const laplace_cognition_guidance_state* state,
    const laplace_cognition_guidance_operation* operations,
    size_t operation_count,
    laplace_cognition_guidance_decision* decision);

#ifdef __cplusplus
}
#endif

#endif
