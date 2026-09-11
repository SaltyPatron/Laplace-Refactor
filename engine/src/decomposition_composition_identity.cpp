#include "laplace/decomposition_composition.h"

#include "laplace/identity.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace {

laplace_decomposition_composition_status Resolve(
    const laplace_decomposition_composition_plan_view& view,
    const laplace_decomposition_composition_identity* const results,
    const std::size_t result_count,
    const laplace_composition_operand& reference,
    laplace_decomposition_composition_identity* const identity) {
    if (identity == nullptr || reference.multiplicity == 0u || reference.flags != 0u) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    *identity = laplace_decomposition_composition_identity{};
    if (reference.reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
        if (reference.reference_index >= view.atom_count || view.atom_positions == nullptr) {
            return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
        }
        const std::uint32_t position =
            view.atom_positions[static_cast<std::size_t>(reference.reference_index)];
        if (laplace_identity_codepoint_witness(
                position, &identity->entity_id,
                &identity->identity_witness) != LAPLACE_IDENTITY_OK) {
            return LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID;
        }
        identity->logical_count = 1u;
        return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
    }
    if (reference.reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
        if (reference.reference_index >= result_count || results == nullptr) {
            return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
        }
        *identity = results[static_cast<std::size_t>(reference.reference_index)];
        return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
    }
    return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
}

laplace_decomposition_composition_status EvaluateView(
    const laplace_decomposition_composition_plan_view& view,
    laplace_decomposition_composition_identity* const results,
    const std::size_t result_capacity,
    std::size_t* const result_count) {
    if (result_count == nullptr) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    *result_count = 0u;
    if (view.request_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW;
    }
    const std::size_t required = static_cast<std::size_t>(view.request_count);
    *result_count = required;
    if (required == 0u) return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
    if (results == nullptr || result_capacity < required || view.requests == nullptr ||
        view.operands == nullptr ||
        (view.atom_count != 0u && view.atom_positions == nullptr)) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }

    try {
        for (std::size_t request_index = 0u; request_index < required; ++request_index) {
            const auto& request = view.requests[request_index];
            if (request.operand_count == 0u ||
                request.first_operand > view.operand_count ||
                request.operand_count > view.operand_count - request.first_operand ||
                request.operand_count > static_cast<std::uint64_t>(SIZE_MAX)) {
                return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
            }
            std::vector<laplace_id_run> runs;
            runs.reserve(static_cast<std::size_t>(request.operand_count));
            std::uint64_t logical_count = 0u;
            for (std::uint64_t offset = 0u; offset < request.operand_count; ++offset) {
                const auto& operand = view.operands[
                    static_cast<std::size_t>(request.first_operand + offset)];
                laplace_decomposition_composition_identity child{};
                const auto status = Resolve(
                    view, results, request_index, operand, &child);
                if (status != LAPLACE_DECOMPOSITION_COMPOSITION_OK) return status;
                if (operand.multiplicity >
                    std::numeric_limits<std::uint64_t>::max() - logical_count) {
                    return LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW;
                }
                logical_count += operand.multiplicity;
                runs.push_back(laplace_id_run{child.entity_id, operand.multiplicity});
            }
            auto& output = results[request_index];
            output = laplace_decomposition_composition_identity{};
            if (logical_count == 1u) {
                const auto& only = view.operands[
                    static_cast<std::size_t>(request.first_operand)];
                if (request.operand_count != 1u || only.multiplicity != 1u ||
                    only.relationship_metadata != 0u) {
                    return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
                }
                const auto status = Resolve(
                    view, results, request_index, only, &output);
                if (status != LAPLACE_DECOMPOSITION_COMPOSITION_OK) return status;
                output.logical_count = 1u;
                continue;
            }
            std::uint64_t identified_count = 0u;
            if (laplace_identity_composite_runs_witness(
                    runs.data(), runs.size(), nullptr, &identified_count,
                    &output.entity_id, &output.identity_witness) != LAPLACE_IDENTITY_OK ||
                identified_count != logical_count) {
                return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
            }
            output.logical_count = identified_count;
        }
        return LAPLACE_DECOMPOSITION_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE;
    }
}

}  // namespace

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_identity_evaluate(
    const laplace_decomposition_composition_plan* const plan,
    laplace_decomposition_composition_identity* const results,
    const size_t result_capacity,
    size_t* const result_count) {
    if (result_count != nullptr) *result_count = 0u;
    if (plan == nullptr || result_count == nullptr) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    laplace_decomposition_composition_plan_view view{};
    if (laplace_decomposition_composition_plan_view_get(plan, &view) !=
        LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
    }
    return EvaluateView(view, results, result_capacity, result_count);
}

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_identity_evaluate_view(
    const laplace_decomposition_composition_plan_view* const view,
    laplace_decomposition_composition_identity* const results,
    const size_t result_capacity,
    size_t* const result_count) {
    if (result_count != nullptr) *result_count = 0u;
    if (view == nullptr || result_count == nullptr) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    return EvaluateView(*view, results, result_capacity, result_count);
}

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_identity_resolve(
    const laplace_decomposition_composition_plan* const plan,
    const laplace_decomposition_composition_identity* const results,
    const size_t result_count,
    const laplace_composition_operand* const reference,
    laplace_decomposition_composition_identity* const identity) {
    if (plan == nullptr || reference == nullptr || identity == nullptr) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    laplace_decomposition_composition_plan_view view{};
    if (laplace_decomposition_composition_plan_view_get(plan, &view) !=
        LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID;
    }
    return Resolve(view, results, result_count, *reference, identity);
}

extern "C" laplace_decomposition_composition_status
laplace_decomposition_composition_identity_resolve_view(
    const laplace_decomposition_composition_plan_view* const view,
    const laplace_decomposition_composition_identity* const results,
    const size_t result_count,
    const laplace_composition_operand* const reference,
    laplace_decomposition_composition_identity* const identity) {
    if (view == nullptr || reference == nullptr || identity == nullptr) {
        return LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT;
    }
    return Resolve(*view, results, result_count, *reference, identity);
}
