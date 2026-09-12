#include "laplace/composition_execution.h"
#include "laplace/detail/composition_frontier.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using laplace::composition::detail::BuildFrontierPlan;
using laplace::composition::detail::FrontierPlan;

struct ActiveFrontierCapture final {
    FrontierPlan expected;
    std::size_t next_frontier{};
    std::vector<laplace_execution_work_receipt> receipts;
    bool mismatch{};
};

thread_local ActiveFrontierCapture* active_frontier_capture = nullptr;
thread_local const laplace_execution_runtime_provider_v1* active_execution_provider = nullptr;
std::mutex frontier_receipt_mutex;
std::unordered_map<
    const laplace_composition_working_set*,
    std::vector<laplace_execution_work_receipt>> frontier_receipts;

extern "C" laplace_execution_status composition_capture_run_work(
    const laplace_execution_grant* grant,
    const laplace_execution_work_request* request,
    const laplace_execution_runtime_provider_v1* provider,
    void* task_state,
    laplace_execution_work_task_fn task,
    laplace_execution_work_receipt* receipt);

extern "C" laplace_execution_status composition_select_oneapi_provider(
    laplace_execution_oneapi_provider_state* state,
    laplace_execution_runtime_provider_v1* provider);

}  // namespace

/*
 * Preserve the existing semantic composition implementation byte-for-byte while
 * placing lifecycle ownership around its externally relevant execution seams:
 *
 * 1. working-set create/destroy become wrappers that bind postflight receipts to
 *    the opaque working-set lifetime;
 * 2. common execution dispatch is intercepted only for CalculateRequestChunk so
 *    every runtime frontier is checked against the shared dependency planner and
 *    its real execution receipt is retained;
 * 3. oneAPI provider selection is intercepted only when a caller explicitly binds
 *    a common runtime provider through the public provider-bound create surface.
 *
 * The implementation unit remains ordinary C++ source included into this one
 * translation unit; it is not a second semantic engine.
 */
#define laplace_composition_working_set_create composition_working_set_create_impl
#define laplace_composition_working_set_destroy composition_working_set_destroy_impl
#define laplace_execution_run_work composition_capture_run_work
#define laplace_execution_oneapi_provider composition_select_oneapi_provider
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wsubobject-linkage"
#endif
#include "composition_legacy.inc"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#undef laplace_execution_oneapi_provider
#undef laplace_execution_run_work
#undef laplace_composition_working_set_destroy
#undef laplace_composition_working_set_create

namespace {

class CaptureScope final {
public:
    explicit CaptureScope(ActiveFrontierCapture& capture) noexcept
        : prior_(active_frontier_capture) {
        active_frontier_capture = &capture;
    }

    ~CaptureScope() {
        active_frontier_capture = prior_;
    }

    CaptureScope(const CaptureScope&) = delete;
    CaptureScope& operator=(const CaptureScope&) = delete;

private:
    ActiveFrontierCapture* prior_{};
};

class ProviderScope final {
public:
    explicit ProviderScope(
        const laplace_execution_runtime_provider_v1* const provider) noexcept
        : prior_(active_execution_provider) {
        active_execution_provider = provider;
    }

    ~ProviderScope() {
        active_execution_provider = prior_;
    }

