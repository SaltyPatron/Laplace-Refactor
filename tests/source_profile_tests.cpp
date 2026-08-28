#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "laplace/source_profile.h"

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_id128 Id(std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_source_profile_manifest MakeProfile(
    std::uint8_t seed,
    const laplace_digest256& selected_boundary = Digest(0xd0u)) {
    laplace_source_profile_manifest value{};
    value.coordinate.kind = LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE;
    value.coordinate.authority = Id(seed);
    value.coordinate.release = Id(static_cast<std::uint8_t>(seed + 0x10u));
    value.coordinate.name_space = Id(static_cast<std::uint8_t>(seed + 0x20u));
    value.coordinate.local_identifier = Id(static_cast<std::uint8_t>(seed + 0x30u));
    value.coordinate.version = 1u;
    value.authority_release_fingerprint = Digest(static_cast<std::uint8_t>(seed + 1u));
    value.license_fingerprint = Digest(static_cast<std::uint8_t>(seed + 2u));
    value.artifact_graph_fingerprint = Digest(static_cast<std::uint8_t>(seed + 3u));
    value.syntax_authority_fingerprint = Digest(static_cast<std::uint8_t>(seed + 4u));
    value.recipe_program_fingerprint = Digest(static_cast<std::uint8_t>(seed + 5u));
    value.universal_ast_mapping_fingerprint = Digest(static_cast<std::uint8_t>(seed + 6u));
    value.highway_references_fingerprint = Digest(static_cast<std::uint8_t>(seed + 7u));
    value.epistemic_witnessing_fingerprint = Digest(static_cast<std::uint8_t>(seed + 8u));
    value.denominator_declaration_fingerprint = Digest(static_cast<std::uint8_t>(seed + 9u));
    value.conformance_fingerprint = Digest(static_cast<std::uint8_t>(seed + 10u));
    value.completion_law_fingerprint = Digest(static_cast<std::uint8_t>(seed + 11u));
    value.selected_boundary_fingerprint = selected_boundary;
    value.byte_count = 100u;
    value.container_count = 1u;
    value.member_count = 1u;
    value.file_count = 1u;
    value.record_count = 2u;
    value.field_count = 4u;
    value.syntax_node_count = 6u;
    value.span_count = 4u;
    value.occurrence_count = 4u;
    value.output_count = 2u;
    value.closure_subject_count = 2u;
    value.persisted_count = 2u;
    value.not_applicable_mask =
        (UINT64_C(1) << 8u) | (UINT64_C(1) << 9u) |
        (UINT64_C(1) << 11u) | (UINT64_C(1) << 12u) |
        (UINT64_C(1) << 13u) | (UINT64_C(1) << 14u) |
        (UINT64_C(1) << 15u);
    value.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    value.flags = LAPLACE_SOURCE_PROFILE_FLAGS_NONE;
    EXPECT_EQ(laplace_source_profile_identify(&value, &value.profile_id),
              LAPLACE_SOURCE_PROFILE_OK);
    return value;
}

void Sort(std::vector<laplace_source_profile_manifest>& profiles) {
    std::sort(profiles.begin(), profiles.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.profile_id.bytes, right.profile_id.bytes, 32u) < 0;
    });
}

laplace_source_profile_status Execute(
    const std::vector<laplace_source_profile_manifest>& profiles,
    laplace_source_profile_receipt* receipt = nullptr,
    laplace_source_profile_error* error = nullptr) {
    laplace_source_profile_receipt local_receipt{};
    laplace_source_profile_error local_error{};
    return laplace_source_profile_validate_batch(
        profiles.data(), profiles.size(),
        receipt == nullptr ? &local_receipt : receipt,
        error == nullptr ? &local_error : error);
}

TEST(SourceProfile, ClosesMultipleProfilesInOneSelectedBoundary) {
    std::vector<laplace_source_profile_manifest> profiles{
        MakeProfile(0x10u), MakeProfile(0x50u)};
    profiles[1].reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC;
    profiles[1].persisted_count = 1u;
    profiles[1].transformed_count = 1u;
    ASSERT_EQ(laplace_source_profile_identify(
                  &profiles[1], &profiles[1].profile_id),
              LAPLACE_SOURCE_PROFILE_OK);
    Sort(profiles);
    laplace_source_profile_receipt first{};
    laplace_source_profile_receipt replay{};
    ASSERT_EQ(Execute(profiles, &first), LAPLACE_SOURCE_PROFILE_OK);
    ASSERT_EQ(Execute(profiles, &replay), LAPLACE_SOURCE_PROFILE_OK);
    EXPECT_EQ(std::memcmp(&first, &replay, sizeof(first)), 0);
    EXPECT_EQ(first.profile_count, 2u);
    EXPECT_EQ(first.closure_subject_count, 4u);
    EXPECT_EQ(first.persisted_count, 3u);
    EXPECT_EQ(first.negative_count, 0u);
    EXPECT_EQ(first.exact_reconstruction_count, 1u);
    EXPECT_EQ(first.semantic_reconstruction_count, 1u);
    EXPECT_EQ(first.no_reconstruction_count, 0u);
    EXPECT_EQ(first.version, LAPLACE_SOURCE_PROFILE_VERSION);
    EXPECT_EQ(first.status, LAPLACE_SOURCE_PROFILE_OK);
    EXPECT_EQ(std::memcmp(
        first.selected_boundary_fingerprint.bytes,
        profiles[0].selected_boundary_fingerprint.bytes, 32u), 0);
}

