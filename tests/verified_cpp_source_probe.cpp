// Actual locked parser -> shared decomposition -> canonical source-plan probe.
// A separate PostgreSQL fixture owns durable database/readback acceptance.
#include "laplace/source_decomposition.h"
#include "laplace/tree_sitter_grammar.h"
#include "sha256_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace {
laplace_digest256 Digest(const std::string& text) {
    laplace_digest256 value{};
    const auto bytes = laplace::internal::Sha256(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    std::memcpy(value.bytes, bytes.data(), bytes.size());
    return value;
}

laplace_source_profile_manifest Declaration() {
    laplace_source_profile_manifest value{};
    value.coordinate.kind = LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE;
    value.coordinate.version = 1u;
    auto identity = Digest("verified C++ fixture source observation");
    std::memcpy(value.coordinate.authority.bytes, identity.bytes, 16u);
    std::memcpy(value.coordinate.release.bytes, identity.bytes + 16u, 16u);
    value.coordinate.name_space = value.coordinate.authority;
    value.coordinate.local_identifier = value.coordinate.release;
    value.authority_release_fingerprint = Digest("fixture authority/release");
    value.license_fingerprint = Digest("fixture: CC0");
    value.syntax_authority_fingerprint = Digest("verified C++ fixture grammar declaration");
    value.recipe_program_fingerprint = Digest("exact Unicode content plus observed syntax");
    value.universal_ast_mapping_fingerprint = Digest("native concrete syntax witnesses only");
    value.highway_references_fingerprint = Digest("no resolved executable references");
    value.epistemic_witnessing_fingerprint = Digest("observation; zero semantic testimony");
    value.denominator_declaration_fingerprint = Digest("all exact bytes and emitted spans");
    value.conformance_fingerprint = Digest("verified_cpp_source_probe.cpp/v1");
    value.completion_law_fingerprint = Digest("exact reconstruction and syntax error retention");
    value.selected_boundary_fingerprint = Digest("one exact C++ translation unit");
    value.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    value.flags = LAPLACE_SOURCE_PROFILE_MAKE_FLAGS(
        LAPLACE_SOURCE_PROFILE_EPISTEMIC_OBSERVATION, LAPLACE_SOURCE_PROFILE_EVIDENCE_CORPUS);
    return value;
}

int Check(const std::string& source, const laplace_decomposition_provider_v1* provider,
          const bool expect_error, const bool expect_missing, const bool expect_empty = false) {
    laplace_tabular_artifact artifact{};
    artifact.artifact_id = Digest(source);
    std::memcpy(artifact.expected_sha256, artifact.artifact_id.bytes, 32u);
    artifact.bytes = reinterpret_cast<const std::uint8_t*>(source.data());
    artifact.byte_count = source.size();
    artifact.name = "fixture.cpp";
    artifact.name_byte_count = 11u;
    artifact.media_type = "text/x-c++";
    artifact.media_type_byte_count = 10u;
    artifact.mode = LAPLACE_TABULAR_ARTIFACT_RAW;
    artifact.flags = LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;
    laplace_tabular_source_input input{};
    input.profile_declaration = Declaration();
    input.artifacts = &artifact;
    input.artifact_count = 1u;
    input.geometry_epoch = Digest("fixture geometry");
    input.occurrence_context_fingerprint = Digest("fixture occurrence context");
    input.preferred_batch_bytes = 65536u;
    if (laplace_tabular_source_graph_identify(&artifact, 1u, nullptr, 0u, nullptr, 0u,
            &input.profile_declaration.artifact_graph_fingerprint) != LAPLACE_TABULAR_SOURCE_OK) return 10;
    laplace_tabular_source_plan* plan = nullptr;
    const auto status = laplace_source_decomposition_plan_create_bounded(&input, provider, 1u, 256u, &plan);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        std::fprintf(stderr, "actual source plan failed: status=%u\n", static_cast<unsigned>(status));
        return 11;
    }
    laplace_tabular_source_plan_view view{};
    if (laplace_tabular_source_plan_view_get(plan, &view) != LAPLACE_TABULAR_SOURCE_OK ||
        view.profile.claim_count != 0u || view.claim_count != 0u ||
        view.profile.record_count != 0u || view.profile.field_count != 0u) return 12;
    const std::size_t count = static_cast<std::size_t>(view.decomposition_witness_count);
    const auto* witnesses = view.decomposition_witnesses;
    std::size_t errors = 0u, missing = 0u, empty = 0u, role_count = 0u;
    std::uint32_t depth = 0u;
    if (witnesses == nullptr || count < 2u) return 13;
    for (std::size_t index = 0u; index < count; ++index) {
        const auto& witness = witnesses[index];
        const bool is_missing = (witness.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_MISSING) != 0u;
        errors += (witness.syntax_flags & (LAPLACE_DECOMPOSITION_SYNTAX_ERROR |
            LAPLACE_DECOMPOSITION_SYNTAX_MISSING)) != 0u;
        missing += is_missing;
        empty += (witness.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_EMPTY) != 0u;
        role_count += witness.field_kind != 0u;
        depth = std::max(depth, witness.depth);
        if (is_missing && (witness.byte_start != witness.byte_end ||
                witness.canonical_content.multiplicity != 0u)) return 14;
        if (witness.byte_end > source.size()) return 15;
    }
    if ((errors != 0u) != expect_error || (missing != 0u) != expect_missing || (empty != 0u) != expect_empty ||
        view.profile.error_count != errors || role_count == 0u) return 16;
    std::vector<std::uint8_t> readback(source.size());
    std::size_t readback_bytes = 0u;
    if (laplace_tabular_source_recompose_artifact(plan, 0u, readback.data(),
            readback.size(), &readback_bytes) != LAPLACE_TABULAR_SOURCE_OK ||
        readback_bytes != source.size() || std::memcmp(readback.data(), source.data(), source.size()) != 0) return 17;
    std::printf("{\"phase\":\"native-source-plan\",\"bytes\":%zu,\"witnesses\":%zu,"
        "\"syntax_errors_or_missing\":%zu,\"missing\":%zu,\"field_roles\":%zu,"
        "\"depth\":%u,\"semantic_testimony\":0,\"postgresql_admission\":false}\n",
        source.size(), count, errors, missing, role_count, depth);
    laplace_tabular_source_plan_destroy(&plan);
    return 0;
}

