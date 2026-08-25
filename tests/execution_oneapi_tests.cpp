#include "laplace/execution.h"

#include "mkl_cblas.h"
#include "mkl_service.h"
#include "oneapi/tbb/task_arena.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

struct OneApiTaskState final {
    std::atomic<std::uint32_t> active{};
    std::atomic<std::uint32_t> maximum_active{};
    std::atomic<std::uint32_t> configuration_failures{};
    std::uint32_t expected_inner_threads{};
    std::uint32_t granted_cpu_slots{};
    bool verify_provider_limits{};
};

laplace_execution_status MklFingerprintChunk(
    void* const raw_state,
    const laplace_execution_chunk* const chunk,
    laplace_digest256* const result) {
    auto& state = *static_cast<OneApiTaskState*>(raw_state);
    const std::uint32_t active =
        state.active.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    std::uint32_t maximum = state.maximum_active.load(std::memory_order_relaxed);
    while (active > maximum && !state.maximum_active.compare_exchange_weak(
               maximum, active, std::memory_order_relaxed)) {
    }
    if (state.verify_provider_limits &&
        (mkl_get_max_threads() !=
             static_cast<int>(state.expected_inner_threads) ||
         oneapi::tbb::this_task_arena::max_concurrency() >
             static_cast<int>(state.granted_cpu_slots))) {
        state.configuration_failures.fetch_add(1U, std::memory_order_relaxed);
    }

    const double left[4] = {1.0, 2.0, 3.0, 4.0};
    const double right[4] = {5.0, 6.0, 7.0, 8.0};
    double product[4] = {};
    cblas_dgemm(
        CblasRowMajor, CblasNoTrans, CblasNoTrans, 2, 2, 2, 1.0,
        left, 2, right, 2, 0.0, product, 2);
    static const double expected[4] = {19.0, 22.0, 43.0, 50.0};
    if (std::memcmp(product, expected, sizeof(product)) != 0) {
        state.active.fetch_sub(1U, std::memory_order_acq_rel);
        return LAPLACE_EXECUTION_RESULT_INVALID;
    }
    for (std::size_t index = 0U; index < sizeof(result->bytes); ++index) {
        result->bytes[index] = static_cast<std::uint8_t>(
            chunk->chunk_index + chunk->first_item + chunk->item_count +
            static_cast<std::uint64_t>(index) + UINT64_C(1));
    }
    state.active.fetch_sub(1U, std::memory_order_acq_rel);
    return LAPLACE_EXECUTION_OK;
}

}  // namespace

TEST(ExecutionOneApi, AppliesOneTbbAndOneMklFromTheSameConservedGrant) {
    const laplace_execution_grant grant{UINT64_C(4096), 4U, 0U};
    const laplace_execution_work_request request{
        64U, 256U, 64U, 4U, 2U, 2U, 0U, 0U};

    laplace_execution_runtime_provider_v1 serial{};
    ASSERT_EQ(laplace_execution_serial_provider(&serial), LAPLACE_EXECUTION_OK);
    OneApiTaskState serial_state{};
    laplace_execution_work_receipt serial_receipt{};
    ASSERT_EQ(
        laplace_execution_run_work(
            &grant, &request, &serial, &serial_state, MklFingerprintChunk,
            &serial_receipt),
        LAPLACE_EXECUTION_OK);

    laplace_execution_oneapi_provider_state provider_state{};
    laplace_execution_runtime_provider_v1 oneapi{};
    ASSERT_EQ(
        laplace_execution_oneapi_provider(&provider_state, &oneapi),
        LAPLACE_EXECUTION_OK);
    OneApiTaskState oneapi_state{};
    oneapi_state.expected_inner_threads = 2U;
    oneapi_state.granted_cpu_slots = grant.cpu_slots;
    oneapi_state.verify_provider_limits = true;
    laplace_execution_work_receipt oneapi_receipt{};
    ASSERT_EQ(
        laplace_execution_run_work(
            &grant, &request, &oneapi, &oneapi_state, MklFingerprintChunk,
            &oneapi_receipt),
        LAPLACE_EXECUTION_OK);

    EXPECT_EQ(oneapi_state.configuration_failures.load(), 0U);
    EXPECT_LE(oneapi_state.maximum_active.load(), oneapi_receipt.plan.outer_workers);
    EXPECT_EQ(provider_state.lifecycle, 0U);
    EXPECT_EQ(
        std::memcmp(
            serial_receipt.plan_fingerprint.bytes,
            oneapi_receipt.plan_fingerprint.bytes,
            sizeof(oneapi_receipt.plan_fingerprint.bytes)),
        0);
    EXPECT_EQ(
        std::memcmp(
            serial_receipt.result_fingerprint.bytes,
            oneapi_receipt.result_fingerprint.bytes,
            sizeof(oneapi_receipt.result_fingerprint.bytes)),
        0);
    EXPECT_NE(
        std::memcmp(
            serial_receipt.provider_fingerprint.bytes,
            oneapi_receipt.provider_fingerprint.bytes,
            sizeof(oneapi_receipt.provider_fingerprint.bytes)),
        0);
}
