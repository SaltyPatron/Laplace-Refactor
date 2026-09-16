// Qualifies the selected PGN syntax provider through the common native source
// owner. No chess rules, canonical chess object model, or database store lives here.
#include "laplace/source_decomposition.h"
#include "laplace/decomposition_composition.h"
#include "laplace/tree_sitter_grammar.h"
#include "sha256_internal.hpp"

#include <tree_sitter/api.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr char Media[] = "application/x-chess-pgn";
constexpr std::uint64_t KindBase = UINT64_C(0x50474e0000000000);

void Require(const bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}
laplace_digest256 Digest(const std::string& text) {
    laplace_digest256 value{};
    const auto result = laplace::internal::Sha256(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    std::memcpy(value.bytes, result.data(), result.size());
    return value;
}
std::string Hex(const std::uint8_t* bytes, const std::size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(count * 2u, '0');
    for (std::size_t index = 0u; index < count; ++index) {
        output[index * 2u] = digits[bytes[index] >> 4u];
        output[index * 2u + 1u] = digits[bytes[index] & 15u];
    }
    return output;
}
laplace_digest256 ParseDigest(const char* text) {
    Require(text != nullptr && std::strlen(text) == 64u, "expected a 64-digit digest");
    laplace_digest256 result{};
    const auto digit = [](const char ch) -> unsigned {
        if (ch >= '0' && ch <= '9') return static_cast<unsigned>(ch - '0');
        if (ch >= 'a' && ch <= 'f') return static_cast<unsigned>(ch - 'a') + 10u;
        throw std::runtime_error("digest contains a non-hexadecimal character");
    };
    for (std::size_t index = 0u; index < 32u; ++index)
        result.bytes[index] = static_cast<std::uint8_t>(
            digit(text[index * 2u]) * 16u + digit(text[index * 2u + 1u]));
    return result;
}
struct Artifact {
    std::string path;
    std::string content;
};
struct Plan {
    laplace_tabular_source_plan* pointer{};
    ~Plan() { laplace_tabular_source_plan_destroy(&pointer); }
};
struct Grammar {
    laplace_tree_sitter_grammar* pointer{};
    ~Grammar() { laplace_tree_sitter_grammar_close(&pointer); }
};
laplace_source_profile_manifest Profile(const laplace_digest256& declaration) {
    laplace_source_profile_manifest value{};
    value.coordinate.kind = LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE;
    value.coordinate.version = 1u;
    const auto coordinate = Digest("PGN syntax qualification fixture authority");
    std::memcpy(value.coordinate.authority.bytes, coordinate.bytes, 16u);
    std::memcpy(value.coordinate.release.bytes, coordinate.bytes + 16u, 16u);
    value.coordinate.name_space = value.coordinate.authority;
    value.coordinate.local_identifier = value.coordinate.release;
    value.authority_release_fingerprint = Digest("PGN syntax fixture release v1");
    value.license_fingerprint = Digest("exact attributed conformance fixture sources");
    value.syntax_authority_fingerprint = declaration;
    value.recipe_program_fingerprint = Digest("shared exact source composition");
    value.universal_ast_mapping_fingerprint = Digest("concrete PGN syntax witnesses, no chess grounding");
    value.highway_references_fingerprint = Digest("no player or chess references resolved");
    value.epistemic_witnessing_fingerprint = Digest("ordinary observation; zero semantic attestations");
    value.denominator_declaration_fingerprint = Digest("every source byte and native syntax witness");
    value.conformance_fingerprint = Digest("pgn_native_source_probe/v1");
    value.completion_law_fingerprint = Digest("exact source reconstruction and typed syntax retention only");
    value.selected_boundary_fingerprint = Digest("declared PGN syntax fixtures");
    value.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    value.flags = LAPLACE_SOURCE_PROFILE_MAKE_FLAGS(
        LAPLACE_SOURCE_PROFILE_EPISTEMIC_OBSERVATION, LAPLACE_SOURCE_PROFILE_EVIDENCE_CORPUS);
    return value;
}

struct Observation {
    std::string root;
    std::string witness;
    std::string source_sha256;
    std::map<std::string, std::uint64_t> named;
    std::uint64_t errors{};
    std::uint64_t missing{};
    std::uint64_t anonymous{};
    std::uint64_t fields{};
    std::uint64_t syntax_witnesses{};
    std::uint64_t mainline_san_moves{};
    std::uint64_t bytes{};
};
bool Equal(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, 32u) == 0;
}

