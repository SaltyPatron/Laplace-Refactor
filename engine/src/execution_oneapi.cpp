#include "laplace/execution.h"

#include "laplace/contract/oneapi-provider.h"

#include "blake3.h"
#include "mkl_service.h"
#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/task_arena.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

constexpr std::uint8_t provider_domain[] =
    "laplace-execution-oneapi-provider-v1";
constexpr std::uint32_t provider_idle = 0U;
constexpr std::uint32_t provider_prepared = 1U;
constexpr std::uint32_t provider_running = 2U;

bool same_plan(
    const laplace_execution_work_plan& left,
    const laplace_execution_work_plan& right) noexcept {
    return left.chunk_items == right.chunk_items &&
        left.chunk_count == right.chunk_count &&
        left.peak_memory_bytes == right.peak_memory_bytes &&
        left.outer_workers == right.outer_workers &&
        left.inner_threads_per_worker == right.inner_threads_per_worker &&
        left.io_slots == right.io_slots && left.reserved == right.reserved;
}

void hash_u32(blake3_hasher& hasher, const std::uint32_t value) {
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)};
    blake3_hasher_update(&hasher, bytes, sizeof(bytes));
}

template <std::size_t Size>
void hash_literal(blake3_hasher& hasher, const char (&value)[Size]) {
    blake3_hasher_update(&hasher, value, Size - 1U);
}

laplace_execution_status provider_prepare(
    void* const raw_state,
    const laplace_execution_grant* const grant,
    const laplace_execution_work_plan* const plan) {
    if (raw_state == nullptr || grant == nullptr || plan == nullptr ||
        grant->cpu_slots == 0U || plan->outer_workers == 0U ||
        plan->inner_threads_per_worker == 0U || plan->reserved != 0U ||
        grant->cpu_slots > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        plan->inner_threads_per_worker >
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        plan->outer_workers >
            grant->cpu_slots / plan->inner_threads_per_worker) {
        return LAPLACE_EXECUTION_PROVIDER_INVALID;
    }
    auto& state = *static_cast<laplace_execution_oneapi_provider_state*>(raw_state);
    std::atomic_ref<std::uint32_t> lifecycle(state.lifecycle);
    std::uint32_t expected = provider_idle;
    if (!lifecycle.compare_exchange_strong(
            expected, provider_prepared, std::memory_order_acq_rel)) {
        return LAPLACE_EXECUTION_PROVIDER_INVALID;
    }
    state.grant = *grant;
    state.plan = *plan;
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status provider_run(
    void* const raw_state,
    const laplace_execution_work_plan* const plan,
    const laplace_execution_chunk* const chunks,
    const std::size_t chunk_count,
    void* const task_state,
    const laplace_execution_work_task_fn task,
    laplace_execution_chunk_result* const results) {
    if (raw_state == nullptr || plan == nullptr || chunks == nullptr ||
        task == nullptr || results == nullptr ||
        chunk_count != static_cast<std::size_t>(plan->chunk_count)) {
        return LAPLACE_EXECUTION_PROVIDER_INVALID;
    }
    auto& state = *static_cast<laplace_execution_oneapi_provider_state*>(raw_state);
    if (!same_plan(state.plan, *plan)) {
        return LAPLACE_EXECUTION_PROVIDER_INVALID;
    }
    std::atomic_ref<std::uint32_t> lifecycle(state.lifecycle);
    std::uint32_t expected = provider_prepared;
    if (!lifecycle.compare_exchange_strong(
            expected, provider_running, std::memory_order_acq_rel)) {
        return LAPLACE_EXECUTION_PROVIDER_INVALID;
    }

    std::atomic<std::size_t> next_chunk{0U};
    std::atomic<std::uint32_t> first_failure{LAPLACE_EXECUTION_OK};
    try {
        oneapi::tbb::global_control control(
            oneapi::tbb::global_control::max_allowed_parallelism,
            static_cast<std::size_t>(state.grant.cpu_slots));
        oneapi::tbb::task_arena arena(static_cast<int>(state.grant.cpu_slots));
        arena.execute([&] {
            oneapi::tbb::parallel_for(
                std::uint32_t{0U}, plan->outer_workers,
                [&](const std::uint32_t) {
#if !defined(LAPLACE_TEST_IGNORE_MKL_THREAD_GRANT)
                    const int prior_mkl_threads = mkl_set_num_threads_local(
                        static_cast<int>(plan->inner_threads_per_worker));
#endif
                    while (first_failure.load(std::memory_order_acquire) ==
                           LAPLACE_EXECUTION_OK) {
                        const std::size_t index =
                            next_chunk.fetch_add(1U, std::memory_order_relaxed);
                        if (index >= chunk_count) {
                            break;
                        }
                        const laplace_execution_status status = task(
                            task_state, &chunks[index],
                            &results[index].result_fingerprint);
                        if (status == LAPLACE_EXECUTION_OK) {
                            results[index].state = LAPLACE_EXECUTION_CHUNK_COMPLETE;
                            results[index].task_status = LAPLACE_EXECUTION_OK;
                            continue;
                        }
                        results[index].state = LAPLACE_EXECUTION_CHUNK_FAILED;
                        results[index].task_status =
                            static_cast<std::uint32_t>(status);
                        std::uint32_t no_failure = LAPLACE_EXECUTION_OK;
                        first_failure.compare_exchange_strong(
                            no_failure, static_cast<std::uint32_t>(status),
                            std::memory_order_acq_rel);
                    }
#if !defined(LAPLACE_TEST_IGNORE_MKL_THREAD_GRANT)
                    (void)mkl_set_num_threads_local(prior_mkl_threads);
#endif
                });
        });
    } catch (...) {
        lifecycle.store(provider_prepared, std::memory_order_release);
        return LAPLACE_EXECUTION_PROVIDER_RUN_FAILED;
    }
    lifecycle.store(provider_prepared, std::memory_order_release);
    return static_cast<laplace_execution_status>(
        first_failure.load(std::memory_order_acquire));
}

laplace_execution_status provider_finish(void* const raw_state) {
    if (raw_state == nullptr) {
        return LAPLACE_EXECUTION_PROVIDER_INVALID;
    }
    auto& state = *static_cast<laplace_execution_oneapi_provider_state*>(raw_state);
    std::atomic_ref<std::uint32_t> lifecycle(state.lifecycle);
    std::uint32_t expected = provider_prepared;
    if (!lifecycle.compare_exchange_strong(
            expected, provider_idle, std::memory_order_acq_rel)) {
        return LAPLACE_EXECUTION_PROVIDER_INVALID;
    }
    return LAPLACE_EXECUTION_OK;
}

void provider_abort(void* const raw_state) {
    if (raw_state != nullptr) {
        auto& state = *static_cast<laplace_execution_oneapi_provider_state*>(raw_state);
        std::atomic_ref<std::uint32_t>(state.lifecycle).store(
            provider_idle, std::memory_order_release);
    }
}

}  // namespace

