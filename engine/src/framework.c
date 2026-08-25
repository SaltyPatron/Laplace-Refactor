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

static int digest_is_zero(const laplace_digest256* digest) {
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
        digest_is_zero(&context->authority_fingerprint) ||
        context->resource_grant.memory_bytes == 0 ||
        context->resource_grant.cpu_slots == 0) {
        return LAPLACE_FRAMEWORK_CONTEXT_INVALID;
    }
    for (index = 0; index < LAPLACE_FRAMEWORK_EPOCH_COUNT; ++index) {
        const int present =
            (context->epoch_mask & (UINT64_C(1) << index)) != 0;
        if (present == digest_is_zero(&context->epochs[index])) {
            return LAPLACE_FRAMEWORK_CONTEXT_INVALID;
        }
    }
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

static void abort_sinks(laplace_framework_sink_v1* sinks, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (sinks[index].abort != NULL) {
            sinks[index].abort(sinks[index].state);
        }
    }
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

static int stream_receipt_is_valid(
    const laplace_framework_stream_receipt* receipt) {
    laplace_framework_stream_receipt expected;
    if (receipt == NULL || receipt->status != LAPLACE_FRAMEWORK_OK ||
        receipt->effect_disposition != LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT ||
        receipt->reserved != 0 || receipt->record_type == 0 ||
        receipt->total_records == 0 || receipt->total_bytes == 0 ||
        receipt->batch_count == 0 || receipt->sink_count == 0 ||
        receipt->failed_batch_index != LAPLACE_FRAMEWORK_NO_INDEX ||
        receipt->failed_sink_index != LAPLACE_FRAMEWORK_NO_INDEX ||
        digest_is_zero(&receipt->receipt_id) ||
        digest_is_zero(&receipt->context_fingerprint) ||
        digest_is_zero(&receipt->source_fingerprint) ||
        digest_is_zero(&receipt->recipe_fingerprint) ||
        digest_is_zero(&receipt->stream_fingerprint) ||
        digest_is_zero(&receipt->sink_artifacts_fingerprint)) {
        return 0;
    }
    expected = *receipt;
    hash_stream_receipt(&expected);
    return digest_equal(&receipt->receipt_id, &expected.receipt_id);
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
    blake3_hasher artifact_hasher;
    laplace_framework_status status;
    size_t sink_index;
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
        stream->reserved != 0 || digest_is_zero(&stream->source_fingerprint) ||
        digest_is_zero(&stream->recipe_fingerprint)) {
        receipt->status = LAPLACE_FRAMEWORK_STREAM_INVALID;
        hash_stream_receipt(receipt);
        return receipt->status;
    }
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
    if (sinks == NULL || sink_count == 0) {
        receipt->status = LAPLACE_FRAMEWORK_SINK_INVALID;
        hash_stream_receipt(receipt);
        return receipt->status;
    }
    for (sink_index = 0; sink_index < sink_count; ++sink_index) {
        const laplace_framework_sink_v1* sink = &sinks[sink_index];
        if (sink->begin == NULL || sink->stage == NULL || sink->seal == NULL ||
            sink->abort == NULL || sink->abi_major != LAPLACE_FRAMEWORK_SINK_ABI_MAJOR ||
            sink->abi_minor > LAPLACE_FRAMEWORK_SINK_ABI_MINOR ||
            sink->flags != 0 || sink->reserved != 0) {
            receipt->status = LAPLACE_FRAMEWORK_SINK_INVALID;
            receipt->failed_sink_index = (uint64_t)sink_index;
            abort_sinks(sinks, begun);
            hash_stream_receipt(receipt);
            return receipt->status;
        }
        status = sink->begin(
            sink->state, context, receipt->record_type,
            receipt->total_records, receipt->total_bytes);
        if (status != LAPLACE_FRAMEWORK_OK) {
            receipt->status = LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
            receipt->failed_sink_index = (uint64_t)sink_index;
            abort_sinks(sinks, sink_index + 1u);
            hash_stream_receipt(receipt);
            return receipt->status;
        }
        begun = sink_index + 1u;
    }
    for (batch_index = 0; batch_index < batch_count; ++batch_index) {
        for (sink_index = 0; sink_index < sink_count; ++sink_index) {
            status = sinks[sink_index].stage(
                sinks[sink_index].state, &stream->batches[batch_index]);
            if (status != LAPLACE_FRAMEWORK_OK) {
                receipt->status = LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
                receipt->failed_batch_index = (uint64_t)batch_index;
                receipt->failed_sink_index = (uint64_t)sink_index;
                abort_sinks(sinks, begun);
                hash_stream_receipt(receipt);
                return receipt->status;
            }
        }
    }
    blake3_hasher_init(&artifact_hasher);
    blake3_hasher_update(
        &artifact_hasher, SINK_DOMAIN, sizeof(SINK_DOMAIN) - 1u);
    hash_u64(&artifact_hasher, (uint64_t)sink_count);
    for (sink_index = 0; sink_index < sink_count; ++sink_index) {
        laplace_digest256 artifact;
        memset(&artifact, 0, sizeof(artifact));
        status = sinks[sink_index].seal(
            sinks[sink_index].state, &receipt->stream_fingerprint,
            &artifact);
        if (status != LAPLACE_FRAMEWORK_OK || digest_is_zero(&artifact)) {
            receipt->status = LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
            receipt->failed_sink_index = (uint64_t)sink_index;
            abort_sinks(sinks, begun);
            hash_stream_receipt(receipt);
            return receipt->status;
        }
        hash_u64(&artifact_hasher, (uint64_t)sink_index);
        blake3_hasher_update(
            &artifact_hasher, artifact.bytes, sizeof(artifact.bytes));
    }
    finish_digest(&artifact_hasher, &receipt->sink_artifacts_fingerprint);
    receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_stream_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status laplace_framework_activate_staged_stream(
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
    if (!stream_receipt_is_valid(staged_receipt) || request == NULL ||
        request->epoch_slot >= LAPLACE_FRAMEWORK_EPOCH_COUNT ||
        request->flags != LAPLACE_FRAMEWORK_KNOWN_ACTIVATION_FLAGS ||
        request->reserved != 0 || digest_is_zero(&request->expected_epoch) ||
        digest_is_zero(&request->next_epoch) ||
        digest_equal(&request->expected_epoch, &request->next_epoch)) {
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
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
    if (provider == NULL || provider->prepare == NULL ||
        provider->commit == NULL || provider->abort == NULL ||
        provider->abi_major != LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR ||
        provider->abi_minor > LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR ||
        provider->flags != 0 || provider->reserved != 0) {
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
    status = provider->prepare(
        provider->state, context, staged_receipt, request,
        &receipt->preparation_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK ||
        digest_is_zero(&receipt->preparation_fingerprint)) {
        provider->abort(
            provider->state, request, &receipt->preparation_fingerprint);
        receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
    receipt->effect_disposition =
        LAPLACE_FRAMEWORK_EFFECT_ACTIVATION_ADMITTED;
    status = provider->commit(
        provider->state, request, &receipt->preparation_fingerprint,
        &receipt->activation_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK ||
        digest_is_zero(&receipt->activation_fingerprint)) {
        provider->abort(
            provider->state, request, &receipt->preparation_fingerprint);
        receipt->status = LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
        hash_activation_receipt(receipt);
        return receipt->status;
    }
    receipt->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_ACTIVATED;
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_activation_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;
}
