#include "laplace/cognition_operator.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 Id(std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field Field(
    std::uint8_t seed,
    std::uint64_t ordinal,
    std::uint32_t flags = 0U,
    std::uint8_t role_seed = 0U) {
    laplace_cognition_operator_field field{};
    field.field_id = Digest(seed);
    field.entity_id = Id(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = Digest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    field.flags = flags;
    if ((flags & (LAPLACE_COGNITION_OPERATOR_FIELD_CONTAINER |
                  LAPLACE_COGNITION_OPERATOR_FIELD_ROLE)) != 0U) {
        field.physicality_id = Digest(static_cast<std::uint8_t>(seed + 100U));
    }
    if ((flags & LAPLACE_COGNITION_OPERATOR_FIELD_ROLE) != 0U) {
        field.role_id = Digest(role_seed);
    }
    return field;
}

laplace_cognition_operator_constraint Constraint(
    std::uint8_t id,
    std::uint32_t family,
    std::uint64_t source,
    std::uint64_t target,
    std::uint32_t source_class = LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY,
    std::uint8_t evidence_root = 0U,
    double precision = 2.0,
    double target_value = 0.0) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = Digest(id);
    constraint.plane_id = Digest(static_cast<std::uint8_t>(id + 20U));
    constraint.law_fingerprint = Digest(static_cast<std::uint8_t>(id + 40U));
    constraint.units_fingerprint = Digest(static_cast<std::uint8_t>(id + 60U));
    if (source_class != LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY) {
        constraint.evidence_root_id = Digest(evidence_root);
    }
    constraint.calculation_receipt_id = Digest(static_cast<std::uint8_t>(id + 80U));
    constraint.source_field_index = source;
    constraint.target_field_index = target;
    constraint.transport_scale = 1.0;
    constraint.transport_offset = 0.0;
    constraint.target_value = target_value;
    constraint.precision = precision;
    constraint.relation_family = family;
    constraint.source_class = source_class;
    constraint.direction = LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET;
    constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
    if (target_value != 0.0) {
        constraint.flags |= LAPLACE_COGNITION_OPERATOR_CONSTRAINT_HAS_TARGET;
    }
    return constraint;
}

struct ProgramFixture {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};
    ProgramFixture() {
        value.program_id = Digest(1U);
        value.boundary_id = Digest(2U);
        value.context_fingerprint = Digest(3U);
        value.evidence_epoch = Digest(4U);
        value.result_contract_fingerprint = Digest(5U);
        value.eligible_relation_families = families.data();
        value.eligible_relation_family_count = families.size();
        value.eligible_source_mask = 7U;
        value.flags =
            LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_POSITIVE_SEMIDEFINITE_PRECISION |
            LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_RELATION_PLANE_SEPARATION |
            LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_MATRIX_FREE_MATERIALIZED_PARITY;
        value.numeric_tolerance = 1e-12;
        value.version = LAPLACE_COGNITION_OPERATOR_VERSION;
    }
};

