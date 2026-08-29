#include "cili_profile_fixture.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace laplace::test {

TEST(CiliSource, CompilesExactHeaderlessMappingsThroughTheGenericPlan) {
    const char* root = std::getenv("LAPLACE_CILI_SOURCE_ROOT");
    if (root == nullptr || root[0] == '\0') {
        root = LAPLACE_CILI_TEST_SOURCE_ROOT;
    }
    if (root[0] == '\0') {
        GTEST_SKIP() << "locked CILI source root unavailable";
    }
    CiliProfileFixture fixture;
    ASSERT_TRUE(fixture.Load(root)) << fixture.error;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_NE(plan, nullptr);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(view.artifact_count, cili_profile::artifact_count);
    EXPECT_EQ(view.profile.byte_count, cili_profile::expected_bytes);
    EXPECT_EQ(view.profile.record_count, cili_profile::expected_records);
    EXPECT_EQ(view.profile.field_count, cili_profile::expected_fields);
    EXPECT_EQ(view.profile.claim_count, cili_profile::expected_claims);
    EXPECT_EQ(view.claim_count, cili_profile::expected_claims);
    EXPECT_EQ(view.profile.reference_count, cili_profile::expected_references);
    EXPECT_EQ(view.reference_occurrence_count,
              cili_profile::expected_references);
    EXPECT_EQ(view.profile.mapping_count, cili_profile::expected_mappings);
    ASSERT_EQ(view.mapping_occurrence_count, cili_profile::expected_mappings);
    EXPECT_EQ(view.profile.reconstruction_class,
              LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC);
    EXPECT_EQ(view.profile.closure_subject_count,
              view.request_count + cili_profile::expected_references +
                  cili_profile::expected_mappings);
    EXPECT_EQ(view.profile.unresolved_count,
              cili_profile::expected_references +
                  cili_profile::expected_mappings);
    EXPECT_EQ(view.reference_occurrences[0].row_ordinal, 1u);
    EXPECT_EQ(view.reference_occurrences[0].column_ordinal, 1u);
    EXPECT_EQ(view.reference_occurrences[1].row_ordinal, 1u);
    EXPECT_EQ(view.reference_occurrences[1].column_ordinal, 2u);
    EXPECT_EQ(view.mapping_occurrences[0].row_ordinal, 1u);
    EXPECT_EQ(view.mapping_occurrences[0].relation_kind,
              LAPLACE_HIGHWAY_KIND_RELATION);
    EXPECT_EQ(view.mapping_occurrences[0].flags,
              LAPLACE_REFERENCE_MAPPING_FLAG_SYMMETRIC);

    std::size_t written = 0u;
    EXPECT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 0u, nullptr, 0u, &written),
              LAPLACE_TABULAR_SOURCE_RECONSTRUCTION_UNAVAILABLE);
    for (std::size_t index = 1u; index < fixture.artifacts.size(); ++index) {
        std::vector<std::uint8_t> recomposed(fixture.storage[index].size());
        ASSERT_EQ(laplace_tabular_source_recompose_artifact(
                      plan, index, recomposed.data(), recomposed.size(),
                      &written),
                  LAPLACE_TABULAR_SOURCE_OK);
        EXPECT_EQ(written, fixture.storage[index].size());
        EXPECT_EQ(recomposed, fixture.storage[index]);
    }
    static constexpr char FirstPwn30Row[] = "i1\t00001740-a\n";
    ASSERT_GE(fixture.storage[1].size(), sizeof(FirstPwn30Row) - 1u);
    EXPECT_EQ(std::memcmp(
                  fixture.storage[1].data(), FirstPwn30Row,
                  sizeof(FirstPwn30Row) - 1u), 0);
    laplace_tabular_source_plan_destroy(&plan);
    EXPECT_EQ(plan, nullptr);
}

}  // namespace laplace::test
