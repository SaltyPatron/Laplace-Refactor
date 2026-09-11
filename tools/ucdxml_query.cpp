#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

#include "blake3.h"
#include "laplace/decomposition.h"
#include "laplace/decomposition_xml.h"
#include "laplace/decomposition_xml_ast.h"
#include "laplace/evidence_testimony.h"
#include "laplace/source_profile.h"
#include "laplace/text_identity.h"
#include "laplace/ucdxml_evidence.h"
#include "laplace/ucdxml_projection.h"
#include "laplace/universal_ast.h"

namespace {

constexpr std::string_view XmlProviderDomain{"laplace-decomposition-xml-v1"};
constexpr std::string_view SourceDomain{"laplace-ucdxml-exact-source-v1"};
constexpr std::string_view ProjectionRecipeDomain{"laplace-uax42-ucdxml-projection-v1"};
constexpr std::string_view GeometryDomain{"laplace-ucdxml-bootstrap-geometry-context-v1"};
constexpr std::string_view OccurrenceDomain{"laplace-ucdxml-source-occurrence-context-v1"};
constexpr std::string_view EvidenceRecipeDomain{"laplace-ucdxml-evidence-recipe-v1"};
constexpr std::string_view EvidenceContextDomain{"laplace-ucdxml-evidence-context-v1"};
constexpr std::string_view StandardTrustDomain{"laplace-standard-source-prior-v1"};
constexpr std::uint64_t XmlKindBase = UINT64_C(0x584d4c0000000000);

struct DecompositionOwner final {
    laplace_decomposition_result* value{};
    ~DecompositionOwner() { laplace_decomposition_result_destroy(&value); }
};

struct RegistryOwner final {
    laplace_grammar_registry* value{};
    ~RegistryOwner() { laplace_grammar_registry_destroy(&value); }
};

struct RecipeOwner final {
    laplace_ast_recipe* value{};
    ~RecipeOwner() { laplace_ast_recipe_destroy(&value); }
};

struct AstOwner final {
    laplace_universal_ast_plan* value{};
    ~AstOwner() { laplace_universal_ast_plan_destroy(&value); }
};

struct ProjectionOwner final {
    laplace_ucdxml_projection* value{};
    ~ProjectionOwner() { laplace_ucdxml_projection_destroy(&value); }
};

void HashU32(blake3_hasher& hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

laplace_digest256 Fingerprint(
    const std::string_view domain,
    const std::uint8_t* const bytes,
    const std::size_t count) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashU64(hasher, static_cast<std::uint64_t>(domain.size()));
    blake3_hasher_update(&hasher, domain.data(), domain.size());
    HashU64(hasher, static_cast<std::uint64_t>(count));
    if (count != 0u) blake3_hasher_update(&hasher, bytes, count);
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

laplace_digest256 Fingerprint(const std::string_view domain) {
    return Fingerprint(domain, nullptr, 0u);
}

laplace_digest256 Fingerprint(
    const std::string_view domain,
    const laplace_digest256& digest) {
    return Fingerprint(domain, digest.bytes, sizeof(digest.bytes));
}

laplace_digest256 FingerprintPair(
    const std::string_view domain,
    const laplace_digest256& first,
    const laplace_digest256& second) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashU64(hasher, static_cast<std::uint64_t>(domain.size()));
    blake3_hasher_update(&hasher, domain.data(), domain.size());
    blake3_hasher_update(&hasher, first.bytes, sizeof(first.bytes));
    blake3_hasher_update(&hasher, second.bytes, sizeof(second.bytes));
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool ReadFile(const char* const path, std::vector<std::uint8_t>* const output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0 ||
        static_cast<std::uint64_t>(size) >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        size > static_cast<std::streamoff>(
            std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    try {
        output->resize(static_cast<std::size_t>(size));
    } catch (...) {
        return false;
    }
    input.read(
        reinterpret_cast<char*>(output->data()),
        static_cast<std::streamsize>(size));
    return input.good() || input.eof();
}

bool ParsePosition(const char* const text, std::uint32_t* const position) {
    if (text == nullptr || position == nullptr || *text == '\0') return false;
    std::string_view value(text);
    if (value.starts_with("U+") || value.starts_with("u+")) value.remove_prefix(2u);
    if (value.empty() || value.size() > 6u) return false;
    std::uint32_t result = 0u;
    for (const char byte : value) {
        std::uint32_t digit = UINT32_MAX;
        if (byte >= '0' && byte <= '9') {
            digit = static_cast<std::uint32_t>(byte - '0');
        } else if (byte >= 'A' && byte <= 'F') {
            digit = static_cast<std::uint32_t>(byte - 'A') + 10u;
        } else if (byte >= 'a' && byte <= 'f') {
            digit = static_cast<std::uint32_t>(byte - 'a') + 10u;
        }
        if (digit == UINT32_MAX ||
            result > (UINT32_C(0x10ffff) - digit) / 16u) {
            return false;
        }
        result = result * 16u + digit;
    }
    if (result >= UINT32_C(0x110000)) return false;
    *position = result;
    return true;
}

bool TextId(const std::string_view text, laplace_id128* const identity) {
    if (identity == nullptr || text.empty()) return false;
    laplace_text_identity value{};
    if (laplace_text_identity_utf8(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), &value) !=
        LAPLACE_TEXT_IDENTITY_OK) {
        return false;
    }
    *identity = value.entity_id;
    return true;
}

void PrintJsonBytes(const std::uint8_t* const bytes, const std::size_t count) {
    std::putchar('"');
    for (std::size_t index = 0u; index < count; ++index) {
        const std::uint8_t byte = bytes[index];
        switch (byte) {
            case '"': std::fputs("\\\"", stdout); break;
            case '\\': std::fputs("\\\\", stdout); break;
            case '\b': std::fputs("\\b", stdout); break;
            case '\f': std::fputs("\\f", stdout); break;
            case '\n': std::fputs("\\n", stdout); break;
            case '\r': std::fputs("\\r", stdout); break;
            case '\t': std::fputs("\\t", stdout); break;
            default:
                if (byte < 0x20u) {
                    std::printf("\\u%04x", static_cast<unsigned int>(byte));
                } else {
                    std::putchar(static_cast<int>(byte));
                }
                break;
        }
    }
    std::putchar('"');
}

void PrintDigest(const laplace_digest256& digest) {
    static constexpr char Hex[] = "0123456789abcdef";
    std::putchar('"');
    for (const std::uint8_t byte : digest.bytes) {
        std::putchar(Hex[byte >> 4u]);
        std::putchar(Hex[byte & 0x0fu]);
    }
    std::putchar('"');
}

void PrintId(const laplace_id128& identity) {
    static constexpr char Hex[] = "0123456789abcdef";
    std::putchar('"');
    for (const std::uint8_t byte : identity.bytes) {
        std::putchar(Hex[byte >> 4u]);
        std::putchar(Hex[byte & 0x0fu]);
    }
    std::putchar('"');
}

void PrintDigestStderr(const laplace_digest256& digest) {
    static constexpr char Hex[] = "0123456789abcdef";
    for (const std::uint8_t byte : digest.bytes) {
        std::fputc(Hex[byte >> 4u], stderr);
        std::fputc(Hex[byte & 0x0fu], stderr);
    }
}

bool BuildSourceProfile(
    const std::uint64_t source_byte_count,
    const laplace_digest256& source_fingerprint,
    const laplace_digest256& xml_fingerprint,
    const laplace_digest256& ast_recipe_fingerprint,
    const laplace_universal_ast_plan_view& ast,
    const laplace_decomposition_summary& decomposition,
    const laplace_ucdxml_projection_summary& projection,
    const laplace_digest256& evidence_recipe,
    laplace_source_profile_manifest* const profile,
    laplace_source_profile_receipt* const receipt) {
    if (profile == nullptr || receipt == nullptr || source_byte_count == 0u ||
        projection.source_attribute_count == 0u ||
        projection.codepoint_declaration_count == 0u || decomposition.span_count == 0u ||
        ast.binding_count == 0u || ast.composition.request_count == 0u) {
        return false;
    }
    *profile = laplace_source_profile_manifest{};
    *receipt = laplace_source_profile_receipt{};
    profile->coordinate.kind = 17u;
    profile->coordinate.version = 1u;
    if (!TextId("Unicode Consortium", &profile->coordinate.authority) ||
        !TextId("Unicode 17.0.0", &profile->coordinate.release) ||
        !TextId("https://www.unicode.org/Public/17.0.0/ucdxml/",
                &profile->coordinate.name_space) ||
        !TextId("ucd.all.grouped.xml", &profile->coordinate.local_identifier)) {
        return false;
    }
    profile->authority_release_fingerprint = Fingerprint(
        "laplace-unicode-17-ucdxml-authority-release-v1", source_fingerprint);
    profile->license_fingerprint = Fingerprint("unicode-license-3.0");
    profile->artifact_graph_fingerprint = source_fingerprint;
    profile->syntax_authority_fingerprint = xml_fingerprint;
    profile->recipe_program_fingerprint = ast_recipe_fingerprint;
    profile->universal_ast_mapping_fingerprint = ast.witness_fingerprint;
    profile->highway_references_fingerprint = Fingerprint(
        "laplace-ucdxml-property-addressing-v1", projection.projection_fingerprint);
    profile->epistemic_witnessing_fingerprint = evidence_recipe;

    blake3_hasher denominator{};
    blake3_hasher_init(&denominator);
    static constexpr std::string_view DenominatorDomain{
        "laplace-ucdxml-source-denominators-v1"};
    blake3_hasher_update(
        &denominator, DenominatorDomain.data(), DenominatorDomain.size());
    HashU64(denominator, source_byte_count);
    HashU64(denominator, ast.composition.atom_count);
    HashU64(denominator, ast.composition.request_count);
    HashU64(denominator, ast.binding_count);
    HashU64(denominator, decomposition.span_count);
    HashU64(denominator, projection.codepoint_declaration_count);
    HashU64(denominator, projection.source_attribute_count);
    HashU32(denominator, projection.covered_position_count);
    blake3_hasher_finalize(
        &denominator, profile->denominator_declaration_fingerprint.bytes,
        sizeof(profile->denominator_declaration_fingerprint.bytes));
    profile->conformance_fingerprint = FingerprintPair(
        "laplace-uax42-r38-ucdxml-conformance-v1",
        projection.projection_fingerprint, ast.plan_fingerprint);
    profile->completion_law_fingerprint = Fingerprint(
        "laplace-ucdxml-complete-source-boundary-v1",
        projection.projection_fingerprint);
    profile->selected_boundary_fingerprint = source_fingerprint;

    profile->byte_count = source_byte_count;
    profile->container_count = 1u;
    profile->member_count = 0u;
    profile->file_count = 1u;
    profile->record_count = projection.codepoint_declaration_count;
    profile->field_count = projection.source_attribute_count;
    profile->syntax_node_count = decomposition.span_count;
    profile->span_count = ast.binding_count;
    profile->edge_count = ast.binding_count > 0u ? ast.binding_count - 1u : 0u;
    profile->reference_count = 0u;
    profile->occurrence_count = projection.source_attribute_count;
    profile->claim_count = projection.source_attribute_count;
    profile->mapping_count = 0u;
    profile->error_count = 0u;
    profile->unknown_count = 0u;
    profile->transformation_count = 0u;
    profile->output_count = ast.composition.request_count;
    profile->closure_subject_count = projection.source_attribute_count;
    profile->persisted_count = profile->closure_subject_count;
    profile->reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    profile->flags = LAPLACE_SOURCE_PROFILE_MAKE_FLAGS(
        LAPLACE_SOURCE_PROFILE_EPISTEMIC_FOUNDATIONAL_SEED,
        LAPLACE_SOURCE_PROFILE_EVIDENCE_STANDARD);
    if (laplace_source_profile_identify(profile, &profile->profile_id) !=
        LAPLACE_SOURCE_PROFILE_OK) {
        return false;
    }
    laplace_source_profile_error error{};
    return laplace_source_profile_validate_batch(
        profile, 1u, receipt, &error) == LAPLACE_SOURCE_PROFILE_OK;
}

struct EvidenceContext final {
    laplace_digest256 source_fingerprint{};
    laplace_digest256 source_profile_id{};
    laplace_digest256 recipe_receipt_id{};
    laplace_digest256 trust_input_id{};
    laplace_digest256 context_fingerprint{};
};

bool PrintProperty(
    const std::vector<std::uint8_t>& source,
    const laplace_ucdxml_projection* const projection,
    const laplace_ucdxml_property_view& property,
    const EvidenceContext& evidence) {
    if (property.property_name_byte_end < property.property_name_byte_start ||
        property.property_name_byte_end > source.size()) {
        return false;
    }
    std::size_t value_bytes = 0u;
    if (laplace_ucdxml_property_value(
            projection, &property, nullptr, 0u, &value_bytes) != LAPLACE_UCDXML_OK) {
        return false;
    }
    std::vector<std::uint8_t> value;
    try {
        value.resize(value_bytes);
    } catch (...) {
        return false;
    }
    if (laplace_ucdxml_property_value(
            projection, &property, value.data(), value.size(), &value_bytes) !=
        LAPLACE_UCDXML_OK) {
        return false;
    }
    const auto* const name = source.data() +
        static_cast<std::size_t>(property.property_name_byte_start);
    const std::size_t name_bytes = static_cast<std::size_t>(
        property.property_name_byte_end - property.property_name_byte_start);

    laplace_decomposition_content evidence_source{};
    evidence_source.bytes = source.data();
    evidence_source.byte_count = static_cast<std::uint64_t>(source.size());
    laplace_ucdxml_evidence_input evidence_input{};
    evidence_input.projection = projection;
    evidence_input.source_content = &evidence_source;
    evidence_input.property = &property;
    evidence_input.source_fingerprint = evidence.source_fingerprint;
    evidence_input.source_profile_id = evidence.source_profile_id;
    evidence_input.evidence_recipe_receipt_id = evidence.recipe_receipt_id;
    evidence_input.trust_input_id = evidence.trust_input_id;
    evidence_input.context_fingerprint = evidence.context_fingerprint;
    if (property.declaration_element_span_index == UINT64_MAX) return false;
    evidence_input.source_ordinal = property.declaration_element_span_index + 1u;
    evidence_input.lineage_memory_limit_bytes = UINT64_C(1048576);
    evidence_input.source_type = LAPLACE_EVIDENCE_SOURCE_STANDARD;
    laplace_ucdxml_evidence_result evidence_result{};
    if (laplace_ucdxml_evidence_emit(&evidence_input, &evidence_result) !=
        LAPLACE_UCDXML_EVIDENCE_OK) {
        return false;
    }

    std::printf(
        "{\"codepoint\":\"U+%04X\",\"property\":",
        static_cast<unsigned int>(property.codepoint_position));
    PrintJsonBytes(name, name_bytes);
    std::fputs(",\"value\":", stdout);
    PrintJsonBytes(value.data(), value.size());
    std::printf(
        ",\"range_first\":\"U+%04X\",\"range_last\":\"U+%04X\","
        "\"inherited\":%s,\"range_declaration\":%s,\"hash_substitution\":%s,"
        "\"observation_fingerprint\":",
        static_cast<unsigned int>(property.range_first),
        static_cast<unsigned int>(property.range_last),
        (property.flags & LAPLACE_UCDXML_PROPERTY_INHERITED_FROM_GROUP) != 0u
            ? "true" : "false",
        (property.flags & LAPLACE_UCDXML_PROPERTY_RANGE_DECLARATION) != 0u
            ? "true" : "false",
        (property.flags & LAPLACE_UCDXML_PROPERTY_HASH_SUBSTITUTION) != 0u
            ? "true" : "false");
    PrintDigest(property.observation_fingerprint);
    std::fputs(",\"codepoint_entity_id\":", stdout);
    PrintId(evidence_result.codepoint_entity_id);
    std::fputs(",\"property_name_entity_id\":", stdout);
    PrintId(evidence_result.property_name_identity.entity_id);
    std::fputs(",\"property_value_entity_id\":", stdout);
    PrintId(evidence_result.property_value_identity.entity_id);
    std::fputs(",\"proposition_entity_id\":", stdout);
    PrintId(evidence_result.proposition_entity_id);
    std::fputs(",\"source_profile_id\":", stdout);
    PrintDigest(evidence.source_profile_id);
    std::fputs(",\"occurrence_id\":", stdout);
    PrintDigest(evidence_result.occurrence.attestation_id);
    std::fputs(",\"evidence_node_id\":", stdout);
    PrintDigest(evidence_result.lineage.node_id);
    std::fputs(",\"evidence_root_id\":", stdout);
    PrintDigest(evidence_result.root.root_node_id);
    std::fputs(",\"testimony_id\":", stdout);
    PrintDigest(evidence_result.testimony.testimony_id);
    std::fputs(",\"lineage_receipt_id\":", stdout);
    PrintDigest(evidence_result.lineage_receipt.receipt_id);
    std::fputs(",\"testimony_receipt_id\":", stdout);
    PrintDigest(evidence_result.testimony_receipt.receipt_id);
    std::printf(
        ",\"evidence_source_type\":%u,\"uncertainty\":\"0/1\"}\n",
        static_cast<unsigned int>(evidence_result.testimony.source_type));
    return true;
}

void Usage(const char* const executable) {
    std::fprintf(
        stderr,
        "usage: %s <ucd.all.grouped.xml> <U+codepoint|hex> [property-name ...]\n",
        executable);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        Usage(argc > 0 ? argv[0] : "laplace_ucdxml_query");
        return 2;
    }
    std::vector<std::uint8_t> source;
    if (!ReadFile(argv[1], &source)) {
        std::fprintf(stderr, "cannot read UCDXML source: %s\n", argv[1]);
        return 1;
    }
    std::uint32_t position = 0u;
    if (!ParsePosition(argv[2], &position)) {
        std::fprintf(stderr, "invalid Unicode codepoint position: %s\n", argv[2]);
        return 2;
    }

    const laplace_digest256 source_fingerprint = Fingerprint(
        SourceDomain, source.data(), source.size());
    const laplace_digest256 xml_fingerprint = Fingerprint(XmlProviderDomain);
    laplace_decomposition_xml_provider xml{};
    if (laplace_decomposition_xml_provider_init(
            &xml, XmlKindBase, &xml_fingerprint) != LAPLACE_DECOMPOSITION_OK) {
        std::fputs("cannot initialize generic XML decomposition provider\n", stderr);
        return 1;
    }

    static constexpr char MediaType[] = "application/xml";
    laplace_decomposition_input decomposition_input{};
    decomposition_input.content.bytes = source.data();
    decomposition_input.content.byte_count = static_cast<std::uint64_t>(source.size());
    decomposition_input.content.media_type = MediaType;
    decomposition_input.content.media_type_byte_count = sizeof(MediaType) - 1u;
    decomposition_input.content.name = argv[1];
    decomposition_input.content.name_byte_count = std::strlen(argv[1]);
    decomposition_input.providers = &xml.provider;
    decomposition_input.provider_count = 1u;
    if (source.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint64_t>::max() - UINT64_C(4096))) {
        std::fputs("UCDXML source is too large for decomposition bounds\n", stderr);
        return 1;
    }
    decomposition_input.maximum_spans = std::max<std::uint64_t>(
        UINT64_C(4096),
        static_cast<std::uint64_t>(source.size()) + UINT64_C(4096));
    decomposition_input.maximum_depth = 32u;

