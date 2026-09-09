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
    const uint8_t* span_has_content;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t span_count;
    uint64_t root_result_index;
    uint32_t recipe_version;
    uint32_t flags;
    laplace_composition_operand root_reference;
} laplace_decomposition_composition_plan_view;

typedef struct laplace_decomposition_composition_identity {
    laplace_id128 entity_id;
    laplace_digest256 identity_witness;
    uint64_t logical_count;
} laplace_decomposition_composition_identity;

typedef enum laplace_decomposition_composition_status {
    LAPLACE_DECOMPOSITION_COMPOSITION_OK = 0,
    LAPLACE_DECOMPOSITION_COMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID = 2,
    LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID = 3,
    LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE = 4,
    LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW = 5
} laplace_decomposition_composition_status;

LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_plan_create(
    const laplace_decomposition_composition_input* input,
    laplace_decomposition_composition_plan** plan);

LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_plan_view_get(
    const laplace_decomposition_composition_plan* plan,
    laplace_decomposition_composition_plan_view* view);

LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_identity_evaluate(
    const laplace_decomposition_composition_plan* plan,
    laplace_decomposition_composition_identity* results,
    size_t result_capacity,
    size_t* result_count);

LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_identity_evaluate_view(
    const laplace_decomposition_composition_plan_view* view,
    laplace_decomposition_composition_identity* results,
    size_t result_capacity,
    size_t* result_count);

LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_identity_resolve(
    const laplace_decomposition_composition_plan* plan,
    const laplace_decomposition_composition_identity* results,
    size_t result_count,
    const laplace_composition_operand* reference,
    laplace_decomposition_composition_identity* identity);

LAPLACE_API laplace_decomposition_composition_status
laplace_decomposition_composition_identity_resolve_view(
    const laplace_decomposition_composition_plan_view* view,
    const laplace_decomposition_composition_identity* results,
    size_t result_count,
    const laplace_composition_operand* reference,
    laplace_decomposition_composition_identity* identity);

LAPLACE_API void laplace_decomposition_composition_plan_destroy(
    laplace_decomposition_composition_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
