#include "laplace/target_attention_projection.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 AttentionDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 AttentionId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool AttentionSameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field AttentionField(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = AttentionDigest(seed);
    field.entity_id = AttentionId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = AttentionDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint AttentionConstraint(
    const std::uint8_t seed,
    const std::uint32_t family,
    const std::uint64_t source,
    const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = AttentionDigest(seed);
    constraint.plane_id = AttentionDigest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = AttentionDigest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = AttentionDigest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id =
        AttentionDigest(static_cast<std::uint8_t>(seed + 80U));
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

struct AttentionProgramFixture {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};

    AttentionProgramFixture(
        const std::uint8_t seed,
        const laplace_digest256& boundary,
        const laplace_digest256& epoch) {
        value.program_id = AttentionDigest(seed);
        value.boundary_id = boundary;
        value.context_fingerprint = AttentionDigest(static_cast<std::uint8_t>(seed + 1U));
        value.evidence_epoch = epoch;
        value.result_contract_fingerprint =
            AttentionDigest(static_cast<std::uint8_t>(seed + 2U));
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

class AttentionCompileHandle {
public:
    AttentionCompileHandle() = default;
    AttentionCompileHandle(const AttentionCompileHandle&) = delete;
    AttentionCompileHandle& operator=(const AttentionCompileHandle&) = delete;
    ~AttentionCompileHandle() { laplace_target_compile_result_destroy(&value); }
    laplace_target_compile_result* value{};
};

class AttentionProjectionHandle {
public:
    AttentionProjectionHandle() = default;
    AttentionProjectionHandle(const AttentionProjectionHandle&) = delete;
    AttentionProjectionHandle& operator=(const AttentionProjectionHandle&) = delete;
    ~AttentionProjectionHandle() {
        laplace_target_attention_projection_result_destroy(&value);
    }
    laplace_target_attention_projection_result* value{};
};

void CompileAttentionFixture(
    AttentionCompileHandle* compiled,
    laplace_target_compile_receipt* receipt) {
    const auto boundary = AttentionDigest(10U);
    const auto epoch = AttentionDigest(11U);
    AttentionProgramFixture qk_program(20U, boundary, epoch);
    AttentionProgramFixture vo_program(30U, boundary, epoch);
    static const std::vector fields{
        AttentionField(40U, 0U), AttentionField(41U, 1U),
        AttentionField(42U, 2U), AttentionField(43U, 3U)};
    static const std::vector qk_constraints{
        AttentionConstraint(50U, 11U, 0U, 1U, 2.0)};
    static const std::vector vo_constraints{
        AttentionConstraint(60U, 12U, 2U, 3U, 3.0)};

    std::array<laplace_target_compile_job, 2> jobs{};
    jobs[0].target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    jobs[0].layer_index = 2U;
    jobs[0].head_index = 5U;
    jobs[0].role_fingerprint = AttentionDigest(70U);
    jobs[0].operator_program = qk_program.value;
    jobs[0].fields = fields.data();
    jobs[0].field_count = fields.size();
    jobs[0].constraints = qk_constraints.data();
    jobs[0].constraint_count = qk_constraints.size();

    jobs[1].target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    jobs[1].layer_index = 2U;
    jobs[1].head_index = 5U;
    jobs[1].role_fingerprint = AttentionDigest(71U);
    jobs[1].operator_program = vo_program.value;
    jobs[1].fields = fields.data();
    jobs[1].field_count = fields.size();
    jobs[1].constraints = vo_constraints.data();
    jobs[1].constraint_count = vo_constraints.size();

    laplace_target_compile_request request{};
    request.evidence_boundary = boundary;
    request.evidence_epoch = epoch;
    request.recipe_fingerprint = AttentionDigest(80U);
    request.target_contract_fingerprint = AttentionDigest(81U);
    request.jobs = jobs.data();
    request.job_count = jobs.size();
    request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    request.version = LAPLACE_TARGET_COMPILE_VERSION;

    ASSERT_EQ(
        laplace_target_compile_execute(&request, &compiled->value, receipt),
        LAPLACE_TARGET_COMPILE_OK);
    ASSERT_NE(compiled->value, nullptr);
}

std::vector<double> ReadMatrix(
    const laplace_target_compile_result* compiled,
    const std::size_t slot_index) {
    laplace_target_compile_slot_receipt slot{};
    EXPECT_EQ(
        laplace_target_compile_result_slot_receipt(compiled, slot_index, &slot),
        LAPLACE_TARGET_COMPILE_OK);
    std::vector<double> matrix(static_cast<std::size_t>(slot.matrix_value_count));
    std::size_t required = 0U;
    EXPECT_EQ(
        laplace_target_compile_result_matrix(
            compiled, slot_index, matrix.data(), matrix.size(), &required),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(required, matrix.size());
    return matrix;
}

std::vector<double> ReadProjectionVector(
    const laplace_target_attention_projection_result* projected,
    const std::size_t head_index,
    laplace_target_attention_projection_status (*reader)(
        const laplace_target_attention_projection_result*,
        size_t, double*, size_t, size_t*)) {
    std::size_t required = 0U;
    EXPECT_EQ(
        reader(projected, head_index, nullptr, 0U, &required),
        LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT);
    std::vector<double> values(required);
    EXPECT_EQ(
        reader(projected, head_index, values.data(), values.size(), &required),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
    EXPECT_EQ(required, values.size());
    return values;
}

TEST(TargetAttentionProjection, DerivesEmbeddingAndQkvoWeightsFromKnownOperators) {
    AttentionCompileHandle compiled;
    laplace_target_compile_receipt compile_receipt{};
    CompileAttentionFixture(&compiled, &compile_receipt);

    laplace_target_attention_head_spec head{};
    head.qk_slot_index = 0U;
    head.vo_slot_index = 1U;
    head.head_rank = 1U;

    laplace_target_attention_projection_request request{};
    request.heads = &head;
    request.head_count = 1U;
    request.hidden_width = 2U;
    request.relative_tolerance = 1e-10;
    request.flags = LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT;
    request.version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;

    AttentionProjectionHandle projected;
    laplace_target_attention_projection_receipt receipt{};
    ASSERT_EQ(
        laplace_target_attention_project(
            compiled.value, &request, &projected.value, &receipt),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
    ASSERT_NE(projected.value, nullptr);
    EXPECT_EQ(receipt.field_count, 4U);
    EXPECT_EQ(receipt.hidden_width, 2U);
    EXPECT_EQ(receipt.semantic_basis_rank, 2U);
    EXPECT_EQ(receipt.null_basis_rank, 0U);
    EXPECT_LE(receipt.max_relative_residual, request.relative_tolerance);

    std::size_t embedding_values = 0U;
    EXPECT_EQ(
        laplace_target_attention_projection_embedding(
            projected.value, nullptr, 0U, &embedding_values),
        LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT);
    ASSERT_EQ(embedding_values, 8U);
    std::vector<double> embedding(embedding_values);
    ASSERT_EQ(
        laplace_target_attention_projection_embedding(
            projected.value, embedding.data(), embedding.size(), &embedding_values),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);

    const auto q = ReadProjectionVector(
        projected.value, 0U, laplace_target_attention_projection_q);
    const auto k = ReadProjectionVector(
        projected.value, 0U, laplace_target_attention_projection_k);
    const auto v = ReadProjectionVector(
        projected.value, 0U, laplace_target_attention_projection_v);
    const auto o = ReadProjectionVector(
        projected.value, 0U, laplace_target_attention_projection_o);
    ASSERT_EQ(q.size(), 2U);
    ASSERT_EQ(k.size(), 2U);
    ASSERT_EQ(v.size(), 2U);
    ASSERT_EQ(o.size(), 2U);

    std::array<double, 4> q_factor{};
    std::array<double, 4> k_factor{};
    std::array<double, 4> v_factor{};
    std::array<double, 4> o_factor{};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t hidden = 0U; hidden < 2U; ++hidden) {
            q_factor[row] += embedding[row * 2U + hidden] * q[hidden];
            k_factor[row] += embedding[row * 2U + hidden] * k[hidden];
            v_factor[row] += embedding[row * 2U + hidden] * v[hidden];
            o_factor[row] += embedding[row * 2U + hidden] * o[hidden];
        }
    }

    const auto qk_matrix = ReadMatrix(compiled.value, 0U);
    const auto vo_matrix = ReadMatrix(compiled.value, 1U);
    ASSERT_EQ(qk_matrix.size(), 16U);
    ASSERT_EQ(vo_matrix.size(), 16U);
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            EXPECT_NEAR(
                q_factor[row] * k_factor[column],
                qk_matrix[row * 4U + column], 1e-9);
            EXPECT_NEAR(
                v_factor[row] * o_factor[column],
                vo_matrix[row * 4U + column], 1e-9);
        }
    }

    laplace_target_attention_head_receipt head_receipt{};
    ASSERT_EQ(
        laplace_target_attention_projection_head_receipt(
            projected.value, 0U, &head_receipt),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
    EXPECT_FALSE(AttentionSameDigest(head_receipt.qk_factorization_id, laplace_digest256{}));
    EXPECT_FALSE(AttentionSameDigest(head_receipt.vo_factorization_id, laplace_digest256{}));
}

TEST(TargetAttentionProjection, RejectsHiddenWidthThatCannotPreserveBothOperators) {
    AttentionCompileHandle compiled;
    laplace_target_compile_receipt compile_receipt{};
    CompileAttentionFixture(&compiled, &compile_receipt);

    laplace_target_attention_head_spec head{};
    head.qk_slot_index = 0U;
    head.vo_slot_index = 1U;
    head.head_rank = 1U;

    laplace_target_attention_projection_request request{};
    request.heads = &head;
    request.head_count = 1U;
    request.hidden_width = 1U;
    request.relative_tolerance = 1e-12;
    request.flags = LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT;
    request.version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;

    AttentionProjectionHandle projected;
    laplace_target_attention_projection_receipt receipt{};
    EXPECT_EQ(
        laplace_target_attention_project(
            compiled.value, &request, &projected.value, &receipt),
        LAPLACE_TARGET_ATTENTION_PROJECTION_HIDDEN_WIDTH_INSUFFICIENT);
    EXPECT_EQ(projected.value, nullptr);
}

TEST(TargetAttentionProjection, IsDeterministicForTheSameClosedOperatorState) {
    AttentionCompileHandle compiled;
    laplace_target_compile_receipt compile_receipt{};
    CompileAttentionFixture(&compiled, &compile_receipt);

    laplace_target_attention_head_spec head{};
    head.qk_slot_index = 0U;
    head.vo_slot_index = 1U;
    head.head_rank = 1U;

    laplace_target_attention_projection_request request{};
    request.heads = &head;
    request.head_count = 1U;
    request.hidden_width = 2U;
    request.relative_tolerance = 1e-10;
    request.flags = LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT;
    request.version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;

    AttentionProjectionHandle first;
    AttentionProjectionHandle second;
    laplace_target_attention_projection_receipt first_receipt{};
    laplace_target_attention_projection_receipt second_receipt{};
    ASSERT_EQ(
        laplace_target_attention_project(
            compiled.value, &request, &first.value, &first_receipt),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
    ASSERT_EQ(
        laplace_target_attention_project(
            compiled.value, &request, &second.value, &second_receipt),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
    EXPECT_TRUE(AttentionSameDigest(first_receipt.projection_id, second_receipt.projection_id));
    EXPECT_TRUE(AttentionSameDigest(
        first_receipt.embedding_fingerprint, second_receipt.embedding_fingerprint));

    std::size_t first_count = 0U;
    std::size_t second_count = 0U;
    EXPECT_EQ(
        laplace_target_attention_projection_embedding(
            first.value, nullptr, 0U, &first_count),
        LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT);
    EXPECT_EQ(
        laplace_target_attention_projection_embedding(
            second.value, nullptr, 0U, &second_count),
        LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT);
    ASSERT_EQ(first_count, second_count);
    std::vector<double> first_embedding(first_count);
    std::vector<double> second_embedding(second_count);
    ASSERT_EQ(
        laplace_target_attention_projection_embedding(
            first.value, first_embedding.data(), first_embedding.size(), &first_count),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
    ASSERT_EQ(
        laplace_target_attention_projection_embedding(
            second.value, second_embedding.data(), second_embedding.size(), &second_count),
        LAPLACE_TARGET_ATTENTION_PROJECTION_OK);
    EXPECT_EQ(first_embedding, second_embedding);
}

}  // namespace
