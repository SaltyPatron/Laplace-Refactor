#include "cili_profile_fixture.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"

namespace laplace::test {

namespace fs = std::filesystem;

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

    /*
     * When the pinned Unicode authority is physically available, exercise the
     * exact CILI members through the recursive product planner too.  The bound
     * is derived from the providers actually selected by this route rather than
     * from an arbitrary memory allowance: each UAX #29 boundary family can emit
     * at most one non-empty partition span per source byte, while the delimited
     * provider emits at most row + fields + delimiters + terminator per record.
     * Canonical requests require at least two Unicode positions and their total
     * operand material is bounded by root + three UAX views + row + field views.
     */
    const char* unicode_environment = std::getenv("LAPLACE_UNICODE_SOURCE_ROOT");
    const fs::path unicode_root = unicode_environment == nullptr
        ? fs::path("/vault/Data/UCD/Public/UCD/latest")
        : fs::path(unicode_environment);
    if (fs::is_directory(unicode_root)) {
        std::uint64_t delimited_bytes = 0u;
        std::uint64_t structural_witness_bound = 0u;
        for (const laplace_tabular_artifact& artifact : fixture.artifacts) {
            if (artifact.mode != LAPLACE_TABULAR_ARTIFACT_DELIMITED) continue;
            ASSERT_LE(
                artifact.byte_count,
                std::numeric_limits<std::uint64_t>::max() - delimited_bytes);
            delimited_bytes += artifact.byte_count;

            const std::uint64_t columns =
                static_cast<std::uint64_t>(artifact.expected_column_count);
            ASSERT_LE(
                columns,
                (std::numeric_limits<std::uint64_t>::max() - 1u) / 2u);
            const std::uint64_t per_record = columns * 2u + 1u;
            ASSERT_EQ(artifact.expected_field_count,
                      artifact.expected_record_count * columns);
            ASSERT_LE(
                artifact.byte_count,
                (std::numeric_limits<std::uint64_t>::max() - 1u) / 3u);
            const std::uint64_t uax_bound = artifact.byte_count * 3u;
            ASSERT_TRUE(
                artifact.expected_record_count == 0u ||
                per_record <= std::numeric_limits<std::uint64_t>::max() /
                    artifact.expected_record_count);
            const std::uint64_t grammar_bound =
                artifact.expected_record_count * per_record;
            ASSERT_LE(
                uax_bound,
                std::numeric_limits<std::uint64_t>::max() - 1u - grammar_bound);
            const std::uint64_t artifact_bound =
                1u + uax_bound + grammar_bound;
            ASSERT_LE(
                artifact_bound,
                std::numeric_limits<std::uint64_t>::max() -
                    structural_witness_bound);
            structural_witness_bound += artifact_bound;
        }
        ASSERT_GT(delimited_bytes, 0u);

        laplace_unicode_source_bundle* unicode_bundle = nullptr;
        laplace_unicode_source_receipt unicode_receipt{};
        const std::string unicode_root_text = unicode_root.string();
        ASSERT_EQ(laplace_unicode_source_bundle_open(
                      unicode_root_text.c_str(),
                      &unicode_bundle,
                      &unicode_receipt),
                  LAPLACE_UNICODE_OK);
        ASSERT_NE(unicode_bundle, nullptr);

        laplace_tabular_source_plan* recursive_plan = nullptr;
        ASSERT_EQ(laplace_tabular_source_plan_create_recursive(
                      &fixture.input, unicode_bundle, &recursive_plan),
                  LAPLACE_TABULAR_SOURCE_OK);
        ASSERT_NE(recursive_plan, nullptr);
        laplace_tabular_source_plan_view recursive{};
        ASSERT_EQ(laplace_tabular_source_plan_view_get(
                      recursive_plan, &recursive),
                  LAPLACE_TABULAR_SOURCE_OK);

        ASSERT_GE(recursive.request_count, view.request_count);
        ASSERT_GE(recursive.operand_count, view.operand_count);
        const std::uint64_t added_requests =
            recursive.request_count - view.request_count;
        const std::uint64_t added_operands =
            recursive.operand_count - view.operand_count;
        ASSERT_GT(recursive.decomposition_witness_count, 0u);
        EXPECT_LE(
            recursive.decomposition_witness_count,
            structural_witness_bound);
        EXPECT_LE(added_requests, recursive.decomposition_witness_count);

        ASSERT_LE(
            delimited_bytes,
            std::numeric_limits<std::uint64_t>::max() / 6u);
        const std::uint64_t canonical_operand_bound = delimited_bytes * 6u;
        EXPECT_LE(added_operands, canonical_operand_bound);
        EXPECT_LE(added_requests, canonical_operand_bound / 2u);

        laplace_tabular_source_plan_destroy(&recursive_plan);
        laplace_unicode_source_bundle_close(&unicode_bundle);
    }

    laplace_tabular_source_plan_destroy(&plan);
    EXPECT_EQ(plan, nullptr);
}

}  // namespace laplace::test
