#ifndef LAPLACE_TREE_SITTER_GRAMMAR_H
#define LAPLACE_TREE_SITTER_GRAMMAR_H

#include <stdint.h>

#include "laplace/decomposition_tree_sitter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_tree_sitter_grammar laplace_tree_sitter_grammar;

typedef enum laplace_tree_sitter_grammar_status {
    LAPLACE_TREE_SITTER_GRAMMAR_OK = 0,
    LAPLACE_TREE_SITTER_GRAMMAR_INVALID_ARGUMENT = 1,
    LAPLACE_TREE_SITTER_GRAMMAR_LIBRARY_OPEN_FAILED = 2,
    LAPLACE_TREE_SITTER_GRAMMAR_SYMBOL_MISSING = 3,
    LAPLACE_TREE_SITTER_GRAMMAR_LANGUAGE_INVALID = 4,
    LAPLACE_TREE_SITTER_GRAMMAR_MEMORY_FAILURE = 5,
    LAPLACE_TREE_SITTER_GRAMMAR_IDENTITY_MISMATCH = 6,
    LAPLACE_TREE_SITTER_GRAMMAR_UNSUPPORTED_HOST = 7
} laplace_tree_sitter_grammar_status;

/*
 * Loads one already-built, receipted grammar provider. library_path and symbol
 * are execution inputs; media_type is exact applicability authority rather than
 * an extension/name guess. provider_fingerprint must bind the exact grammar
 * repository revision, generated parser/scanner bytes, build recipe, shared
 * object, media-type declaration, and Tree-sitter runtime generation.
 */
LAPLACE_API laplace_tree_sitter_grammar_status laplace_tree_sitter_grammar_open(
    const char* library_path,
    const char* language_symbol,
    const char* media_type,
    uint64_t media_type_byte_count,
    uint64_t kind_base,
    const laplace_digest256* provider_fingerprint,
    laplace_tree_sitter_grammar** grammar);

/* Verify immutable shared-object bytes before executing its language entrypoint.
 * The declaration fingerprint binds the retained source/build/runtime receipt;
 * the returned provider identity also binds the checked bytes and exact syntax
 * selection. Linux loads the same open inode through /proc/self/fd. */
LAPLACE_API laplace_tree_sitter_grammar_status laplace_tree_sitter_grammar_open_verified(
    const char* library_path,
    const char* language_symbol,
    const char* media_type,
    uint64_t media_type_byte_count,
    uint64_t kind_base,
    const laplace_digest256* declaration_fingerprint,
    const uint8_t expected_sha256[32],
    uint64_t expected_bytes,
    laplace_tree_sitter_grammar** grammar);

LAPLACE_API const laplace_decomposition_provider_v1*
laplace_tree_sitter_grammar_provider(
    const laplace_tree_sitter_grammar* grammar);

LAPLACE_API void laplace_tree_sitter_grammar_close(
    laplace_tree_sitter_grammar** grammar);

#ifdef __cplusplus
}
#endif

#endif
