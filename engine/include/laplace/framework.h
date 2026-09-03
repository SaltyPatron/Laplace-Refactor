#ifndef LAPLACE_FRAMEWORK_H
#define LAPLACE_FRAMEWORK_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/framework.h"
#include "laplace/contract/isa.h"
#include "laplace/execution.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAPLACE_FRAMEWORK_NO_INDEX UINT64_MAX

typedef struct laplace_framework_operation_descriptor {
    uint32_t opcode;
    uint32_t module_id;
    uint32_t input_type;
    uint32_t output_type;
    uint16_t instruction_version;
    uint16_t introduced_minor;
    uint32_t flags;
} laplace_framework_operation_descriptor;

typedef struct laplace_framework_context {
    laplace_digest256 epochs[LAPLACE_FRAMEWORK_EPOCH_COUNT];
    laplace_digest256 authority_fingerprint;
    laplace_execution_grant resource_grant;
    uint64_t epoch_mask;
    uint16_t major;
    uint16_t minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_framework_context;

typedef struct laplace_framework_canonical_batch {
    const uint8_t* canonical_bytes;
    uint64_t byte_count;
    uint64_t record_count;
    uint64_t first_ordinal;
    uint32_t record_type;
    uint32_t flags;
} laplace_framework_canonical_batch;

typedef struct laplace_framework_canonical_stream {
    const laplace_framework_canonical_batch* batches;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    uint64_t batch_count;
    uint32_t flags;
    uint32_t reserved;
} laplace_framework_canonical_stream;

typedef enum laplace_framework_status {
    LAPLACE_FRAMEWORK_OK = 0,
    LAPLACE_FRAMEWORK_INVALID_ARGUMENT = 1,
    LAPLACE_FRAMEWORK_UNSUPPORTED_VERSION = 2,
    LAPLACE_FRAMEWORK_CONTEXT_INVALID = 3,
    LAPLACE_FRAMEWORK_REGISTRY_INVALID = 4,
    LAPLACE_FRAMEWORK_STREAM_INVALID = 5,
    LAPLACE_FRAMEWORK_SINK_INVALID = 6,
    LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED = 7,
    LAPLACE_FRAMEWORK_SINK_STAGE_FAILED = 8,
    LAPLACE_FRAMEWORK_SINK_SEAL_FAILED = 9,
    LAPLACE_FRAMEWORK_OVERFLOW = 10,
    LAPLACE_FRAMEWORK_EFFECT_NOT_AUTHORIZED = 11,
    LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID = 12,
    LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID = 13,
    LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED = 14,
    LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED = 15,
    LAPLACE_FRAMEWORK_PRODUCER_INVALID = 16,
    LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED = 17,
    LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED = 18,
    LAPLACE_FRAMEWORK_PRODUCER_FINISH_FAILED = 19,
    LAPLACE_FRAMEWORK_PRODUCER_CANCELLED = 20,
    LAPLACE_FRAMEWORK_REPLAY_INVALID = 21,
    LAPLACE_FRAMEWORK_REPLAY_MISMATCH = 22
} laplace_framework_status;

typedef laplace_framework_status (*laplace_framework_sink_begin_fn)(
    void* state,
    const laplace_framework_context* context,
    uint32_t record_type,
    uint64_t total_records,
    uint64_t total_bytes);

typedef laplace_framework_status (*laplace_framework_sink_stage_fn)(
    void* state,
    const laplace_framework_canonical_batch* batch);

typedef laplace_framework_status (*laplace_framework_sink_seal_fn)(
    void* state,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint);

typedef void (*laplace_framework_sink_abort_fn)(void* state);

typedef struct laplace_framework_sink_v1 {
    void* state;
    laplace_framework_sink_begin_fn begin;
    laplace_framework_sink_stage_fn stage;
    laplace_framework_sink_seal_fn seal;
    laplace_framework_sink_abort_fn abort;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_framework_sink_v1;

typedef struct laplace_framework_sink_artifact_output {
    laplace_digest256* artifact_fingerprints;
    uint64_t capacity;
    uint64_t count;
    uint64_t reserved;
} laplace_framework_sink_artifact_output;

typedef struct laplace_framework_stream_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 stream_fingerprint;
    laplace_digest256 sink_artifacts_fingerprint;
    uint64_t total_records;
    uint64_t total_bytes;
    uint64_t batch_count;
    uint64_t sink_count;
    uint64_t failed_batch_index;
    uint64_t failed_sink_index;
    uint32_t record_type;
    uint32_t effect_disposition;
    laplace_framework_status status;
    uint32_t reserved;
} laplace_framework_stream_receipt;

typedef struct laplace_framework_producer_plan {
    laplace_digest256 producer_fingerprint;
    laplace_digest256 initial_cursor_fingerprint;
    uint64_t batch_count;
    uint64_t total_records;
    uint64_t total_bytes;
    uint32_t record_type;
    uint32_t flags;
    uint64_t reserved;
} laplace_framework_producer_plan;

typedef struct laplace_framework_replay_checkpoint {
    laplace_digest256 checkpoint_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 producer_fingerprint;
    laplace_digest256 plan_fingerprint;
    laplace_digest256 prefix_fingerprint;
    laplace_digest256 cursor_fingerprint;
    uint64_t completed_batches;
    uint64_t completed_records;
    uint64_t completed_bytes;
    uint64_t next_ordinal;
    uint32_t record_type;
    uint32_t flags;
    uint64_t reserved;
} laplace_framework_replay_checkpoint;

typedef laplace_framework_status (*laplace_framework_producer_prepare_fn)(
    void* state,
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    laplace_framework_producer_plan* plan);

typedef laplace_framework_status (*laplace_framework_producer_next_fn)(
    void* state,
    uint64_t batch_index,
    laplace_framework_canonical_batch* batch,
    laplace_digest256* cursor_fingerprint);

typedef laplace_framework_status (*laplace_framework_producer_finish_fn)(
    void* state,
    laplace_digest256* completion_fingerprint);

typedef void (*laplace_framework_producer_abort_fn)(void* state);

typedef struct laplace_framework_producer_v1 {
    void* state;
    laplace_framework_producer_prepare_fn prepare;
    laplace_framework_producer_next_fn next;
    laplace_framework_producer_finish_fn finish;
    laplace_framework_producer_abort_fn abort;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_framework_producer_v1;

typedef int (*laplace_framework_cancel_requested_fn)(void* state);

typedef void (*laplace_framework_progress_observer_fn)(
    void* state,
    const laplace_framework_replay_checkpoint* checkpoint);

typedef struct laplace_framework_producer_control_v1 {
    void* state;
    const laplace_framework_replay_checkpoint* replay_checkpoint;
    laplace_framework_cancel_requested_fn cancel_requested;
    laplace_framework_progress_observer_fn observe_progress;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_framework_producer_control_v1;

typedef struct laplace_framework_producer_receipt {
    laplace_digest256 receipt_id;
    laplace_framework_stream_receipt stream;
    laplace_framework_replay_checkpoint checkpoint;
    laplace_digest256 plan_fingerprint;
    laplace_digest256 completion_fingerprint;
    laplace_digest256 replay_checkpoint_id;
    uint64_t progress_events;
    laplace_framework_status status;
    uint32_t replay_verified;
    uint64_t reserved;
} laplace_framework_producer_receipt;

typedef struct laplace_framework_activation_request {
    laplace_digest256 expected_epoch;
    laplace_digest256 next_epoch;
    uint32_t epoch_slot;
    uint32_t flags;
    uint64_t reserved;
} laplace_framework_activation_request;

typedef laplace_framework_status (*laplace_framework_activation_prepare_fn)(
    void* state,
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    laplace_digest256* preparation_fingerprint);

typedef laplace_framework_status (*laplace_framework_activation_commit_fn)(
    void* state,
    const laplace_framework_activation_request* request,
    const laplace_digest256* preparation_fingerprint,
    laplace_digest256* activation_fingerprint);

typedef void (*laplace_framework_activation_abort_fn)(
    void* state,
    const laplace_framework_activation_request* request,
    const laplace_digest256* preparation_fingerprint);

typedef struct laplace_framework_activation_provider_v1 {
    void* state;
    laplace_framework_activation_prepare_fn prepare;
    laplace_framework_activation_commit_fn commit;
    laplace_framework_activation_abort_fn abort;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_framework_activation_provider_v1;

typedef struct laplace_framework_activation_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 staged_receipt_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 preparation_fingerprint;
    laplace_digest256 activation_fingerprint;
    laplace_digest256 expected_epoch;
    laplace_digest256 next_epoch;
    uint32_t epoch_slot;
    uint32_t effect_disposition;
    laplace_framework_status status;
    uint32_t reserved;
} laplace_framework_activation_receipt;

LAPLACE_API size_t laplace_framework_operation_count(void);

LAPLACE_API const laplace_framework_operation_descriptor*
laplace_framework_operations(void);

LAPLACE_API const laplace_framework_operation_descriptor*
laplace_framework_operation_find(uint32_t opcode);

LAPLACE_API laplace_framework_status laplace_framework_registry_validate(void);

LAPLACE_API laplace_framework_status laplace_framework_context_validate(
    const laplace_framework_context* context);

LAPLACE_API laplace_framework_status laplace_framework_context_fingerprint(
    const laplace_framework_context* context,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_framework_status laplace_framework_stream_receipt_validate(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* receipt);

LAPLACE_API laplace_framework_status laplace_framework_canonical_stream_fingerprint(
    const laplace_framework_canonical_batch* batches,
    size_t batch_count,
    laplace_digest256* fingerprint,
    uint32_t* record_type,
    uint64_t* total_records,
    uint64_t* total_bytes);

LAPLACE_API laplace_framework_status
laplace_framework_canonical_empty_stream_fingerprint(
    uint32_t record_type,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_framework_status
laplace_framework_sink_artifacts_fingerprint(
    const laplace_digest256* artifact_fingerprints,
    size_t artifact_count,
    laplace_digest256* aggregate_fingerprint);

LAPLACE_API laplace_framework_status laplace_framework_stage_canonical_stream(
    const laplace_framework_context* context,
    const laplace_framework_canonical_stream* stream,
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    laplace_framework_stream_receipt* receipt);

LAPLACE_API laplace_framework_status
laplace_framework_stage_canonical_stream_with_artifacts(
    const laplace_framework_context* context,
    const laplace_framework_canonical_stream* stream,
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    laplace_framework_sink_artifact_output* artifact_output,
    laplace_framework_stream_receipt* receipt);

LAPLACE_API laplace_framework_status laplace_framework_run_producer(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_v1* producer,
    const laplace_framework_producer_control_v1* control,
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    laplace_framework_producer_receipt* receipt);

LAPLACE_API laplace_framework_status
laplace_framework_run_producer_with_artifacts(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_v1* producer,
    const laplace_framework_producer_control_v1* control,
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    laplace_framework_sink_artifact_output* artifact_output,
    laplace_framework_producer_receipt* receipt);

LAPLACE_API laplace_framework_status laplace_framework_activate_staged_stream(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt);

LAPLACE_API laplace_framework_status laplace_framework_admit_staged_stream(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt);

LAPLACE_API laplace_framework_status laplace_framework_commit_admitted_stream(
    const laplace_framework_context* context,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt);

LAPLACE_API laplace_framework_status laplace_framework_admitted_stream_validate(
    const laplace_framework_context* context,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    const laplace_framework_activation_receipt* receipt);

LAPLACE_API laplace_framework_status laplace_framework_abort_admitted_stream(
    const laplace_framework_context* context,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
