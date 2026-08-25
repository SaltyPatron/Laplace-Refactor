#ifndef LAPLACE_SPOOL_H
#define LAPLACE_SPOOL_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/framework.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_canonical_spool laplace_canonical_spool;

typedef enum laplace_spool_status {
    LAPLACE_SPOOL_OK = 0,
    LAPLACE_SPOOL_INVALID_ARGUMENT = 1,
    LAPLACE_SPOOL_STATE_INVALID = 2,
    LAPLACE_SPOOL_BATCH_INVALID = 3,
    LAPLACE_SPOOL_SIZE_OVERFLOW = 4,
    LAPLACE_SPOOL_FILE_OPEN_FAILED = 5,
    LAPLACE_SPOOL_FILE_IO_FAILED = 6,
    LAPLACE_SPOOL_FILE_MAPPING_FAILED = 7,
    LAPLACE_SPOOL_MEMORY_FAILURE = 8
} laplace_spool_status;

typedef struct laplace_canonical_spool_summary {
    laplace_digest256 spool_fingerprint;
    laplace_digest256 stream_fingerprint;
    laplace_digest256 producer_fingerprint;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    uint64_t batch_count;
    uint64_t total_records;
    uint64_t total_bytes;
    uint32_t record_type;
    uint32_t status;
} laplace_canonical_spool_summary;

LAPLACE_API laplace_spool_status laplace_canonical_spool_create(
    const char* directory,
    uint32_t record_type,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    laplace_canonical_spool** spool);

LAPLACE_API laplace_spool_status laplace_canonical_spool_append(
    laplace_canonical_spool* spool,
    const laplace_framework_canonical_batch* batch);

LAPLACE_API laplace_spool_status laplace_canonical_spool_seal(
    laplace_canonical_spool* spool,
    laplace_canonical_spool_summary* summary);

LAPLACE_API laplace_spool_status laplace_canonical_spool_producer(
    laplace_canonical_spool* spool,
    laplace_framework_producer_v1* producer);

LAPLACE_API void laplace_canonical_spool_destroy(
    laplace_canonical_spool** spool);

#ifdef __cplusplus
}
#endif

#endif
