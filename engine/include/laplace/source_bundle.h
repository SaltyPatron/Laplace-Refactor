#ifndef LAPLACE_SOURCE_BUNDLE_H
#define LAPLACE_SOURCE_BUNDLE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_SOURCE_BUNDLE_VERSION = 1,
    LAPLACE_SOURCE_BUNDLE_ABI_MAJOR = 1,
    LAPLACE_SOURCE_BUNDLE_ABI_MINOR = 0
};

typedef enum laplace_source_bundle_status {
    LAPLACE_SOURCE_BUNDLE_OK = 0,
    LAPLACE_SOURCE_BUNDLE_INVALID_ARGUMENT = 1,
    LAPLACE_SOURCE_BUNDLE_ROOT_INVALID = 2,
    LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID = 3,
    LAPLACE_SOURCE_BUNDLE_DIGEST_MISMATCH = 4,
    LAPLACE_SOURCE_BUNDLE_VERSION_MISMATCH = 5,
    LAPLACE_SOURCE_BUNDLE_DUPLICATE_PATH = 6,
    LAPLACE_SOURCE_BUNDLE_OVERFLOW = 7,
    LAPLACE_SOURCE_BUNDLE_MEMORY_FAILURE = 8
} laplace_source_bundle_status;

/*
 * Exact source acquisition is a substrate mechanism, not a Unicode/lexical/
 * chess/media semantic owner. A source recipe supplies immutable artifact
 * expectations; this owner opens only beneath the selected root, rejects links
 * and traversal, validates exact byte counts and SHA-256, and retains the bytes
 * for downstream grammar/codec providers.
 *
 * `version_marker` is an optional exact byte substring required by the source
 * recipe. It is release validation only and never enters canonical content
 * identity. Paths, media families, provider names, and local roots likewise do
 * not become content identity here.
 */
typedef struct laplace_source_artifact_expectation {
    const char* relative_path;
    const char* version_marker;
    uint64_t expected_bytes;
    uint8_t expected_sha256[32];
} laplace_source_artifact_expectation;

typedef struct laplace_source_bundle_request {
    const char* source_root;
    const laplace_source_artifact_expectation* artifacts;
    uint64_t artifact_count;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_source_bundle_request;

typedef struct laplace_source_bundle_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 manifest_fingerprint;
    laplace_digest256 verified_file_set_fingerprint;
    uint64_t total_source_bytes;
    uint64_t verified_file_count;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t version;
    uint32_t status;
} laplace_source_bundle_receipt;

typedef struct laplace_source_file_view {
    const uint8_t* bytes;
    uint64_t byte_count;
    uint64_t artifact_index;
} laplace_source_file_view;

typedef struct laplace_source_bundle laplace_source_bundle;

LAPLACE_API laplace_source_bundle_status laplace_source_bundle_open(
    const laplace_source_bundle_request* request,
    laplace_source_bundle** bundle,
    laplace_source_bundle_receipt* receipt);

LAPLACE_API laplace_source_bundle_status laplace_source_bundle_file(
    const laplace_source_bundle* bundle,
    const char* relative_path,
    laplace_source_file_view* view);

LAPLACE_API laplace_source_bundle_status laplace_source_bundle_file_at(
    const laplace_source_bundle* bundle,
    uint64_t artifact_index,
    laplace_source_file_view* view);

LAPLACE_API laplace_source_bundle_status laplace_source_bundle_receipt_get(
    const laplace_source_bundle* bundle,
    laplace_source_bundle_receipt* receipt);

LAPLACE_API void laplace_source_bundle_close(
    laplace_source_bundle** bundle);

#ifdef __cplusplus
}
#endif

#endif
