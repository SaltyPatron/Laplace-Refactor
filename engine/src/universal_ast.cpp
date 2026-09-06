#include "laplace/universal_ast.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

#include "blake3.h"

namespace {

constexpr std::uint32_t PlanVersion = 1u;
constexpr std::uint64_t NoBinding = UINT64_MAX;
constexpr std::uint64_t SymbolLowMask = UINT64_C(0xffff);
constexpr std::uint64_t SymbolBaseMask = ~SymbolLowMask;

struct GrammarEntry final {
    laplace_grammar_provider_declaration declaration{};
    laplace_highway_coordinate coordinate{};
};

struct RecipeRule final {
    laplace_ast_recipe_rule declaration{};
    laplace_highway_coordinate grammar_coordinate{};
    laplace_highway_coordinate role_coordinate{};
};

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool DigestEqual(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

int DigestCompare(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes));
}

int IdCompare(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes));
}

bool IdEqual(const laplace_id128& left, const laplace_id128& right) {
    return IdCompare(left, right) == 0;
}

bool HighwayKeyZero(const laplace_highway_key& key) {
    if (key.kind != 0u || key.reserved != 0u || key.version != 0u) return false;
    const laplace_id128 zero{};
    return IdEqual(key.authority, zero) && IdEqual(key.release, zero) &&
        IdEqual(key.name_space, zero) && IdEqual(key.local_identifier, zero);
}

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

void HashDigest(blake3_hasher& hasher, const laplace_digest256& value) {
    blake3_hasher_update(&hasher, value.bytes, sizeof(value.bytes));
}

void HashId(blake3_hasher& hasher, const laplace_id128& value) {
    blake3_hasher_update(&hasher, value.bytes, sizeof(value.bytes));
}

void HashCoordinate(blake3_hasher& hasher, const laplace_highway_coordinate& value) {
    HashId(hasher, value.coordinate);
    HashDigest(hasher, value.collision_fingerprint);
    HashU32(hasher, value.kind);
    HashU64(hasher, value.version);
}

void HashReference(blake3_hasher& hasher, const laplace_composition_operand& value) {
    HashU64(hasher, value.reference_index);
    HashU64(hasher, value.multiplicity);
    HashU64(hasher, value.relationship_metadata);
    HashU32(hasher, value.reference_kind);
    HashU32(hasher, value.flags);
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 value{};
    blake3_hasher_finalize(&hasher, value.bytes, sizeof(value.bytes));
    return value;
}

void BeginHash(blake3_hasher& hasher, const std::string_view domain) {
    blake3_hasher_init(&hasher);
    HashU64(hasher, static_cast<std::uint64_t>(domain.size()));
    blake3_hasher_update(&hasher, domain.data(), domain.size());
}

bool CoordinateForKey(
    const laplace_highway_key& key,
    const std::uint32_t expected_kind,
    laplace_highway_coordinate& output) {
    return key.kind == expected_kind && key.reserved == 0u &&
        laplace_highway_coordinate_calculate(&key, &output) == LAPLACE_HIGHWAY_OK &&
        output.kind == expected_kind;
}

bool GrammarEntryLess(const GrammarEntry& left, const GrammarEntry& right) {
    const int provider = DigestCompare(
        left.declaration.provider_fingerprint,
        right.declaration.provider_fingerprint);
    if (provider != 0) return provider < 0;
    return IdCompare(left.coordinate.coordinate, right.coordinate.coordinate) < 0;
}

const GrammarEntry* ResolveProvider(
    const laplace_grammar_registry* registry,
    const laplace_digest256& provider_fingerprint);

laplace_universal_ast_status ValidateSymbol(
    const GrammarEntry& grammar,
    const std::uint64_t kind,
    const std::uint64_t grammar_kind) {
    const auto in_range = [&](const std::uint64_t value) {
        return (value & SymbolBaseMask) == grammar.declaration.kind_base &&
            (value & SymbolLowMask) < grammar.declaration.kind_count;
    };
    return in_range(kind) && in_range(grammar_kind)
        ? LAPLACE_UNIVERSAL_AST_OK
        : LAPLACE_UNIVERSAL_AST_SYMBOL_OUT_OF_RANGE;
}