// Deliberate provider defect: change the observed comment node kind while
// retaining its bytes and parent/ordinal topology. This cannot pass conformance.
struct Defect {
    const laplace_decomposition_provider_v1* original{};
    std::uint64_t comment_kind{};
    std::uint64_t replacement_kind{};
    laplace_decomposition_provider_v1 provider{};
};
laplace_decomposition_status DefectApplicable(void* state,
    const laplace_decomposition_content* content, const laplace_decomposition_span* span,
    int* applicable) {
    const auto* defect = static_cast<const Defect*>(state);
    return defect->original->applicable(defect->original->state, content, span, applicable);
}
struct Emission {
    const Defect* defect;
    laplace_decomposition_emit_event_fn emit;
    void* state;
};
int DefectEmit(void* state, const laplace_decomposition_event* event) {
    const auto* emission = static_cast<const Emission*>(state);
    auto changed = *event;
    if (changed.kind == emission->defect->comment_kind)
        changed.kind = emission->defect->replacement_kind;
    return emission->emit(emission->state, &changed);
}
laplace_decomposition_status DefectApply(void* state,
    const laplace_decomposition_content* content, const laplace_decomposition_span* span,
    laplace_decomposition_emit_event_fn emit, void* emit_state) {
    const auto* defect = static_cast<const Defect*>(state);
    Emission emission{defect, emit, emit_state};
    return defect->original->apply_events(defect->original->state, content, span,
                                         DefectEmit, &emission);
}