    DecompositionOwner decomposition;
    const auto decomposition_status = laplace_decomposition_run(
        &decomposition_input, &decomposition.value);
    if (decomposition_status != LAPLACE_DECOMPOSITION_OK ||
        decomposition.value == nullptr) {
        std::fprintf(
            stderr, "generic XML decomposition failed: %u\n",
            static_cast<unsigned int>(decomposition_status));
        return 1;
    }
    laplace_decomposition_summary decomposition_summary{};
    if (laplace_decomposition_summary_get(
            decomposition.value, &decomposition_summary) != LAPLACE_DECOMPOSITION_OK) {
        std::fputs("generic XML decomposition summary failed\n", stderr);
        return 1;
    }

    RegistryOwner registry;
    RecipeOwner recipe;
    const auto recipe_status = laplace_decomposition_xml_ast_recipe_create(
        &xml, &registry.value, &recipe.value);
    if (recipe_status != LAPLACE_UNIVERSAL_AST_OK || registry.value == nullptr ||
        recipe.value == nullptr) {
        std::fprintf(
            stderr, "generic XML AST recipe creation failed: %u\n",
            static_cast<unsigned int>(recipe_status));
        return 1;
    }
    laplace_digest256 ast_recipe_fingerprint{};
    if (laplace_ast_recipe_fingerprint(
            recipe.value, &ast_recipe_fingerprint) != LAPLACE_UNIVERSAL_AST_OK) {
        std::fputs("generic XML AST recipe identity failed\n", stderr);
        return 1;
    }