TEST(SourceProfile, RejectsProfilesFromDifferentSelectedBoundaries) {
    std::vector<laplace_source_profile_manifest> profiles{
        MakeProfile(0x10u), MakeProfile(0x50u, Digest(0xe0u))};
    Sort(profiles);
    laplace_source_profile_error error{};
    EXPECT_EQ(Execute(profiles, nullptr, &error),
              LAPLACE_SOURCE_PROFILE_BOUNDARY_MISMATCH);
#if defined(LAPLACE_TEST_SOURCE_PROFILE_BOUNDARY_BYPASS)
    ADD_FAILURE() << "selected-boundary mutant survived";
#else
    EXPECT_EQ(error.profile_index, 1u);
#endif
}

TEST(SourceProfile, IdentityBindsEveryRequiredSectionAndClosure) {
    const auto original = MakeProfile(0x20u);
    std::array<laplace_source_profile_manifest, 17> changes{};
    changes.fill(original);
    changes[0].authority_release_fingerprint.bytes[0] ^= 1u;
    changes[1].license_fingerprint.bytes[0] ^= 1u;
    changes[2].artifact_graph_fingerprint.bytes[0] ^= 1u;
    changes[3].syntax_authority_fingerprint.bytes[0] ^= 1u;
    changes[4].recipe_program_fingerprint.bytes[0] ^= 1u;
    changes[5].universal_ast_mapping_fingerprint.bytes[0] ^= 1u;
    changes[6].highway_references_fingerprint.bytes[0] ^= 1u;
    changes[7].epistemic_witnessing_fingerprint.bytes[0] ^= 1u;
    changes[8].denominator_declaration_fingerprint.bytes[0] ^= 1u;
    changes[9].conformance_fingerprint.bytes[0] ^= 1u;
    changes[10].completion_law_fingerprint.bytes[0] ^= 1u;
    changes[11].selected_boundary_fingerprint.bytes[0] ^= 1u;
    changes[12].coordinate.release.bytes[0] ^= 1u;
    changes[13].byte_count += 1u;
    changes[14].persisted_count = 1u;
    changes[14].accepted_count = 1u;
    changes[15].not_applicable_mask &= ~(UINT64_C(1) << 8u);
    changes[16].reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC;
    for (const auto& changed : changes) {
        laplace_digest256 changed_id{};
        ASSERT_EQ(laplace_source_profile_identify(&changed, &changed_id),
                  LAPLACE_SOURCE_PROFILE_OK);
        EXPECT_NE(std::memcmp(changed_id.bytes, original.profile_id.bytes, 32u), 0);
    }
}

TEST(SourceProfile, RejectsIncompleteDenominatorsAndInvalidNotApplicableState) {
    auto profile = MakeProfile(0x30u);
    profile.file_count = 0u;
    EXPECT_EQ(laplace_source_profile_identify(&profile, &profile.profile_id),
              LAPLACE_SOURCE_PROFILE_DENOMINATOR_INVALID);
    profile = MakeProfile(0x30u);
    profile.not_applicable_mask |= UINT64_C(1) << 4u;
    EXPECT_EQ(laplace_source_profile_identify(&profile, &profile.profile_id),
              LAPLACE_SOURCE_PROFILE_DENOMINATOR_INVALID);
    profile = MakeProfile(0x30u);
    profile.not_applicable_mask |= UINT64_C(1) << 63u;
    EXPECT_EQ(laplace_source_profile_identify(&profile, &profile.profile_id),
              LAPLACE_SOURCE_PROFILE_DENOMINATOR_INVALID);
}

TEST(SourceProfile, RejectsIncompleteClosureAndLossInExactReconstruction) {
    auto profile = MakeProfile(0x40u);
    profile.closure_subject_count += 1u;
    EXPECT_EQ(laplace_source_profile_identify(&profile, &profile.profile_id),
              LAPLACE_SOURCE_PROFILE_DISPOSITION_INVALID);
    profile = MakeProfile(0x40u);
    profile.persisted_count = 1u;
    profile.lossy_count = 1u;
    EXPECT_EQ(laplace_source_profile_identify(&profile, &profile.profile_id),
              LAPLACE_SOURCE_PROFILE_DISPOSITION_INVALID);
    profile.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC;
    EXPECT_EQ(laplace_source_profile_identify(&profile, &profile.profile_id),
              LAPLACE_SOURCE_PROFILE_OK);
}

TEST(SourceProfile, RejectsWrongCoordinateIdentityAndOrdering) {
    auto profile = MakeProfile(0x60u);
    profile.coordinate.kind = LAPLACE_HIGHWAY_KIND_LANGUAGE;
    EXPECT_EQ(laplace_source_profile_identify(&profile, &profile.profile_id),
              LAPLACE_SOURCE_PROFILE_COORDINATE_INVALID);
    std::vector<laplace_source_profile_manifest> profiles{
        MakeProfile(0x10u), MakeProfile(0x50u)};
    Sort(profiles);
    auto drift = profiles;
    drift[0].record_count += 1u;
    EXPECT_EQ(Execute(drift), LAPLACE_SOURCE_PROFILE_IDENTITY_MISMATCH);
    std::reverse(profiles.begin(), profiles.end());
    EXPECT_EQ(Execute(profiles), LAPLACE_SOURCE_PROFILE_ORDER_INVALID);
}

}  // namespace
