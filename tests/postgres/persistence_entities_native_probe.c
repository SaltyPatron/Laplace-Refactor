/* Execute the actual adapter callbacks with a native checking sink. Linker
 * section collection omits PG entrypoints; this is not a PostgreSQL execution
 * fixture and cannot establish a stored deposit receipt. */
#ifndef ENTITY_ADAPTER_SOURCE
#define ENTITY_ADAPTER_SOURCE "../../integrations/postgresql/extension/src/persistence_entities_pg.c"
#endif
#include ENTITY_ADAPTER_SOURCE

#undef fprintf
#undef printf
#undef qsort

#include <stdio.h>
#include <stdlib.h>

static unsigned assertions = 0;
#define CHECK(c) do { ++assertions; if (!(c)) { fprintf(stderr, "assertion %u at line %d failed: %s\n", assertions, __LINE__, #c); exit(1); } } while (0)

typedef struct proof_sink {
    const laplace_persistence_entity_record* expected;
    uint64_t count;
    uint64_t received;
    uint64_t stages;
} proof_sink;

static laplace_framework_status proof_begin(void* opaque,
    const laplace_framework_context* context, uint32_t type,
    uint64_t records, uint64_t bytes) {
    proof_sink* sink = opaque;
    (void)context;
    CHECK(type == LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE);
    CHECK(records == sink->count);
    CHECK(bytes == records * laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_ENTITY));
    sink->received = 0u;
    sink->stages = 0u;
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status proof_stage(void* opaque,
    const laplace_framework_canonical_batch* batch) {
    proof_sink* sink = opaque;
    size_t offset = 0u;
    CHECK(batch->first_ordinal == sink->received);
    for (uint64_t i = 0u; i < batch->record_count; ++i) {
        laplace_persistence_record record;
        size_t consumed = 0u;
        CHECK(laplace_persistence_frame_decode(batch->canonical_bytes + offset,
            batch->byte_count - offset, &record, &consumed) == LAPLACE_PERSISTENCE_OK);
        CHECK(record.kind == LAPLACE_PERSISTENCE_RECORD_ENTITY);
        CHECK(sink->received < sink->count);
        CHECK(memcmp(&record.value.entity, &sink->expected[sink->received], sizeof(record.value.entity)) == 0);
        ++sink->received;
        offset += consumed;
    }
    CHECK(offset == batch->byte_count);
    ++sink->stages;
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status proof_seal(void* opaque,
    const laplace_digest256* stream, laplace_digest256* artifact) {
    proof_sink* sink = opaque;
    CHECK(sink->received == sink->count);
    *artifact = *stream;
    return LAPLACE_FRAMEWORK_OK;
}

static void proof_abort(void* opaque) { (void)opaque; }
static int proof_cancel(void* opaque) { (void)opaque; return 0; }
static void proof_progress(void* opaque, const laplace_framework_replay_checkpoint* checkpoint) {
    (void)opaque; (void)checkpoint;
}
static int compare_entity(const void* a, const void* b) {
    const laplace_persistence_entity_record* left = a;
    const laplace_persistence_entity_record* right = b;
    return memcmp(left->entity_id.bytes, right->entity_id.bytes, 16u);
}

int main(void) {
    laplace_persistence_entity_record entities[5];
    laplace_persistence_entity_record bad[5];
    laplace_framework_context context = {0};
    laplace_digest256 source = {{3}}, recipe = {{4}};
    uint64_t width = laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_ENTITY);
    uint8_t buffer[4096];
    entity_producer_state state;
    laplace_framework_producer_v1 producer;
    laplace_framework_producer_receipt first = {0}, repeat = {0}, whole = {0};
    proof_sink check_sink = {entities, 5u, 0u, 0u};
    laplace_framework_sink_v1 sink = {&check_sink, proof_begin, proof_stage, proof_seal, proof_abort,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR, LAPLACE_FRAMEWORK_SINK_ABI_MINOR, 0, 0};
    laplace_framework_producer_control_v1 control = {NULL, NULL, proof_cancel, proof_progress,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR, LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR, 0, 0};
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.epoch_mask = 1u;
    context.resource_grant.memory_bytes = 1u << 20;
    context.resource_grant.cpu_slots = 1u;
    context.resource_grant.io_slots = 1u;
    for (uint32_t i = 0u; i < 5u; ++i)
        CHECK(laplace_identity_codepoint_witness(0x61u + i, &entities[i].entity_id,
            &entities[i].identity_witness) == LAPLACE_IDENTITY_OK);
    qsort(entities, 5u, sizeof(entities[0]), compare_entity);
    CHECK(entity_producer_initialize(&context, &source, &recipe, entities, 5u,
        width * 2u + 1u, buffer, &state, &producer) == LAPLACE_FRAMEWORK_OK);
    CHECK(laplace_framework_run_producer(&context, &source, &recipe, &producer, &control,
        &sink, 1u, &first) == LAPLACE_FRAMEWORK_OK);
    CHECK(first.stream.total_records == 5u && first.stream.total_bytes == width * 5u);
    CHECK(first.stream.batch_count == 3u && check_sink.stages == 3u);
    CHECK(laplace_framework_stream_receipt_identity_validate(&first.stream) == LAPLACE_FRAMEWORK_OK);
    CHECK(memcmp(first.stream.stream_fingerprint.bytes, state.stream_fingerprint.bytes, 32u) == 0);
    CHECK(laplace_framework_run_producer(&context, &source, &recipe, &producer, &control,
        &sink, 1u, &repeat) == LAPLACE_FRAMEWORK_OK);
    CHECK(memcmp(&first, &repeat, sizeof(first)) == 0);
    control.replay_checkpoint = &first.checkpoint;
    CHECK(laplace_framework_run_producer(&context, &source, &recipe, &producer, &control,
        &sink, 1u, &repeat) == LAPLACE_FRAMEWORK_OK && repeat.replay_verified == 1u);
    control.replay_checkpoint = NULL;
    CHECK(entity_producer_initialize(&context, &source, &recipe, entities, 5u,
        width * 5u, buffer, &state, &producer) == LAPLACE_FRAMEWORK_OK);
    CHECK(laplace_framework_run_producer(&context, &source, &recipe, &producer, &control,
        &sink, 1u, &whole) == LAPLACE_FRAMEWORK_OK);
    CHECK(whole.stream.batch_count == 1u && check_sink.stages == 1u);
    CHECK(memcmp(first.stream.stream_fingerprint.bytes, whole.stream.stream_fingerprint.bytes, 32u) == 0);
    CHECK(memcmp(first.receipt_id.bytes, whole.receipt_id.bytes, 32u) != 0);
    {
        laplace_framework_producer_plan plan;
        laplace_framework_context changed = context;
        laplace_digest256 wrong = {{9}};
        changed.resource_grant.memory_bytes += 1u;
        CHECK(producer.prepare(producer.state, &changed, &source, &recipe, &plan) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
        CHECK(producer.prepare(producer.state, &context, &wrong, &recipe, &plan) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
        CHECK(producer.prepare(producer.state, &context, &source, &wrong, &plan) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
        laplace_framework_canonical_batch batch;
        CHECK(producer.next(producer.state, state.batch_count, &batch, &wrong) == LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED);
    }
    memcpy(bad, entities, sizeof(bad)); bad[1] = bad[0];
    CHECK(entity_producer_initialize(&context, &source, &recipe, bad, 5u,
        width * 2u, buffer, &state, &producer) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
    memcpy(bad, entities, sizeof(bad)); bad[0] = entities[1]; bad[1] = entities[0];
    CHECK(entity_producer_initialize(&context, &source, &recipe, bad, 5u,
        width * 2u, buffer, &state, &producer) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
    memcpy(bad, entities, sizeof(bad)); bad[0].identity_witness.bytes[0] ^= 1u;
    CHECK(entity_producer_initialize(&context, &source, &recipe, bad, 5u,
        width * 2u, buffer, &state, &producer) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
    CHECK(entity_producer_initialize(&context, &source, &recipe, entities, 0u,
        width * 2u, buffer, &state, &producer) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
    CHECK(entity_producer_initialize(&context, &source, &recipe, entities, 5u,
        width - 1u, buffer, &state, &producer) == LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED);
    CHECK(laplace_pg_persistence_entities_memory_bytes(0u, width) == 0u);
    CHECK(laplace_pg_persistence_entities_memory_bytes(UINT64_MAX, width) == 0u);
    CHECK(laplace_pg_persistence_entities_memory_bytes(5u, width - 1u) == 0u);
    CHECK(laplace_pg_persistence_entities_memory_bytes(5u, width) > 5u * width);
    CHECK(laplace_pg_persistence_entities_memory_bytes(5u, width * 5u) >
        laplace_pg_persistence_entities_memory_bytes(5u, width));
    printf("{\"status\":\"passed\",\"assertions\":%u,\"native_entities\":5,\"frame_bytes\":%llu,\"split_batches\":3,\"whole_batches\":1,\"postgres_execution\":false}\n", assertions, (unsigned long long)width);
    return 0;
}
