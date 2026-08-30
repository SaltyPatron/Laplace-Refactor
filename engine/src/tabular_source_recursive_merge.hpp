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
    const std::uint64_t request_insert_index,
    const laplace_decomposition_composition_plan_view& source) {
#if defined(LAPLACE_TEST_TABULAR_RECURSIVE_DROP_CANONICAL_PLAN)
    (void)destination_atoms;
    (void)destination_operands;
    (void)destination_requests;
    (void)request_insert_index;
    (void)source;
    return LAPLACE_TABULAR_SOURCE_OK;
#else
    if (source.atom_count == 0u || source.atom_positions == nullptr ||
        (source.operand_count != 0u && source.operands == nullptr) ||
        (source.request_count != 0u && source.requests == nullptr) ||
        source.atom_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        source.operand_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        source.request_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        destination_requests.empty() ||
        request_insert_index !=
            static_cast<std::uint64_t>(destination_requests.size() - 1u) ||
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
    const std::uint64_t request_base = request_insert_index;
    if (source.operand_count >
            std::numeric_limits<std::uint64_t>::max() - operand_base ||
        source.request_count >
            std::numeric_limits<std::uint64_t>::max() - request_base) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }

    for (std::size_t source_index = 0u;
         source_index < source_operand_count;
         ++source_index) {
        const laplace_composition_operand& operand = source.operands[source_index];
        if (operand.multiplicity != 1u ||
            operand.relationship_metadata != 0u || operand.flags != 0u) {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
        if (operand.reference_kind ==
            LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
            if (operand.reference_index >= source.atom_count) {
                return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            }
        } else if (operand.reference_kind ==
                   LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
            if (operand.reference_index >= source.request_count) {
                return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            }
        } else {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
    }

    for (std::size_t source_index = 0u;
         source_index < source_request_count;
         ++source_index) {
        const laplace_composition_request& request = source.requests[source_index];
        if (request.operand_count == 0u || request.flags != 0u ||
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
            operand.reference_index =
                atom_indexes[static_cast<std::size_t>(operand.reference_index)];
        } else {
            operand.reference_index += request_base;
        }
        destination_operands.push_back(operand);
    }

    std::vector<laplace_composition_request> merged_requests;
    merged_requests.reserve(destination_requests.size() + source_request_count);
    merged_requests.insert(
        merged_requests.end(),
        destination_requests.begin(),
        destination_requests.begin() +
            static_cast<std::ptrdiff_t>(request_insert_index));
    for (std::size_t source_index = 0u;
         source_index < source_request_count;
         ++source_index) {
        laplace_composition_request request = source.requests[source_index];
        request.first_operand += operand_base;
        request.source_ordinal =
            request_base + static_cast<std::uint64_t>(source_index) + 1u;
        merged_requests.push_back(request);
    }
    laplace_composition_request final_root = destination_requests.back();
    final_root.source_ordinal =
        request_base + source.request_count + 1u;
    merged_requests.push_back(final_root);
    destination_requests.swap(merged_requests);
    return LAPLACE_TABULAR_SOURCE_OK;
#endif
}

}  // namespace laplace::internal

#endif