extern "C" laplace_execution_status laplace_execution_oneapi_provider(
    laplace_execution_oneapi_provider_state* const state,
    laplace_execution_runtime_provider_v1* const provider) {
    if (state == nullptr || provider == nullptr || state->lifecycle != provider_idle ||
        state->reserved != 0U) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    laplace_execution_runtime_provider_v1 next{};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, provider_domain, sizeof(provider_domain) - 1U);
    hash_literal(hasher, LAPLACE_ONEAPI_SELECTION_SHA256);
    hash_literal(hasher, LAPLACE_ONEAPI_RUNTIME_VERSION);
    hash_literal(hasher, LAPLACE_ONEAPI_TBB_VERSION);
    hash_literal(hasher, LAPLACE_ONEAPI_MKL_VERSION);
    hash_u32(hasher, LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MAJOR);
    hash_u32(hasher, LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MINOR);
    blake3_hasher_finalize(
        &hasher, next.provider_fingerprint.bytes,
        sizeof(next.provider_fingerprint.bytes));
    next.state = state;
    next.prepare = provider_prepare;
    next.run = provider_run;
    next.finish = provider_finish;
    next.abort = provider_abort;
    next.abi_major = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MAJOR;
    next.abi_minor = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MINOR;
    next.flags = LAPLACE_EXECUTION_KNOWN_PROVIDER_FLAGS;
    *provider = next;
    return LAPLACE_EXECUTION_OK;
}
