#include "laplace/cognition_solver.h"

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

laplace_cognition_operator_field Field(std::uint8_t seed) {
    laplace_cognition_operator_field field{};
    field.field_id = Digest(seed);
    field.entity_id = Id(static_cast<std::uint8_t>(seed + 20U));
    field.recipe_fingerprint = Digest(static_cast<std::uint8_t>(seed + 40U));
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint Constraint(
    std::uint8_t seed,
    std::uint64_t source,
    std::uint64_t target,
    double precision,
    double target_value) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = Digest(seed);
    constraint.plane_id = Digest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = Digest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = Digest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id = Digest(static_cast<std::uint8_t>(seed + 80U));
    constraint.source_field_index = source;
    constraint.target_field_index = target;
    constraint.transport_scale = 1.0;
    constraint.precision = precision;
    constraint.target_value = target_value;
    constraint.relation_family = 11U;
    constraint.source_class = LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY;
    constraint.direction = LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET;
    constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
    if (target_value != 0.0) {
        constraint.flags = LAPLACE_COGNITION_OPERATOR_CONSTRAINT_HAS_TARGET;
    }
    return constraint;
}

class OperatorHandle {
public:
    ~OperatorHandle() { laplace_cognition_operator_destroy(&value); }
    laplace_cognition_operator* value{};
    laplace_cognition_operator_receipt receipt{};
};

OperatorHandle BuildOperator(
    const std::vector<laplace_cognition_operator_field>& fields,
    const std::vector<laplace_cognition_operator_constraint>& constraints) {
    static const std::array<std::uint32_t, 1> families{{11U}};
    laplace_cognition_operator_program program{};
    program.program_id = Digest(1U);
    program.boundary_id = Digest(2U);
    program.context_fingerprint = Digest(3U);
    program.evidence_epoch = Digest(4U);
    program.result_contract_fingerprint = Digest(5U);
    program.eligible_relation_families = families.data();
    program.eligible_relation_family_count = families.size();
    program.eligible_source_mask = 7U;
    program.flags =
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_POSITIVE_SEMIDEFINITE_PRECISION |
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_MATRIX_FREE_MATERIALIZED_PARITY;
    program.numeric_tolerance = 1e-12;
    program.version = LAPLACE_COGNITION_OPERATOR_VERSION;
    OperatorHandle result;
    EXPECT_EQ(laplace_cognition_operator_create(
                  &program, fields.data(), fields.size(), constraints.data(),
                  constraints.size(), &result.value, &result.receipt),
              LAPLACE_COGNITION_OPERATOR_OK);
    return result;
}

laplace_cognition_solver_program SolverProgram(
    const laplace_cognition_operator_receipt& operator_receipt,
    double regularization = 1e-6,
    std::uint64_t max_iterations = 64U) {
    laplace_cognition_solver_program program{};
    program.program_id = Digest(150U);
    program.operator_id = operator_receipt.operator_id;
    program.result_contract_fingerprint = Digest(151U);
    program.max_iterations = max_iterations;
    program.absolute_residual_tolerance = 1e-10;
    program.relative_residual_tolerance = 1e-10;
    program.regularization = regularization;
    program.method = LAPLACE_COGNITION_SOLVER_METHOD_CONJUGATE_GRADIENT;
    program.flags = LAPLACE_COGNITION_SOLVER_REQUIRE_PSD_OPERATOR |
        LAPLACE_COGNITION_SOLVER_RECORD_ITERATION_RECEIPTS;
    program.version = LAPLACE_COGNITION_SOLVER_VERSION;
    return program;
}

TEST(CognitionSolver, ConvergesMatrixFreeWithoutRewritingEvidencePrecision) {
    const std::vector fields{Field(20U), Field(21U), Field(22U)};
    const std::vector constraints{
        Constraint(30U, 0U, 1U, 3.0, 2.0),
        Constraint(31U, 1U, 2U, 4.0, -1.0)};
    auto operator_value = BuildOperator(fields, constraints);
    auto program = SolverProgram(operator_value.receipt, 1e-5);
    const std::array<double, 3> initial{{0.0, 0.0, 0.0}};
    std::array<double, 3> solution{};
    laplace_cognition_solver_receipt receipt{};
    ASSERT_EQ(laplace_cognition_solver_execute(
                  operator_value.value, &program, initial.data(), initial.size(),
                  solution.data(), solution.size(), &receipt),
              LAPLACE_COGNITION_SOLVER_OK);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_SOLVER_CONVERGED);
    EXPECT_LT(receipt.final_residual_l2, receipt.initial_residual_l2);
    EXPECT_LE(receipt.final_residual_l2,
              std::max(program.absolute_residual_tolerance,
                       program.relative_residual_tolerance * receipt.initial_residual_l2));
    EXPECT_TRUE(Same(
        receipt.evidence_precision_fingerprint,
        operator_value.receipt.constraint_set_fingerprint));
    EXPECT_TRUE(std::isfinite(receipt.final_energy));
}

TEST(CognitionSolver, NumericalRegularizationIsSolverStateNotEvidenceWeight) {
    const std::vector fields{Field(20U), Field(21U)};
    const std::vector constraints{Constraint(30U, 0U, 1U, 2.0, 0.0)};
    auto operator_value = BuildOperator(fields, constraints);
    auto program = SolverProgram(operator_value.receipt, 0.5);
    const std::array<double, 2> initial{{3.0, 3.0}};
    std::array<double, 2> solution{};
    laplace_cognition_solver_receipt receipt{};
    ASSERT_EQ(laplace_cognition_solver_execute(
                  operator_value.value, &program, initial.data(), initial.size(),
                  solution.data(), solution.size(), &receipt),
              LAPLACE_COGNITION_SOLVER_OK);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_SOLVER_CONVERGED);
    EXPECT_NEAR(solution[0], 0.0, 1e-9);
    EXPECT_NEAR(solution[1], 0.0, 1e-9);
    EXPECT_TRUE(Same(
        receipt.evidence_precision_fingerprint,
        operator_value.receipt.constraint_set_fingerprint));
    EXPECT_DOUBLE_EQ(receipt.regularization, 0.5);
}

TEST(CognitionSolver, IterationBoundProducesTypedDisposition) {
    const std::vector fields{Field(20U), Field(21U), Field(22U), Field(23U)};
    const std::vector constraints{
        Constraint(30U, 0U, 1U, 1.0, 1.0),
        Constraint(31U, 1U, 2U, 2.0, 2.0),
        Constraint(32U, 2U, 3U, 3.0, 3.0)};
    auto operator_value = BuildOperator(fields, constraints);
    auto program = SolverProgram(operator_value.receipt, 1e-8, 1U);
    const std::array<double, 4> initial{{5.0, -4.0, 3.0, -2.0}};
    std::array<double, 4> solution{};
    laplace_cognition_solver_receipt receipt{};
    ASSERT_EQ(laplace_cognition_solver_execute(
                  operator_value.value, &program, initial.data(), initial.size(),
                  solution.data(), solution.size(), &receipt),
              LAPLACE_COGNITION_SOLVER_OK);
    EXPECT_EQ(receipt.iteration_count, 1U);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_SOLVER_ITERATION_LIMIT);
    EXPECT_GT(receipt.final_residual_l2, 0.0);
}

}  // namespace
