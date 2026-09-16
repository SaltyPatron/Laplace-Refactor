#include "postgres.h"

#include <stdint.h>
#include <string.h>

#include "blake3.h"
#include "miscadmin.h"
#include "utils/memutils.h"

#include "persistence_entities_pg.h"

/* This is a transport adapter over the shared native persistence codec and
 * framework producer. These domains identify its execution, never content. */
static const char ENTITY_PRODUCER_DOMAIN[] =
    "laplace-postgresql-entity-producer-v1";
static const char ENTITY_CURSOR_DOMAIN[] =
    "laplace-postgresql-entity-producer-cursor-v1";

typedef struct entity_producer_state {
    uint8_t* canonical_bytes;
    uint64_t entity_count;
    uint64_t frame_bytes;
    uint64_t records_per_batch;
    uint64_t batch_count;
    laplace_digest256 context_fingerprint;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 stream_fingerprint;
    laplace_digest256 producer_fingerprint;
} entity_producer_state;

static void entity_hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index)
        bytes[index] = (uint8_t)(value >> (8u * index));
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void entity_cursor(
    const entity_producer_state* state, uint64_t next_batch,
    laplace_digest256* fingerprint) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ENTITY_CURSOR_DOMAIN, sizeof(ENTITY_CURSOR_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, state->producer_fingerprint.bytes,
                        sizeof(state->producer_fingerprint.bytes));
    entity_hash_u64(&hasher, next_batch);
    blake3_hasher_finalize(&hasher, fingerprint->bytes, sizeof(fingerprint->bytes));
}

static int entity_layout(
    uint64_t entity_count, uint64_t maximum_batch_bytes,
    uint64_t* frame_bytes, uint64_t* total_bytes,
    uint64_t* records_per_batch) {
    *frame_bytes = (uint64_t)laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_ENTITY);
    if (entity_count == 0u || *frame_bytes == 0u ||
        maximum_batch_bytes < *frame_bytes ||
        entity_count > (uint64_t)MaxAllocSize / *frame_bytes)
        return 0;
    *total_bytes = entity_count * *frame_bytes;
    *records_per_batch = maximum_batch_bytes / *frame_bytes;
    if (*records_per_batch > entity_count) *records_per_batch = entity_count;
    return 1;
}

uint64_t laplace_pg_persistence_entities_memory_bytes(
    uint64_t entity_count, uint64_t maximum_batch_bytes) {
    uint64_t frame_bytes;
    uint64_t total_bytes;
    uint64_t records_per_batch;
    uint64_t sink_bytes;
    uint64_t adapter_bytes;
    if (!entity_layout(entity_count, maximum_batch_bytes,
            &frame_bytes, &total_bytes, &records_per_batch) ||
        !LAPLACE_PG_PERSISTENCE_BATCH_MEMORY_SYMBOL(
            records_per_batch * frame_bytes, records_per_batch, &sink_bytes))
        return 0u;
    adapter_bytes = total_bytes + sizeof(entity_producer_state);
    if (sink_bytes > UINT64_MAX - adapter_bytes) return 0u;
    return adapter_bytes + sink_bytes;
}

