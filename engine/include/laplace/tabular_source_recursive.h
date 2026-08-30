#ifndef LAPLACE_TABULAR_SOURCE_RECURSIVE_H
#define LAPLACE_TABULAR_SOURCE_RECURSIVE_H

#include "laplace/tabular_source.h"
#include "laplace/unicode_root.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Product admission path: preserve the source profile's exact tabular grammar
 * while recursively dispatching every admitted artifact through the common
 * decomposition provider set before composition deposition.
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
