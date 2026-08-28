#include "iso_639_profile_fixture.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

const char* SourceRoot() {
    const char* root = std::getenv("LAPLACE_ISO_639_SOURCE_ROOT");
    return root == nullptr ? "/vault/Data/ISO639" : root;
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
    EXPECT_EQ(view.profile.mapping_count,
              laplace::test::iso_profile::expected_claims);
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
