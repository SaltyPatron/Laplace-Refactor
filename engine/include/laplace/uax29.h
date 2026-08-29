#ifndef LAPLACE_UAX29_H
#define LAPLACE_UAX29_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/unicode_root.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_uax29_tables laplace_uax29_tables;

typedef enum laplace_uax29_status {
    LAPLACE_UAX29_OK = 0,
    LAPLACE_UAX29_INVALID_ARGUMENT = 1,
    LAPLACE_UAX29_SOURCE_INCOMPLETE = 2,
    LAPLACE_UAX29_SOURCE_SYNTAX_INVALID = 3,
    LAPLACE_UAX29_INVALID_UTF8 = 4,
    LAPLACE_UAX29_MEMORY_FAILURE = 5,
    LAPLACE_UAX29_EMIT_FAILURE = 6
} laplace_uax29_status;

typedef enum laplace_uax29_boundary_kind {
    LAPLACE_UAX29_GRAPHEME = 1,
    LAPLACE_UAX29_WORD = 2,
    LAPLACE_UAX29_SENTENCE = 3
} laplace_uax29_boundary_kind;

typedef struct laplace_uax29_span {
    uint64_t byte_start;
    uint64_t byte_end;
    uint64_t codepoint_start;
    uint64_t codepoint_end;
    uint8_t kind;
    uint8_t reserved[7];
} laplace_uax29_span;

typedef struct laplace_uax29_summary {
    uint64_t input_bytes;
    uint64_t codepoint_count;
    uint64_t grapheme_count;
    uint64_t word_count;
    uint64_t sentence_count;
} laplace_uax29_summary;

typedef int (*laplace_uax29_emit_fn)(
    void* state,
    const laplace_uax29_span* span);

/*
 * Builds UAX #29 property tables only from the already-verified Unicode source
 * bundle. No case folding, normalization, or language-specific rewriting is
 * performed here: exact source bytes remain exact source bytes ("King" is not
 * "king"). The tables consume GraphemeBreakProperty, WordBreakProperty,
 * SentenceBreakProperty, DerivedCoreProperties/InCB, and Extended_Pictographic.
 */
LAPLACE_API laplace_uax29_status laplace_uax29_tables_create(
    const laplace_unicode_source_bundle* bundle,
    laplace_uax29_tables** tables);

LAPLACE_API void laplace_uax29_tables_destroy(
    laplace_uax29_tables** tables);

/*
 * Emits boundaries over the exact UTF-8 byte sequence. Offsets always index the
 * caller's original bytes. The operation is segmentation, not canonicalization:
 * it never rewrites, lowercases, NFC-normalizes, or otherwise aliases content.
 */
LAPLACE_API laplace_uax29_status laplace_uax29_segment(
    const laplace_uax29_tables* tables,
    const uint8_t* utf8,
    size_t byte_count,
    laplace_uax29_boundary_kind kind,
    laplace_uax29_emit_fn emit,
    void* emit_state,
    laplace_uax29_summary* summary);

#ifdef __cplusplus
}
#endif

#endif
