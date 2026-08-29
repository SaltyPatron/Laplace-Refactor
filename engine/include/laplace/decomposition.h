#ifndef LAPLACE_DECOMPOSITION_H
#define LAPLACE_DECOMPOSITION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_decomposition_result laplace_decomposition_result;

typedef enum laplace_decomposition_status {
    LAPLACE_DECOMPOSITION_OK = 0,
    LAPLACE_DECOMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_DECOMPOSITION_PROVIDER_INVALID = 2,
    LAPLACE_DECOMPOSITION_PROVIDER_FAILURE = 3,
    LAPLACE_DECOMPOSITION_RANGE_INVALID = 4,
    LAPLACE_DECOMPOSITION_LIMIT_EXCEEDED = 5,
    LAPLACE_DECOMPOSITION_MEMORY_FAILURE = 6
} laplace_decomposition_status;

enum {
    LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR = 0,
    LAPLACE_DECOMPOSITION_SPAN_REDISPATCH = 1u
};

typedef struct laplace_decomposition_content {
    const uint8_t* bytes;
    uint64_t byte_count;
    const char* media_type;
    uint64_t media_type_byte_count;
    const char* name;
    uint64_t name_byte_count;
} laplace_decomposition_content;

typedef struct laplace_decomposition_span {
    uint64_t byte_start;
    uint64_t byte_end;
    uint64_t parent_span_index;
    laplace_digest256 provider_fingerprint;
    uint64_t kind;
    uint32_t depth;
    uint32_t flags;
} laplace_decomposition_span;

typedef int (*laplace_decomposition_emit_fn)(
    void* emit_state,
    uint64_t byte_start,
    uint64_t byte_end,
    uint64_t kind,
    uint32_t flags);

typedef laplace_decomposition_status (*laplace_decomposition_applicable_fn)(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    int* applicable);

typedef laplace_decomposition_status (*laplace_decomposition_apply_fn)(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state);

typedef struct laplace_decomposition_provider_v1 {
    void* state;
    laplace_digest256 provider_fingerprint;
    laplace_decomposition_applicable_fn applicable;
    laplace_decomposition_apply_fn apply;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_decomposition_provider_v1;

typedef struct laplace_decomposition_input {
    laplace_decomposition_content content;
    const laplace_decomposition_provider_v1* providers;
    uint64_t provider_count;
    uint64_t maximum_spans;
    uint32_t maximum_depth;
    uint32_t flags;
} laplace_decomposition_input;

typedef struct laplace_decomposition_summary {
    uint64_t span_count;
    uint64_t provider_execution_count;
    uint64_t applicable_execution_count;
    uint64_t redispatch_count;
    uint32_t maximum_depth_reached;
    uint32_t status;
} laplace_decomposition_summary;

/*
 * Applies every applicable provider to an exact content span. Newly exposed
 * spans can be redispatched to the OTHER providers, allowing container,
 * grammar, Unicode, language, chess, media, and future authorities to compose
 * recursively without a source-named dispatcher. Provider+span execution is
 * deduplicated, so redispatch converges instead of becoming an infinite loop.
 */
LAPLACE_API laplace_decomposition_status laplace_decomposition_run(
    const laplace_decomposition_input* input,
    laplace_decomposition_result** result);

LAPLACE_API const laplace_decomposition_span* laplace_decomposition_spans(
    const laplace_decomposition_result* result,
    size_t* span_count);

LAPLACE_API laplace_decomposition_status laplace_decomposition_summary_get(
    const laplace_decomposition_result* result,
    laplace_decomposition_summary* summary);

LAPLACE_API void laplace_decomposition_result_destroy(
    laplace_decomposition_result** result);

#ifdef __cplusplus
}
#endif

#endif
