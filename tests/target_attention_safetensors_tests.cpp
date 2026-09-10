#include "laplace/target_attention_safetensors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 ExportDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 ExportId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool ExportSame(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field ExportField(
    const std::uint8_t seed, const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = ExportDigest(seed);
    field.entity_id = ExportId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = ExportDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint ExportConstraint(
    const std::uint8_t seed, const std::uint32_t family,
    const std::uint64_t source, const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = ExportDigest(seed);
    constraint.plane_id = ExportDigest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = ExportDigest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = ExportDigest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id =
        ExportDigest(static_cast<std::uint8_t>(seed + 80U));
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

struct ExportProgram final {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};

    ExportProgram(
        const std::uint8_t seed,
        const laplace_digest256& boundary,
        const laplace_digest256& epoch) {
        value.program_id = ExportDigest(seed);
        value.boundary_id = boundary;
        value.context_fingerprint = ExportDigest(static_cast<std::uint8_t>(seed + 1U));
        value.evidence_epoch = epoch;
        value.result_contract_fingerprint = ExportDigest(static_cast<std::uint8_t>(seed + 2U));
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

class ExportCompileHandle final {
public:
    ~ExportCompileHandle() { laplace_target_compile_result_destroy(&value); }
    laplace_target_compile_result* value{};
};

class ExportProjectionHandle final {
public:
    ~ExportProjectionHandle() {
        laplace_target_attention_projection_result_destroy(&value);
    }
    laplace_target_attention_projection_result* value{};
};

struct ExportFixture final {
    ExportCompileHandle compiled;
    ExportProjectionHandle projected;
    laplace_target_compile_receipt compile_receipt{};
    laplace_target_attention_projection_receipt projection_receipt{};

    ExportFixture() {
        const auto boundary = ExportDigest(10U);
        const auto epoch = ExportDigest(11U);
        ExportProgram qk_program(20U, boundary, epoch);
        ExportProgram vo_program(30U, boundary, epoch);
        const std::vector fields{
            ExportField(40U, 0U), ExportField(41U, 1U),
            ExportField(42U, 2U), ExportField(43U, 3U)};
        const std::vector qk_constraints{
            ExportConstraint(50U, 11U, 0U, 1U, 2.0)};
        const std::vector vo_constraints{
            ExportConstraint(60U, 12U, 2U, 3U, 3.0)};
        std::array<laplace_target_compile_job, 2> jobs{};
        jobs[0].target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
        jobs[0].layer_index = 2U;
        jobs[0].head_index = 5U;
        jobs[0].role_fingerprint = ExportDigest(70U);
        jobs[0].operator_program = qk_program.value;
        jobs[0].fields = fields.data();
        jobs[0].field_count = fields.size();
        jobs[0].constraints = qk_constraints.data();
        jobs[0].constraint_count = qk_constraints.size();
        jobs[1].target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
        jobs[1].layer_index = 2U;
        jobs[1].head_index = 5U;
        jobs[1].role_fingerprint = ExportDigest(71U);
        jobs[1].operator_program = vo_program.value;
        jobs[1].fields = fields.data();
        jobs[1].field_count = fields.size();
        jobs[1].constraints = vo_constraints.data();
        jobs[1].constraint_count = vo_constraints.size();

        laplace_target_compile_request compile_request{};
        compile_request.evidence_boundary = boundary;
        compile_request.evidence_epoch = epoch;
        compile_request.recipe_fingerprint = ExportDigest(80U);
        compile_request.target_contract_fingerprint = ExportDigest(81U);
        compile_request.jobs = jobs.data();
        compile_request.job_count = jobs.size();
        compile_request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
        compile_request.version = LAPLACE_TARGET_COMPILE_VERSION;
        EXPECT_EQ(
            laplace_target_compile_execute(
                &compile_request, &compiled.value, &compile_receipt),
            LAPLACE_TARGET_COMPILE_OK);
        EXPECT_NE(compiled.value, nullptr);

        laplace_target_attention_head_spec head{};
        head.qk_slot_index = 0U;
        head.vo_slot_index = 1U;
        head.head_rank = 1U;
        laplace_target_attention_projection_request projection_request{};
        projection_request.heads = &head;
        projection_request.head_count = 1U;
        projection_request.hidden_width = 2U;
        projection_request.relative_tolerance = 1e-10;
        projection_request.flags = LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT;
        projection_request.version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;
        EXPECT_EQ(
            laplace_target_attention_project(
                compiled.value, &projection_request, &projected.value,
                &projection_receipt),
            LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
        EXPECT_NE(projected.value, nullptr);
    }
};

std::uint64_t ExportHeaderLength(const std::vector<std::uint8_t>& artifact) {
    std::uint64_t result = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        result |= static_cast<std::uint64_t>(artifact[index]) << (index * 8U);
    }
    return result;
}

TEST(TargetAttentionSafeTensors, EmitsEmbeddingAndActualQkvoWeights) {
    ExportFixture fixture;
    std::size_t required = 0U;
    laplace_target_attention_safetensors_receipt measured{};
    EXPECT_EQ(
        laplace_target_attention_safetensors_encode(
            &fixture.compile_receipt, fixture.projected.value,
            &fixture.projection_receipt, nullptr, 0U, &required, &measured),
        LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT);
    ASSERT_GT(required, 8U);

    std::vector<std::uint8_t> artifact(required);
    laplace_target_attention_safetensors_receipt receipt{};
    ASSERT_EQ(
        laplace_target_attention_safetensors_encode(
            &fixture.compile_receipt, fixture.projected.value,
            &fixture.projection_receipt, artifact.data(), artifact.size(),
            &required, &receipt),
        LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK);
    EXPECT_TRUE(ExportSame(receipt.compile_receipt_id, fixture.compile_receipt.receipt_id));
    EXPECT_TRUE(ExportSame(receipt.projection_id, fixture.projection_receipt.projection_id));
    EXPECT_EQ(receipt.tensor_count, 5U);
    EXPECT_EQ(receipt.head_count, 1U);
    EXPECT_EQ(receipt.field_count, 4U);
    EXPECT_EQ(receipt.hidden_width, 2U);

    const auto header_length = ExportHeaderLength(artifact);
    ASSERT_EQ(header_length, receipt.header_byte_count);
    const std::string header(
        reinterpret_cast<const char*>(artifact.data() + 8U),
        static_cast<std::size_t>(header_length));
    EXPECT_NE(header.find("\"laplace.embedding\""), std::string::npos);
    EXPECT_NE(header.find("\"laplace.q.layer_2.head_5.expert_0\""), std::string::npos);
    EXPECT_NE(header.find("\"laplace.k.layer_2.head_5.expert_0\""), std::string::npos);
    EXPECT_NE(header.find("\"laplace.v.layer_2.head_5.expert_0\""), std::string::npos);
    EXPECT_NE(header.find("\"laplace.o.layer_2.head_5.expert_0\""), std::string::npos);
    EXPECT_NE(header.find("laplace_qk_factorization_id"), std::string::npos);
    EXPECT_NE(header.find("laplace_vo_factorization_id"), std::string::npos);
}

TEST(TargetAttentionSafeTensors, CanonicalValidationRejectsArtifactDrift) {
    ExportFixture fixture;
    std::size_t required = 0U;
    laplace_target_attention_safetensors_receipt receipt{};
    ASSERT_EQ(
        laplace_target_attention_safetensors_encode(
            &fixture.compile_receipt, fixture.projected.value,
            &fixture.projection_receipt, nullptr, 0U, &required, &receipt),
        LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT);
    std::vector<std::uint8_t> artifact(required);
    ASSERT_EQ(
        laplace_target_attention_safetensors_encode(
            &fixture.compile_receipt, fixture.projected.value,
            &fixture.projection_receipt, artifact.data(), artifact.size(),
            &required, &receipt),
        LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK);

    laplace_target_attention_safetensors_receipt validated{};
    EXPECT_EQ(
        laplace_target_attention_safetensors_validate(
            &fixture.compile_receipt, fixture.projected.value,
            &fixture.projection_receipt, artifact.data(), artifact.size(), &validated),
        LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK);
    EXPECT_TRUE(ExportSame(receipt.artifact_id, validated.artifact_id));

    artifact.back() ^= UINT8_C(1);
    EXPECT_EQ(
        laplace_target_attention_safetensors_validate(
            &fixture.compile_receipt, fixture.projected.value,
            &fixture.projection_receipt, artifact.data(), artifact.size(), &validated),
        LAPLACE_TARGET_ATTENTION_SAFETENSORS_CORRUPT);
}

TEST(TargetAttentionSafeTensors, RejectsMismatchedProjectionReceipt) {
    ExportFixture fixture;
    auto changed = fixture.projection_receipt;
    changed.hidden_width += 1U;
    std::size_t required = 0U;
    laplace_target_attention_safetensors_receipt receipt{};
    EXPECT_EQ(
        laplace_target_attention_safetensors_encode(
            &fixture.compile_receipt, fixture.projected.value, &changed,
            nullptr, 0U, &required, &receipt),
        LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION);
    EXPECT_EQ(required, 0U);
}

}  // namespace
