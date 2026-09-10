#include "laplace/target_factorization.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 FactorDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 FactorId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool FactorSame(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field FactorField(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = FactorDigest(seed);
    field.entity_id = FactorId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = FactorDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint FactorConstraint(
    const std::uint8_t seed,
    const std::uint32_t family,
    const std::uint64_t source,
    const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = FactorDigest(seed);
    constraint.plane_id = FactorDigest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = FactorDigest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = FactorDigest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id =
        FactorDigest(static_cast<std::uint8_t>(seed + 80U));
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

struct FactorProgramFixture {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};

    FactorProgramFixture(
        const std::uint8_t seed,
        const laplace_digest256& boundary,
        const laplace_digest256& epoch) {
        value.program_id = FactorDigest(seed);
        value.boundary_id = boundary;
        value.context_fingerprint = FactorDigest(static_cast<std::uint8_t>(seed + 1U));
        value.evidence_epoch = epoch;
        value.result_contract_fingerprint =
            FactorDigest(static_cast<std::uint8_t>(seed + 2U));
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

class FactorCompileHandle {
public:
    FactorCompileHandle() = default;
    FactorCompileHandle(const FactorCompileHandle&) = delete;
    FactorCompileHandle& operator=(const FactorCompileHandle&) = delete;
    ~FactorCompileHandle() { laplace_target_compile_result_destroy(&value); }
    laplace_target_compile_result* value{};
};

void CompileFactorFixture(
    const std::vector<laplace_cognition_operator_field>& fields,
    const std::vector<laplace_cognition_operator_constraint>& constraints,
    FactorCompileHandle* result,
    laplace_target_compile_receipt* receipt) {
    const auto boundary = FactorDigest(10U);
    const auto epoch = FactorDigest(11U);
    FactorProgramFixture program(20U, boundary, epoch);

    laplace_target_compile_job job{};
    job.target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    job.layer_index = 1U;
    job.head_index = 2U;
    job.role_fingerprint = FactorDigest(70U);
    job.operator_program = program.value;
    job.fields = fields.data();
    job.field_count = fields.size();
    job.constraints = constraints.data();
    job.constraint_count = constraints.size();

    laplace_target_compile_request request{};
    request.evidence_boundary = boundary;
    request.evidence_epoch = epoch;
    request.recipe_fingerprint = FactorDigest(80U);
    request.target_contract_fingerprint = FactorDigest(81U);
    request.jobs = &job;
    request.job_count = 1U;
    request.version = LAPLACE_TARGET_COMPILE_VERSION;

    ASSERT_EQ(
        laplace_target_compile_execute(&request, &result->value, receipt),
        LAPLACE_TARGET_COMPILE_OK);
    ASSERT_NE(result->value, nullptr);
}

TEST(TargetFactorization, DerivesPairWeightsFromCompiledOperatorWithoutTraining) {
    const std::vector fields{FactorField(40U, 0U), FactorField(41U, 1U)};
    const std::vector constraints{FactorConstraint(50U, 11U, 0U, 1U, 2.0)};

    FactorCompileHandle compiled;
    laplace_target_compile_receipt compile_receipt{};
    CompileFactorFixture(fields, constraints, &compiled, &compile_receipt);

    laplace_target_factorization_request request{};
    request.slot_index = 0U;
    request.requested_rank = 2U;
    request.relative_tolerance = 1e-10;
    request.flags = LAPLACE_TARGET_FACTORIZATION_REQUIRE_EXACT;
    request.version = LAPLACE_TARGET_FACTORIZATION_VERSION;

    std::size_t factor_values = 0U;
    std::size_t singular_values = 0U;
    laplace_target_factorization_receipt preflight{};
    EXPECT_EQ(
        laplace_target_factorize_slot(
            compiled.value, &request, nullptr, 0U, nullptr, 0U, nullptr, 0U,
            &factor_values, &singular_values, &preflight),
        LAPLACE_TARGET_FACTORIZATION_CAPACITY_INSUFFICIENT);
    ASSERT_EQ(factor_values, 4U);
    ASSERT_EQ(singular_values, 2U);

    std::vector<double> left(factor_values);
    std::vector<double> right(factor_values);
    std::vector<double> sigma(singular_values);
    laplace_target_factorization_receipt receipt{};
    ASSERT_EQ(
        laplace_target_factorize_slot(
            compiled.value, &request,
            left.data(), left.size(), right.data(), right.size(),
            sigma.data(), sigma.size(), &factor_values, &singular_values, &receipt),
        LAPLACE_TARGET_FACTORIZATION_OK);
    EXPECT_EQ(receipt.target_role, LAPLACE_TARGET_ROLE_COMPATIBILITY_QK);
    EXPECT_LE(receipt.relative_residual, request.relative_tolerance);
    EXPECT_FALSE(FactorSame(receipt.left_factor_fingerprint, laplace_digest256{}));
    EXPECT_FALSE(FactorSame(receipt.right_factor_fingerprint, laplace_digest256{}));

    std::vector<double> matrix(4U);
    std::size_t matrix_values = 0U;
    ASSERT_EQ(
        laplace_target_compile_result_matrix(
            compiled.value, 0U, matrix.data(), matrix.size(), &matrix_values),
        LAPLACE_TARGET_COMPILE_OK);
    ASSERT_EQ(matrix_values, matrix.size());

    for (std::size_t row = 0U; row < 2U; ++row) {
        for (std::size_t column = 0U; column < 2U; ++column) {
            double reconstructed = 0.0;
            for (std::size_t rank = 0U; rank < 2U; ++rank) {
                reconstructed += left[row * 2U + rank] * right[column * 2U + rank];
            }
            EXPECT_NEAR(reconstructed, matrix[row * 2U + column], 1e-9);
        }
    }

    std::vector<double> left_again(factor_values);
    std::vector<double> right_again(factor_values);
    std::vector<double> sigma_again(singular_values);
    laplace_target_factorization_receipt receipt_again{};
    ASSERT_EQ(
        laplace_target_factorize_slot(
            compiled.value, &request,
            left_again.data(), left_again.size(), right_again.data(), right_again.size(),
            sigma_again.data(), sigma_again.size(),
            &factor_values, &singular_values, &receipt_again),
        LAPLACE_TARGET_FACTORIZATION_OK);
    EXPECT_EQ(left, left_again);
    EXPECT_EQ(right, right_again);
    EXPECT_EQ(sigma, sigma_again);
    EXPECT_TRUE(FactorSame(receipt.factorization_id, receipt_again.factorization_id));
}

TEST(TargetFactorization, RefusesToHideTargetRankLoss) {
    const std::vector fields{
        FactorField(40U, 0U), FactorField(41U, 1U), FactorField(42U, 2U)};
    const std::vector constraints{
        FactorConstraint(50U, 11U, 0U, 1U, 2.0),
        FactorConstraint(51U, 12U, 1U, 2U, 3.0)};

    FactorCompileHandle compiled;
    laplace_target_compile_receipt compile_receipt{};
    CompileFactorFixture(fields, constraints, &compiled, &compile_receipt);

    laplace_target_factorization_request request{};
    request.slot_index = 0U;
    request.requested_rank = 1U;
    request.relative_tolerance = 1e-12;
    request.flags = LAPLACE_TARGET_FACTORIZATION_REQUIRE_EXACT;
    request.version = LAPLACE_TARGET_FACTORIZATION_VERSION;

    std::vector<double> left(3U);
    std::vector<double> right(3U);
    std::vector<double> sigma(1U);
    std::size_t factor_values = 0U;
    std::size_t singular_values = 0U;
    laplace_target_factorization_receipt receipt{};
    EXPECT_EQ(
        laplace_target_factorize_slot(
            compiled.value, &request,
            left.data(), left.size(), right.data(), right.size(),
            sigma.data(), sigma.size(), &factor_values, &singular_values, &receipt),
        LAPLACE_TARGET_FACTORIZATION_RANK_INSUFFICIENT);
    EXPECT_EQ(receipt.status, LAPLACE_TARGET_FACTORIZATION_RANK_INSUFFICIENT);
    EXPECT_EQ(receipt.requested_rank, 1U);
    EXPECT_GT(receipt.relative_residual, request.relative_tolerance);
    EXPECT_GT(receipt.residual_frobenius, 0.0);
}

}  // namespace
