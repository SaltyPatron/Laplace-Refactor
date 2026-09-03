#ifndef LAPLACE_DECOMPOSITION_FIXED_WIDTH_H
#define LAPLACE_DECOMPOSITION_FIXED_WIDTH_H

#include <stdint.h>

#include "laplace/decomposition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_decomposition_fixed_width_field {
    uint32_t width;
    uint32_t flags;
} laplace_decomposition_fixed_width_field;

enum {
    LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_LEFT = 1u,
    LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_RIGHT = 2u,
    LAPLACE_DECOMPOSITION_FIXED_WIDTH_KNOWN_FIELD_FLAGS = 3u,
    LAPLACE_DECOMPOSITION_FIXED_WIDTH_NO_OVERFLOW_FIELD = UINT32_MAX,
    LAPLACE_DECOMPOSITION_FIXED_WIDTH_LF = 1u,
    LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF = 2u
};

typedef struct laplace_decomposition_fixed_width_provider {
    const laplace_decomposition_fixed_width_field* fields;
    uint64_t record_kind;
    uint64_t header_record_kind;
    uint64_t field_kind;
    uint64_t value_kind;
    uint64_t terminator_kind;
    uint64_t overflow_kind;
    uint64_t nominal_record_bytes;
    uint32_t field_count;
    uint32_t header_record_count;
    uint32_t terminator;
    uint32_t padding_byte;
    uint32_t overflow_field_index;
    uint32_t maximum_overflow_bytes;
    laplace_decomposition_provider_v1 provider;
} laplace_decomposition_fixed_width_provider;

/*
 * Generic fixed-width serialization grammar. Every physical field is retained
 * as an exact structural span. A separately emitted, padding-trimmed value span
 * is textual content; padding therefore remains reconstructable without
 * becoming part of the semantic value. A recipe may name one field that
 * absorbs a bounded record-width overflow. The overflow bytes receive their
 * own witness span, so a malformed or ambiguous upstream row cannot be
 * silently shifted, truncated, or repaired.
 *
 * The field declaration storage is caller-owned and must outlive every use of
 * the initialized provider.
 */
LAPLACE_API laplace_decomposition_status
laplace_decomposition_fixed_width_provider_init(
    laplace_decomposition_fixed_width_provider* storage,
    const laplace_decomposition_fixed_width_field* fields,
    uint32_t field_count,
    uint32_t header_record_count,
    uint32_t terminator,
    uint32_t padding_byte,
    uint32_t overflow_field_index,
    uint32_t maximum_overflow_bytes,
    uint64_t kind_base,
    const laplace_digest256* provider_fingerprint);

#ifdef __cplusplus
}
#endif

#endif
