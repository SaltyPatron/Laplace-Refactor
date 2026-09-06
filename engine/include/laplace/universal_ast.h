#ifndef LAPLACE_UNIVERSAL_AST_H
#define LAPLACE_UNIVERSAL_AST_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/decomposition.h"
#include "laplace/decomposition_composition.h"
#include "laplace/export.h"
#include "laplace/highway.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAPLACE_AST_RECIPE_ANY_FIELD UINT64_MAX

typedef struct laplace_grammar_registry laplace_grammar_registry;
typedef struct laplace_ast_recipe laplace_ast_recipe;
typedef struct laplace_universal_ast_plan laplace_universal_ast_plan;

typedef struct laplace_grammar_provider_declaration {
    /* Kind must be LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL. The local identifier
     * names the grammar/provider generation rather than one parsed source node. */
    laplace_highway_key grammar_coordinate;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 release_fingerprint;
    laplace_digest256 conformance_fingerprint;
    uint64_t kind_base;
    uint64_t kind_count;
    uint32_t flags;
    uint32_t reserved;
} laplace_grammar_provider_declaration;

typedef enum laplace_ast_rule_disposition {
    LAPLACE_AST_RULE_PRESERVE = 1,
    LAPLACE_AST_RULE_DECLARED_LOSS = 2
} laplace_ast_rule_disposition;

enum {
    LAPLACE_AST_RECIPE_HAS_INVERSE = 1u,
    LAPLACE_AST_RECIPE_ALLOWS_DECLARED_LOSS = 2u,
    LAPLACE_AST_RECIPE_KNOWN_FLAGS = 3u
};

typedef struct laplace_ast_recipe_rule {
    /* The provider generation is resolved through the immutable grammar
     * registry. Provider fingerprints select mechanism; the registry supplies
     * the stable grammar coordinate that becomes recipe authority. */
    laplace_digest256 provider_fingerprint;
    uint64_t kind;
    uint64_t grammar_kind;
    /* Exact field id, or LAPLACE_AST_RECIPE_ANY_FIELD when one role law applies
     * to all fields while the exact observed field remains in the witness. */
    uint64_t field_kind;
    uint32_t required_syntax_flags;
    uint32_t forbidden_syntax_flags;
    uint32_t disposition;
    uint32_t flags;
    /* Required for PRESERVE and must be an AST_ROLE Highway key. Must be the
     * canonical all-zero key for DECLARED_LOSS. */
    laplace_highway_key role_coordinate;
} laplace_ast_recipe_rule;

typedef struct laplace_ast_recipe_declaration {
    /* Kind must be LAPLACE_HIGHWAY_KIND_RECIPE. */
    laplace_highway_key recipe_coordinate;
    laplace_digest256 grammar_registry_fingerprint;
    laplace_digest256 loss_contract_fingerprint;
    laplace_digest256 inverse_recipe_fingerprint;
    laplace_digest256 conformance_fingerprint;
    uint32_t flags;
    uint32_t reserved;
} laplace_ast_recipe_declaration;

typedef struct laplace_universal_ast_compile_input {
    const laplace_grammar_registry* grammar_registry;
    const laplace_ast_recipe* recipe;
    const laplace_decomposition_content* content;
    const laplace_decomposition_result* decomposition;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    uint64_t source_ordinal_base;
    uint32_t flags;
    uint32_t reserved;
} laplace_universal_ast_compile_input;

typedef struct laplace_universal_ast_binding {
    laplace_highway_coordinate grammar_coordinate;
    laplace_highway_coordinate role_coordinate;
    laplace_digest256 provider_fingerprint;
    laplace_composition_operand content_reference;
    uint64_t syntax_span_index;
    uint64_t parent_binding_index;
    uint64_t byte_start;
    uint64_t byte_end;
    uint64_t kind;
    uint64_t grammar_kind;
    uint64_t field_kind;
    uint64_t sibling_ordinal;
    uint32_t syntax_flags;
    uint32_t disposition;
    uint32_t has_content;
    uint32_t reserved;
} laplace_universal_ast_binding;

