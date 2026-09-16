#ifndef LAPLACE_CANONICAL_COMPOSITION_PLAN_HPP
#define LAPLACE_CANONICAL_COMPOSITION_PLAN_HPP

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "laplace/composition.h"

namespace laplace::detail {

inline constexpr std::string_view CanonicalNumberTag{"number"};

// Shared native lowering of exact Unicode strings, canonical unsigned decimal
// numerals and typed ordered records into the ordinary composition working set.
// Roles are trajectory metadata; the identity kernel still sees only children.
inline bool DecodeUtf8(const std::string_view input, std::vector<std::uint32_t>& output) {
    std::size_t offset = 0U;
    while (offset < input.size()) {
        const auto first = static_cast<std::uint8_t>(input[offset]);
        std::uint32_t value{};
        std::size_t length{};
        std::uint32_t minimum{};
        if (first <= 0x7fU) {
            value = first;
            length = 1U;
            minimum = 0U;
        } else if ((first & 0xe0U) == 0xc0U) {
            value = first & 0x1fU;
            length = 2U;
            minimum = 0x80U;
        } else if ((first & 0xf0U) == 0xe0U) {
            value = first & 0x0fU;
            length = 3U;
            minimum = 0x800U;
        } else if ((first & 0xf8U) == 0xf0U) {
            value = first & 0x07U;
            length = 4U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (offset + length > input.size()) {
            return false;
        }
        for (std::size_t index = 1U; index < length; ++index) {
            const auto continuation =
                static_cast<std::uint8_t>(input[offset + index]);
            if ((continuation & 0xc0U) != 0x80U) {
                return false;
            }
            value = (value << 6U) | (continuation & 0x3fU);
        }
        if (value < minimum || value > 0x10ffffU ||
            (value >= 0xd800U && value <= 0xdfffU)) {
            return false;
        }
        output.push_back(value);
        offset += length;
    }
    return !output.empty();
}

inline std::string Decimal(const std::uint64_t value) {
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec != std::errc{}) {
        return {};
    }
    return std::string(buffer.data(), converted.ptr);
}


template <class Role>
class CanonicalCompositionPlanBuilder {
public:
    CanonicalCompositionPlanBuilder(
        std::vector<std::uint32_t>& atom_positions,
        std::vector<laplace_composition_operand>& operands,
        std::vector<laplace_composition_request>& requests,
        const laplace_digest256& recipe_fingerprint,
        const laplace_digest256& geometry_epoch,
        const laplace_digest256& occurrence_context,
        std::uint32_t recipe_version, std::uint32_t request_flags,
        std::uint64_t role_shift, std::uint64_t known_prefix = 0U,
        std::uint64_t maximum_requests = UINT64_MAX,
        std::uint64_t maximum_operands = UINT64_MAX)
        : atom_positions_(atom_positions), operands_(operands), requests_(requests),
          recipe_fingerprint_(recipe_fingerprint), geometry_epoch_(geometry_epoch),
          occurrence_context_(occurrence_context), recipe_version_(recipe_version),
          request_flags_(request_flags), role_shift_(role_shift),
          known_prefix_(known_prefix), maximum_requests_(maximum_requests),
          maximum_operands_(maximum_operands) {}

protected:
    struct StringResult {
        std::uint64_t index{};
        bool has_value{};
    };

    static constexpr std::uint64_t InvalidIndex =
        std::numeric_limits<std::uint64_t>::max();

    std::uint64_t AtomIndex(const std::uint32_t position) {
        const auto prior = atom_indexes_.find(position);
        if (prior != atom_indexes_.end()) {
            return prior->second;
        }
        if (atom_positions_.size() >= UINT64_MAX - known_prefix_) return InvalidIndex;
        const auto index = known_prefix_ +
            static_cast<std::uint64_t>(atom_positions_.size());
        atom_positions_.push_back(position);
        atom_indexes_.emplace(position, index);
        return index;
    }

