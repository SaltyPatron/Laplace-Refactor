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

typedef struct laplace_decomposition_composition_input {
    const laplace_decomposition_content* content;
    const laplace_decomposition_result* decomposition;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    uint64_t source_ordinal_base;
    uint32_t flags;
    uint32_t reserved;
} laplace_decomposition_composition_input;

typedef struct laplace_decomposition_composition_plan_view {
    laplace_digest256 trace_fingerprint;
    const uint32_t* atom_positions;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t span_count;
    uint64_t root_result_index;
    uint32_t recipe_version;
    uint32_t flags;
} laplace_decomposition_composition_plan_view;

typedef enum laplace_decomposition_composition_status {
    LAPLACE_DECOMPOSITION_COMPOSITION_OK = 0,
    LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID = 2,
    LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID = 3,
    LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE = 4,
    LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW = 5
} laplace_decomposition_composition_status;

/*
 * Lowers exact textual content exposed by decomposition into canonical
 * composition work without allowing parser/provider metadata to alter content
 * identity. Equal canonical content must resolve to the same entity regardless
 * of byte offset, provider, source, syntax role, tier, or occurrence.
 *
 * Decomposition structure remains a separate witnessed trace. Provider identity,
 * kind, byte ranges, flags, depth, media type, parentage, source and occurrence
 * context belong to that witness/evidence path; they are not constituents of the
 * content entity and must not be wrapped around content merely to mint another
 * Merkle identity.
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