int CheckCorpus(int argc, char** argv, const laplace_decomposition_provider_v1* provider) {
    const std::size_t count = static_cast<std::size_t>(argc - 3);
    if (count == 0u) return 30;
    std::vector<std::string> storage(count), names(count), media(count);
    std::vector<laplace_tabular_artifact> artifacts(count);
    std::size_t bytes = 0u;
    for (std::size_t index = 0u; index < count; ++index) {
        names[index] = argv[index + 3u];
        const auto path = std::filesystem::path(argv[2]) / names[index];
        std::ifstream stream(path, std::ios::binary);
        storage[index].assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        if (storage[index].empty()) return 31;
        const auto suffix = path.extension().string();
        media[index] = (suffix == ".cpp" || suffix == ".h" || suffix == ".hpp" ||
            suffix == ".cc" || suffix == ".cxx" || suffix == ".hxx") ? "text/x-c++" : "text/plain";
        auto& artifact = artifacts[index];
        artifact.artifact_id = Digest(storage[index]);
        std::memcpy(artifact.expected_sha256, artifact.artifact_id.bytes, 32u);
        artifact.bytes = reinterpret_cast<const std::uint8_t*>(storage[index].data());
        artifact.byte_count = storage[index].size();
        artifact.name = names[index].data();
        artifact.name_byte_count = names[index].size();
        artifact.media_type = media[index].data();
        artifact.media_type_byte_count = media[index].size();
        artifact.mode = LAPLACE_TABULAR_ARTIFACT_RAW;
        artifact.flags = LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;
        bytes += storage[index].size();
    }
    laplace_tabular_source_input input{};
    input.profile_declaration = Declaration();
    input.artifacts = artifacts.data();
    input.artifact_count = count;
    input.geometry_epoch = Digest("fixture geometry");
    input.occurrence_context_fingerprint = Digest("whole corpus occurrence context");
    input.preferred_batch_bytes = 1048576u;
    const auto graph_status = laplace_tabular_source_graph_identify(artifacts.data(), count,
        nullptr, 0u, nullptr, 0u, &input.profile_declaration.artifact_graph_fingerprint);
    if (graph_status != LAPLACE_TABULAR_SOURCE_OK) {
        std::fprintf(stderr, "whole corpus graph status=%u\n", static_cast<unsigned>(graph_status));
        return 32;
    }
    laplace_tabular_source_plan* plan = nullptr;
    const auto status = laplace_source_decomposition_plan_create_bounded(&input, provider, 1u, 256u, &plan);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        std::fprintf(stderr, "whole corpus plan status=%u\n", static_cast<unsigned>(status));
        return 33;
    }
    laplace_tabular_source_plan_view view{};
    if (laplace_tabular_source_plan_view_get(plan, &view) != LAPLACE_TABULAR_SOURCE_OK ||
        view.artifact_count != count || view.profile.claim_count != 0u || view.claim_count != 0u) return 34;
    for (std::size_t index = 0u; index < count; ++index) {
        std::vector<std::uint8_t> output(storage[index].size());
        std::size_t output_bytes = 0u;
        if (laplace_tabular_source_recompose_artifact(plan, index, output.data(), output.size(),
                &output_bytes) != LAPLACE_TABULAR_SOURCE_OK || output_bytes != storage[index].size() ||
            std::memcmp(output.data(), storage[index].data(), output.size()) != 0) return 35;
    }
    std::printf("{\"phase\":\"whole-native-source-plan\",\"files\":%zu,\"bytes\":%zu,"
        "\"requests\":%llu,\"witnesses\":%llu,\"syntax_errors_or_missing\":%llu,"
        "\"semantic_testimony\":0,\"all_files_reconstructed\":true,\"postgresql_admission\":false}\n",
        count, bytes, static_cast<unsigned long long>(view.request_count),
        static_cast<unsigned long long>(view.decomposition_witness_count),
        static_cast<unsigned long long>(view.profile.error_count));
    laplace_tabular_source_plan_destroy(&plan);
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc < 2) return 64;
    std::ifstream stream(argv[1], std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (bytes.empty()) return 65;
    auto sha = Digest(bytes);
    const auto declaration = Declaration().syntax_authority_fingerprint;
    laplace_tree_sitter_grammar* grammar = nullptr;
    const auto open = [&](const char* symbol, const laplace_digest256& expected) {
        return laplace_tree_sitter_grammar_open_verified(argv[1], symbol, "text/x-c++", 10u,
            UINT64_C(0x4350500000000000), &declaration, expected.bytes, bytes.size(), &grammar);
    };
    auto bad = sha;
    bad.bytes[0] ^= 1u;
    if (open("tree_sitter_cpp", bad) != LAPLACE_TREE_SITTER_GRAMMAR_IDENTITY_MISMATCH || grammar != nullptr) return 20;
    if (open("tree_sitter_absent", sha) != LAPLACE_TREE_SITTER_GRAMMAR_SYMBOL_MISSING || grammar != nullptr) return 21;
    if (open("tree_sitter_cpp", sha) != LAPLACE_TREE_SITTER_GRAMMAR_OK || grammar == nullptr) return 22;
    const auto* provider = laplace_tree_sitter_grammar_provider(grammar);
    const std::string clean = "// café: exact UTF-8\n#define PLUS(x) ((x)+1)\nnamespace n { template<class T> T f(T x) { if (x) { while(x) { x = PLUS(x-2); } } return x; } }\n";
    int result = Check(clean, provider, false, false);
    if (result == 0) result = Check("int f() { return 1 }\n", provider, true, true);
    if (result == 0) result = Check("int f( { @ return 1; }\n", provider, true, false);
    if (result == 0) result = Check("int f() { if (1) }\n", provider, true, true, true);
    if (result == 0 && argc > 2) result = CheckCorpus(argc, argv, provider);
    laplace_tree_sitter_grammar_close(&grammar);
    return result;
}
