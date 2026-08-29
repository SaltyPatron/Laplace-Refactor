#ifndef LAPLACE_DECOMPOSITION_UAX29_H
#define LAPLACE_DECOMPOSITION_UAX29_H

#include <stdint.h>

#include "laplace/decomposition.h"
#include "laplace/uax29.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAPLACE_DECOMPOSITION_KIND_UAX29_GRAPHEME UINT64_C(0x5541582900000001)
#define LAPLACE_DECOMPOSITION_KIND_UAX29_WORD UINT64_C(0x5541582900000002)
#define LAPLACE_DECOMPOSITION_KIND_UAX29_SENTENCE UINT64_C(0x5541582900000003)

typedef struct laplace_decomposition_uax29_provider {
    const laplace_uax29_tables* tables;
    laplace_decomposition_provider_v1 provider;
} laplace_decomposition_uax29_provider;

/* Caller owns both tables and storage; both must outlive decomposition_run. */
LAPLACE_API laplace_decomposition_status laplace_decomposition_uax29_provider_init(
    laplace_decomposition_uax29_provider* storage,
    const laplace_uax29_tables* tables,
    const laplace_digest256* provider_fingerprint);

#ifdef __cplusplus
}
#endif

#endif
