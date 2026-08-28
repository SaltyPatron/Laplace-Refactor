#include "laplace/tabular_source.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "context_fixture.h"
#include "sha256_internal.hpp"

namespace {

void Fill(laplace_digest256& value, const std::uint8_t seed) {
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

void Fill(laplace_id128& value, const std::uint8_t seed) {
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

std::array<std::uint8_t, 32> DecodeSha(const std::string_view text) {
    std::array<std::uint8_t, 32> result{};
    EXPECT_EQ(text.size(), 64u);
    for (std::size_t index = 0u; index < result.size(); ++index) {
        const auto nibble = [](const char value) -> std::uint8_t {
            if (value >= '0' && value <= '9') {
                return static_cast<std::uint8_t>(value - '0');
            }
            return static_cast<std::uint8_t>(value - 'a' + 10);
        };
        result[index] = static_cast<std::uint8_t>(
            (nibble(text[index * 2u]) << 4u) |
            nibble(text[index * 2u + 1u]));
    }
    return result;
}

laplace_source_profile_manifest Declaration() {
    laplace_source_profile_manifest value{};
    value.coordinate.kind = 17u;
    Fill(value.coordinate.authority, 0x10u);
    Fill(value.coordinate.release, 0x30u);
    Fill(value.coordinate.name_space, 0x50u);
    Fill(value.coordinate.local_identifier, 0x70u);
    value.coordinate.version = 1u;
    Fill(value.authority_release_fingerprint, 0x90u);
    Fill(value.license_fingerprint, 0x91u);
    Fill(value.syntax_authority_fingerprint, 0x93u);
    Fill(value.recipe_program_fingerprint, 0x94u);
    Fill(value.universal_ast_mapping_fingerprint, 0x95u);
    Fill(value.highway_references_fingerprint, 0x96u);
    Fill(value.epistemic_witnessing_fingerprint, 0x97u);
    Fill(value.denominator_declaration_fingerprint, 0x98u);
    Fill(value.conformance_fingerprint, 0x99u);
    Fill(value.completion_law_fingerprint, 0x9au);
    Fill(value.selected_boundary_fingerprint, 0x9bu);
    value.reconstruction_class = 1u;
    return value;
}

struct Fixture {
    std::array<std::uint8_t, 5> archive{{0u, 1u, 255u, 'P', 'K'}};
    std::string text{"Id\tName\neng\tEnglish\njpn\t日本語\n"};
    std::array<laplace_tabular_artifact, 2> artifacts{};
    laplace_source_profile_manifest declaration{Declaration()};
    laplace_tabular_source_input input{};

    Fixture() {
        const auto archive_sha = DecodeSha(
            "3caaab6abbbcc0bc44f88ef7b56033746fa2f37a94067e43df296518eba3cef5");
        const auto text_sha = DecodeSha(
            "0f572261737480d63a5b9d2298e95c0ad5b6964062efc263f0a5707c21c7e01c");
        std::memcpy(artifacts[0].artifact_id.bytes, archive_sha.data(), 32u);
        std::memcpy(artifacts[0].expected_sha256, archive_sha.data(), 32u);
        artifacts[0].bytes = archive.data();
        artifacts[0].name = "release.zip";
        artifacts[0].byte_count = archive.size();
        artifacts[0].name_byte_count = std::strlen(artifacts[0].name);
        artifacts[0].mode = LAPLACE_TABULAR_ARTIFACT_RAW;
        artifacts[0].flags = LAPLACE_TABULAR_ARTIFACT_CONTAINER |
            LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;

        std::memcpy(artifacts[1].artifact_id.bytes, text_sha.data(), 32u);
        artifacts[1].parent_artifact_id = artifacts[0].artifact_id;
        std::memcpy(artifacts[1].expected_sha256, text_sha.data(), 32u);
        artifacts[1].bytes = reinterpret_cast<const std::uint8_t*>(text.data());
        artifacts[1].name = "tables/languages.tab";
        artifacts[1].byte_count = text.size();
        artifacts[1].name_byte_count = std::strlen(artifacts[1].name);
        artifacts[1].expected_record_count = 3u;
        artifacts[1].expected_field_count = 6u;
        artifacts[1].reference_column_mask = 1u;
        artifacts[1].mode = LAPLACE_TABULAR_ARTIFACT_DELIMITED;
        artifacts[1].delimiter = '\t';
        artifacts[1].line_terminator = LAPLACE_TABULAR_TERMINATOR_LF;
        artifacts[1].expected_column_count = 2u;
        artifacts[1].outcome_type = 5u;
        artifacts[1].flags = LAPLACE_TABULAR_ARTIFACT_MEMBER |
            LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;

        EXPECT_EQ(laplace_tabular_artifact_graph_identify(
                      artifacts.data(), artifacts.size(),
                      &declaration.artifact_graph_fingerprint),
                  LAPLACE_TABULAR_SOURCE_OK);
        input.profile_declaration = declaration;
        Fill(input.geometry_epoch, 0xc0u);
        Fill(input.occurrence_context_fingerprint, 0xd0u);
        input.artifacts = artifacts.data();
        input.artifact_count = artifacts.size();
        input.preferred_batch_bytes = 4096u;
    }
};

void RefreshArtifact(laplace_tabular_artifact& artifact) {
    const auto sha = laplace::internal::Sha256(
        artifact.bytes, static_cast<std::size_t>(artifact.byte_count));
    std::memcpy(artifact.expected_sha256, sha.data(), sha.size());
    std::memcpy(artifact.artifact_id.bytes, sha.data(), sha.size());
}

void RefreshGraph(Fixture& fixture) {
    ASSERT_EQ(laplace_tabular_artifact_graph_identify(
                  fixture.artifacts.data(), fixture.artifacts.size(),
                  &fixture.input.profile_declaration.artifact_graph_fingerprint),
              LAPLACE_TABULAR_SOURCE_OK);
}

laplace_composition_known_entity Atom(
    const std::uint32_t position, const std::size_t index) {
    laplace_composition_known_entity result{};
    EXPECT_EQ(laplace_identity_codepoint_witness(
                  position, &result.entity_id, &result.identity_witness),
              LAPLACE_IDENTITY_OK);
    Fill(result.physicality_id,
         static_cast<std::uint8_t>(0x20u + index));
    result.centroid.component[index % 4u] = 1.0;
    result.atom = position;
    result.has_atom = 1u;
    return result;
}

}  // namespace

TEST(TabularSource, CompilesRawAndDelimitedArtifactsIntoOneExactAstPlan) {
    Fixture fixture;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_NE(plan, nullptr);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(view.artifact_count, 2u);
    EXPECT_EQ(view.claim_count, 2u);
    EXPECT_EQ(view.profile.byte_count,
              fixture.archive.size() + fixture.text.size());
    EXPECT_EQ(view.profile.container_count, 1u);
    EXPECT_EQ(view.profile.member_count, 1u);
    EXPECT_EQ(view.profile.file_count, 2u);
    EXPECT_EQ(view.profile.record_count, 3u);
    EXPECT_EQ(view.profile.field_count, 6u);
    EXPECT_EQ(view.profile.reference_count, 2u);
    EXPECT_EQ(view.profile.claim_count, 2u);
    EXPECT_EQ(view.profile.mapping_count, 2u);
    EXPECT_EQ(view.profile.occurrence_count, 0u);
    EXPECT_EQ(view.profile.output_count, view.request_count);
    EXPECT_EQ(
        view.profile.closure_subject_count,
        view.request_count + view.profile.reference_count);
    EXPECT_EQ(view.profile.unresolved_count, view.profile.reference_count);
    EXPECT_EQ(view.profile.persisted_count, view.request_count);
    EXPECT_EQ(view.root_result_index + 1u, view.request_count);
    EXPECT_LT(view.claim_result_indexes[0], view.claim_result_indexes[1]);
    EXPECT_LT(view.claim_source_ordinals[0], view.claim_source_ordinals[1]);
    EXPECT_EQ(view.claim_outcome_types[0], 5u);
    EXPECT_EQ(view.claim_outcome_types[1], 5u);

    std::vector<std::uint8_t> archive(fixture.archive.size());
    std::vector<std::uint8_t> text(fixture.text.size());
    std::size_t bytes{};
    ASSERT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 0u, archive.data(), archive.size(), &bytes),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(bytes, fixture.archive.size());
    EXPECT_EQ(archive,
              std::vector<std::uint8_t>(
                  fixture.archive.begin(), fixture.archive.end()));
    ASSERT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 1u, text.data(), text.size(), &bytes),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(bytes, fixture.text.size());
    EXPECT_EQ(std::memcmp(text.data(), fixture.text.data(), text.size()), 0);
    laplace_tabular_source_plan_destroy(&plan);
    EXPECT_EQ(plan, nullptr);
}

