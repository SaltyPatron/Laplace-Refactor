#include "iso_639_profile_fixture.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "laplace/contract/evidence_testimony.h"
#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"

namespace {

namespace fs = std::filesystem;

const char* SourceRoot() {
    const char* root = std::getenv("LAPLACE_ISO_639_SOURCE_ROOT");
    return root == nullptr ? "/vault/Data/ISO639" : root;
}

fs::path UnicodeSourceRoot() {
    const char* root = std::getenv("LAPLACE_UNICODE_SOURCE_ROOT");
    return fs::path(
        root == nullptr ? "/vault/Data/UCD/Public/UCD/latest" : root);
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

}  // namespace

TEST(Iso639SourceProfile, CompilesLockedReleaseThroughGenericTabularEngine) {
    laplace::test::Iso639ProfileFixture fixture;
    ASSERT_TRUE(fixture.Load(SourceRoot())) << fixture.error;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_NE(plan, nullptr);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(view.artifact_count,
              laplace::test::iso_profile::artifact_count);
    EXPECT_EQ(view.profile.byte_count,
              laplace::test::iso_profile::expected_bytes);
    EXPECT_EQ(view.profile.container_count, 1u);
    EXPECT_EQ(view.profile.member_count, 4u);
    EXPECT_EQ(view.profile.file_count, 5u);
    EXPECT_EQ(view.profile.record_count,
              laplace::test::iso_profile::expected_records);
    EXPECT_EQ(view.profile.field_count,
              laplace::test::iso_profile::expected_fields);
    EXPECT_EQ(view.profile.claim_count,
              laplace::test::iso_profile::expected_claims);
    ASSERT_EQ(view.claim_count,
              laplace::test::iso_profile::expected_claims);
    for (std::size_t index = 0u;
         index < static_cast<std::size_t>(view.claim_count); ++index) {
        EXPECT_EQ(view.claim_outcome_types[index],
                  LAPLACE_EVIDENCE_OUTCOME_ASSERTION);
    }
    EXPECT_EQ(view.profile.mapping_count, 0u);
    EXPECT_EQ(view.profile.reference_count,
              laplace::test::iso_profile::expected_references);
    EXPECT_EQ(view.profile.unresolved_count,
              laplace::test::iso_profile::expected_references);
    EXPECT_EQ(
        view.profile.closure_subject_count,
        view.request_count + laplace::test::iso_profile::expected_references);
    EXPECT_EQ(view.profile.reconstruction_class,
              LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC);
    EXPECT_EQ(view.profile.transformed_count, view.request_count);
    EXPECT_EQ(view.profile.persisted_count, 0u);
    EXPECT_EQ(view.decomposition_witness_count, 0u);
    EXPECT_EQ(view.decomposition_witness_media_type_byte_count, 0u);
    EXPECT_EQ(view.decomposition_witnesses, nullptr);
    EXPECT_EQ(view.decomposition_witness_media_types, nullptr);

    std::size_t written{};
    EXPECT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 0u, nullptr, 0u, &written),
              LAPLACE_TABULAR_SOURCE_RECONSTRUCTION_UNAVAILABLE);
    for (std::size_t index = 1u; index < fixture.artifacts.size(); ++index) {
        std::vector<std::uint8_t> output(fixture.storage[index].size());
        ASSERT_EQ(laplace_tabular_source_recompose_artifact(
                      plan, index, output.data(), output.size(), &written),
                  LAPLACE_TABULAR_SOURCE_OK);
        ASSERT_EQ(written, fixture.storage[index].size());
        EXPECT_EQ(output, fixture.storage[index]);
    }
    laplace_tabular_source_plan_destroy(&plan);
}

