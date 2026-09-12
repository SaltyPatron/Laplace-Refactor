#include "laplace/target_scope_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 IdentityDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 IdentityId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool IdentitySame(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field IdentityField(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = IdentityDigest(seed);
    field.entity_id = IdentityId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = IdentityDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint IdentityConstraint(const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = IdentityDigest(50U);
    constraint.plane_id = IdentityDigest(60U);
    constraint.law_fingerprint = IdentityDigest(70U);
    constraint.units_fingerprint = IdentityDigest(80U);
    constraint.calculation_receipt_id = IdentityDigest(90U);
    constraint.source_field_index = 0U;
    constraint.target_field_index = 1U;
    constraint.transport_scale = 1.0;
    constraint.precision = precision;
    constraint.relation_family = 11U;
    constraint.source_class = LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY;
    constraint.direction = LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET;
    constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
    return constraint;
}

struct IdentityCompileHandle {
    laplace_target_compile_result* value{};
    ~IdentityCompileHandle() { laplace_target_compile_result_destroy(&value); }
};

laplace_target_scope_plan_request IdentityRequest(
    const std::vector<laplace_cognition_operator_field>& fields,
    const std::vector<laplace_cognition_operator_constraint>& constraints,
    const laplace_target_scope_slot_spec* slot) {
    laplace_target_scope_plan_request request{};
    request.evidence_boundary = IdentityDigest(10U);
    request.evidence_epoch = IdentityDigest(11U);
    request.recipe_fingerprint = IdentityDigest(12U);
    request.target_contract_fingerprint = IdentityDigest(13U);
    request.context_fingerprint = IdentityDigest(14U);
    request.fields = fields.data();
    request.field_count = fields.size();
    request.constraints = constraints.data();
    request.constraint_count = constraints.size();
    request.slots = slot;
    request.slot_count = 1U;
    request.numeric_tolerance = 1e-12;
    request.operator_program_flags =
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_POSITIVE_SEMIDEFINITE_PRECISION |
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_RELATION_PLANE_SEPARATION |
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_MATRIX_FREE_MATERIALIZED_PARITY;
    request.target_compile_flags = 0U;
    request.version = LAPLACE_TARGET_SCOPE_PLAN_VERSION;
    return request;
}

TEST(TargetScopeIdentity, RequestFingerprintBindsTypedEstateValues) {
    const std::vector fields{IdentityField(40U, 0U), IdentityField(41U, 1U)};
    const std::vector first_constraints{IdentityConstraint(2.0)};
    const std::vector second_constraints{IdentityConstraint(3.0)};
    const std::array<std::uint32_t, 1> families{{11U}};
    laplace_target_scope_slot_spec slot{};
    slot.target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    slot.layer_index = 1U;
    slot.head_index = 2U;
    slot.eligible_relation_families = families.data();
    slot.eligible_relation_family_count = families.size();
    slot.eligible_source_mask = 1U;
    slot.head_rank = 1U;

    auto first_request = IdentityRequest(fields, first_constraints, &slot);
    auto second_request = IdentityRequest(fields, second_constraints, &slot);
    IdentityCompileHandle first;
    IdentityCompileHandle second;
    laplace_target_compile_receipt first_compile{};
    laplace_target_compile_receipt second_compile{};
    laplace_target_scope_plan_receipt first_scope{};
    laplace_target_scope_plan_receipt second_scope{};

    ASSERT_EQ(
        laplace_target_scope_plan_compile(
            &first_request, &first.value, &first_compile, &first_scope),
        LAPLACE_TARGET_SCOPE_PLAN_OK);
    ASSERT_EQ(
        laplace_target_scope_plan_compile(
            &second_request, &second.value, &second_compile, &second_scope),
        LAPLACE_TARGET_SCOPE_PLAN_OK);

    EXPECT_TRUE(IdentitySame(first_scope.slot_set_fingerprint,
                             second_scope.slot_set_fingerprint));
    EXPECT_FALSE(IdentitySame(first_scope.request_fingerprint,
                              second_scope.request_fingerprint));
    EXPECT_FALSE(IdentitySame(first_scope.receipt_id, second_scope.receipt_id));
    EXPECT_FALSE(IdentitySame(first_compile.request_fingerprint,
                              second_compile.request_fingerprint));
}

}  // namespace