TEST(TabularSource, SeparatesExactContentFromNonInvertibleSourceDistribution) {
    Fixture fixture;
    fixture.artifacts[0].flags = LAPLACE_TABULAR_ARTIFACT_CONTAINER;
    fixture.input.profile_declaration.reconstruction_class =
        LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC;
    RefreshGraph(fixture);
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(view.profile.reconstruction_class,
              LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC);
    EXPECT_EQ(view.profile.transformed_count, view.request_count);
    EXPECT_EQ(view.profile.persisted_count, 0u);
    std::size_t bytes{};
    EXPECT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 0u, nullptr, 0u, &bytes),
              LAPLACE_TABULAR_SOURCE_RECONSTRUCTION_UNAVAILABLE);
    std::vector<std::uint8_t> text(fixture.text.size());
    ASSERT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 1u, text.data(), text.size(), &bytes),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(bytes, fixture.text.size());
    EXPECT_EQ(std::memcmp(text.data(), fixture.text.data(), text.size()), 0);
    laplace_tabular_source_plan_destroy(&plan);
}

TEST(TabularSource, GeneratedPlanExecutesThroughCanonicalComposition) {
    Fixture fixture;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    std::vector<laplace_composition_known_entity> known;
    known.reserve(static_cast<std::size_t>(view.atom_count));
    for (std::size_t index = 0u;
         index < static_cast<std::size_t>(view.atom_count); ++index) {
        known.push_back(Atom(view.atom_positions[index], index));
    }
    auto context = laplace_test_context(3u);
    context.resource_grant.memory_bytes = UINT64_C(256) * 1024u * 1024u;
    const laplace_composition_working_set_input composition{
        &context,
        &view.source_fingerprint,
        &view.profile.recipe_program_fingerprint,
        known.data(),
        view.atom_count,
        view.operands,
        view.operand_count,
        view.requests,
        view.request_count,
        fixture.input.preferred_batch_bytes,
        0u};
    laplace_composition_working_set* working_set = nullptr;
    ASSERT_EQ(laplace_composition_working_set_create(
                  &composition, &working_set),
              LAPLACE_COMPOSITION_OK);
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.request_count, view.request_count);
    laplace_source_profile_manifest closed_profile{};
    ASSERT_EQ(laplace_tabular_source_profile_finalize(
                  plan, &summary, &closed_profile),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(closed_profile.occurrence_count,
              summary.logical_occurrence_count);
    EXPECT_FALSE(std::all_of(
        closed_profile.profile_id.bytes,
        closed_profile.profile_id.bytes + 32u,
        [](const std::uint8_t value) { return value == 0u; }));
    std::size_t result_count{};
    const auto* results = laplace_composition_working_set_results(
        working_set, &result_count);
    ASSERT_NE(results, nullptr);
    ASSERT_EQ(result_count, view.request_count);
    EXPECT_FALSE(std::all_of(
        results[view.root_result_index].entity_id.bytes,
        results[view.root_result_index].entity_id.bytes + 16u,
        [](const std::uint8_t value) { return value == 0u; }));
    laplace_composition_working_set_destroy(&working_set);
    laplace_tabular_source_plan_destroy(&plan);
}

