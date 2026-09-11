#ifndef LAPLACE_TEXT_IDENTITY_H
#define LAPLACE_TEXT_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/identity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_text_identity {
    laplace_id128 entity_id;
    laplace_digest256 identity_witness;
    uint64_t codepoint_count;
} laplace_text_identity;

typedef enum laplace_text_identity_status {
    LAPLACE_TEXT_IDENTITY_OK = 0,
    LAPLACE_TEXT_IDENTITY_INVALID_ARGUMENT = 1,
    LAPLACE_TEXT_IDENTITY_UTF8_INVALID = 2,
    LAPLACE_TEXT_IDENTITY_EMPTY = 3,
    LAPLACE_TEXT_IDENTITY_OVERFLOW = 4,
    LAPLACE_TEXT_IDENTITY_MEMORY_FAILURE = 5,
    LAPLACE_TEXT_IDENTITY_IDENTITY_FAILURE = 6
} laplace_text_identity_status;

/*
 * Resolve canonical Unicode scalar UTF-8 bytes to the same content identity law
 * used by decomposition/composition: each scalar maps through
 * laplace_identity_codepoint_witness and ordered content maps through
 * laplace_identity_composite_runs_witness. Source, language, syntax, trust and
 * occurrence are deliberately absent from identity.
 *
 * Empty text is rejected here because the current public identity contract has
 * no canonical empty-content entity. Callers must preserve empty-vs-absence in
 * their typed source/recipe layer until that contract is defined explicitly.
 */
LAPLACE_API laplace_text_identity_status laplace_text_identity_utf8(
    const uint8_t* bytes,
    size_t byte_count,
    laplace_text_identity* identity);

#ifdef __cplusplus
}
#endif

#endif
