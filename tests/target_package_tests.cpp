#include "laplace/target_package.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 PackageDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 PackageId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool PackageSame(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_operator_field PackageField(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = PackageDigest(seed);
    field.entity_id = PackageId(static_cast<std::uint8_t>(seed + 40U));
    field.recipe_fingerprint = PackageDigest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint PackageConstraint(
    const std::uint8_t seed,
    const std::uint32_t family,
    const std::uint64_t source,
    const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = PackageDigest(seed);
    constraint.plane_id = PackageDigest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = PackageDigest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = PackageDigest(static_cast<std::uint8_t>(seed + 60U));
    constraint.calculation_receipt_id =
        PackageDigest(static_cast<std::uint8_t>(seed + 80U));
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

struct PackageProgram {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};

    PackageProgram(
        const std::uint8_t seed,
        const laplace_digest256& boundary,
        const laplace_digest256& epoch) {
        value.program_id = PackageDigest(seed);
        value.boundary_id = boundary;
        value.context_fingerprint = PackageDigest(static_cast<std::uint8_t>(seed + 1U));
        value.evidence_epoch = epoch;
        value.result_contract_fingerprint =
            PackageDigest(static_cast<std::uint8_t>(seed + 2U));
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

class CompileHandle final {
public:
    CompileHandle() = default;
    CompileHandle(const CompileHandle&) = delete;
    CompileHandle& operator=(const CompileHandle&) = delete;
    CompileHandle(CompileHandle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    CompileHandle& operator=(CompileHandle&& other) noexcept {
        if (this != &other) {
            laplace_target_compile_result_destroy(&value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~CompileHandle() { laplace_target_compile_result_destroy(&value); }
    laplace_target_compile_result* value{};
};

class PackageHandle final {
public:
    ~PackageHandle() { laplace_target_package_destroy(&value); }
    laplace_target_package* value{};
};

struct CompiledPackageFixture final {
    CompileHandle result;
    laplace_target_compile_receipt receipt{};
};

CompiledPackageFixture CompileFixture(const bool reverse_jobs) {
    const auto boundary = PackageDigest(10U);
    const auto epoch = PackageDigest(11U);
    PackageProgram qk_program(20U, boundary, epoch);
    PackageProgram vo_program(30U, boundary, epoch);
    const std::vector fields{
        PackageField(40U, 0U), PackageField(41U, 1U), PackageField(42U, 2U)};
    const std::vector qk_constraints{
        PackageConstraint(50U, 11U, 0U, 1U, 2.0)};
    const std::vector vo_constraints{
        PackageConstraint(60U, 12U, 1U, 2U, 3.0)};

    laplace_target_compile_job qk{};
    qk.target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    qk.layer_index = 2U;
    qk.head_index = 1U;
    qk.role_fingerprint = PackageDigest(70U);
    qk.operator_program = qk_program.value;
    qk.fields = fields.data();
    qk.field_count = fields.size();
    qk.constraints = qk_constraints.data();
    qk.constraint_count = qk_constraints.size();

    laplace_target_compile_job vo{};
    vo.target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    vo.layer_index = 2U;
    vo.head_index = 1U;
    vo.role_fingerprint = PackageDigest(71U);
    vo.operator_program = vo_program.value;
    vo.fields = fields.data();
    vo.field_count = fields.size();
    vo.constraints = vo_constraints.data();
    vo.constraint_count = vo_constraints.size();

    std::array<laplace_target_compile_job, 2> jobs =
        reverse_jobs
            ? std::array<laplace_target_compile_job, 2>{{vo, qk}}
            : std::array<laplace_target_compile_job, 2>{{qk, vo}};
    laplace_target_compile_request request{};
    request.evidence_boundary = boundary;
    request.evidence_epoch = epoch;
    request.recipe_fingerprint = PackageDigest(80U);
    request.target_contract_fingerprint = PackageDigest(81U);
    request.jobs = jobs.data();
    request.job_count = jobs.size();
    request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    request.version = LAPLACE_TARGET_COMPILE_VERSION;

    CompiledPackageFixture compiled;
    EXPECT_EQ(
        laplace_target_compile_execute(
            &request, &compiled.result.value, &compiled.receipt),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_NE(compiled.result.value, nullptr);
    return compiled;
}

std::vector<std::uint8_t> Encode(
    const CompiledPackageFixture& compiled,
    laplace_target_package_receipt* receipt) {
    std::size_t required = 0U;
    EXPECT_EQ(
        laplace_target_package_measure(
            compiled.result.value, &compiled.receipt, &required),
        LAPLACE_TARGET_PACKAGE_OK);
    EXPECT_GT(required, 0U);
    std::vector<std::uint8_t> bytes(required);
    EXPECT_EQ(
        laplace_target_package_encode(
            compiled.result.value, &compiled.receipt,
            bytes.data(), bytes.size(), receipt),
        LAPLACE_TARGET_PACKAGE_OK);
    EXPECT_EQ(receipt->byte_count, bytes.size());
    return bytes;
}

std::vector<double> CompileMatrix(
    const laplace_target_compile_result* result,
    const std::size_t slot) {
    laplace_target_compile_slot_receipt slot_receipt{};
    EXPECT_EQ(
        laplace_target_compile_result_slot_receipt(result, slot, &slot_receipt),
        LAPLACE_TARGET_COMPILE_OK);
    std::vector<double> values(
        static_cast<std::size_t>(slot_receipt.matrix_value_count));
    std::size_t required = 0U;
    EXPECT_EQ(
        laplace_target_compile_result_matrix(
            result, slot, values.data(), values.size(), &required),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(required, values.size());
    return values;
}

TEST(TargetPackage, RoundTripsExactOperatorStateAndProvenance) {
    const auto compiled = CompileFixture(false);
    laplace_target_package_receipt encoded_receipt{};
    const auto bytes = Encode(compiled, &encoded_receipt);

    PackageHandle decoded;
    laplace_target_package_receipt decoded_receipt{};
    ASSERT_EQ(
        laplace_target_package_decode(
            bytes.data(), bytes.size(), &decoded.value, &decoded_receipt),
        LAPLACE_TARGET_PACKAGE_OK);
    ASSERT_NE(decoded.value, nullptr);
    EXPECT_TRUE(PackageSame(encoded_receipt.package_id, decoded_receipt.package_id));
    EXPECT_TRUE(PackageSame(
        compiled.receipt.receipt_id, decoded_receipt.compile_receipt_id));
    EXPECT_EQ(laplace_target_package_slot_count(decoded.value), 2U);

    laplace_target_compile_receipt recovered_compile{};
    ASSERT_EQ(
        laplace_target_package_compile_receipt(
            decoded.value, &recovered_compile),
        LAPLACE_TARGET_PACKAGE_OK);
    EXPECT_TRUE(PackageSame(
        compiled.receipt.receipt_id, recovered_compile.receipt_id));
    EXPECT_TRUE(PackageSame(
        compiled.receipt.request_fingerprint,
        recovered_compile.request_fingerprint));

    for (std::size_t slot = 0U; slot < 2U; ++slot) {
        laplace_target_compile_slot_receipt original_slot{};
        laplace_target_compile_slot_receipt decoded_slot{};
        ASSERT_EQ(
            laplace_target_compile_result_slot_receipt(
                compiled.result.value, slot, &original_slot),
            LAPLACE_TARGET_COMPILE_OK);
        ASSERT_EQ(
            laplace_target_package_slot_receipt(
                decoded.value, slot, &decoded_slot),
            LAPLACE_TARGET_PACKAGE_OK);
        EXPECT_TRUE(PackageSame(original_slot.slot_id, decoded_slot.slot_id));
        EXPECT_TRUE(PackageSame(
            original_slot.operator_id, decoded_slot.operator_id));
        EXPECT_TRUE(PackageSame(
            original_slot.matrix_fingerprint, decoded_slot.matrix_fingerprint));

        const auto expected_matrix = CompileMatrix(compiled.result.value, slot);
        std::vector<double> decoded_matrix(expected_matrix.size());
        std::size_t required = 0U;
        ASSERT_EQ(
            laplace_target_package_matrix(
                decoded.value, slot, decoded_matrix.data(),
                decoded_matrix.size(), &required),
            LAPLACE_TARGET_PACKAGE_OK);
        EXPECT_EQ(required, expected_matrix.size());
        EXPECT_EQ(decoded_matrix, expected_matrix);
    }
}

TEST(TargetPackage, EncodingIsCanonicalAcrossCallerJobOrder) {
    const auto forward = CompileFixture(false);
    const auto reverse = CompileFixture(true);
    ASSERT_TRUE(PackageSame(
        forward.receipt.receipt_id, reverse.receipt.receipt_id));

    laplace_target_package_receipt forward_receipt{};
    laplace_target_package_receipt reverse_receipt{};
    const auto forward_bytes = Encode(forward, &forward_receipt);
    const auto reverse_bytes = Encode(reverse, &reverse_receipt);
    EXPECT_EQ(forward_bytes, reverse_bytes);
    EXPECT_TRUE(PackageSame(
        forward_receipt.package_id, reverse_receipt.package_id));
}

TEST(TargetPackage, MeasurementPreventsPartialSerialization) {
    const auto compiled = CompileFixture(false);
    std::size_t required = 0U;
    ASSERT_EQ(
        laplace_target_package_measure(
            compiled.result.value, &compiled.receipt, &required),
        LAPLACE_TARGET_PACKAGE_OK);
    ASSERT_GT(required, 1U);
    std::vector<std::uint8_t> too_small(required - 1U, 0xA5U);
    laplace_target_package_receipt receipt{};
    EXPECT_EQ(
        laplace_target_package_encode(
            compiled.result.value, &compiled.receipt,
            too_small.data(), too_small.size(), &receipt),
        LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT);
    EXPECT_TRUE(std::all_of(
        std::begin(receipt.package_id.bytes), std::end(receipt.package_id.bytes),
        [](const std::uint8_t byte) { return byte == 0U; }));
}

TEST(TargetPackage, DetectsBitCorruptionAndTrailingBytes) {
    const auto compiled = CompileFixture(false);
    laplace_target_package_receipt encoded_receipt{};
    auto bytes = Encode(compiled, &encoded_receipt);
    ASSERT_GT(bytes.size(), 80U);

    auto corrupted = bytes;
    corrupted.back() ^= UINT8_C(1);
    PackageHandle corrupted_package;
    laplace_target_package_receipt corrupted_receipt{};
    EXPECT_EQ(
        laplace_target_package_decode(
            corrupted.data(), corrupted.size(), &corrupted_package.value,
            &corrupted_receipt),
        LAPLACE_TARGET_PACKAGE_CORRUPT);
    EXPECT_EQ(corrupted_package.value, nullptr);

    auto digest_corrupted = bytes;
    digest_corrupted[24U] ^= UINT8_C(1);
    EXPECT_EQ(
        laplace_target_package_decode(
            digest_corrupted.data(), digest_corrupted.size(),
            &corrupted_package.value, &corrupted_receipt),
        LAPLACE_TARGET_PACKAGE_CORRUPT);

    auto extended = bytes;
    extended.push_back(0U);
    EXPECT_EQ(
        laplace_target_package_decode(
            extended.data(), extended.size(), &corrupted_package.value,
            &corrupted_receipt),
        LAPLACE_TARGET_PACKAGE_INVALID_FORMAT);
}

}  // namespace