typedef struct laplace_universal_ast_plan_view {
    laplace_digest256 grammar_registry_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 plan_fingerprint;
    laplace_digest256 witness_fingerprint;
    laplace_decomposition_composition_plan_view composition;
    const laplace_universal_ast_binding* bindings;
    uint64_t binding_count;
    uint64_t preserved_count;
    uint64_t declared_loss_count;
    uint64_t content_binding_count;
    uint64_t missing_binding_count;
    uint64_t error_binding_count;
    uint32_t version;
    uint32_t status;
} laplace_universal_ast_plan_view;

typedef enum laplace_universal_ast_status {
    LAPLACE_UNIVERSAL_AST_OK = 0,
    LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT = 1,
    LAPLACE_UNIVERSAL_AST_GRAMMAR_INVALID = 2,
    LAPLACE_UNIVERSAL_AST_GRAMMAR_DUPLICATE = 3,
    LAPLACE_UNIVERSAL_AST_RECIPE_INVALID = 4,
    LAPLACE_UNIVERSAL_AST_RECIPE_REGISTRY_MISMATCH = 5,
    LAPLACE_UNIVERSAL_AST_UNREGISTERED_PROVIDER = 6,
    LAPLACE_UNIVERSAL_AST_SYMBOL_OUT_OF_RANGE = 7,
    LAPLACE_UNIVERSAL_AST_UNMAPPED_SYNTAX = 8,
    LAPLACE_UNIVERSAL_AST_AMBIGUOUS_RULE = 9,
    LAPLACE_UNIVERSAL_AST_DECOMPOSITION_INVALID = 10,
    LAPLACE_UNIVERSAL_AST_CANONICAL_CONTENT_FAILURE = 11,
    LAPLACE_UNIVERSAL_AST_MEMORY_FAILURE = 12,
    LAPLACE_UNIVERSAL_AST_OVERFLOW = 13
} laplace_universal_ast_status;

LAPLACE_API laplace_universal_ast_status laplace_grammar_registry_create(
    const laplace_grammar_provider_declaration* declarations,
    size_t declaration_count,
    laplace_grammar_registry** registry);

LAPLACE_API laplace_universal_ast_status laplace_grammar_registry_fingerprint(
    const laplace_grammar_registry* registry,
    laplace_digest256* fingerprint);

LAPLACE_API void laplace_grammar_registry_destroy(
    laplace_grammar_registry** registry);

LAPLACE_API laplace_universal_ast_status laplace_ast_recipe_create(
    const laplace_grammar_registry* registry,
    const laplace_ast_recipe_declaration* declaration,
    const laplace_ast_recipe_rule* rules,
    size_t rule_count,
    laplace_ast_recipe** recipe);

LAPLACE_API laplace_universal_ast_status laplace_ast_recipe_fingerprint(
    const laplace_ast_recipe* recipe,
    laplace_digest256* fingerprint);

LAPLACE_API void laplace_ast_recipe_destroy(laplace_ast_recipe** recipe);

/*
 * Compile concrete syntax into one typed universal-AST plan. Canonical content
 * is lowered through the existing decomposition->composition Merkle bridge;
 * grammar symbols/fields/error state are mapped by immutable recipe rules to
 * full Highway AST-role coordinates. Unregistered providers, unmapped syntax,
 * and ambiguous rules fail closed before any persistence/effect boundary.
 */
LAPLACE_API laplace_universal_ast_status laplace_universal_ast_plan_create(
    const laplace_universal_ast_compile_input* input,
    laplace_universal_ast_plan** plan);

LAPLACE_API laplace_universal_ast_status laplace_universal_ast_plan_view_get(
    const laplace_universal_ast_plan* plan,
    laplace_universal_ast_plan_view* view);

LAPLACE_API void laplace_universal_ast_plan_destroy(
    laplace_universal_ast_plan** plan);

#ifdef __cplusplus
}
#endif

#endif