    laplace_universal_ast_compile_input ast_input{};
    ast_input.grammar_registry = registry.value;
    ast_input.recipe = recipe.value;
    ast_input.content = &decomposition_input.content;
    ast_input.decomposition = decomposition.value;
    ast_input.geometry_epoch = Fingerprint(GeometryDomain);
    ast_input.occurrence_context_fingerprint = Fingerprint(
        OccurrenceDomain, source_fingerprint);
    ast_input.source_ordinal_base = 1u;
    AstOwner ast;
    const auto ast_status = laplace_universal_ast_plan_create(
        &ast_input, &ast.value);
    if (ast_status != LAPLACE_UNIVERSAL_AST_OK || ast.value == nullptr) {
        std::fprintf(
            stderr, "universal AST compilation failed: %u\n",
            static_cast<unsigned int>(ast_status));
        return 1;
    }
    laplace_universal_ast_plan_view ast_view{};
    if (laplace_universal_ast_plan_view_get(ast.value, &ast_view) !=
        LAPLACE_UNIVERSAL_AST_OK) {
        std::fputs("universal AST plan view failed\n", stderr);
        return 1;
    }

    laplace_ucdxml_projection_input projection_input{};
    projection_input.content = &decomposition_input.content;
    projection_input.decomposition = decomposition.value;
    projection_input.xml_provider = &xml;
    projection_input.source_fingerprint = source_fingerprint;
    projection_input.recipe_fingerprint = Fingerprint(
        ProjectionRecipeDomain, ast_recipe_fingerprint);

