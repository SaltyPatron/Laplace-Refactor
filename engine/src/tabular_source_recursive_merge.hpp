#ifndef LAPLACE_TABULAR_SOURCE_RECURSIVE_MERGE_HPP
#define LAPLACE_TABULAR_SOURCE_RECURSIVE_MERGE_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "laplace/decomposition_composition.h"
#include "laplace/tabular_source.h"

namespace laplace::internal {

inline laplace_tabular_source_status MergeRecursiveCanonicalComposition(
    std::vector<std::uint32_t>& destination_atoms,
    std::vector<laplace_composition_operand>& destination_operands,
    std::vector<laplace_composition_request>& destination_requests,
    const laplace_decomposition_composition_plan_view& source) {
#if defined(LAPLACE_TEST_TABULAR_RECURSIVE_DROP_CANONICAL_PLAN)
    (void)destination_atoms;
    (void)destination_operands;
    (void)destination_requests;
    (void)source;
    return LAPLACE_TABULAR_SOURCE_OK;
#else
    if (source.atom_count == 0u || source.atom_positions == nullptr ||
        (source.operand_count != 0u && source.operands == nullptr) ||
        (source.request_count != 0u && source.requests == nullptr) ||
        source.atom_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        source.operand_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        source.request_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        destination_atoms.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()) ||
        destination_operands.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()) ||
        destination_requests.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }

    const std::size_t source_atom_count =
        static_cast<std::size_t>(source.atom_count);
    const std::size_t source_operand_count =
        static_cast<std::size_t>(source.operand_count);
    const std::size_t source_request_count =
        static_cast<std::size_t>(source.request_count);
    if (source_atom_count > destination_atoms.max_size() - destination_atoms.size() ||
        source_operand_count >
            destination_operands.max_size() - destination_operands.size() ||
        source_request_count >
            destination_requests.max_size() - destination_requests.size()) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }

    const std::uint64_t operand_base =
        static_cast<std::uint64_t>(destination_operands.size());
    const std::uint64_t request_base =
        static_cast<std::uint64_t>(destination_requests.size());
    if (source.operand_count >
            std::numeric_limits<std::uint64_t>::max() - operand_base ||
        source.request_count >
            std::numeric_limits<std::uint64_t>::max() - request_base) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }

    std::vector<std::uint64_t> atom_indexes;
    atom_indexes.reserve(source_atom_count);
    for (std::size_t source_index = 0u;
         source_index < source_atom_count;
         ++source_index) {
        const std::uint32_t position = source.atom_positions[source_index];
        std::uint64_t destination_index =
            std::numeric_limits<std::uint64_t>::max();
        for (std::size_t candidate = 0u;
             candidate < destination_atoms.size();
             ++candidate) {
            if (destination_atoms[candidate] == position) {
                destination_index = static_cast<std::uint64_t>(candidate);
                break;
            }
        }
        if (destination_index == std::numeric_limits<std::uint64_t>::max()) {
            if (destination_atoms.size() >=
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
                return LAPLACE_TABULAR_SOURCE_OVERFLOW;
            }
            destination_index =
                static_cast<std::uint64_t>(destination_atoms.size());
            destination_atoms.push_back(position);
        }
        atom_indexes.push_back(destination_index);
    }

    destination_operands.reserve(
        destination_operands.size() + source_operand_count);
    for (std::size_t source_index = 0u;
         source_index < source_operand_count;
         ++source_index) {
        laplace_composition_operand operand = source.operands[source_index];
        if (operand.reference_kind ==
            LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
            if (operand.reference_index >= source.atom_count) {
                return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            }
            operand.reference_index =
                atom_indexes[static_cast<std::size_t>(operand.reference_index)];
        } else if (operand.reference_kind ==
                   LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
            if (operand.reference_index >= source.request_count) {
                return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            }
            operand.reference_index += request_base;
        } else {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
        destination_operands.push_back(operand);
    }

    destination_requests.reserve(
        destination_requests.size() + source_request_count);
    for (std::size_t source_index = 0u;
         source_index < source_request_count;
         ++source_index) {
        laplace_composition_request request = source.requests[source_index];
        if (request.operand_count == 0u ||
            request.first_operand > source.operand_count ||
            request.operand_count >
                source.operand_count - request.first_operand) {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
        const std::uint64_t operand_end =
            request.first_operand + request.operand_count;
        for (std::uint64_t operand_index = request.first_operand;
             operand_index < operand_end;
             ++operand_index) {
            const laplace_composition_operand& operand =
                source.operands[static_cast<std::size_t>(operand_index)];
            if (operand.reference_kind ==
                    LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT &&
                operand.reference_index >=
                    static_cast<std::uint64_t>(source_index)) {
                return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            }
        }
        request.first_operand += operand_base;
        if (request_base >
                std::numeric_limits<std::uint64_t>::max() -
                    static_cast<std::uint64_t>(source_index) - 1u) {
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        }
        request.source_ordinal =
            request_base + static_cast<std::uint64_t>(source_index) + 1u;
        destination_requests.push_back(request);
    }
    return LAPLACE_TABULAR_SOURCE_OK;
#endif
}

}  // namespace laplace::internal

#endif
