#include "laplace/execution.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

static_assert(sizeof(laplace_execution_chunk) == 24U);
static_assert(sizeof(laplace_execution_chunk_result) == 40U);
static_assert(sizeof(laplace_execution_oneapi_provider_state) == 64U);
static_assert(sizeof(laplace_execution_work_receipt) == 256U);

struct SyntheticTopology final {
    std::array<laplace_execution_processor, 8> processors{};
    std::array<laplace_execution_cache, 3> caches{};
    std::array<std::uint32_t, 16> cache_processor_ids{};
    std::array<laplace_execution_memory_domain, 2> domains{};
    laplace_execution_topology topology{};

    SyntheticTopology() {
        for (std::uint32_t index = 0U; index < processors.size(); ++index) {
            processors[index].logical_id = index;
            processors[index].package_id = index < 4U ? 0U : 1U;
            processors[index].core_id = (index % 4U) / 2U;
            processors[index].memory_domain_id = index < 4U ? 0U : 1U;
            processors[index].maximum_frequency_hz =
                index < 4U ? UINT64_C(5200000000) : UINT64_C(3900000000);
            processors[index].core_kind = index < 4U
                ? LAPLACE_EXECUTION_CORE_PERFORMANCE
                : LAPLACE_EXECUTION_CORE_EFFICIENCY;
            processors[index].flags = LAPLACE_EXECUTION_PROCESSOR_ONLINE |
                (index == 3U || index == 7U ? 0U : LAPLACE_EXECUTION_PROCESSOR_ALLOWED);
        }
        for (std::uint32_t index = 0U; index < 4U; ++index) {
            cache_processor_ids[index] = index;
            cache_processor_ids[4U + index] = 4U + index;
            cache_processor_ids[8U + index] = index;
            cache_processor_ids[12U + index] = 4U + index;
        }
        caches[0] = {
            UINT64_C(2097152), 2U, LAPLACE_EXECUTION_CACHE_UNIFIED, 64U, 0U, 4U, 0U};
        caches[1] = {
            UINT64_C(2097152), 2U, LAPLACE_EXECUTION_CACHE_UNIFIED, 64U, 4U, 4U, 0U};
        caches[2] = {
            UINT64_C(33554432), 3U, LAPLACE_EXECUTION_CACHE_UNIFIED, 64U, 8U, 8U, 0U};
        domains[0] = {0U, 4U, UINT64_C(8589934592), UINT64_C(6442450944)};
        domains[1] = {1U, 4U, UINT64_C(8589934592), UINT64_C(6442450944)};
        topology.processors = processors.data();
        topology.caches = caches.data();
        topology.cache_processor_ids = cache_processor_ids.data();
        topology.memory_domains = domains.data();
        topology.processor_count = static_cast<std::uint32_t>(processors.size());
        topology.processor_capacity = topology.processor_count;
        topology.cache_count = static_cast<std::uint32_t>(caches.size());
        topology.cache_capacity = topology.cache_count;
        topology.cache_processor_id_count =
            static_cast<std::uint32_t>(cache_processor_ids.size());
        topology.cache_processor_id_capacity = topology.cache_processor_id_count;
        topology.memory_domain_count = static_cast<std::uint32_t>(domains.size());
        topology.memory_domain_capacity = topology.memory_domain_count;
        topology.total_memory_bytes = UINT64_C(17179869184);
        topology.usable_memory_bytes = UINT64_C(12884901888);
        topology.page_bytes = 4096U;
        topology.isa_flags = LAPLACE_EXECUTION_ISA_X86_AVX2 |
            LAPLACE_EXECUTION_ISA_X86_FMA;
        topology.flags = LAPLACE_EXECUTION_TOPOLOGY_AFFINITY_CONSTRAINED |
            LAPLACE_EXECUTION_TOPOLOGY_HYBRID_CORES;
    }
};

struct WorkTaskState final {
    std::vector<laplace_execution_chunk> chunks;
    std::uint64_t fail_chunk{UINT64_MAX};
};