std::vector<Observation> Observe(const std::vector<Artifact>& files,
    const laplace_decomposition_provider_v1* provider, const TSLanguage* language,
    const laplace_digest256& declaration, const char* occurrence,
    const std::uint32_t maximum_depth) {
    std::vector<laplace_tabular_artifact> artifacts(files.size());
    for (std::size_t index = 0u; index < files.size(); ++index) {
        auto& artifact = artifacts[index];
        artifact.artifact_id = Digest(files[index].content);
        std::memcpy(artifact.expected_sha256, artifact.artifact_id.bytes, 32u);
        artifact.bytes = reinterpret_cast<const std::uint8_t*>(files[index].content.data());
        artifact.byte_count = files[index].content.size();
        artifact.name = files[index].path.data();
        artifact.name_byte_count = files[index].path.size();
        artifact.media_type = Media;
        artifact.media_type_byte_count = sizeof(Media) - 1u;
        artifact.mode = LAPLACE_TABULAR_ARTIFACT_RAW;
        artifact.flags = LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;
    }
    laplace_tabular_source_input input{};
    input.profile_declaration = Profile(declaration);
    input.geometry_epoch = Digest("syntax qualification geometry context, no physicality deposition");
    input.occurrence_context_fingerprint = Digest(occurrence);
    input.artifacts = artifacts.data();
    input.artifact_count = artifacts.size();
    input.preferred_batch_bytes = 65536u;
    Require(laplace_tabular_source_graph_identify(artifacts.data(), artifacts.size(),
        nullptr, 0u, nullptr, 0u, &input.profile_declaration.artifact_graph_fingerprint)
        == LAPLACE_TABULAR_SOURCE_OK, "native source artifact graph failed");
    Plan plan;
    const auto status = laplace_source_decomposition_plan_create_bounded(
        &input, provider, 1u, maximum_depth, &plan.pointer);
    Require(status == LAPLACE_TABULAR_SOURCE_OK && plan.pointer != nullptr,
            "native source decomposition refused");
    laplace_tabular_source_plan_view view{};
    Require(laplace_tabular_source_plan_view_get(plan.pointer, &view)
        == LAPLACE_TABULAR_SOURCE_OK, "native source plan view failed");
    Require(view.artifact_count == files.size() && view.claim_count == 0u &&
        view.profile.claim_count == 0u && view.profile.record_count == 0u &&
        view.profile.field_count == 0u, "syntax qualification invented semantic records or attestations");

    laplace_decomposition_composition_plan_view identity_view{};
    identity_view.atom_positions = view.atom_positions;
    identity_view.operands = view.operands;
    identity_view.requests = view.requests;
    identity_view.atom_count = view.atom_count;
    identity_view.operand_count = view.operand_count;
    identity_view.request_count = view.request_count;
    std::vector<laplace_decomposition_composition_identity> identities(
        static_cast<std::size_t>(view.request_count));
    std::size_t identity_count = 0u;
    Require(laplace_decomposition_composition_identity_evaluate_view(&identity_view,
        identities.data(), identities.size(), &identity_count)
        == LAPLACE_DECOMPOSITION_COMPOSITION_OK && identity_count == identities.size(),
        "canonical native source identity calculation failed");
    std::vector<Observation> observations(files.size());
    for (std::size_t index = 0u; index < files.size(); ++index) {
        std::vector<std::uint8_t> readback(files[index].content.size());
        std::size_t count = 0u;
        Require(laplace_tabular_source_recompose_artifact(plan.pointer, index,
            readback.data(), readback.size(), &count) == LAPLACE_TABULAR_SOURCE_OK &&
            count == files[index].content.size() &&
            std::memcmp(readback.data(), files[index].content.data(), count) == 0,
            "native source reconstruction lost exact bytes");
        auto& observation = observations[index];
        const auto source_digest = Digest(files[index].content);
        observation.source_sha256 = Hex(source_digest.bytes, 32u);
        observation.bytes = count;
    }
    std::map<std::pair<std::uint64_t, std::uint64_t>,
             const laplace_tabular_decomposition_witness*> nodes;
    for (std::uint64_t index = 0u; index < view.decomposition_witness_count; ++index) {
        const auto& node = view.decomposition_witnesses[index];
        Require(nodes.emplace(std::make_pair(node.artifact_index, node.span_index), &node).second,
                "duplicate syntax witness ordinal");
    }
    std::uint64_t errors = 0u;
    for (std::uint64_t index = 0u; index < view.decomposition_witness_count; ++index) {
        const auto& node = view.decomposition_witnesses[index];
        Require(node.artifact_index < files.size(), "syntax witness names a foreign artifact");
        auto& observation = observations[static_cast<std::size_t>(node.artifact_index)];
        Require(node.byte_start <= node.byte_end && node.byte_end <= observation.bytes,
                "syntax witness has invalid source interval");
        if (node.span_index == 0u) {
            Require(node.parent_span_index == UINT64_MAX && node.byte_start == 0u &&
                    node.byte_end == observation.bytes && observation.root.empty(),
                    "source content root does not cover its complete artifact");
            laplace_decomposition_composition_identity identity{};
            Require(laplace_decomposition_composition_identity_resolve_view(
                &identity_view, identities.data(), identities.size(),
                &node.canonical_content, &identity) == LAPLACE_DECOMPOSITION_COMPOSITION_OK,
                "source content root does not resolve through native composition");
            observation.root = Hex(identity.entity_id.bytes, 16u);
            observation.witness = Hex(identity.identity_witness.bytes, 32u);
        }
        const bool missing = (node.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_MISSING) != 0u;
        const bool error = (node.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_ERROR) != 0u;
        errors += error || missing ? 1u : 0u;
        observation.errors += error ? 1u : 0u;
        observation.missing += missing ? 1u : 0u;
        if (missing) Require(node.byte_start == node.byte_end &&
            node.canonical_content.multiplicity == 0u,
            "missing syntax was converted to invented content");
        if (!Equal(node.provider_fingerprint, provider->provider_fingerprint)) continue;
        ++observation.syntax_witnesses;
        observation.fields += node.field_kind != 0u ? 1u : 0u;
        if ((node.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_NAMED) == 0u) {
            ++observation.anonymous;
            continue;
        }
        const auto symbol = static_cast<TSSymbol>(node.kind & UINT64_C(0xffff));
        const char* const name = error ? "ERROR" : ts_language_symbol_name(language, symbol);
        Require(name != nullptr, "selected syntax symbol is absent from its language");
        ++observation.named[name];
        if (node.parent_span_index != UINT64_MAX) {
            const auto parent = nodes.find(std::make_pair(node.artifact_index, node.parent_span_index));
            Require(parent != nodes.end(), "syntax parent witness disappeared");
            const auto& parent_node = *parent->second;
            Require(parent_node.byte_start <= node.byte_start &&
                    parent_node.byte_end >= node.byte_end && parent_node.depth + 1u == node.depth,
                    "syntax parent no longer owns its exact child interval");
            if (std::strcmp(name, "san_move") == 0 &&
                Equal(parent_node.provider_fingerprint, provider->provider_fingerprint)) {
                const auto parent_symbol = static_cast<TSSymbol>(parent_node.kind & UINT64_C(0xffff));
                const char* const parent_name = ts_language_symbol_name(language, parent_symbol);
                if (parent_name != nullptr && std::strcmp(parent_name, "movetext") == 0)
                    ++observation.mainline_san_moves;
            }
        }
    }
    for (const auto& observation : observations)
        Require(!observation.root.empty(), "source content root witness is missing");
    Require(errors == view.profile.error_count, "source profile dropped syntax error dispositions");
    return observations;
}