bool RuleLess(const RecipeRule& left, const RecipeRule& right) {
    int comparison = DigestCompare(
        left.declaration.provider_fingerprint,
        right.declaration.provider_fingerprint);
    if (comparison != 0) return comparison < 0;
    if (left.declaration.kind != right.declaration.kind) {
        return left.declaration.kind < right.declaration.kind;
    }
    if (left.declaration.grammar_kind != right.declaration.grammar_kind) {
        return left.declaration.grammar_kind < right.declaration.grammar_kind;
    }
    if (left.declaration.field_kind != right.declaration.field_kind) {
        return left.declaration.field_kind < right.declaration.field_kind;
    }
    if (left.declaration.required_syntax_flags !=
        right.declaration.required_syntax_flags) {
        return left.declaration.required_syntax_flags <
            right.declaration.required_syntax_flags;
    }
    if (left.declaration.forbidden_syntax_flags !=
        right.declaration.forbidden_syntax_flags) {
        return left.declaration.forbidden_syntax_flags <
            right.declaration.forbidden_syntax_flags;
    }
    if (left.declaration.disposition != right.declaration.disposition) {
        return left.declaration.disposition < right.declaration.disposition;
    }
    return IdCompare(
        left.role_coordinate.coordinate,
        right.role_coordinate.coordinate) < 0;
}

bool RuleMatches(
    const RecipeRule& rule,
    const laplace_decomposition_span& span) {
    if (!DigestEqual(rule.declaration.provider_fingerprint,
                     span.provider_fingerprint) ||
        rule.declaration.kind != span.kind ||
        rule.declaration.grammar_kind != span.grammar_kind ||
        (rule.declaration.field_kind != LAPLACE_AST_RECIPE_ANY_FIELD &&
         rule.declaration.field_kind != span.field_kind)) {
        return false;
    }
    if ((span.syntax_flags & rule.declaration.required_syntax_flags) !=
        rule.declaration.required_syntax_flags) {
        return false;
    }
    return (span.syntax_flags & rule.declaration.forbidden_syntax_flags) == 0u;
}

}  // namespace

struct laplace_grammar_registry {
    laplace_digest256 fingerprint{};
    std::vector<GrammarEntry> entries;
};

struct laplace_ast_recipe {
    laplace_ast_recipe_declaration declaration{};
    laplace_highway_coordinate coordinate{};
    laplace_digest256 fingerprint{};
    std::vector<RecipeRule> rules;
};

struct laplace_universal_ast_plan {
    laplace_universal_ast_plan_view view{};
    laplace_decomposition_composition_plan* composition_plan{};
    std::vector<laplace_universal_ast_binding> bindings;
};

