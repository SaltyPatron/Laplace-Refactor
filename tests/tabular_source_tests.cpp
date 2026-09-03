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
    std::array<laplace_tabular_column, 2> columns{};
    std::array<laplace_tabular_reference_rule, 2> reference_rules{};
    std::array<laplace_tabular_mapping_rule, 1> mapping_rules{};
    laplace_source_profile_manifest declaration{Declaration()};
    laplace_tabular_source_input input{};

    Fixture() {
        static constexpr std::array<std::string_view, 2> ColumnNames{{
            "Id", "Name"}};
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
        for (std::size_t index = 0u; index < columns.size(); ++index) {
            columns[index].bytes = reinterpret_cast<const std::uint8_t*>(
                ColumnNames[index].data());
            columns[index].byte_count = ColumnNames[index].size();
        }
        artifacts[1].columns = columns.data();
        artifacts[1].header_record_count = 1u;
        artifacts[1].flags = LAPLACE_TABULAR_ARTIFACT_MEMBER |
            LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;

        Fill(reference_rules[0].name_space, 0x55u);
        reference_rules[0].artifact_index = 1u;
        reference_rules[0].column_index = 0u;
        reference_rules[0].kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
        reference_rules[0].flags = LAPLACE_REFERENCE_RULE_ENDPOINT |
            LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION;

        EXPECT_EQ(laplace_tabular_source_graph_identify(
                      artifacts.data(), artifacts.size(),
                      reference_rules.data(), 1u, nullptr, 0u,
                      &declaration.artifact_graph_fingerprint),
                  LAPLACE_TABULAR_SOURCE_OK);
        input.profile_declaration = declaration;
        Fill(input.geometry_epoch, 0xc0u);
        Fill(input.occurrence_context_fingerprint, 0xd0u);
        input.artifacts = artifacts.data();
        input.artifact_count = artifacts.size();
        input.reference_rules = reference_rules.data();
        input.reference_rule_count = 1u;
        input.preferred_batch_bytes = 4096u;
    }
};

void RefreshArtifact(laplace_tabular_artifact& artifact) {
    const auto sha = laplace::internal::Sha256(
        artifact.bytes, static_cast<std::size_t>(artifact.byte_count));
    std::memcpy(artifact.expected_sha256, sha.data(), sha.size());
    std::memcpy(artifact.artifact_id.bytes, sha.data(), sha.size());
}

struct FixedWidthFixture {
    std::string text{"ID NameQ \r\n001AB  ZQ\r\n002ABCDEQR\r\n"};
    std::array<laplace_tabular_artifact, 1> artifacts{};
    std::array<laplace_tabular_column, 3> columns{};
    std::array<laplace_tabular_fixed_width_field, 3> fields{{
        {3u, LAPLACE_TABULAR_FIXED_WIDTH_TRIM_LEFT |
                 LAPLACE_TABULAR_FIXED_WIDTH_TRIM_RIGHT},
        {4u, LAPLACE_TABULAR_FIXED_WIDTH_TRIM_LEFT |
                 LAPLACE_TABULAR_FIXED_WIDTH_TRIM_RIGHT},
        {2u, LAPLACE_TABULAR_FIXED_WIDTH_TRIM_LEFT |
                 LAPLACE_TABULAR_FIXED_WIDTH_TRIM_RIGHT}}};
    std::array<laplace_tabular_reference_rule, 1> reference_rules{};
    laplace_source_profile_manifest declaration{Declaration()};
    laplace_tabular_source_input input{};