static laplace_framework_status entity_prepare(
    void* opaque, const laplace_framework_context* context,
    const laplace_digest256* source, const laplace_digest256* recipe,
    laplace_framework_producer_plan* plan) {
    const entity_producer_state* state = (const entity_producer_state*)opaque;
    laplace_digest256 fingerprint;
    if (state == NULL || context == NULL || source == NULL || recipe == NULL || plan == NULL)
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    memset(plan, 0, sizeof(*plan));
    if (laplace_framework_context_fingerprint(context, &fingerprint) != LAPLACE_FRAMEWORK_OK ||
        memcmp(&fingerprint, &state->context_fingerprint, sizeof(fingerprint)) != 0 ||
        memcmp(source, &state->source_fingerprint, sizeof(*source)) != 0 ||
        memcmp(recipe, &state->recipe_fingerprint, sizeof(*recipe)) != 0)
        return LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED;
    plan->producer_fingerprint = state->producer_fingerprint;
    entity_cursor(state, 0u, &plan->initial_cursor_fingerprint);
    plan->batch_count = state->batch_count;
    plan->total_records = state->entity_count;
    plan->total_bytes = state->entity_count * state->frame_bytes;
    plan->record_type = LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE;
    plan->flags = LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS;
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status entity_next(
    void* opaque, uint64_t batch_index,
    laplace_framework_canonical_batch* batch, laplace_digest256* cursor) {
    const entity_producer_state* state = (const entity_producer_state*)opaque;
    uint64_t first;
    uint64_t count;
    if (state == NULL || batch == NULL || cursor == NULL)
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    if (batch_index >= state->batch_count) return LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
    first = batch_index * state->records_per_batch;
    count = state->entity_count - first;
    if (count > state->records_per_batch) count = state->records_per_batch;
    memset(batch, 0, sizeof(*batch));
    batch->canonical_bytes = state->canonical_bytes + first * state->frame_bytes;
    batch->byte_count = count * state->frame_bytes;
    batch->record_count = count;
    batch->first_ordinal = first;
    batch->record_type = LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE;
    batch->flags = LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS;
    entity_cursor(state, batch_index + 1u, cursor);
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status entity_finish(void* opaque, laplace_digest256* completion) {
    const entity_producer_state* state = (const entity_producer_state*)opaque;
    if (state == NULL || completion == NULL) return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    *completion = state->stream_fingerprint;
    return LAPLACE_FRAMEWORK_OK;
}

static void entity_abort(void* opaque) {
    /* All bytes belong to the caller's PostgreSQL memory context. */
    (void)opaque;
}

static laplace_framework_status entity_producer_initialize(
    const laplace_framework_context* context,
    const laplace_digest256* source, const laplace_digest256* recipe,
    const laplace_persistence_entity_record* entities,
    uint64_t entity_count, uint64_t maximum_batch_bytes,
    uint8_t* canonical_bytes, entity_producer_state* state,
    laplace_framework_producer_v1* producer) {
    laplace_framework_canonical_batch whole;
    uint64_t total_bytes;
    uint64_t index;
    uint64_t checked_records;
    uint64_t checked_bytes;
    uint32_t record_type;
    blake3_hasher hasher;
    if (context == NULL || source == NULL || recipe == NULL || entities == NULL ||
        canonical_bytes == NULL || state == NULL || producer == NULL)
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    memset(producer, 0, sizeof(*producer));
    if (!entity_layout(entity_count, maximum_batch_bytes, &state->frame_bytes,
            &total_bytes, &state->records_per_batch) ||
        laplace_framework_context_fingerprint(context, &state->context_fingerprint) != LAPLACE_FRAMEWORK_OK)
        return LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED;
    for (index = 0u; index < entity_count; ++index) {
        size_t written = 0u;
        if ((index != 0u && memcmp(entities[index - 1u].entity_id.bytes,
                entities[index].entity_id.bytes, sizeof(entities[index].entity_id.bytes)) >= 0) ||
            laplace_persistence_frame_encode_entity(&entities[index].entity_id,
                &entities[index].identity_witness,
                canonical_bytes + index * state->frame_bytes,
                (size_t)state->frame_bytes, &written) != LAPLACE_PERSISTENCE_OK ||
            written != state->frame_bytes)
            return LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED;
    }
    state->canonical_bytes = canonical_bytes;
    state->entity_count = entity_count;
    state->batch_count = 1u + (entity_count - 1u) / state->records_per_batch;
    state->source_fingerprint = *source;
    state->recipe_fingerprint = *recipe;
    memset(&whole, 0, sizeof(whole));
    whole.canonical_bytes = canonical_bytes;
    whole.byte_count = total_bytes;
    whole.record_count = entity_count;
    whole.record_type = LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE;
    whole.flags = LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS;
    if (laplace_framework_canonical_stream_fingerprint(&whole, 1u,
            &state->stream_fingerprint, &record_type, &checked_records, &checked_bytes) != LAPLACE_FRAMEWORK_OK ||
        checked_records != entity_count || checked_bytes != total_bytes ||
        record_type != LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE)
        return LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ENTITY_PRODUCER_DOMAIN, sizeof(ENTITY_PRODUCER_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, state->context_fingerprint.bytes, sizeof(state->context_fingerprint.bytes));
    blake3_hasher_update(&hasher, source->bytes, sizeof(source->bytes));
    blake3_hasher_update(&hasher, recipe->bytes, sizeof(recipe->bytes));
    blake3_hasher_update(&hasher, state->stream_fingerprint.bytes, sizeof(state->stream_fingerprint.bytes));
    entity_hash_u64(&hasher, state->records_per_batch);
    blake3_hasher_finalize(&hasher, state->producer_fingerprint.bytes, sizeof(state->producer_fingerprint.bytes));
    producer->state = state;
    producer->prepare = entity_prepare;
    producer->next = entity_next;
    producer->finish = entity_finish;
    producer->abort = entity_abort;
    producer->abi_major = LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR;
    producer->abi_minor = LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR;
    producer->flags = LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS;
    return LAPLACE_FRAMEWORK_OK;
}

void laplace_pg_persistence_deposit_entities(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_persistence_entity_record* entities,
    uint64_t entity_count, uint64_t maximum_batch_bytes,
    const laplace_pg_persistence_options* options,
    laplace_pg_persistence_producer_result* result) {
    entity_producer_state state;
    laplace_framework_producer_v1 producer;
    laplace_pg_persistence_options bounded;
    uint64_t frame_bytes;
    uint64_t total_bytes;
    uint64_t records_per_batch;
    uint64_t required_bytes;
    uint8_t* canonical_bytes;
    if (context == NULL || options == NULL || entities == NULL || result == NULL ||
        source_fingerprint == NULL || recipe_fingerprint == NULL ||
        !entity_layout(entity_count, maximum_batch_bytes,
            &frame_bytes, &total_bytes, &records_per_batch))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace entity producer arguments are invalid")));
    required_bytes = laplace_pg_persistence_entities_memory_bytes(entity_count, maximum_batch_bytes);
    if (required_bytes == 0u || options->reserved_memory_bytes > context->resource_grant.memory_bytes ||
        required_bytes > context->resource_grant.memory_bytes - options->reserved_memory_bytes)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace entity producer memory grant is exhausted")));
    bounded = *options;
    bounded.reserved_memory_bytes += total_bytes + sizeof(state);
    canonical_bytes = (uint8_t*)palloc((Size)total_bytes);
    CHECK_FOR_INTERRUPTS();
    if (entity_producer_initialize(context, source_fingerprint, recipe_fingerprint,
            entities, entity_count, maximum_batch_bytes, canonical_bytes,
            &state, &producer) != LAPLACE_FRAMEWORK_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION),
            errmsg("Laplace entity producer requires exact ordered unique native identities")));
    LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_BOUNDED_SYMBOL(
        context, source_fingerprint, recipe_fingerprint, &producer, &bounded, result);
    pfree(canonical_bytes);
}
