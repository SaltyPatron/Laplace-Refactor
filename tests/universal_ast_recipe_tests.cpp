#include "laplace/universal_ast.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint64_t BaseA = UINT64_C(0x4100000000000000);
constexpr std::uint64_t BaseB = UINT64_C(0x4200000000000000);
constexpr std::uint64_t NoBinding = UINT64_MAX;

void Fill(laplace_id128& value, const std::uint8_t seed) {
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_highway_key Key(
    const std::uint32_t kind,
    const std::uint8_t seed,
    const std::uint64_t version = 1u) {
    laplace_highway_key key{};
    key.kind = kind;
    Fill(key.authority, seed);
    Fill(key.release, static_cast<std::uint8_t>(seed + 0x10u));
    Fill(key.name_space, static_cast<std::uint8_t>(seed + 0x20u));
    Fill(key.local_identifier, static_cast<std::uint8_t>(seed + 0x30u));
    key.version = version;
    return key;
}

struct ProviderState {
    std::uint64_t base{};
    std::uint64_t child_field{};
};

laplace_decomposition_status Applicable(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *applicable = span->depth == 0u ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status Apply(
    void* opaque,
    const laplace_decomposition_content*,
    const laplace_decomposition_span*,
    laplace_decomposition_emit_event_fn emit,
    void* emit_state) {
    if (opaque == nullptr || emit == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const auto& state = *static_cast<const ProviderState*>(opaque);
    const std::array<laplace_decomposition_event, 2> events{{
        {0u, 2u, UINT64_MAX,
         state.base | 1u, state.base | 11u,
         0u, 0u,
         LAPLACE_DECOMPOSITION_SPAN_TEXT,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED},
        {0u, 1u, 0u,
         state.base | 2u, state.base | 12u,
         state.child_field, 1u,
         LAPLACE_DECOMPOSITION_SPAN_TEXT,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED}
    }};
    for (const auto& event : events) {
        if (emit(emit_state, &event) != 0) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
    }
    return LAPLACE_DECOMPOSITION_OK;
}

struct Fixture {
    std::array<std::uint8_t, 2> bytes{{'x', 'y'}};
    ProviderState state_a{BaseA, 7u};
    ProviderState state_b{BaseB, 9u};
    std::array<laplace_decomposition_provider_v1, 2> providers{};
    std::array<laplace_grammar_provider_declaration, 2> grammars{};
    laplace_decomposition_result* decomposition{};
    laplace_grammar_registry* registry{};
    laplace_digest256 registry_fingerprint{};

    Fixture() {
        providers[0].state = &state_a;
        providers[0].provider_fingerprint = Digest(0x11u);
        providers[0].applicable = Applicable;
        providers[0].abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
        providers[0].abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
        providers[0].apply_events = Apply;
        providers[1].state = &state_b;
        providers[1].provider_fingerprint = Digest(0x21u);
        providers[1].applicable = Applicable;
        providers[1].abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
        providers[1].abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
        providers[1].apply_events = Apply;

        grammars[0].grammar_coordinate =
            Key(LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL, 0x31u);
        grammars[0].provider_fingerprint = providers[0].provider_fingerprint;
        grammars[0].release_fingerprint = Digest(0x41u);
        grammars[0].conformance_fingerprint = Digest(0x51u);
        grammars[0].kind_base = BaseA;
        grammars[0].kind_count = 256u;
        grammars[1].grammar_coordinate =
            Key(LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL, 0x61u);
        grammars[1].provider_fingerprint = providers[1].provider_fingerprint;
        grammars[1].release_fingerprint = Digest(0x71u);
        grammars[1].conformance_fingerprint = Digest(0x81u);
        grammars[1].kind_base = BaseB;
        grammars[1].kind_count = 256u;

        EXPECT_EQ(laplace_grammar_registry_create(
                      grammars.data(), grammars.size(), &registry),
                  LAPLACE_UNIVERSAL_AST_OK);
        EXPECT_NE(registry, nullptr);
        EXPECT_EQ(laplace_grammar_registry_fingerprint(
                      registry, &registry_fingerprint),
                  LAPLACE_UNIVERSAL_AST_OK);

        static constexpr char Media[] = "text/plain";
        laplace_decomposition_input input{};
        input.content.bytes = bytes.data();
        input.content.byte_count = bytes.size();
        input.content.media_type = Media;
        input.content.media_type_byte_count = sizeof(Media) - 1u;
        input.providers = providers.data();
        input.provider_count = providers.size();
        input.maximum_spans = 16u;
        input.maximum_depth = 8u;
        EXPECT_EQ(laplace_decomposition_run(&input, &decomposition),
                  LAPLACE_DECOMPOSITION_OK);
        EXPECT_NE(decomposition, nullptr);
    }

    ~Fixture() {
        laplace_decomposition_result_destroy(&decomposition);
        laplace_grammar_registry_destroy(&registry);
    }
};

laplace_ast_recipe_rule Rule(
    const laplace_digest256& provider,
    const std::uint64_t kind,
    const std::uint64_t grammar_kind,
    const std::uint64_t field,
    const std::uint8_t role_seed) {
    laplace_ast_recipe_rule rule{};
    rule.provider_fingerprint = provider;
    rule.kind = kind;
    rule.grammar_kind = grammar_kind;
    rule.field_kind = field;
    rule.required_syntax_flags = LAPLACE_DECOMPOSITION_SYNTAX_NAMED;
    rule.disposition = LAPLACE_AST_RULE_PRESERVE;
    rule.role_coordinate = Key(LAPLACE_HIGHWAY_KIND_AST_ROLE, role_seed);
    return rule;
}

laplace_ast_recipe* MakeRecipe(
    Fixture& fixture,
    const std::vector<laplace_ast_recipe_rule>& rules,
    const std::uint32_t flags = 0u) {
    laplace_ast_recipe_declaration declaration{};
    declaration.recipe_coordinate = Key(LAPLACE_HIGHWAY_KIND_RECIPE, 0xa1u);
    declaration.grammar_registry_fingerprint = fixture.registry_fingerprint;
    declaration.conformance_fingerprint = Digest(0xb1u);
    declaration.flags = flags;
    if ((flags & LAPLACE_AST_RECIPE_ALLOWS_DECLARED_LOSS) != 0u) {
        declaration.loss_contract_fingerprint = Digest(0xc1u);
    }
    laplace_ast_recipe* recipe = nullptr;
    EXPECT_EQ(laplace_ast_recipe_create(
                  fixture.registry, &declaration,
                  rules.data(), rules.size(), &recipe),
              LAPLACE_UNIVERSAL_AST_OK);
    return recipe;
}

laplace_universal_ast_status Compile(
    Fixture& fixture,
    laplace_ast_recipe* recipe,
    laplace_universal_ast_plan** plan) {
    static constexpr char Media[] = "text/plain";
    laplace_decomposition_content content{};
    content.bytes = fixture.bytes.data();
    content.byte_count = fixture.bytes.size();
    content.media_type = Media;
    content.media_type_byte_count = sizeof(Media) - 1u;
    laplace_universal_ast_compile_input input{};
    input.grammar_registry = fixture.registry;
    input.recipe = recipe;
    input.content = &content;
    input.decomposition = fixture.decomposition;
    input.geometry_epoch = Digest(0xd1u);
    input.occurrence_context_fingerprint = Digest(0xe1u);
    input.source_ordinal_base = 100u;
    return laplace_universal_ast_plan_create(&input, plan);
}

bool SameReference(
    const laplace_composition_operand& left,
    const laplace_composition_operand& right) {
    return left.reference_index == right.reference_index &&
        left.multiplicity == right.multiplicity &&
        left.relationship_metadata == right.relationship_metadata &&
        left.reference_kind == right.reference_kind &&
        left.flags == right.flags;
}

std::vector<laplace_ast_recipe_rule> CompleteRules(const Fixture& fixture) {
    return {
        Rule(fixture.providers[0].provider_fingerprint,
             BaseA | 1u, BaseA | 11u, 0u, 0x12u),
        Rule(fixture.providers[0].provider_fingerprint,
             BaseA | 2u, BaseA | 12u, 7u, 0x13u),
        Rule(fixture.providers[1].provider_fingerprint,
             BaseB | 1u, BaseB | 11u, 0u, 0x22u),
        Rule(fixture.providers[1].provider_fingerprint,
             BaseB | 2u, BaseB | 12u, 9u, 0x23u)
    };
}

TEST(UniversalAstRecipe, UnrelatedGrammarsShareContentButKeepDistinctTypedRoles) {
    Fixture fixture;
    auto rules = CompleteRules(fixture);
    laplace_ast_recipe* recipe = MakeRecipe(fixture, rules);
    ASSERT_NE(recipe, nullptr);
    laplace_universal_ast_plan* plan = nullptr;
    ASSERT_EQ(Compile(fixture, recipe, &plan), LAPLACE_UNIVERSAL_AST_OK);
    ASSERT_NE(plan, nullptr);
    laplace_universal_ast_plan_view view{};
    ASSERT_EQ(laplace_universal_ast_plan_view_get(plan, &view),
              LAPLACE_UNIVERSAL_AST_OK);
    ASSERT_EQ(view.binding_count, 4u);
    EXPECT_EQ(view.preserved_count, 4u);
    EXPECT_EQ(view.declared_loss_count, 0u);
    EXPECT_EQ(view.content_binding_count, 4u);
    EXPECT_EQ(view.bindings[0].parent_binding_index, NoBinding);
    EXPECT_EQ(view.bindings[1].parent_binding_index, 0u);
    EXPECT_EQ(view.bindings[2].parent_binding_index, NoBinding);
    EXPECT_EQ(view.bindings[3].parent_binding_index, 2u);

    /* Both grammar roots cover "xy", and both grammar children cover "x".
     * Content is reused; grammar and role coordinates remain distinct paths. */
    EXPECT_TRUE(SameReference(
        view.bindings[0].content_reference,
        view.bindings[2].content_reference));
    EXPECT_TRUE(SameReference(
        view.bindings[1].content_reference,
        view.bindings[3].content_reference));
    EXPECT_NE(std::memcmp(
                  view.bindings[0].grammar_coordinate.coordinate.bytes,
                  view.bindings[2].grammar_coordinate.coordinate.bytes,
                  sizeof(view.bindings[0].grammar_coordinate.coordinate.bytes)),
              0);
    EXPECT_NE(std::memcmp(
                  view.bindings[0].role_coordinate.coordinate.bytes,
                  view.bindings[2].role_coordinate.coordinate.bytes,
                  sizeof(view.bindings[0].role_coordinate.coordinate.bytes)),
              0);

    laplace_universal_ast_plan_destroy(&plan);
    laplace_ast_recipe_destroy(&recipe);
}

TEST(UniversalAstRecipe, UnmappedSyntaxFailsClosedInsteadOfBecomingOpaqueRecord) {
    Fixture fixture;
    auto rules = CompleteRules(fixture);
    rules.pop_back();
    laplace_ast_recipe* recipe = MakeRecipe(fixture, rules);
    ASSERT_NE(recipe, nullptr);
    laplace_universal_ast_plan* plan = nullptr;
    EXPECT_EQ(Compile(fixture, recipe, &plan),
              LAPLACE_UNIVERSAL_AST_UNMAPPED_SYNTAX);
    EXPECT_EQ(plan, nullptr);
    laplace_ast_recipe_destroy(&recipe);
}

TEST(UniversalAstRecipe, OverlappingRoleRulesAreRejectedAtCompileAsAmbiguous) {
    Fixture fixture;
    auto rules = CompleteRules(fixture);
    auto wildcard = rules[1];
    wildcard.field_kind = LAPLACE_AST_RECIPE_ANY_FIELD;
    wildcard.role_coordinate = Key(LAPLACE_HIGHWAY_KIND_AST_ROLE, 0x33u);
    rules.push_back(wildcard);
    laplace_ast_recipe* recipe = MakeRecipe(fixture, rules);
    ASSERT_NE(recipe, nullptr);
    laplace_universal_ast_plan* plan = nullptr;
    EXPECT_EQ(Compile(fixture, recipe, &plan),
              LAPLACE_UNIVERSAL_AST_AMBIGUOUS_RULE);
    EXPECT_EQ(plan, nullptr);
    laplace_ast_recipe_destroy(&recipe);
}

TEST(UniversalAstRecipe, GrammarRegistryRejectsProviderOrKindNamespaceCollision) {
    Fixture fixture;
    auto declarations = fixture.grammars;
    declarations[1].provider_fingerprint = declarations[0].provider_fingerprint;
    laplace_grammar_registry* duplicate = nullptr;
    EXPECT_EQ(laplace_grammar_registry_create(
                  declarations.data(), declarations.size(), &duplicate),
              LAPLACE_UNIVERSAL_AST_GRAMMAR_DUPLICATE);
    EXPECT_EQ(duplicate, nullptr);

    declarations = fixture.grammars;
    declarations[1].kind_base = declarations[0].kind_base;
    EXPECT_EQ(laplace_grammar_registry_create(
                  declarations.data(), declarations.size(), &duplicate),
              LAPLACE_UNIVERSAL_AST_GRAMMAR_DUPLICATE);
    EXPECT_EQ(duplicate, nullptr);
}

}  // namespace