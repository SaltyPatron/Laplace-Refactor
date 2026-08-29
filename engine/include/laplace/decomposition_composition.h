#ifndef LAPLACE_DECOMPOSITION_COMPOSITION_H
#define LAPLACE_DECOMPOSITION_COMPOSITION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "laplace/decomposition.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_decomposition_composition_plan
    laplace_decomposition_composition_plan;

typedef enum laplace_decomposition_composition_status {
    LAPLACE_DECOMPOSITION_COMPOSITION_OK = 0,
    LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_DECOMPOSITION_COMPOSITION_RANGE_INVALID = 2,
    LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID = 3,
    LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE = 4,
    LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW = 5
} laplace_decomposition_composition_status;

typedef struct laplace_decomposition_composition_occurrence {
    uint64_t decomposition_span_index;
    uint64_t composition_result_index;
    uint64_t parent_decomposition_span_index;
    uint64_t byte_start;
    uint64_t byte_end;
    laplace_digest256 provider_fingerprint;
    uint64_t kind;
    uint32_t depth;
    uint32_t flags;
} laplace_decomposition_composition_occurrence;

typedef struct laplace_decomposition_composition_input {
    const laplace_decomposition_result* decomposition;
    const uint8_t* bytes;
    uint64_t byte_count;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    uint64_t first_source_ordinal;
    uint32_t recipe_version;
    uint32_t flags;
} laplace_decomposition_composition_input;

typedef struct laplace_decomposition_composition_plan_view {
    const uint32_t* atom_positions;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    const laplace_decomposition_composition_occurrence* occurrences;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t occurrence_count;
} laplace_decomposition_composition_plan_view;

/*
 * Lowers every TEXT span discovered by any decomposition authority onto the
 * canonical Unicode atom floor. Structural identity remains separate in the
 * occurrence table: the same exact text may reuse one composition identity while
 * UAX29, Tree-sitter, TSV, language and domain providers retain distinct witnessed
 * roles over that content. No case folding or normalization occurs here.
 */
LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_plan_create(
    const laplace_decomposition_composition_input* input,
    laplace_decomposition_composition_plan** plan);

LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_plan_view_get(
    const laplace_decomposition_composition_plan* plan,
    laplace_decomposition_composition_plan_view* view);

LAPLACE_API void laplace_decomposition_composition_plan_destroy(
    laplace_decomposition_composition_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
