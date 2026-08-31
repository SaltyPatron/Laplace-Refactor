#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "laplace/standing_calculation.h"

namespace {

laplace_digest256 digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

bool equal_digest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_standing_coordinate coordinate(std::uint8_t participant_seed) {
    laplace_standing_coordinate value{};
    value.participant_id = digest(participant_seed);
    value.evaluation_law_id = digest(0x20u);
    value.world_context_id = digest(0x30u);
    value.language_modality_id = digest(0x40u);
    value.valid_time_scope_id = digest(0x50u);
    value.evidence_boundary_id = digest(0x60u);
    value.rating_recipe_id = digest(0x70u);
    value.participant_role = 1u;
    value.arena_kind = 1u;
    value.rating_recipe_version = 1u;
    value.flags = LAPLACE_STANDING_FLAGS_NONE;
    return value;
}

laplace_standing_state onboard(
    std::uint8_t participant_seed,
    double rating,
    double deviation,
    double volatility = 0.06) {
    const auto participant = coordinate(participant_seed);
    const auto epoch = digest(0x80u);
    laplace_standing_state state{};
    EXPECT_EQ(laplace_standing_onboard(
                  &participant, &epoch, rating, deviation, volatility, &state),
              LAPLACE_STANDING_OK);
    return state;
}

laplace_standing_event event(
    const laplace_standing_state& participant,
    const laplace_standing_state& opponent,
    const laplace_digest256& period_id,
    std::uint8_t seed,
    std::uint64_t score_numerator,
    std::uint64_t score_denominator = 1u) {
    laplace_standing_event value{};
    value.participant_coordinate_id = participant.coordinate_id;
    value.participant_prior_state_id = participant.state_id;
    value.opponent_prior_state = opponent;
    value.period_id = period_id;
    value.eligible_root_id = digest(seed);
    value.outcome_mapping_id = digest(static_cast<std::uint8_t>(seed + 1u));
    value.context_id = digest(static_cast<std::uint8_t>(seed + 2u));
    value.valid_time_id = digest(static_cast<std::uint8_t>(seed + 3u));
    value.score_numerator = score_numerator;
    value.score_denominator = score_denominator;
    value.outcome_kind = score_numerator == 0u
        ? LAPLACE_STANDING_OUTCOME_REFUTE
        : (score_numerator == score_denominator
               ? LAPLACE_STANDING_OUTCOME_CONFIRM
               : LAPLACE_STANDING_OUTCOME_DRAW);
    value.flags = LAPLACE_STANDING_FLAGS_NONE;
    EXPECT_EQ(laplace_standing_event_identify(&value, &value.event_id),
              LAPLACE_STANDING_OK);
    return value;
}

laplace_standing_status calculate(
    const laplace_standing_state& prior,
    const laplace_digest256& period,
    const laplace_standing_event* events,
    std::size_t event_count,
    laplace_standing_state* successor,
    laplace_standing_period_receipt* receipt = nullptr,
    laplace_standing_error* error = nullptr) {
    laplace_standing_period_receipt local_receipt{};
    laplace_standing_error local_error{};
    return laplace_standing_calculate_period(
        &prior, &period, events, event_count, 0.5, 0.000001,
        successor, receipt == nullptr ? &local_receipt : receipt,
        error == nullptr ? &local_error : error);
}

TEST(StandingCalculation, MatchesCorrectedGlickoTwoReferenceExample) {
    const auto participant = onboard(0x01u, 1500.0, 200.0);
    const auto period = digest(0x90u);
    const std::array<laplace_standing_state, 3> opponents{
        onboard(0x02u, 1400.0, 30.0),
        onboard(0x03u, 1550.0, 100.0),
        onboard(0x04u, 1700.0, 300.0)};
    const std::array<laplace_standing_event, 3> events{
        event(participant, opponents[0], period, 0xa0u, 1u),
        event(participant, opponents[1], period, 0xb0u, 0u),
        event(participant, opponents[2], period, 0xc0u, 0u)};
    laplace_standing_state successor{};
    laplace_standing_period_receipt receipt{};

    ASSERT_EQ(calculate(participant, period, events.data(), events.size(),
                        &successor, &receipt),
              LAPLACE_STANDING_OK);
    EXPECT_NEAR(successor.rating, 1464.06, 0.01);
    EXPECT_NEAR(successor.rating_deviation, 151.52, 0.01);
    EXPECT_NEAR(successor.volatility, 0.05999, 0.00001);
    EXPECT_EQ(successor.eligible_match_count, 3u);
    EXPECT_EQ(successor.period_ordinal, 1u);
    EXPECT_TRUE(equal_digest(successor.prior_state_id, participant.state_id));
    EXPECT_TRUE(equal_digest(receipt.successor_state_id, successor.state_id));
    EXPECT_EQ(receipt.status, LAPLACE_STANDING_OK);
}

TEST(StandingCalculation, CanonicalOrderProducesOneStateAndReceipt) {
    const auto participant = onboard(0x01u, 1500.0, 200.0);
    const auto period = digest(0x91u);
    const auto opponent_a = onboard(0x02u, 1420.0, 80.0);
    const auto opponent_b = onboard(0x03u, 1620.0, 140.0);
    std::array<laplace_standing_event, 2> forward{
        event(participant, opponent_a, period, 0xa1u, 1u),
        event(participant, opponent_b, period, 0xb1u, 0u)};
    auto reverse = forward;
    std::reverse(reverse.begin(), reverse.end());
    laplace_standing_state first{};
    laplace_standing_state second{};
    laplace_standing_period_receipt first_receipt{};
    laplace_standing_period_receipt second_receipt{};

    ASSERT_EQ(calculate(participant, period, forward.data(), forward.size(),
                        &first, &first_receipt), LAPLACE_STANDING_OK);
    ASSERT_EQ(calculate(participant, period, reverse.data(), reverse.size(),
                        &second, &second_receipt), LAPLACE_STANDING_OK);
    EXPECT_EQ(std::memcmp(&first, &second, sizeof(first)), 0);
    EXPECT_EQ(std::memcmp(&first_receipt, &second_receipt,
                          sizeof(first_receipt)), 0);
}

TEST(StandingCalculation, TypedBatchCarriesTheCompletePeriodProgram) {
    const auto participant = onboard(0x01u, 1500.0, 200.0);
    const auto period = digest(0x98u);
    std::array<laplace_standing_period_input, 2> inputs{};
    inputs[0].prior_state = participant;
    inputs[0].event = event(
        participant, onboard(0x02u, 1400.0, 80.0), period, 0xa7u, 1u);
    inputs[0].volatility_constraint = 0.5;
    inputs[0].convergence_tolerance = 0.000001;
    inputs[1] = inputs[0];
    inputs[1].event = event(
        participant, onboard(0x03u, 1600.0, 80.0), period, 0xb7u, 0u);
    laplace_standing_period_result result{};
    laplace_standing_error error{};

    ASSERT_EQ(laplace_standing_calculate_period_batch(
                  inputs.data(), inputs.size(), &result, &error),
              LAPLACE_STANDING_OK);
    EXPECT_EQ(result.successor_state.eligible_match_count, 2u);
    EXPECT_EQ(result.receipt.eligible_event_count, 2u);

    inputs[1].prior_state.rating += 1.0;
    EXPECT_EQ(laplace_standing_calculate_period_batch(
                  inputs.data(), inputs.size(), &result, &error),
              LAPLACE_STANDING_PRIOR_MISMATCH);
    EXPECT_EQ(error.event_index, 1u);
}

TEST(StandingCalculation, RealOpponentPriorChangesUpdate) {
    const auto participant = onboard(0x01u, 1500.0, 200.0);
    const auto period = digest(0x92u);
    const auto low = onboard(0x02u, 1200.0, 50.0);
    const auto high = onboard(0x03u, 1800.0, 50.0);
    const auto low_event = event(participant, low, period, 0xa2u, 1u);
    const auto high_event = event(participant, high, period, 0xb2u, 1u);
    laplace_standing_state after_low{};
    laplace_standing_state after_high{};

    ASSERT_EQ(calculate(participant, period, &low_event, 1u, &after_low),
              LAPLACE_STANDING_OK);
    ASSERT_EQ(calculate(participant, period, &high_event, 1u, &after_high),
              LAPLACE_STANDING_OK);
    EXPECT_GT(after_high.rating, after_low.rating + 50.0);
}

TEST(StandingCalculation, SuccessorConsumesPriorState) {
    const auto initial = onboard(0x01u, 1500.0, 200.0);
    const auto opponent = onboard(0x02u, 1400.0, 80.0);
    const auto first_period = digest(0x93u);
    const auto first_event = event(initial, opponent, first_period, 0xa3u, 1u);
    laplace_standing_state first{};
    ASSERT_EQ(calculate(initial, first_period, &first_event, 1u, &first),
              LAPLACE_STANDING_OK);

    const auto second_period = digest(0x94u);
    const auto second_event = event(first, opponent, second_period, 0xb3u, 1u);
    laplace_standing_state second{};
    ASSERT_EQ(calculate(first, second_period, &second_event, 1u, &second),
              LAPLACE_STANDING_OK);
    EXPECT_GT(second.rating, first.rating);
    EXPECT_LT(second.rating_deviation, first.rating_deviation);
    EXPECT_EQ(second.eligible_match_count, 2u);
    EXPECT_EQ(second.period_ordinal, 2u);
    EXPECT_TRUE(equal_digest(second.prior_state_id, first.state_id));
}

TEST(StandingCalculation, RejectsDuplicateDependenceRoot) {
    const auto participant = onboard(0x01u, 1500.0, 200.0);
    const auto period = digest(0x95u);
    const auto opponent_a = onboard(0x02u, 1400.0, 80.0);
    const auto opponent_b = onboard(0x03u, 1600.0, 80.0);
    std::array<laplace_standing_event, 2> events{
        event(participant, opponent_a, period, 0xa4u, 1u),
        event(participant, opponent_b, period, 0xb4u, 0u)};
    events[1].eligible_root_id = events[0].eligible_root_id;
    ASSERT_EQ(laplace_standing_event_identify(&events[1], &events[1].event_id),
              LAPLACE_STANDING_OK);
    laplace_standing_state successor{};
    EXPECT_EQ(calculate(participant, period, events.data(), events.size(),
                        &successor),
              LAPLACE_STANDING_DEPENDENCE_ROOT_DUPLICATE);
}

TEST(StandingCalculation, UnknownAndAbsenceAreNotLosses) {
    const auto participant = onboard(0x01u, 1500.0, 200.0);
    const auto opponent = onboard(0x02u, 1400.0, 80.0);
    const auto period = digest(0x96u);
    auto unknown = event(participant, opponent, period, 0xa5u, 0u);
    unknown.outcome_kind = LAPLACE_STANDING_OUTCOME_UNKNOWN;
    EXPECT_EQ(laplace_standing_event_identify(&unknown, &unknown.event_id),
              LAPLACE_STANDING_OUTCOME_NOT_RATEABLE);
    unknown.outcome_kind = LAPLACE_STANDING_OUTCOME_ABSENT;
    EXPECT_EQ(laplace_standing_event_identify(&unknown, &unknown.event_id),
              LAPLACE_STANDING_OUTCOME_NOT_RATEABLE);
}

TEST(StandingCalculation, RejectsCrossArenaAndIdentityDrift) {
    const auto participant = onboard(0x01u, 1500.0, 200.0);
    const auto period = digest(0x97u);
    auto other_coordinate = coordinate(0x02u);
    other_coordinate.world_context_id = digest(0xeeu);
    const auto epoch = digest(0x80u);
    laplace_standing_state other_arena{};
    ASSERT_EQ(laplace_standing_onboard(
                  &other_coordinate, &epoch, 1400.0, 80.0, 0.06, &other_arena),
              LAPLACE_STANDING_OK);
    auto cross_arena = event(participant, other_arena, period, 0xa6u, 1u);
    laplace_standing_state successor{};
    EXPECT_EQ(calculate(participant, period, &cross_arena, 1u, &successor),
              LAPLACE_STANDING_ARENA_MISMATCH);

    const auto same_arena = onboard(0x03u, 1400.0, 80.0);
    auto drift = event(participant, same_arena, period, 0xb6u, 1u);
    drift.opponent_prior_state.rating += 1.0;
    EXPECT_EQ(calculate(participant, period, &drift, 1u, &successor),
              LAPLACE_STANDING_STATE_IDENTITY_MISMATCH);
}

}  // namespace
