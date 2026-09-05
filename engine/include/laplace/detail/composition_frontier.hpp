#ifndef LAPLACE_DETAIL_COMPOSITION_FRONTIER_HPP
#define LAPLACE_DETAIL_COMPOSITION_FRONTIER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#include "laplace/composition.h"
#include "laplace/execution.h"

namespace laplace::composition::detail {

/*
 * Physical scheduling plan for one already-topologically-ordered composition
 * working set.  This is deliberately derived only from canonical request
 * dependencies.  Source/file identity, read chunking, preferred persistence
 * batch size, worker identity, and the execution provider are not inputs.
 *
 * Requests in the same frontier have no prior-result dependency on one another
 * and may therefore execute concurrently.  `request_indices` is stable in
 * canonical request-index order inside every frontier; parallel completion
 * order is not allowed to become semantic order.
 */
struct FrontierPlan final {
    std::vector<std::uint64_t> depth_by_request;
    std::vector<std::uint64_t> frontier_offsets;
    std::vector<std::uint64_t> request_indices;
    std::uint64_t dependency_edge_count{};
    std::uint64_t maximum_frontier_width{};

    [[nodiscard]] std::uint64_t frontier_count() const noexcept {
        return frontier_offsets.empty()
            ? 0U
            : static_cast<std::uint64_t>(frontier_offsets.size() - 1U);
    }
};

/*
 * Exact cheap physical preflight over an already-declared semantic request DAG.
 * This deliberately does not calculate identities, geometry, trajectories,
 * presence, or persistence.  It asks the common execution planner how each
 * ready frontier would be chunked under the admitted conserved resource grant.
 *
 * The dependency plan remains a separate field so a change in CPU/memory/I/O
 * grant can change only physical work plans, never canonical dependencies.
 */
struct FrontierExecutionPlan final {
    FrontierPlan dependency_plan;
    std::vector<laplace_execution_work_plan> work_plans;
    std::uint64_t total_planned_items{};
    std::uint64_t total_chunk_count{};
    std::uint64_t maximum_frontier_peak_memory_bytes{};
    std::uint32_t worker_grant{};
    std::uint32_t maximum_outer_workers{};
};

[[nodiscard]] inline laplace_composition_status BuildFrontierPlan(
    const laplace_composition_working_set_input& input,
    FrontierPlan& output) noexcept {
    output = FrontierPlan{};

    if ((input.request_count != 0U && input.requests == nullptr) ||
        (input.operand_count != 0U && input.operands == nullptr) ||
        (input.known_entity_count != 0U && input.known_entities == nullptr) ||
        input.request_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        input.operand_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }

    try {
        const auto request_count = static_cast<std::size_t>(input.request_count);
        output.depth_by_request.assign(request_count, 0U);

        std::uint64_t maximum_depth = 0U;
        for (std::uint64_t request_index = 0U;
             request_index < input.request_count; ++request_index) {
            const auto request_offset = static_cast<std::size_t>(request_index);
            const auto& request = input.requests[request_offset];
            if (request.first_operand > input.operand_count ||
                request.operand_count > input.operand_count - request.first_operand) {
                output = FrontierPlan{};
                return LAPLACE_COMPOSITION_REFERENCE_INVALID;
            }

            std::uint64_t depth = 0U;
            const std::uint64_t operand_end =
                request.first_operand + request.operand_count;
            for (std::uint64_t operand_index = request.first_operand;
                 operand_index < operand_end; ++operand_index) {
                const auto& operand =
                    input.operands[static_cast<std::size_t>(operand_index)];
                if (operand.reference_kind ==
                    LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
                    if (operand.reference_index >= input.known_entity_count) {
                        output = FrontierPlan{};
                        return LAPLACE_COMPOSITION_REFERENCE_INVALID;
                    }
                    continue;
                }
                if (operand.reference_kind !=
                        LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT ||
                    operand.reference_index >= request_index) {
                    output = FrontierPlan{};
                    return LAPLACE_COMPOSITION_REFERENCE_INVALID;
                }

                if (output.dependency_edge_count ==
                    std::numeric_limits<std::uint64_t>::max()) {
                    output = FrontierPlan{};
                    return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
                }
                ++output.dependency_edge_count;

#if !defined(LAPLACE_TEST_COMPOSITION_FRONTIER_FLATTEN_DEPENDENCIES)
                const std::uint64_t parent_depth =
                    output.depth_by_request[
                        static_cast<std::size_t>(operand.reference_index)];
                if (parent_depth == std::numeric_limits<std::uint64_t>::max()) {
                    output = FrontierPlan{};
                    return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
                }
                depth = std::max(depth, parent_depth + 1U);
#endif
            }
            output.depth_by_request[request_offset] = depth;
            maximum_depth = std::max(maximum_depth, depth);
        }

        if (input.request_count == 0U) {
            output.frontier_offsets.assign(1U, 0U);
            return LAPLACE_COMPOSITION_OK;
        }
        if (maximum_depth == std::numeric_limits<std::uint64_t>::max() ||
            maximum_depth + 1U >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max() - 1U)) {
            output = FrontierPlan{};
            return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
        }

        const std::uint64_t frontier_count = maximum_depth + 1U;
        output.frontier_offsets.assign(
            static_cast<std::size_t>(frontier_count + 1U), 0U);
        for (const std::uint64_t request_depth : output.depth_by_request) {
            ++output.frontier_offsets[
                static_cast<std::size_t>(request_depth + 1U)];
        }
        for (std::size_t frontier = 1U;
             frontier < output.frontier_offsets.size(); ++frontier) {
            output.frontier_offsets[frontier] +=
                output.frontier_offsets[frontier - 1U];
        }

        std::vector<std::uint64_t> cursors = output.frontier_offsets;
        output.request_indices.assign(request_count, 0U);
        for (std::uint64_t request_index = 0U;
             request_index < input.request_count; ++request_index) {
            const std::uint64_t request_depth =
                output.depth_by_request[static_cast<std::size_t>(request_index)];
            auto& cursor = cursors[static_cast<std::size_t>(request_depth)];
            output.request_indices[static_cast<std::size_t>(cursor)] = request_index;
            ++cursor;
        }

        for (std::size_t frontier = 0U;
             frontier + 1U < output.frontier_offsets.size(); ++frontier) {
            const std::uint64_t width =
                output.frontier_offsets[frontier + 1U] -
                output.frontier_offsets[frontier];
            output.maximum_frontier_width =
                std::max(output.maximum_frontier_width, width);
        }
        return LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        output = FrontierPlan{};
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
}

[[nodiscard]] inline laplace_composition_status BuildFrontierExecutionPlan(
    const laplace_composition_working_set_input& input,
    FrontierExecutionPlan& output) noexcept {
    output = FrontierExecutionPlan{};
    if (input.context == nullptr) {
        return LAPLACE_COMPOSITION_CONTEXT_INVALID;
    }

    const laplace_composition_status dependency_status =
        BuildFrontierPlan(input, output.dependency_plan);
    if (dependency_status != LAPLACE_COMPOSITION_OK) {
        output = FrontierExecutionPlan{};
        return dependency_status;
    }

    output.worker_grant = input.context->resource_grant.cpu_slots;
    if (input.request_count == 0U) {
        return LAPLACE_COMPOSITION_OK;
    }

    try {
        const std::uint64_t frontier_count =
            output.dependency_plan.frontier_count();
        output.work_plans.reserve(static_cast<std::size_t>(frontier_count));
        for (std::uint64_t frontier = 0U;
             frontier < frontier_count; ++frontier) {
            const std::size_t offset = static_cast<std::size_t>(frontier);
            const std::uint64_t width =
                output.dependency_plan.frontier_offsets[offset + 1U] -
                output.dependency_plan.frontier_offsets[offset];
            if (width == 0U) {
                output = FrontierExecutionPlan{};
                return LAPLACE_COMPOSITION_REFERENCE_INVALID;
            }

            laplace_execution_work_request request{};
            request.item_count = width;
            request.minimum_chunk_items = 1U;
            request.outer_worker_limit = input.context->resource_grant.cpu_slots;
            request.inner_threads_per_worker = 1U;
            laplace_execution_work_plan plan{};
            const laplace_execution_status execution_status =
                laplace_execution_plan_work(
                    &input.context->resource_grant, &request, &plan);
            if (execution_status != LAPLACE_EXECUTION_OK) {
                output = FrontierExecutionPlan{};
                if (execution_status == LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT ||
                    execution_status == LAPLACE_EXECUTION_CAPACITY_INSUFFICIENT) {
                    return LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT;
                }
                if (execution_status == LAPLACE_EXECUTION_OVERFLOW) {
                    return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
                }
                if (execution_status == LAPLACE_EXECUTION_INVALID_ARGUMENT) {
                    return LAPLACE_COMPOSITION_CONTEXT_INVALID;
                }
                return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
            }

            if (plan.chunk_count >
                    std::numeric_limits<std::uint64_t>::max() -
                        output.total_chunk_count ||
                width >
                    std::numeric_limits<std::uint64_t>::max() -
                        output.total_planned_items) {
                output = FrontierExecutionPlan{};
                return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
            }
            output.total_chunk_count += plan.chunk_count;
            output.total_planned_items += width;
            output.maximum_frontier_peak_memory_bytes = std::max(
                output.maximum_frontier_peak_memory_bytes,
                plan.peak_memory_bytes);
            output.maximum_outer_workers = std::max(
                output.maximum_outer_workers,
                plan.outer_workers);
            output.work_plans.push_back(plan);
        }
        return LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        output = FrontierExecutionPlan{};
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
}

}  // namespace laplace::composition::detail

#endif