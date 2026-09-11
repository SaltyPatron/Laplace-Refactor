#ifndef LAPLACE_TABULAR_SOURCE_RECURSIVE_H
#define LAPLACE_TABULAR_SOURCE_RECURSIVE_H

#include <stdint.h>

#include "laplace/decomposition.h"
#include "laplace/tabular_source.h"
#include "laplace/unicode_root.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Source-neutral recursive admission path. The tabular profile remains the
 * envelope/reconstruction authority for this adapter, while structural content
 * is dispatched through the caller-selected common decomposition providers plus
 * the exact artifact grammar provider derived from the tabular recipe.
 *
 * No Unicode source bundle, language provider, parser family, or host storage is
 * implicitly required here. Provider order is part of the execution boundary.
 * Structural spans are lowered through the common decomposition->composition
 * bridge and inserted before the existing source root; syntax witness metadata
 * never becomes canonical content identity by itself.
 */
LAPLACE_API laplace_tabular_source_status
laplace_tabular_source_plan_create_recursive_with_providers(
    const laplace_tabular_source_input* input,
    const laplace_decomposition_provider_v1* common_providers,
    uint64_t common_provider_count,
    laplace_tabular_source_plan** plan);

/* Compatibility adapter for callers that still select the verified Unicode
 * UAX #29 provider from a Unicode bundle. It constructs that provider and then
 * delegates to the provider-neutral entrypoint above; the generic recursive
 * source engine itself does not own or open Unicode source data. */
LAPLACE_API laplace_tabular_source_status
laplace_tabular_source_plan_create_recursive(
    const laplace_tabular_source_input* input,
    const laplace_unicode_source_bundle* unicode_bundle,
    laplace_tabular_source_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
