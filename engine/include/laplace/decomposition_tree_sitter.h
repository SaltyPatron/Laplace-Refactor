#ifndef LAPLACE_DECOMPOSITION_TREE_SITTER_H
#define LAPLACE_DECOMPOSITION_TREE_SITTER_H

#include <stdint.h>

#include "laplace/decomposition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TSLanguage TSLanguage;

typedef struct laplace_decomposition_tree_sitter_provider {
    const TSLanguage* language;
    uint64_t kind_base;
    laplace_decomposition_provider_v1 provider;
} laplace_decomposition_tree_sitter_provider;

/*
 * kind_base reserves the provider's upper 48 bits; the Tree-sitter symbol id is
 * placed in the low 16 bits. The provider fingerprint must bind the exact
 * grammar repository/revision/generated parser bytes and Tree-sitter runtime.
 */
LAPLACE_API laplace_decomposition_status laplace_decomposition_tree_sitter_provider_init(
    laplace_decomposition_tree_sitter_provider* storage,
    const TSLanguage* language,
    uint64_t kind_base,
    const laplace_digest256* provider_fingerprint);

#ifdef __cplusplus
}
#endif

#endif