TEST(TabularSource, RejectsDigestGrammarUtf8AndDenominatorDefects) {
    Fixture fixture;
    laplace_tabular_source_plan* plan = nullptr;
    fixture.archive[0] ^= 1u;
    EXPECT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_DIGEST_MISMATCH);
    fixture.archive[0] ^= 1u;

    fixture.text.pop_back();
    fixture.artifacts[1].byte_count = fixture.text.size();
    RefreshArtifact(fixture.artifacts[1]);
    RefreshGraph(fixture);
    EXPECT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID);

    Fixture invalid_utf8;
    invalid_utf8.text[invalid_utf8.text.find("English")] =
        static_cast<char>(0xff);
    RefreshArtifact(invalid_utf8.artifacts[1]);
    RefreshGraph(invalid_utf8);
    EXPECT_EQ(laplace_tabular_source_plan_create(&invalid_utf8.input, &plan),
              LAPLACE_TABULAR_SOURCE_UTF8_INVALID);

    Fixture denominator;
    denominator.artifacts[1].expected_field_count = 5u;
    laplace_digest256 graph{};
    ASSERT_EQ(laplace_tabular_artifact_graph_identify(
                  denominator.artifacts.data(), denominator.artifacts.size(),
                  &graph),
              LAPLACE_TABULAR_SOURCE_OK);
    denominator.input.profile_declaration.artifact_graph_fingerprint = graph;
    EXPECT_EQ(laplace_tabular_source_plan_create(&denominator.input, &plan),
              LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH);
}

