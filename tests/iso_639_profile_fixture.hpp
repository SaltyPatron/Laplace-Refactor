#ifndef LAPLACE_TEST_ISO_639_PROFILE_FIXTURE_HPP
#define LAPLACE_TEST_ISO_639_PROFILE_FIXTURE_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "laplace/source/iso_639_3_20260415_profile.h"
#include "laplace/tabular_source.h"

namespace laplace::test {

namespace iso_profile = generated::iso_639_3_20260415;

template <std::size_t Size>
void CopyBytes(void* output, const std::array<std::uint8_t, Size>& input) {
    std::memcpy(output, input.data(), input.size());
}

struct Iso639ProfileFixture {
    std::array<std::vector<std::uint8_t>, iso_profile::artifact_count> storage;
    std::array<laplace_tabular_artifact, iso_profile::artifact_count> artifacts{};
    std::array<laplace_tabular_reference_rule,
               iso_profile::reference_rule_count> reference_rules{};
    laplace_source_profile_manifest declaration{};
    laplace_tabular_source_input input{};
    std::string error;

    bool Load(const std::string& source_root) {
        declaration.coordinate.kind = iso_profile::kind;
        CopyBytes(declaration.coordinate.authority.bytes, iso_profile::authority_id);
        CopyBytes(declaration.coordinate.release.bytes, iso_profile::release_id);
        CopyBytes(
            declaration.coordinate.name_space.bytes, iso_profile::namespace_id);
        CopyBytes(
            declaration.coordinate.local_identifier.bytes,
            iso_profile::local_identifier_id);
        declaration.coordinate.version = iso_profile::version;
        CopyBytes(
            declaration.authority_release_fingerprint.bytes,
            iso_profile::authority_release_fingerprint);
        CopyBytes(
            declaration.license_fingerprint.bytes,
            iso_profile::license_fingerprint);
        CopyBytes(
            declaration.syntax_authority_fingerprint.bytes,
            iso_profile::syntax_authority_fingerprint);
        CopyBytes(
            declaration.recipe_program_fingerprint.bytes,
            iso_profile::recipe_program_fingerprint);
        CopyBytes(
            declaration.universal_ast_mapping_fingerprint.bytes,
            iso_profile::universal_ast_mapping_fingerprint);
        CopyBytes(
            declaration.highway_references_fingerprint.bytes,
            iso_profile::highway_references_fingerprint);
        CopyBytes(
            declaration.epistemic_witnessing_fingerprint.bytes,
            iso_profile::epistemic_witnessing_fingerprint);
        CopyBytes(
            declaration.denominator_declaration_fingerprint.bytes,
            iso_profile::denominator_declaration_fingerprint);
        CopyBytes(
            declaration.conformance_fingerprint.bytes,
            iso_profile::conformance_fingerprint);
        CopyBytes(
            declaration.completion_law_fingerprint.bytes,
            iso_profile::completion_law_fingerprint);
        CopyBytes(
            declaration.selected_boundary_fingerprint.bytes,
            iso_profile::selected_boundary_fingerprint);
        declaration.reconstruction_class = iso_profile::reconstruction_class;

        for (std::size_t index = 0u;
             index < iso_profile::artifact_count; ++index) {
            const auto& generated = iso_profile::artifacts[index];
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
            CopyBytes(artifact.artifact_id.bytes, generated.sha256);
            CopyBytes(artifact.parent_artifact_id.bytes, generated.parent_id);
            CopyBytes(artifact.expected_sha256, generated.sha256);
            artifact.bytes = storage[index].data();
            artifact.name = generated.name;
            artifact.byte_count = generated.byte_count;
            artifact.name_byte_count = std::strlen(generated.name);
            artifact.expected_record_count = generated.record_count;
            artifact.expected_field_count = generated.field_count;
            artifact.reference_column_mask = generated.reference_column_mask;
            artifact.mode = generated.mode;
            artifact.delimiter = generated.delimiter;
            artifact.line_terminator = generated.line_terminator;
            artifact.expected_column_count = generated.column_count;
            artifact.outcome_type = generated.outcome_type;
            artifact.flags = generated.flags;
        }
        for (std::size_t index = 0u;
             index < iso_profile::reference_rule_count; ++index) {
            const auto& generated = iso_profile::reference_rules[index];
            auto& rule = reference_rules[index];
            CopyBytes(rule.name_space.bytes, generated.namespace_id);
            rule.artifact_index = generated.artifact_index;
            rule.column_index = generated.column_index;
            rule.kind = generated.kind;
            rule.flags = generated.flags;
        }
        if (laplace_tabular_artifact_graph_identify(
                artifacts.data(), artifacts.size(),
                &declaration.artifact_graph_fingerprint) !=
            LAPLACE_TABULAR_SOURCE_OK) {
            error = "artifact graph validation failed";
            return false;
        }
        input.profile_declaration = declaration;
        CopyBytes(
            input.geometry_epoch.bytes,
            iso_profile::native_geometry_fixture_fingerprint);
        CopyBytes(
            input.occurrence_context_fingerprint.bytes,
            iso_profile::occurrence_context_fingerprint);
        input.artifacts = artifacts.data();
        input.artifact_count = artifacts.size();
        input.reference_rules = reference_rules.data();
        input.reference_rule_count = reference_rules.size();
        input.preferred_batch_bytes = iso_profile::preferred_batch_bytes;
        return true;
    }
};

}  // namespace laplace::test

#endif
