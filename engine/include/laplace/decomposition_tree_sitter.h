#ifndef LAPLACE_DECOMPOSITION_TREE_SITTER_H
#define LAPLACE_DECOMPOSITION_TREE_SITTER_H

#include <stdint.h>

#include "laplace/decomposition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TSLanguage TSLanguage;

enum { LAPLACE_TREE_SITTER_MEDIA_TYPE_CAPACITY = 128 };

typedef struct laplace_decomposition_tree_sitter_provider {
    const TSLanguage* language;
    uint64_t kind_base;
    char media_type[LAPLACE_TREE_SITTER_MEDIA_TYPE_CAPACITY];
    uint32_t media_type_byte_count;
    uint32_t reserved;
    laplace_decomposition_provider_v1 provider;
} laplace_decomposition_tree_sitter_provider;

/*
 * kind_base reserves the provider's upper 48 bits; the Tree-sitter symbol id is
 * placed in the low 16 bits. media_type is an exact applicability declaration,
 * not a filename heuristic. provider_fingerprint must bind the exact grammar
 * repository/revision/generated parser bytes, build recipe, shared object,
 * media-type declaration, and Tree-sitter runtime.
 */
LAPLACE_API laplace_decomposition_status laplace_decomposition_tree_sitter_provider_init(
    laplace_decomposition_tree_sitter_provider* storage,
    const TSLanguage* language,
    const char* media_type,
    uint64_t media_type_byte_count,
    uint64_t kind_base,
    const laplace_digest256* provider_fingerprint);

#ifdef __cplusplus
}
#endif

#endif
