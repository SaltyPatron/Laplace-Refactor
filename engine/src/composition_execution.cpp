#include "laplace/composition_execution.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "blake3.h"
#include "laplace/detail/composition_frontier.hpp"

namespace {

constexpr char PlanDomain[] = "laplace-composition-frontier-execution-plan-v1";

void HashU32(blake3_hasher& hasher, const std::uint32_t value) {
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)};
    blake3_hasher_update(&hasher, bytes, sizeof(bytes));
}

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (std::size_t index = 0U; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(&hasher, bytes, sizeof(bytes));
}

bool AddOverflow(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return true;
    }
    output = left + right;
    return false;
}

laplace_composition_status MapExecutionStatus(
    const laplace_execution_status status) {
    switch (status) {
        case LAPLACE_EXECUTION_OK:
            return LAPLACE_COMPOSITION_OK;
        case LAPLACE_EXECUTION_INVALID_ARGUMENT:
            return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
        case LAPLACE_EXECUTION_CAPACITY_INSUFFICIENT:
        case LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT:
            return LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT;
        case LAPLACE_EXECUTION_OVERFLOW:
            return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
        default:
            return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }
}

}  // namespace

extern "C" laplace_composition_status
laplace_composition_frontier_execution_plan_build(
    const laplace_composition_working_set_input* const input,
    laplace_composition_frontier_execution_plan* const plan) {
    if (plan != nullptr) {
        *plan = laplace_composition_frontier_execution_plan{};
    }
    if (input == nullptr || plan == nullptr || input->context == nullptr ||
        input->request_count == 0U || input->context->resource_grant.cpu_slots == 0U) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }

    laplace::composition::detail::FrontierPlan frontier_plan;
    const auto frontier_status =
        laplace::composition::detail::BuildFrontierPlan(*input, frontier_plan);
    if (frontier_status != LAPLACE_COMPOSITION_OK) {
        return frontier_status;
    }
    const std::uint64_t frontier_count = frontier_plan.frontier_count();
    if (frontier_count == 0U ||
        frontier_plan.frontier_offsets.size() !=
            static_cast<std::size_t>(frontier_count + 1U)) {
        return LAPLACE_COMPOSITION_REFERENCE_INVALID;
    }

    laplace_composition_frontier_execution_plan next{};
    next.request_count = input->request_count;
    next.dependency_edge_count = frontier_plan.dependency_edge_count;
    next.frontier_count = frontier_count;
    next.dependency_depth = frontier_count - 1U;
    next.maximum_frontier_width = frontier_plan.maximum_frontier_width;
    next.minimum_frontier_width = std::numeric_limits<std::uint64_t>::max();
    next.grant_memory_bytes = input->context->resource_grant.memory_bytes;
    next.grant_cpu_slots = input->context->resource_grant.cpu_slots;
    next.grant_io_slots = input->context->resource_grant.io_slots;
    next.minimum_outer_workers = std::numeric_limits<std::uint32_t>::max();
    next.version = LAPLACE_COMPOSITION_FRONTIER_EXECUTION_PLAN_VERSION;

    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, PlanDomain, sizeof(PlanDomain) - 1U);
    HashU64(hasher, next.request_count);
    HashU64(hasher, next.dependency_edge_count);
    HashU64(hasher, next.frontier_count);
    HashU64(hasher, next.dependency_depth);
    HashU64(hasher, next.maximum_frontier_width);
    HashU64(hasher, next.grant_memory_bytes);
    HashU32(hasher, next.grant_cpu_slots);
    HashU32(hasher, next.grant_io_slots);

    HashU64(
        hasher,
        static_cast<std::uint64_t>(frontier_plan.depth_by_request.size()));
    for (const std::uint64_t depth : frontier_plan.depth_by_request) {
        HashU64(hasher, depth);
    }
    HashU64(
        hasher,
        static_cast<std::uint64_t>(frontier_plan.request_indices.size()));
    for (const std::uint64_t request_index : frontier_plan.request_indices) {
        HashU64(hasher, request_index);
    }

    for (std::uint64_t frontier = 0U; frontier < frontier_count; ++frontier) {
        const std::uint64_t first =
            frontier_plan.frontier_offsets[static_cast<std::size_t>(frontier)];
        const std::uint64_t end =
            frontier_plan.frontier_offsets[static_cast<std::size_t>(frontier + 1U)];
        if (end <= first) {
            return LAPLACE_COMPOSITION_REFERENCE_INVALID;
        }
        const std::uint64_t width = end - first;
        next.minimum_frontier_width =
            std::min(next.minimum_frontier_width, width);

        laplace_execution_work_request work{};
        work.item_count = width;
        work.minimum_chunk_items = 1U;
        work.outer_worker_limit = input->context->resource_grant.cpu_slots;
        work.inner_threads_per_worker = 1U;

        laplace_execution_work_plan work_plan{};
        const auto execution_status = laplace_execution_plan_work(
            &input->context->resource_grant, &work, &work_plan);
        if (execution_status != LAPLACE_EXECUTION_OK) {
            return MapExecutionStatus(execution_status);
        }
        std::uint64_t chunk_total{};
        if (AddOverflow(
                next.total_planned_chunks,
                work_plan.chunk_count,
                chunk_total)) {
            return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
        }
        next.total_planned_chunks = chunk_total;
        next.minimum_outer_workers =
            std::min(next.minimum_outer_workers, work_plan.outer_workers);
        next.maximum_outer_workers =
            std::max(next.maximum_outer_workers, work_plan.outer_workers);

        HashU64(hasher, frontier);
        HashU64(hasher, first);
        HashU64(hasher, end);
        HashU64(hasher, width);
        HashU64(hasher, work_plan.chunk_items);
        HashU64(hasher, work_plan.chunk_count);
        HashU64(hasher, work_plan.peak_memory_bytes);
        HashU32(hasher, work_plan.outer_workers);
        HashU32(hasher, work_plan.inner_threads_per_worker);
        HashU32(hasher, work_plan.io_slots);
    }

    if (next.minimum_frontier_width ==
            std::numeric_limits<std::uint64_t>::max() ||
        next.minimum_outer_workers ==
            std::numeric_limits<std::uint32_t>::max()) {
        return LAPLACE_COMPOSITION_REFERENCE_INVALID;
    }
    next.status = LAPLACE_COMPOSITION_OK;
    HashU64(hasher, next.minimum_frontier_width);
    HashU64(hasher, next.total_planned_chunks);
    HashU32(hasher, next.minimum_outer_workers);
    HashU32(hasher, next.maximum_outer_workers);
    HashU32(hasher, next.version);
    HashU32(hasher, next.status);
    blake3_hasher_finalize(
        &hasher, next.plan_fingerprint.bytes,
        sizeof(next.plan_fingerprint.bytes));

    *plan = next;
    return LAPLACE_COMPOSITION_OK;
}
