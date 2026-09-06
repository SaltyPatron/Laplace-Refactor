#include "laplace/target_safetensors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 SafeDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 SafeId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool SafeSame(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field SafeField(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = SafeDigest(seed);
    field.entity_id = SafeId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = SafeDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint SafeConstraint(
    const std::uint8_t seed,
    const std::uint32_t family,
    const std::uint64_t source,
    const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = SafeDigest(seed);
    constraint.plane_id = SafeDigest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = SafeDigest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = SafeDigest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id = SafeDigest(static_cast<std::uint8_t>(seed + 80U));
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

class SafeCompileHandle final {
public:
    ~SafeCompileHandle() { laplace_target_compile_result_destroy(&value); }
    laplace_target_compile_result* value{};
};

struct SafeProgram final {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};

    SafeProgram(
        const std::uint8_t seed,
        const laplace_digest256& boundary,
        const laplace_digest256& epoch) {
        value.program_id = SafeDigest(seed);
        value.boundary_id = boundary;
        value.context_fingerprint = SafeDigest(static_cast<std::uint8_t>(seed + 1U));
        value.evidence_epoch = epoch;
        value.result_contract_fingerprint = SafeDigest(static_cast<std::uint8_t>(seed + 2U));
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

std::vector<std::uint8_t> BuildTargetPackage(
    laplace_target_package_receipt* package_receipt,
    laplace_target_compile_receipt* compile_receipt) {
    const auto boundary = SafeDigest(10U);
    const auto epoch = SafeDigest(11U);
    SafeProgram qk_program(20U, boundary, epoch);
    SafeProgram vo_program(30U, boundary, epoch);
    const std::vector fields{
        SafeField(40U, 0U), SafeField(41U, 1U), SafeField(42U, 2U)};
    const std::vector qk_constraints{
        SafeConstraint(50U, 11U, 0U, 1U, 2.0)};
    const std::vector vo_constraints{
        SafeConstraint(60U, 12U, 1U, 2U, 3.0)};

    std::array<laplace_target_compile_job, 2> jobs{};
    jobs[0].target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    jobs[0].layer_index = 2U;
    jobs[0].head_index = 1U;
    jobs[0].role_fingerprint = SafeDigest(70U);
    jobs[0].operator_program = qk_program.value;
    jobs[0].fields = fields.data();
    jobs[0].field_count = fields.size();
    jobs[0].constraints = qk_constraints.data();
    jobs[0].constraint_count = qk_constraints.size();

    jobs[1].target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    jobs[1].layer_index = 2U;
    jobs[1].head_index = 1U;
    jobs[1].role_fingerprint = SafeDigest(71U);
    jobs[1].operator_program = vo_program.value;
    jobs[1].fields = fields.data();
    jobs[1].field_count = fields.size();
    jobs[1].constraints = vo_constraints.data();
    jobs[1].constraint_count = vo_constraints.size();

    laplace_target_compile_request request{};
    request.evidence_boundary = boundary;
    request.evidence_epoch = epoch;
    request.recipe_fingerprint = SafeDigest(80U);
    request.target_contract_fingerprint = SafeDigest(81U);
    request.jobs = jobs.data();
    request.job_count = jobs.size();
    request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    request.version = LAPLACE_TARGET_COMPILE_VERSION;

    SafeCompileHandle compiled;
    EXPECT_EQ(
        laplace_target_compile_execute(&request, &compiled.value, compile_receipt),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_NE(compiled.value, nullptr);
    std::size_t package_bytes = 0U;
    EXPECT_EQ(
        laplace_target_package_measure(
            compiled.value, compile_receipt, &package_bytes),
        LAPLACE_TARGET_PACKAGE_OK);
    std::vector<std::uint8_t> package(package_bytes);
    EXPECT_EQ(
        laplace_target_package_encode(
            compiled.value, compile_receipt, package.data(), package.size(),
            package_receipt),
        LAPLACE_TARGET_PACKAGE_OK);
    return package;
}

std::uint64_t HeaderLength(const std::vector<std::uint8_t>& artifact) {
    std::uint64_t result = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        result |= static_cast<std::uint64_t>(artifact[index]) << (index * 8U);
    }
    return result;
}

TEST(TargetSafeTensors, EmitsExactF64TensorsAndPackageProvenance) {
    laplace_target_package_receipt package_receipt{};
    laplace_target_compile_receipt compile_receipt{};
    const auto package = BuildTargetPackage(&package_receipt, &compile_receipt);

    std::size_t required = 0U;
    laplace_target_safetensors_receipt receipt{};
    EXPECT_EQ(
        laplace_target_safetensors_encode(
            package.data(), package.size(), nullptr, 0U, &required, &receipt),
        LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT);
    ASSERT_GT(required, 8U);
    std::vector<std::uint8_t> artifact(required);
    ASSERT_EQ(
        laplace_target_safetensors_encode(
            package.data(), package.size(), artifact.data(), artifact.size(),
            &required, &receipt),
        LAPLACE_TARGET_SAFETENSORS_OK);
    EXPECT_TRUE(SafeSame(receipt.target_package_id, package_receipt.package_id));
    EXPECT_TRUE(SafeSame(receipt.compile_receipt_id, compile_receipt.receipt_id));
    EXPECT_EQ(receipt.slot_count, 2U);
    EXPECT_EQ(receipt.tensor_count, 4U);
    EXPECT_EQ(
        receipt.data_byte_count,
        (compile_receipt.matrix_value_count + compile_receipt.rhs_value_count) * 8U);

    const auto header_length = HeaderLength(artifact);
    ASSERT_EQ(header_length, receipt.header_byte_count);
    ASSERT_LE(8U + header_length, artifact.size());
    const std::string header(
        reinterpret_cast<const char*>(artifact.data() + 8U),
        static_cast<std::size_t>(header_length));
    EXPECT_NE(header.find("\"dtype\":\"F64\""), std::string::npos);
    EXPECT_NE(header.find("laplace.qk.layer_2.head_1.expert_0.matrix"), std::string::npos);
    EXPECT_NE(header.find("laplace.vo.layer_2.head_1.expert_0.rhs"), std::string::npos);
    EXPECT_NE(header.find("laplace_package_id"), std::string::npos);
}

TEST(TargetSafeTensors, CanonicalCodecValidatorRejectsAnyArtifactDrift) {
    laplace_target_package_receipt package_receipt{};
    laplace_target_compile_receipt compile_receipt{};
    const auto package = BuildTargetPackage(&package_receipt, &compile_receipt);
    std::size_t required = 0U;
    laplace_target_safetensors_receipt encoded{};
    ASSERT_EQ(
        laplace_target_safetensors_encode(
            package.data(), package.size(), nullptr, 0U, &required, &encoded),
        LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT);
    std::vector<std::uint8_t> artifact(required);
    ASSERT_EQ(
        laplace_target_safetensors_encode(
            package.data(), package.size(), artifact.data(), artifact.size(),
            &required, &encoded),
        LAPLACE_TARGET_SAFETENSORS_OK);

    laplace_target_safetensors_receipt validated{};
    ASSERT_EQ(
        laplace_target_safetensors_validate(
            package.data(), package.size(), artifact.data(), artifact.size(),
            &validated),
        LAPLACE_TARGET_SAFETENSORS_OK);
    EXPECT_TRUE(SafeSame(encoded.artifact_id, validated.artifact_id));

    auto corrupted = artifact;
    corrupted.back() ^= UINT8_C(1);
    EXPECT_EQ(
        laplace_target_safetensors_validate(
            package.data(), package.size(), corrupted.data(), corrupted.size(),
            &validated),
        LAPLACE_TARGET_SAFETENSORS_CORRUPT);

    auto extended = artifact;
    extended.push_back(0U);
    EXPECT_EQ(
        laplace_target_safetensors_validate(
            package.data(), package.size(), extended.data(), extended.size(),
            &validated),
        LAPLACE_TARGET_SAFETENSORS_CORRUPT);
}

TEST(TargetSafeTensors, CorruptTargetPackageCannotReachCodec) {
    laplace_target_package_receipt package_receipt{};
    laplace_target_compile_receipt compile_receipt{};
    auto package = BuildTargetPackage(&package_receipt, &compile_receipt);
    ASSERT_GT(package.size(), 56U);
    package.back() ^= UINT8_C(1);

    std::size_t required = 0U;
    laplace_target_safetensors_receipt receipt{};
    EXPECT_EQ(
        laplace_target_safetensors_encode(
            package.data(), package.size(), nullptr, 0U, &required, &receipt),
        LAPLACE_TARGET_SAFETENSORS_INVALID_PACKAGE);
    EXPECT_EQ(required, 0U);
}

}  // namespace
