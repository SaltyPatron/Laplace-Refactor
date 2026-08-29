#ifndef LAPLACE_TEST_POSTGRES_TABULAR_PROFILE_PROBE_HPP
#define LAPLACE_TEST_POSTGRES_TABULAR_PROFILE_PROBE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "laplace/tabular_source.h"

namespace laplace::test {

inline void PrintProfileHex(
    const std::string& name, const std::uint8_t* bytes, std::size_t count) {
    std::printf("%s=", name.c_str());
    for (std::size_t index = 0u; index < count; ++index) {
        std::printf("%02x", static_cast<unsigned int>(bytes[index]));
    }
    std::printf("\n");
}

inline void PrintProfileNumber(const std::string& name, std::uint64_t value) {
    std::printf("%s=%llu\n", name.c_str(),
                static_cast<unsigned long long>(value));
}

template <std::size_t Size>
void PrintProfileArray(
    const std::string& name,
    const std::array<std::uint8_t, Size>& value) {
    PrintProfileHex(name, value.data(), value.size());
}

template <typename Profile, typename Fixture>
int RunTabularProfileProbe(
    int argc, char** argv, const std::string& prefix,
    const char* usage_label) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s %s-SOURCE-ROOT\n", argv[0], usage_label);
        return 64;
    }
    Fixture fixture;
    if (!fixture.Load(argv[1])) {
        std::fprintf(stderr, "%s\n", fixture.error.c_str());
        return 65;
    }
    laplace_tabular_source_plan* plan = nullptr;
    if (laplace_tabular_source_plan_create(&fixture.input, &plan) !=
        LAPLACE_TABULAR_SOURCE_OK) {
        std::fprintf(stderr, "locked %s profile did not compile\n", usage_label);
        return 66;
    }
    laplace_tabular_source_plan_view view{};
    if (laplace_tabular_source_plan_view_get(plan, &view) !=
        LAPLACE_TABULAR_SOURCE_OK) {
        laplace_tabular_source_plan_destroy(&plan);
        return 67;
    }
    PrintProfileNumber(prefix + "_KIND", Profile::coordinate_kind);
    PrintProfileNumber(prefix + "_VERSION", Profile::coordinate_version);
    PrintProfileNumber(
        prefix + "_RECONSTRUCTION_CLASS", Profile::reconstruction);
    PrintProfileNumber(
        prefix + "_SOURCE_FLAGS",
        LAPLACE_SOURCE_PROFILE_MAKE_FLAGS(
            Profile::epistemic_class, Profile::evidence_type));
    PrintProfileArray(prefix + "_AUTHORITY_ID", Profile::authority);
    PrintProfileArray(prefix + "_RELEASE_ID", Profile::release);
    PrintProfileArray(prefix + "_NAMESPACE_ID", Profile::name_space);
    PrintProfileArray(
        prefix + "_LOCAL_IDENTIFIER_ID", Profile::local_identifier);
    PrintProfileArray(
        prefix + "_AUTHORITY_RELEASE_FINGERPRINT",
        Profile::authority_release);
    PrintProfileArray(
        prefix + "_LICENSE_FINGERPRINT", Profile::license);
    PrintProfileHex(
        prefix + "_ARTIFACT_GRAPH_FINGERPRINT",
        fixture.declaration.artifact_graph_fingerprint.bytes, 32u);
    PrintProfileArray(
        prefix + "_SYNTAX_AUTHORITY_FINGERPRINT",
        Profile::syntax_authority);
    PrintProfileArray(
        prefix + "_RECIPE_PROGRAM_FINGERPRINT",
        Profile::recipe_program);
    PrintProfileArray(
        prefix + "_UNIVERSAL_AST_MAPPING_FINGERPRINT",
        Profile::universal_ast_mapping);
    PrintProfileArray(
        prefix + "_HIGHWAY_REFERENCES_FINGERPRINT",
        Profile::highway_references);
    PrintProfileArray(
        prefix + "_EPISTEMIC_WITNESSING_FINGERPRINT",
        Profile::epistemic_witnessing);
    PrintProfileArray(
        prefix + "_DENOMINATOR_DECLARATION_FINGERPRINT",
        Profile::denominator_declaration);
    PrintProfileArray(
        prefix + "_CONFORMANCE_FINGERPRINT",
        Profile::conformance);
    PrintProfileArray(
        prefix + "_COMPLETION_LAW_FINGERPRINT",
        Profile::completion_law);
    PrintProfileArray(
        prefix + "_SELECTED_BOUNDARY_FINGERPRINT",
        Profile::selected_boundary);
    PrintProfileArray(
        prefix + "_OCCURRENCE_CONTEXT_FINGERPRINT",
        Profile::occurrence_context);
    PrintProfileNumber(prefix + "_ARTIFACT_COUNT", view.artifact_count);
    PrintProfileNumber(prefix + "_EXPECTED_BYTES", view.profile.byte_count);
    PrintProfileNumber(prefix + "_EXPECTED_RECORDS", view.profile.record_count);
    PrintProfileNumber(prefix + "_EXPECTED_FIELDS", view.profile.field_count);
    PrintProfileNumber(
        prefix + "_EXPECTED_REFERENCES", view.profile.reference_count);
    PrintProfileNumber(
        prefix + "_EXPECTED_REFERENCE_COORDINATES",
        Profile::reference_coordinate_count);
    PrintProfileNumber(prefix + "_EXPECTED_CLAIMS", view.profile.claim_count);
    PrintProfileNumber(prefix + "_EXPECTED_MAPPINGS", view.profile.mapping_count);
    PrintProfileNumber(prefix + "_EXPECTED_REQUESTS", view.request_count);
    PrintProfileNumber(
        prefix + "_PREFERRED_BATCH_BYTES", Profile::batch_bytes);
    PrintProfileNumber(
        prefix + "_REFERENCE_RULE_COUNT", fixture.reference_rules.size());
    PrintProfileNumber(
        prefix + "_MAPPING_RULE_COUNT", fixture.mapping_rules.size());
    for (std::size_t index = 0u; index < fixture.artifacts.size(); ++index) {
        const auto& generated = Profile::artifact_declarations[index];
        const auto& artifact = fixture.artifacts[index];
        const std::string item = prefix + "_ARTIFACT_" +
            std::to_string(index) + "_";
        PrintProfileHex(item + "ID", artifact.artifact_id.bytes, 32u);
        PrintProfileHex(
            item + "PARENT_ID", artifact.parent_artifact_id.bytes, 32u);
        PrintProfileHex(
            item + "NAME",
            reinterpret_cast<const std::uint8_t*>(generated.name),
            std::char_traits<char>::length(generated.name));
        PrintProfileHex(
            item + "MEDIA_TYPE",
            reinterpret_cast<const std::uint8_t*>(generated.media_type),
            std::char_traits<char>::length(generated.media_type));
        PrintProfileHex(
            item + "LOCAL_PATH",
            reinterpret_cast<const std::uint8_t*>(generated.local_discovery_path),
            std::char_traits<char>::length(generated.local_discovery_path));
        PrintProfileNumber(item + "RECORDS", artifact.expected_record_count);
        PrintProfileNumber(item + "FIELDS", artifact.expected_field_count);
        PrintProfileNumber(item + "REFERENCE_MASK",
                           artifact.reference_column_mask);
        PrintProfileNumber(item + "MODE", artifact.mode);
        PrintProfileNumber(item + "DELIMITER", artifact.delimiter);
        PrintProfileNumber(item + "TERMINATOR", artifact.line_terminator);
        PrintProfileNumber(item + "COLUMNS", artifact.expected_column_count);
        PrintProfileNumber(
            item + "HEADER_RECORDS", artifact.header_record_count);
        PrintProfileNumber(item + "OUTCOME", artifact.outcome_type);
        PrintProfileNumber(item + "FLAGS", artifact.flags);
        for (std::size_t column = 0u;
             column < artifact.expected_column_count; ++column) {
            PrintProfileHex(
                item + "COLUMN_" + std::to_string(column),
                artifact.columns[column].bytes,
                static_cast<std::size_t>(artifact.columns[column].byte_count));
        }
    }
    for (std::size_t index = 0u;
         index < fixture.reference_rules.size(); ++index) {
        const auto& rule = fixture.reference_rules[index];
        const std::string item = prefix + "_REFERENCE_RULE_" +
            std::to_string(index) + "_";
        PrintProfileHex(
            item + "NAMESPACE", rule.name_space.bytes,
            sizeof(rule.name_space.bytes));
        PrintProfileNumber(item + "ARTIFACT", rule.artifact_index);
        PrintProfileNumber(item + "COLUMN", rule.column_index);
        PrintProfileNumber(item + "KIND", rule.kind);
        PrintProfileNumber(item + "FLAGS", rule.flags);
    }
    for (std::size_t index = 0u;
         index < fixture.mapping_rules.size(); ++index) {
        const auto& rule = fixture.mapping_rules[index];
        const std::string item = prefix + "_MAPPING_RULE_" +
            std::to_string(index) + "_";
        PrintProfileHex(
            item + "RELATION", rule.relation_content,
            static_cast<std::size_t>(rule.relation_content_byte_count));
        PrintProfileNumber(item + "ARTIFACT", rule.artifact_index);
        PrintProfileNumber(item + "LEFT_COLUMN", rule.left_column_index);
        PrintProfileNumber(item + "RIGHT_COLUMN", rule.right_column_index);
        PrintProfileNumber(item + "VERSION", rule.relation_version);
        PrintProfileNumber(item + "KIND", rule.relation_kind);
        PrintProfileNumber(item + "FLAGS", rule.flags);
    }
    laplace_tabular_source_plan_destroy(&plan);
    return 0;
}

}  // namespace laplace::test

#endif
