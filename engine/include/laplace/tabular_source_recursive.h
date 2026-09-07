#ifndef LAPLACE_TABULAR_SOURCE_RECURSIVE_H
#define LAPLACE_TABULAR_SOURCE_RECURSIVE_H

#include "laplace/tabular_source.h"
#include "laplace/decomposition.h"
#include "laplace/unicode_root.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { LAPLACE_TABULAR_RECURSIVE_POLICY_VERSION = 1 };

typedef struct laplace_tabular_recursive_policy {
    /* Finite limits apply independently to each exact artifact decomposition. */
    uint64_t maximum_spans;
    uint32_t maximum_depth;
    uint32_t version;
} laplace_tabular_recursive_policy;

/* Qualified grammar providers share the same recursive dispatch, canonical
 * composition, retained syntax witnesses, and final source-plan lifetime.
 * Provider states are borrowed for this call only. This admits parser mechanism;
 * it does not infer source-specific semantics or close missing template claims. */
LAPLACE_API laplace_tabular_source_status
laplace_tabular_source_plan_create_recursive_with_providers(
    const laplace_tabular_source_input* input,
    const laplace_unicode_source_bundle* unicode_bundle,
    const laplace_decomposition_provider_v1* additional_providers,
    size_t additional_provider_count,
    const laplace_tabular_recursive_policy* policy,
    laplace_tabular_source_plan** plan);

/*
 * Product admission path: preserve the source profile's exact tabular grammar
 * while recursively dispatching every admitted artifact through the common
 * decomposition provider set before composition deposition.
 *
 * Structural decomposition spans are lowered to canonical content references
 * and inserted into the same working set before the existing final source
 * root. Provider, parser, span, media-type, and occurrence witness metadata do
 * not become canonical composition children, and the final source root is not
 * replaced or wrapped by decomposition testimony. Artifacts for which no
 * provider exposes structural child spans retain only their existing canonical
 * source representation.
 *
 * The verified Unicode bundle supplies UAX #29 authority for this transitional
 * path. It does not weaken or replace the exact artifact/reconstruction checks
 * performed by the underlying tabular recipe.
 */
LAPLACE_API laplace_tabular_source_status
laplace_tabular_source_plan_create_recursive(
    const laplace_tabular_source_input* input,
    const laplace_unicode_source_bundle* unicode_bundle,
    laplace_tabular_source_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
