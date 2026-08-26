#ifndef LAPLACE_UNICODE_ROOT_BUILDER_H
#define LAPLACE_UNICODE_ROOT_BUILDER_H

#include <stdint.h>

#include "laplace/export.h"
#include "laplace/spool.h"
#include "laplace/unicode_root.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_UNICODE_ROOT_BUILDER_ABI_MAJOR = 1,
    LAPLACE_UNICODE_ROOT_BUILDER_ABI_MINOR = 0
};

typedef enum laplace_unicode_root_build_status {
    LAPLACE_UNICODE_ROOT_BUILD_OK = 0,
    LAPLACE_UNICODE_ROOT_BUILD_INVALID_ARGUMENT = 1,
    LAPLACE_UNICODE_ROOT_BUILD_MEMORY_FAILURE = 2,
    LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE = 3,
    LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE = 4,
    LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE = 5
} laplace_unicode_root_build_status;

typedef enum laplace_unicode_root_build_stage {
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NONE = 0,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_SOURCE = 1,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_CORE = 2,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET = 3,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_PLACEMENT = 4,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC = 5,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS = 6,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_POSITIONS = 7,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_CONTRACTIONS = 8,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS = 9,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_MANIFEST = 10,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_VALIDATION = 11,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_SPOOL_SEAL = 12,
    LAPLACE_UNICODE_ROOT_BUILD_STAGE_COMPLETE = 13
} laplace_unicode_root_build_stage;

typedef struct laplace_unicode_root_build_request {
    const char* source_root;
    const char* spool_directory;
    const laplace_unicode_numeric_provider_v1* numeric_provider;
    uint64_t maximum_batch_bytes;
    uint32_t maximum_batch_frames;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_unicode_root_build_request;

typedef struct laplace_unicode_root_build_summary {
    laplace_digest256 receipt_id;
    laplace_unicode_source_receipt source;
    laplace_unicode_core_summary core;
    laplace_unicode_ducet_summary ducet;
    laplace_unicode_placement_summary placement;
    laplace_unicode_numeric_receipt numeric;
    laplace_unicode_root_stream_summary stream;
    laplace_canonical_spool_summary spool;
    uint64_t maximum_batch_bytes;
    uint32_t maximum_batch_frames;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t stage;
    uint32_t unicode_status;
    uint32_t spool_status;
    uint32_t status;
} laplace_unicode_root_build_summary;

LAPLACE_API laplace_unicode_root_build_status
laplace_unicode_root_build_canonical_spool(
    const laplace_unicode_root_build_request* request,
    laplace_canonical_spool** spool,
    laplace_unicode_root_build_summary* summary);

#ifdef __cplusplus
}
#endif

#endif
