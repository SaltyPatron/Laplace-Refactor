#include "laplace/framework.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake3.h"

static const uint8_t CONTEXT_DOMAIN[] = "laplace-framework-context-v1";
static const uint8_t STREAM_DOMAIN[] = "laplace-framework-canonical-stream-v1";
static const uint8_t SINK_DOMAIN[] = "laplace-framework-sink-artifacts-v1";
static const uint8_t RECEIPT_DOMAIN[] = "laplace-framework-stream-receipt-v1";
static const uint8_t PRODUCER_PLAN_DOMAIN[] =
    "laplace-framework-producer-plan-v1";
static const uint8_t PRODUCER_PREFIX_DOMAIN[] =
    "laplace-framework-producer-prefix-v1";
static const uint8_t PRODUCER_CHECKPOINT_DOMAIN[] =
    "laplace-framework-producer-checkpoint-v1";
static const uint8_t PRODUCER_RECEIPT_DOMAIN[] =
    "laplace-framework-producer-receipt-v1";
static const uint8_t ACTIVATION_REQUEST_DOMAIN[] =
    "laplace-framework-activation-request-v1";
static const uint8_t ACTIVATION_RECEIPT_DOMAIN[] =
    "laplace-framework-activation-receipt-v1";

#define OPERATION_DESCRIPTOR(symbol, handler, opcode_value, version_value, minor_value, input_value, output_value, module_value) \
    {opcode_value, module_value, input_value, output_value, version_value, minor_value, UINT32_C(0)},

static const laplace_framework_operation_descriptor OPERATIONS[] = {
    LAPLACE_ISA_OPERATION_REGISTRY(OPERATION_DESCRIPTOR)
};

#undef OPERATION_DESCRIPTOR

