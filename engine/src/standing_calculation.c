#include "laplace/standing_calculation.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "blake3.h"

static int bytes_zero(const void* value, size_t count) {
    const uint8_t* bytes = (const uint8_t*)value;
    uint8_t aggregate = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0u;
}

static int digest_equal(const laplace_digest256* left, const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_double(blake3_hasher* hasher, double value) {
    uint64_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    hash_u64(hasher, bits);
}

static void finish_digest(blake3_hasher* hasher, laplace_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

static int numeric_state_valid(double rating, double deviation, double volatility) {
    return isfinite(rating) && isfinite(deviation) && deviation > 0.0 &&
        isfinite(volatility) && volatility > 0.0;
}

static int initial_state_matches_recipe(
    const laplace_standing_state* state,
    const laplace_standing_recipe* recipe) {
    return state->period_ordinal != 0u ||
        (state->eligible_match_count == 0u &&
         state->rating == recipe->default_rating &&
         state->rating_deviation == recipe->default_rating_deviation &&
         state->volatility == recipe->default_volatility);
}

static uint64_t greatest_common_divisor(uint64_t left, uint64_t right) {
    while (right != 0u) {
        const uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

static int recipe_shape_valid(const laplace_standing_recipe* recipe) {
    uint32_t index;
    const uint32_t allowed_mask =
        (UINT32_C(1) << LAPLACE_STANDING_OUTCOME_UNCERTAIN) - UINT32_C(1);
    if (recipe == NULL ||
        bytes_zero(&recipe->authority_receipt_id,
                   sizeof(recipe->authority_receipt_id)) ||
        bytes_zero(&recipe->evaluation_law_id,
                   sizeof(recipe->evaluation_law_id)) ||
        bytes_zero(&recipe->world_context_id,
                   sizeof(recipe->world_context_id)) ||
        bytes_zero(&recipe->language_modality_id,
                   sizeof(recipe->language_modality_id)) ||
        bytes_zero(&recipe->valid_time_scope_id,
                   sizeof(recipe->valid_time_scope_id)) ||
        bytes_zero(&recipe->evidence_boundary_id,
                   sizeof(recipe->evidence_boundary_id)) ||
        !numeric_state_valid(recipe->default_rating,
                             recipe->default_rating_deviation,
                             recipe->default_volatility) ||
        !isfinite(recipe->volatility_constraint) ||
        recipe->volatility_constraint <= 0.0 ||
        !isfinite(recipe->convergence_tolerance) ||
        recipe->convergence_tolerance <= 0.0 ||
        recipe->rateable_outcome_mask == 0u ||
        (recipe->rateable_outcome_mask & ~allowed_mask) != 0u ||
        recipe->participant_role == 0u || recipe->arena_kind == 0u ||
        recipe->version != LAPLACE_STANDING_VERSION ||
        recipe->flags != LAPLACE_STANDING_FLAGS_NONE) {
        return 0;
    }
    for (index = 0u; index < LAPLACE_STANDING_OUTCOME_KIND_COUNT; ++index) {
        const int rateable =
            (recipe->rateable_outcome_mask & (UINT32_C(1) << index)) != 0u;
        if ((rateable &&
             (recipe->score_denominator[index] == 0u ||
              recipe->score_numerator[index] >
                  recipe->score_denominator[index] ||
              greatest_common_divisor(recipe->score_numerator[index],
                                      recipe->score_denominator[index]) != 1u)) ||
            (!rateable &&
             (recipe->score_numerator[index] != 0u ||
              recipe->score_denominator[index] != 0u))) {
            return 0;
        }
    }
    return 1;
}

laplace_standing_status laplace_standing_recipe_identify(
    const laplace_standing_recipe* recipe,
    laplace_digest256* recipe_id) {
    blake3_hasher hasher;
    uint32_t index;
    if (recipe_id == NULL || !recipe_shape_valid(recipe)) {
        return LAPLACE_STANDING_RECIPE_INVALID;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STANDING_RECIPE_DOMAIN,
        sizeof(LAPLACE_STANDING_RECIPE_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, recipe->authority_receipt_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->evaluation_law_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->world_context_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->language_modality_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->valid_time_scope_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->evidence_boundary_id.bytes, 32u);
    hash_double(&hasher, recipe->default_rating);
    hash_double(&hasher, recipe->default_rating_deviation);
    hash_double(&hasher, recipe->default_volatility);
    hash_double(&hasher, recipe->volatility_constraint);
    hash_double(&hasher, recipe->convergence_tolerance);
    for (index = 0u; index < LAPLACE_STANDING_OUTCOME_KIND_COUNT; ++index) {
        hash_u64(&hasher, recipe->score_numerator[index]);
        hash_u64(&hasher, recipe->score_denominator[index]);
    }
    hash_u32(&hasher, recipe->rateable_outcome_mask);
    hash_u32(&hasher, recipe->participant_role);
    hash_u32(&hasher, recipe->arena_kind);
    hash_u32(&hasher, recipe->version);
    hash_u32(&hasher, recipe->flags);
    finish_digest(&hasher, recipe_id);
    return LAPLACE_STANDING_OK;
}

laplace_standing_status laplace_standing_outcome_mapping_identify(
    const laplace_standing_recipe* recipe,
    uint32_t outcome_kind,
    laplace_digest256* mapping_id) {
    laplace_digest256 expected_recipe_id;
    blake3_hasher hasher;
    uint32_t index;
    laplace_standing_status status;
    if (mapping_id == NULL || outcome_kind < LAPLACE_STANDING_OUTCOME_CONFIRM ||
        outcome_kind > LAPLACE_STANDING_OUTCOME_INVALID_EXECUTION) {
        return LAPLACE_STANDING_RECIPE_INVALID;
    }
    status = laplace_standing_recipe_identify(recipe, &expected_recipe_id);
    if (status != LAPLACE_STANDING_OK) {
        return status;
    }
    if (!digest_equal(&recipe->recipe_id, &expected_recipe_id)) {
        return LAPLACE_STANDING_RECIPE_IDENTITY_MISMATCH;
    }
    index = outcome_kind - 1u;
    if ((recipe->rateable_outcome_mask & (UINT32_C(1) << index)) == 0u) {
        return LAPLACE_STANDING_OUTCOME_NOT_RATEABLE;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STANDING_OUTCOME_MAPPING_DOMAIN,
        sizeof(LAPLACE_STANDING_OUTCOME_MAPPING_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, recipe->recipe_id.bytes, 32u);
    hash_u32(&hasher, outcome_kind);
    hash_u64(&hasher, recipe->score_numerator[index]);
    hash_u64(&hasher, recipe->score_denominator[index]);
    finish_digest(&hasher, mapping_id);
    return LAPLACE_STANDING_OK;
}

static void hash_arena_scope(
    blake3_hasher* hasher, const laplace_standing_coordinate* coordinate) {
    blake3_hasher_update(hasher, coordinate->evaluation_law_id.bytes, 32u);
    blake3_hasher_update(hasher, coordinate->world_context_id.bytes, 32u);
    blake3_hasher_update(hasher, coordinate->language_modality_id.bytes, 32u);
    blake3_hasher_update(hasher, coordinate->valid_time_scope_id.bytes, 32u);
    blake3_hasher_update(hasher, coordinate->evidence_boundary_id.bytes, 32u);
    blake3_hasher_update(hasher, coordinate->rating_recipe_id.bytes, 32u);
    hash_u32(hasher, coordinate->participant_role);
    hash_u32(hasher, coordinate->arena_kind);
    hash_u32(hasher, coordinate->rating_recipe_version);
    hash_u32(hasher, coordinate->flags);
}

laplace_standing_status laplace_standing_coordinate_identify(
    const laplace_standing_coordinate* coordinate,
    laplace_digest256* arena_scope_id,
    laplace_digest256* coordinate_id) {
    blake3_hasher hasher;
    if (coordinate == NULL || arena_scope_id == NULL || coordinate_id == NULL ||
        bytes_zero(&coordinate->participant_id, sizeof(coordinate->participant_id)) ||
        bytes_zero(&coordinate->evaluation_law_id, sizeof(coordinate->evaluation_law_id)) ||
        bytes_zero(&coordinate->world_context_id, sizeof(coordinate->world_context_id)) ||
        bytes_zero(&coordinate->language_modality_id, sizeof(coordinate->language_modality_id)) ||
        bytes_zero(&coordinate->valid_time_scope_id, sizeof(coordinate->valid_time_scope_id)) ||
        bytes_zero(&coordinate->evidence_boundary_id, sizeof(coordinate->evidence_boundary_id)) ||
        bytes_zero(&coordinate->rating_recipe_id, sizeof(coordinate->rating_recipe_id)) ||
        coordinate->participant_role == 0u || coordinate->arena_kind == 0u ||
        coordinate->rating_recipe_version == 0u ||
        coordinate->flags != LAPLACE_STANDING_FLAGS_NONE) {
        return LAPLACE_STANDING_COORDINATE_INVALID;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STANDING_ARENA_SCOPE_DOMAIN,
        sizeof(LAPLACE_STANDING_ARENA_SCOPE_DOMAIN) - 1u);
    hash_arena_scope(&hasher, coordinate);
    finish_digest(&hasher, arena_scope_id);
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STANDING_COORDINATE_DOMAIN,
        sizeof(LAPLACE_STANDING_COORDINATE_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, coordinate->participant_id.bytes, 32u);
    blake3_hasher_update(&hasher, arena_scope_id->bytes, 32u);
    finish_digest(&hasher, coordinate_id);
    return LAPLACE_STANDING_OK;
}

static void hash_state(blake3_hasher* hasher, const laplace_standing_state* state) {
    blake3_hasher_update(hasher, state->coordinate_id.bytes, 32u);
    blake3_hasher_update(hasher, state->arena_scope_id.bytes, 32u);
    blake3_hasher_update(hasher, state->prior_state_id.bytes, 32u);
    blake3_hasher_update(hasher, state->epoch_id.bytes, 32u);
    blake3_hasher_update(hasher, state->rating_recipe_id.bytes, 32u);
    hash_double(hasher, state->rating);
    hash_double(hasher, state->rating_deviation);
    hash_double(hasher, state->volatility);
    hash_u64(hasher, state->eligible_match_count);
    hash_u64(hasher, state->period_ordinal);
    hash_u32(hasher, state->rating_recipe_version);
    hash_u32(hasher, state->flags);
}

laplace_standing_status laplace_standing_state_identify(
    const laplace_standing_state* state, laplace_digest256* state_id) {
    blake3_hasher hasher;
    if (state == NULL || state_id == NULL ||
        bytes_zero(&state->coordinate_id, sizeof(state->coordinate_id)) ||
        bytes_zero(&state->arena_scope_id, sizeof(state->arena_scope_id)) ||
        bytes_zero(&state->epoch_id, sizeof(state->epoch_id)) ||
        bytes_zero(&state->rating_recipe_id, sizeof(state->rating_recipe_id)) ||
        !numeric_state_valid(state->rating, state->rating_deviation, state->volatility) ||
        state->rating_recipe_version == 0u ||
        state->flags != LAPLACE_STANDING_FLAGS_NONE ||
        (state->period_ordinal == 0u && !bytes_zero(&state->prior_state_id, sizeof(state->prior_state_id))) ||
        (state->period_ordinal != 0u && bytes_zero(&state->prior_state_id, sizeof(state->prior_state_id)))) {
        return LAPLACE_STANDING_STATE_INVALID;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STANDING_STATE_DOMAIN,
        sizeof(LAPLACE_STANDING_STATE_DOMAIN) - 1u);
    hash_state(&hasher, state);
    finish_digest(&hasher, state_id);
    return LAPLACE_STANDING_OK;
}

static int outcome_rateable(uint32_t outcome_kind) {
    return outcome_kind >= LAPLACE_STANDING_OUTCOME_CONFIRM &&
        outcome_kind <= LAPLACE_STANDING_OUTCOME_UNCERTAIN;
}

static void hash_event(blake3_hasher* hasher, const laplace_standing_event* event) {
    blake3_hasher_update(hasher, event->participant_coordinate_id.bytes, 32u);
    blake3_hasher_update(hasher, event->participant_prior_state_id.bytes, 32u);
    blake3_hasher_update(hasher, event->opponent_prior_state.state_id.bytes, 32u);
    blake3_hasher_update(hasher, event->period_id.bytes, 32u);
    blake3_hasher_update(hasher, event->eligible_root_id.bytes, 32u);
    blake3_hasher_update(hasher, event->outcome_mapping_id.bytes, 32u);
    blake3_hasher_update(hasher, event->context_id.bytes, 32u);
    blake3_hasher_update(hasher, event->valid_time_id.bytes, 32u);
    hash_u64(hasher, event->score_numerator);
    hash_u64(hasher, event->score_denominator);
    hash_u32(hasher, event->outcome_kind);
    hash_u32(hasher, event->flags);
}

laplace_standing_status laplace_standing_event_identify(
    const laplace_standing_event* event, laplace_digest256* event_id) {
    blake3_hasher hasher;
    if (event == NULL || event_id == NULL ||
        bytes_zero(&event->participant_coordinate_id, sizeof(event->participant_coordinate_id)) ||
        bytes_zero(&event->participant_prior_state_id, sizeof(event->participant_prior_state_id)) ||
        bytes_zero(&event->period_id, sizeof(event->period_id)) ||
        bytes_zero(&event->eligible_root_id, sizeof(event->eligible_root_id)) ||
        bytes_zero(&event->outcome_mapping_id, sizeof(event->outcome_mapping_id)) ||
        bytes_zero(&event->context_id, sizeof(event->context_id)) ||
        bytes_zero(&event->valid_time_id, sizeof(event->valid_time_id)) ||
        event->score_denominator == 0u ||
        event->score_numerator > event->score_denominator ||
        !outcome_rateable(event->outcome_kind) ||
        event->flags != LAPLACE_STANDING_FLAGS_NONE) {
        return outcome_rateable(event == NULL ? 0u : event->outcome_kind)
            ? LAPLACE_STANDING_EVENT_INVALID
            : LAPLACE_STANDING_OUTCOME_NOT_RATEABLE;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STANDING_EVENT_DOMAIN,
        sizeof(LAPLACE_STANDING_EVENT_DOMAIN) - 1u);
    hash_event(&hasher, event);
    finish_digest(&hasher, event_id);
    return LAPLACE_STANDING_OK;
}

laplace_standing_status laplace_standing_onboard(
    const laplace_standing_recipe* recipe,
    const laplace_digest256* participant_id,
    const laplace_digest256* initialization_epoch_id,
    laplace_standing_state* state) {
    laplace_standing_coordinate coordinate;
    laplace_digest256 expected_recipe_id;
    laplace_standing_status status;
    if (state == NULL || participant_id == NULL || initialization_epoch_id == NULL ||
        bytes_zero(participant_id, sizeof(*participant_id)) ||
        bytes_zero(initialization_epoch_id, sizeof(*initialization_epoch_id)) ||
        recipe == NULL) {
        return LAPLACE_STANDING_INVALID_ARGUMENT;
    }
    status = laplace_standing_recipe_identify(recipe, &expected_recipe_id);
    if (status != LAPLACE_STANDING_OK) {
        return status;
    }
    if (!digest_equal(&recipe->recipe_id, &expected_recipe_id)) {
        return LAPLACE_STANDING_RECIPE_IDENTITY_MISMATCH;
    }
    memset(&coordinate, 0, sizeof(coordinate));
    coordinate.participant_id = *participant_id;
    coordinate.evaluation_law_id = recipe->evaluation_law_id;
    coordinate.world_context_id = recipe->world_context_id;
    coordinate.language_modality_id = recipe->language_modality_id;
    coordinate.valid_time_scope_id = recipe->valid_time_scope_id;
    coordinate.evidence_boundary_id = recipe->evidence_boundary_id;
    coordinate.rating_recipe_id = recipe->recipe_id;
    coordinate.participant_role = recipe->participant_role;
    coordinate.arena_kind = recipe->arena_kind;
    coordinate.rating_recipe_version = recipe->version;
    coordinate.flags = LAPLACE_STANDING_FLAGS_NONE;
    memset(state, 0, sizeof(*state));
    status = laplace_standing_coordinate_identify(
        &coordinate, &state->arena_scope_id, &state->coordinate_id);
    if (status != LAPLACE_STANDING_OK) {
        return status;
    }
    state->epoch_id = *initialization_epoch_id;
    state->rating_recipe_id = recipe->recipe_id;
    state->rating = recipe->default_rating;
    state->rating_deviation = recipe->default_rating_deviation;
    state->volatility = recipe->default_volatility;
    state->rating_recipe_version = recipe->version;
    state->flags = LAPLACE_STANDING_FLAGS_NONE;
    status = laplace_standing_state_identify(state, &state->state_id);
    return status;
}

static int compare_event(const void* left, const void* right) {
    const laplace_standing_event* first = (const laplace_standing_event*)left;
    const laplace_standing_event* second = (const laplace_standing_event*)right;
    return memcmp(first->event_id.bytes, second->event_id.bytes, 32u);
}

static int compare_digest(const void* left, const void* right) {
    const laplace_digest256* first = (const laplace_digest256*)left;
    const laplace_digest256* second = (const laplace_digest256*)right;
    return memcmp(first->bytes, second->bytes, 32u);
}

static double g_function(double phi) {
    const double pi_squared = LAPLACE_STANDING_PI * LAPLACE_STANDING_PI;
    return 1.0 / sqrt(1.0 + (3.0 * phi * phi / pi_squared));
}

static double expectation(double mu, double opponent_mu, double opponent_phi) {
    const double exponent = -g_function(opponent_phi) * (mu - opponent_mu);
    if (exponent >= 0.0) {
        const double value = exp(-exponent);
        return value / (1.0 + value);
    }
    return 1.0 / (1.0 + exp(exponent));
}

static double volatility_function(
    double value, double delta_squared, double phi_squared, double variance,
    double a, double tau_squared) {
    const double exponential = exp(value);
    const double denominator = phi_squared + variance + exponential;
    return exponential * (delta_squared - phi_squared - variance - exponential) /
            (2.0 * denominator * denominator) -
        (value - a) / tau_squared;
}

static laplace_standing_status solve_volatility(
    double phi, double volatility, double variance, double delta,
    double tau, double epsilon, double* result, uint32_t* iterations) {
    const double a = log(volatility * volatility);
    const double delta_squared = delta * delta;
    const double phi_squared = phi * phi;
    const double tau_squared = tau * tau;
    double left = a;
    double right;
    double f_left;
    double f_right;
    uint32_t step = 0u;
    if (delta_squared > phi_squared + variance) {
        right = log(delta_squared - phi_squared - variance);
    } else {
        do {
            step += 1u;
            if (step > LAPLACE_STANDING_MAX_BRACKET_STEPS) {
                return LAPLACE_STANDING_CONVERGENCE_FAILED;
            }
            right = a - (double)step * tau;
        } while (volatility_function(
            right, delta_squared, phi_squared, variance, a, tau_squared) < 0.0);
    }
    f_left = volatility_function(
        left, delta_squared, phi_squared, variance, a, tau_squared);
    f_right = volatility_function(
        right, delta_squared, phi_squared, variance, a, tau_squared);
    *iterations = 0u;
    while (fabs(right - left) > epsilon) {
        const double divisor = f_right - f_left;
        double candidate;
        double f_candidate;
        if (*iterations >= LAPLACE_STANDING_MAX_ITERATIONS ||
            divisor == 0.0 || !isfinite(divisor)) {
            return LAPLACE_STANDING_CONVERGENCE_FAILED;
        }
        candidate = left + (left - right) * f_left / divisor;
        f_candidate = volatility_function(
            candidate, delta_squared, phi_squared, variance, a, tau_squared);
        if (!isfinite(candidate) || !isfinite(f_candidate)) {
            return LAPLACE_STANDING_NUMERIC_INVALID;
        }
        if (f_candidate * f_right <= 0.0) {
            left = right;
            f_left = f_right;
        } else {
            f_left /= 2.0;
        }
        right = candidate;
        f_right = f_candidate;
        *iterations += 1u;
    }
    *result = exp(left / 2.0);
    return isfinite(*result) && *result > 0.0
        ? LAPLACE_STANDING_OK
        : LAPLACE_STANDING_NUMERIC_INVALID;
}

static void initialize_receipt(laplace_standing_period_receipt* receipt) {
    memset(receipt, 0, sizeof(*receipt));
    receipt->version = LAPLACE_STANDING_VERSION;
    receipt->flags = LAPLACE_STANDING_FLAGS_NONE;
}

laplace_standing_status laplace_standing_calculate_period(
    const laplace_standing_recipe* recipe,
    const laplace_standing_state* prior_state,
    const laplace_digest256* period_id,
    const laplace_standing_event* events,
    size_t event_count,
    laplace_standing_state* successor_state,
    laplace_standing_period_receipt* receipt,
    laplace_standing_error* error) {
    laplace_standing_event* ordered = NULL;
    laplace_digest256* roots = NULL;
    laplace_digest256 expected_recipe_id;
    laplace_digest256 expected_state_id;
    blake3_hasher input_hasher;
    blake3_hasher output_hasher;
    blake3_hasher receipt_hasher;
    double participant_rating;
    double participant_deviation;
    double participant_volatility;
    double mu;
    double phi;
    double variance_inverse = 0.0;
    double improvement_sum = 0.0;
    double variance;
    double delta;
    double next_volatility = 0.0;
    double phi_star;
    double next_phi;
    double next_mu;
    uint32_t iterations = 0u;
    size_t index;
    laplace_standing_status status;
    if (receipt != NULL) {
        initialize_receipt(receipt);
    }
    if (error != NULL) {
        error->event_index = UINT64_MAX;
        error->volatility_iterations = 0u;
        error->reserved = 0u;
    }
    if (recipe == NULL || prior_state == NULL || period_id == NULL || events == NULL ||
        event_count == 0u || successor_state == NULL || receipt == NULL ||
        bytes_zero(period_id, sizeof(*period_id))) {
        return LAPLACE_STANDING_INVALID_ARGUMENT;
    }
    status = laplace_standing_recipe_identify(recipe, &expected_recipe_id);
    if (status != LAPLACE_STANDING_OK) {
        return status;
    }
    if (!digest_equal(&recipe->recipe_id, &expected_recipe_id)) {
        return LAPLACE_STANDING_RECIPE_IDENTITY_MISMATCH;
    }
    status = laplace_standing_state_identify(prior_state, &expected_state_id);
    if (status != LAPLACE_STANDING_OK) {
        return status;
    }
    if (!digest_equal(&expected_state_id, &prior_state->state_id)) {
        return LAPLACE_STANDING_STATE_IDENTITY_MISMATCH;
    }
    if (!digest_equal(&prior_state->rating_recipe_id, &recipe->recipe_id) ||
        prior_state->rating_recipe_version != recipe->version) {
        return LAPLACE_STANDING_ARENA_MISMATCH;
    }
    if (!initial_state_matches_recipe(prior_state, recipe)) {
        return LAPLACE_STANDING_PRIOR_MISMATCH;
    }
    if (event_count > SIZE_MAX / sizeof(*ordered) ||
        event_count > SIZE_MAX / sizeof(*roots)) {
        return LAPLACE_STANDING_OVERFLOW;
    }
    ordered = (laplace_standing_event*)malloc(event_count * sizeof(*ordered));
    roots = (laplace_digest256*)malloc(event_count * sizeof(*roots));
    if (ordered == NULL || roots == NULL) {
        free(ordered);
        free(roots);
        return LAPLACE_STANDING_RESOURCE_INSUFFICIENT;
    }
    memcpy(ordered, events, event_count * sizeof(*ordered));
    qsort(ordered, event_count, sizeof(*ordered), compare_event);
    for (index = 0u; index < event_count; ++index) {
        laplace_digest256 expected_event_id;
        laplace_digest256 expected_mapping_id;
        laplace_digest256 expected_opponent_state_id;
        status = laplace_standing_state_identify(
            &ordered[index].opponent_prior_state, &expected_opponent_state_id);
        if (status != LAPLACE_STANDING_OK) {
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        if (!digest_equal(&expected_opponent_state_id,
                &ordered[index].opponent_prior_state.state_id)) {
            status = LAPLACE_STANDING_STATE_IDENTITY_MISMATCH;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        status = laplace_standing_event_identify(&ordered[index], &expected_event_id);
        if (status != LAPLACE_STANDING_OK) {
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        if (!digest_equal(&expected_event_id, &ordered[index].event_id)) {
            status = LAPLACE_STANDING_EVENT_IDENTITY_MISMATCH;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        status = laplace_standing_outcome_mapping_identify(
            recipe, ordered[index].outcome_kind, &expected_mapping_id);
        if (status != LAPLACE_STANDING_OK) {
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
#if !defined(LAPLACE_TEST_STANDING_ACCEPT_CALLER_MAPPING)
        if (!digest_equal(&expected_mapping_id,
                          &ordered[index].outcome_mapping_id) ||
            ordered[index].score_numerator !=
                recipe->score_numerator[ordered[index].outcome_kind - 1u] ||
            ordered[index].score_denominator !=
                recipe->score_denominator[ordered[index].outcome_kind - 1u]) {
            status = LAPLACE_STANDING_OUTCOME_MAPPING_MISMATCH;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
#endif
        if (index != 0u && digest_equal(
                &ordered[index - 1u].event_id, &ordered[index].event_id)) {
            status = LAPLACE_STANDING_EVENT_DUPLICATE;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        if (!digest_equal(&ordered[index].participant_coordinate_id,
                &prior_state->coordinate_id) ||
            !digest_equal(&ordered[index].participant_prior_state_id,
                &prior_state->state_id) ||
            !digest_equal(&ordered[index].period_id, period_id)) {
            status = LAPLACE_STANDING_PRIOR_MISMATCH;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        if (!digest_equal(&ordered[index].opponent_prior_state.arena_scope_id,
                &prior_state->arena_scope_id) ||
            digest_equal(&ordered[index].opponent_prior_state.coordinate_id,
                &prior_state->coordinate_id) ||
            !digest_equal(&ordered[index].opponent_prior_state.rating_recipe_id,
                &prior_state->rating_recipe_id) ||
            ordered[index].opponent_prior_state.rating_recipe_version !=
                prior_state->rating_recipe_version) {
            status = LAPLACE_STANDING_ARENA_MISMATCH;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        if (!initial_state_matches_recipe(
                &ordered[index].opponent_prior_state, recipe)) {
            status = LAPLACE_STANDING_PRIOR_MISMATCH;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
        roots[index] = ordered[index].eligible_root_id;
    }
    qsort(roots, event_count, sizeof(*roots), compare_digest);
    for (index = 1u; index < event_count; ++index) {
        if (digest_equal(&roots[index - 1u], &roots[index])) {
            status = LAPLACE_STANDING_DEPENDENCE_ROOT_DUPLICATE;
            if (error != NULL) error->event_index = (uint64_t)index;
            goto failure;
        }
    }
#if defined(LAPLACE_TEST_STANDING_RESET_PRIOR)
    participant_rating = LAPLACE_STANDING_SCALE_CENTER;
    participant_deviation = 350.0;
    participant_volatility = 0.06;
#else
    participant_rating = prior_state->rating;
    participant_deviation = prior_state->rating_deviation;
    participant_volatility = prior_state->volatility;
#endif
    mu = (participant_rating - LAPLACE_STANDING_SCALE_CENTER) /
        LAPLACE_STANDING_SCALE_DIVISOR;
    phi = participant_deviation / LAPLACE_STANDING_SCALE_DIVISOR;
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(&input_hasher, LAPLACE_STANDING_INPUT_DOMAIN,
        sizeof(LAPLACE_STANDING_INPUT_DOMAIN) - 1u);
    blake3_hasher_update(&input_hasher, prior_state->state_id.bytes, 32u);
    blake3_hasher_update(&input_hasher, period_id->bytes, 32u);
    blake3_hasher_update(&input_hasher, recipe->recipe_id.bytes, 32u);
    hash_u64(&input_hasher, (uint64_t)event_count);
    for (index = 0u; index < event_count; ++index) {
        double opponent_rating = ordered[index].opponent_prior_state.rating;
        double opponent_deviation = ordered[index].opponent_prior_state.rating_deviation;
        double opponent_mu;
        double opponent_phi;
        double g;
        double expected;
        double score;
#if defined(LAPLACE_TEST_STANDING_NEUTRAL_OPPONENT)
        opponent_rating = LAPLACE_STANDING_SCALE_CENTER;
        opponent_deviation = 350.0;
#endif
        opponent_mu = (opponent_rating - LAPLACE_STANDING_SCALE_CENTER) /
            LAPLACE_STANDING_SCALE_DIVISOR;
        opponent_phi = opponent_deviation / LAPLACE_STANDING_SCALE_DIVISOR;
        g = g_function(opponent_phi);
        expected = expectation(mu, opponent_mu, opponent_phi);
        score = (double)ordered[index].score_numerator /
            (double)ordered[index].score_denominator;
        variance_inverse += g * g * expected * (1.0 - expected);
        improvement_sum += g * (score - expected);
        blake3_hasher_update(&input_hasher, ordered[index].event_id.bytes, 32u);
    }
    if (!isfinite(variance_inverse) || variance_inverse <= 0.0 ||
        !isfinite(improvement_sum)) {
        status = LAPLACE_STANDING_NUMERIC_INVALID;
        goto failure;
    }
    variance = 1.0 / variance_inverse;
    delta = variance * improvement_sum;
    status = solve_volatility(phi, participant_volatility, variance, delta,
        recipe->volatility_constraint, recipe->convergence_tolerance,
        &next_volatility, &iterations);
    if (status != LAPLACE_STANDING_OK) {
        if (error != NULL) error->volatility_iterations = iterations;
        goto failure;
    }
    phi_star = sqrt(phi * phi + next_volatility * next_volatility);
    next_phi = 1.0 / sqrt(1.0 / (phi_star * phi_star) + 1.0 / variance);
    next_mu = mu + next_phi * next_phi * improvement_sum;
    if (!isfinite(phi_star) || !isfinite(next_phi) || next_phi <= 0.0 ||
        !isfinite(next_mu)) {
        status = LAPLACE_STANDING_NUMERIC_INVALID;
        goto failure;
    }
    if (UINT64_MAX - prior_state->eligible_match_count < (uint64_t)event_count ||
        prior_state->period_ordinal == UINT64_MAX) {
        status = LAPLACE_STANDING_OVERFLOW;
        goto failure;
    }
    memset(successor_state, 0, sizeof(*successor_state));
    successor_state->coordinate_id = prior_state->coordinate_id;
    successor_state->arena_scope_id = prior_state->arena_scope_id;
    successor_state->prior_state_id = prior_state->state_id;
    successor_state->epoch_id = *period_id;
    successor_state->rating_recipe_id = prior_state->rating_recipe_id;
    successor_state->rating = next_mu * LAPLACE_STANDING_SCALE_DIVISOR +
        LAPLACE_STANDING_SCALE_CENTER;
    successor_state->rating_deviation = next_phi * LAPLACE_STANDING_SCALE_DIVISOR;
    successor_state->volatility = next_volatility;
    successor_state->eligible_match_count =
        prior_state->eligible_match_count + (uint64_t)event_count;
    successor_state->period_ordinal = prior_state->period_ordinal + 1u;
    successor_state->rating_recipe_version = prior_state->rating_recipe_version;
    successor_state->flags = LAPLACE_STANDING_FLAGS_NONE;
    status = laplace_standing_state_identify(
        successor_state, &successor_state->state_id);
    if (status != LAPLACE_STANDING_OK) {
        goto failure;
    }
    receipt->prior_state_id = prior_state->state_id;
    receipt->successor_state_id = successor_state->state_id;
    receipt->period_id = *period_id;
    finish_digest(&input_hasher, &receipt->input_fingerprint);
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(&output_hasher, LAPLACE_STANDING_OUTPUT_DOMAIN,
        sizeof(LAPLACE_STANDING_OUTPUT_DOMAIN) - 1u);
    blake3_hasher_update(&output_hasher, successor_state->state_id.bytes, 32u);
    finish_digest(&output_hasher, &receipt->output_fingerprint);
    receipt->eligible_event_count = (uint64_t)event_count;
    receipt->prior_match_count = prior_state->eligible_match_count;
    receipt->successor_match_count = successor_state->eligible_match_count;
    receipt->volatility_iterations = iterations;
    receipt->status = LAPLACE_STANDING_OK;
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(&receipt_hasher, LAPLACE_STANDING_RECEIPT_DOMAIN,
        sizeof(LAPLACE_STANDING_RECEIPT_DOMAIN) - 1u);
    blake3_hasher_update(&receipt_hasher, receipt->prior_state_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->successor_state_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->period_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(&receipt_hasher, receipt->eligible_event_count);
    hash_u64(&receipt_hasher, receipt->prior_match_count);
    hash_u64(&receipt_hasher, receipt->successor_match_count);
    hash_u32(&receipt_hasher, receipt->volatility_iterations);
    hash_u32(&receipt_hasher, receipt->version);
    hash_u32(&receipt_hasher, receipt->status);
    hash_u32(&receipt_hasher, receipt->flags);
    finish_digest(&receipt_hasher, &receipt->receipt_id);
    if (error != NULL) error->volatility_iterations = iterations;
    free(ordered);
    free(roots);
    return LAPLACE_STANDING_OK;

failure:
    receipt->status = (uint32_t)status;
    free(ordered);
    free(roots);
    return status;
}

laplace_standing_status laplace_standing_calculate_period_batch(
    const laplace_standing_period_input* inputs,
    size_t input_count,
    laplace_standing_period_result* result,
    laplace_standing_error* error) {
    laplace_standing_event* events;
    laplace_digest256 identified_state_id;
    laplace_digest256 identified_recipe_id;
    size_t index;
    laplace_standing_status status;
    if (inputs == NULL || input_count == 0u || result == NULL ||
        input_count > SIZE_MAX / sizeof(*events)) {
        return LAPLACE_STANDING_INVALID_ARGUMENT;
    }
    events = (laplace_standing_event*)malloc(input_count * sizeof(*events));
    if (events == NULL) {
        return LAPLACE_STANDING_RESOURCE_INSUFFICIENT;
    }
    for (index = 0u; index < input_count; ++index) {
        status = laplace_standing_recipe_identify(
            &inputs[index].recipe, &identified_recipe_id);
        if (status != LAPLACE_STANDING_OK ||
            !digest_equal(&identified_recipe_id,
                          &inputs[index].recipe.recipe_id) ||
            !digest_equal(&inputs[index].recipe.recipe_id,
                          &inputs[0].recipe.recipe_id)) {
            if (error != NULL) {
                error->event_index = (uint64_t)index;
                error->volatility_iterations = 0u;
                error->reserved = 0u;
            }
            free(events);
            return status == LAPLACE_STANDING_OK
                ? LAPLACE_STANDING_RECIPE_IDENTITY_MISMATCH
                : status;
        }
        status = laplace_standing_state_identify(
            &inputs[index].prior_state, &identified_state_id);
        if (status != LAPLACE_STANDING_OK ||
            !digest_equal(&identified_state_id,
                          &inputs[index].prior_state.state_id) ||
            !digest_equal(&inputs[index].prior_state.state_id,
                          &inputs[0].prior_state.state_id)) {
            if (error != NULL) {
                error->event_index = (uint64_t)index;
                error->volatility_iterations = 0u;
                error->reserved = 0u;
            }
            free(events);
            return LAPLACE_STANDING_PRIOR_MISMATCH;
        }
        events[index] = inputs[index].event;
    }
    memset(result, 0, sizeof(*result));
    status = laplace_standing_calculate_period(
        &inputs[0].recipe, &inputs[0].prior_state,
        &inputs[0].event.period_id, events, input_count,
        &result->successor_state,
        &result->receipt, error);
    free(events);
    return status;
}