TEST(TabularSource, RejectsMissingUnknownAndDuplicateArtifactGraphEdges) {
    Fixture fixture;
    laplace_digest256 graph{};

    std::memset(
        fixture.artifacts[1].parent_artifact_id.bytes, 0,
        sizeof(fixture.artifacts[1].parent_artifact_id.bytes));
    EXPECT_EQ(laplace_tabular_artifact_graph_identify(
                  fixture.artifacts.data(), fixture.artifacts.size(), &graph),
              LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID);

    Fixture unknown;
    Fill(unknown.artifacts[1].parent_artifact_id, 0xeeu);
    EXPECT_EQ(laplace_tabular_artifact_graph_identify(
                  unknown.artifacts.data(), unknown.artifacts.size(), &graph),
              LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID);

    Fixture duplicate;
    duplicate.artifacts[1].artifact_id = duplicate.artifacts[0].artifact_id;
    std::memcpy(
        duplicate.artifacts[1].expected_sha256,
        duplicate.artifacts[1].artifact_id.bytes,
        sizeof(duplicate.artifacts[1].expected_sha256));
    EXPECT_EQ(laplace_tabular_artifact_graph_identify(
                  duplicate.artifacts.data(), duplicate.artifacts.size(), &graph),
              LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID);
}

TEST(TabularSource, ArtifactGraphIdentityIncludesParentContainment) {
    Fixture fixture;
    std::array<std::uint8_t, 5> second_archive{{0u, 2u, 254u, 'P', 'K'}};
    std::array<laplace_tabular_artifact, 3> artifacts{{
        fixture.artifacts[0], fixture.artifacts[0], fixture.artifacts[1]}};
    artifacts[1].bytes = second_archive.data();
    artifacts[1].name = "release2.zip";
    artifacts[1].byte_count = second_archive.size();
    artifacts[1].name_byte_count = std::strlen(artifacts[1].name);
    RefreshArtifact(artifacts[1]);

    artifacts[2].parent_artifact_id = artifacts[0].artifact_id;
    laplace_digest256 first_parent_graph{};
    ASSERT_EQ(laplace_tabular_artifact_graph_identify(
                  artifacts.data(), artifacts.size(), &first_parent_graph),
              LAPLACE_TABULAR_SOURCE_OK);

    artifacts[2].parent_artifact_id = artifacts[1].artifact_id;
    laplace_digest256 second_parent_graph{};
    ASSERT_EQ(laplace_tabular_artifact_graph_identify(
                  artifacts.data(), artifacts.size(), &second_parent_graph),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_NE(std::memcmp(
                  first_parent_graph.bytes, second_parent_graph.bytes,
                  sizeof(first_parent_graph.bytes)),
              0);
}
