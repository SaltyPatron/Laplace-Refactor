#ifndef LAPLACE_IDENTITY_H
#define LAPLACE_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/identity.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_id128 {
    uint8_t bytes[LAPLACE_IDENTITY_BYTES];
} laplace_id128;

typedef struct laplace_id_run {
    laplace_id128 id;
    uint64_t count;
} laplace_id_run;

typedef enum laplace_identity_status {
    LAPLACE_IDENTITY_OK = 0,
    LAPLACE_IDENTITY_INVALID_ARGUMENT = 1,
    LAPLACE_IDENTITY_POSITION_OUT_OF_RANGE = 2,
    LAPLACE_IDENTITY_EMPTY_COMPOSITION = 3,
    LAPLACE_IDENTITY_ZERO_RUN = 4,
    LAPLACE_IDENTITY_COUNT_OVERFLOW = 5
} laplace_identity_status;

LAPLACE_API laplace_identity_status laplace_unicode_position_encode(
    uint32_t position,
    uint8_t out_bytes[4],
    size_t* out_length);

LAPLACE_API laplace_identity_status laplace_identity_codepoint(
    uint32_t position,
    laplace_id128* out_id);

LAPLACE_API laplace_identity_status laplace_identity_codepoint_batch(
    const uint32_t* positions,
    size_t position_count,
    laplace_id128* out_ids);

LAPLACE_API laplace_identity_status laplace_identity_composite(
    const laplace_id128* child_ids,
    size_t child_count,
    laplace_id128* out_id);

LAPLACE_API laplace_identity_status laplace_identity_composite_runs(
    const laplace_id_run* runs,
    size_t run_count,
    uint64_t* out_logical_count,
    laplace_id128* out_id);

LAPLACE_API int laplace_identity_equal(
    const laplace_id128* left,
    const laplace_id128* right);
LAPLACE_API int laplace_identity_compare(
    const laplace_id128* left,
    const laplace_id128* right);

#ifdef __cplusplus
}
#endif

#endif
