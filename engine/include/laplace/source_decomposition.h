#ifndef LAPLACE_SOURCE_DECOMPOSITION_H
#define LAPLACE_SOURCE_DECOMPOSITION_H

#include <stdint.h>

#include "laplace/decomposition.h"
#include "laplace/export.h"
#include "laplace/tabular_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Source-agnostic recursive decomposition boundary for the current source-profile
 * working set. The caller supplies the active grammar/codec authorities as the
 * same generic decomposition providers used elsewhere in Laplace. No Unicode,
 * corpus, language, media, chess, or source-family owner is selected here.
 *
 * The source profile remains responsible for exact artifact/reconstruction law.
 * Providers contribute structural spans only. Their identities, media types,
 * syntax roles, offsets, and occurrence witnesses stay outside canonical content
 * identity; content-bearing spans are lowered through the ordinary canonical
 * decomposition->composition bridge before the source working set is deposited.
 *
 * Artifact-local fixed-width/delimited grammar declared by the selected source
 * profile is appended to this common provider set. A caller therefore chooses
 * finite authorities once; this routine never performs a source-named dispatcher.
 */
LAPLACE_API laplace_tabular_source_status
laplace_source_decomposition_plan_create(
    const laplace_tabular_source_input* input,
    const laplace_decomposition_provider_v1* providers,
    uint64_t provider_count,
    laplace_tabular_source_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
