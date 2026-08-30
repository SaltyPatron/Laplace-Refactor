#ifndef LAPLACE_TEXT_DECOMPOSITION_H
#define LAPLACE_TEXT_DECOMPOSITION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "laplace/export.h"
#include "laplace/uax29.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_text_decomposition_plan laplace_text_decomposition_plan;

typedef enum laplace_text_decomposition_status {
    LAPLACE_TEXT_DECOMPOSITION_OK = 0,
    LAPLACE_TEXT_DECOMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_TEXT_DECOMPOSITION_UTF8_INVALID = 2,
    LAPLACE_TEXT_DECOMPOSITION_UAX29_FAILURE = 3,
    LAPLACE_TEXT_DECOMPOSITION_MEMORY_FAILURE = 4,
    LAPLACE_TEXT_DECOMPOSITION_OVERFLOW = 5
} laplace_text_decomposition_status;

typedef struct laplace_text_decomposition_span_result {
    laplace_uax29_span span;
    uint64_t composition_result_index;
} laplace_text_decomposition_span_result;

typedef struct laplace_text_decomposition_plan_view {
    const uint32_t* atom_positions;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    const laplace_text_decomposition_span_result* spans;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t span_count;
    uint64_t grapheme_count;
    uint64_t word_count;
    uint64_t sentence_count;
} laplace_text_decomposition_plan_view;

typedef struct laplace_text_decomposition_input {
    const laplace_uax29_tables* uax29;
    const uint8_t* utf8;
    uint64_t byte_count;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    uint64_t first_source_ordinal;
    uint32_t recipe_version;
    uint32_t flags;
} laplace_text_decomposition_input;

/*
 * Builds one shared composition program for all three Unicode UAX #29 views.
 * Each grapheme/word/sentence is a composition over the SAME exact codepoint
 * atoms. Boundaries are not forced into a fake tree: overlapping authorities
 * remain independent views over identical leaves. Content identity therefore
 * remains exact while structural roles are carried separately by span records.
 */
LAPLACE_API laplace_text_decomposition_status laplace_text_decomposition_plan_create(
    const laplace_text_decomposition_input* input,
    laplace_text_decomposition_plan** plan);

LAPLACE_API laplace_text_decomposition_status laplace_text_decomposition_plan_view_get(
    const laplace_text_decomposition_plan* plan,
    laplace_text_decomposition_plan_view* view);

LAPLACE_API void laplace_text_decomposition_plan_destroy(
    laplace_text_decomposition_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