    ProjectionOwner projection;
    laplace_ucdxml_projection_summary summary{};
    const auto projection_status = laplace_ucdxml_projection_create(
        &projection_input, &projection.value, &summary);
    if (projection_status != LAPLACE_UCDXML_OK || projection.value == nullptr) {
        std::fprintf(
            stderr, "UAX #42 projection failed: %u\n",
            static_cast<unsigned int>(projection_status));
        return 1;
    }

    const laplace_digest256 evidence_recipe = FingerprintPair(
        EvidenceRecipeDomain,
        projection_input.recipe_fingerprint, ast_recipe_fingerprint);
    laplace_source_profile_manifest source_profile{};
    laplace_source_profile_receipt source_profile_receipt{};
    if (!BuildSourceProfile(
            static_cast<std::uint64_t>(source.size()),
            source_fingerprint, xml_fingerprint, ast_recipe_fingerprint,
            ast_view, decomposition_summary, summary, evidence_recipe,
            &source_profile, &source_profile_receipt)) {
        std::fputs("UCDXML source profile validation failed\n", stderr);
        return 1;
    }
    EvidenceContext evidence{};
    evidence.source_fingerprint = source_fingerprint;
    evidence.source_profile_id = source_profile.profile_id;
    evidence.recipe_receipt_id = evidence_recipe;
    evidence.trust_input_id = Fingerprint(
        StandardTrustDomain, source_profile.profile_id);
    evidence.context_fingerprint = FingerprintPair(
        EvidenceContextDomain,
        projection_input.recipe_fingerprint, source_profile.profile_id);