laplace_execution_status FingerprintChunk(
    void* const raw_state,
    const laplace_execution_chunk* const chunk,
    laplace_digest256* const result) {
    auto& state = *static_cast<WorkTaskState*>(raw_state);
    state.chunks.push_back(*chunk);
    if (chunk->chunk_index == state.fail_chunk) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    std::memset(
        result->bytes,
        static_cast<int>((chunk->chunk_index % UINT64_C(251)) + UINT64_C(1)),
        sizeof(result->bytes));
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status ZeroFingerprintChunk(
    void*,
    const laplace_execution_chunk*,
    laplace_digest256* const result) {
    std::memset(result, 0, sizeof(*result));
    return LAPLACE_EXECUTION_OK;
}

struct ReverseProviderState final {
    std::uint32_t prepare_calls{};
    std::uint32_t finish_calls{};
    std::uint32_t abort_calls{};
};

laplace_execution_status ReversePrepare(
    void* const raw_state,
    const laplace_execution_grant*,
    const laplace_execution_work_plan*) {
    ++static_cast<ReverseProviderState*>(raw_state)->prepare_calls;
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status ReverseRun(
    void*,
    const laplace_execution_work_plan*,
    const laplace_execution_chunk* const chunks,
    const std::size_t chunk_count,
    void* const task_state,
    const laplace_execution_work_task_fn task,
    laplace_execution_chunk_result* const results) {
    for (std::size_t offset = 0U; offset < chunk_count; ++offset) {
        const std::size_t index = chunk_count - offset - 1U;
        const auto status = task(
            task_state, &chunks[index], &results[index].result_fingerprint);
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

laplace_execution_status ReverseFinish(void* const raw_state) {
    ++static_cast<ReverseProviderState*>(raw_state)->finish_calls;
    return LAPLACE_EXECUTION_OK;
}

void ReverseAbort(void* const raw_state) {
    ++static_cast<ReverseProviderState*>(raw_state)->abort_calls;
}

laplace_execution_runtime_provider_v1 ReverseProvider(
    ReverseProviderState& state) {
    laplace_execution_runtime_provider_v1 provider{};
    provider.state = &state;
    std::memset(provider.provider_fingerprint.bytes, 0x5a,
                sizeof(provider.provider_fingerprint.bytes));
    provider.prepare = ReversePrepare;
    provider.run = ReverseRun;
    provider.finish = ReverseFinish;
    provider.abort = ReverseAbort;
    provider.abi_major = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MINOR;
    return provider;
}

laplace_execution_status OmitLastRun(
    void*,
    const laplace_execution_work_plan*,
    const laplace_execution_chunk* const chunks,
    const std::size_t chunk_count,
    void* const task_state,
    const laplace_execution_work_task_fn task,
    laplace_execution_chunk_result* const results) {
    for (std::size_t index = 0U; index + 1U < chunk_count; ++index) {
        const auto status = task(
            task_state, &chunks[index], &results[index].result_fingerprint);
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

TEST(ExecutionTopology, AcceptsTypedNumaCacheHybridAndAffinityState) {
    SyntheticTopology fixture;
    EXPECT_EQ(laplace_execution_topology_validate(&fixture.topology), LAPLACE_EXECUTION_OK);
}

TEST(ExecutionTopology, RejectsDuplicateProcessorAndIncorrectDomainAccounting) {
    SyntheticTopology fixture;
    fixture.processors[1].logical_id = fixture.processors[0].logical_id;
    EXPECT_EQ(
        laplace_execution_topology_validate(&fixture.topology),
        LAPLACE_EXECUTION_TOPOLOGY_INVALID);

    SyntheticTopology domain_fixture;
    domain_fixture.domains[1].processor_count = 3U;
    EXPECT_EQ(
        laplace_execution_topology_validate(&domain_fixture.topology),
        LAPLACE_EXECUTION_TOPOLOGY_INVALID);

    SyntheticTopology flag_fixture;
    flag_fixture.topology.isa_flags |= UINT64_C(1) << 63U;
    EXPECT_EQ(
        laplace_execution_topology_validate(&flag_fixture.topology),
        LAPLACE_EXECUTION_TOPOLOGY_INVALID);
}

TEST(ExecutionTopology, HostObservationPublishesAValidatedCallerOwnedSnapshot) {
    laplace_execution_topology_size required{};
    ASSERT_EQ(laplace_execution_topology_measure_host(&required), LAPLACE_EXECUTION_OK);
    ASSERT_GT(required.processor_count, 0U);
    ASSERT_GT(required.memory_domain_count, 0U);

    std::vector<laplace_execution_processor> processors(required.processor_count);
    std::vector<laplace_execution_cache> caches(required.cache_count);
    std::vector<std::uint32_t> cache_members(required.cache_processor_id_count);
    std::vector<laplace_execution_memory_domain> domains(required.memory_domain_count);
    laplace_execution_topology topology{};
    topology.processors = processors.data();
    topology.processor_capacity = required.processor_count;
    topology.caches = caches.data();
    topology.cache_capacity = required.cache_count;
    topology.cache_processor_ids = cache_members.data();
    topology.cache_processor_id_capacity = required.cache_processor_id_count;
    topology.memory_domains = domains.data();
    topology.memory_domain_capacity = required.memory_domain_count;
    ASSERT_EQ(laplace_execution_topology_observe_host(&topology), LAPLACE_EXECUTION_OK);
    EXPECT_EQ(laplace_execution_topology_validate(&topology), LAPLACE_EXECUTION_OK);
    EXPECT_GT(topology.usable_memory_bytes, 0U);
    EXPECT_GT(topology.page_bytes, 0U);
    EXPECT_TRUE(std::any_of(
        processors.begin(), processors.end(), [](const laplace_execution_processor& processor) {
            return (processor.flags & LAPLACE_EXECUTION_PROCESSOR_ALLOWED) != 0U;
        }));
}

TEST(ExecutionGrant, RootSubtractsResourcesOwnedByOtherRuntimeComponents) {
    SyntheticTopology fixture;
    const laplace_execution_external_ownership ownership{
        UINT64_C(2147483648), 2U, 3U};
    laplace_execution_grant grant{};
    ASSERT_EQ(
        laplace_execution_root_grant(&fixture.topology, &ownership, &grant),
        LAPLACE_EXECUTION_OK);
    EXPECT_EQ(grant.cpu_slots, 4U);
    EXPECT_EQ(grant.memory_bytes, UINT64_C(10737418240));
    EXPECT_EQ(grant.io_slots, 3U);
}

TEST(ExecutionGrant, WeightedAndNestedPartitionsConserveEveryParentResource) {
    const laplace_execution_grant parent{1200U, 12U, 6U};
    const std::array<laplace_execution_partition_request, 3> requests{{
        {100U, 1U, 1U, 1U, 1U, 0U, 0U},
        {200U, 2U, 2U, 1U, 2U, 1U, 0U},
        {100U, 1U, 1U, 2U, 1U, 1U, 0U},
    }};
    std::array<laplace_execution_grant, 3> children{};
    ASSERT_EQ(
        laplace_execution_partition_grant(
            &parent, requests.data(), requests.size(), children.data()),
        LAPLACE_EXECUTION_OK);
    std::uint64_t memory_sum{};
    std::uint32_t cpu_sum{};
    std::uint32_t io_sum{};
    for (const auto& child : children) {
        memory_sum += child.memory_bytes;
        cpu_sum += child.cpu_slots;
        io_sum += child.io_slots;
    }
    EXPECT_EQ(memory_sum, parent.memory_bytes);
    EXPECT_EQ(cpu_sum, parent.cpu_slots);
    EXPECT_EQ(io_sum, parent.io_slots);

    const std::array<laplace_execution_partition_request, 2> nested_requests{{
        {0U, 1U, 1U, 1U, 0U, 0U, 0U},
        {0U, 1U, 1U, 1U, 0U, 0U, 0U},
    }};
    std::array<laplace_execution_grant, 2> nested{};
    ASSERT_EQ(
        laplace_execution_partition_grant(
            &children[1], nested_requests.data(), nested_requests.size(), nested.data()),
        LAPLACE_EXECUTION_OK);
    EXPECT_EQ(nested[0].memory_bytes + nested[1].memory_bytes, children[1].memory_bytes);
    EXPECT_EQ(nested[0].cpu_slots + nested[1].cpu_slots, children[1].cpu_slots);
    EXPECT_EQ(nested[0].io_slots + nested[1].io_slots, children[1].io_slots);
}

TEST(ExecutionGrant, InsufficientMinimumCannotMutatePublishedChildren) {
    const laplace_execution_grant parent{100U, 2U, 1U};
    const std::array<laplace_execution_partition_request, 2> requests{{
        {80U, 1U, 1U, 1U, 2U, 1U, 0U},
        {80U, 1U, 1U, 1U, 2U, 1U, 0U},
    }};
    std::array<laplace_execution_grant, 2> children{{
        {17U, 17U, 17U},
        {17U, 17U, 17U},
    }};
    const auto before = children;
    EXPECT_EQ(
        laplace_execution_partition_grant(
            &parent, requests.data(), requests.size(), children.data()),
        LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT);
    for (std::size_t index = 0U; index < children.size(); ++index) {
        EXPECT_EQ(children[index].memory_bytes, before[index].memory_bytes);
        EXPECT_EQ(children[index].cpu_slots, before[index].cpu_slots);
        EXPECT_EQ(children[index].io_slots, before[index].io_slots);
    }

    const laplace_execution_partition_request no_weight{
        100U, 0U, 0U, 0U, 2U, 0U, 0U};
    laplace_execution_grant unchanged{19U, 19U, 19U};
    EXPECT_EQ(
        laplace_execution_partition_grant(&parent, &no_weight, 1U, &unchanged),
        LAPLACE_EXECUTION_INVALID_ARGUMENT);
    EXPECT_EQ(unchanged.memory_bytes, 19U);
    EXPECT_EQ(unchanged.cpu_slots, 19U);
    EXPECT_EQ(unchanged.io_slots, 19U);
}

TEST(ExecutionPlan, CouplesOuterWorkersInnerThreadsChunksAndPeakMemory) {
    const laplace_execution_grant grant{1000U, 8U, 2U};
    const laplace_execution_work_request request{
        1000U, 100U, 10U, 10U, 8U, 2U, 1U, 0U};
    laplace_execution_work_plan plan{};
    ASSERT_EQ(laplace_execution_plan_work(&grant, &request, &plan), LAPLACE_EXECUTION_OK);
    EXPECT_EQ(plan.outer_workers, 4U);
    EXPECT_EQ(plan.inner_threads_per_worker, 2U);
    EXPECT_EQ(plan.chunk_items, 22U);
    EXPECT_EQ(plan.chunk_count, 46U);
    EXPECT_EQ(plan.peak_memory_bytes, 980U);
    EXPECT_LE(plan.outer_workers * plan.inner_threads_per_worker, grant.cpu_slots);
    EXPECT_LE(plan.peak_memory_bytes, grant.memory_bytes);
}

TEST(ExecutionPlan, ResourceFailureCannotMutatePriorPlan) {
    const laplace_execution_grant grant{99U, 1U, 0U};
    const laplace_execution_work_request request{
        100U, 100U, 10U, 1U, 1U, 1U, 0U, 0U};
    laplace_execution_work_plan plan{7U, 7U, 7U, 7U, 7U, 7U, 0U};
    const auto before = plan;
    EXPECT_EQ(
        laplace_execution_plan_work(&grant, &request, &plan),
        LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT);
    EXPECT_EQ(plan.chunk_items, before.chunk_items);
    EXPECT_EQ(plan.chunk_count, before.chunk_count);
    EXPECT_EQ(plan.peak_memory_bytes, before.peak_memory_bytes);
    EXPECT_EQ(plan.outer_workers, before.outer_workers);
}

TEST(ExecutionRuntime, SerialAndReverseProvidersShareExactChunksAndResults) {
    const laplace_execution_grant grant{4096U, 4U, 1U};
    const laplace_execution_work_request request{
        23U, 256U, 16U, 2U, 4U, 1U, 0U, 0U};
    laplace_execution_runtime_provider_v1 serial{};
    ASSERT_EQ(
        laplace_execution_serial_provider(&serial), LAPLACE_EXECUTION_OK);
    WorkTaskState serial_state;
    laplace_execution_work_receipt serial_receipt{};
    ASSERT_EQ(
        laplace_execution_run_work(
            &grant, &request, &serial, &serial_state, FingerprintChunk,
            &serial_receipt),
        LAPLACE_EXECUTION_OK);
    EXPECT_EQ(serial_receipt.flags, LAPLACE_EXECUTION_WORK_CALCULATION_ONLY);
    EXPECT_EQ(serial_receipt.completed_chunks, serial_receipt.plan.chunk_count);
    EXPECT_EQ(serial_receipt.completed_items, request.item_count);
    ASSERT_FALSE(serial_state.chunks.empty());
    EXPECT_EQ(serial_state.chunks.front().first_item, 0U);
    EXPECT_EQ(
        serial_state.chunks.back().first_item +
            serial_state.chunks.back().item_count,
        request.item_count);

    ReverseProviderState provider_state;
    auto reverse = ReverseProvider(provider_state);
    WorkTaskState reverse_state;
    laplace_execution_work_receipt reverse_receipt{};
    ASSERT_EQ(
        laplace_execution_run_work(
            &grant, &request, &reverse, &reverse_state, FingerprintChunk,
            &reverse_receipt),
        LAPLACE_EXECUTION_OK);
    EXPECT_EQ(provider_state.prepare_calls, 1U);
    EXPECT_EQ(provider_state.finish_calls, 1U);
    EXPECT_EQ(provider_state.abort_calls, 0U);
    EXPECT_EQ(
        std::memcmp(
            serial_receipt.plan_fingerprint.bytes,
            reverse_receipt.plan_fingerprint.bytes,
            sizeof(serial_receipt.plan_fingerprint.bytes)),
        0);
    EXPECT_EQ(
        std::memcmp(
            serial_receipt.result_fingerprint.bytes,
            reverse_receipt.result_fingerprint.bytes,
            sizeof(serial_receipt.result_fingerprint.bytes)),
        0);
    EXPECT_NE(
        std::memcmp(
            serial_receipt.receipt_id.bytes,
            reverse_receipt.receipt_id.bytes,
            sizeof(serial_receipt.receipt_id.bytes)),
        0);

    WorkTaskState repeated_state;
    laplace_execution_work_receipt repeated_receipt{};
    ASSERT_EQ(
        laplace_execution_run_work(
            &grant, &request, &serial, &repeated_state, FingerprintChunk,
            &repeated_receipt),
        LAPLACE_EXECUTION_OK);
    EXPECT_EQ(
        std::memcmp(
            serial_receipt.receipt_id.bytes,
            repeated_receipt.receipt_id.bytes,
            sizeof(serial_receipt.receipt_id.bytes)),
        0);
}

TEST(ExecutionRuntime, ExplicitCompletionStateAcceptsAnAllZeroResultFingerprint) {
    const laplace_execution_grant grant{4096U, 2U, 1U};
    const laplace_execution_work_request request{
        8U, 256U, 16U, 2U, 2U, 1U, 0U, 0U};
    laplace_execution_runtime_provider_v1 serial{};
    ASSERT_EQ(
        laplace_execution_serial_provider(&serial), LAPLACE_EXECUTION_OK);
    laplace_execution_work_receipt receipt{};
    ASSERT_EQ(
        laplace_execution_run_work(
            &grant, &request, &serial, nullptr, ZeroFingerprintChunk, &receipt),
        LAPLACE_EXECUTION_OK);
    EXPECT_EQ(receipt.completed_chunks, receipt.plan.chunk_count);
    EXPECT_EQ(receipt.completed_items, request.item_count);
}

TEST(ExecutionRuntime, AnAllZeroProviderFingerprintRemainsAValidIdentity) {
    const laplace_execution_grant grant{4096U, 2U, 1U};
    const laplace_execution_work_request request{
        8U, 256U, 16U, 2U, 2U, 1U, 0U, 0U};
    ReverseProviderState state{};
    auto provider = ReverseProvider(state);
    std::memset(
        provider.provider_fingerprint.bytes, 0,
        sizeof(provider.provider_fingerprint.bytes));
    WorkTaskState task_state;
    laplace_execution_work_receipt receipt{};
    ASSERT_EQ(
        laplace_execution_run_work(
            &grant, &request, &provider, &task_state, FingerprintChunk, &receipt),
        LAPLACE_EXECUTION_OK);
    EXPECT_EQ(receipt.completed_items, request.item_count);
}

TEST(ExecutionRuntime, MissingChunkResultAbortsAndCannotPublishSuccess) {
    const laplace_execution_grant grant{4096U, 4U, 1U};
    const laplace_execution_work_request request{
        23U, 256U, 16U, 2U, 4U, 1U, 0U, 0U};
    ReverseProviderState provider_state;
    auto provider = ReverseProvider(provider_state);
    provider.run = OmitLastRun;
    WorkTaskState task_state;
    laplace_execution_work_receipt receipt{};
    EXPECT_EQ(
        laplace_execution_run_work(
            &grant, &request, &provider, &task_state, FingerprintChunk, &receipt),
        LAPLACE_EXECUTION_RESULT_INVALID);
    EXPECT_EQ(provider_state.prepare_calls, 1U);
    EXPECT_EQ(provider_state.finish_calls, 0U);
    EXPECT_EQ(provider_state.abort_calls, 1U);
    EXPECT_LT(receipt.completed_chunks, receipt.plan.chunk_count);
    EXPECT_EQ(receipt.status, LAPLACE_EXECUTION_RESULT_INVALID);
}

TEST(ExecutionRuntime, TaskFailureAbortsWithAnExactPartialReceipt) {
    const laplace_execution_grant grant{4096U, 4U, 1U};
    const laplace_execution_work_request request{
        23U, 256U, 16U, 2U, 4U, 1U, 0U, 0U};
    laplace_execution_runtime_provider_v1 serial{};
    ASSERT_EQ(
        laplace_execution_serial_provider(&serial), LAPLACE_EXECUTION_OK);
    WorkTaskState state;
    state.fail_chunk = 1U;
    laplace_execution_work_receipt receipt{};
    EXPECT_EQ(
        laplace_execution_run_work(
            &grant, &request, &serial, &state, FingerprintChunk, &receipt),
        LAPLACE_EXECUTION_PROVIDER_RUN_FAILED);
    EXPECT_EQ(receipt.completed_chunks, 1U);
    EXPECT_EQ(receipt.completed_items, state.chunks.front().item_count);
    EXPECT_EQ(receipt.status, LAPLACE_EXECUTION_PROVIDER_RUN_FAILED);
}

}  // namespace
