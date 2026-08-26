#ifndef LAPLACE_PERFCACHE_H
#define LAPLACE_PERFCACHE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/perfcache.h"
#include "laplace/identity.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_perfcache_contract {
    laplace_id128 module_id;
    laplace_id128 key_schema_id;
    laplace_id128 value_schema_id;
    laplace_id128 activation_epoch_id;
    laplace_digest256 activation_epoch_fingerprint;
    laplace_digest256 module_contract_fingerprint;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 dependency_fingerprint;
    uint32_t key_bytes;
    uint32_t value_bytes;
    uint32_t access_law;
    uint64_t flags;
} laplace_perfcache_contract;

typedef struct laplace_perfcache_spec {
    laplace_perfcache_contract contract;
    const uint8_t* records;
    uint64_t record_count;
    const uint8_t* metadata;
    uint64_t metadata_bytes;
} laplace_perfcache_spec;

typedef struct laplace_perfcache_view {
    laplace_perfcache_contract contract;
    const uint8_t* artifact;
    size_t artifact_bytes;
    const uint8_t* records;
    uint64_t record_count;
    uint32_t record_stride;
    const uint8_t* metadata;
    uint64_t metadata_bytes;
    laplace_digest256 artifact_digest;
} laplace_perfcache_view;

typedef struct laplace_perfcache_mapping {
    laplace_perfcache_view view;
    const uint8_t* mapped_address;
    size_t mapped_bytes;
    intptr_t native_handle;
    uint64_t device_id;
    uint64_t file_id;
} laplace_perfcache_mapping;

typedef struct laplace_perfcache_file_builder laplace_perfcache_file_builder;

typedef enum laplace_perfcache_status {
    LAPLACE_PERFCACHE_OK = 0,
    LAPLACE_PERFCACHE_INVALID_ARGUMENT = 1,
    LAPLACE_PERFCACHE_SIZE_OVERFLOW = 2,
    LAPLACE_PERFCACHE_BUFFER_TOO_SMALL = 3,
    LAPLACE_PERFCACHE_BAD_MAGIC = 4,
    LAPLACE_PERFCACHE_UNSUPPORTED_VERSION = 5,
    LAPLACE_PERFCACHE_HEADER_INVALID = 6,
    LAPLACE_PERFCACHE_SECTION_INVALID = 7,
    LAPLACE_PERFCACHE_DIGEST_MISMATCH = 8,
    LAPLACE_PERFCACHE_CONTRACT_MISMATCH = 9,
    LAPLACE_PERFCACHE_KEYS_NOT_SORTED_UNIQUE = 10,
    LAPLACE_PERFCACHE_KEY_NOT_FOUND = 11,
    LAPLACE_PERFCACHE_SEMANTIC_MISMATCH = 12,
    LAPLACE_PERFCACHE_FILE_OPEN_FAILED = 13,
    LAPLACE_PERFCACHE_FILE_IO_FAILED = 14,
    LAPLACE_PERFCACHE_FILE_TYPE_INVALID = 15,
    LAPLACE_PERFCACHE_FILE_MAPPING_FAILED = 16,
    LAPLACE_PERFCACHE_FILE_SYNC_FAILED = 17,
    LAPLACE_PERFCACHE_FILE_RENAME_FAILED = 18,
    LAPLACE_PERFCACHE_DENSE_KEY_MISMATCH = 19,
    LAPLACE_PERFCACHE_LOOKUP_UNSUPPORTED = 20,
    LAPLACE_PERFCACHE_ARTIFACT_CONFLICT = 21
} laplace_perfcache_status;

typedef laplace_perfcache_status (*laplace_perfcache_record_validator)(
    void* context,
    uint64_t record_index,
    const uint8_t* record,
    uint32_t record_stride);

typedef laplace_perfcache_status (*laplace_perfcache_view_validator)(
    void* context,
    const laplace_perfcache_view* view,
    uint64_t* invalid_record_index);

LAPLACE_API laplace_perfcache_status laplace_perfcache_measure(
    const laplace_perfcache_spec* spec,
    size_t* artifact_bytes);

LAPLACE_API laplace_perfcache_status laplace_perfcache_layout_measure(
    const laplace_perfcache_contract* contract,
    uint64_t record_count,
    uint64_t metadata_bytes,
    size_t* artifact_bytes);

/*
 * Seals records and metadata already placed at their canonical offsets in one
 * caller-owned artifact buffer.  The header and terminal digest are written by
 * the canonical perfcache codec; no second records/metadata copy is performed.
 */
LAPLACE_API laplace_perfcache_status laplace_perfcache_layout_seal(
    const laplace_perfcache_contract* contract,
    uint64_t record_count,
    uint64_t metadata_bytes,
    uint8_t* artifact,
    size_t artifact_capacity,
    size_t* artifact_bytes,
    laplace_digest256* artifact_digest);

LAPLACE_API laplace_perfcache_status laplace_perfcache_write(
    const laplace_perfcache_spec* spec,
    uint8_t* artifact,
    size_t artifact_capacity,
    size_t* artifact_bytes);

LAPLACE_API laplace_perfcache_status laplace_perfcache_validate(
    const uint8_t* artifact,
    size_t artifact_bytes,
    const laplace_perfcache_contract* expected_contract,
    laplace_perfcache_view* view);

LAPLACE_API laplace_perfcache_status laplace_perfcache_lookup_batch(
    const laplace_perfcache_view* view,
    const uint8_t* keys,
    size_t key_count,
    uint64_t* record_indexes,
    uint8_t* found);

LAPLACE_API laplace_perfcache_status laplace_perfcache_validate_records(
    const laplace_perfcache_view* view,
    laplace_perfcache_record_validator validator,
    void* context,
    uint64_t* invalid_record_index);

LAPLACE_API laplace_perfcache_status laplace_perfcache_mapping_open(
    const char* path,
    const laplace_perfcache_contract* expected_contract,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    uint64_t* invalid_record_index,
    laplace_perfcache_mapping* mapping);

LAPLACE_API laplace_perfcache_status laplace_perfcache_mapping_close(
    laplace_perfcache_mapping* mapping);

LAPLACE_API laplace_perfcache_status laplace_perfcache_publish_file(
    const char* path,
    const uint8_t* artifact,
    size_t artifact_bytes,
    const laplace_perfcache_contract* expected_contract,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    uint64_t* invalid_record_index);

LAPLACE_API laplace_perfcache_status laplace_perfcache_file_builder_create(
    const char* path,
    const laplace_perfcache_contract* contract,
    uint64_t record_count,
    uint64_t maximum_metadata_bytes,
    laplace_perfcache_file_builder** builder);

LAPLACE_API laplace_perfcache_status laplace_perfcache_file_builder_append(
    laplace_perfcache_file_builder* builder,
    uint64_t first_record_index,
    const uint8_t* records,
    uint64_t record_count,
    const uint8_t* metadata,
    uint64_t metadata_bytes);

LAPLACE_API laplace_perfcache_status laplace_perfcache_file_builder_seal(
    laplace_perfcache_file_builder* builder,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    laplace_perfcache_view_validator view_validator,
    void* view_validator_context,
    uint64_t* invalid_record_index,
    size_t* artifact_bytes,
    laplace_digest256* artifact_digest);

LAPLACE_API void laplace_perfcache_file_builder_destroy(
    laplace_perfcache_file_builder** builder);

#ifdef __cplusplus
}
#endif

#endif