namespace {

const GrammarEntry* ResolveProvider(
    const laplace_grammar_registry* registry,
    const laplace_digest256& provider_fingerprint) {
    if (registry == nullptr) return nullptr;
    const auto found = std::lower_bound(
        registry->entries.begin(), registry->entries.end(), provider_fingerprint,
        [](const GrammarEntry& entry, const laplace_digest256& value) {
            return DigestCompare(entry.declaration.provider_fingerprint, value) < 0;
        });
    if (found == registry->entries.end() ||
        !DigestEqual(found->declaration.provider_fingerprint, provider_fingerprint)) {
        return nullptr;
    }
    return &*found;
}

laplace_digest256 GrammarRegistryFingerprint(
    const std::vector<GrammarEntry>& entries) {
    blake3_hasher hasher;
    BeginHash(hasher, "laplace-grammar-registry-v1");
    HashU64(hasher, static_cast<std::uint64_t>(entries.size()));
    for (const auto& entry : entries) {
        HashCoordinate(hasher, entry.coordinate);
        HashDigest(hasher, entry.declaration.provider_fingerprint);
        HashDigest(hasher, entry.declaration.release_fingerprint);
        HashDigest(hasher, entry.declaration.conformance_fingerprint);
        HashU64(hasher, entry.declaration.kind_base);
        HashU64(hasher, entry.declaration.kind_count);
        HashU32(hasher, entry.declaration.flags);
    }
    return Finish(hasher);
}

laplace_digest256 RecipeFingerprint(const laplace_ast_recipe& recipe) {
    blake3_hasher hasher;
    BeginHash(hasher, "laplace-universal-ast-recipe-v1");
    HashCoordinate(hasher, recipe.coordinate);
    HashDigest(hasher, recipe.declaration.grammar_registry_fingerprint);
    HashDigest(hasher, recipe.declaration.loss_contract_fingerprint);
    HashDigest(hasher, recipe.declaration.inverse_recipe_fingerprint);
    HashDigest(hasher, recipe.declaration.conformance_fingerprint);
    HashU32(hasher, recipe.declaration.flags);
    HashU64(hasher, static_cast<std::uint64_t>(recipe.rules.size()));
    for (const auto& rule : recipe.rules) {
        HashCoordinate(hasher, rule.grammar_coordinate);
        HashDigest(hasher, rule.declaration.provider_fingerprint);
        HashU64(hasher, rule.declaration.kind);
        HashU64(hasher, rule.declaration.grammar_kind);
        HashU64(hasher, rule.declaration.field_kind);
        HashU32(hasher, rule.declaration.required_syntax_flags);
        HashU32(hasher, rule.declaration.forbidden_syntax_flags);
        HashU32(hasher, rule.declaration.disposition);
        HashU32(hasher, rule.declaration.flags);
        if (rule.declaration.disposition == LAPLACE_AST_RULE_PRESERVE) {
            HashCoordinate(hasher, rule.role_coordinate);
        }
    }
    return Finish(hasher);
}

laplace_universal_ast_status FindRule(
    const laplace_ast_recipe& recipe,
    const laplace_decomposition_span& span,
    const RecipeRule** match) {
    *match = nullptr;
    for (const auto& rule : recipe.rules) {
        if (!RuleMatches(rule, span)) continue;
        if (*match != nullptr) return LAPLACE_UNIVERSAL_AST_AMBIGUOUS_RULE;
        *match = &rule;
    }
    return *match == nullptr
        ? LAPLACE_UNIVERSAL_AST_UNMAPPED_SYNTAX
        : LAPLACE_UNIVERSAL_AST_OK;
}

void BuildPlanFingerprints(laplace_universal_ast_plan& plan) {
    blake3_hasher semantic;
    BeginHash(semantic, "laplace-universal-ast-plan-v1");
    HashDigest(semantic, plan.view.grammar_registry_fingerprint);
    HashDigest(semantic, plan.view.recipe_fingerprint);
    HashU64(semantic, static_cast<std::uint64_t>(plan.bindings.size()));
    for (const auto& binding : plan.bindings) {
        HashCoordinate(semantic, binding.grammar_coordinate);
        HashCoordinate(semantic, binding.role_coordinate);
        HashU64(semantic, binding.parent_binding_index);
        HashU64(semantic, binding.sibling_ordinal);
        HashU32(semantic, binding.disposition);
        HashU32(semantic, binding.has_content);
        if (binding.has_content != 0u) HashReference(semantic, binding.content_reference);
    }
    plan.view.plan_fingerprint = Finish(semantic);

    blake3_hasher witness;
    BeginHash(witness, "laplace-universal-ast-witness-v1");
    HashDigest(witness, plan.view.plan_fingerprint);
    HashDigest(witness, plan.view.composition.trace_fingerprint);
    for (const auto& binding : plan.bindings) {
        HashDigest(witness, binding.provider_fingerprint);
        HashU64(witness, binding.syntax_span_index);
        HashU64(witness, binding.byte_start);
        HashU64(witness, binding.byte_end);
        HashU64(witness, binding.kind);
        HashU64(witness, binding.grammar_kind);
        HashU64(witness, binding.field_kind);
        HashU64(witness, binding.sibling_ordinal);
        HashU32(witness, binding.syntax_flags);
    }
    plan.view.witness_fingerprint = Finish(witness);
}

}  // namespace

extern "C" laplace_universal_ast_status laplace_grammar_registry_create(
    const laplace_grammar_provider_declaration* declarations,
    const size_t declaration_count,
    laplace_grammar_registry** registry) {
    if (declarations == nullptr || declaration_count == 0u || registry == nullptr ||
        *registry != nullptr) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    try {
        auto* created = new laplace_grammar_registry{};
        created->entries.reserve(declaration_count);
        for (size_t index = 0u; index < declaration_count; ++index) {
            const auto& declaration = declarations[index];
            GrammarEntry entry{};
            entry.declaration = declaration;
            if (declaration.flags != 0u || declaration.reserved != 0u ||
                DigestZero(declaration.provider_fingerprint) ||
                DigestZero(declaration.release_fingerprint) ||
                DigestZero(declaration.conformance_fingerprint) ||
                (declaration.kind_base & SymbolLowMask) != 0u ||
                declaration.kind_count == 0u ||
                declaration.kind_count > SymbolLowMask + 1u ||
                !CoordinateForKey(
                    declaration.grammar_coordinate,
                    LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL,
                    entry.coordinate)) {
                delete created;
                return LAPLACE_UNIVERSAL_AST_GRAMMAR_INVALID;
            }
            created->entries.push_back(entry);
        }
        std::sort(created->entries.begin(), created->entries.end(), GrammarEntryLess);
        for (size_t index = 1u; index < created->entries.size(); ++index) {
            const auto& prior = created->entries[index - 1u];
            const auto& current = created->entries[index];
            if (DigestEqual(prior.declaration.provider_fingerprint,
                            current.declaration.provider_fingerprint) ||
                IdEqual(prior.coordinate.coordinate, current.coordinate.coordinate) ||
                prior.declaration.kind_base == current.declaration.kind_base) {
                delete created;
                return LAPLACE_UNIVERSAL_AST_GRAMMAR_DUPLICATE;
            }
        }
        created->fingerprint = GrammarRegistryFingerprint(created->entries);
        *registry = created;
        return LAPLACE_UNIVERSAL_AST_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_UNIVERSAL_AST_MEMORY_FAILURE;
    }
}

