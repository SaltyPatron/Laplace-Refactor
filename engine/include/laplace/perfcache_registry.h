#ifndef LAPLACE_PERFCACHE_REGISTRY_H
#define LAPLACE_PERFCACHE_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/framework.h"
#include "laplace/perfcache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_perfcache_registry laplace_perfcache_registry;
typedef struct laplace_perfcache_prepared_generation
    laplace_perfcache_prepared_generation;
typedef struct laplace_perfcache_activation laplace_perfcache_activation;

typedef struct laplace_perfcache_artifact_handle {
    laplace_perfcache_view view;
    laplace_digest256 loaded_identity;
    void* provider_handle;
} laplace_perfcache_artifact_handle;

typedef laplace_perfcache_status (*laplace_perfcache_artifact_open_fn)(
    void* state,
    const char* path,
    const laplace_perfcache_contract* expected_contract,
    const laplace_digest256* expected_artifact_digest,
    laplace_perfcache_record_validator validator,
    void* validator_context,
    uint64_t* invalid_record_index,
    laplace_perfcache_artifact_handle* handle);

typedef laplace_perfcache_status (*laplace_perfcache_artifact_prefault_fn)(
    void* state,
    laplace_perfcache_artifact_handle* handle,
    const laplace_execution_grant* resource_grant,
    uint64_t* touched_bytes,
    uint64_t* touched_pages);

typedef void (*laplace_perfcache_artifact_close_fn)(
    void* state,
    laplace_perfcache_artifact_handle* handle);

typedef struct laplace_perfcache_artifact_provider_v1 {
    void* state;
    laplace_perfcache_artifact_open_fn open;
    laplace_perfcache_artifact_prefault_fn prefault;
    laplace_perfcache_artifact_close_fn close;
    laplace_digest256 provider_fingerprint;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_perfcache_artifact_provider_v1;

typedef laplace_perfcache_status (*laplace_perfcache_module_lookup_fn)(
    void* state,
    const laplace_perfcache_view* view,
    const uint8_t* keys,
    size_t key_count,
    uint64_t* record_indexes,
    uint8_t* found);

typedef struct laplace_perfcache_module_v1 {
    laplace_id128 module_id;
    laplace_id128 key_schema_id;
    laplace_id128 value_schema_id;
    laplace_digest256 module_contract_fingerprint;
    laplace_perfcache_record_validator validate_record;
    laplace_perfcache_module_lookup_fn lookup_batch;
    void* state;
    uint32_t access_law;
    uint32_t key_bytes;
    uint32_t value_bytes;
    uint32_t flags;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t reserved;
} laplace_perfcache_module_v1;

typedef struct laplace_perfcache_generation_dependency {
    laplace_id128 module_id;
    laplace_digest256 artifact_digest;
} laplace_perfcache_generation_dependency;

typedef struct laplace_perfcache_generation_artifact {
    const char* path;
    laplace_perfcache_contract contract;
    laplace_digest256 expected_artifact_digest;
    const laplace_perfcache_generation_dependency* dependencies;
    size_t dependency_count;
    uint32_t flags;
    uint32_t reserved;
} laplace_perfcache_generation_artifact;

typedef struct laplace_perfcache_generation_request {
    const laplace_perfcache_generation_artifact* artifacts;
    size_t artifact_count;
    laplace_id128 activation_epoch_id;
    laplace_digest256 epoch_fingerprint;
    laplace_digest256 staged_receipt_id;
    laplace_digest256 stream_fingerprint;
    laplace_digest256 staged_sink_artifacts_fingerprint;
    laplace_digest256 sink_artifact_set_fingerprint;
    laplace_digest256 required_module_set_fingerprint;
    uint32_t flags;
    uint32_t reserved;
} laplace_perfcache_generation_request;

typedef struct laplace_perfcache_generation_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 context_fingerprint;
    laplace_id128 activation_epoch_id;
    laplace_digest256 epoch_fingerprint;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 manifest_fingerprint;
    laplace_digest256 artifact_set_fingerprint;
    laplace_digest256 loaded_objects_fingerprint;
    laplace_digest256 staged_receipt_id;
    laplace_digest256 stream_fingerprint;
    laplace_digest256 staged_sink_artifacts_fingerprint;
    laplace_digest256 sink_artifact_set_fingerprint;
    laplace_digest256 required_module_set_fingerprint;
    uint64_t artifact_count;
    uint64_t mapped_bytes;
    uint64_t prefaulted_bytes;
    uint64_t prefaulted_pages;
    uint64_t active_reader_count;
    uint32_t disposition;
    uint32_t status;
} laplace_perfcache_generation_receipt;

typedef struct laplace_perfcache_epoch {
    laplace_id128 activation_epoch_id;
    laplace_digest256 epoch_fingerprint;
} laplace_perfcache_epoch;

