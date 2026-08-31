#ifndef LAPLACE_STANDING_CALCULATION_H
#define LAPLACE_STANDING_CALCULATION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/standing_calculation.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAPLACE_STANDING_OUTCOME_KIND_COUNT 9u

typedef struct laplace_standing_recipe {
    laplace_digest256 recipe_id;
    laplace_digest256 authority_receipt_id;
    laplace_digest256 evaluation_law_id;
    laplace_digest256 world_context_id;
    laplace_digest256 language_modality_id;
    laplace_digest256 valid_time_scope_id;
    laplace_digest256 evidence_boundary_id;
    double default_rating;
    double default_rating_deviation;
    double default_volatility;
    double volatility_constraint;
    double convergence_tolerance;
    uint64_t score_numerator[LAPLACE_STANDING_OUTCOME_KIND_COUNT];
    uint64_t score_denominator[LAPLACE_STANDING_OUTCOME_KIND_COUNT];
    uint32_t rateable_outcome_mask;
    uint32_t participant_role;
    uint32_t arena_kind;
    uint32_t version;
    uint32_t flags;
} laplace_standing_recipe;

typedef struct laplace_standing_coordinate {
    laplace_digest256 participant_id;
    laplace_digest256 evaluation_law_id;
    laplace_digest256 world_context_id;
    laplace_digest256 language_modality_id;
    laplace_digest256 valid_time_scope_id;
    laplace_digest256 evidence_boundary_id;
    laplace_digest256 rating_recipe_id;
    uint32_t participant_role;
    uint32_t arena_kind;
    uint32_t rating_recipe_version;
    uint32_t flags;
} laplace_standing_coordinate;

typedef struct laplace_standing_state {
    laplace_digest256 state_id;
    laplace_digest256 coordinate_id;
    laplace_digest256 arena_scope_id;
    laplace_digest256 prior_state_id;
    laplace_digest256 epoch_id;
    laplace_digest256 rating_recipe_id;
    double rating;
    double rating_deviation;
    double volatility;
    uint64_t eligible_match_count;
    uint64_t period_ordinal;
    uint32_t rating_recipe_version;
    uint32_t flags;
} laplace_standing_state;

typedef struct laplace_standing_event {
    laplace_digest256 event_id;
    laplace_digest256 participant_coordinate_id;
    laplace_digest256 participant_prior_state_id;
    laplace_standing_state opponent_prior_state;
    laplace_digest256 period_id;
    laplace_digest256 eligible_root_id;
    laplace_digest256 outcome_mapping_id;
    laplace_digest256 context_id;
    laplace_digest256 valid_time_id;
    uint64_t score_numerator;
    uint64_t score_denominator;
    uint32_t outcome_kind;
    uint32_t flags;
} laplace_standing_event;

typedef struct laplace_standing_period_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 prior_state_id;
    laplace_digest256 successor_state_id;
    laplace_digest256 period_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t eligible_event_count;
    uint64_t prior_match_count;
    uint64_t successor_match_count;
    uint32_t volatility_iterations;
    uint32_t version;
    uint32_t status;
    uint32_t flags;
} laplace_standing_period_receipt;

typedef struct laplace_standing_error {
    uint64_t event_index;
    uint32_t volatility_iterations;
    uint32_t reserved;
} laplace_standing_error;

typedef struct laplace_standing_period_input {
    laplace_standing_recipe recipe;
    laplace_standing_state prior_state;
    laplace_standing_event event;
} laplace_standing_period_input;

typedef struct laplace_standing_period_result {
    laplace_standing_state successor_state;
    laplace_standing_period_receipt receipt;
} laplace_standing_period_result;

typedef enum laplace_standing_status {
    LAPLACE_STANDING_OK = 0,
    LAPLACE_STANDING_INVALID_ARGUMENT = 1,
    LAPLACE_STANDING_COORDINATE_INVALID = 2,
    LAPLACE_STANDING_STATE_INVALID = 3,
    LAPLACE_STANDING_STATE_IDENTITY_MISMATCH = 4,
    LAPLACE_STANDING_EVENT_INVALID = 5,
    LAPLACE_STANDING_EVENT_IDENTITY_MISMATCH = 6,
    LAPLACE_STANDING_ARENA_MISMATCH = 7,
    LAPLACE_STANDING_PRIOR_MISMATCH = 8,
    LAPLACE_STANDING_DEPENDENCE_ROOT_DUPLICATE = 9,
    LAPLACE_STANDING_EVENT_DUPLICATE = 10,
    LAPLACE_STANDING_OUTCOME_NOT_RATEABLE = 11,
    LAPLACE_STANDING_NUMERIC_INVALID = 12,
    LAPLACE_STANDING_CONVERGENCE_FAILED = 13,
    LAPLACE_STANDING_RESOURCE_INSUFFICIENT = 14,
    LAPLACE_STANDING_OVERFLOW = 15,
    LAPLACE_STANDING_RECIPE_INVALID = 16,
    LAPLACE_STANDING_RECIPE_IDENTITY_MISMATCH = 17,
    LAPLACE_STANDING_OUTCOME_MAPPING_MISMATCH = 18
} laplace_standing_status;

LAPLACE_API laplace_standing_status laplace_standing_recipe_identify(
    const laplace_standing_recipe* recipe,
    laplace_digest256* recipe_id);

LAPLACE_API laplace_standing_status laplace_standing_outcome_mapping_identify(
    const laplace_standing_recipe* recipe,
    uint32_t outcome_kind,
    laplace_digest256* mapping_id);

LAPLACE_API laplace_standing_status laplace_standing_coordinate_identify(
    const laplace_standing_coordinate* coordinate,
    laplace_digest256* arena_scope_id,
    laplace_digest256* coordinate_id);

LAPLACE_API laplace_standing_status laplace_standing_state_identify(
    const laplace_standing_state* state,
    laplace_digest256* state_id);

LAPLACE_API laplace_standing_status laplace_standing_event_identify(
    const laplace_standing_event* event,
    laplace_digest256* event_id);

LAPLACE_API laplace_standing_status laplace_standing_onboard(
    const laplace_standing_recipe* recipe,
    const laplace_digest256* participant_id,
    const laplace_digest256* initialization_epoch_id,
    laplace_standing_state* state);

LAPLACE_API laplace_standing_status laplace_standing_calculate_period(
    const laplace_standing_recipe* recipe,
    const laplace_standing_state* prior_state,
    const laplace_digest256* period_id,
    const laplace_standing_event* events,
    size_t event_count,
    laplace_standing_state* successor_state,
    laplace_standing_period_receipt* receipt,
    laplace_standing_error* error);

LAPLACE_API laplace_standing_status laplace_standing_calculate_period_batch(
    const laplace_standing_period_input* inputs,
    size_t input_count,
    laplace_standing_period_result* result,
    laplace_standing_error* error);

#ifdef __cplusplus
}
#endif

#endif