class OperatorHandle {
public:
    OperatorHandle() = default;
    OperatorHandle(const OperatorHandle&) = delete;
    OperatorHandle& operator=(const OperatorHandle&) = delete;
    OperatorHandle(OperatorHandle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    ~OperatorHandle() { laplace_cognition_operator_destroy(&value); }
    laplace_cognition_operator* value{};
};

OperatorHandle Build(
    ProgramFixture* program,
    const std::vector<laplace_cognition_operator_field>& fields,
    const std::vector<laplace_cognition_operator_constraint>& constraints,
    laplace_cognition_operator_receipt* receipt) {
    OperatorHandle result;
    EXPECT_EQ(laplace_cognition_operator_create(
                  &program->value, fields.data(), fields.size(),
                  constraints.data(), constraints.size(), &result.value, receipt),
              LAPLACE_COGNITION_OPERATOR_OK);
    return result;
}

TEST(CognitionOperator, RelationPlaneTypeErasureChangesIdentity) {
    ProgramFixture program;
    const std::vector fields{Field(20U, 0U), Field(21U, 1U)};
    auto first_constraint = Constraint(30U, 11U, 0U, 1U);
    auto second_constraint = first_constraint;
    second_constraint.plane_id = Digest(90U);
    second_constraint.law_fingerprint = Digest(91U);
    second_constraint.units_fingerprint = Digest(92U);
    second_constraint.relation_family = 12U;
    second_constraint.direction = LAPLACE_COGNITION_OPERATOR_DIRECTION_SYMMETRIC;
    second_constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_SEMANTIC;
    laplace_cognition_operator_receipt first_receipt{};
    laplace_cognition_operator_receipt second_receipt{};
    auto first = Build(&program, fields, {first_constraint}, &first_receipt);
    auto second = Build(&program, fields, {second_constraint}, &second_receipt);
    EXPECT_FALSE(Same(first_receipt.operator_id, second_receipt.operator_id));
}

TEST(CognitionOperator, PreservesNaryContainerRoleMetadata) {
    ProgramFixture program;
    const std::vector first_fields{
        Field(20U, 0U, LAPLACE_COGNITION_OPERATOR_FIELD_CONTAINER),
        Field(21U, 1U, LAPLACE_COGNITION_OPERATOR_FIELD_ROLE, 70U),
        Field(22U, 2U, LAPLACE_COGNITION_OPERATOR_FIELD_ROLE, 71U)};
    auto second_fields = first_fields;
    second_fields[1].role_id = Digest(72U);
    second_fields[1].ordinal = 2U;
    second_fields[2].ordinal = 1U;
    const std::vector constraints{
        Constraint(30U, 11U, 0U, 1U),
        Constraint(31U, 11U, 0U, 2U)};
    laplace_cognition_operator_receipt first_receipt{};
    laplace_cognition_operator_receipt second_receipt{};
    auto first = Build(&program, first_fields, constraints, &first_receipt);
    auto second = Build(&program, second_fields, constraints, &second_receipt);
    EXPECT_FALSE(Same(first_receipt.field_set_fingerprint,
                      second_receipt.field_set_fingerprint));
    EXPECT_FALSE(Same(first_receipt.operator_id, second_receipt.operator_id));
}

TEST(CognitionOperator, CollapsesDependentEvidenceRootCopies) {
    ProgramFixture program;
    const std::vector fields{Field(20U, 0U), Field(21U, 1U)};
    auto first = Constraint(
        30U, 11U, 0U, 1U, LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY, 80U);
    auto copy = first;
    copy.constraint_id = Digest(31U);
    copy.calculation_receipt_id = Digest(99U);
    laplace_cognition_operator_receipt receipt{};
    auto value = Build(&program, fields, {first, copy}, &receipt);
    EXPECT_EQ(laplace_cognition_operator_constraint_count(value.value), 1U);
    EXPECT_EQ(receipt.selected_constraint_count, 1U);
    EXPECT_EQ(receipt.deduplicated_dependent_count, 1U);
    EXPECT_EQ(receipt.testimony_constraint_count, 1U);
}

TEST(CognitionOperator, KeepsContradictionAsPositivePrecisionConstraints) {
    ProgramFixture program;
    const std::vector fields{Field(20U, 0U), Field(21U, 1U)};
    auto positive = Constraint(
        30U, 11U, 0U, 1U, LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY,
        80U, 3.0, 1.0);
    auto negative = Constraint(
        31U, 11U, 0U, 1U, LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY,
        81U, 4.0, -1.0);
    negative.plane_id = positive.plane_id;
    negative.law_fingerprint = positive.law_fingerprint;
    negative.units_fingerprint = positive.units_fingerprint;
    laplace_cognition_operator_receipt receipt{};
    auto value = Build(&program, fields, {positive, negative}, &receipt);
    const std::array<double, 2> input{{0.25, -0.5}};
    double energy = -1.0;
    ASSERT_EQ(laplace_cognition_operator_energy(
                  value.value, input.data(), input.size(), &energy),
              LAPLACE_COGNITION_OPERATOR_OK);
    EXPECT_TRUE(std::isfinite(energy));
    EXPECT_GE(energy, 0.0);
    EXPECT_EQ(receipt.selected_constraint_count, 2U);
}

TEST(CognitionOperator, MatrixFreeAndMaterializedDenseAgree) {
    ProgramFixture program;
    const std::vector fields{Field(20U, 0U), Field(21U, 1U), Field(22U, 2U)};
    auto c0 = Constraint(30U, 11U, 0U, 1U);
    auto c1 = Constraint(
        31U, 12U, 1U, 2U, LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY,
        81U, 1.5, 2.0);
    c1.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_AFFINE;
    c1.transport_scale = -0.5;
    c1.transport_offset = 0.75;
    laplace_cognition_operator_receipt operator_receipt{};
    auto value = Build(&program, fields, {c0, c1}, &operator_receipt);
    const std::array<double, 3> input{{1.25, -2.0, 0.5}};
    std::array<double, 3> matrix_free{};
    laplace_cognition_operator_application_receipt application_receipt{};
    ASSERT_EQ(laplace_cognition_operator_apply(
                  value.value, input.data(), input.size(), matrix_free.data(),
                  matrix_free.size(), &application_receipt),
              LAPLACE_COGNITION_OPERATOR_OK);
    std::array<double, 9> dense{};
    std::array<double, 3> rhs{};
    laplace_digest256 dense_receipt{};
    ASSERT_EQ(laplace_cognition_operator_materialize_dense(
                  value.value, dense.data(), dense.size(), rhs.data(), rhs.size(),
                  &dense_receipt),
              LAPLACE_COGNITION_OPERATOR_OK);
    for (std::size_t row = 0U; row < 3U; ++row) {
        double calculated = -rhs[row];
        for (std::size_t column = 0U; column < 3U; ++column) {
            calculated += dense[row * 3U + column] * input[column];
        }
        EXPECT_NEAR(calculated, matrix_free[row], program.value.numeric_tolerance);
    }
    EXPECT_FALSE(Same(dense_receipt, laplace_digest256{}));
}

TEST(CognitionOperator, ProgramScopeFiltersUnselectedRelationsAndSources) {
    ProgramFixture program;
    program.families = {{11U, 99U}};
    program.value.eligible_relation_families = program.families.data();
    program.value.eligible_source_mask = 1U;
    const std::vector fields{Field(20U, 0U), Field(21U, 1U)};
    auto selected = Constraint(30U, 11U, 0U, 1U);
    auto excluded_relation = Constraint(31U, 12U, 0U, 1U);
    auto excluded_testimony = Constraint(
        32U, 11U, 0U, 1U, LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY, 80U);
    laplace_cognition_operator_receipt receipt{};
    auto value = Build(
        &program, fields, {selected, excluded_relation, excluded_testimony}, &receipt);
    EXPECT_EQ(receipt.input_constraint_count, 3U);
    EXPECT_EQ(receipt.selected_constraint_count, 1U);
    EXPECT_EQ(receipt.physicality_constraint_count, 1U);
    EXPECT_EQ(receipt.testimony_constraint_count, 0U);
}

TEST(CognitionOperator, RejectsNegativePrecisionRatherThanEncodingContradiction) {
    ProgramFixture program;
    const std::vector fields{Field(20U, 0U), Field(21U, 1U)};
    auto invalid = Constraint(30U, 11U, 0U, 1U);
    invalid.precision = -1.0;
    laplace_cognition_operator* value = nullptr;
    laplace_cognition_operator_receipt receipt{};
    EXPECT_EQ(laplace_cognition_operator_create(
                  &program.value, fields.data(), fields.size(), &invalid, 1U,
                  &value, &receipt),
              LAPLACE_COGNITION_OPERATOR_CONSTRAINT_INVALID);
    EXPECT_EQ(value, nullptr);
}

}  // namespace