typedef struct laplace_perfcache_pin {
    void* generation;
    laplace_perfcache_registry* registry;
    laplace_perfcache_epoch epoch;
} laplace_perfcache_pin;

typedef struct laplace_perfcache_registry_snapshot {
    laplace_id128 active_activation_epoch_id;
    laplace_digest256 active_epoch_fingerprint;
    laplace_digest256 active_artifact_set_fingerprint;
    uint64_t active_artifact_count;
    uint64_t active_reader_count;
    uint64_t retired_generation_count;
    uint64_t retired_reader_count;
    uint32_t has_active_generation;
    uint32_t reserved;
} laplace_perfcache_registry_snapshot;

typedef enum laplace_perfcache_registry_status {
    LAPLACE_PERFCACHE_REGISTRY_OK = 0,
    LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT = 1,
    LAPLACE_PERFCACHE_REGISTRY_PROVIDER_INVALID = 2,
    LAPLACE_PERFCACHE_REGISTRY_MODULE_INVALID = 3,
    LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH = 4,
    LAPLACE_PERFCACHE_REGISTRY_ARTIFACT_OPEN_FAILED = 5,
    LAPLACE_PERFCACHE_REGISTRY_PREFAULT_FAILED = 6,
    LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH = 7,
    LAPLACE_PERFCACHE_REGISTRY_NO_ACTIVE_GENERATION = 8,
    LAPLACE_PERFCACHE_REGISTRY_MODULE_NOT_FOUND = 9,
    LAPLACE_PERFCACHE_REGISTRY_BUSY = 10,
    LAPLACE_PERFCACHE_REGISTRY_ALLOCATION_FAILED = 11,
    LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR = 12,
    LAPLACE_PERFCACHE_REGISTRY_MANIFEST_MISMATCH = 13,
    LAPLACE_PERFCACHE_REGISTRY_DEPENDENCY_INVALID = 14,
    LAPLACE_PERFCACHE_REGISTRY_ALREADY_RESERVED = 15,
    LAPLACE_PERFCACHE_REGISTRY_NOT_AUTHORIZED = 16
} laplace_perfcache_registry_status;

enum {
    LAPLACE_PERFCACHE_MODULE_REQUIRED = 1u,
    LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED = 1u
};

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_registry_create(
    const laplace_perfcache_module_v1* modules,
    size_t module_count,
    laplace_perfcache_registry** registry);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_dependency_fingerprint(
    const laplace_perfcache_generation_dependency* dependencies,
    size_t dependency_count,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_required_module_set_fingerprint(
    const laplace_perfcache_module_v1* modules,
    size_t module_count,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_generation_manifest_fingerprint(
    const laplace_perfcache_generation_request* request,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_generation_artifact_set_fingerprint(
    const laplace_perfcache_generation_request* request,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_registry_destroy(
    laplace_perfcache_registry* registry);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_registry_prepare(
    laplace_perfcache_registry* registry,
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_perfcache_artifact_provider_v1* provider,
    const laplace_perfcache_generation_request* request,
    laplace_perfcache_prepared_generation** prepared,
    laplace_perfcache_generation_receipt* receipt);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_activation_create(
    laplace_perfcache_registry* registry,
    laplace_perfcache_prepared_generation** prepared,
    uint32_t has_expected_epoch,
    const laplace_perfcache_epoch* expected_epoch,
    laplace_perfcache_activation** activation,
    laplace_framework_activation_provider_v1* provider);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_activation_receipt_get(
    const laplace_perfcache_activation* activation,
    laplace_perfcache_generation_receipt* receipt);

LAPLACE_API void laplace_perfcache_activation_destroy(
    laplace_perfcache_activation* activation);

LAPLACE_API void laplace_perfcache_registry_discard_prepared(
    laplace_perfcache_prepared_generation** prepared);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_registry_pin(
    laplace_perfcache_registry* registry,
    uint32_t has_expected_epoch,
    const laplace_perfcache_epoch* expected_epoch,
    laplace_perfcache_pin* pin);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_pin_lookup_batch(
    const laplace_perfcache_pin* pin,
    const laplace_id128* module_id,
    const uint8_t* keys,
    size_t key_count,
    uint64_t* record_indexes,
    uint8_t* found);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_pin_view(
    const laplace_perfcache_pin* pin,
    const laplace_id128* module_id,
    const laplace_perfcache_view** view);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_pin_release(
    laplace_perfcache_pin* pin);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_registry_collect(
    laplace_perfcache_registry* registry,
    uint64_t* collected_generation_count,
    uint64_t* collected_artifact_count,
    uint64_t* released_bytes);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_registry_snapshot_get(
    laplace_perfcache_registry* registry,
    laplace_perfcache_registry_snapshot* snapshot);

LAPLACE_API laplace_perfcache_registry_status laplace_perfcache_file_provider(
    laplace_perfcache_artifact_provider_v1* provider);

#ifdef __cplusplus
}
#endif

#endif
