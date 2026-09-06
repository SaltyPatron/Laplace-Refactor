#include "laplace/composition_execution.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr std::uint64_t RequestCount = 8U;

bool Zero(const laplace_digest256& value) {
    return std::all_of(
        std::begin(value.bytes), std::end(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_composition_frontier_execution_plan Scenario(
    const bool chain,
    const std::uint32_t cpu_slots) {
    laplace_framework_context context{};
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    context.resource_grant.cpu_slots = cpu_slots;
    context.resource_grant.io_slots = 2U;

    const std::array<laplace_composition_known_entity, 1> known{};
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    operands.reserve(static_cast<std::size_t>(RequestCount));
    requests.reserve(static_cast<std::size_t>(RequestCount));

    for (std::uint64_t index = 0U; index < RequestCount; ++index) {
        laplace_composition_operand operand{};
        operand.reference_index = chain && index != 0U ? index - 1U : 0U;
        operand.multiplicity = 1U;
        operand.reference_kind = chain && index != 0U
            ? LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT
            : LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        operands.push_back(operand);

        laplace_composition_request request{};
        request.first_operand = index;
        request.operand_count = 1U;
        request.recipe_version = 1U;
        requests.push_back(request);
    }

    laplace_composition_working_set_input input{};
    input.context = &context;
    input.known_entities = known.data();
    input.known_entity_count = known.size();
    input.operands = operands.data();
    input.operand_count = operands.size();
    input.requests = requests.data();
    input.request_count = requests.size();

    laplace_composition_frontier_execution_plan plan{};
    EXPECT_EQ(
        laplace_composition_frontier_execution_plan_build(&input, &plan),
        LAPLACE_COMPOSITION_OK);
    return plan;
}

TEST(CompositionFrontierExecution, WideFrontierUsesConservedCpuGrant) {
    const auto plan = Scenario(false, 4U);

    EXPECT_EQ(plan.request_count, RequestCount);
    EXPECT_EQ(plan.dependency_edge_count, 0U);
    EXPECT_EQ(plan.frontier_count, 1U);
    EXPECT_EQ(plan.dependency_depth, 0U);
    EXPECT_EQ(plan.minimum_frontier_width, RequestCount);
    EXPECT_EQ(plan.maximum_frontier_width, RequestCount);
    EXPECT_EQ(plan.minimum_outer_workers, 4U);
    EXPECT_EQ(plan.maximum_outer_workers, 4U);
    EXPECT_EQ(plan.total_planned_chunks, 4U);
    EXPECT_EQ(plan.grant_cpu_slots, 4U);
    EXPECT_EQ(plan.grant_io_slots, 2U);
    EXPECT_EQ(plan.version, LAPLACE_COMPOSITION_FRONTIER_EXECUTION_PLAN_VERSION);
    EXPECT_EQ(plan.status, LAPLACE_COMPOSITION_OK);
    EXPECT_FALSE(Zero(plan.plan_fingerprint));
}

TEST(CompositionFrontierExecution, DeepChainCannotClaimGrantWideParallelism) {
    const auto plan = Scenario(true, 8U);

    EXPECT_EQ(plan.request_count, RequestCount);
    EXPECT_EQ(plan.dependency_edge_count, RequestCount - 1U);
    EXPECT_EQ(plan.frontier_count, RequestCount);
    EXPECT_EQ(plan.dependency_depth, RequestCount - 1U);
    EXPECT_EQ(plan.minimum_frontier_width, 1U);
    EXPECT_EQ(plan.maximum_frontier_width, 1U);
    EXPECT_EQ(plan.minimum_outer_workers, 1U);
    EXPECT_EQ(plan.maximum_outer_workers, 1U);
    EXPECT_EQ(plan.total_planned_chunks, RequestCount);
    EXPECT_EQ(plan.grant_cpu_slots, 8U);
    EXPECT_FALSE(Zero(plan.plan_fingerprint));
}

TEST(CompositionFrontierExecution, GrantChangesOnlyPhysicalSchedulingProjection) {
    const auto scalar = Scenario(false, 1U);
    const auto parallel = Scenario(false, 4U);

    EXPECT_EQ(scalar.request_count, parallel.request_count);
    EXPECT_EQ(scalar.dependency_edge_count, parallel.dependency_edge_count);
    EXPECT_EQ(scalar.frontier_count, parallel.frontier_count);
    EXPECT_EQ(scalar.dependency_depth, parallel.dependency_depth);
    EXPECT_EQ(scalar.minimum_frontier_width, parallel.minimum_frontier_width);
    EXPECT_EQ(scalar.maximum_frontier_width, parallel.maximum_frontier_width);
    EXPECT_EQ(scalar.maximum_outer_workers, 1U);
    EXPECT_EQ(parallel.maximum_outer_workers, 4U);
    EXPECT_EQ(scalar.total_planned_chunks, 1U);
    EXPECT_EQ(parallel.total_planned_chunks, 4U);
    EXPECT_FALSE(Same(scalar.plan_fingerprint, parallel.plan_fingerprint));
}

}  // namespace
