#include "laplace/cognition_guidance.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_guidance_header Header() {
    laplace_cognition_guidance_header header{};
    header.program_id = Digest(1U);
    header.goal_id = Digest(2U);
    header.bindings_fingerprint = Digest(3U);
    header.scope_fingerprint = Digest(4U);
    header.world_id = Digest(5U);
    header.time_fingerprint = Digest(6U);
    header.context_fingerprint = Digest(7U);
    header.evidence_epoch = Digest(8U);
    header.authority_id = Digest(9U);
    header.result_contract_fingerprint = Digest(10U);
    header.version = LAPLACE_COGNITION_GUIDANCE_VERSION;
    return header;
}

laplace_cognition_obligation Obligation(
    std::uint8_t id,
    std::uint32_t kind,
    std::uint32_t disposition,
    std::uint32_t flags = LAPLACE_COGNITION_OBLIGATION_REQUIRED) {
    const auto header = Header();
    laplace_cognition_obligation obligation{};
    obligation.obligation_id = Digest(id);
    obligation.binding_fingerprint = Digest(static_cast<std::uint8_t>(40U + id));
    obligation.world_id = header.world_id;
    obligation.time_fingerprint = header.time_fingerprint;
    obligation.context_fingerprint = header.context_fingerprint;
    obligation.evidence_boundary = Digest(20U);
    obligation.authority_id = header.authority_id;
    obligation.result_contract_fingerprint = header.result_contract_fingerprint;
    obligation.kind = kind;
    obligation.disposition = disposition;
    obligation.flags = flags;
    if (disposition == LAPLACE_COGNITION_OBLIGATION_SATISFIED) {
        obligation.value_id = Digest(static_cast<std::uint8_t>(80U + id));
        obligation.resolution_receipt_id = Digest(static_cast<std::uint8_t>(100U + id));
    } else if (disposition != LAPLACE_COGNITION_OBLIGATION_OPEN) {
        obligation.resolution_receipt_id = Digest(static_cast<std::uint8_t>(100U + id));
    }
    return obligation;
}

