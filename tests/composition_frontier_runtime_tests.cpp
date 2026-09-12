#include "laplace/composition_execution.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "context_fixture.h"

namespace {

constexpr std::uint64_t RequestCount = 8U;

void Fill(laplace_digest256& digest, const std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(digest.bytes); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

bool SameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_composition_known_entity Atom(
    const std::uint32_t position,
    const laplace_point4d& point,
    const std::uint8_t physicality_seed) {
    laplace_composition_known_entity result{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(
            position, &result.entity_id, &result.identity_witness),
        LAPLACE_IDENTITY_OK);
    Fill(result.physicality_id, physicality_seed);
    result.centroid = point;
    result.atom = position;
    result.has_atom = 1U;
    return result;
}

struct ScenarioResult final {
    laplace_composition_frontier_execution_plan preflight{};
    laplace_composition_working_set_summary summary{};
    std::vector<laplace_composition_result> results;
    std::vector<laplace_execution_work_receipt> receipts;
};

ScenarioResult Scenario(
    const bool chain,
    const std::uint32_t cpu_slots,
    const laplace_execution_runtime_provider_v1* const execution_provider = nullptr) {
    auto context = laplace_test_context(0x31U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    context.resource_grant.cpu_slots = cpu_slots;
    context.resource_grant.io_slots = 2U;

    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x21U);
    Fill(calculation_recipe, 0x41U);

    const std::array<laplace_composition_known_entity, 2> known{{
        Atom(0x41U, laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x61U),
        Atom(0x42U, laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x81U)}};

    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    operands.reserve(static_cast<std::size_t>(RequestCount * 2U));
    requests.reserve(static_cast<std::size_t>(RequestCount));
    for (std::uint64_t index = 0U; index < RequestCount; ++index) {
        laplace_composition_operand first{};
        first.reference_index = chain && index != 0U ? index - 1U : 0U;
        first.multiplicity = 1U;
        first.reference_kind = chain && index != 0U
            ? LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT
            : LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        laplace_composition_operand second{};
        second.reference_index = 1U;
        second.multiplicity = 1U;
        second.reference_kind = LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        operands.push_back(first);
        operands.push_back(second);

        laplace_composition_request request{};
        request.first_operand = index * 2U;
        request.operand_count = 2U;
        request.source_ordinal = index + 1U;
        request.recipe_version = 1U;
        Fill(request.recipe_fingerprint, 0xA1U);
        Fill(request.geometry_epoch, 0xB1U);
        Fill(request.occurrence_context_fingerprint, 0xC1U);
        requests.push_back(request);
    }

    laplace_composition_working_set_input input{};
    input.context = &context;
    input.source_fingerprint = &source;
    input.calculation_recipe_fingerprint = &calculation_recipe;
    input.known_entities = known.data();
    input.known_entity_count = known.size();
    input.operands = operands.data();
    input.operand_count = operands.size();
    input.requests = requests.data();
    input.request_count = requests.size();

    ScenarioResult result{};
    EXPECT_EQ(
        laplace_composition_frontier_execution_plan_build(
            &input, &result.preflight),
        LAPLACE_COMPOSITION_OK);

    laplace_composition_working_set* working_set = nullptr;
    const auto create_status = execution_provider == nullptr
        ? laplace_composition_working_set_create(&input, &working_set)
        : laplace_composition_working_set_create_with_provider(
              &input, execution_provider, &working_set);
    EXPECT_EQ(create_status, LAPLACE_COMPOSITION_OK);
    if (create_status != LAPLACE_COMPOSITION_OK || working_set == nullptr) {
        return result;
    }
    EXPECT_EQ(
        laplace_composition_working_set_summary_get(
            working_set, &result.summary),
        LAPLACE_COMPOSITION_OK);

    std::size_t result_count = 0U;
    const auto* results =
        laplace_composition_working_set_results(working_set, &result_count);
    EXPECT_NE(results, nullptr);
    if (results != nullptr) {
        result.results.assign(results, results + result_count);
    }

    std::size_t receipt_count = 0U;
    const auto* receipts =
        laplace_composition_working_set_frontier_execution_receipts(
            working_set, &receipt_count);
    EXPECT_NE(receipts, nullptr);
    if (receipts != nullptr) {
        result.receipts.assign(receipts, receipts + receipt_count);
    }
    laplace_composition_working_set_destroy(&working_set);
    EXPECT_EQ(working_set, nullptr);
    return result;
}

std::uint64_t CompletedItems(
    const std::vector<laplace_execution_work_receipt>& receipts) {
    std::uint64_t result = 0U;
    for (const auto& receipt : receipts) {
        result += receipt.completed_items;
    }
    return result;
}

std::uint64_t CompletedChunks(
    const std::vector<laplace_execution_work_receipt>& receipts) {
    std::uint64_t result = 0U;
    for (const auto& receipt : receipts) {
        result += receipt.completed_chunks;
    }
    return result;
}

}  // namespace

TEST(
    CompositionFrontierRuntime,
    WideWorkingSetPublishesOneActualReceiptMatchingPreflightShape) {
    const auto result = Scenario(false, 4U);

    ASSERT_EQ(result.preflight.frontier_count, 1U);
    ASSERT_EQ(result.receipts.size(), result.preflight.frontier_count);
    EXPECT_EQ(result.summary.semantic_calculation_count, RequestCount);
    EXPECT_EQ(CompletedItems(result.receipts), RequestCount);
    EXPECT_EQ(
        CompletedChunks(result.receipts),
        result.preflight.total_planned_chunks);
    EXPECT_EQ(result.receipts[0].plan.outer_workers, 4U);
    EXPECT_EQ(
        result.receipts[0].plan.outer_workers,
        result.preflight.maximum_outer_workers);
    EXPECT_EQ(result.receipts[0].status, LAPLACE_EXECUTION_OK);
}

TEST(
    CompositionFrontierRuntime,
    DeepWorkingSetPublishesOneActualReceiptPerDependencyFrontier) {
    const auto result = Scenario(true, 8U);

    ASSERT_EQ(result.preflight.frontier_count, RequestCount);
    ASSERT_EQ(result.receipts.size(), result.preflight.frontier_count);
    EXPECT_EQ(result.summary.semantic_calculation_count, RequestCount);
    EXPECT_EQ(CompletedItems(result.receipts), RequestCount);
    EXPECT_EQ(
        CompletedChunks(result.receipts),
        result.preflight.total_planned_chunks);
    for (const auto& receipt : result.receipts) {
        EXPECT_EQ(receipt.completed_items, 1U);
        EXPECT_EQ(receipt.completed_chunks, 1U);
        EXPECT_EQ(receipt.plan.outer_workers, 1U);
        EXPECT_EQ(receipt.status, LAPLACE_EXECUTION_OK);
    }
}

TEST(
    CompositionFrontierRuntime,
    WideFrontierAccountsForConcurrentScratchResidency) {
    const auto wide = Scenario(false, 4U);
    const auto deep = Scenario(true, 4U);

    ASSERT_EQ(wide.preflight.maximum_frontier_width, RequestCount);
    ASSERT_EQ(deep.preflight.maximum_frontier_width, 1U);
    EXPECT_GT(
        wide.summary.estimated_peak_working_bytes,
        deep.summary.estimated_peak_working_bytes);
}

TEST(
    CompositionFrontierRuntime,
    RuntimeReceiptCardinalityCannotDivergeFromSharedPlanner) {
    const auto wide = Scenario(false, 4U);
    const auto deep = Scenario(true, 4U);

    EXPECT_EQ(wide.receipts.size(), wide.preflight.frontier_count);
    EXPECT_EQ(deep.receipts.size(), deep.preflight.frontier_count);
    EXPECT_NE(wide.receipts.size(), deep.receipts.size());
    EXPECT_EQ(wide.summary.semantic_calculation_count, RequestCount);
    EXPECT_EQ(deep.summary.semantic_calculation_count, RequestCount);
}

TEST(
    CompositionFrontierRuntime,
    CallerSelectedProviderIsReceiptedWithoutChangingSemanticOutput) {
    laplace_execution_runtime_provider_v1 serial_provider{};
    ASSERT_EQ(
        laplace_execution_serial_provider(&serial_provider),
        LAPLACE_EXECUTION_OK);
    Fill(serial_provider.provider_fingerprint, 0xE1U);

    const auto automatic = Scenario(false, 4U);
    const auto explicit_serial = Scenario(false, 4U, &serial_provider);

    ASSERT_EQ(automatic.results.size(), RequestCount);
    ASSERT_EQ(explicit_serial.results.size(), RequestCount);
    ASSERT_EQ(explicit_serial.receipts.size(), 1U);
    EXPECT_TRUE(SameDigest(
        automatic.summary.receipt_id,
        explicit_serial.summary.receipt_id));
    EXPECT_TRUE(SameDigest(
        automatic.summary.stream_fingerprint,
        explicit_serial.summary.stream_fingerprint));
    EXPECT_EQ(
        std::memcmp(
            automatic.results.data(),
            explicit_serial.results.data(),
            automatic.results.size() * sizeof(laplace_composition_result)),
        0);
    EXPECT_TRUE(SameDigest(
        explicit_serial.receipts[0].provider_fingerprint,
        serial_provider.provider_fingerprint));
    EXPECT_EQ(explicit_serial.receipts[0].completed_items, RequestCount);
    EXPECT_EQ(explicit_serial.summary.semantic_calculation_count, RequestCount);
}

TEST(
    CompositionFrontierRuntime,
    ProviderBoundCreateRejectsMissingProviderWithoutFallback) {
    laplace_composition_working_set* working_set =
        reinterpret_cast<laplace_composition_working_set*>(
            static_cast<std::uintptr_t>(1U));
    EXPECT_EQ(
        laplace_composition_working_set_create_with_provider(
            nullptr, nullptr, &working_set),
        LAPLACE_COMPOSITION_INVALID_ARGUMENT);
    EXPECT_EQ(working_set, nullptr);
}
