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

laplace_execution_status ReversePrepare(
    void*,
    const laplace_execution_grant*,
    const laplace_execution_work_plan*) {
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status ReverseRun(
    void*,
    const laplace_execution_work_plan*,
    const laplace_execution_chunk* chunks,
    const std::size_t chunk_count,
    void* task_state,
    const laplace_execution_work_task_fn task,
    laplace_execution_chunk_result* results) {
    if (chunks == nullptr || task == nullptr || results == nullptr) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    for (std::size_t remaining = chunk_count; remaining != 0U; --remaining) {
        const std::size_t index = remaining - 1U;
        const auto status = task(
            task_state,
            &chunks[index],
            &results[index].result_fingerprint);
        if (status != LAPLACE_EXECUTION_OK) {
            results[index].state = LAPLACE_EXECUTION_CHUNK_FAILED;
            results[index].task_status = static_cast<std::uint32_t>(status);
            return status;
        }
        results[index].state = LAPLACE_EXECUTION_CHUNK_COMPLETE;
        results[index].task_status = LAPLACE_EXECUTION_OK;
    }
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status ReverseFinish(void*) {
    return LAPLACE_EXECUTION_OK;
}

void ReverseAbort(void*) {}

laplace_execution_runtime_provider_v1 ReverseProvider() {
    laplace_execution_runtime_provider_v1 provider{};
    Fill(provider.provider_fingerprint, 0xD1U);
    provider.prepare = ReversePrepare;
    provider.run = ReverseRun;
    provider.finish = ReverseFinish;
    provider.abort = ReverseAbort;
    provider.abi_major = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MINOR;
    provider.flags = LAPLACE_EXECUTION_KNOWN_PROVIDER_FLAGS;
    return provider;
}

void ExpectSameSemanticWorkingSet(
    const ScenarioResult& expected,
    const ScenarioResult& actual) {
    ASSERT_EQ(expected.results.size(), RequestCount);
    ASSERT_EQ(actual.results.size(), RequestCount);
    EXPECT_TRUE(SameDigest(
        expected.summary.receipt_id,
        actual.summary.receipt_id));
    EXPECT_TRUE(SameDigest(
        expected.summary.stream_fingerprint,
        actual.summary.stream_fingerprint));
    EXPECT_EQ(
        std::memcmp(
            expected.results.data(),
            actual.results.data(),
            expected.results.size() * sizeof(laplace_composition_result)),
        0);
    EXPECT_EQ(actual.summary.semantic_calculation_count, RequestCount);
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

    ExpectSameSemanticWorkingSet(automatic, explicit_serial);
    ASSERT_EQ(explicit_serial.receipts.size(), 1U);
    EXPECT_TRUE(SameDigest(
        explicit_serial.receipts[0].provider_fingerprint,
        serial_provider.provider_fingerprint));
    EXPECT_EQ(explicit_serial.receipts[0].completed_items, RequestCount);
}

TEST(
    CompositionFrontierRuntime,
    ReversedPhysicalChunkCompletionPreservesCanonicalSemanticOutput) {
    const auto reverse_provider = ReverseProvider();
    const auto automatic = Scenario(false, 4U);
    const auto reversed = Scenario(false, 4U, &reverse_provider);

    ASSERT_EQ(automatic.preflight.frontier_count, 1U);
    ASSERT_GT(automatic.preflight.total_planned_chunks, 1U);
    ExpectSameSemanticWorkingSet(automatic, reversed);
    ASSERT_EQ(reversed.receipts.size(), 1U);
    EXPECT_TRUE(SameDigest(
        reversed.receipts[0].provider_fingerprint,
        reverse_provider.provider_fingerprint));
    EXPECT_EQ(
        reversed.receipts[0].completed_chunks,
        reversed.receipts[0].plan.chunk_count);
    EXPECT_EQ(reversed.receipts[0].completed_items, RequestCount);
}

TEST(
    CompositionFrontierRuntime,
    WorkerGrantAndChunkPlanCannotAlterCanonicalSemanticIdentity) {
    const auto scalar = Scenario(false, 1U);
    ASSERT_EQ(scalar.preflight.maximum_outer_workers, 1U);
    ASSERT_EQ(scalar.preflight.total_planned_chunks, 1U);

    for (const std::uint32_t slots : {2U, 3U, 4U}) {
        const auto parallel = Scenario(false, slots);
        EXPECT_EQ(parallel.preflight.maximum_outer_workers, slots);
        EXPECT_GT(parallel.preflight.total_planned_chunks, 1U);
        ExpectSameSemanticWorkingSet(scalar, parallel);
    }
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