class StateHandle {
public:
    StateHandle() = default;
    StateHandle(const StateHandle&) = delete;
    StateHandle& operator=(const StateHandle&) = delete;
    StateHandle(StateHandle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    StateHandle& operator=(StateHandle&&) = delete;
    ~StateHandle() { laplace_cognition_guidance_state_destroy(&value); }
    laplace_cognition_guidance_state* value{};
};

StateHandle MakeState(const std::vector<laplace_cognition_obligation>& obligations) {
    StateHandle state;
    const auto header = Header();
    EXPECT_EQ(laplace_cognition_guidance_state_create(
                  &header, obligations.data(), obligations.size(), &state.value),
              LAPLACE_COGNITION_GUIDANCE_OK);
    return state;
}

laplace_cognition_guidance_operation Operation(
    std::uint8_t id,
    const laplace_digest256& target,
    std::uint32_t kind,
    std::uint64_t reduction,
    std::uint64_t information,
    std::uint64_t cost,
    std::uint64_t novelty,
    std::uint32_t flags) {
    laplace_cognition_guidance_operation operation{};
    operation.operation_id = Digest(id);
    operation.target_obligation_id = target;
    operation.operands_fingerprint = Digest(static_cast<std::uint8_t>(id + 20U));
    operation.preconditions_fingerprint = Digest(static_cast<std::uint8_t>(id + 40U));
    operation.predicted_effect_fingerprint = Digest(static_cast<std::uint8_t>(id + 60U));
    operation.authority_id = Header().authority_id;
    operation.receipt_contract_fingerprint = Digest(static_cast<std::uint8_t>(id + 80U));
    operation.expected_obligation_reduction = reduction;
    operation.information_value = information;
    operation.resource_cost = cost;
    operation.novelty = novelty;
    operation.kind = kind;
    operation.flags = flags;
    return operation;
}

TEST(GuidanceState, RetainsTypedObligationsInCanonicalIdentity) {
    const std::vector first{
        Obligation(30U, 1U, LAPLACE_COGNITION_OBLIGATION_OPEN),
        Obligation(31U, 5U, LAPLACE_COGNITION_OBLIGATION_OPEN)};
    const std::vector second{
        Obligation(30U, 7U, LAPLACE_COGNITION_OBLIGATION_OPEN),
        Obligation(31U, 11U, LAPLACE_COGNITION_OBLIGATION_OPEN)};
    auto left = MakeState(first);
    auto right = MakeState(second);
    laplace_digest256 left_id{};
    laplace_digest256 right_id{};
    ASSERT_EQ(laplace_cognition_guidance_state_identify(left.value, &left_id),
              LAPLACE_COGNITION_GUIDANCE_OK);
    ASSERT_EQ(laplace_cognition_guidance_state_identify(right.value, &right_id),
              LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_FALSE(Same(left_id, right_id));
}

TEST(GuidanceState, RequiresSemanticCompletion) {
    const std::vector obligations{
        Obligation(30U, 2U, LAPLACE_COGNITION_OBLIGATION_SATISFIED),
        Obligation(31U, 5U, LAPLACE_COGNITION_OBLIGATION_OPEN)};
    auto state = MakeState(obligations);
    std::uint32_t completion = 0U;
    std::uint64_t remaining = 0U;
    ASSERT_EQ(laplace_cognition_guidance_completion(
                  state.value, &completion, &remaining),
              LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_EQ(completion, LAPLACE_COGNITION_COMPLETION_INCOMPLETE);
    EXPECT_EQ(remaining, 1U);
}

TEST(GuidanceState, ProjectsFoldsAndTransformsOneLayer) {
    const std::vector obligations{
        Obligation(30U, 2U, LAPLACE_COGNITION_OBLIGATION_SATISFIED),
        Obligation(31U, 5U, LAPLACE_COGNITION_OBLIGATION_OPEN),
        Obligation(32U, 7U, LAPLACE_COGNITION_OBLIGATION_OPEN)};
    auto state = MakeState(obligations);
    std::array<laplace_cognition_query_projection, 3> projected{};
    std::size_t projected_count = 0U;
    ASSERT_EQ(laplace_cognition_guidance_project_queries(
                  state.value, projected.data(), projected.size(), &projected_count),
              LAPLACE_COGNITION_GUIDANCE_OK);
    ASSERT_EQ(projected_count, 2U);
    EXPECT_NE(projected[0].query_kind, projected[1].query_kind);

    laplace_digest256 prior_id{};
    ASSERT_EQ(laplace_cognition_guidance_state_identify(state.value, &prior_id),
              LAPLACE_COGNITION_GUIDANCE_OK);
    const laplace_cognition_resolution resolution{
        Digest(31U), Digest(140U), Digest(141U),
        LAPLACE_COGNITION_OBLIGATION_SATISFIED, 0U};
    StateHandle next;
    laplace_cognition_guidance_transition_receipt transition{};
    ASSERT_EQ(laplace_cognition_guidance_apply_resolutions(
                  state.value, &resolution, 1U, &next.value, &transition),
              LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_TRUE(Same(transition.prior_state_id, prior_id));
    EXPECT_FALSE(Same(transition.prior_state_id, transition.next_state_id));
    EXPECT_EQ(transition.remaining_open_count, 1U);

    projected_count = 0U;
    ASSERT_EQ(laplace_cognition_guidance_project_queries(
                  next.value, projected.data(), projected.size(), &projected_count),
              LAPLACE_COGNITION_GUIDANCE_OK);
    ASSERT_EQ(projected_count, 1U);
    EXPECT_EQ(projected[0].query_kind, 7U);
}

TEST(GuidanceEvidence, CollapsesDependenceRootsBeforeNextLayer) {
    std::vector<laplace_cognition_evidence_observation> observations;
    auto observation = [](std::uint8_t id, std::uint8_t root, std::int32_t polarity) {
        laplace_cognition_evidence_observation value{};
        value.observation_id = Digest(id);
        value.proposition_id = Digest(150U);
        value.dependence_root_id = Digest(root);
        value.source_id = Digest(static_cast<std::uint8_t>(id + 30U));
        value.context_fingerprint = Header().context_fingerprint;
        value.evidence_epoch = Header().evidence_epoch;
        value.uncertainty_fingerprint = Digest(static_cast<std::uint8_t>(id + 60U));
        value.standing_fingerprint = Digest(static_cast<std::uint8_t>(id + 90U));
        value.polarity = polarity;
        return value;
    };
    observations.push_back(observation(50U, 70U, 1));
    observations.push_back(observation(51U, 70U, 1));
    observations.push_back(observation(52U, 70U, 1));
    observations.push_back(observation(53U, 71U, 1));
    observations.push_back(observation(54U, 72U, -1));
    observations.push_back(observation(55U, 72U, 1));
    laplace_cognition_evidence_fold_summary summary{};
    ASSERT_EQ(laplace_cognition_evidence_fold(
                  observations.data(), observations.size(), &summary),
              LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_EQ(summary.raw_observation_count, 6U);
    EXPECT_EQ(summary.independent_root_count, 3U);
    EXPECT_EQ(summary.positive_root_count, 3U);
    EXPECT_EQ(summary.negative_root_count, 1U);
    EXPECT_EQ(summary.contradictory_root_count, 1U);
}

TEST(GuidanceScheduler, RanksRelevantReductionBeforeRawNovelty) {
    const std::vector obligations{
        Obligation(31U, 5U, LAPLACE_COGNITION_OBLIGATION_OPEN)};
    auto state = MakeState(obligations);
    const std::array operations{
        Operation(
            80U, Digest(31U), LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH,
            10U, 8U, 12U, 1U, LAPLACE_COGNITION_OPERATION_INFORMATION_SEEKING),
        Operation(
            81U, Digest(31U), LAPLACE_COGNITION_OPERATION_RESEARCH,
            1U, 9U, 1U, 1000U, LAPLACE_COGNITION_OPERATION_INFORMATION_SEEKING)};
    laplace_cognition_guidance_decision decision{};
    ASSERT_EQ(laplace_cognition_guidance_select_operation(
                  state.value, operations.data(), operations.size(), &decision),
              LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_TRUE(Same(decision.selected_operation_id, Digest(80U)));
    EXPECT_EQ(decision.selected_expected_obligation_reduction, 10U);
}

TEST(GuidanceScheduler, TerminalActRequiresCompletedObligations) {
    const std::vector obligations{
        Obligation(31U, 5U, LAPLACE_COGNITION_OBLIGATION_OPEN,
                   LAPLACE_COGNITION_OBLIGATION_REQUIRED |
                   LAPLACE_COGNITION_OBLIGATION_ALLOW_TYPED_UNRESOLVED)};
    auto state = MakeState(obligations);
    const auto answer = Operation(
        90U, laplace_digest256{}, LAPLACE_COGNITION_OPERATION_ANSWER,
        0U, 0U, 1U, 0U, LAPLACE_COGNITION_OPERATION_TERMINAL_ACT);
    laplace_cognition_guidance_decision decision{};
    EXPECT_EQ(laplace_cognition_guidance_select_operation(
                  state.value, &answer, 1U, &decision),
              LAPLACE_COGNITION_GUIDANCE_NO_OPERATION);

    const laplace_cognition_resolution resolution{
        Digest(31U), laplace_digest256{}, Digest(142U),
        LAPLACE_COGNITION_OBLIGATION_UNSUPPORTED, 0U};
    StateHandle next;
    laplace_cognition_guidance_transition_receipt transition{};
    ASSERT_EQ(laplace_cognition_guidance_apply_resolutions(
                  state.value, &resolution, 1U, &next.value, &transition),
              LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_EQ(transition.completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    ASSERT_EQ(laplace_cognition_guidance_select_operation(
                  next.value, &answer, 1U, &decision),
              LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_EQ(decision.selected_kind, LAPLACE_COGNITION_OPERATION_ANSWER);
}

}  // namespace
