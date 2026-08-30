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
    const laplace_composition_operand* span_references;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t span_count;
    uint64_t root_result_index;
    uint32_t recipe_version;
    uint32_t flags;
    laplace_composition_operand root_reference;
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
 *
 * span_references is parallel to the decomposition span array. Every reference
 * names only the exact canonical content covered by that span. Equal span bytes
 * therefore reuse one canonical reference even when their provider, kind, range,
 * depth, media type, parentage, or other witness metadata differs. Structural
 * witnesses retain those distinctions through the decomposition result and bind
 * them to canonical content through this parallel reference array.
 *
 * root_reference is the canonical root carrier and is identical to
 * span_references[0]. A single Unicode position is a KNOWN_ENTITY reference into
 * atom_positions and therefore requires no composition request. Composite
 * content is a PRIOR_RESULT reference into requests/results. root_result_index
 * remains the composite-result alias and is UINT64_MAX when the canonical root
 * is already a known Tier-0 entity.
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