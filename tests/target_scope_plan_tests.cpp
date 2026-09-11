#include "laplace/target_scope_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 ScopeDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 ScopeId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool ScopeSame(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field ScopeField(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = ScopeDigest(seed);
    field.entity_id = ScopeId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = ScopeDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint ScopeConstraint(
    const std::uint8_t seed,
    const std::uint32_t family,
    const std::uint64_t source,
    const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = ScopeDigest(seed);
    constraint.plane_id = ScopeDigest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = ScopeDigest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = ScopeDigest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id =
        ScopeDigest(static_cast<std::uint8_t>(seed + 80U));
    constraint.source_field_index = source;
    constraint.target_field_index = target;
    constraint.transport_scale = 1.0;
    constraint.precision = precision;
    constraint.relation_family = family;
    constraint.source_class = LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY;
    constraint.direction = LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET;
    constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
    return constraint;
}

struct ScopeCompileHandle {
    laplace_target_compile_result* value{};
    ~ScopeCompileHandle() { laplace_target_compile_result_destroy(&value); }
};

laplace_target_scope_plan_request ScopeRequest(
    const std::vector<laplace_cognition_operator_field>& fields,
    const std::vector<laplace_cognition_operator_constraint>& constraints,
    const laplace_target_scope_slot_spec* slots,
    const std::size_t slot_count) {
    laplace_target_scope_plan_request request{};
    request.evidence_boundary = ScopeDigest(10U);
    request.evidence_epoch = ScopeDigest(11U);
    request.recipe_fingerprint = ScopeDigest(12U);
    request.target_contract_fingerprint = ScopeDigest(13U);
    request.context_fingerprint = ScopeDigest(14U);
    request.fields = fields.data();
    request.field_count = fields.size();
    request.constraints = constraints.data();
    request.constraint_count = constraints.size();
    request.slots = slots;
    request.slot_count = slot_count;
    request.numeric_tolerance = 1e-12;
    request.operator_program_flags =
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_POSITIVE_SEMIDEFINITE_PRECISION |
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_RELATION_PLANE_SEPARATION |
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_MATRIX_FREE_MATERIALIZED_PARITY;
    request.target_compile_flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    request.version = LAPLACE_TARGET_SCOPE_PLAN_VERSION;
    return request;
}

