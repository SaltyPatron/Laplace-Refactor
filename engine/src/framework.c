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
        &hasher, receipt->stream_fingerprint.bytes,
        sizeof(receipt->stream_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->sink_artifacts_fingerprint.bytes,
        sizeof(receipt->sink_artifacts_fingerprint.bytes));
    finish_digest(&hasher, &receipt->receipt_id);
}

laplace_framework_status laplace_framework_stage_canonical_stream(
    const laplace_framework_context* context,
    const laplace_framework_canonical_batch* batches,
    size_t batch_count,
    laplace_framework_sink_v1* sinks,
    size_t sink_count,
    laplace_framework_stream_receipt* receipt) {
    blake3_hasher artifact_hasher;
    laplace_framework_status status;
    size_t sink_index;
    size_t batch_index;
    size_t begun = 0;
    if (receipt == NULL) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    memset(receipt, 0, sizeof(*receipt));
    receipt->failed_batch_index = LAPLACE_FRAMEWORK_NO_INDEX;
    receipt->failed_sink_index = LAPLACE_FRAMEWORK_NO_INDEX;
    receipt->batch_count = (uint64_t)batch_count;
    receipt->sink_count = (uint64_t)sink_count;
    status = laplace_framework_context_fingerprint(
        context, &receipt->context_fingerprint);
    if (status != LAPLACE_FRAMEWORK_OK) {
        receipt->status = status;
        hash_stream_receipt(receipt);
        return status;
    }
    status = laplace_framework_canonical_stream_fingerprint(
        batches, batch_count, &receipt->stream_fingerprint,
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
                sinks[sink_index].state, &batches[batch_index]);
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
    receipt->status = LAPLACE_FRAMEWORK_OK;
    hash_stream_receipt(receipt);
    return LAPLACE_FRAMEWORK_OK;
}
