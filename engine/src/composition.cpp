#include "laplace/composition_execution.h"
#include "laplace/detail/composition_frontier.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <limits>
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
    std::uint64_t wall_time_ns{};
    std::uint64_t core_time_ns{};
    std::uint64_t worker_capacity_ns{};
    bool mismatch{};
};

thread_local ActiveFrontierCapture* active_frontier_capture = nullptr;
thread_local const laplace_execution_runtime_provider_v1* active_execution_provider = nullptr;
std::mutex frontier_receipt_mutex;
std::unordered_map<
    const laplace_composition_working_set*,
    std::vector<laplace_execution_work_receipt>> frontier_receipts;
std::unordered_map<
    const laplace_composition_working_set*,
    laplace_composition_frontier_execution_postflight> frontier_postflights;

extern "C" laplace_execution_status composition_capture_run_work(
    const laplace_execution_grant* grant,
    const laplace_execution_work_request* request,
    const laplace_execution_runtime_provider_v1* provider,
    void* task_state,
    laplace_execution_work_task_fn task,
    laplace_execution_work_receipt* receipt);

bool AddMetric(
    std::uint64_t& total,
    const std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += value;
    return true;
}

bool MultiplyMetric(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

std::uint64_t ClockDeltaNanoseconds(
    const std::clock_t start,
    const std::clock_t end) noexcept {
    if (start == static_cast<std::clock_t>(-1) ||
        end == static_cast<std::clock_t>(-1) || end < start ||
        CLOCKS_PER_SEC <= 0) {
        return 0U;
    }
    const auto ticks = static_cast<std::uint64_t>(end - start);
    const auto ticks_per_second = static_cast<std::uint64_t>(CLOCKS_PER_SEC);
    const std::uint64_t whole_seconds = ticks / ticks_per_second;
    const std::uint64_t remainder_ticks = ticks % ticks_per_second;
    constexpr std::uint64_t NanosecondsPerSecond = UINT64_C(1000000000);
    if (whole_seconds >
        std::numeric_limits<std::uint64_t>::max() / NanosecondsPerSecond) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t whole_ns = whole_seconds * NanosecondsPerSecond;
    const std::uint64_t remainder_ns =
        (remainder_ticks * NanosecondsPerSecond) / ticks_per_second;
    if (remainder_ns >
        std::numeric_limits<std::uint64_t>::max() - whole_ns) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return whole_ns + remainder_ns;
}

bool AccumulatePhysicalTiming(
    ActiveFrontierCapture& capture,
    const std::chrono::steady_clock::time_point wall_start,
    const std::chrono::steady_clock::time_point wall_end,
    const std::clock_t core_start,
    const std::clock_t core_end,
    const std::uint32_t workers) noexcept {
    const auto wall_duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            wall_end - wall_start).count();
    if (wall_duration < 0) {
        return false;
    }
    const auto wall_ns = static_cast<std::uint64_t>(wall_duration);
    const auto core_ns = ClockDeltaNanoseconds(core_start, core_end);
    std::uint64_t capacity_ns{};
    if (!MultiplyMetric(
            wall_ns,
            static_cast<std::uint64_t>(std::max<std::uint32_t>(workers, 1U)),
            capacity_ns)) {
        return false;
    }
    return AddMetric(capture.wall_time_ns, wall_ns) &&
        AddMetric(capture.core_time_ns, core_ns) &&
        AddMetric(capture.worker_capacity_ns, capacity_ns);
}

std::uint32_t EfficiencyPpm(
    const std::uint64_t used_ns,
    const std::uint64_t capacity_ns) noexcept {
    if (capacity_ns == 0U) {
        return 0U;
    }
    const long double ratio =
        static_cast<long double>(std::min(used_ns, capacity_ns)) /
        static_cast<long double>(capacity_ns);
    const long double scaled = ratio * 1000000.0L;
    return static_cast<std::uint32_t>(scaled);
}

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

