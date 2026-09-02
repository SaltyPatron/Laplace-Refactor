#include "laplace/tabular_source.h"
#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

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
    value.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    return value;
}

struct Fixture {
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
    std::array<laplace_tabular_reference_rule, 1> references{};
    laplace_source_profile_manifest declaration{Declaration()};
    laplace_tabular_source_input input{};

    Fixture() {
        static constexpr std::array<std::string_view, 3> Names{{
            "ID", "Name", "Q"}};
        auto& artifact = artifacts[0];
        artifact.bytes = reinterpret_cast<const std::uint8_t*>(text.data());
        artifact.name = "players.txt";
        artifact.media_type = nullptr;
        artifact.byte_count = text.size();
        artifact.name_byte_count = std::strlen(artifact.name);
        artifact.media_type_byte_count = 0u;
        artifact.expected_record_count = 3u;
        artifact.expected_field_count = 9u;
        artifact.reference_column_mask = 1u;
        artifact.mode = LAPLACE_TABULAR_ARTIFACT_FIXED_WIDTH;
        artifact.line_terminator = LAPLACE_TABULAR_TERMINATOR_CRLF;
        artifact.expected_column_count = static_cast<std::uint32_t>(columns.size());
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
                Names[index].data());
            columns[index].byte_count = Names[index].size();
        }
        artifact.columns = columns.data();
        const auto sha = laplace::internal::Sha256(
            artifact.bytes, static_cast<std::size_t>(artifact.byte_count));
        std::memcpy(artifact.expected_sha256, sha.data(), sha.size());
        std::memcpy(artifact.artifact_id.bytes, sha.data(), sha.size());

        Fill(references[0].name_space, 0x66u);
        references[0].artifact_index = 0u;
        references[0].column_index = 0u;
        references[0].kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
        references[0].flags = LAPLACE_REFERENCE_RULE_ENDPOINT |
            LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION;
        EXPECT_EQ(laplace_tabular_source_graph_identify(
                      artifacts.data(), artifacts.size(),
                      references.data(), references.size(), nullptr, 0u,
                      &declaration.artifact_graph_fingerprint),
                  LAPLACE_TABULAR_SOURCE_OK);
        input.profile_declaration = declaration;
        Fill(input.geometry_epoch, 0xc1u);
        Fill(input.occurrence_context_fingerprint, 0xd1u);
        input.artifacts = artifacts.data();
        input.artifact_count = artifacts.size();
        input.reference_rules = references.data();
        input.reference_rule_count = references.size();
        input.preferred_batch_bytes = 4096u;
    }
};

TEST(TabularFixedWidthMedia, MissingMediaTypeUsesUtf8PlainTextNotDelimitedTsv) {
    const char* unicode_environment = std::getenv("LAPLACE_UNICODE_SOURCE_ROOT");
    if (unicode_environment == nullptr || unicode_environment[0] == '\0' ||
        !std::filesystem::is_directory(unicode_environment)) {
        GTEST_SKIP() << "pinned Unicode source root unavailable";
    }

    laplace_unicode_source_bundle* unicode_bundle = nullptr;
    laplace_unicode_source_receipt unicode_receipt{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  unicode_environment, &unicode_bundle, &unicode_receipt),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(unicode_bundle, nullptr);

    Fixture fixture;
    laplace_tabular_source_plan* plan = nullptr;
    ASSERT_EQ(laplace_tabular_source_plan_create_recursive(
                  &fixture.input, unicode_bundle, &plan),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_NE(plan, nullptr);

    laplace_tabular_source_plan_view view{};
    ASSERT_EQ(laplace_tabular_source_plan_view_get(plan, &view),
              LAPLACE_TABULAR_SOURCE_OK);
    ASSERT_GT(view.decomposition_witness_count, 0u);
    ASSERT_NE(view.decomposition_witnesses, nullptr);
    ASSERT_NE(view.decomposition_witness_media_types, nullptr);

    static constexpr std::string_view Expected{"text/plain; charset=utf-8"};
    static constexpr std::string_view Wrong{"text/tab-separated-values"};
    const auto& witness = view.decomposition_witnesses[0];
    ASSERT_EQ(witness.artifact_index, 0u);
    ASSERT_LE(
        witness.media_type_byte_offset + witness.media_type_byte_count,
        view.decomposition_witness_media_type_byte_count);
    const std::string_view observed{
        reinterpret_cast<const char*>(
            view.decomposition_witness_media_types + witness.media_type_byte_offset),
        static_cast<std::size_t>(witness.media_type_byte_count)};
    EXPECT_EQ(observed, Expected);
    EXPECT_NE(observed, Wrong);

    laplace_tabular_source_plan_destroy(&plan);
    laplace_unicode_source_bundle_close(&unicode_bundle);
    EXPECT_EQ(plan, nullptr);
    EXPECT_EQ(unicode_bundle, nullptr);
}

}  // namespace
