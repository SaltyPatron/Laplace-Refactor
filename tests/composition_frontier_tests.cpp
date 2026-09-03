#include "laplace/detail/composition_frontier.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

using laplace::composition::detail::BuildFrontierPlan;
using laplace::composition::detail::FrontierPlan;

laplace_composition_request Request(
    const std::uint64_t first_operand,
    const std::uint64_t operand_count) {
    laplace_composition_request request{};
    request.first_operand = first_operand;
    request.operand_count = operand_count;
    request.source_ordinal = first_operand + 1U;
    request.recipe_version = 1U;
    return request;
}

laplace_composition_operand Known(const std::uint64_t index) {
    return laplace_composition_operand{
        index, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U};
}

laplace_composition_operand Prior(const std::uint64_t index) {
    return laplace_composition_operand{
        index, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT, 0U};
}

template <std::size_t KnownCount, std::size_t OperandCount, std::size_t RequestCount>
laplace_composition_working_set_input Input(
    const std::array<laplace_composition_known_entity, KnownCount>& known,
    const std::array<laplace_composition_operand, OperandCount>& operands,
    const std::array<laplace_composition_request, RequestCount>& requests) {
    laplace_composition_working_set_input input{};
    input.known_entities = known.data();
    input.known_entity_count = known.size();
    input.operands = operands.data();
    input.operand_count = operands.size();
    input.requests = requests.data();
    input.request_count = requests.size();
    input.preferred_batch_bytes = 4096U;
    return input;
}

void ExpectSamePlan(const FrontierPlan& left, const FrontierPlan& right) {
    EXPECT_EQ(left.depth_by_request, right.depth_by_request);
    EXPECT_EQ(left.frontier_offsets, right.frontier_offsets);
    EXPECT_EQ(left.request_indices, right.request_indices);
    EXPECT_EQ(left.dependency_edge_count, right.dependency_edge_count);
    EXPECT_EQ(left.maximum_frontier_width, right.maximum_frontier_width);
    EXPECT_EQ(left.frontier_count(), right.frontier_count());
}

}  // namespace