TEST(Iso639SourceProfile,
     RecursiveAdmissionCarriesCanonicalDecompositionBeforeFinalRoot) {
    const fs::path unicode_root = UnicodeSourceRoot();
    if (!fs::is_directory(unicode_root)) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << unicode_root;
    }
    const std::string unicode_root_text = unicode_root.string();

    laplace::test::Iso639ProfileFixture fixture;
    ASSERT_TRUE(fixture.Load(SourceRoot())) << fixture.error;

    laplace_tabular_source_plan* legacy_plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &legacy_plan),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_NE(legacy_plan, nullptr);
    laplace_tabular_source_plan_view legacy{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(legacy_plan, &legacy),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_EQ(legacy.root_result_index + 1u, legacy.request_count);
    const laplace_composition_request legacy_root =
        legacy.requests[legacy.root_result_index];

    laplace_unicode_source_bundle* unicode_bundle = nullptr;
    laplace_unicode_source_receipt unicode_receipt{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  unicode_root_text.c_str(), &unicode_bundle, &unicode_receipt),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(unicode_bundle, nullptr);

    laplace_tabular_source_plan* recursive_plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create_recursive(
                  &fixture.input, unicode_bundle, &recursive_plan),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_NE(recursive_plan, nullptr);
    laplace_tabular_source_plan_view recursive{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(recursive_plan, &recursive),
              LAPLACE_TABULAR_SOURCE_OK);

    ASSERT_GT(recursive.request_count, legacy.request_count);
    const std::uint64_t added_request_count =
        recursive.request_count - legacy.request_count;
    EXPECT_EQ(recursive.root_result_index,
              legacy.root_result_index + added_request_count);
    EXPECT_EQ(recursive.root_result_index + 1u, recursive.request_count);
    EXPECT_EQ(recursive.atom_count, legacy.atom_count);
    EXPECT_GT(recursive.operand_count, legacy.operand_count);
    EXPECT_GT(recursive.profile.span_count, legacy.profile.span_count);
    EXPECT_EQ(recursive.profile.output_count, recursive.request_count);
    EXPECT_EQ(recursive.profile.transformed_count, recursive.request_count);

    ASSERT_GT(recursive.decomposition_witness_count, 0u);
    ASSERT_NE(recursive.decomposition_witnesses, nullptr);
    ASSERT_GT(recursive.decomposition_witness_media_type_byte_count, 0u);
    ASSERT_NE(recursive.decomposition_witness_media_types, nullptr);
    EXPECT_EQ(
        recursive.profile.span_count,
        legacy.profile.span_count + recursive.decomposition_witness_count);

    for (std::uint64_t witness_index = 0u;
         witness_index < recursive.decomposition_witness_count;
         ++witness_index) {
        const laplace_tabular_decomposition_witness& witness =
            recursive.decomposition_witnesses[witness_index];
        EXPECT_LT(witness.artifact_index, recursive.artifact_count);
        EXPECT_LT(witness.byte_start, witness.byte_end);
        ASSERT_LT(witness.artifact_index, fixture.artifacts.size());
        EXPECT_LE(
            witness.byte_end,
            fixture.artifacts[static_cast<std::size_t>(witness.artifact_index)]
                .byte_count);
        EXPECT_EQ(witness.canonical_content.multiplicity, 1u);
        EXPECT_EQ(witness.canonical_content.relationship_metadata, 0u);
        EXPECT_EQ(witness.canonical_content.flags, 0u);
        if (witness.span_index == 0u) {
            EXPECT_EQ(
                witness.parent_span_index,
                std::numeric_limits<std::uint64_t>::max());
        } else {
            EXPECT_LT(witness.parent_span_index, witness.span_index);
        }
        if (witness.canonical_content.reference_kind ==
            LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
            EXPECT_LT(witness.canonical_content.reference_index,
                      recursive.atom_count);
        } else {
            ASSERT_EQ(witness.canonical_content.reference_kind,
                      LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT);
            EXPECT_LT(witness.canonical_content.reference_index,
                      recursive.root_result_index);
        }
        ASSERT_LE(
            witness.media_type_byte_offset,
            recursive.decomposition_witness_media_type_byte_count);
        ASSERT_LE(
            witness.media_type_byte_count,
            recursive.decomposition_witness_media_type_byte_count -
                witness.media_type_byte_offset);
    }

    const laplace_composition_request& recursive_root =
        recursive.requests[recursive.root_result_index];
    EXPECT_EQ(recursive_root.first_operand, legacy_root.first_operand);
    EXPECT_EQ(recursive_root.operand_count, legacy_root.operand_count);
    EXPECT_EQ(recursive_root.recipe_version, legacy_root.recipe_version);
    EXPECT_EQ(recursive_root.flags, legacy_root.flags);
    EXPECT_EQ(recursive_root.source_ordinal,
              recursive.root_result_index + 1u);
    EXPECT_TRUE(SameDigest(
        recursive_root.calculation_recipe_fingerprint,
        legacy_root.calculation_recipe_fingerprint));
    EXPECT_TRUE(SameDigest(
        recursive_root.geometry_epoch, legacy_root.geometry_epoch));
    EXPECT_TRUE(SameDigest(
        recursive_root.occurrence_context_fingerprint,
        legacy_root.occurrence_context_fingerprint));

    for (std::uint64_t request_index = legacy.root_result_index;
         request_index < recursive.root_result_index;
         ++request_index) {
        const laplace_composition_request& request =
            recursive.requests[request_index];
        EXPECT_EQ(request.flags, 0u);
        EXPECT_EQ(request.source_ordinal, request_index + 1u);
        ASSERT_GT(request.operand_count, 0u);
        ASSERT_LE(request.first_operand + request.operand_count,
                  recursive.operand_count);
        for (std::uint64_t operand_index = request.first_operand;
             operand_index < request.first_operand + request.operand_count;
             ++operand_index) {
            const laplace_composition_operand& operand =
                recursive.operands[operand_index];
            EXPECT_EQ(operand.multiplicity, 1u);
            EXPECT_EQ(operand.relationship_metadata, 0u);
            EXPECT_EQ(operand.flags, 0u);
        }
    }

    ASSERT_LE(legacy_root.first_operand + legacy_root.operand_count,
              legacy.operand_count);
    ASSERT_LE(recursive_root.first_operand + recursive_root.operand_count,
              recursive.operand_count);
    for (std::uint64_t offset = 0u; offset < legacy_root.operand_count; ++offset) {
        const laplace_composition_operand& before =
            legacy.operands[legacy_root.first_operand + offset];
        const laplace_composition_operand& after =
            recursive.operands[recursive_root.first_operand + offset];
        EXPECT_EQ(after.reference_index, before.reference_index);
        EXPECT_EQ(after.multiplicity, before.multiplicity);
        EXPECT_EQ(after.relationship_metadata, before.relationship_metadata);
        EXPECT_EQ(after.reference_kind, before.reference_kind);
        EXPECT_EQ(after.flags, before.flags);
    }

    laplace_tabular_source_plan_destroy(&recursive_plan);
    laplace_unicode_source_bundle_close(&unicode_bundle);
    laplace_tabular_source_plan_destroy(&legacy_plan);
}

TEST(Iso639SourceProfile, ParentTopologyAndReleaseBytesAreRequired) {
    laplace::test::Iso639ProfileFixture fixture;
    ASSERT_TRUE(fixture.Load(SourceRoot())) << fixture.error;
    laplace_tabular_source_plan* plan = nullptr;

    fixture.artifacts[1].parent_artifact_id.bytes[0] ^= 0x80u;
    EXPECT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID);
    fixture.artifacts[1].parent_artifact_id.bytes[0] ^= 0x80u;

    fixture.storage[2][0] ^= 0x01u;
    EXPECT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_DIGEST_MISMATCH);
}
