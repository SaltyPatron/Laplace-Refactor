#include "laplace/target_compile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 TargetDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 TargetId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool TargetSame(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field TargetField(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = TargetDigest(seed);
    field.entity_id = TargetId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = TargetDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint TargetConstraint(
    const std::uint8_t seed,
    const std::uint32_t family,
    const std::uint64_t source,
    const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = TargetDigest(seed);
    constraint.plane_id = TargetDigest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = TargetDigest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = TargetDigest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id =
        TargetDigest(static_cast<std::uint8_t>(seed + 80U));
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

struct TargetProgramFixture {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};

    TargetProgramFixture(
        const std::uint8_t seed,
        const laplace_digest256& boundary,
        const laplace_digest256& epoch) {
        value.program_id = TargetDigest(seed);
        value.boundary_id = boundary;
        value.context_fingerprint = TargetDigest(static_cast<std::uint8_t>(seed + 1U));
        value.evidence_epoch = epoch;
        value.result_contract_fingerprint =
            TargetDigest(static_cast<std::uint8_t>(seed + 2U));
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

class TargetCompileHandle {
public:
    TargetCompileHandle() = default;
    TargetCompileHandle(const TargetCompileHandle&) = delete;
    TargetCompileHandle& operator=(const TargetCompileHandle&) = delete;
    ~TargetCompileHandle() { laplace_target_compile_result_destroy(&value); }
    laplace_target_compile_result* value{};
};

TEST(TargetCompile, CompilesDistinctQkAndVoOperatorsBeforeTensorLayout) {
    const auto boundary = TargetDigest(10U);
    const auto epoch = TargetDigest(11U);
    TargetProgramFixture qk_program(20U, boundary, epoch);
    TargetProgramFixture vo_program(30U, boundary, epoch);
    const std::vector fields{
        TargetField(40U, 0U), TargetField(41U, 1U), TargetField(42U, 2U)};
    const std::vector qk_constraints{
        TargetConstraint(50U, 11U, 0U, 1U, 2.0)};
    const std::vector vo_constraints{
        TargetConstraint(60U, 12U, 1U, 2U, 3.0)};

    std::array<laplace_target_compile_job, 2> jobs{};
    jobs[0].target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    jobs[0].layer_index = 2U;
    jobs[0].head_index = 1U;
    jobs[0].role_fingerprint = TargetDigest(70U);
    jobs[0].operator_program = qk_program.value;
    jobs[0].fields = fields.data();
    jobs[0].field_count = fields.size();
    jobs[0].constraints = qk_constraints.data();
    jobs[0].constraint_count = qk_constraints.size();

    jobs[1].target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    jobs[1].layer_index = 2U;
    jobs[1].head_index = 1U;
    jobs[1].role_fingerprint = TargetDigest(71U);
    jobs[1].operator_program = vo_program.value;
    jobs[1].fields = fields.data();
    jobs[1].field_count = fields.size();
    jobs[1].constraints = vo_constraints.data();
    jobs[1].constraint_count = vo_constraints.size();

    laplace_target_compile_request request{};
    request.evidence_boundary = boundary;
    request.evidence_epoch = epoch;
    request.recipe_fingerprint = TargetDigest(80U);
    request.target_contract_fingerprint = TargetDigest(81U);
    request.jobs = jobs.data();
    request.job_count = jobs.size();
    request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    request.version = LAPLACE_TARGET_COMPILE_VERSION;

    TargetCompileHandle result;
    laplace_target_compile_receipt receipt{};
    ASSERT_EQ(
        laplace_target_compile_execute(&request, &result.value, &receipt),
        LAPLACE_TARGET_COMPILE_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.slot_count, 2U);
    EXPECT_EQ(receipt.distinct_operator_count, 2U);
    EXPECT_EQ(laplace_target_compile_result_slot_count(result.value), 2U);

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
    EXPECT_FALSE(TargetSame(qk.operator_id, vo.operator_id));
    EXPECT_FALSE(TargetSame(qk.matrix_fingerprint, vo.matrix_fingerprint));
    EXPECT_EQ(qk.relation_plane_count, 1U);
    EXPECT_EQ(vo.relation_plane_count, 1U);

    std::vector<double> qk_matrix(static_cast<std::size_t>(qk.matrix_value_count));
    std::vector<double> vo_matrix(static_cast<std::size_t>(vo.matrix_value_count));
    std::size_t required = 0U;
    ASSERT_EQ(
        laplace_target_compile_result_matrix(
            result.value, 0U, qk_matrix.data(), qk_matrix.size(), &required),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(required, qk_matrix.size());
    ASSERT_EQ(
        laplace_target_compile_result_matrix(
            result.value, 1U, vo_matrix.data(), vo_matrix.size(), &required),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(required, vo_matrix.size());
    EXPECT_NE(qk_matrix, vo_matrix);
}

TEST(TargetCompile, RejectsQkVoFlatteningEvenWhenConsumerSlotsDiffer) {
    const auto boundary = TargetDigest(10U);
    const auto epoch = TargetDigest(11U);
    TargetProgramFixture program(20U, boundary, epoch);
    const std::vector fields{TargetField(40U, 0U), TargetField(41U, 1U)};
    const std::vector constraints{TargetConstraint(50U, 11U, 0U, 1U, 2.0)};

    std::array<laplace_target_compile_job, 2> jobs{};
    for (auto& job : jobs) {
        job.layer_index = 1U;
        job.head_index = 3U;
        job.operator_program = program.value;
        job.fields = fields.data();
        job.field_count = fields.size();
        job.constraints = constraints.data();
        job.constraint_count = constraints.size();
    }
    jobs[0].target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    jobs[0].role_fingerprint = TargetDigest(70U);
    jobs[1].target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    jobs[1].role_fingerprint = TargetDigest(71U);

    laplace_target_compile_request request{};
    request.evidence_boundary = boundary;
    request.evidence_epoch = epoch;
    request.recipe_fingerprint = TargetDigest(80U);
    request.target_contract_fingerprint = TargetDigest(81U);
    request.jobs = jobs.data();
    request.job_count = jobs.size();
    request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    request.version = LAPLACE_TARGET_COMPILE_VERSION;

    TargetCompileHandle result;
    laplace_target_compile_receipt receipt{};
    EXPECT_EQ(
        laplace_target_compile_execute(&request, &result.value, &receipt),
        LAPLACE_TARGET_COMPILE_FLATTENED_OPERATOR);
    EXPECT_EQ(result.value, nullptr);
}

TEST(TargetCompile, CanonicalJobOrderingMakesReceiptInputOrderIndependent) {
    const auto boundary = TargetDigest(10U);
    const auto epoch = TargetDigest(11U);
    TargetProgramFixture qk_program(20U, boundary, epoch);
    TargetProgramFixture vo_program(30U, boundary, epoch);
    const std::vector fields{TargetField(40U, 0U), TargetField(41U, 1U)};
    const std::vector qk_constraints{TargetConstraint(50U, 11U, 0U, 1U, 2.0)};
    const std::vector vo_constraints{TargetConstraint(60U, 12U, 0U, 1U, 3.0)};

    laplace_target_compile_job qk{};
    qk.target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    qk.layer_index = 1U;
    qk.head_index = 0U;
    qk.role_fingerprint = TargetDigest(70U);
    qk.operator_program = qk_program.value;
    qk.fields = fields.data();
    qk.field_count = fields.size();
    qk.constraints = qk_constraints.data();
    qk.constraint_count = qk_constraints.size();

    laplace_target_compile_job vo{};
    vo.target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    vo.layer_index = 1U;
    vo.head_index = 0U;
    vo.role_fingerprint = TargetDigest(71U);
    vo.operator_program = vo_program.value;
    vo.fields = fields.data();
    vo.field_count = fields.size();
    vo.constraints = vo_constraints.data();
    vo.constraint_count = vo_constraints.size();

    std::array<laplace_target_compile_job, 2> forward{{qk, vo}};
    std::array<laplace_target_compile_job, 2> reverse{{vo, qk}};

    auto compile = [&](const std::array<laplace_target_compile_job, 2>& jobs,
                       TargetCompileHandle* result,
                       laplace_target_compile_receipt* receipt) {
        laplace_target_compile_request request{};
        request.evidence_boundary = boundary;
        request.evidence_epoch = epoch;
        request.recipe_fingerprint = TargetDigest(80U);
        request.target_contract_fingerprint = TargetDigest(81U);
        request.jobs = jobs.data();
        request.job_count = jobs.size();
        request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
        request.version = LAPLACE_TARGET_COMPILE_VERSION;
        return laplace_target_compile_execute(&request, &result->value, receipt);
    };

    TargetCompileHandle first;
    TargetCompileHandle second;
    laplace_target_compile_receipt first_receipt{};
    laplace_target_compile_receipt second_receipt{};
    ASSERT_EQ(compile(forward, &first, &first_receipt), LAPLACE_TARGET_COMPILE_OK);
    ASSERT_EQ(compile(reverse, &second, &second_receipt), LAPLACE_TARGET_COMPILE_OK);
    EXPECT_TRUE(TargetSame(first_receipt.request_fingerprint,
                           second_receipt.request_fingerprint));
    EXPECT_TRUE(TargetSame(first_receipt.receipt_id, second_receipt.receipt_id));
}

}  // namespace
