#include "../iso_639_profile_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

void PrintHex(const char* name, const std::uint8_t* bytes, std::size_t count) {
    std::printf("%s=", name);
    for (std::size_t index = 0u; index < count; ++index) {
        std::printf("%02x", static_cast<unsigned int>(bytes[index]));
    }
    std::printf("\n");
}

void PrintNumber(const char* name, std::uint64_t value) {
    std::printf("%s=%llu\n", name,
                static_cast<unsigned long long>(value));
}

template <std::size_t Size>
void PrintArray(const char* name, const std::array<std::uint8_t, Size>& value) {
    PrintHex(name, value.data(), value.size());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s ISO-639-SOURCE-ROOT\n", argv[0]);
        return 64;
    }
    laplace::test::Iso639ProfileFixture fixture;
    if (!fixture.Load(argv[1])) {
        std::fprintf(stderr, "%s\n", fixture.error.c_str());
        return 65;
    }
    laplace_tabular_source_plan* plan = nullptr;
    if (laplace_tabular_source_plan_create(&fixture.input, &plan) !=
        LAPLACE_TABULAR_SOURCE_OK) {
        std::fprintf(stderr, "locked ISO 639 profile did not compile\n");
        return 66;
    }
    laplace_tabular_source_plan_view view{};
    if (laplace_tabular_source_plan_view_get(plan, &view) !=
        LAPLACE_TABULAR_SOURCE_OK) {
        laplace_tabular_source_plan_destroy(&plan);
        return 67;
    }
    PrintNumber("ISO_KIND", laplace::test::iso_profile::kind);
    PrintNumber("ISO_VERSION", laplace::test::iso_profile::version);
    PrintNumber(
        "ISO_RECONSTRUCTION_CLASS",
        laplace::test::iso_profile::reconstruction_class);
    PrintArray("ISO_AUTHORITY_ID", laplace::test::iso_profile::authority_id);
    PrintArray("ISO_RELEASE_ID", laplace::test::iso_profile::release_id);
    PrintArray("ISO_NAMESPACE_ID", laplace::test::iso_profile::namespace_id);
    PrintArray(
        "ISO_LOCAL_IDENTIFIER_ID",
        laplace::test::iso_profile::local_identifier_id);
    PrintArray(
        "ISO_AUTHORITY_RELEASE_FINGERPRINT",
        laplace::test::iso_profile::authority_release_fingerprint);
    PrintArray(
        "ISO_LICENSE_FINGERPRINT",
        laplace::test::iso_profile::license_fingerprint);
    PrintHex(
        "ISO_ARTIFACT_GRAPH_FINGERPRINT",
        fixture.declaration.artifact_graph_fingerprint.bytes, 32u);
    PrintArray(
        "ISO_SYNTAX_AUTHORITY_FINGERPRINT",
        laplace::test::iso_profile::syntax_authority_fingerprint);
    PrintArray(
        "ISO_RECIPE_PROGRAM_FINGERPRINT",
        laplace::test::iso_profile::recipe_program_fingerprint);
    PrintArray(
        "ISO_UNIVERSAL_AST_MAPPING_FINGERPRINT",
        laplace::test::iso_profile::universal_ast_mapping_fingerprint);
    PrintArray(
        "ISO_HIGHWAY_REFERENCES_FINGERPRINT",
        laplace::test::iso_profile::highway_references_fingerprint);
    PrintArray(
        "ISO_EPISTEMIC_WITNESSING_FINGERPRINT",
        laplace::test::iso_profile::epistemic_witnessing_fingerprint);
    PrintArray(
        "ISO_DENOMINATOR_DECLARATION_FINGERPRINT",
        laplace::test::iso_profile::denominator_declaration_fingerprint);
    PrintArray(
        "ISO_CONFORMANCE_FINGERPRINT",
        laplace::test::iso_profile::conformance_fingerprint);
    PrintArray(
        "ISO_COMPLETION_LAW_FINGERPRINT",
        laplace::test::iso_profile::completion_law_fingerprint);
    PrintArray(
        "ISO_SELECTED_BOUNDARY_FINGERPRINT",
        laplace::test::iso_profile::selected_boundary_fingerprint);
    PrintArray(
        "ISO_OCCURRENCE_CONTEXT_FINGERPRINT",
        laplace::test::iso_profile::occurrence_context_fingerprint);
    PrintNumber("ISO_ARTIFACT_COUNT", view.artifact_count);
    PrintNumber("ISO_EXPECTED_BYTES", view.profile.byte_count);
    PrintNumber("ISO_EXPECTED_RECORDS", view.profile.record_count);
    PrintNumber("ISO_EXPECTED_FIELDS", view.profile.field_count);
    PrintNumber("ISO_EXPECTED_REFERENCES", view.profile.reference_count);
    PrintNumber("ISO_EXPECTED_CLAIMS", view.profile.claim_count);
    PrintNumber("ISO_EXPECTED_REQUESTS", view.request_count);
    PrintNumber(
        "ISO_PREFERRED_BATCH_BYTES",
        laplace::test::iso_profile::preferred_batch_bytes);
    PrintNumber("ISO_REFERENCE_RULE_COUNT", fixture.reference_rules.size());
    for (std::size_t index = 0u; index < fixture.artifacts.size(); ++index) {
        const auto& generated = laplace::test::iso_profile::artifacts[index];
        const auto& artifact = fixture.artifacts[index];
        const std::string prefix = "ISO_ARTIFACT_" + std::to_string(index) + "_";
        PrintHex(
            (prefix + "ID").c_str(), artifact.artifact_id.bytes, 32u);
        PrintHex(
            (prefix + "PARENT_ID").c_str(),
            artifact.parent_artifact_id.bytes, 32u);
        PrintHex(
            (prefix + "NAME").c_str(),
            reinterpret_cast<const std::uint8_t*>(generated.name),
            std::char_traits<char>::length(generated.name));
        PrintHex(
            (prefix + "LOCAL_PATH").c_str(),
            reinterpret_cast<const std::uint8_t*>(
                generated.local_discovery_path),
            std::char_traits<char>::length(generated.local_discovery_path));
        PrintNumber((prefix + "RECORDS").c_str(), artifact.expected_record_count);
        PrintNumber((prefix + "FIELDS").c_str(), artifact.expected_field_count);
        PrintNumber(
            (prefix + "REFERENCE_MASK").c_str(),
            artifact.reference_column_mask);
        PrintNumber((prefix + "MODE").c_str(), artifact.mode);
        PrintNumber((prefix + "DELIMITER").c_str(), artifact.delimiter);
        PrintNumber(
            (prefix + "TERMINATOR").c_str(), artifact.line_terminator);
        PrintNumber(
            (prefix + "COLUMNS").c_str(), artifact.expected_column_count);
        PrintNumber(
            (prefix + "OUTCOME").c_str(), artifact.outcome_type);
        PrintNumber((prefix + "FLAGS").c_str(), artifact.flags);
    }
    for (std::size_t index = 0u;
         index < fixture.reference_rules.size(); ++index) {
        const auto& rule = fixture.reference_rules[index];
        const std::string prefix =
            "ISO_REFERENCE_RULE_" + std::to_string(index) + "_";
        PrintHex(
            (prefix + "NAMESPACE").c_str(),
            rule.name_space.bytes, sizeof(rule.name_space.bytes));
        PrintNumber((prefix + "ARTIFACT").c_str(), rule.artifact_index);
        PrintNumber((prefix + "COLUMN").c_str(), rule.column_index);
        PrintNumber((prefix + "KIND").c_str(), rule.kind);
        PrintNumber((prefix + "FLAGS").c_str(), rule.flags);
    }
    laplace_tabular_source_plan_destroy(&plan);
    return 0;
}
