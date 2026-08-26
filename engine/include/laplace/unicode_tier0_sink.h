#ifndef LAPLACE_UNICODE_TIER0_SINK_H
#define LAPLACE_UNICODE_TIER0_SINK_H

#include <stdint.h>

#include "laplace/export.h"
#include "laplace/framework.h"
#include "laplace/perfcache.h"
#include "laplace/unicode_root.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_UNICODE_TIER0_SINK_ABI_MAJOR = 2,
    LAPLACE_UNICODE_TIER0_SINK_ABI_MINOR = 0
};

typedef struct laplace_unicode_tier0_sink laplace_unicode_tier0_sink;

/*
 * One Unicode root stream produces a coherent direct/reverse perfcache bundle.
 * Neither artifact is an independent source of canonical Unicode state.
 */
typedef struct laplace_unicode_tier0_sink_configuration {
    const char* target_path;
    const char* reverse_target_path;
    laplace_unicode_root_stream_expectation root_expectation;
    laplace_id128 activation_epoch_id;
    laplace_digest256 activation_epoch_fingerprint;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_unicode_tier0_sink_configuration;

typedef struct laplace_unicode_tier0_sink_result {
    laplace_perfcache_contract contract;
    laplace_perfcache_contract reverse_contract;
    laplace_unicode_root_stream_summary root_summary;
    laplace_digest256 root_framework_stream_fingerprint;
    laplace_digest256 artifact_digest;
    laplace_digest256 reverse_artifact_digest;
    laplace_digest256 artifact_set_fingerprint;
    uint64_t root_frame_count;
    uint64_t root_encoded_bytes;
    uint64_t atom_count;
    uint64_t atom_metadata_bytes;
    uint64_t artifact_bytes;
    uint64_t reverse_artifact_bytes;
    uint32_t sealed;
    uint32_t reserved;
} laplace_unicode_tier0_sink_result;

/*
 * Creates a framework sink which consumes the complete canonical Unicode root
 * stream and projects only its atom frames into the dense Tier-0 execution
 * plane. The perfcache remains an inert derived artifact until separately
 * admitted and activated by the generation registry. A non-OK stage result poisons
 * that run; the framework aborts it and callers must not retry the callback directly.
 */
LAPLACE_API laplace_perfcache_status laplace_unicode_tier0_sink_create(
    const laplace_unicode_tier0_sink_configuration* configuration,
    laplace_unicode_tier0_sink** state,
    laplace_framework_sink_v1* sink);

LAPLACE_API laplace_perfcache_status laplace_unicode_tier0_sink_result_get(
    const laplace_unicode_tier0_sink* state,
    laplace_unicode_tier0_sink_result* result);

LAPLACE_API void laplace_unicode_tier0_sink_destroy(
    laplace_unicode_tier0_sink** state);

#ifdef __cplusplus
}
#endif

#endif