    ProviderScope(const ProviderScope&) = delete;
    ProviderScope& operator=(const ProviderScope&) = delete;

private:
    const laplace_execution_runtime_provider_v1* prior_{};
};

bool ReceiptSetComplete(
    const ActiveFrontierCapture& capture,
    const laplace_composition_working_set_input& input) noexcept {
    if (capture.mismatch ||
        capture.next_frontier != capture.expected.frontier_count() ||
        capture.receipts.size() != capture.expected.frontier_count()) {
        return false;
    }
    std::uint64_t completed_items = 0U;
    for (const auto& receipt : capture.receipts) {
        if (receipt.status != LAPLACE_EXECUTION_OK ||
            receipt.completed_items > UINT64_MAX - completed_items) {
            return false;
        }
        completed_items += receipt.completed_items;
    }
    return completed_items == input.request_count;
}

extern "C" laplace_execution_status composition_select_oneapi_provider(
    laplace_execution_oneapi_provider_state* const state,
    laplace_execution_runtime_provider_v1* const provider) {
    if (active_execution_provider == nullptr) {
        return laplace_execution_oneapi_provider(state, provider);
    }
    if (provider == nullptr) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    *provider = *active_execution_provider;
    return LAPLACE_EXECUTION_OK;
}

extern "C" laplace_execution_status composition_capture_run_work(
    const laplace_execution_grant* const grant,
    const laplace_execution_work_request* const request,
    const laplace_execution_runtime_provider_v1* const provider,
    void* const task_state,
    const laplace_execution_work_task_fn task,
    laplace_execution_work_receipt* const receipt) {
    auto* const capture = active_frontier_capture;
    if (capture == nullptr || task != CalculateRequestChunk) {
        return laplace_execution_run_work(
            grant, request, provider, task_state, task, receipt);
    }
    if (task_state == nullptr || receipt == nullptr ||
        capture->next_frontier >= capture->expected.frontier_count()) {
        capture->mismatch = true;
        return LAPLACE_EXECUTION_RESULT_INVALID;
    }

    auto& level_task = *static_cast<RequestLevelTask*>(task_state);
    if (level_task.request_indexes == nullptr) {
        capture->mismatch = true;
        return LAPLACE_EXECUTION_RESULT_INVALID;
    }
    const std::size_t frontier = capture->next_frontier;
    const std::uint64_t first = capture->expected.frontier_offsets[frontier];
    const std::uint64_t end = capture->expected.frontier_offsets[frontier + 1U];
    if (end < first ||
        end - first != level_task.request_indexes->size()) {
        capture->mismatch = true;
        return LAPLACE_EXECUTION_RESULT_INVALID;
    }
    for (std::size_t offset = 0U;
         offset < level_task.request_indexes->size(); ++offset) {
        if ((*level_task.request_indexes)[offset] !=
            capture->expected.request_indices[
                static_cast<std::size_t>(first) + offset]) {
            capture->mismatch = true;
            return LAPLACE_EXECUTION_RESULT_INVALID;
        }
    }

    const auto status = laplace_execution_run_work(
        grant, request, provider, task_state, task, receipt);
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }
    try {
        capture->receipts.push_back(*receipt);
    } catch (const std::bad_alloc&) {
        return LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT;
    }
    ++capture->next_frontier;
    return LAPLACE_EXECUTION_OK;
}

}  // namespace

extern "C" laplace_composition_status
laplace_composition_working_set_create_with_provider(
    const laplace_composition_working_set_input* const input,
    const laplace_execution_runtime_provider_v1* const provider,
    laplace_composition_working_set** const working_set) {
    if (working_set != nullptr) {
        *working_set = nullptr;
    }
    if (input == nullptr || provider == nullptr || working_set == nullptr) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }

    ProviderScope provider_scope(provider);
    return laplace_composition_working_set_create(input, working_set);
}

extern "C" laplace_composition_status laplace_composition_working_set_create(
    const laplace_composition_working_set_input* const input,
    laplace_composition_working_set** const working_set) {
    if (input == nullptr || working_set == nullptr ||
        active_frontier_capture != nullptr) {
        return composition_working_set_create_impl(input, working_set);
    }

    ActiveFrontierCapture capture{};
    const auto frontier_status = BuildFrontierPlan(*input, capture.expected);
    if (frontier_status != LAPLACE_COMPOSITION_OK) {
        return composition_working_set_create_impl(input, working_set);
    }
    try {
        capture.receipts.reserve(
            static_cast<std::size_t>(capture.expected.frontier_count()));
    } catch (const std::bad_alloc&) {
        if (working_set != nullptr) {
            *working_set = nullptr;
        }
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }

    laplace_composition_status status{};
    {
        CaptureScope scope(capture);
        status = composition_working_set_create_impl(input, working_set);
    }
    if (status != LAPLACE_COMPOSITION_OK) {
        return status;
    }
    if (working_set == nullptr || *working_set == nullptr ||
        !ReceiptSetComplete(capture, *input)) {
        if (working_set != nullptr && *working_set != nullptr) {
            composition_working_set_destroy_impl(working_set);
        }
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }

    try {
        std::lock_guard<std::mutex> lock(frontier_receipt_mutex);
        frontier_receipts.emplace(*working_set, std::move(capture.receipts));
    } catch (const std::bad_alloc&) {
        composition_working_set_destroy_impl(working_set);
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
    return LAPLACE_COMPOSITION_OK;
}

extern "C" const laplace_execution_work_receipt*
laplace_composition_working_set_frontier_execution_receipts(
    const laplace_composition_working_set* const working_set,
    size_t* const receipt_count) {
    if (receipt_count != nullptr) {
        *receipt_count = 0U;
    }
    if (working_set == nullptr || receipt_count == nullptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(frontier_receipt_mutex);
    const auto found = frontier_receipts.find(working_set);
    if (found == frontier_receipts.end()) {
        return nullptr;
    }
    *receipt_count = found->second.size();
    return found->second.empty() ? nullptr : found->second.data();
}

extern "C" void laplace_composition_working_set_destroy(
    laplace_composition_working_set** const working_set) {
    if (working_set != nullptr && *working_set != nullptr) {
        std::lock_guard<std::mutex> lock(frontier_receipt_mutex);
        frontier_receipts.erase(*working_set);
    }
    composition_working_set_destroy_impl(working_set);
}
