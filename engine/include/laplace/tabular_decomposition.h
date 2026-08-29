#ifndef LAPLACE_TABULAR_DECOMPOSITION_H
#define LAPLACE_TABULAR_DECOMPOSITION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/decomposition_composition.h"
#include "laplace/export.h"
#include "laplace/tabular_source.h"
#include "laplace/uax29.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_tabular_decomposition_plan
    laplace_tabular_decomposition_plan;

typedef enum laplace_tabular_decomposition_status {
    LAPLACE_TABULAR_DECOMPOSITION_OK = 0,
    LAPLACE_TABULAR_DECOMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_TABULAR_DECOMPOSITION_SOURCE_PLAN_INVALID = 2,
    LAPLACE_TABULAR_DECOMPOSITION_PROVIDER_FAILURE = 3,
    LAPLACE_TABULAR_DECOMPOSITION_COMPOSITION_FAILURE = 4,
    LAPLACE_TABULAR_DECOMPOSITION_MEMORY_FAILURE = 5,
    LAPLACE_TABULAR_DECOMPOSITION_OVERFLOW = 6
} laplace_tabular_decomposition_status;

typedef struct laplace_tabular_decomposition_occurrence {
    laplace_decomposition_composition_occurrence occurrence;
    uint64_t artifact_index;
} laplace_tabular_decomposition_occurrence;

typedef struct laplace_tabular_decomposition_input {
    const laplace_tabular_source_plan* source_plan;
    const laplace_tabular_artifact* artifacts;
    uint64_t artifact_count;
    const laplace_uax29_tables* uax29;
    laplace_digest256 uax29_provider_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    uint64_t first_source_ordinal;
    uint32_t flags;
    uint32_t reserved;
} laplace_tabular_decomposition_input;

typedef struct laplace_tabular_decomposition_plan_view {
    const uint32_t* atom_positions;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    const laplace_tabular_decomposition_occurrence* occurrences;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t occurrence_count;
    uint64_t artifact_count;
} laplace_tabular_decomposition_plan_view;

/*
 * Extends a validated tabular source envelope with recursive content
 * decomposition. The tabular grammar exposes exact field spans; UAX29 then
 * decomposes those field bytes without rewriting them. All resulting textual
 * spans lower onto the same Unicode atom positions and preserve provider/kind
 * occurrence metadata separately from content identity.
 */
LAPLACE_API laplace_tabular_decomposition_status
laplace_tabular_decomposition_plan_create(
    const laplace_tabular_decomposition_input* input,
    laplace_tabular_decomposition_plan** plan);

LAPLACE_API laplace_tabular_decomposition_status
laplace_tabular_decomposition_plan_view_get(
    const laplace_tabular_decomposition_plan* plan,
    laplace_tabular_decomposition_plan_view* view);

LAPLACE_API void laplace_tabular_decomposition_plan_destroy(
    laplace_tabular_decomposition_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