    FixedWidthFixture() {
        static constexpr std::array<std::string_view, 3> ColumnNames{{
            "ID", "Name", "Q"}};
        auto& artifact = artifacts[0];
        artifact.bytes = reinterpret_cast<const std::uint8_t*>(text.data());
        artifact.name = "players.txt";
        artifact.media_type = "text/plain; charset=us-ascii";
        artifact.byte_count = text.size();
        artifact.name_byte_count = std::strlen(artifact.name);
        artifact.media_type_byte_count = std::strlen(artifact.media_type);
        artifact.expected_record_count = 3u;
        artifact.expected_field_count = 9u;
        artifact.reference_column_mask = 1u;
        artifact.mode = LAPLACE_TABULAR_ARTIFACT_FIXED_WIDTH;
        artifact.line_terminator = LAPLACE_TABULAR_TERMINATOR_CRLF;
        artifact.expected_column_count =
            static_cast<std::uint32_t>(columns.size());
        artifact.header_record_count = 1u;
        artifact.outcome_type = 1u;
        artifact.flags = LAPLACE_TABULAR_ARTIFACT_CONTAINER |
            LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;
        artifact.fixed_width_fields = fields.data();
        artifact.padding_byte = ' ';
        artifact.overflow_field_index = 1u;
        artifact.maximum_overflow_bytes = 1u;
        artifact.expected_overflow_record_count = 1u;
        for (std::size_t index = 0u; index < columns.size(); ++index) {
            columns[index].bytes = reinterpret_cast<const std::uint8_t*>(
                ColumnNames[index].data());
            columns[index].byte_count = ColumnNames[index].size();
        }
        artifact.columns = columns.data();
        RefreshArtifact(artifact);

        Fill(reference_rules[0].name_space, 0x66u);
        reference_rules[0].artifact_index = 0u;
        reference_rules[0].column_index = 0u;
        reference_rules[0].kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
        reference_rules[0].flags = LAPLACE_REFERENCE_RULE_ENDPOINT |
            LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION;
        EXPECT_EQ(laplace_tabular_source_graph_identify(
                      artifacts.data(), artifacts.size(),
                      reference_rules.data(), reference_rules.size(),
                      nullptr, 0u, &declaration.artifact_graph_fingerprint),
                  LAPLACE_TABULAR_SOURCE_OK);
        input.profile_declaration = declaration;
        Fill(input.geometry_epoch, 0xc1u);
        Fill(input.occurrence_context_fingerprint, 0xd1u);
        input.artifacts = artifacts.data();
        input.artifact_count = artifacts.size();
        input.reference_rules = reference_rules.data();
        input.reference_rule_count = reference_rules.size();
        input.preferred_batch_bytes = 4096u;
    }
};

