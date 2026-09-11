#include "laplace/decomposition_xml_ast.h"

#include "blake3.h"
#include "laplace/identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view AuthorityLabel{"Laplace"};
constexpr std::string_view GrammarReleaseLabel{"generic XML structural grammar v1"};
constexpr std::string_view GrammarNamespaceLabel{"urn:laplace:grammar:xml"};
constexpr std::string_view GrammarLocalLabel{"xml-structure"};
constexpr std::string_view RecipeReleaseLabel{"generic XML universal AST recipe v1"};
constexpr std::string_view RecipeNamespaceLabel{"urn:laplace:recipe:xml"};
constexpr std::string_view RecipeLocalLabel{"lossless-xml-ast"};
constexpr std::string_view RoleNamespaceLabel{"urn:laplace:ast-role:xml"};

laplace_digest256 Digest(const std::string_view domain) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain.data(), domain.size());
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool ContentId(const std::string_view text, laplace_id128* const output) {
    if (output == nullptr || text.empty()) return false;
    std::vector<laplace_id128> atoms;
    try {
        atoms.resize(text.size());
    } catch (...) {
        return false;
    }
    for (std::size_t index = 0u; index < text.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte >= 0x80u ||
            laplace_identity_codepoint(
                static_cast<std::uint32_t>(byte), &atoms[index]) !=
                LAPLACE_IDENTITY_OK) {
            return false;
        }
    }
    return laplace_identity_composite(atoms.data(), atoms.size(), output) ==
        LAPLACE_IDENTITY_OK;
}

bool Key(
    const std::uint32_t kind,
    const std::string_view release,
    const std::string_view name_space,
    const std::string_view local,
    laplace_highway_key* const output) {
    if (output == nullptr) return false;
    *output = laplace_highway_key{};
    output->kind = kind;
    output->version = 1u;
    return ContentId(AuthorityLabel, &output->authority) &&
        ContentId(release, &output->release) &&
        ContentId(name_space, &output->name_space) &&
        ContentId(local, &output->local_identifier);
}

struct RuleDeclaration final {
    std::uint64_t kind;
    std::uint64_t field;
    const char* role;
};

constexpr std::array<RuleDeclaration, 16> Rules{{
    {1u, 0u, "document"},
    {2u, 0u, "element"},
    {3u, 0u, "start-tag"},
    {4u, 0u, "end-tag"},
    {5u, LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE, "attribute"},
    {6u, LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_NAME, "attribute-name"},
    {7u, LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_VALUE, "attribute-value"},
    {8u, LAPLACE_DECOMPOSITION_XML_FIELD_CONTENT, "text"},
    {9u, 0u, "comment"},
    {10u, LAPLACE_DECOMPOSITION_XML_FIELD_CONTENT, "cdata"},
    {11u, 0u, "processing-instruction"},
    {12u, 0u, "doctype"},
    {13u, LAPLACE_DECOMPOSITION_XML_FIELD_ELEMENT_NAME, "element-name"},
    {13u, LAPLACE_DECOMPOSITION_XML_FIELD_END_NAME, "end-element-name"},
    {14u, LAPLACE_DECOMPOSITION_XML_FIELD_CONTENT, "cdata-text"},
    {15u, 0u, "byte-order-mark"}
}};

}  // namespace

extern "C" laplace_universal_ast_status laplace_decomposition_xml_ast_recipe_create(
    const laplace_decomposition_xml_provider* const xml_provider,
    laplace_grammar_registry** const registry,
    laplace_ast_recipe** const recipe) {
    if (xml_provider == nullptr || registry == nullptr || recipe == nullptr ||
        *registry != nullptr || *recipe != nullptr ||
        xml_provider->provider.state != xml_provider ||
        xml_provider->provider.abi_major != LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR ||
        xml_provider->provider.abi_minor > LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR ||
        xml_provider->kind_base == 0u ||
        (xml_provider->kind_base & UINT64_C(0xffff)) != 0u) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }

    laplace_grammar_provider_declaration grammar{};
    if (!Key(
            LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL,
            GrammarReleaseLabel, GrammarNamespaceLabel, GrammarLocalLabel,
            &grammar.grammar_coordinate)) {
        return LAPLACE_UNIVERSAL_AST_GRAMMAR_INVALID;
    }
    grammar.provider_fingerprint = xml_provider->provider.provider_fingerprint;
    grammar.release_fingerprint = Digest("laplace-generic-xml-grammar-release-v1");
    grammar.conformance_fingerprint = Digest("laplace-generic-xml-grammar-conformance-v1");
    grammar.kind_base = xml_provider->kind_base;
    grammar.kind_count = LAPLACE_DECOMPOSITION_XML_KIND_COUNT;

    laplace_grammar_registry* created_registry = nullptr;
    auto status = laplace_grammar_registry_create(&grammar, 1u, &created_registry);
    if (status != LAPLACE_UNIVERSAL_AST_OK) return status;

    laplace_digest256 registry_fingerprint{};
    if (laplace_grammar_registry_fingerprint(
            created_registry, &registry_fingerprint) != LAPLACE_UNIVERSAL_AST_OK) {
        laplace_grammar_registry_destroy(&created_registry);
        return LAPLACE_UNIVERSAL_AST_GRAMMAR_INVALID;
    }

    std::array<laplace_ast_recipe_rule, Rules.size()> rules{};
    for (std::size_t index = 0u; index < Rules.size(); ++index) {
        const auto& declaration = Rules[index];
        auto& rule = rules[index];
        rule.provider_fingerprint = xml_provider->provider.provider_fingerprint;
        rule.kind = xml_provider->kind_base | declaration.kind;
        rule.grammar_kind = rule.kind;
        rule.field_kind = declaration.field;
        rule.disposition = LAPLACE_AST_RULE_PRESERVE;
        if (!Key(
                LAPLACE_HIGHWAY_KIND_AST_ROLE,
                RecipeReleaseLabel, RoleNamespaceLabel, declaration.role,
                &rule.role_coordinate)) {
            laplace_grammar_registry_destroy(&created_registry);
            return LAPLACE_UNIVERSAL_AST_RECIPE_INVALID;
        }
    }

    laplace_ast_recipe_declaration recipe_declaration{};
    if (!Key(
            LAPLACE_HIGHWAY_KIND_RECIPE,
            RecipeReleaseLabel, RecipeNamespaceLabel, RecipeLocalLabel,
            &recipe_declaration.recipe_coordinate)) {
        laplace_grammar_registry_destroy(&created_registry);
        return LAPLACE_UNIVERSAL_AST_RECIPE_INVALID;
    }
    recipe_declaration.grammar_registry_fingerprint = registry_fingerprint;
    recipe_declaration.conformance_fingerprint =
        Digest("laplace-generic-xml-universal-ast-conformance-v1");

    laplace_ast_recipe* created_recipe = nullptr;
    status = laplace_ast_recipe_create(
        created_registry, &recipe_declaration,
        rules.data(), rules.size(), &created_recipe);
    if (status != LAPLACE_UNIVERSAL_AST_OK) {
        laplace_grammar_registry_destroy(&created_registry);
        return status;
    }

    *registry = created_registry;
    *recipe = created_recipe;
    return LAPLACE_UNIVERSAL_AST_OK;
}