TEST(CompositionFrontierPlan, WideIndependentRequestsShareOneFrontier) {
    const std::array<laplace_composition_known_entity, 4> known{};
    const std::array<laplace_composition_operand, 4> operands{{
        Known(0U), Known(1U), Known(2U), Known(3U)}};
    const std::array<laplace_composition_request, 4> requests{{
        Request(0U, 1U), Request(1U, 1U), Request(2U, 1U), Request(3U, 1U)}};
    const auto input = Input(known, operands, requests);

    FrontierPlan plan;
    ASSERT_EQ(BuildFrontierPlan(input, plan), LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(plan.frontier_count(), 1U);
    EXPECT_EQ(plan.maximum_frontier_width, 4U);
    EXPECT_EQ(plan.dependency_edge_count, 0U);
    EXPECT_EQ(plan.depth_by_request,
              (std::vector<std::uint64_t>{0U, 0U, 0U, 0U}));
    EXPECT_EQ(plan.frontier_offsets,
              (std::vector<std::uint64_t>{0U, 4U}));
    EXPECT_EQ(plan.request_indices,
              (std::vector<std::uint64_t>{0U, 1U, 2U, 3U}));
}

TEST(CompositionFrontierPlan, DeepChainHasOneRequestPerDependencyFrontier) {
    const std::array<laplace_composition_known_entity, 1> known{};
    const std::array<laplace_composition_operand, 4> operands{{
        Known(0U), Prior(0U), Prior(1U), Prior(2U)}};
    const std::array<laplace_composition_request, 4> requests{{
        Request(0U, 1U), Request(1U, 1U), Request(2U, 1U), Request(3U, 1U)}};
    const auto input = Input(known, operands, requests);

    FrontierPlan plan;
    ASSERT_EQ(BuildFrontierPlan(input, plan), LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(plan.frontier_count(), 4U);
    EXPECT_EQ(plan.maximum_frontier_width, 1U);
    EXPECT_EQ(plan.dependency_edge_count, 3U);
    EXPECT_EQ(plan.depth_by_request,
              (std::vector<std::uint64_t>{0U, 1U, 2U, 3U}));
    EXPECT_EQ(plan.frontier_offsets,
              (std::vector<std::uint64_t>{0U, 1U, 2U, 3U, 4U}));
    EXPECT_EQ(plan.request_indices,
              (std::vector<std::uint64_t>{0U, 1U, 2U, 3U}));
}

TEST(CompositionFrontierPlan, MixedDagPreservesStableCanonicalOrderWithinFrontiers) {
    const std::array<laplace_composition_known_entity, 2> known{};
    const std::array<laplace_composition_operand, 7> operands{{
        Known(0U),
        Known(1U),
        Prior(0U), Prior(1U),
        Prior(0U),
        Prior(2U), Prior(3U)}};
    const std::array<laplace_composition_request, 5> requests{{
        Request(0U, 1U),
        Request(1U, 1U),
        Request(2U, 2U),
        Request(4U, 1U),
        Request(5U, 2U)}};
    const auto input = Input(known, operands, requests);

    FrontierPlan plan;
    ASSERT_EQ(BuildFrontierPlan(input, plan), LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(plan.frontier_count(), 3U);
    EXPECT_EQ(plan.maximum_frontier_width, 2U);
    EXPECT_EQ(plan.dependency_edge_count, 5U);
    EXPECT_EQ(plan.depth_by_request,
              (std::vector<std::uint64_t>{0U, 0U, 1U, 1U, 2U}));
    EXPECT_EQ(plan.frontier_offsets,
              (std::vector<std::uint64_t>{0U, 2U, 4U, 5U}));
    EXPECT_EQ(plan.request_indices,
              (std::vector<std::uint64_t>{0U, 1U, 2U, 3U, 4U}));
}

TEST(CompositionFrontierPlan, PhysicalPlanKnobsCannotRedefineDependencyFrontiers) {
    const std::array<laplace_composition_known_entity, 2> known{};
    const std::array<laplace_composition_operand, 4> operands{{
        Known(0U), Known(1U), Prior(0U), Prior(1U)}};
    const std::array<laplace_composition_request, 3> requests{{
        Request(0U, 1U), Request(1U, 1U), Request(2U, 2U)}};

    auto first = Input(known, operands, requests);
    laplace_framework_context first_context{};
    first_context.resource_grant = {UINT64_C(64) * 1024U * 1024U, 1U, 1U};
    first.context = &first_context;
    first.preferred_batch_bytes = 1U;

    auto second = first;
    laplace_framework_context second_context{};
    second_context.resource_grant = {UINT64_C(8) * 1024U * 1024U * 1024U, 64U, 16U};
    second.context = &second_context;
    second.preferred_batch_bytes = UINT64_C(64) * 1024U * 1024U;

    FrontierPlan scalar_shape;
    FrontierPlan wide_shape;
    ASSERT_EQ(BuildFrontierPlan(first, scalar_shape), LAPLACE_COMPOSITION_OK);
    ASSERT_EQ(BuildFrontierPlan(second, wide_shape), LAPLACE_COMPOSITION_OK);
    ExpectSamePlan(scalar_shape, wide_shape);
    EXPECT_EQ(scalar_shape.frontier_count(), 2U);
    EXPECT_EQ(scalar_shape.maximum_frontier_width, 2U);
}

TEST(CompositionFrontierPlan, RejectsForwardSelfUnknownAndOutOfRangeReferences) {
    const std::array<laplace_composition_known_entity, 1> known{};

    {
        const std::array<laplace_composition_operand, 1> operands{{Prior(0U)}};
        const std::array<laplace_composition_request, 1> requests{{Request(0U, 1U)}};
        FrontierPlan plan;
        EXPECT_EQ(BuildFrontierPlan(Input(known, operands, requests), plan),
                  LAPLACE_COMPOSITION_REFERENCE_INVALID);
        EXPECT_TRUE(plan.depth_by_request.empty());
    }

    {
        const std::array<laplace_composition_operand, 1> operands{{Known(1U)}};
        const std::array<laplace_composition_request, 1> requests{{Request(0U, 1U)}};
        FrontierPlan plan;
        EXPECT_EQ(BuildFrontierPlan(Input(known, operands, requests), plan),
                  LAPLACE_COMPOSITION_REFERENCE_INVALID);
    }

    {
        auto unknown = Known(0U);
        unknown.reference_kind = 999U;
        const std::array<laplace_composition_operand, 1> operands{{unknown}};
        const std::array<laplace_composition_request, 1> requests{{Request(0U, 1U)}};
        FrontierPlan plan;
        EXPECT_EQ(BuildFrontierPlan(Input(known, operands, requests), plan),
                  LAPLACE_COMPOSITION_REFERENCE_INVALID);
    }

    {
        const std::array<laplace_composition_operand, 1> operands{{Known(0U)}};
        const std::array<laplace_composition_request, 1> requests{{Request(1U, 1U)}};
        FrontierPlan plan;
        EXPECT_EQ(BuildFrontierPlan(Input(known, operands, requests), plan),
                  LAPLACE_COMPOSITION_REFERENCE_INVALID);
    }
}