static void hash_u16(blake3_hasher* hasher, uint16_t value) {
    const uint8_t bytes[2] = {
        (uint8_t)(value & UINT16_C(0xff)),
        (uint8_t)(value >> 8)
    };
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value & UINT32_C(0xff)),
        (uint8_t)((value >> 8) & UINT32_C(0xff)),
        (uint8_t)((value >> 16) & UINT32_C(0xff)),
        (uint8_t)(value >> 24)
    };
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void finish_digest(blake3_hasher* hasher, laplace_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

static void finish_digest_copy(
    const blake3_hasher* hasher,
    laplace_digest256* digest) {
    blake3_hasher copy = *hasher;
    finish_digest(&copy, digest);
}

static int digest_has_canonical_zero_payload(const laplace_digest256* digest) {
    uint8_t aggregate = 0;
    size_t index;
    for (index = 0; index < sizeof(digest->bytes); ++index) {
        aggregate = (uint8_t)(aggregate | digest->bytes[index]);
    }
    return aggregate == 0;
}

static int digest_equal(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

size_t laplace_framework_operation_count(void) {
    return sizeof(OPERATIONS) / sizeof(OPERATIONS[0]);
}

const laplace_framework_operation_descriptor* laplace_framework_operations(void) {
    return OPERATIONS;
}

const laplace_framework_operation_descriptor* laplace_framework_operation_find(
    uint32_t opcode) {
    size_t low = 0;
    size_t high = laplace_framework_operation_count();
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        if (OPERATIONS[middle].opcode < opcode) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low < laplace_framework_operation_count() &&
        OPERATIONS[low].opcode == opcode) {
        return &OPERATIONS[low];
    }
    return NULL;
}

laplace_framework_status laplace_framework_registry_validate(void) {
    size_t index;
    if (laplace_framework_operation_count() != LAPLACE_ISA_OPERATION_COUNT) {
        return LAPLACE_FRAMEWORK_REGISTRY_INVALID;
    }
    for (index = 0; index < laplace_framework_operation_count(); ++index) {
        const laplace_framework_operation_descriptor* operation = &OPERATIONS[index];
        if (operation->opcode == 0 || operation->module_id == 0 ||
            operation->input_type == 0 || operation->output_type == 0 ||
            operation->instruction_version == 0 || operation->flags != 0 ||
            (index != 0 && OPERATIONS[index - 1u].opcode >= operation->opcode)) {
            return LAPLACE_FRAMEWORK_REGISTRY_INVALID;
        }
    }
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_context_validate(
    const laplace_framework_context* context) {
    const uint64_t known_epoch_mask =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_COUNT) - UINT64_C(1);
    size_t index;
    if (context == NULL) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    if (context->major != LAPLACE_FRAMEWORK_MAJOR ||
        context->minor > LAPLACE_FRAMEWORK_MINOR) {
        return LAPLACE_FRAMEWORK_UNSUPPORTED_VERSION;
    }
    if ((context->flags & ~LAPLACE_FRAMEWORK_KNOWN_CONTEXT_FLAGS) != 0 ||
        context->reserved != 0 ||
        (context->epoch_mask & ~known_epoch_mask) != 0 ||
        context->epoch_mask == 0 ||
        context->resource_grant.memory_bytes == 0 ||
        context->resource_grant.cpu_slots == 0) {
        return LAPLACE_FRAMEWORK_CONTEXT_INVALID;
    }
    for (index = 0; index < LAPLACE_FRAMEWORK_EPOCH_COUNT; ++index) {
        const int present =
            (context->epoch_mask & (UINT64_C(1) << index)) != 0;
        if (!present &&
            !digest_has_canonical_zero_payload(&context->epochs[index])) {
            return LAPLACE_FRAMEWORK_CONTEXT_INVALID;
        }
#if defined(LAPLACE_TEST_REJECT_ZERO_CONTEXT_VALUES)
        if (present &&
            digest_has_canonical_zero_payload(&context->epochs[index])) {
            return LAPLACE_FRAMEWORK_CONTEXT_INVALID;
        }
#endif
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_CONTEXT_VALUES)
    if (digest_has_canonical_zero_payload(&context->authority_fingerprint)) {
        return LAPLACE_FRAMEWORK_CONTEXT_INVALID;
    }
#endif
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_context_fingerprint(
    const laplace_framework_context* context,
    laplace_digest256* fingerprint) {
    blake3_hasher hasher;
    size_t index;
    laplace_framework_status status;
    if (fingerprint == NULL) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    status = laplace_framework_context_validate(context);
    if (status != LAPLACE_FRAMEWORK_OK) {
        memset(fingerprint, 0, sizeof(*fingerprint));
        return status;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, CONTEXT_DOMAIN, sizeof(CONTEXT_DOMAIN) - 1u);
    hash_u16(&hasher, context->major);
    hash_u16(&hasher, context->minor);
    hash_u32(&hasher, context->flags);
    hash_u64(&hasher, context->epoch_mask);
    for (index = 0; index < LAPLACE_FRAMEWORK_EPOCH_COUNT; ++index) {
        if ((context->epoch_mask & (UINT64_C(1) << index)) != 0) {
            hash_u32(&hasher, (uint32_t)index);
            blake3_hasher_update(
                &hasher, context->epochs[index].bytes,
                sizeof(context->epochs[index].bytes));
        }
    }
    blake3_hasher_update(
        &hasher, context->authority_fingerprint.bytes,
        sizeof(context->authority_fingerprint.bytes));
    hash_u64(&hasher, context->resource_grant.memory_bytes);
    hash_u32(&hasher, context->resource_grant.cpu_slots);
    hash_u32(&hasher, context->resource_grant.io_slots);
    finish_digest(&hasher, fingerprint);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_canonical_stream_fingerprint(
    const laplace_framework_canonical_batch* batches,
    size_t batch_count,
    laplace_digest256* fingerprint,
    uint32_t* record_type,
    uint64_t* total_records,
    uint64_t* total_bytes) {
    blake3_hasher hasher;
    uint64_t records = 0;
    uint64_t bytes = 0;
    uint32_t type;
    size_t index;
    if (batches == NULL || batch_count == 0 || fingerprint == NULL ||
        record_type == NULL || total_records == NULL || total_bytes == NULL) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    type = batches[0].record_type;
    if (type == 0) {
        return LAPLACE_FRAMEWORK_STREAM_INVALID;
    }
    for (index = 0; index < batch_count; ++index) {
        const laplace_framework_canonical_batch* batch = &batches[index];
        if (batch->canonical_bytes == NULL || batch->byte_count == 0 ||
            batch->record_count == 0 || batch->first_ordinal != records ||
            batch->record_type != type ||
            batch->flags != LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS ||
            UINT64_MAX - records < batch->record_count ||
            UINT64_MAX - bytes < batch->byte_count || batch->byte_count > SIZE_MAX) {
            return LAPLACE_FRAMEWORK_STREAM_INVALID;
        }
        records += batch->record_count;
        bytes += batch->byte_count;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, STREAM_DOMAIN, sizeof(STREAM_DOMAIN) - 1u);
    hash_u32(&hasher, type);
    hash_u64(&hasher, records);
    hash_u64(&hasher, bytes);
    for (index = 0; index < batch_count; ++index) {
        blake3_hasher_update(
            &hasher, batches[index].canonical_bytes,
            (size_t)batches[index].byte_count);
    }
    finish_digest(&hasher, fingerprint);
    *record_type = type;
    *total_records = records;
    *total_bytes = bytes;
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status validate_sinks(
    const laplace_framework_sink_v1* sinks,
    size_t sink_count,
    uint64_t* failed_sink_index) {
    size_t sink_index;
    if (sinks == NULL || sink_count == 0) {
        return LAPLACE_FRAMEWORK_SINK_INVALID;
    }
    for (sink_index = 0; sink_index < sink_count; ++sink_index) {
        const laplace_framework_sink_v1* sink = &sinks[sink_index];
        if (sink->begin == NULL || sink->stage == NULL || sink->seal == NULL ||
            sink->abort == NULL ||
            sink->abi_major != LAPLACE_FRAMEWORK_SINK_ABI_MAJOR ||
            sink->abi_minor > LAPLACE_FRAMEWORK_SINK_ABI_MINOR ||
            sink->flags != 0 || sink->reserved != 0) {
            if (failed_sink_index != NULL) {
                *failed_sink_index = (uint64_t)sink_index;
            }
            return LAPLACE_FRAMEWORK_SINK_INVALID;
        }
    }
    return LAPLACE_FRAMEWORK_OK;
}

static void abort_sinks(laplace_framework_sink_v1* sinks, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (sinks[index].abort != NULL) {
            sinks[index].abort(sinks[index].state);
        }
    }
}

static laplace_framework_status begin_sinks(
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    const laplace_framework_context* context,
    uint32_t record_type,
    uint64_t total_records,
    uint64_t total_bytes,
    size_t* begun,
    uint64_t* failed_sink_index) {
    size_t sink_index;
    *begun = 0;
    for (sink_index = 0; sink_index < sink_count; ++sink_index) {
        const laplace_framework_status status = sinks[sink_index].begin(
            sinks[sink_index].state, context, record_type,
            total_records, total_bytes);
        *begun = sink_index + 1u;
        if (status != LAPLACE_FRAMEWORK_OK) {
            *failed_sink_index = (uint64_t)sink_index;
            return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
        }
    }
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status stage_batch_to_sinks(
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    const laplace_framework_canonical_batch* batch,
    uint64_t batch_index,
    uint64_t* failed_batch_index,
    uint64_t* failed_sink_index) {
    size_t sink_index;
    for (sink_index = 0; sink_index < sink_count; ++sink_index) {
        const laplace_framework_status status = sinks[sink_index].stage(
            sinks[sink_index].state, batch);
        if (status != LAPLACE_FRAMEWORK_OK) {
            *failed_batch_index = batch_index;
            *failed_sink_index = (uint64_t)sink_index;
            return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        }
    }
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status seal_sinks(
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifacts_fingerprint,
    uint64_t* failed_sink_index) {
    blake3_hasher artifact_hasher;
    size_t sink_index;
    blake3_hasher_init(&artifact_hasher);
    blake3_hasher_update(
        &artifact_hasher, SINK_DOMAIN, sizeof(SINK_DOMAIN) - 1u);
    hash_u64(&artifact_hasher, (uint64_t)sink_count);
    for (sink_index = 0; sink_index < sink_count; ++sink_index) {
        laplace_digest256 artifact;
        laplace_framework_status status;
        memset(&artifact, 0, sizeof(artifact));
        status = sinks[sink_index].seal(
            sinks[sink_index].state, stream_fingerprint, &artifact);
        if (status != LAPLACE_FRAMEWORK_OK) {
            *failed_sink_index = (uint64_t)sink_index;
            return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
        }
#if defined(LAPLACE_TEST_REJECT_ZERO_SINK_VALUES)
        if (digest_has_canonical_zero_payload(&artifact)) {
            *failed_sink_index = (uint64_t)sink_index;
            return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
        }
#endif
        hash_u64(&artifact_hasher, (uint64_t)sink_index);
        blake3_hasher_update(
            &artifact_hasher, artifact.bytes, sizeof(artifact.bytes));
    }
    finish_digest(&artifact_hasher, artifacts_fingerprint);
    return LAPLACE_FRAMEWORK_OK;
}

static void hash_stream_receipt(laplace_framework_stream_receipt* receipt) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, RECEIPT_DOMAIN, sizeof(RECEIPT_DOMAIN) - 1u);
    hash_u32(&hasher, (uint32_t)receipt->status);
    hash_u32(&hasher, receipt->record_type);
    hash_u32(&hasher, receipt->effect_disposition);
    hash_u64(&hasher, receipt->total_records);
    hash_u64(&hasher, receipt->total_bytes);
    hash_u64(&hasher, receipt->batch_count);
    hash_u64(&hasher, receipt->sink_count);
    hash_u64(&hasher, receipt->failed_batch_index);
    hash_u64(&hasher, receipt->failed_sink_index);
    blake3_hasher_update(
        &hasher, receipt->context_fingerprint.bytes,
        sizeof(receipt->context_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->source_fingerprint.bytes,
        sizeof(receipt->source_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->recipe_fingerprint.bytes,
        sizeof(receipt->recipe_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->stream_fingerprint.bytes,
        sizeof(receipt->stream_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->sink_artifacts_fingerprint.bytes,
        sizeof(receipt->sink_artifacts_fingerprint.bytes));
    finish_digest(&hasher, &receipt->receipt_id);
}

static void initialize_stream_hasher(
    blake3_hasher* hasher,
    uint32_t record_type,
    uint64_t total_records,
    uint64_t total_bytes) {
    blake3_hasher_init(hasher);
    blake3_hasher_update(hasher, STREAM_DOMAIN, sizeof(STREAM_DOMAIN) - 1u);
    hash_u32(hasher, record_type);
    hash_u64(hasher, total_records);
    hash_u64(hasher, total_bytes);
}

static void hash_producer_plan(
    const laplace_digest256* context_fingerprint,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_plan* plan,
    laplace_digest256* plan_fingerprint) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, PRODUCER_PLAN_DOMAIN, sizeof(PRODUCER_PLAN_DOMAIN) - 1u);
    blake3_hasher_update(
        &hasher, context_fingerprint->bytes,
        sizeof(context_fingerprint->bytes));
    blake3_hasher_update(
        &hasher, source_fingerprint->bytes, sizeof(source_fingerprint->bytes));
    blake3_hasher_update(
        &hasher, recipe_fingerprint->bytes, sizeof(recipe_fingerprint->bytes));
    blake3_hasher_update(
        &hasher, plan->producer_fingerprint.bytes,
        sizeof(plan->producer_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, plan->initial_cursor_fingerprint.bytes,
        sizeof(plan->initial_cursor_fingerprint.bytes));
    hash_u64(&hasher, plan->batch_count);
    hash_u64(&hasher, plan->total_records);
    hash_u64(&hasher, plan->total_bytes);
    hash_u32(&hasher, plan->record_type);
    hash_u32(&hasher, plan->flags);
    finish_digest(&hasher, plan_fingerprint);
}

static void initialize_prefix_hasher(
    blake3_hasher* hasher,
    uint32_t record_type) {
    blake3_hasher_init(hasher);
    blake3_hasher_update(
        hasher, PRODUCER_PREFIX_DOMAIN, sizeof(PRODUCER_PREFIX_DOMAIN) - 1u);
    hash_u32(hasher, record_type);
}

static void update_prefix_hasher(
    blake3_hasher* hasher,
    uint64_t batch_index,
    const laplace_framework_canonical_batch* batch) {
    hash_u64(hasher, batch_index);
    hash_u64(hasher, batch->first_ordinal);
    hash_u64(hasher, batch->record_count);
    hash_u64(hasher, batch->byte_count);
    blake3_hasher_update(
        hasher, batch->canonical_bytes, (size_t)batch->byte_count);
}

static void hash_checkpoint(laplace_framework_replay_checkpoint* checkpoint) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, PRODUCER_CHECKPOINT_DOMAIN,
        sizeof(PRODUCER_CHECKPOINT_DOMAIN) - 1u);
    blake3_hasher_update(
        &hasher, checkpoint->context_fingerprint.bytes,
        sizeof(checkpoint->context_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, checkpoint->source_fingerprint.bytes,
        sizeof(checkpoint->source_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, checkpoint->recipe_fingerprint.bytes,
        sizeof(checkpoint->recipe_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, checkpoint->producer_fingerprint.bytes,
        sizeof(checkpoint->producer_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, checkpoint->plan_fingerprint.bytes,
        sizeof(checkpoint->plan_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, checkpoint->prefix_fingerprint.bytes,
        sizeof(checkpoint->prefix_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, checkpoint->cursor_fingerprint.bytes,
        sizeof(checkpoint->cursor_fingerprint.bytes));
    hash_u64(&hasher, checkpoint->completed_batches);
    hash_u64(&hasher, checkpoint->completed_records);
    hash_u64(&hasher, checkpoint->completed_bytes);
    hash_u64(&hasher, checkpoint->next_ordinal);
    hash_u32(&hasher, checkpoint->record_type);
    hash_u32(&hasher, checkpoint->flags);
    finish_digest(&hasher, &checkpoint->checkpoint_id);
}

static void set_checkpoint(
    laplace_framework_replay_checkpoint* checkpoint,
    const laplace_digest256* context_fingerprint,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_plan* plan,
    const laplace_digest256* plan_fingerprint,
    const laplace_digest256* prefix_fingerprint,
    const laplace_digest256* cursor_fingerprint,
    uint64_t completed_batches,
    uint64_t completed_records,
    uint64_t completed_bytes) {
    memset(checkpoint, 0, sizeof(*checkpoint));
    checkpoint->context_fingerprint = *context_fingerprint;
    checkpoint->source_fingerprint = *source_fingerprint;
    checkpoint->recipe_fingerprint = *recipe_fingerprint;
    checkpoint->producer_fingerprint = plan->producer_fingerprint;
    checkpoint->plan_fingerprint = *plan_fingerprint;
    checkpoint->prefix_fingerprint = *prefix_fingerprint;
    checkpoint->cursor_fingerprint = *cursor_fingerprint;
    checkpoint->completed_batches = completed_batches;
    checkpoint->completed_records = completed_records;
    checkpoint->completed_bytes = completed_bytes;
    checkpoint->next_ordinal = completed_records;
    checkpoint->record_type = plan->record_type;
    checkpoint->flags = LAPLACE_FRAMEWORK_KNOWN_REPLAY_FLAGS;
    hash_checkpoint(checkpoint);
}

static int checkpoint_checksum_is_valid(
    const laplace_framework_replay_checkpoint* checkpoint) {
    laplace_framework_replay_checkpoint expected;
    if (checkpoint == NULL || checkpoint->record_type == 0 ||
        checkpoint->flags != LAPLACE_FRAMEWORK_KNOWN_REPLAY_FLAGS ||
        checkpoint->reserved != 0 || checkpoint->next_ordinal !=
            checkpoint->completed_records) {
        return 0;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_PRODUCER_VALUES)
    if (digest_has_canonical_zero_payload(&checkpoint->checkpoint_id) ||
        digest_has_canonical_zero_payload(&checkpoint->context_fingerprint) ||
        digest_has_canonical_zero_payload(&checkpoint->source_fingerprint) ||
        digest_has_canonical_zero_payload(&checkpoint->recipe_fingerprint) ||
        digest_has_canonical_zero_payload(&checkpoint->producer_fingerprint) ||
        digest_has_canonical_zero_payload(&checkpoint->plan_fingerprint) ||
        digest_has_canonical_zero_payload(&checkpoint->prefix_fingerprint) ||
        digest_has_canonical_zero_payload(&checkpoint->cursor_fingerprint)) {
        return 0;
    }
#endif
    expected = *checkpoint;
    hash_checkpoint(&expected);
    return digest_equal(&checkpoint->checkpoint_id, &expected.checkpoint_id);
}

static void hash_producer_receipt(
    laplace_framework_producer_receipt* receipt) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, PRODUCER_RECEIPT_DOMAIN,
        sizeof(PRODUCER_RECEIPT_DOMAIN) - 1u);
    hash_u32(&hasher, (uint32_t)receipt->status);
    hash_u32(&hasher, receipt->replay_verified);
    hash_u64(&hasher, receipt->progress_events);
    blake3_hasher_update(
        &hasher, receipt->stream.receipt_id.bytes,
        sizeof(receipt->stream.receipt_id.bytes));
    blake3_hasher_update(
        &hasher, receipt->checkpoint.checkpoint_id.bytes,
        sizeof(receipt->checkpoint.checkpoint_id.bytes));
    blake3_hasher_update(
        &hasher, receipt->plan_fingerprint.bytes,
        sizeof(receipt->plan_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->completion_fingerprint.bytes,
        sizeof(receipt->completion_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->replay_checkpoint_id.bytes,
        sizeof(receipt->replay_checkpoint_id.bytes));
    finish_digest(&hasher, &receipt->receipt_id);
}

static int stream_receipt_is_valid(
    const laplace_framework_stream_receipt* receipt) {
    laplace_framework_stream_receipt expected;
    if (receipt == NULL || receipt->status != LAPLACE_FRAMEWORK_OK ||
        receipt->effect_disposition != LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT ||
        receipt->reserved != 0 || receipt->record_type == 0 ||
        receipt->total_records == 0 || receipt->total_bytes == 0 ||
        receipt->batch_count == 0 || receipt->sink_count == 0 ||
        receipt->failed_batch_index != LAPLACE_FRAMEWORK_NO_INDEX ||
        receipt->failed_sink_index != LAPLACE_FRAMEWORK_NO_INDEX) {
        return 0;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_STREAM_VALUES)
    if (digest_has_canonical_zero_payload(&receipt->receipt_id) ||
        digest_has_canonical_zero_payload(&receipt->context_fingerprint) ||
        digest_has_canonical_zero_payload(&receipt->source_fingerprint) ||
        digest_has_canonical_zero_payload(&receipt->recipe_fingerprint) ||
        digest_has_canonical_zero_payload(&receipt->stream_fingerprint) ||
        digest_has_canonical_zero_payload(&receipt->sink_artifacts_fingerprint)) {
        return 0;
    }
#endif
    expected = *receipt;
    hash_stream_receipt(&expected);
    return digest_equal(&receipt->receipt_id, &expected.receipt_id);
}

laplace_framework_status laplace_framework_stream_receipt_validate(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* receipt) {
    laplace_digest256 context_fingerprint;
    laplace_framework_status status = laplace_framework_context_fingerprint(
        context, &context_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        return status;
    }
    if (!stream_receipt_is_valid(receipt) ||
        !digest_equal(&context_fingerprint, &receipt->context_fingerprint)) {
        return LAPLACE_FRAMEWORK_STREAM_INVALID;
    }
    return LAPLACE_FRAMEWORK_OK;
}

static void hash_activation_request(
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    laplace_digest256* fingerprint) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, ACTIVATION_REQUEST_DOMAIN,
        sizeof(ACTIVATION_REQUEST_DOMAIN) - 1u);
    blake3_hasher_update(
        &hasher, staged_receipt->receipt_id.bytes,
        sizeof(staged_receipt->receipt_id.bytes));
    hash_u32(&hasher, request->epoch_slot);
    hash_u32(&hasher, request->flags);
    blake3_hasher_update(
        &hasher, request->expected_epoch.bytes,
        sizeof(request->expected_epoch.bytes));
    blake3_hasher_update(
        &hasher, request->next_epoch.bytes,
        sizeof(request->next_epoch.bytes));
    finish_digest(&hasher, fingerprint);
}

static void hash_activation_receipt(
    laplace_framework_activation_receipt* receipt) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, ACTIVATION_RECEIPT_DOMAIN,
        sizeof(ACTIVATION_RECEIPT_DOMAIN) - 1u);
    hash_u32(&hasher, (uint32_t)receipt->status);
    hash_u32(&hasher, receipt->epoch_slot);
    hash_u32(&hasher, receipt->effect_disposition);
    blake3_hasher_update(
        &hasher, receipt->context_fingerprint.bytes,
        sizeof(receipt->context_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->staged_receipt_id.bytes,
        sizeof(receipt->staged_receipt_id.bytes));
    blake3_hasher_update(
        &hasher, receipt->request_fingerprint.bytes,
        sizeof(receipt->request_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->preparation_fingerprint.bytes,
        sizeof(receipt->preparation_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->activation_fingerprint.bytes,
        sizeof(receipt->activation_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->expected_epoch.bytes,
        sizeof(receipt->expected_epoch.bytes));
    blake3_hasher_update(
        &hasher, receipt->next_epoch.bytes,
        sizeof(receipt->next_epoch.bytes));
    finish_digest(&hasher, &receipt->receipt_id);
}

laplace_framework_status laplace_framework_stage_canonical_stream(
    const laplace_framework_context* context,
    const laplace_framework_canonical_stream* stream,
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    laplace_framework_stream_receipt* receipt) {
    laplace_framework_status status;
    size_t batch_index;
    size_t batch_count;
    size_t begun = 0;
    if (receipt == NULL) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    memset(receipt, 0, sizeof(*receipt));
    receipt->failed_batch_index = LAPLACE_FRAMEWORK_NO_INDEX;
    receipt->failed_sink_index = LAPLACE_FRAMEWORK_NO_INDEX;
    receipt->sink_count = (uint64_t)sink_count;
    status = laplace_framework_context_fingerprint(
        context, &receipt->context_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        receipt->status = status;
        hash_stream_receipt(receipt);
        return status;
    }
    if (stream == NULL || stream->batches == NULL || stream->batch_count == 0 ||
        stream->batch_count > SIZE_MAX ||
        stream->flags != LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS ||
        stream->reserved != 0) {
        receipt->status = LAPLACE_FRAMEWORK_STREAM_INVALID;
        hash_stream_receipt(receipt);
        return receipt->status;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_STREAM_VALUES)
    if (digest_has_canonical_zero_payload(&stream->source_fingerprint) ||
        digest_has_canonical_zero_payload(&stream->recipe_fingerprint)) {
        receipt->status = LAPLACE_FRAMEWORK_STREAM_INVALID;
        hash_stream_receipt(receipt);
        return receipt->status;
    }
#endif
    batch_count = (size_t)stream->batch_count;
    receipt->batch_count = stream->batch_count;
#if !defined(LAPLACE_TEST_OMIT_STREAM_BINDING_FROM_RECEIPT)
    receipt->source_fingerprint = stream->source_fingerprint;
    receipt->recipe_fingerprint = stream->recipe_fingerprint;
#endif
    status = laplace_framework_canonical_stream_fingerprint(
        stream->batches, batch_count, &receipt->stream_fingerprint,
        &receipt->record_type, &receipt->total_records, &receipt->total_bytes);
    if (status != LAPLACE_FRAMEWORK_OK) {
        receipt->status = status;
        hash_stream_receipt(receipt);
        return status;
    }
    status = validate_sinks(sinks, sink_count, &receipt->failed_sink_index);
    if (status != LAPLACE_FRAMEWORK_OK) {
        receipt->status = status;
        hash_stream_receipt(receipt);
        return status;
    }
    status = begin_sinks(
        sinks, sink_count, context, receipt->record_type,
        receipt->total_records, receipt->total_bytes, &begun,
        &receipt->failed_sink_index);
    if (status != LAPLACE_FRAMEWORK_OK) {
        receipt->status = status;
        abort_sinks(sinks, begun);
        hash_stream_receipt(receipt);
        return status;
    }
    for (batch_index = 0; batch_index < batch_count; ++batch_index) {
        status = stage_batch_to_sinks(
            sinks, sink_count, &stream->batches[batch_index],
            (uint64_t)batch_index, &receipt->failed_batch_index,
            &receipt->failed_sink_index);
        if (status != LAPLACE_FRAMEWORK_OK) {
            receipt->status = status;
            abort_sinks(sinks, begun);
            hash_stream_receipt(receipt);
            return status;
        }
    }
    status = seal_sinks(
        sinks, sink_count, &receipt->stream_fingerprint,
        &receipt->sink_artifacts_fingerprint, &receipt->failed_sink_index);
    if (status != LAPLACE_FRAMEWORK_OK) {
        receipt->status = status;
        abort_sinks(sinks, begun);
        hash_stream_receipt(receipt);
        return status;
    }
    receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_stream_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_run_producer(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_v1* producer,
    const laplace_framework_producer_control_v1* control,
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    laplace_framework_producer_receipt* receipt) {
    blake3_hasher stream_hasher;
    blake3_hasher prefix_hasher;
    laplace_framework_producer_plan plan;
    laplace_digest256 prefix_fingerprint;
    laplace_framework_status status;
    uint64_t completed_records = 0;
    uint64_t completed_bytes = 0;
    uint64_t batch_index;
    size_t begun = 0;
    int producer_prepared = 0;
    int replay_verified = 0;
    if (receipt == NULL) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    memset(receipt, 0, sizeof(*receipt));
    memset(&plan, 0, sizeof(plan));
    receipt->stream.failed_batch_index = LAPLACE_FRAMEWORK_NO_INDEX;
    receipt->stream.failed_sink_index = LAPLACE_FRAMEWORK_NO_INDEX;
    receipt->stream.sink_count = (uint64_t)sink_count;
    status = laplace_framework_context_fingerprint(
        context, &receipt->stream.context_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        goto fail;
    }
    if (source_fingerprint == NULL || recipe_fingerprint == NULL) {
        status = LAPLACE_FRAMEWORK_PRODUCER_INVALID;
        goto fail;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_PRODUCER_VALUES)
    if (digest_has_canonical_zero_payload(source_fingerprint) ||
        digest_has_canonical_zero_payload(recipe_fingerprint)) {
        status = LAPLACE_FRAMEWORK_PRODUCER_INVALID;
        goto fail;
    }
#endif
    receipt->stream.source_fingerprint = *source_fingerprint;
    receipt->stream.recipe_fingerprint = *recipe_fingerprint;
    if (producer == NULL || producer->prepare == NULL ||
        producer->next == NULL || producer->finish == NULL ||
        producer->abort == NULL ||
        producer->abi_major != LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR ||
        producer->abi_minor > LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR ||
        producer->flags != LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS ||
        producer->reserved != 0 || control == NULL ||
        control->cancel_requested == NULL ||
        control->observe_progress == NULL ||
        control->abi_major != LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR ||
        control->abi_minor > LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR ||
        control->flags != LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS ||
        control->reserved != 0) {
        status = LAPLACE_FRAMEWORK_PRODUCER_INVALID;
        goto fail;
    }
    status = validate_sinks(
        sinks, sink_count, &receipt->stream.failed_sink_index);
    if (status != LAPLACE_FRAMEWORK_OK) {
        goto fail;
    }
    producer_prepared = 1;
    status = producer->prepare(
        producer->state, context, source_fingerprint, recipe_fingerprint,
        &plan);
    if (status != LAPLACE_FRAMEWORK_OK) {
        status = LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED;
        goto fail;
    }
    if (plan.batch_count == 0 || plan.total_records == 0 ||
        plan.total_bytes == 0 || plan.record_type == 0 ||
        plan.flags != LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS ||
        plan.reserved != 0) {
        status = LAPLACE_FRAMEWORK_PRODUCER_INVALID;
        goto fail;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_PRODUCER_VALUES)
    if (digest_has_canonical_zero_payload(&plan.producer_fingerprint) ||
        digest_has_canonical_zero_payload(&plan.initial_cursor_fingerprint)) {
        status = LAPLACE_FRAMEWORK_PRODUCER_INVALID;
        goto fail;
    }
#endif
    receipt->stream.batch_count = plan.batch_count;
    receipt->stream.total_records = plan.total_records;
    receipt->stream.total_bytes = plan.total_bytes;
    receipt->stream.record_type = plan.record_type;
    hash_producer_plan(
        &receipt->stream.context_fingerprint, source_fingerprint,
        recipe_fingerprint, &plan, &receipt->plan_fingerprint);
    initialize_stream_hasher(
        &stream_hasher, plan.record_type, plan.total_records, plan.total_bytes);
    initialize_prefix_hasher(&prefix_hasher, plan.record_type);
    finish_digest_copy(&prefix_hasher, &prefix_fingerprint);
    set_checkpoint(
        &receipt->checkpoint, &receipt->stream.context_fingerprint,
        source_fingerprint, recipe_fingerprint, &plan,
        &receipt->plan_fingerprint, &prefix_fingerprint,
        &plan.initial_cursor_fingerprint, 0, 0, 0);
    if (control->replay_checkpoint != NULL) {
        const laplace_framework_replay_checkpoint* replay =
            control->replay_checkpoint;
        receipt->replay_checkpoint_id = replay->checkpoint_id;
        if (!checkpoint_checksum_is_valid(replay) ||
            replay->completed_batches > plan.batch_count ||
            replay->completed_records > plan.total_records ||
            replay->completed_bytes > plan.total_bytes ||
            replay->record_type != plan.record_type ||
            !digest_equal(
                &replay->context_fingerprint,
                &receipt->stream.context_fingerprint) ||
            !digest_equal(&replay->source_fingerprint, source_fingerprint) ||
            !digest_equal(&replay->recipe_fingerprint, recipe_fingerprint) ||
            !digest_equal(
                &replay->producer_fingerprint, &plan.producer_fingerprint) ||
            !digest_equal(
                &replay->plan_fingerprint, &receipt->plan_fingerprint)) {
            status = LAPLACE_FRAMEWORK_REPLAY_INVALID;
            goto fail;
        }
        if (replay->completed_batches == 0) {
            if (!digest_equal(
                    &replay->checkpoint_id,
                    &receipt->checkpoint.checkpoint_id)) {
                status = LAPLACE_FRAMEWORK_REPLAY_MISMATCH;
                goto fail;
            }
            replay_verified = 1;
        }
    }
#if !defined(LAPLACE_TEST_IGNORE_PRODUCER_CANCELLATION)
    if (control->cancel_requested(control->state) != 0) {
        status = LAPLACE_FRAMEWORK_PRODUCER_CANCELLED;
        goto fail;
    }
#endif
    control->observe_progress(control->state, &receipt->checkpoint);
    receipt->progress_events = 1;
#if !defined(LAPLACE_TEST_IGNORE_PRODUCER_CANCELLATION)
    if (control->cancel_requested(control->state) != 0) {
        status = LAPLACE_FRAMEWORK_PRODUCER_CANCELLED;
        goto fail;
    }
#endif
    status = begin_sinks(
        sinks, sink_count, context, plan.record_type, plan.total_records,
        plan.total_bytes, &begun, &receipt->stream.failed_sink_index);
    if (status != LAPLACE_FRAMEWORK_OK) {
        goto fail;
    }
    for (batch_index = 0; batch_index < plan.batch_count; ++batch_index) {
        laplace_framework_canonical_batch batch;
        laplace_digest256 cursor_fingerprint;
        memset(&batch, 0, sizeof(batch));
        memset(&cursor_fingerprint, 0, sizeof(cursor_fingerprint));
#if !defined(LAPLACE_TEST_IGNORE_PRODUCER_CANCELLATION)
        if (control->cancel_requested(control->state) != 0) {
            status = LAPLACE_FRAMEWORK_PRODUCER_CANCELLED;
            goto fail;
        }
#endif
        status = producer->next(
            producer->state, batch_index, &batch, &cursor_fingerprint);
        if (status != LAPLACE_FRAMEWORK_OK) {
            receipt->stream.failed_batch_index = batch_index;
            status = LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
            goto fail;
        }
        if (batch.canonical_bytes == NULL || batch.byte_count == 0 ||
            batch.record_count == 0 ||
            batch.first_ordinal != completed_records ||
            batch.record_type != plan.record_type ||
            batch.flags != LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS ||
            batch.byte_count > SIZE_MAX ||
            UINT64_MAX - completed_records < batch.record_count ||
            UINT64_MAX - completed_bytes < batch.byte_count) {
            receipt->stream.failed_batch_index = batch_index;
            status = LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
            goto fail;
        }
#if defined(LAPLACE_TEST_REJECT_ZERO_PRODUCER_VALUES)
        if (digest_has_canonical_zero_payload(&cursor_fingerprint)) {
            receipt->stream.failed_batch_index = batch_index;
            status = LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
            goto fail;
        }
#endif
        completed_records += batch.record_count;
        completed_bytes += batch.byte_count;
        if (completed_records > plan.total_records ||
            completed_bytes > plan.total_bytes) {
            receipt->stream.failed_batch_index = batch_index;
            status = LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
            goto fail;
        }
        status = stage_batch_to_sinks(
            sinks, sink_count, &batch, batch_index,
            &receipt->stream.failed_batch_index,
            &receipt->stream.failed_sink_index);
        if (status != LAPLACE_FRAMEWORK_OK) {
            goto fail;
        }
        blake3_hasher_update(
            &stream_hasher, batch.canonical_bytes, (size_t)batch.byte_count);
        update_prefix_hasher(&prefix_hasher, batch_index, &batch);
        finish_digest_copy(&prefix_hasher, &prefix_fingerprint);
        set_checkpoint(
            &receipt->checkpoint, &receipt->stream.context_fingerprint,
            source_fingerprint, recipe_fingerprint, &plan,
            &receipt->plan_fingerprint, &prefix_fingerprint,
            &cursor_fingerprint, batch_index + 1u, completed_records,
            completed_bytes);
        if (control->replay_checkpoint != NULL &&
            control->replay_checkpoint->completed_batches ==
                batch_index + 1u) {
#if !defined(LAPLACE_TEST_OMIT_REPLAY_PREFIX_ASSERTION)
            if (!digest_equal(
                    &control->replay_checkpoint->checkpoint_id,
                    &receipt->checkpoint.checkpoint_id)) {
                status = LAPLACE_FRAMEWORK_REPLAY_MISMATCH;
                goto fail;
            }
#endif
            replay_verified = 1;
        }
        control->observe_progress(control->state, &receipt->checkpoint);
        ++receipt->progress_events;
    }
    if (completed_records != plan.total_records ||
        completed_bytes != plan.total_bytes ||
        (control->replay_checkpoint != NULL && replay_verified == 0)) {
        receipt->stream.failed_batch_index = plan.batch_count;
        status = LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
        goto fail;
    }
#if !defined(LAPLACE_TEST_IGNORE_PRODUCER_CANCELLATION)
    if (control->cancel_requested(control->state) != 0) {
        status = LAPLACE_FRAMEWORK_PRODUCER_CANCELLED;
        goto fail;
    }
#endif
    status = producer->finish(
        producer->state, &receipt->completion_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        status = LAPLACE_FRAMEWORK_PRODUCER_FINISH_FAILED;
        goto fail;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_PRODUCER_VALUES)
    if (digest_has_canonical_zero_payload(&receipt->completion_fingerprint)) {
        status = LAPLACE_FRAMEWORK_PRODUCER_FINISH_FAILED;
        goto fail;
    }
#endif
    finish_digest(&stream_hasher, &receipt->stream.stream_fingerprint);
    status = seal_sinks(
        sinks, sink_count, &receipt->stream.stream_fingerprint,
        &receipt->stream.sink_artifacts_fingerprint,
        &receipt->stream.failed_sink_index);
    if (status != LAPLACE_FRAMEWORK_OK) {
        goto fail;
    }
    receipt->stream.effect_disposition =
        LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
    receipt->stream.status = LAPLACE_FRAMEWORK_OK;
    hash_stream_receipt(&receipt->stream);
    receipt->replay_verified = (uint32_t)replay_verified;
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_producer_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;

fail:
    abort_sinks(sinks, begun);
    if (producer_prepared != 0) {
        producer->abort(producer->state);
    }
    receipt->stream.effect_disposition = LAPLACE_FRAMEWORK_EFFECT_NONE;
    receipt->stream.status = status;
    hash_stream_receipt(&receipt->stream);
    receipt->replay_verified = (uint32_t)replay_verified;
    receipt->status = status;
    hash_producer_receipt(receipt);
    return status;
}

static int activation_provider_is_valid(
    const laplace_framework_activation_provider_v1* provider) {
    return provider != NULL && provider->prepare != NULL &&
        provider->commit != NULL && provider->abort != NULL &&
        provider->abi_major == LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR &&
        provider->abi_minor <= LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR &&
        provider->flags == 0 && provider->reserved == 0;
}

static int admitted_receipt_is_valid(
    const laplace_framework_activation_receipt* receipt) {
    laplace_framework_activation_receipt expected;
    if (receipt == NULL || receipt->status != LAPLACE_FRAMEWORK_OK ||
        receipt->effect_disposition !=
            LAPLACE_FRAMEWORK_EFFECT_ACTIVATION_ADMITTED ||
        receipt->epoch_slot >= LAPLACE_FRAMEWORK_EPOCH_COUNT ||
        receipt->reserved != 0) {
        return 0;
    }
    expected = *receipt;
    hash_activation_receipt(&expected);
    return digest_equal(&receipt->receipt_id, &expected.receipt_id);
}

laplace_framework_status laplace_framework_admit_staged_stream(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt) {
    laplace_framework_status status;
    if (receipt == NULL) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    memset(receipt, 0, sizeof(*receipt));
    status = laplace_framework_context_fingerprint(
        context, &receipt->context_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        receipt->status = status;
        hash_activation_receipt(receipt);
        return status;
    }
#if defined(LAPLACE_TEST_SKIP_STAGED_RECEIPT_CONTEXT_BINDING)
    if (!stream_receipt_is_valid(staged_receipt) || request == NULL ||
#else
    if (laplace_framework_stream_receipt_validate(context, staged_receipt) !=
            LAPLACE_FRAMEWORK_OK ||
        request == NULL ||
#endif
        request->epoch_slot >= LAPLACE_FRAMEWORK_EPOCH_COUNT ||
        request->flags != LAPLACE_FRAMEWORK_KNOWN_ACTIVATION_FLAGS ||
        request->reserved != 0 ||
        digest_equal(&request->expected_epoch, &request->next_epoch)) {
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_ACTIVATION_VALUES)
    if (digest_has_canonical_zero_payload(&request->expected_epoch) ||
        digest_has_canonical_zero_payload(&request->next_epoch)) {
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
#endif
    receipt->staged_receipt_id = staged_receipt->receipt_id;
    receipt->expected_epoch = request->expected_epoch;
    receipt->next_epoch = request->next_epoch;
    receipt->epoch_slot = request->epoch_slot;
    hash_activation_request(
        staged_receipt, request, &receipt->request_fingerprint);
    if ((context->flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0) {
        receipt->status = LAPLACE_FRAMEWORK_EFFECT_NOT_AUTHORIZED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
    if ((context->epoch_mask &
         (UINT64_C(1) << request->epoch_slot)) == UINT64_C(0)
#if !defined(LAPLACE_TEST_ACTIVATE_WITHOUT_EXPECTED_EPOCH)
        || !digest_equal(
            &context->epochs[request->epoch_slot], &request->expected_epoch)
#endif
        ) {
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
    if (!activation_provider_is_valid(provider)) {
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
    status = provider->prepare(
        provider->state, context, staged_receipt, request,
        &receipt->preparation_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        provider->abort(
            provider->state, request, &receipt->preparation_fingerprint);
        receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_ACTIVATION_VALUES)
    if (digest_has_canonical_zero_payload(&receipt->preparation_fingerprint)) {
        provider->abort(
            provider->state, request, &receipt->preparation_fingerprint);
        receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
#endif
    receipt->effect_disposition =
        LAPLACE_FRAMEWORK_EFFECT_ACTIVATION_ADMITTED;
#if defined(LAPLACE_TEST_COMMIT_DURING_ACTIVATION_ADMISSION)
    status = provider->commit(
        provider->state, request, &receipt->preparation_fingerprint,
        &receipt->activation_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        provider->abort(
            provider->state, request, &receipt->preparation_fingerprint);
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
#endif
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_activation_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_commit_admitted_stream(
    const laplace_framework_context* context,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt) {
    laplace_digest256 context_fingerprint;
    laplace_framework_status status;
    if (context == NULL || request == NULL || receipt == NULL ||
        !activation_provider_is_valid(provider) ||
        laplace_framework_context_fingerprint(
            context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK ||
        !admitted_receipt_is_valid(receipt) ||
        !digest_equal(&receipt->context_fingerprint, &context_fingerprint) ||
        receipt->epoch_slot != request->epoch_slot ||
        !digest_equal(&receipt->expected_epoch, &request->expected_epoch) ||
        !digest_equal(&receipt->next_epoch, &request->next_epoch)) {
        return LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID;
    }
    status = provider->commit(
        provider->state, request, &receipt->preparation_fingerprint,
        &receipt->activation_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        provider->abort(
            provider->state, request, &receipt->preparation_fingerprint);
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
#if defined(LAPLACE_TEST_REJECT_ZERO_ACTIVATION_VALUES)
    if (digest_has_canonical_zero_payload(&receipt->activation_fingerprint)) {
        provider->abort(
            provider->state, request, &receipt->preparation_fingerprint);
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
#endif
    receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_ACTIVATED;
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_activation_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_abort_admitted_stream(
    const laplace_framework_context* context,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt) {
    laplace_digest256 context_fingerprint;
    if (context == NULL || request == NULL || receipt == NULL ||
        !activation_provider_is_valid(provider) ||
        laplace_framework_context_fingerprint(
            context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK ||
        !admitted_receipt_is_valid(receipt) ||
        !digest_equal(&receipt->context_fingerprint, &context_fingerprint) ||
        receipt->epoch_slot != request->epoch_slot ||
        !digest_equal(&receipt->expected_epoch, &request->expected_epoch) ||
        !digest_equal(&receipt->next_epoch, &request->next_epoch)) {
        return LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID;
    }
    provider->abort(
        provider->state, request, &receipt->preparation_fingerprint);
    memset(&receipt->activation_fingerprint, 0,
           sizeof(receipt->activation_fingerprint));
    receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_activation_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_activate_staged_stream(
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    const laplace_framework_activation_provider_v1* provider,
    laplace_framework_activation_receipt* receipt) {
    laplace_framework_status status = laplace_framework_admit_staged_stream(
        context, staged_receipt, request, provider, receipt);
    if (status != LAPLACE_FRAMEWORK_OK) {
        return status;
    }
    return laplace_framework_commit_admitted_stream(
        context, request, provider, receipt);
}