bool BuildPostflight(
    const ActiveFrontierCapture& capture,
    const laplace_composition_working_set_input& input,
    laplace_composition_frontier_execution_postflight& postflight) noexcept {
    postflight = laplace_composition_frontier_execution_postflight{};
    laplace_composition_frontier_execution_plan preflight{};
    if (laplace_composition_frontier_execution_plan_build(
            &input, &preflight) != LAPLACE_COMPOSITION_OK) {
        return false;
    }
    postflight.plan_fingerprint = preflight.plan_fingerprint;
    postflight.request_count = input.request_count;
    postflight.frontier_count = capture.expected.frontier_count();
    postflight.dependency_depth =
        postflight.frontier_count == 0U ? 0U : postflight.frontier_count - 1U;
    postflight.minimum_frontier_width =
        std::numeric_limits<std::uint64_t>::max();
    postflight.minimum_outer_workers =
        std::numeric_limits<std::uint32_t>::max();
    postflight.grant_cpu_slots = input.context->resource_grant.cpu_slots;
    postflight.wall_time_ns = capture.wall_time_ns;
    postflight.core_time_ns = capture.core_time_ns;
    postflight.worker_capacity_ns = capture.worker_capacity_ns;

    for (std::size_t frontier = 0U;
         frontier < static_cast<std::size_t>(postflight.frontier_count);
         ++frontier) {
        const std::uint64_t first = capture.expected.frontier_offsets[frontier];
        const std::uint64_t end = capture.expected.frontier_offsets[frontier + 1U];
        if (end <= first) {
            return false;
        }
        const std::uint64_t width = end - first;
        if (!AddMetric(postflight.total_frontier_width, width)) {
            return false;
        }
        postflight.minimum_frontier_width =
            std::min(postflight.minimum_frontier_width, width);
        postflight.maximum_frontier_width =
            std::max(postflight.maximum_frontier_width, width);
    }

    for (const auto& receipt : capture.receipts) {
        if (!AddMetric(postflight.completed_items, receipt.completed_items) ||
            !AddMetric(postflight.completed_chunks, receipt.completed_chunks)) {
            return false;
        }
        postflight.minimum_outer_workers = std::min(
            postflight.minimum_outer_workers, receipt.plan.outer_workers);
        postflight.maximum_outer_workers = std::max(
            postflight.maximum_outer_workers, receipt.plan.outer_workers);
    }

    if (postflight.minimum_frontier_width ==
            std::numeric_limits<std::uint64_t>::max() ||
        postflight.minimum_outer_workers ==
            std::numeric_limits<std::uint32_t>::max() ||
        postflight.completed_items != input.request_count ||
        postflight.total_frontier_width != input.request_count) {
        return false;
    }
    const std::uint64_t bounded_core_ns =
        std::min(postflight.core_time_ns, postflight.worker_capacity_ns);
    postflight.idle_wait_capacity_ns =
        postflight.worker_capacity_ns - bounded_core_ns;
    postflight.parallel_efficiency_ppm = EfficiencyPpm(
        bounded_core_ns, postflight.worker_capacity_ns);
    postflight.version =
        LAPLACE_COMPOSITION_FRONTIER_EXECUTION_POSTFLIGHT_VERSION;
    postflight.status = LAPLACE_COMPOSITION_OK;
    return true;
}

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
 * 3. an explicitly bound runtime provider is substituted only at that same common
 *    execution seam, leaving ordinary oneAPI/serial provider selection unchanged.
 *
 * The implementation unit remains ordinary C++ source included into this one
 * translation unit; it is not a second semantic engine.
 */
#define laplace_composition_working_set_create composition_working_set_create_impl
#define laplace_composition_working_set_destroy composition_working_set_destroy_impl
#define laplace_composition_working_set_resolve_presence composition_working_set_resolve_presence_impl
#define laplace_execution_run_work composition_capture_run_work
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wsubobject-linkage"
#endif
#include "composition_legacy.inc"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#undef laplace_execution_run_work
#undef laplace_composition_working_set_resolve_presence
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

laplace_digest256 CompositionSemanticContextFingerprint(
    const laplace_framework_context& context) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, "laplace-composition-semantic-context-v1");
    HashU32(hasher, context.major);
    HashU32(hasher, context.minor);
    HashU32(hasher, context.flags);
    HashU64(hasher, context.epoch_mask);
    for (std::uint32_t index = 0U;
         index < LAPLACE_FRAMEWORK_EPOCH_COUNT; ++index) {
        if ((context.epoch_mask & (UINT64_C(1) << index)) == 0U) {
            continue;
        }
        HashU32(hasher, index);
        blake3_hasher_update(
            &hasher, context.epochs[index].bytes,
            sizeof(context.epochs[index].bytes));
    }
    blake3_hasher_update(
        &hasher, context.authority_fingerprint.bytes,
        sizeof(context.authority_fingerprint.bytes));
    return Finish(hasher);
}

void BindSemanticInputFingerprint(
    const laplace_composition_working_set_input& input,
    laplace_composition_working_set& working_set) {
    const auto semantic_context =
        CompositionSemanticContextFingerprint(*input.context);
    working_set.summary.input_fingerprint =
        InputFingerprint(input, semantic_context);
}