TEST(TargetScopePlan, GeneratesDistinctQkVoJobsFromOneTypedEstate) {
    const std::vector fields{
        ScopeField(40U, 0U), ScopeField(41U, 1U), ScopeField(42U, 2U)};
    const std::vector constraints{
        ScopeConstraint(50U, 11U, 0U, 1U, 2.0),
        ScopeConstraint(60U, 12U, 1U, 2U, 3.0)};
    const std::array<std::uint32_t, 1> qk_families{{11U}};
    const std::array<std::uint32_t, 1> vo_families{{12U}};
    std::array<laplace_target_scope_slot_spec, 2> slots{};
    slots[0].target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    slots[0].layer_index = 2U;
    slots[0].head_index = 1U;
    slots[0].eligible_relation_families = qk_families.data();
    slots[0].eligible_relation_family_count = qk_families.size();
    slots[0].eligible_source_mask = 1U;
    slots[0].head_rank = 2U;
    slots[1].target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    slots[1].layer_index = 2U;
    slots[1].head_index = 1U;
    slots[1].eligible_relation_families = vo_families.data();
    slots[1].eligible_relation_family_count = vo_families.size();
    slots[1].eligible_source_mask = 1U;
    slots[1].head_rank = 2U;

    auto request = ScopeRequest(fields, constraints, slots.data(), slots.size());
    ScopeCompileHandle result;
    laplace_target_compile_receipt compile{};
    laplace_target_scope_plan_receipt scope{};
    ASSERT_EQ(
        laplace_target_scope_plan_compile(&request, &result.value, &compile, &scope),
        LAPLACE_TARGET_SCOPE_PLAN_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(scope.status, LAPLACE_TARGET_SCOPE_PLAN_OK);
    EXPECT_EQ(scope.compile_status, LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(scope.slot_count, 2U);
    EXPECT_EQ(scope.field_count, 3U);
    EXPECT_EQ(scope.constraint_count, 2U);
    EXPECT_EQ(scope.selected_constraint_count, 2U);
    EXPECT_EQ(scope.source_mask_union, 1U);
    EXPECT_TRUE(ScopeSame(scope.compile_receipt_id, compile.receipt_id));
    EXPECT_TRUE(ScopeSame(scope.compile_request_fingerprint, compile.request_fingerprint));

    laplace_target_compile_slot_receipt qk{};
    laplace_target_compile_slot_receipt vo{};
    ASSERT_EQ(
        laplace_target_compile_result_slot_receipt(result.value, 0U, &qk),
        LAPLACE_TARGET_COMPILE_OK);
    ASSERT_EQ(
        laplace_target_compile_result_slot_receipt(result.value, 1U, &vo),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(qk.target_role, LAPLACE_TARGET_ROLE_COMPATIBILITY_QK);
    EXPECT_EQ(vo.target_role, LAPLACE_TARGET_ROLE_CONTRIBUTION_VO);
    EXPECT_EQ(qk.selected_constraint_count, 1U);
    EXPECT_EQ(vo.selected_constraint_count, 1U);
    EXPECT_FALSE(ScopeSame(qk.role_fingerprint, vo.role_fingerprint));
    EXPECT_FALSE(ScopeSame(qk.operator_id, vo.operator_id));
    EXPECT_FALSE(ScopeSame(qk.matrix_fingerprint, vo.matrix_fingerprint));
}

TEST(TargetScopePlan, CanonicalizesRelationFamilySetBeforeProgramIdentity) {
    const std::vector fields{ScopeField(40U, 0U), ScopeField(41U, 1U)};
    const std::vector constraints{
        ScopeConstraint(50U, 11U, 0U, 1U, 2.0),
        ScopeConstraint(51U, 13U, 0U, 1U, 4.0)};
    const std::array<std::uint32_t, 2> forward_families{{11U, 13U}};
    const std::array<std::uint32_t, 2> reverse_families{{13U, 11U}};
    laplace_target_scope_slot_spec forward{};
    forward.target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    forward.layer_index = 1U;
    forward.eligible_relation_families = forward_families.data();
    forward.eligible_relation_family_count = forward_families.size();
    forward.eligible_source_mask = 1U;
    forward.head_rank = 1U;
    laplace_target_scope_slot_spec reverse = forward;
    reverse.eligible_relation_families = reverse_families.data();

    auto first_request = ScopeRequest(fields, constraints, &forward, 1U);
    auto second_request = ScopeRequest(fields, constraints, &reverse, 1U);
    first_request.target_compile_flags = 0U;
    second_request.target_compile_flags = 0U;

    ScopeCompileHandle first;
    ScopeCompileHandle second;
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
    EXPECT_TRUE(ScopeSame(first_scope.request_fingerprint, second_scope.request_fingerprint));
    EXPECT_TRUE(ScopeSame(first_scope.receipt_id, second_scope.receipt_id));
    EXPECT_TRUE(ScopeSame(first_compile.receipt_id, second_compile.receipt_id));
}

TEST(TargetScopePlan, RejectsSelectorWithNoMatchingTypedConstraint) {
    const std::vector fields{ScopeField(40U, 0U), ScopeField(41U, 1U)};
    const std::vector constraints{ScopeConstraint(50U, 11U, 0U, 1U, 2.0)};
    const std::array<std::uint32_t, 1> families{{99U}};
    laplace_target_scope_slot_spec slot{};
    slot.target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    slot.eligible_relation_families = families.data();
    slot.eligible_relation_family_count = families.size();
    slot.eligible_source_mask = 1U;
    slot.head_rank = 1U;
    auto request = ScopeRequest(fields, constraints, &slot, 1U);
    request.target_compile_flags = 0U;

    ScopeCompileHandle result;
    laplace_target_compile_receipt compile{};
    laplace_target_scope_plan_receipt scope{};
    EXPECT_EQ(
        laplace_target_scope_plan_compile(&request, &result.value, &compile, &scope),
        LAPLACE_TARGET_SCOPE_PLAN_NO_MATCHING_CONSTRAINTS);
    EXPECT_EQ(result.value, nullptr);
    EXPECT_EQ(scope.status, LAPLACE_TARGET_SCOPE_PLAN_NO_MATCHING_CONSTRAINTS);
}

TEST(TargetScopePlan, TargetRankChangesScopeReceiptWithoutRewritingOperator) {
    const std::vector fields{
        ScopeField(40U, 0U), ScopeField(41U, 1U), ScopeField(42U, 2U)};
    const std::vector constraints{ScopeConstraint(50U, 11U, 0U, 1U, 2.0)};
    const std::array<std::uint32_t, 1> families{{11U}};
    laplace_target_scope_slot_spec rank_one{};
    rank_one.target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    rank_one.layer_index = 1U;
    rank_one.head_index = 4U;
    rank_one.eligible_relation_families = families.data();
    rank_one.eligible_relation_family_count = families.size();
    rank_one.eligible_source_mask = 1U;
    rank_one.head_rank = 1U;
    laplace_target_scope_slot_spec rank_two = rank_one;
    rank_two.head_rank = 2U;

    auto first_request = ScopeRequest(fields, constraints, &rank_one, 1U);
    auto second_request = ScopeRequest(fields, constraints, &rank_two, 1U);
    first_request.target_compile_flags = 0U;
    second_request.target_compile_flags = 0U;
    ScopeCompileHandle first;
    ScopeCompileHandle second;
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
    EXPECT_TRUE(ScopeSame(first_compile.receipt_id, second_compile.receipt_id));
    EXPECT_FALSE(ScopeSame(first_scope.request_fingerprint, second_scope.request_fingerprint));
    EXPECT_FALSE(ScopeSame(first_scope.receipt_id, second_scope.receipt_id));
}

}  // namespace