    StringResult String(const std::string_view value) {
        const auto prior = string_indexes_.find(std::string(value));
        if (prior != string_indexes_.end()) {
            return {prior->second, true};
        }
        std::vector<std::uint32_t> positions;
        if (!DecodeUtf8(value, positions)) {
            return {};
        }
        const std::uint64_t first =
            static_cast<std::uint64_t>(operands_.size());
        if (first > maximum_operands_ || positions.size() > maximum_operands_ - first)
            return {};
        for (const auto position : positions) {
            const auto atom = AtomIndex(position);
            if (atom == InvalidIndex) return {};
            operands_.push_back(laplace_composition_operand{
                atom, 1U, 0U,
                LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U});
        }
        const std::uint64_t request = AddRequest(first, positions.size());
        if (request == InvalidIndex) {
            return {};
        }
        string_indexes_.emplace(std::string(value), request);
        return {request, true};
    }

    std::uint64_t Number(
        const std::uint64_t value,
        const std::uint64_t number_tag) {
        const auto prior = number_indexes_.find(value);
        if (prior != number_indexes_.end()) {
            return prior->second;
        }
        const std::string digits = Decimal(value);
        const auto surface = String(digits);
        if (digits.empty() || !surface.has_value) {
            return InvalidIndex;
        }
        const auto result = Node({
            {number_tag, Role::Tag},
            {surface.index, Role::Identifier}});
        if (result != InvalidIndex) {
            number_indexes_.emplace(value, result);
        }
        return result;
    }

    std::uint64_t Node(
        const std::vector<std::pair<std::uint64_t, Role>>& children) {
        if (children.size() < 2U) {
            return InvalidIndex;
        }
        const std::uint64_t first =
            static_cast<std::uint64_t>(operands_.size());
        if (first > maximum_operands_ || children.size() > maximum_operands_ - first)
            return InvalidIndex;
        for (const auto& [index, role] : children) {
            if (index >= requests_.size()) {
                return InvalidIndex;
            }
            operands_.push_back(laplace_composition_operand{
                index, 1U, (static_cast<std::uint64_t>(role) << role_shift_),
                LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT, 0U});
        }
        return AddRequest(first, children.size());
    }

    std::uint64_t NodeReferences(
        const std::vector<laplace_composition_operand>& children) {
        if (children.size() < 2U || operands_.size() > maximum_operands_ ||
            children.size() > maximum_operands_ - operands_.size()) return InvalidIndex;
        for (const auto& child : children) {
            if (child.multiplicity == 0U || child.flags != 0U ||
                (child.reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT
                    ? child.reference_index >= requests_.size()
                    : child.reference_kind != LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY ||
                        child.reference_index >= known_prefix_ + atom_positions_.size()))
                return InvalidIndex;
        }
        const auto first = static_cast<std::uint64_t>(operands_.size());
        operands_.insert(operands_.end(), children.begin(), children.end());
        return AddRequest(first, children.size());
    }

    std::uint64_t AddRequest(
        const std::uint64_t first_operand,
        const std::size_t operand_count) {
        if (operand_count == 0U || requests_.size() >= maximum_requests_ ||
            requests_.size() == UINT64_MAX) {
            return InvalidIndex;
        }
        const auto index =
            static_cast<std::uint64_t>(requests_.size());
        requests_.push_back(laplace_composition_request{
            first_operand,
            static_cast<std::uint64_t>(operand_count),
            index + 1U,
            recipe_version_,
            request_flags_,
            recipe_fingerprint_,
            geometry_epoch_,
            occurrence_context_});
        return index;
    }


private:
    std::vector<std::uint32_t>& atom_positions_;
    std::vector<laplace_composition_operand>& operands_;
    std::vector<laplace_composition_request>& requests_;
    const laplace_digest256& recipe_fingerprint_;
    const laplace_digest256& geometry_epoch_;
    const laplace_digest256& occurrence_context_;
    std::uint32_t recipe_version_;
    std::uint32_t request_flags_;
    std::uint64_t role_shift_;
    std::uint64_t known_prefix_;
    std::uint64_t maximum_requests_;
    std::uint64_t maximum_operands_;
    std::map<std::uint32_t, std::uint64_t> atom_indexes_;
    std::map<std::string, std::uint64_t> string_indexes_;
    std::map<std::uint64_t, std::uint64_t> number_indexes_;
};

}  // namespace laplace::detail
#endif
