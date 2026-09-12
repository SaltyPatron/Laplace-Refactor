#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "laplace/source/cili_pwn_mappings_20240611_profile.h"
#include "laplace/source/cili_pwn_mappings_20260903_profile.h"
#include "laplace/source/iso_639_3_20260415_profile.h"
#include "laplace/tabular_source.h"
#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"

namespace {

template <std::size_t Size>
void CopyBytes(void* output, const std::array<std::uint8_t, Size>& input) {
    std::memcpy(output, input.data(), input.size());
}

int HexNibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

bool ParseDigest256(const char* text, laplace_digest256* output) {
    if (text == nullptr || output == nullptr || std::strlen(text) != 64u) {
        return false;
    }
    for (std::size_t index = 0u; index < 32u; ++index) {
        const int high = HexNibble(text[index * 2u]);
        const int low = HexNibble(text[index * 2u + 1u]);
        if (high < 0 || low < 0) {
            return false;
        }
        output->bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

void PrintHex(const char* name, const std::uint8_t* bytes, std::size_t count) {
    std::printf("%s=", name);
    for (std::size_t index = 0u; index < count; ++index) {
        std::printf("%02x", static_cast<unsigned int>(bytes[index]));
    }
    std::printf("\n");
}

void PrintNumber(const char* name, std::uint64_t value) {
    std::printf("%s=%llu\n", name, static_cast<unsigned long long>(value));
}

template <std::size_t Size>
void PrintArray(const char* name, const std::array<std::uint8_t, Size>& value) {
    PrintHex(name, value.data(), value.size());
}

template <typename Profile>
struct RuntimeProfile {
    std::array<std::vector<std::uint8_t>, Profile::artifact_size> storage;
    std::array<std::vector<laplace_tabular_column>, Profile::artifact_size> columns;
    std::array<std::vector<laplace_tabular_fixed_width_field>, Profile::artifact_size>
        fixed_width_fields;
    std::array<laplace_tabular_artifact, Profile::artifact_size> artifacts{};
    std::array<laplace_tabular_reference_rule, Profile::reference_rule_size>
        reference_rules{};
    std::array<laplace_tabular_mapping_rule, Profile::mapping_rule_size> mapping_rules{};
    laplace_source_profile_manifest declaration{};
    laplace_tabular_source_input input{};
    std::string error;

    bool Load(const std::string& source_root, const laplace_digest256& geometry_epoch) {
        declaration.coordinate.kind = Profile::coordinate_kind;
        CopyBytes(declaration.coordinate.authority.bytes, Profile::authority);
        CopyBytes(declaration.coordinate.release.bytes, Profile::release);
        CopyBytes(declaration.coordinate.name_space.bytes, Profile::name_space);
        CopyBytes(declaration.coordinate.local_identifier.bytes, Profile::local_identifier);
        declaration.coordinate.version = Profile::coordinate_version;
        CopyBytes(declaration.authority_release_fingerprint.bytes, Profile::authority_release);
        CopyBytes(declaration.license_fingerprint.bytes, Profile::license);
        CopyBytes(declaration.syntax_authority_fingerprint.bytes, Profile::syntax_authority);
        CopyBytes(declaration.recipe_program_fingerprint.bytes, Profile::recipe_program);
        CopyBytes(
            declaration.universal_ast_mapping_fingerprint.bytes,
            Profile::universal_ast_mapping);
        CopyBytes(
            declaration.highway_references_fingerprint.bytes,
            Profile::highway_references);
        CopyBytes(
            declaration.epistemic_witnessing_fingerprint.bytes,
            Profile::epistemic_witnessing);
        CopyBytes(
            declaration.denominator_declaration_fingerprint.bytes,
            Profile::denominator_declaration);
        CopyBytes(declaration.conformance_fingerprint.bytes, Profile::conformance);
        CopyBytes(declaration.completion_law_fingerprint.bytes, Profile::completion_law);
        CopyBytes(
            declaration.selected_boundary_fingerprint.bytes,
            Profile::selected_boundary);
        declaration.reconstruction_class = Profile::reconstruction;
        declaration.flags = LAPLACE_SOURCE_PROFILE_MAKE_FLAGS(
            Profile::epistemic_class, Profile::evidence_type);

        for (std::size_t index = 0u; index < Profile::artifact_size; ++index) {
            const auto& generated = Profile::artifact_declarations[index];
            const std::string path = source_root + "/" + generated.local_discovery_path;
            std::ifstream stream(path, std::ios::binary);
            if (!stream) {
                error = "cannot open " + path;
                return false;
            }
            storage[index].assign(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
            if (storage[index].size() != generated.byte_count) {
                error = "byte denominator changed for " + path;
                return false;
            }
            auto& artifact = artifacts[index];
            CopyBytes(artifact.artifact_id.bytes, generated.sha256);
            CopyBytes(artifact.parent_artifact_id.bytes, generated.parent_id);
            CopyBytes(artifact.expected_sha256, generated.sha256);
            artifact.bytes = storage[index].data();
            artifact.name = generated.name;
            artifact.media_type = generated.media_type;
            columns[index].reserve(generated.column_count);
            for (std::size_t column_index = 0u;
                 column_index < generated.column_count; ++column_index) {
                const auto& column = generated.columns[column_index];
                columns[index].push_back(laplace_tabular_column{
                    reinterpret_cast<const std::uint8_t*>(column.bytes),
                    column.byte_count});
            }
            artifact.columns = columns[index].empty() ? nullptr : columns[index].data();
            fixed_width_fields[index].reserve(generated.column_count);
            if (generated.fixed_width_fields != nullptr) {
                for (std::size_t field_index = 0u;
                     field_index < generated.column_count; ++field_index) {
                    fixed_width_fields[index].push_back(laplace_tabular_fixed_width_field{
                        generated.fixed_width_fields[field_index].width,
                        generated.fixed_width_fields[field_index].flags});
                }
            }
            artifact.fixed_width_fields = fixed_width_fields[index].empty()
                ? nullptr : fixed_width_fields[index].data();
            artifact.byte_count = generated.byte_count;
            artifact.name_byte_count = std::strlen(generated.name);
            artifact.media_type_byte_count = std::strlen(generated.media_type);
            artifact.expected_record_count = generated.record_count;
            artifact.expected_field_count = generated.field_count;
            artifact.reference_column_mask = generated.reference_column_mask;
            artifact.mode = generated.mode;
            artifact.delimiter = generated.delimiter;
            artifact.line_terminator = generated.line_terminator;
            artifact.expected_column_count = generated.column_count;
            artifact.outcome_type = generated.outcome_type;
            artifact.header_record_count = generated.header_record_count;
            artifact.flags = generated.flags;
            artifact.padding_byte = generated.padding_byte;
            artifact.overflow_field_index = generated.overflow_field_index;
            artifact.maximum_overflow_bytes = generated.maximum_overflow_bytes;
            artifact.expected_overflow_record_count = generated.expected_overflow_record_count;
        }
        for (std::size_t index = 0u; index < Profile::reference_rule_size; ++index) {
            const auto& generated = Profile::reference_rule_declarations[index];
            auto& rule = reference_rules[index];
            CopyBytes(rule.name_space.bytes, generated.namespace_id);
            rule.artifact_index = generated.artifact_index;
            rule.column_index = generated.column_index;
            rule.kind = generated.kind;
            rule.flags = generated.flags;
        }
        for (std::size_t index = 0u; index < Profile::mapping_rule_size; ++index) {
            const auto& generated = Profile::mapping_rule_declarations[index];
            auto& rule = mapping_rules[index];
            rule.relation_content = reinterpret_cast<const std::uint8_t*>(
                generated.relation_content);
            rule.relation_content_byte_count = generated.relation_content_byte_count;
            rule.artifact_index = generated.artifact_index;
            rule.left_column_index = generated.left_column_index;
            rule.right_column_index = generated.right_column_index;
            rule.relation_version = generated.relation_version;
            rule.relation_kind = generated.relation_kind;
            rule.flags = generated.flags;
        }
        if (laplace_tabular_source_graph_identify(
                artifacts.data(), artifacts.size(),
                reference_rules.empty() ? nullptr : reference_rules.data(),
                reference_rules.size(),
                mapping_rules.empty() ? nullptr : mapping_rules.data(),
                mapping_rules.size(),
                &declaration.artifact_graph_fingerprint) != LAPLACE_TABULAR_SOURCE_OK) {
            error = "artifact graph validation failed";
            return false;
        }
        input.profile_declaration = declaration;
        input.geometry_epoch = geometry_epoch;
        CopyBytes(
            input.occurrence_context_fingerprint.bytes,
            Profile::occurrence_context);
        input.artifacts = artifacts.data();
        input.artifact_count = artifacts.size();
        input.reference_rules = reference_rules.empty() ? nullptr : reference_rules.data();
        input.reference_rule_count = reference_rules.size();
        input.mapping_rules = mapping_rules.empty() ? nullptr : mapping_rules.data();
        input.mapping_rule_count = mapping_rules.size();
        input.preferred_batch_bytes = Profile::batch_bytes;
        return true;
    }
};

template <typename Profile>
int RunProfile(
    const char* profile_name,
    const char* source_root,
    const char* unicode_root,
    const laplace_digest256& geometry_epoch) {
    RuntimeProfile<Profile> runtime;
    if (!runtime.Load(source_root, geometry_epoch)) {
        std::fprintf(stderr, "%s\n", runtime.error.c_str());
        return 65;
    }
    laplace_unicode_source_bundle* unicode_bundle = nullptr;
    laplace_unicode_source_receipt unicode_receipt{};
    if (laplace_unicode_source_bundle_open(
            unicode_root, &unicode_bundle, &unicode_receipt) != LAPLACE_UNICODE_OK ||
        unicode_bundle == nullptr) {
        std::fprintf(stderr, "locked Unicode source did not open\n");
        return 66;
    }
    laplace_tabular_source_plan* plan = nullptr;
    const auto status = laplace_tabular_source_plan_create_recursive(
        &runtime.input, unicode_bundle, &plan);
    if (status != LAPLACE_TABULAR_SOURCE_OK || plan == nullptr) {
        laplace_unicode_source_bundle_close(&unicode_bundle);
        std::fprintf(stderr, "source profile did not compile: status=%u\n",
                     static_cast<unsigned int>(status));
        return 67;
    }
    laplace_tabular_source_plan_view view{};
    if (laplace_tabular_source_plan_view_get(plan, &view) != LAPLACE_TABULAR_SOURCE_OK) {
        laplace_tabular_source_plan_destroy(&plan);
        laplace_unicode_source_bundle_close(&unicode_bundle);
        return 68;
    }

    std::printf("SCHEMA=laplace.source-profile-compile/v1\n");
    std::printf("PROFILE=%s\n", profile_name);
    PrintNumber("KIND", Profile::coordinate_kind);
    PrintNumber("VERSION", Profile::coordinate_version);
    PrintNumber("RECONSTRUCTION_CLASS", Profile::reconstruction);
    PrintNumber(
        "SOURCE_FLAGS",
        LAPLACE_SOURCE_PROFILE_MAKE_FLAGS(Profile::epistemic_class, Profile::evidence_type));
    PrintArray("AUTHORITY_ID", Profile::authority);
    PrintArray("RELEASE_ID", Profile::release);
    PrintArray("NAMESPACE_ID", Profile::name_space);
    PrintArray("LOCAL_IDENTIFIER_ID", Profile::local_identifier);
    PrintArray("AUTHORITY_RELEASE_FINGERPRINT", Profile::authority_release);
    PrintArray("LICENSE_FINGERPRINT", Profile::license);
    PrintHex(
        "ARTIFACT_GRAPH_FINGERPRINT",
        runtime.declaration.artifact_graph_fingerprint.bytes, 32u);
    PrintArray("SYNTAX_AUTHORITY_FINGERPRINT", Profile::syntax_authority);
    PrintArray("RECIPE_PROGRAM_FINGERPRINT", Profile::recipe_program);
    PrintArray("UNIVERSAL_AST_MAPPING_FINGERPRINT", Profile::universal_ast_mapping);
    PrintArray("HIGHWAY_REFERENCES_FINGERPRINT", Profile::highway_references);
    PrintArray("EPISTEMIC_WITNESSING_FINGERPRINT", Profile::epistemic_witnessing);
    PrintArray("DENOMINATOR_DECLARATION_FINGERPRINT", Profile::denominator_declaration);
    PrintArray("CONFORMANCE_FINGERPRINT", Profile::conformance);
    PrintArray("COMPLETION_LAW_FINGERPRINT", Profile::completion_law);
    PrintArray("SELECTED_BOUNDARY_FINGERPRINT", Profile::selected_boundary);
    PrintArray("OCCURRENCE_CONTEXT_FINGERPRINT", Profile::occurrence_context);
    PrintHex("GEOMETRY_EPOCH", runtime.input.geometry_epoch.bytes, 32u);
    PrintHex("SOURCE_FINGERPRINT", view.source_fingerprint.bytes, 32u);
    PrintHex("RECONSTRUCTION_FINGERPRINT", view.reconstruction_fingerprint.bytes, 32u);
    PrintNumber("PREFERRED_BATCH_BYTES", Profile::batch_bytes);
    PrintNumber("ARTIFACT_COUNT", runtime.artifacts.size());
    PrintNumber("REFERENCE_RULE_COUNT", runtime.reference_rules.size());
    PrintNumber("MAPPING_RULE_COUNT", runtime.mapping_rules.size());
    PrintNumber("ATOM_COUNT", view.atom_count);
    PrintNumber("OPERAND_COUNT", view.operand_count);
    PrintNumber("REQUEST_COUNT", view.request_count);
    PrintNumber("CLAIM_COUNT", view.claim_count);
    PrintNumber("REFERENCE_OCCURRENCE_COUNT", view.reference_occurrence_count);
    PrintNumber("MAPPING_OCCURRENCE_COUNT", view.mapping_occurrence_count);

    for (std::size_t index = 0u; index < runtime.artifacts.size(); ++index) {
        const auto& generated = Profile::artifact_declarations[index];
        const auto& artifact = runtime.artifacts[index];
        const std::string prefix = "ARTIFACT_" + std::to_string(index) + "_";
        PrintHex((prefix + "ID").c_str(), artifact.artifact_id.bytes, 32u);
        PrintHex((prefix + "PARENT_ID").c_str(), artifact.parent_artifact_id.bytes, 32u);
        PrintHex((prefix + "EXPECTED_SHA256").c_str(), artifact.expected_sha256, 32u);
        PrintHex(
            (prefix + "NAME").c_str(),
            reinterpret_cast<const std::uint8_t*>(artifact.name),
            static_cast<std::size_t>(artifact.name_byte_count));
        PrintHex(
            (prefix + "MEDIA_TYPE").c_str(),
            reinterpret_cast<const std::uint8_t*>(artifact.media_type),
            static_cast<std::size_t>(artifact.media_type_byte_count));
        PrintHex(
            (prefix + "LOCAL_PATH").c_str(),
            reinterpret_cast<const std::uint8_t*>(generated.local_discovery_path),
            std::strlen(generated.local_discovery_path));
        PrintNumber((prefix + "RECORDS").c_str(), artifact.expected_record_count);
        PrintNumber((prefix + "FIELDS").c_str(), artifact.expected_field_count);
        PrintNumber((prefix + "REFERENCE_MASK").c_str(), artifact.reference_column_mask);
        PrintNumber((prefix + "MODE").c_str(), artifact.mode);
        PrintNumber((prefix + "DELIMITER").c_str(), artifact.delimiter);
        PrintNumber((prefix + "TERMINATOR").c_str(), artifact.line_terminator);
        PrintNumber((prefix + "COLUMNS").c_str(), artifact.expected_column_count);
        PrintNumber((prefix + "OUTCOME").c_str(), artifact.outcome_type);
        PrintNumber((prefix + "FLAGS").c_str(), artifact.flags);
        PrintNumber((prefix + "HEADER_RECORDS").c_str(), artifact.header_record_count);
        for (std::size_t column = 0u; column < artifact.expected_column_count; ++column) {
            PrintHex(
                (prefix + "COLUMN_" + std::to_string(column)).c_str(),
                artifact.columns[column].bytes,
                static_cast<std::size_t>(artifact.columns[column].byte_count));
        }
    }
    for (std::size_t index = 0u; index < runtime.reference_rules.size(); ++index) {
        const auto& rule = runtime.reference_rules[index];
        const std::string prefix = "REFERENCE_RULE_" + std::to_string(index) + "_";
        PrintHex((prefix + "NAMESPACE").c_str(), rule.name_space.bytes, 16u);
        PrintNumber((prefix + "ARTIFACT").c_str(), rule.artifact_index);
        PrintNumber((prefix + "COLUMN").c_str(), rule.column_index);
        PrintNumber((prefix + "KIND").c_str(), rule.kind);
        PrintNumber((prefix + "FLAGS").c_str(), rule.flags);
    }
    for (std::size_t index = 0u; index < runtime.mapping_rules.size(); ++index) {
        const auto& rule = runtime.mapping_rules[index];
        const std::string prefix = "MAPPING_RULE_" + std::to_string(index) + "_";
        PrintHex(
            (prefix + "RELATION").c_str(), rule.relation_content,
            static_cast<std::size_t>(rule.relation_content_byte_count));
        PrintNumber((prefix + "ARTIFACT").c_str(), rule.artifact_index);
        PrintNumber((prefix + "LEFT_COLUMN").c_str(), rule.left_column_index);
        PrintNumber((prefix + "RIGHT_COLUMN").c_str(), rule.right_column_index);
        PrintNumber((prefix + "VERSION").c_str(), rule.relation_version);
        PrintNumber((prefix + "KIND").c_str(), rule.relation_kind);
        PrintNumber((prefix + "FLAGS").c_str(), rule.flags);
    }

    laplace_tabular_source_plan_destroy(&plan);
    laplace_unicode_source_bundle_close(&unicode_bundle);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(
            stderr,
            "usage: %s PROFILE SOURCE-ROOT UNICODE-SOURCE-ROOT GEOMETRY-EPOCH-HEX\n"
            "profiles: iso-639-3-20260415, cili-pwn-mappings-20240611, cili-pwn-mappings-20260903\n",
            argv[0]);
        return 64;
    }
    laplace_digest256 geometry_epoch{};
    if (!ParseDigest256(argv[4], &geometry_epoch)) {
        std::fprintf(stderr, "geometry epoch must be 64 lowercase hexadecimal characters\n");
        return 64;
    }
    const std::string profile = argv[1];
    if (profile == "iso-639-3-20260415") {
        return RunProfile<laplace::generated::iso_639_3_20260415::Profile>(
            argv[1], argv[2], argv[3], geometry_epoch);
    }
    if (profile == "cili-pwn-mappings-20240611") {
        return RunProfile<laplace::generated::cili_pwn_mappings_20240611::Profile>(
            argv[1], argv[2], argv[3], geometry_epoch);
    }
    if (profile == "cili-pwn-mappings-20260903") {
        return RunProfile<laplace::generated::cili_pwn_mappings_20260903::Profile>(
            argv[1], argv[2], argv[3], geometry_epoch);
    }
    std::fprintf(stderr, "unsupported source profile: %s\n", argv[1]);
    return 64;
}