extern "C" laplace_universal_ast_status laplace_grammar_registry_fingerprint(
    const laplace_grammar_registry* registry,
    laplace_digest256* fingerprint) {
    if (registry == nullptr || fingerprint == nullptr) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    *fingerprint = registry->fingerprint;
    return LAPLACE_UNIVERSAL_AST_OK;
}

extern "C" void laplace_grammar_registry_destroy(laplace_grammar_registry** registry) {
    if (registry == nullptr) return;
    delete *registry;
    *registry = nullptr;
}

extern "C" laplace_universal_ast_status laplace_ast_recipe_create(
    const laplace_grammar_registry* registry,
    const laplace_ast_recipe_declaration* declaration,
    const laplace_ast_recipe_rule* rules,
    const size_t rule_count,
    laplace_ast_recipe** recipe) {
    if (registry == nullptr || declaration == nullptr || rules == nullptr ||
        rule_count == 0u || recipe == nullptr || *recipe != nullptr) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    if (!DigestEqual(declaration->grammar_registry_fingerprint,
                     registry->fingerprint)) {
        return LAPLACE_UNIVERSAL_AST_RECIPE_REGISTRY_MISMATCH;
    }
    if ((declaration->flags & ~LAPLACE_AST_RECIPE_KNOWN_FLAGS) != 0u ||
        declaration->reserved != 0u ||
        DigestZero(declaration->conformance_fingerprint) ||
        ((declaration->flags & LAPLACE_AST_RECIPE_HAS_INVERSE) != 0u) !=
            !DigestZero(declaration->inverse_recipe_fingerprint) ||
        ((declaration->flags & LAPLACE_AST_RECIPE_ALLOWS_DECLARED_LOSS) != 0u &&
         DigestZero(declaration->loss_contract_fingerprint))) {
        return LAPLACE_UNIVERSAL_AST_RECIPE_INVALID;
    }

    try {
        auto* created = new laplace_ast_recipe{};
        created->declaration = *declaration;
        if (!CoordinateForKey(
                declaration->recipe_coordinate,
                LAPLACE_HIGHWAY_KIND_RECIPE,
                created->coordinate)) {
            delete created;
            return LAPLACE_UNIVERSAL_AST_RECIPE_INVALID;
        }
        created->rules.reserve(rule_count);
        for (size_t index = 0u; index < rule_count; ++index) {
            RecipeRule rule{};
            rule.declaration = rules[index];
            const GrammarEntry* grammar = ResolveProvider(
                registry, rule.declaration.provider_fingerprint);
            if (grammar == nullptr) {
                delete created;
                return LAPLACE_UNIVERSAL_AST_UNREGISTERED_PROVIDER;
            }
            const auto symbol_status = ValidateSymbol(
                *grammar, rule.declaration.kind, rule.declaration.grammar_kind);
            if (symbol_status != LAPLACE_UNIVERSAL_AST_OK) {
                delete created;
                return symbol_status;
            }
            if (rule.declaration.flags != 0u ||
                (rule.declaration.required_syntax_flags &
                 ~LAPLACE_DECOMPOSITION_KNOWN_SYNTAX_FLAGS) != 0u ||
                (rule.declaration.forbidden_syntax_flags &
                 ~LAPLACE_DECOMPOSITION_KNOWN_SYNTAX_FLAGS) != 0u ||
                (rule.declaration.required_syntax_flags &
                 rule.declaration.forbidden_syntax_flags) != 0u ||
                (rule.declaration.disposition != LAPLACE_AST_RULE_PRESERVE &&
                 rule.declaration.disposition != LAPLACE_AST_RULE_DECLARED_LOSS)) {
                delete created;
                return LAPLACE_UNIVERSAL_AST_RECIPE_INVALID;
            }
            rule.grammar_coordinate = grammar->coordinate;
            if (rule.declaration.disposition == LAPLACE_AST_RULE_PRESERVE) {
                if (!CoordinateForKey(
                        rule.declaration.role_coordinate,
                        LAPLACE_HIGHWAY_KIND_AST_ROLE,
                        rule.role_coordinate)) {
                    delete created;
                    return LAPLACE_UNIVERSAL_AST_RECIPE_INVALID;
                }
            } else {
                if ((declaration->flags &
                     LAPLACE_AST_RECIPE_ALLOWS_DECLARED_LOSS) == 0u ||
                    !HighwayKeyZero(rule.declaration.role_coordinate)) {
                    delete created;
                    return LAPLACE_UNIVERSAL_AST_RECIPE_INVALID;
                }
            }
            created->rules.push_back(rule);
        }
        std::sort(created->rules.begin(), created->rules.end(), RuleLess);
        created->fingerprint = RecipeFingerprint(*created);
        *recipe = created;
        return LAPLACE_UNIVERSAL_AST_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_UNIVERSAL_AST_MEMORY_FAILURE;
    }
}