    if (argc == 3) {
        std::size_t property_count = 0u;
        if (laplace_ucdxml_property_count(
                projection.value, position, &property_count) != LAPLACE_UCDXML_OK) {
            std::fprintf(
                stderr, "no UCDXML declaration for U+%04X\n",
                static_cast<unsigned int>(position));
            return 1;
        }
        for (std::size_t index = 0u; index < property_count; ++index) {
            laplace_ucdxml_property_view property{};
            if (laplace_ucdxml_property(
                    projection.value, position, index, &property) !=
                    LAPLACE_UCDXML_OK ||
                !PrintProperty(source, projection.value, property, evidence)) {
                std::fprintf(stderr, "cannot materialize/evidence property %zu\n", index);
                return 1;
            }
        }
    } else {
        for (int index = 3; index < argc; ++index) {
            laplace_ucdxml_property_view property{};
            const auto status = laplace_ucdxml_property_find(
                projection.value, position,
                reinterpret_cast<const std::uint8_t*>(argv[index]),
                std::strlen(argv[index]), &property);
            if (status == LAPLACE_UCDXML_PROPERTY_NOT_FOUND) {
                std::printf(
                    "{\"codepoint\":\"U+%04X\",\"property\":",
                    static_cast<unsigned int>(position));
                PrintJsonBytes(
                    reinterpret_cast<const std::uint8_t*>(argv[index]),
                    std::strlen(argv[index]));
                std::fputs(",\"present\":false}\n", stdout);
                continue;
            }
            if (status != LAPLACE_UCDXML_OK ||
                !PrintProperty(source, projection.value, property, evidence)) {
                std::fprintf(stderr, "cannot query/evidence UCDXML property: %s\n", argv[index]);
                return 1;
            }
        }
    }