void RefreshSemanticPublicationFingerprints(
    laplace_composition_working_set& state) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(
        hasher, "laplace-composition-working-set-semantic-receipt-v2");
    blake3_hasher_update(
        &hasher, state.summary.input_fingerprint.bytes,
        sizeof(state.summary.input_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, state.summary.stream_fingerprint.bytes,
        sizeof(state.summary.stream_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, state.summary.presence_receipt_id.bytes,
        sizeof(state.summary.presence_receipt_id.bytes));
    HashU64(hasher, state.summary.novel_entity_count);
    HashU64(hasher, state.summary.novel_physicality_count);
    HashU64(hasher, state.summary.novel_trajectory_vertex_count);
    HashU64(hasher, state.summary.occurrence_count);
    HashU32(hasher, state.effect_disposition);
    state.summary.receipt_id = Finish(hasher);

    blake3_hasher_init(&hasher);
    HashString(hasher, ProducerDomain);
    blake3_hasher_update(
        &hasher, state.summary.receipt_id.bytes,
        sizeof(state.summary.receipt_id.bytes));
    state.producer_fingerprint = Finish(hasher);
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

    const auto* const execution_provider =
        active_execution_provider == nullptr ? provider : active_execution_provider;
    const auto wall_start = std::chrono::steady_clock::now();
    const auto core_start = std::clock();
    const auto status = laplace_execution_run_work(
        grant, request, execution_provider, task_state, task, receipt);
    const auto core_end = std::clock();
    const auto wall_end = std::chrono::steady_clock::now();
    if (status != LAPLACE_EXECUTION_OK) {
        return status;
    }
    if (!AccumulatePhysicalTiming(
            *capture, wall_start, wall_end, core_start, core_end,
            receipt->plan.outer_workers)) {
        capture->mismatch = true;
        return LAPLACE_EXECUTION_RESULT_INVALID;
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
    if (input == nullptr || working_set == nullptr) {
        return composition_working_set_create_impl(input, working_set);
    }
    if (active_frontier_capture != nullptr) {
        const auto status = composition_working_set_create_impl(input, working_set);
        if (status == LAPLACE_COMPOSITION_OK &&
            working_set != nullptr && *working_set != nullptr) {
            BindSemanticInputFingerprint(*input, **working_set);
        }
        return status;
    }

    ActiveFrontierCapture capture{};
    const auto frontier_status = BuildFrontierPlan(*input, capture.expected);
    if (frontier_status != LAPLACE_COMPOSITION_OK) {
        const auto status = composition_working_set_create_impl(input, working_set);
        if (status == LAPLACE_COMPOSITION_OK && *working_set != nullptr) {
            BindSemanticInputFingerprint(*input, **working_set);
        }
        return status;
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

    BindSemanticInputFingerprint(*input, **working_set);

    laplace_composition_frontier_execution_postflight postflight{};
    if (!BuildPostflight(capture, *input, postflight)) {
        composition_working_set_destroy_impl(working_set);
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }
    try {
        std::lock_guard<std::mutex> lock(frontier_receipt_mutex);
        frontier_receipts.emplace(*working_set, capture.receipts);
        frontier_postflights.emplace(*working_set, postflight);
    } catch (const std::bad_alloc&) {
        std::lock_guard<std::mutex> lock(frontier_receipt_mutex);
        frontier_receipts.erase(*working_set);
        frontier_postflights.erase(*working_set);
        composition_working_set_destroy_impl(working_set);
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
    return LAPLACE_COMPOSITION_OK;
}

extern "C" laplace_composition_status
laplace_composition_working_set_resolve_presence(
    laplace_composition_working_set* const working_set,
    const laplace_composition_presence_provider_v1* const provider,
    laplace_composition_presence_receipt* const receipt) {
    const auto status = composition_working_set_resolve_presence_impl(
        working_set, provider, receipt);
    if (status == LAPLACE_COMPOSITION_OK && working_set != nullptr) {
        RefreshSemanticPublicationFingerprints(*working_set);
    }
    return status;
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

extern "C" laplace_composition_status
laplace_composition_working_set_frontier_execution_postflight_get(
    const laplace_composition_working_set* const working_set,
    laplace_composition_frontier_execution_postflight* const postflight) {
    if (postflight != nullptr) {
        *postflight = laplace_composition_frontier_execution_postflight{};
    }
    if (working_set == nullptr || postflight == nullptr) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(frontier_receipt_mutex);
    const auto found = frontier_postflights.find(working_set);
    if (found == frontier_postflights.end()) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    *postflight = found->second;
    return LAPLACE_COMPOSITION_OK;
}

extern "C" void laplace_composition_working_set_destroy(
    laplace_composition_working_set** const working_set) {
    if (working_set != nullptr && *working_set != nullptr) {
        std::lock_guard<std::mutex> lock(frontier_receipt_mutex);
        frontier_receipts.erase(*working_set);
        frontier_postflights.erase(*working_set);
    }
    composition_working_set_destroy_impl(working_set);
}