extern "C" laplace_universal_ast_status laplace_ast_recipe_fingerprint(
    const laplace_ast_recipe* recipe,
    laplace_digest256* fingerprint) {
    if (recipe == nullptr || fingerprint == nullptr) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    *fingerprint = recipe->fingerprint;
    return LAPLACE_UNIVERSAL_AST_OK;
}

extern "C" void laplace_ast_recipe_destroy(laplace_ast_recipe** recipe) {
    if (recipe == nullptr) return;
    delete *recipe;
    *recipe = nullptr;
}

extern "C" laplace_universal_ast_status laplace_universal_ast_plan_create(
    const laplace_universal_ast_compile_input* input,
    laplace_universal_ast_plan** plan) {
    if (input == nullptr || plan == nullptr || *plan != nullptr ||
        input->grammar_registry == nullptr || input->recipe == nullptr ||
        input->content == nullptr || input->decomposition == nullptr ||
        input->flags != 0u || input->reserved != 0u) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    if (!DigestEqual(input->grammar_registry->fingerprint,
                     input->recipe->declaration.grammar_registry_fingerprint)) {
        return LAPLACE_UNIVERSAL_AST_RECIPE_REGISTRY_MISMATCH;
    }

    try {
        auto* created = new laplace_universal_ast_plan{};
        laplace_decomposition_composition_input canonical_input{};
        canonical_input.content = input->content;
        canonical_input.decomposition = input->decomposition;
        canonical_input.recipe_fingerprint = input->recipe->fingerprint;
        canonical_input.geometry_epoch = input->geometry_epoch;
        canonical_input.occurrence_context_fingerprint =
            input->occurrence_context_fingerprint;
        canonical_input.source_ordinal_base = input->source_ordinal_base;
        const auto canonical_status = laplace_decomposition_composition_plan_create(
            &canonical_input, &created->composition_plan);
        if (canonical_status != LAPLACE_DECOMPOSITION_COMPOSITION_OK ||
            created->composition_plan == nullptr ||
            laplace_decomposition_composition_plan_view_get(
                created->composition_plan, &created->view.composition) !=
                LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
            laplace_decomposition_composition_plan_destroy(&created->composition_plan);
            delete created;
            return LAPLACE_UNIVERSAL_AST_CANONICAL_CONTENT_FAILURE;
        }

        std::size_t span_count = 0u;
        const auto* spans = laplace_decomposition_spans(
            input->decomposition, &span_count);
        if (spans == nullptr || span_count == 0u ||
            created->view.composition.span_count != span_count) {
            laplace_decomposition_composition_plan_destroy(&created->composition_plan);
            delete created;
            return LAPLACE_UNIVERSAL_AST_DECOMPOSITION_INVALID;
        }

        std::vector<std::uint64_t> span_to_binding(span_count, NoBinding);
        created->bindings.reserve(span_count > 0u ? span_count - 1u : 0u);
        for (std::size_t span_index = 1u; span_index < span_count; ++span_index) {
            const auto& span = spans[span_index];
            const GrammarEntry* grammar = ResolveProvider(
                input->grammar_registry, span.provider_fingerprint);
            if (grammar == nullptr) {
                laplace_decomposition_composition_plan_destroy(&created->composition_plan);
                delete created;
                return LAPLACE_UNIVERSAL_AST_UNREGISTERED_PROVIDER;
            }
            const auto symbol_status = ValidateSymbol(*grammar, span.kind, span.grammar_kind);
            if (symbol_status != LAPLACE_UNIVERSAL_AST_OK) {
                laplace_decomposition_composition_plan_destroy(&created->composition_plan);
                delete created;
                return symbol_status;
            }
            const RecipeRule* rule = nullptr;
            const auto rule_status = FindRule(*input->recipe, span, &rule);
            if (rule_status != LAPLACE_UNIVERSAL_AST_OK) {
                laplace_decomposition_composition_plan_destroy(&created->composition_plan);
                delete created;
                return rule_status;
            }

            laplace_universal_ast_binding binding{};
            binding.grammar_coordinate = grammar->coordinate;
            binding.role_coordinate = rule->role_coordinate;
            binding.provider_fingerprint = span.provider_fingerprint;
            binding.syntax_span_index = static_cast<std::uint64_t>(span_index);
            binding.byte_start = span.byte_start;
            binding.byte_end = span.byte_end;
            binding.kind = span.kind;
            binding.grammar_kind = span.grammar_kind;
            binding.field_kind = span.field_kind;
            binding.sibling_ordinal = span.sibling_ordinal;
            binding.syntax_flags = span.syntax_flags;
            binding.disposition = rule->declaration.disposition;
            binding.has_content = created->view.composition.span_has_content[span_index];
            if (binding.has_content != 0u) {
                binding.content_reference =
                    created->view.composition.span_references[span_index];
                ++created->view.content_binding_count;
            }
            if ((span.syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_MISSING) != 0u) {
                ++created->view.missing_binding_count;
            }
            if ((span.syntax_flags &
                 (LAPLACE_DECOMPOSITION_SYNTAX_ERROR |
                  LAPLACE_DECOMPOSITION_SYNTAX_HAS_ERROR)) != 0u) {
                ++created->view.error_binding_count;
            }
            if (binding.disposition == LAPLACE_AST_RULE_PRESERVE) {
                ++created->view.preserved_count;
            } else {
                ++created->view.declared_loss_count;
            }

            if (span.parent_span_index == 0u) {
                binding.parent_binding_index = NoBinding;
            } else if (span.parent_span_index < span_to_binding.size() &&
                       span_to_binding[static_cast<std::size_t>(
                           span.parent_span_index)] != NoBinding) {
                binding.parent_binding_index = span_to_binding[
                    static_cast<std::size_t>(span.parent_span_index)];
            } else {
                laplace_decomposition_composition_plan_destroy(&created->composition_plan);
                delete created;
                return LAPLACE_UNIVERSAL_AST_DECOMPOSITION_INVALID;
            }
            span_to_binding[span_index] =
                static_cast<std::uint64_t>(created->bindings.size());
            created->bindings.push_back(binding);
        }

        created->view.grammar_registry_fingerprint =
            input->grammar_registry->fingerprint;
        created->view.recipe_fingerprint = input->recipe->fingerprint;
        created->view.bindings = created->bindings.empty()
            ? nullptr : created->bindings.data();
        created->view.binding_count =
            static_cast<std::uint64_t>(created->bindings.size());
        created->view.version = PlanVersion;
        created->view.status = LAPLACE_UNIVERSAL_AST_OK;
        BuildPlanFingerprints(*created);
        *plan = created;
        return LAPLACE_UNIVERSAL_AST_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_UNIVERSAL_AST_MEMORY_FAILURE;
    }
}

extern "C" laplace_universal_ast_status laplace_universal_ast_plan_view_get(
    const laplace_universal_ast_plan* plan,
    laplace_universal_ast_plan_view* view) {
    if (plan == nullptr || view == nullptr || plan->view.version != PlanVersion ||
        plan->view.status != LAPLACE_UNIVERSAL_AST_OK ||
        plan->view.binding_count != plan->bindings.size() ||
        (plan->view.binding_count != 0u && plan->view.bindings == nullptr)) {
        return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
    }
    *view = plan->view;
    return LAPLACE_UNIVERSAL_AST_OK;
}

extern "C" void laplace_universal_ast_plan_destroy(
    laplace_universal_ast_plan** plan) {
    if (plan == nullptr || *plan == nullptr) return;
    laplace_decomposition_composition_plan_destroy(&(*plan)->composition_plan);
    delete *plan;
    *plan = nullptr;
}