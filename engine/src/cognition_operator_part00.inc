#include "laplace/cognition_operator.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <tuple>
#include <vector>

namespace {

using DigestKey = std::array<std::uint8_t, 32>;

bool Zero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool Zero(const laplace_id128& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

DigestKey Key(const laplace_digest256& value) {
    DigestKey result{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), result.begin());
    return result;
}

bool DigestLess(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashF64(blake3_hasher* const hasher, const double value) {
    HashU64(hasher, std::bit_cast<std::uint64_t>(value));
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool ProgramValid(const laplace_cognition_operator_program& program) {
    if (Zero(program.program_id) || Zero(program.boundary_id) ||
        Zero(program.context_fingerprint) || Zero(program.evidence_epoch) ||
        Zero(program.result_contract_fingerprint) ||
        program.eligible_relation_families == nullptr ||
        program.eligible_relation_family_count == 0U ||
        program.eligible_source_mask == 0U ||
        (program.eligible_source_mask & ~UINT32_C(7)) != 0U ||
        (program.flags & ~LAPLACE_COGNITION_OPERATOR_PROGRAM_KNOWN_MASK) != 0U ||
        !std::isfinite(program.numeric_tolerance) || program.numeric_tolerance <= 0.0 ||
        program.version != LAPLACE_COGNITION_OPERATOR_VERSION ||
        program.reserved != 0U) {
        return false;
    }
    std::set<std::uint32_t> families;
    for (std::size_t index = 0U; index < program.eligible_relation_family_count; ++index) {
        if (program.eligible_relation_families[index] == 0U ||
            !families.insert(program.eligible_relation_families[index]).second) {
            return false;
        }
    }
    return true;
}

bool FieldValid(const laplace_cognition_operator_field& field) {
    if (Zero(field.field_id) || Zero(field.entity_id) ||
        Zero(field.recipe_fingerprint) || field.value_dimension != 1U ||
        (field.flags & ~LAPLACE_COGNITION_OPERATOR_FIELD_KNOWN_MASK) != 0U) {
        return false;
    }
    const bool container =
        (field.flags & LAPLACE_COGNITION_OPERATOR_FIELD_CONTAINER) != 0U;
    const bool role =
        (field.flags & LAPLACE_COGNITION_OPERATOR_FIELD_ROLE) != 0U;
    if (container && role) return false;
    if (role && (Zero(field.role_id) || Zero(field.physicality_id))) return false;
    if (container && Zero(field.physicality_id)) return false;
    return true;
}

bool SourceClassValid(const std::uint32_t value) {
    return value >= LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY &&
        value <= LAPLACE_COGNITION_OPERATOR_SOURCE_DERIVED;
}

bool DirectionValid(const std::uint32_t value) {
    return value >= LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET &&
        value <= LAPLACE_COGNITION_OPERATOR_DIRECTION_SYMMETRIC;
}

bool TransportValid(const std::uint32_t value) {
    return value >= LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY &&
        value <= LAPLACE_COGNITION_OPERATOR_TRANSPORT_CUSTOM;
}

bool ConstraintValid(
    const laplace_cognition_operator_program& program,
    const laplace_cognition_operator_constraint& constraint,
    const std::size_t field_count) {
    if (Zero(constraint.constraint_id) || Zero(constraint.plane_id) ||
        Zero(constraint.law_fingerprint) || Zero(constraint.units_fingerprint) ||
        Zero(constraint.calculation_receipt_id) ||
        constraint.source_field_index >= field_count ||
        constraint.target_field_index >= field_count ||
        constraint.source_field_index == constraint.target_field_index ||
        !std::isfinite(constraint.transport_scale) ||
        !std::isfinite(constraint.transport_offset) ||
        !std::isfinite(constraint.target_value) ||
        !std::isfinite(constraint.precision) || constraint.precision <= 0.0 ||
        constraint.relation_family == 0U ||
        !SourceClassValid(constraint.source_class) ||
        !DirectionValid(constraint.direction) || !TransportValid(constraint.transport_kind) ||
        (constraint.flags & ~LAPLACE_COGNITION_OPERATOR_CONSTRAINT_KNOWN_MASK) != 0U ||
        constraint.reserved != 0U) {
        return false;
    }
    if ((constraint.flags & LAPLACE_COGNITION_OPERATOR_CONSTRAINT_HAS_TARGET) == 0U &&
        constraint.target_value != 0.0) {
        return false;
    }
    if (constraint.source_class != LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY &&
        Zero(constraint.evidence_root_id)) {
        return false;
    }
    if (constraint.source_class == LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY &&
        !Zero(constraint.evidence_root_id)) {
        return false;
    }
    if (constraint.transport_kind == LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY &&
        (constraint.transport_scale != 1.0 || constraint.transport_offset != 0.0)) {
        return false;
    }
    if (constraint.transport_kind == LAPLACE_COGNITION_OPERATOR_TRANSPORT_SIGNED &&
        !(constraint.transport_scale == 1.0 || constraint.transport_scale == -1.0)) {
        return false;
    }
    const std::uint32_t source_bit = UINT32_C(1) << (constraint.source_class - 1U);
    if ((program.eligible_source_mask & source_bit) == 0U) return false;
    return std::find(
        program.eligible_relation_families,
        program.eligible_relation_families + program.eligible_relation_family_count,
        constraint.relation_family) !=
        program.eligible_relation_families + program.eligible_relation_family_count;
}

void HashField(
    blake3_hasher* const hasher,
    const laplace_cognition_operator_field& field) {
    HashDigest(hasher, field.field_id);
    HashId(hasher, field.entity_id);
    HashDigest(hasher, field.physicality_id);
    HashDigest(hasher, field.role_id);
    HashDigest(hasher, field.recipe_fingerprint);
    HashU64(hasher, field.ordinal);
    HashU32(hasher, field.value_dimension);
    HashU32(hasher, field.flags);
}