void Print(const Observation& result, const std::size_t ordinal) {
    std::cout << "{\"artifact_ordinal\":" << ordinal
              << ",\"bytes\":" << result.bytes
              << ",\"source_sha256\":" << std::quoted(result.source_sha256)
              << ",\"canonical_source_root\":" << std::quoted(result.root)
              << ",\"canonical_source_witness\":" << std::quoted(result.witness)
              << ",\"errors\":" << result.errors << ",\"missing\":" << result.missing
              << ",\"anonymous_nodes\":" << result.anonymous
              << ",\"field_nodes\":" << result.fields
              << ",\"syntax_witnesses\":" << result.syntax_witnesses
              << ",\"mainline_san_moves\":" << result.mainline_san_moves
              << ",\"named_node_counts\":{";
    bool first = true;
    for (const auto& item : result.named) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << std::quoted(item.first) << ':' << item.second;
    }
    std::cout << "},\"exact_reconstruction\":true,\"replay_identity_equal\":true,"
                 "\"occurrence_context_identity_equal\":true,\"artifact_name_identity_equal\":true,"
                 "\"semantic_attestations\":0,"
                 "\"chess_legality_verified\":false,\"complete_game_verified\":false,"
                 "\"postgresql_admission\":false}\n";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        Require(argc >= 7 && argc <= 70,
            "usage: probe LIBRARY SHA256 BYTES DECLARATION MODE FILE...");
        const auto expected = ParseDigest(argv[2]);
        const auto declaration = ParseDigest(argv[4]);
        const std::string mode = argv[5];
        Require(mode == "normal" || mode == "comment-kind-defect" || mode == "depth-exhaustion",
                "unknown qualification mode");
        const std::uint64_t expected_bytes = std::stoull(argv[3]);
        Grammar grammar;
        Require(laplace_tree_sitter_grammar_open_verified(argv[1], "tree_sitter_pgn",
            Media, sizeof(Media) - 1u, KindBase, &declaration, expected.bytes,
            expected_bytes, &grammar.pointer) == LAPLACE_TREE_SITTER_GRAMMAR_OK,
            "verified PGN grammar load failed");
        const auto* provider = laplace_tree_sitter_grammar_provider(grammar.pointer);
        Require(provider != nullptr && provider->apply_events != nullptr,
                "PGN syntax event provider is unavailable");
        const auto* storage =
            static_cast<const laplace_decomposition_tree_sitter_provider*>(provider->state);
        const auto* language = storage->language;
        Defect defect{};
        if (mode == "comment-kind-defect") {
            const auto comment = ts_language_symbol_for_name(language, "inline_comment", 14u, true);
            const auto replacement = ts_language_symbol_for_name(language, "san_move", 8u, true);
            Require(comment != 0u && replacement != 0u, "deliberate defect symbols unavailable");
            defect.original = provider;
            defect.comment_kind = KindBase | comment;
            defect.replacement_kind = KindBase | replacement;
            defect.provider = *provider;
            defect.provider.state = &defect;
            defect.provider.applicable = DefectApplicable;
            defect.provider.apply_events = DefectApply;
            provider = &defect.provider;
        }
        std::vector<Artifact> files;
        files.reserve(static_cast<std::size_t>(argc - 6));
        std::uint64_t total = 0u;
        for (int index = 6; index < argc; ++index) {
            const std::filesystem::path path(argv[index]);
            const auto size = std::filesystem::file_size(path);
            Require(size > 0u && size <= 1048576u, "fixture is outside its byte envelope");
            total += size;
            Require(total <= 8388608u, "fixture batch is outside its byte envelope");
            std::ifstream stream(path, std::ios::binary);
            std::string content{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
            Require(content.size() == size, "fixture read is incomplete");
            files.push_back(Artifact{path.filename().string(), std::move(content)});
        }
        const auto first = Observe(files, provider, language, declaration,
            "PGN syntax fixture observation", mode == "depth-exhaustion" ? 1u : 256u);
        const auto replay = Observe(files, provider, language, declaration,
            "PGN syntax fixture observation", 256u);
        const auto another = Observe(files, provider, language, declaration,
            "a distinct observation of the same exact PGN source", 256u);
        auto renamed_files = files;
        for (auto& file : renamed_files) file.path = "renamed-" + file.path;
        const auto renamed = Observe(renamed_files, provider, language, declaration,
            "PGN syntax fixture observation", 256u);
        for (std::size_t index = 0u; index < files.size(); ++index) {
            Require(first[index].root == renamed[index].root &&
                first[index].witness == renamed[index].witness &&
                first[index].named == renamed[index].named &&
                first[index].errors == renamed[index].errors &&
                first[index].missing == renamed[index].missing,
                "artifact filename entered canonical source content identity or syntax");
            Require(first[index].root == replay[index].root &&
                first[index].witness == replay[index].witness &&
                first[index].named == replay[index].named &&
                first[index].errors == replay[index].errors &&
                first[index].missing == replay[index].missing,
                "exact source replay changed canonical identity or syntax");
            Require(first[index].root == another[index].root &&
                first[index].witness == another[index].witness,
                "observation context entered canonical source identity");
            Print(first[index], index);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "pgn-native-source-qualification: " << error.what() << '\n';
        return 42;
    }
}
