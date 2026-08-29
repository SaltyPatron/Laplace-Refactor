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
    LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR = 1,
    LAPLACE_DECOMPOSITION_SPAN_REDISPATCH = 1u,
    LAPLACE_DECOMPOSITION_SPAN_TEXT = 2u,
    LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT = 4u
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

/*
 * Resolves the media type of an exact child span without rewriting the child
 * bytes. Returning a null pointer and zero byte count inherits the parent's
 * media type. Returning non-empty bytes retypes only the recursive dispatch
 * context, allowing e.g. HTML script content to be revisited as JavaScript or
 * a container member to be revisited under its own codec/grammar authority.
 * The engine copies the returned bytes before the callback returns control to
 * another provider, so resolver-owned storage need only survive the callback.
 */
typedef laplace_decomposition_status (*laplace_decomposition_media_type_resolver_fn)(
    void* resolver_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* parent_span,
    const laplace_digest256* provider_fingerprint,
    uint64_t child_byte_start,
    uint64_t child_byte_end,
    uint64_t child_kind,
    uint32_t child_flags,
    const char** media_type,
    uint64_t* media_type_byte_count);

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
 * recursively without a source-named dispatcher. TEXT marks textual bytes;
 * GRAMMAR_INPUT means a grammar is authorized to parse that exact range. The
 * flags describe applicability only and never rewrite or alias the bytes.
 * Provider+span execution is deduplicated, so redispatch converges instead of
 * becoming an infinite loop.
 */
LAPLACE_API laplace_decomposition_status laplace_decomposition_run(
    const laplace_decomposition_input* input,
    laplace_decomposition_result** result);

/*
 * Same recursive machine as laplace_decomposition_run, with an optional
 * recipe-owned media resolver. Media type participates in the execution key,
 * so an identical exact span may legitimately be revisited under distinct
 * grammar/codec authorities without collapsing those interpretations.
 */
LAPLACE_API laplace_decomposition_status laplace_decomposition_run_with_media_resolver(
    const laplace_decomposition_input* input,
    laplace_decomposition_media_type_resolver_fn resolver,
    void* resolver_state,
    laplace_decomposition_result** result);

LAPLACE_API const laplace_decomposition_span* laplace_decomposition_spans(
    const laplace_decomposition_result* result,
    size_t* span_count);

/*
 * Returns the exact media-type bytes used when dispatching one result span.
 * The returned storage belongs to result and remains valid until result is
 * destroyed. A null return with byte_count=0 means no media type was asserted.
 */
LAPLACE_API const char* laplace_decomposition_span_media_type(
    const laplace_decomposition_result* result,
    size_t span_index,
    size_t* byte_count);

LAPLACE_API laplace_decomposition_status laplace_decomposition_summary_get(
    const laplace_decomposition_result* result,
    laplace_decomposition_summary* summary);

LAPLACE_API void laplace_decomposition_result_destroy(
    laplace_decomposition_result** result);

#ifdef __cplusplus
}
#endif

#endif