    std::fputs("source_profile=", stderr);
    PrintDigestStderr(source_profile.profile_id);
    std::fputs(" source_profile_receipt=", stderr);
    PrintDigestStderr(source_profile_receipt.receipt_id);
    std::fprintf(
        stderr,
        " epistemic_class=%u evidence_source_type=%u ast_bindings=%llu "
        "ast_content_bindings=%llu canonical_atoms=%llu canonical_requests=%llu "
        "declarations=%llu groups=%llu source_attributes=%llu covered_positions=%u ast_plan=",
        static_cast<unsigned int>(laplace_source_profile_epistemic_class(&source_profile)),
        static_cast<unsigned int>(laplace_source_profile_evidence_source_type(&source_profile)),
        static_cast<unsigned long long>(ast_view.binding_count),
        static_cast<unsigned long long>(ast_view.content_binding_count),
        static_cast<unsigned long long>(ast_view.composition.atom_count),
        static_cast<unsigned long long>(ast_view.composition.request_count),
        static_cast<unsigned long long>(summary.codepoint_declaration_count),
        static_cast<unsigned long long>(summary.group_count),
        static_cast<unsigned long long>(summary.source_attribute_count),
        static_cast<unsigned int>(summary.covered_position_count));
    PrintDigestStderr(ast_view.plan_fingerprint);
    std::fputs(" ast_witness=", stderr);
    PrintDigestStderr(ast_view.witness_fingerprint);
    std::fputs(" projection=", stderr);
    PrintDigestStderr(summary.projection_fingerprint);
    std::fputc('\n', stderr);
    return 0;
}
