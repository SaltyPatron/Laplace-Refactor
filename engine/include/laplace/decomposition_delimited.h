#ifndef LAPLACE_DECOMPOSITION_DELIMITED_H
#define LAPLACE_DECOMPOSITION_DELIMITED_H

#include <stdint.h>

#include "laplace/decomposition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_decomposition_delimited_provider {
    uint64_t record_kind;
    uint64_t header_record_kind;
    uint64_t field_kind;
    uint64_t delimiter_kind;
    uint64_t terminator_kind;
    uint32_t delimiter;
    uint32_t terminator;
    uint32_t expected_column_count;
    uint32_t header_record_count;
    laplace_decomposition_provider_v1 provider;
} laplace_decomposition_delimited_provider;

enum {
    LAPLACE_DECOMPOSITION_DELIMITED_LF = 1,
    LAPLACE_DECOMPOSITION_DELIMITED_CRLF = 2
};

/*
 * Generic serialization grammar. It discovers records, fields, delimiters and
 * terminators; non-empty field spans are marked TEXT and redispatched so UAX29,
 * language, reference, and domain providers can continue decomposition. It is
 * intentionally not ISO/CILI/etc. specific.
 */
LAPLACE_API laplace_decomposition_status laplace_decomposition_delimited_provider_init(
    laplace_decomposition_delimited_provider* storage,
    uint32_t delimiter,
    uint32_t terminator,
    uint32_t expected_column_count,
    uint32_t header_record_count,
    uint64_t kind_base,
    const laplace_digest256* provider_fingerprint);

#ifdef __cplusplus
}
#endif

#endif