void RefreshGraph(Fixture& fixture) {
    ASSERT_EQ(laplace_tabular_source_graph_identify(
                  fixture.artifacts.data(), fixture.artifacts.size(),
                  fixture.input.reference_rules,
                  static_cast<std::size_t>(fixture.input.reference_rule_count),
                  fixture.input.mapping_rules,
                  static_cast<std::size_t>(fixture.input.mapping_rule_count),
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
    ASSERT_EQ(view.reference_occurrence_count, 2u);
    EXPECT_EQ(view.reference_occurrences[0].artifact_ordinal, 2u);
    EXPECT_EQ(view.reference_occurrences[0].row_ordinal, 1u);
    EXPECT_EQ(view.reference_occurrences[0].column_ordinal, 1u);
    EXPECT_EQ(view.reference_occurrences[1].row_ordinal, 2u);
    EXPECT_EQ(view.reference_occurrences[0].kind,
              LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE);
    EXPECT_EQ(view.profile.claim_count, 2u);
    EXPECT_EQ(view.profile.mapping_count, 0u);
    EXPECT_EQ(view.mapping_occurrence_count, 0u);
    EXPECT_EQ(view.profile.occurrence_count, 0u);
    EXPECT_EQ(view.profile.output_count, view.request_count);
    EXPECT_EQ(
        view.profile.closure_subject_count,
        view.request_count + view.profile.reference_count);
    EXPECT_EQ(view.profile.unresolved_count, view.profile.reference_count);
    EXPECT_EQ(view.profile.persisted_count, view.request_count);
    EXPECT_EQ(view.root_result_index + 1u, view.request_count);
    std::uint64_t source_occurrence_count = 0u;
    for (std::uint64_t request_index = 0u;
         request_index < view.request_count; ++request_index) {
        if ((view.requests[static_cast<std::size_t>(request_index)].flags &
             LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE) != 0u) {
            ++source_occurrence_count;
        }
    }
    /* One source boundary, two artifact roots, two record claims, one exact
     * raw value, and six exact delimited field values are witnessed.  Tags,
     * schema nodes, codepoint atoms, and recursive grammar spans are not. */
    EXPECT_EQ(source_occurrence_count, 12u);
    EXPECT_LT(view.claim_result_indexes[0], view.claim_result_indexes[1]);
    EXPECT_LT(view.claim_source_ordinals[0], view.claim_source_ordinals[1]);
    EXPECT_EQ(view.claim_outcome_types[0], 5u);
    EXPECT_EQ(view.claim_outcome_types[1], 5u);
    for (std::size_t claim_index = 0u;
         claim_index < static_cast<std::size_t>(view.claim_count);
         ++claim_index) {
        const std::uint64_t result_index =
            view.claim_result_indexes[claim_index];
        ASSERT_LT(result_index, view.request_count);
        const auto& request = view.requests[result_index];
        ASSERT_EQ(request.operand_count, fixture.columns.size() + 2u);
        ASSERT_LE(request.first_operand + request.operand_count,
                  view.operand_count);
        const auto* row = view.operands + request.first_operand;
        EXPECT_EQ(row[0].relationship_metadata, 1u << 6u);
        EXPECT_EQ(row[1].relationship_metadata, 7u << 6u);
        EXPECT_EQ(row[2].relationship_metadata, 7u << 6u);
        EXPECT_EQ(row[3].relationship_metadata, 10u << 6u);
        EXPECT_EQ(request.source_ordinal,
                  view.claim_source_ordinals[claim_index]);
    }

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

TEST(TabularSource, FixedWidthRecipePreservesPaddingAndBoundedOverflowExactly) {
    FixedWidthFixture fixture;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_NE(plan, nullptr);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(view.profile.record_count, 3u);
    EXPECT_EQ(view.profile.field_count, 9u);
    EXPECT_EQ(view.claim_count, 2u);
    EXPECT_EQ(view.reference_occurrence_count, 2u);
    EXPECT_EQ(view.reference_occurrences[0].row_ordinal, 1u);
    EXPECT_EQ(view.reference_occurrences[1].row_ordinal, 2u);

    std::vector<std::uint8_t> recomposed(fixture.text.size());
    std::size_t output_bytes = 0u;
    ASSERT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 0u, recomposed.data(), recomposed.size(), &output_bytes),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_EQ(output_bytes, fixture.text.size());
    EXPECT_EQ(std::memcmp(
                  recomposed.data(), fixture.text.data(), output_bytes),
              0);
    laplace_tabular_source_plan_destroy(&plan);

    fixture.artifacts[0].expected_overflow_record_count = 0u;
    ASSERT_EQ(laplace_tabular_source_graph_identify(
                  fixture.artifacts.data(), fixture.artifacts.size(),
                  fixture.reference_rules.data(), fixture.reference_rules.size(),
                  nullptr, 0u,
                  &fixture.input.profile_declaration.artifact_graph_fingerprint),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH);
    EXPECT_EQ(plan, nullptr);
}

TEST(TabularSource, CompilesDeclaredReferencePairsIntoGenericMappingOccurrences) {
    Fixture fixture;
    fixture.artifacts[1].reference_column_mask = 3u;
    Fill(fixture.reference_rules[1].name_space, 0x65u);
    fixture.reference_rules[1].artifact_index = 1u;
    fixture.reference_rules[1].column_index = 1u;
    fixture.reference_rules[1].kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
    fixture.reference_rules[1].flags = LAPLACE_REFERENCE_RULE_ENDPOINT |
        LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION;
    fixture.input.reference_rule_count = fixture.reference_rules.size();
    static constexpr std::string_view Relation{"="};
    fixture.mapping_rules[0].relation_content =
        reinterpret_cast<const std::uint8_t*>(Relation.data());
    fixture.mapping_rules[0].relation_content_byte_count = Relation.size();
    fixture.mapping_rules[0].artifact_index = 1u;
    fixture.mapping_rules[0].left_column_index = 0u;
    fixture.mapping_rules[0].right_column_index = 1u;
    fixture.mapping_rules[0].relation_version = 1u;
    fixture.mapping_rules[0].relation_kind =
        LAPLACE_HIGHWAY_KIND_RELATION;
    fixture.mapping_rules[0].flags = LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED;
    fixture.input.mapping_rules = fixture.mapping_rules.data();
    fixture.input.mapping_rule_count = fixture.mapping_rules.size();
    RefreshGraph(fixture);

    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_EQ(view.reference_occurrence_count, 4u);
    ASSERT_EQ(view.mapping_occurrence_count, 2u);
    EXPECT_EQ(view.profile.mapping_count, 2u);
    EXPECT_EQ(view.profile.closure_subject_count,
              view.request_count + view.profile.reference_count +
                  view.profile.mapping_count);
    EXPECT_EQ(view.profile.unresolved_count,
              view.profile.reference_count + view.profile.mapping_count);
    EXPECT_EQ(view.mapping_occurrences[0].left_reference_occurrence_index, 0u);
    EXPECT_EQ(view.mapping_occurrences[0].right_reference_occurrence_index, 1u);
    EXPECT_EQ(view.mapping_occurrences[1].left_reference_occurrence_index, 2u);
    EXPECT_EQ(view.mapping_occurrences[1].right_reference_occurrence_index, 3u);
    EXPECT_LT(view.mapping_occurrences[0].row_result_index,
              view.mapping_occurrences[1].row_result_index);
    EXPECT_LT(view.mapping_occurrences[0].relation_result_index,
              view.request_count);
    EXPECT_EQ(view.mapping_occurrences[0].relation_kind,
              LAPLACE_HIGHWAY_KIND_RELATION);
    EXPECT_EQ(view.mapping_occurrences[0].flags,
              LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED);
    laplace_tabular_source_plan_destroy(&plan);

    static constexpr std::string_view MutatedRelation{"same-as"};
    fixture.mapping_rules[0].relation_content =
        reinterpret_cast<const std::uint8_t*>(MutatedRelation.data());
    fixture.mapping_rules[0].relation_content_byte_count =
        MutatedRelation.size();
    EXPECT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_PROFILE_INVALID);
    EXPECT_EQ(plan, nullptr);

    fixture.input.reference_rule_count = 1u;
    EXPECT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_PROFILE_INVALID);
    EXPECT_EQ(plan, nullptr);
}

