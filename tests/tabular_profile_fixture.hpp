#ifndef LAPLACE_TEST_TABULAR_PROFILE_FIXTURE_HPP
#define LAPLACE_TEST_TABULAR_PROFILE_FIXTURE_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "laplace/tabular_source.h"

namespace laplace::test {

template <std::size_t Size>
void CopyProfileBytes(
    void* output, const std::array<std::uint8_t, Size>& input) {
    std::memcpy(output, input.data(), input.size());
}

template <typename Profile>
struct TabularProfileFixture {
    std::array<std::vector<std::uint8_t>, Profile::artifact_size> storage;
    std::array<std::vector<laplace_tabular_column>,
               Profile::artifact_size> columns;
    std::array<laplace_tabular_artifact, Profile::artifact_size> artifacts{};
    std::array<laplace_tabular_reference_rule,
               Profile::reference_rule_size> reference_rules{};
    std::array<laplace_tabular_mapping_rule,
               Profile::mapping_rule_size> mapping_rules{};
    laplace_source_profile_manifest declaration{};
    laplace_tabular_source_input input{};
    std::string error;

    bool Load(const std::string& source_root) {
        declaration.coordinate.kind = Profile::coordinate_kind;
        CopyProfileBytes(declaration.coordinate.authority.bytes,
                         Profile::authority);
        CopyProfileBytes(declaration.coordinate.release.bytes, Profile::release);
        CopyProfileBytes(declaration.coordinate.name_space.bytes,
                         Profile::name_space);
        CopyProfileBytes(declaration.coordinate.local_identifier.bytes,
                         Profile::local_identifier);
        declaration.coordinate.version = Profile::coordinate_version;
        CopyProfileBytes(declaration.authority_release_fingerprint.bytes,
                         Profile::authority_release);
        CopyProfileBytes(declaration.license_fingerprint.bytes, Profile::license);
        CopyProfileBytes(declaration.syntax_authority_fingerprint.bytes,
                         Profile::syntax_authority);
        CopyProfileBytes(declaration.recipe_program_fingerprint.bytes,
                         Profile::recipe_program);
        CopyProfileBytes(declaration.universal_ast_mapping_fingerprint.bytes,
                         Profile::universal_ast_mapping);
        CopyProfileBytes(declaration.highway_references_fingerprint.bytes,
                         Profile::highway_references);
        CopyProfileBytes(declaration.epistemic_witnessing_fingerprint.bytes,
                         Profile::epistemic_witnessing);
        CopyProfileBytes(declaration.denominator_declaration_fingerprint.bytes,
                         Profile::denominator_declaration);
        CopyProfileBytes(declaration.conformance_fingerprint.bytes,
                         Profile::conformance);
        CopyProfileBytes(declaration.completion_law_fingerprint.bytes,
                         Profile::completion_law);
        CopyProfileBytes(declaration.selected_boundary_fingerprint.bytes,
                         Profile::selected_boundary);
        declaration.reconstruction_class = Profile::reconstruction;
        declaration.flags = LAPLACE_SOURCE_PROFILE_MAKE_FLAGS(
            Profile::epistemic_class, Profile::evidence_type);

        for (std::size_t index = 0u; index < Profile::artifact_size; ++index) {
            const auto& generated = Profile::artifact_declarations[index];
            const std::string path = source_root + "/" +
                generated.local_discovery_path;
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
            CopyProfileBytes(artifact.artifact_id.bytes, generated.sha256);
            CopyProfileBytes(
                artifact.parent_artifact_id.bytes, generated.parent_id);
            CopyProfileBytes(artifact.expected_sha256, generated.sha256);
            artifact.bytes = storage[index].data();
            artifact.name = generated.name;
            columns[index].reserve(generated.column_count);
            for (std::size_t column_index = 0u;
                 column_index < generated.column_count; ++column_index) {
                const auto& column = generated.columns[column_index];
                columns[index].push_back(laplace_tabular_column{
                    reinterpret_cast<const std::uint8_t*>(column.bytes),
                    column.byte_count});
            }
            artifact.columns = columns[index].empty()
                ? nullptr : columns[index].data();
            artifact.byte_count = generated.byte_count;
            artifact.name_byte_count = std::strlen(generated.name);
            artifact.expected_record_count = generated.record_count;
            artifact.expected_field_count = generated.field_count;
            artifact.reference_column_mask = generated.reference_column_mask;
            artifact.mode = generated.mode;
            artifact.delimiter = generated.delimiter;
            artifact.line_terminator = generated.line_terminator;
            artifact.expected_column_count = generated.column_count;
            artifact.header_record_count = generated.header_record_count;
            artifact.outcome_type = generated.outcome_type;
            artifact.flags = generated.flags;
        }
        for (std::size_t index = 0u;
             index < Profile::reference_rule_size; ++index) {
            const auto& generated =
                Profile::reference_rule_declarations[index];
            auto& rule = reference_rules[index];
            CopyProfileBytes(rule.name_space.bytes, generated.namespace_id);
            rule.artifact_index = generated.artifact_index;
            rule.column_index = generated.column_index;
            rule.kind = generated.kind;
            rule.flags = generated.flags;
        }
        for (std::size_t index = 0u;
             index < Profile::mapping_rule_size; ++index) {
            const auto& generated = Profile::mapping_rule_declarations[index];
            auto& rule = mapping_rules[index];
            rule.relation_content = reinterpret_cast<const std::uint8_t*>(
                generated.relation_content);
            rule.relation_content_byte_count =
                generated.relation_content_byte_count;
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
                &declaration.artifact_graph_fingerprint) !=
            LAPLACE_TABULAR_SOURCE_OK) {
            error = "artifact graph validation failed";
            return false;
        }
        input.profile_declaration = declaration;
        CopyProfileBytes(input.geometry_epoch.bytes,
                         Profile::native_geometry_fixture);
        CopyProfileBytes(input.occurrence_context_fingerprint.bytes,
                         Profile::occurrence_context);
        input.artifacts = artifacts.data();
        input.artifact_count = artifacts.size();
        input.reference_rules = reference_rules.empty()
            ? nullptr : reference_rules.data();
        input.reference_rule_count = reference_rules.size();
        input.mapping_rules = mapping_rules.empty()
            ? nullptr : mapping_rules.data();
        input.mapping_rule_count = mapping_rules.size();
        input.preferred_batch_bytes = Profile::batch_bytes;
        return true;
    }
};

}  // namespace laplace::test

#endif