TEST(TabularSource, HeaderlessExactTablesUseDeclaredColumnsWithoutLosingFirstRow) {
    Fixture fixture;
    fixture.text = "eng\tEnglish\njpn\t日本語\n";
    fixture.artifacts[1].bytes = reinterpret_cast<const std::uint8_t*>(
        fixture.text.data());
    fixture.artifacts[1].byte_count = fixture.text.size();
    fixture.artifacts[1].expected_record_count = 2u;
    fixture.artifacts[1].expected_field_count = 4u;
    fixture.artifacts[1].header_record_count = 0u;
    RefreshArtifact(fixture.artifacts[1]);
    RefreshGraph(fixture);

    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(view.claim_count, 2u);
    EXPECT_EQ(view.reference_occurrence_count, 2u);
    EXPECT_EQ(view.reference_occurrences[0].row_ordinal, 1u);
    EXPECT_EQ(view.reference_occurrences[1].row_ordinal, 2u);
    std::vector<std::uint8_t> output(fixture.text.size());
    std::size_t written{};
    ASSERT_EQ(laplace_tabular_source_recompose_artifact(
                  plan, 1u, output.data(), output.size(), &written),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(written, fixture.text.size());
    EXPECT_EQ(std::memcmp(
                  output.data(), fixture.text.data(), fixture.text.size()), 0);
    laplace_tabular_source_plan_destroy(&plan);
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
    EXPECT_EQ(summary.occurrence_count, 12u);
    EXPECT_GT(summary.logical_occurrence_count, 0u);
    laplace_source_profile_manifest closed_profile{};
    ASSERT_EQ(laplace_tabular_source_profile_finalize(
                  plan, &summary, &closed_profile),
              LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_EQ(closed_profile.occurrence_count, summary.occurrence_count);
    EXPECT_NE(closed_profile.occurrence_count,
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

TEST(TabularSource, MissingSourceOccurrenceIntentIsObservable) {
    Fixture fixture;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_GT(view.request_count, 1u);

    std::vector<laplace_composition_request> mutated_requests(
        view.requests, view.requests + static_cast<std::size_t>(view.request_count));
    mutated_requests[static_cast<std::size_t>(view.root_result_index)].flags &=
        ~LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE;

    std::vector<laplace_composition_known_entity> known;
    known.reserve(static_cast<std::size_t>(view.atom_count));
    for (std::size_t index = 0u;
         index < static_cast<std::size_t>(view.atom_count); ++index) {
        known.push_back(Atom(view.atom_positions[index], index));
    }
    auto context = laplace_test_context(4u);
    context.resource_grant.memory_bytes = UINT64_C(256) * 1024u * 1024u;
    const laplace_composition_working_set_input composition{
        &context,
        &view.source_fingerprint,
        &view.profile.recipe_program_fingerprint,
        known.data(),
        view.atom_count,
        view.operands,
        view.operand_count,
        mutated_requests.data(),
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
    EXPECT_EQ(summary.occurrence_count, 11u);
    EXPECT_NE(summary.occurrence_count, 12u);
    EXPECT_GT(summary.logical_occurrence_count, 0u);
    laplace_composition_working_set_destroy(&working_set);
    laplace_tabular_source_plan_destroy(&plan);
}

TEST(TabularSource, BlanketStructuralOccurrenceEmissionIsObservable) {
    Fixture fixture;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create(&fixture.input, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);

    std::vector<laplace_composition_request> mutated_requests(
        view.requests, view.requests + static_cast<std::size_t>(view.request_count));
    for (auto& request : mutated_requests) {
        request.flags |= LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE;
    }
    const auto emitted = std::count_if(
        mutated_requests.begin(), mutated_requests.end(),
        [](const laplace_composition_request& request) {
            return (request.flags &
                    LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE) != 0u;
        });
    EXPECT_EQ(static_cast<std::uint64_t>(emitted), view.request_count);
    EXPECT_GT(view.request_count, 12u);

    const auto actual = std::count_if(
        view.requests,
        view.requests + static_cast<std::size_t>(view.request_count),
        [](const laplace_composition_request& request) {
            return (request.flags &
                    LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE) != 0u;
        });
    EXPECT_EQ(static_cast<std::uint64_t>(actual), 12u);
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
    ASSERT_EQ(laplace_tabular_source_graph_identify(
                  denominator.artifacts.data(), denominator.artifacts.size(),
                  denominator.input.reference_rules,
                  static_cast<std::size_t>(
                      denominator.input.reference_rule_count),
                  denominator.input.mapping_rules,
                  static_cast<std::size_t>(
                      denominator.input.mapping_rule_count),
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
