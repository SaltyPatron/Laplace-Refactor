#include "laplace/framework.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

void FillDigest(laplace_digest256* digest, std::uint8_t seed) {
    for (std::size_t index = 0; index < sizeof(digest->bytes); ++index) {
        digest->bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_framework_context Context() {
    laplace_framework_context context{};
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.flags = LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY;
    context.epoch_mask =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_IDENTITY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_DATABASE) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_NUMERIC) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PACKAGE);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY], 0x10u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY], 0x30u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE], 0x40u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC], 0x50u);
    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_PACKAGE], 0x70u);
    FillDigest(&context.authority_fingerprint, 0x90u);
    context.resource_grant = laplace_execution_grant{1u << 20u, 4u, 1u};
    return context;
}

laplace_framework_canonical_batch Batch(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    std::uint64_t record_count,
    std::uint64_t first_ordinal) {
    return laplace_framework_canonical_batch{
        bytes,
        static_cast<std::uint64_t>(byte_count),
        record_count,
        first_ordinal,
        LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
}

laplace_framework_canonical_stream Stream(
    const laplace_framework_canonical_batch* batches,
    std::size_t batch_count,
    std::uint8_t source_seed = 0xc0u,
    std::uint8_t recipe_seed = 0xe0u) {
    laplace_framework_canonical_stream stream{};
    stream.batches = batches;
    stream.batch_count = static_cast<std::uint64_t>(batch_count);
    stream.flags = LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS;
    FillDigest(&stream.source_fingerprint, source_seed);
    FillDigest(&stream.recipe_fingerprint, recipe_seed);
    return stream;
}

struct MemorySink final {
    std::vector<std::uint8_t> bytes;
    std::uint64_t expected_records{};
    std::uint64_t expected_bytes{};
    std::uint32_t record_type{};
    bool begun{};
    bool sealed{};
    bool aborted{};
    bool fail_seal{};
};

laplace_framework_status BeginSink(
    void* state,
    const laplace_framework_context*,
    std::uint32_t record_type,
    std::uint64_t total_records,
    std::uint64_t total_bytes) {
    auto* sink = static_cast<MemorySink*>(state);
    sink->bytes.clear();
    sink->expected_records = total_records;
    sink->expected_bytes = total_bytes;
    sink->record_type = record_type;
    sink->begun = true;
    sink->sealed = false;
    sink->aborted = false;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StageSink(
    void* state,
    const laplace_framework_canonical_batch* batch) {
    auto* sink = static_cast<MemorySink*>(state);
    if (!sink->begun || sink->sealed || batch->record_type != sink->record_type) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    try {
        sink->bytes.insert(
            sink->bytes.end(), batch->canonical_bytes,
            batch->canonical_bytes + batch->byte_count);
    } catch (...) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SealSink(
    void* state,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    auto* sink = static_cast<MemorySink*>(state);
    if (sink->fail_seal || !sink->begun ||
        sink->bytes.size() != sink->expected_bytes) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    *artifact_fingerprint = *stream_fingerprint;
    sink->sealed = true;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void* state) {
    auto* sink = static_cast<MemorySink*>(state);
    sink->bytes.clear();
    sink->sealed = false;
    sink->aborted = true;
}

laplace_framework_sink_v1 Sink(MemorySink* state) {
    return laplace_framework_sink_v1{
        state,
        BeginSink,
        StageSink,
        SealSink,
        AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR,
        0u,
        0u};
}

struct ProducerState final {
    std::array<std::array<std::uint8_t, 2>, 3> bytes{{
        {{1u, 2u}}, {{3u, 4u}}, {{5u, 6u}}}};
    std::uint64_t next_calls{};
    std::uint64_t invalid_batch{UINT64_MAX};
    bool invalid_plan{};
    bool prepared{};
    bool finished{};
    bool aborted{};
};

laplace_framework_status PrepareProducer(
    void* opaque,
    const laplace_framework_context*,
    const laplace_digest256*,
    const laplace_digest256*,
    laplace_framework_producer_plan* plan) {
    auto* state = static_cast<ProducerState*>(opaque);
    state->next_calls = 0;
    state->prepared = true;
    state->finished = false;
    state->aborted = false;
    *plan = laplace_framework_producer_plan{};
    FillDigest(&plan->producer_fingerprint, 0x21u);
    FillDigest(&plan->initial_cursor_fingerprint, 0x31u);
    plan->batch_count = static_cast<std::uint64_t>(state->bytes.size());
    plan->total_records = static_cast<std::uint64_t>(state->bytes.size());
    plan->total_bytes = static_cast<std::uint64_t>(
        state->bytes.size() * state->bytes.front().size());
    plan->record_type = LAPLACE_ISA_VALUE_U32_VECTOR;
    plan->flags = LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS;
    if (state->invalid_plan) {
        plan->total_bytes = 0u;
    }
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status NextProducer(
    void* opaque,
    std::uint64_t batch_index,
    laplace_framework_canonical_batch* batch,
    laplace_digest256* cursor_fingerprint) {
    auto* state = static_cast<ProducerState*>(opaque);
    if (!state->prepared || batch_index >= state->bytes.size()) {
        return LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
    }
    ++state->next_calls;
    const auto index = static_cast<std::size_t>(batch_index);
    *batch = Batch(
        state->bytes[index].data(), state->bytes[index].size(), 1u,
        batch_index == state->invalid_batch ? batch_index + 1u : batch_index);
    FillDigest(
        cursor_fingerprint,
        static_cast<std::uint8_t>(0x41u + batch_index));
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status FinishProducer(
    void* opaque,
    laplace_digest256* completion_fingerprint) {
    auto* state = static_cast<ProducerState*>(opaque);
    state->finished = true;
    FillDigest(completion_fingerprint, 0x61u);
    return LAPLACE_FRAMEWORK_OK;
}

void AbortProducer(void* opaque) {
    static_cast<ProducerState*>(opaque)->aborted = true;
}

laplace_framework_producer_v1 Producer(ProducerState* state) {
    return laplace_framework_producer_v1{
        state,
        PrepareProducer,
        NextProducer,
        FinishProducer,
        AbortProducer,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS,
        0u};
}

struct ProducerControlState final {
    std::vector<laplace_framework_replay_checkpoint> checkpoints;
    std::uint64_t cancel_after_batches{UINT64_MAX};
    bool cancelled{};
};

int ProducerCancellationRequested(void* opaque) {
    return static_cast<ProducerControlState*>(opaque)->cancelled ? 1 : 0;
}

void ObserveProducerProgress(
    void* opaque,
    const laplace_framework_replay_checkpoint* checkpoint) {
    auto* state = static_cast<ProducerControlState*>(opaque);
    state->checkpoints.push_back(*checkpoint);
    if (checkpoint->completed_batches == state->cancel_after_batches) {
        state->cancelled = true;
    }
}

laplace_framework_producer_control_v1 ProducerControl(
    ProducerControlState* state,
    const laplace_framework_replay_checkpoint* replay = nullptr) {
    return laplace_framework_producer_control_v1{
        state,
        replay,
        ProducerCancellationRequested,
        ObserveProducerProgress,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS,
        0u};
}

void ProducerBindings(
    laplace_digest256* source_fingerprint,
    laplace_digest256* recipe_fingerprint) {
    FillDigest(source_fingerprint, 0xc4u);
    FillDigest(recipe_fingerprint, 0xe4u);
}

struct ActivationState final {
    laplace_digest256 current_epoch{};
    std::uint32_t prepare_count{};
    std::uint32_t commit_count{};
    std::uint32_t abort_count{};
    bool fail_prepare{};
    bool fail_commit{};
};

laplace_framework_status PrepareActivation(
    void* opaque,
    const laplace_framework_context*,
    const laplace_framework_stream_receipt*,
    const laplace_framework_activation_request* request,
    laplace_digest256* preparation_fingerprint) {
    auto* state = static_cast<ActivationState*>(opaque);
    ++state->prepare_count;
    if (state->fail_prepare ||
        std::memcmp(state->current_epoch.bytes, request->expected_epoch.bytes,
                    sizeof(state->current_epoch.bytes)) != 0) {
        return LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
    }
    FillDigest(preparation_fingerprint, 0xa0u);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status CommitActivation(
    void* opaque,
    const laplace_framework_activation_request* request,
    const laplace_digest256*,
    laplace_digest256* activation_fingerprint) {
    auto* state = static_cast<ActivationState*>(opaque);
    ++state->commit_count;
    if (state->fail_commit) {
        return LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
    }
    state->current_epoch = request->next_epoch;
    FillDigest(activation_fingerprint, 0xb0u);
    return LAPLACE_FRAMEWORK_OK;
}

void AbortActivation(
    void* opaque,
    const laplace_framework_activation_request*,
    const laplace_digest256*) {
    ++static_cast<ActivationState*>(opaque)->abort_count;
}

laplace_framework_activation_provider_v1 ActivationProvider(
    ActivationState* state) {
    return laplace_framework_activation_provider_v1{
        state,
        PrepareActivation,
        CommitActivation,
        AbortActivation,
        LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR,
        LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR,
        0u,
        0u};
}

laplace_framework_stream_receipt StagedReceipt(
    const laplace_framework_context* context,
    MemorySink* memory) {
    static constexpr std::array<std::uint8_t, 4> bytes{{4u, 3u, 2u, 1u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 1u, 0u);
    const auto stream = Stream(&batch, 1u);
    auto sink = Sink(memory);
    laplace_framework_stream_receipt receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  context, &stream, &sink, 1u, &receipt),
              LAPLACE_FRAMEWORK_OK);
    return receipt;
}

TEST(FrameworkRegistry, GeneratedDescriptorsMatchIsaContract) {
    ASSERT_EQ(laplace_framework_registry_validate(), LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_operation_count(), LAPLACE_ISA_OPERATION_COUNT);
    const auto* identity = laplace_framework_operation_find(
        LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH);
    ASSERT_NE(identity, nullptr);
    EXPECT_EQ(identity->input_type, LAPLACE_ISA_VALUE_U32_VECTOR);
    EXPECT_EQ(identity->output_type, LAPLACE_ISA_VALUE_ID128_VECTOR);
    EXPECT_EQ(identity->instruction_version,
              LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH);
    EXPECT_EQ(laplace_framework_operation_find(UINT32_MAX), nullptr);
}

TEST(FrameworkContext, AllBoundFactsAffectFingerprint) {
    auto context = Context();
    laplace_digest256 initial{};
    laplace_digest256 changed{};
    ASSERT_EQ(laplace_framework_context_fingerprint(&context, &initial),
              LAPLACE_FRAMEWORK_OK);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC].bytes[4] ^= 0x80u;
    ASSERT_EQ(laplace_framework_context_fingerprint(&context, &changed),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_NE(std::memcmp(initial.bytes, changed.bytes, sizeof(initial.bytes)), 0);

    FillDigest(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_SOURCE], 0xb0u);
    EXPECT_EQ(laplace_framework_context_validate(&context),
              LAPLACE_FRAMEWORK_CONTEXT_INVALID);
}

TEST(CanonicalStream, PartitionBoundariesDoNotChangeFingerprint) {
    const std::array<std::uint8_t, 6> bytes{{1u, 2u, 3u, 4u, 5u, 6u}};
    const auto whole = Batch(bytes.data(), bytes.size(), 3u, 0u);
    const std::array<laplace_framework_canonical_batch, 2> partitions{{
        Batch(bytes.data(), 2u, 1u, 0u),
        Batch(bytes.data() + 2u, 4u, 2u, 1u)}};
    laplace_digest256 whole_digest{};
    laplace_digest256 partition_digest{};
    std::uint32_t whole_type = 0;
    std::uint32_t partition_type = 0;
    std::uint64_t whole_records = 0;
    std::uint64_t partition_records = 0;
    std::uint64_t whole_bytes = 0;
    std::uint64_t partition_bytes = 0;
    ASSERT_EQ(laplace_framework_canonical_stream_fingerprint(
                  &whole, 1u, &whole_digest, &whole_type,
                  &whole_records, &whole_bytes),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_canonical_stream_fingerprint(
                  partitions.data(), partitions.size(), &partition_digest,
                  &partition_type, &partition_records, &partition_bytes),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(std::memcmp(
                  whole_digest.bytes, partition_digest.bytes,
                  sizeof(whole_digest.bytes)),
              0);
    EXPECT_EQ(whole_type, partition_type);
    EXPECT_EQ(whole_records, partition_records);
    EXPECT_EQ(whole_bytes, partition_bytes);
}

TEST(CanonicalStream, FansOneStreamOutToEverySink) {
    auto context = Context();
    const std::array<std::uint8_t, 5> bytes{{9u, 8u, 7u, 6u, 5u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 2u, 0u);
    const auto stream = Stream(&batch, 1u);
    MemorySink left{};
    MemorySink right{};
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    laplace_framework_stream_receipt receipt{};
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream, sinks.data(), sinks.size(), &receipt),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_TRUE(left.sealed);
    EXPECT_TRUE(right.sealed);
    EXPECT_EQ(left.bytes, right.bytes);
    EXPECT_EQ(left.bytes, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    EXPECT_EQ(receipt.total_records, 2u);
    EXPECT_EQ(receipt.total_bytes, bytes.size());
    EXPECT_EQ(receipt.sink_count, sinks.size());
    EXPECT_EQ(receipt.effect_disposition, LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT);
}

TEST(CanonicalStream, SourceAndRecipeAreMandatoryReceiptBindings) {
    auto context = Context();
    const std::array<std::uint8_t, 2> bytes{{7u, 9u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 1u, 0u);
    auto stream_a = Stream(&batch, 1u, 0xc0u, 0xe0u);
    auto stream_b = Stream(&batch, 1u, 0xc1u, 0xe0u);
    auto stream_c = Stream(&batch, 1u, 0xc0u, 0xe1u);
    MemorySink memory_a{};
    MemorySink memory_b{};
    MemorySink memory_c{};
    auto sink_a = Sink(&memory_a);
    auto sink_b = Sink(&memory_b);
    auto sink_c = Sink(&memory_c);
    laplace_framework_stream_receipt receipt_a{};
    laplace_framework_stream_receipt receipt_b{};
    laplace_framework_stream_receipt receipt_c{};
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_a, &sink_a, 1u, &receipt_a),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_b, &sink_b, 1u, &receipt_b),
              LAPLACE_FRAMEWORK_OK);
    ASSERT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_c, &sink_c, 1u, &receipt_c),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(std::memcmp(receipt_a.stream_fingerprint.bytes,
                          receipt_b.stream_fingerprint.bytes,
                          sizeof(receipt_a.stream_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.source_fingerprint.bytes,
                          receipt_b.source_fingerprint.bytes,
                          sizeof(receipt_a.source_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_b.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.recipe_fingerprint.bytes,
                          receipt_c.recipe_fingerprint.bytes,
                          sizeof(receipt_a.recipe_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_c.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);

    std::memset(&stream_a.source_fingerprint, 0, sizeof(stream_a.source_fingerprint));
    MemorySink zero_source{};
    auto zero_source_sink = Sink(&zero_source);
    laplace_framework_stream_receipt zero_source_receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream_a, &zero_source_sink, 1u,
                  &zero_source_receipt),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_TRUE(zero_source.begun);
    EXPECT_EQ(zero_source_receipt.effect_disposition,
              LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT);
    EXPECT_EQ(std::memcmp(zero_source_receipt.source_fingerprint.bytes,
                          stream_a.source_fingerprint.bytes,
                          sizeof(stream_a.source_fingerprint.bytes)), 0);
    EXPECT_EQ(std::memcmp(zero_source_receipt.recipe_fingerprint.bytes,
                          stream_a.recipe_fingerprint.bytes,
                          sizeof(stream_a.recipe_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          zero_source_receipt.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);
}

TEST(CanonicalStream, SealFailureAbortsEveryStagedSink) {
    auto context = Context();
    const std::array<std::uint8_t, 3> bytes{{3u, 2u, 1u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 1u, 0u);
    const auto stream = Stream(&batch, 1u);
    MemorySink left{};
    MemorySink right{};
    right.fail_seal = true;
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    laplace_framework_stream_receipt receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream, sinks.data(), sinks.size(), &receipt),
              LAPLACE_FRAMEWORK_SINK_SEAL_FAILED);
    EXPECT_TRUE(left.aborted);
    EXPECT_TRUE(right.aborted);
    EXPECT_FALSE(left.sealed);
    EXPECT_FALSE(right.sealed);
    EXPECT_TRUE(left.bytes.empty());
    EXPECT_TRUE(right.bytes.empty());
    EXPECT_EQ(receipt.failed_sink_index, 1u);
}

TEST(CanonicalStream, RejectsOrdinalGapsBeforeOpeningSinks) {
    auto context = Context();
    const std::array<std::uint8_t, 2> bytes{{1u, 2u}};
    const auto batch = Batch(bytes.data(), bytes.size(), 1u, 4u);
    const auto stream = Stream(&batch, 1u);
    MemorySink memory{};
    auto sink = Sink(&memory);
    laplace_framework_stream_receipt receipt{};
    EXPECT_EQ(laplace_framework_stage_canonical_stream(
                  &context, &stream, &sink, 1u, &receipt),
              LAPLACE_FRAMEWORK_STREAM_INVALID);
    EXPECT_FALSE(memory.begun);
}

TEST(FrameworkProducer, FansCanonicalBatchesThroughEveryExistingSink) {
    auto context = Context();
    laplace_digest256 source_fingerprint{};
    laplace_digest256 recipe_fingerprint{};
    ProducerBindings(&source_fingerprint, &recipe_fingerprint);
    ProducerState producer_state{};
    auto producer = Producer(&producer_state);
    ProducerControlState control_state{};
    auto control = ProducerControl(&control_state);
    MemorySink left{};
    MemorySink right{};
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    laplace_framework_producer_receipt receipt{};

    ASSERT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &producer, &control, sinks.data(), sinks.size(), &receipt),
              LAPLACE_FRAMEWORK_OK);
    const std::vector<std::uint8_t> expected{1u, 2u, 3u, 4u, 5u, 6u};
    EXPECT_EQ(left.bytes, expected);
    EXPECT_EQ(right.bytes, expected);
    EXPECT_TRUE(left.sealed);
    EXPECT_TRUE(right.sealed);
    EXPECT_FALSE(left.aborted);
    EXPECT_FALSE(right.aborted);
    EXPECT_TRUE(producer_state.finished);
    EXPECT_FALSE(producer_state.aborted);
    EXPECT_EQ(producer_state.next_calls, 3u);
    EXPECT_EQ(receipt.stream.batch_count, 3u);
    EXPECT_EQ(receipt.stream.total_records, 3u);
    EXPECT_EQ(receipt.stream.total_bytes, 6u);
    EXPECT_EQ(receipt.stream.effect_disposition,
              LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT);
    EXPECT_EQ(receipt.checkpoint.completed_batches, 3u);
    EXPECT_EQ(receipt.checkpoint.completed_records, 3u);
    EXPECT_EQ(receipt.checkpoint.completed_bytes, 6u);
    EXPECT_EQ(receipt.progress_events, 4u);
    EXPECT_EQ(receipt.replay_verified, 0u);

    const std::array<laplace_framework_canonical_batch, 3> batches{{
        Batch(producer_state.bytes[0].data(), 2u, 1u, 0u),
        Batch(producer_state.bytes[1].data(), 2u, 1u, 1u),
        Batch(producer_state.bytes[2].data(), 2u, 1u, 2u)}};
    laplace_digest256 expected_stream{};
    std::uint32_t record_type = 0;
    std::uint64_t total_records = 0;
    std::uint64_t total_bytes = 0;
    ASSERT_EQ(laplace_framework_canonical_stream_fingerprint(
                  batches.data(), batches.size(), &expected_stream,
                  &record_type, &total_records, &total_bytes),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(std::memcmp(
                  expected_stream.bytes, receipt.stream.stream_fingerprint.bytes,
                  sizeof(expected_stream.bytes)),
              0);
}

TEST(FrameworkProducer, CancellationReturnsExactBoundaryAndAbortsEverySink) {
    auto context = Context();
    laplace_digest256 source_fingerprint{};
    laplace_digest256 recipe_fingerprint{};
    ProducerBindings(&source_fingerprint, &recipe_fingerprint);

    ProducerState initial_state{};
    auto initial_producer = Producer(&initial_state);
    ProducerControlState initial_control_state{};
    initial_control_state.cancel_after_batches = 0u;
    auto initial_control = ProducerControl(&initial_control_state);
    MemorySink initial_memory{};
    auto initial_sink = Sink(&initial_memory);
    laplace_framework_producer_receipt initial_receipt{};
    EXPECT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &initial_producer, &initial_control, &initial_sink, 1u,
                  &initial_receipt),
              LAPLACE_FRAMEWORK_PRODUCER_CANCELLED);
    EXPECT_EQ(initial_receipt.checkpoint.completed_batches, 0u);
    EXPECT_FALSE(initial_memory.begun);
    EXPECT_TRUE(initial_state.aborted);

    ProducerState producer_state{};
    auto producer = Producer(&producer_state);
    ProducerControlState control_state{};
    control_state.cancel_after_batches = 1u;
    auto control = ProducerControl(&control_state);
    MemorySink left{};
    MemorySink right{};
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    laplace_framework_producer_receipt receipt{};

    EXPECT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &producer, &control, sinks.data(), sinks.size(), &receipt),
              LAPLACE_FRAMEWORK_PRODUCER_CANCELLED);
    EXPECT_EQ(receipt.status, LAPLACE_FRAMEWORK_PRODUCER_CANCELLED);
    EXPECT_EQ(receipt.stream.effect_disposition, LAPLACE_FRAMEWORK_EFFECT_NONE);
    EXPECT_EQ(receipt.checkpoint.completed_batches, 1u);
    EXPECT_EQ(receipt.checkpoint.completed_records, 1u);
    EXPECT_EQ(receipt.checkpoint.completed_bytes, 2u);
    EXPECT_EQ(receipt.checkpoint.next_ordinal, 1u);
    EXPECT_EQ(receipt.progress_events, 2u);
    EXPECT_EQ(producer_state.next_calls, 1u);
    EXPECT_TRUE(producer_state.aborted);
    EXPECT_FALSE(producer_state.finished);
    EXPECT_TRUE(left.aborted);
    EXPECT_TRUE(right.aborted);
    EXPECT_FALSE(left.sealed);
    EXPECT_FALSE(right.sealed);
    EXPECT_TRUE(left.bytes.empty());
    EXPECT_TRUE(right.bytes.empty());
}

TEST(FrameworkProducer, ReplayRestartsAndAssertsTheExactPriorPrefix) {
    auto context = Context();
    laplace_digest256 source_fingerprint{};
    laplace_digest256 recipe_fingerprint{};
    ProducerBindings(&source_fingerprint, &recipe_fingerprint);

    ProducerState interrupted_state{};
    auto interrupted_producer = Producer(&interrupted_state);
    ProducerControlState interrupted_control_state{};
    interrupted_control_state.cancel_after_batches = 1u;
    auto interrupted_control = ProducerControl(&interrupted_control_state);
    MemorySink interrupted_memory{};
    auto interrupted_sink = Sink(&interrupted_memory);
    laplace_framework_producer_receipt interrupted{};
    ASSERT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &interrupted_producer, &interrupted_control,
                  &interrupted_sink, 1u, &interrupted),
              LAPLACE_FRAMEWORK_PRODUCER_CANCELLED);

    ProducerState replay_state{};
    auto replay_producer = Producer(&replay_state);
    ProducerControlState replay_control_state{};
    auto replay_control = ProducerControl(
        &replay_control_state, &interrupted.checkpoint);
    MemorySink replay_memory{};
    auto replay_sink = Sink(&replay_memory);
    laplace_framework_producer_receipt replayed{};
    ASSERT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &replay_producer, &replay_control, &replay_sink, 1u,
                  &replayed),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(replay_state.next_calls, 3u);
    EXPECT_EQ(replayed.replay_verified, 1u);
    EXPECT_EQ(std::memcmp(
                  replayed.replay_checkpoint_id.bytes,
                  interrupted.checkpoint.checkpoint_id.bytes,
                  sizeof(replayed.replay_checkpoint_id.bytes)),
              0);
    EXPECT_EQ(replay_memory.bytes,
              (std::vector<std::uint8_t>{1u, 2u, 3u, 4u, 5u, 6u}));

    ProducerState altered_state{};
    altered_state.bytes[0][0] ^= 0x80u;
    auto altered_producer = Producer(&altered_state);
    ProducerControlState altered_control_state{};
    auto altered_control = ProducerControl(
        &altered_control_state, &interrupted.checkpoint);
    MemorySink altered_memory{};
    auto altered_sink = Sink(&altered_memory);
    laplace_framework_producer_receipt altered{};
    EXPECT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &altered_producer, &altered_control, &altered_sink, 1u,
                  &altered),
              LAPLACE_FRAMEWORK_REPLAY_MISMATCH);
    EXPECT_EQ(altered_state.next_calls, 1u);
    EXPECT_TRUE(altered_state.aborted);
    EXPECT_TRUE(altered_memory.aborted);
    EXPECT_FALSE(altered_memory.sealed);
    EXPECT_TRUE(altered_memory.bytes.empty());
}

TEST(FrameworkProducer, InvalidLaterBatchCannotSealAnyPartialArtifact) {
    auto context = Context();
    laplace_digest256 source_fingerprint{};
    laplace_digest256 recipe_fingerprint{};
    ProducerBindings(&source_fingerprint, &recipe_fingerprint);
    ProducerState producer_state{};
    producer_state.invalid_batch = 1u;
    auto producer = Producer(&producer_state);
    ProducerControlState control_state{};
    auto control = ProducerControl(&control_state);
    MemorySink left{};
    MemorySink right{};
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    laplace_framework_producer_receipt receipt{};

    EXPECT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &producer, &control, sinks.data(), sinks.size(), &receipt),
              LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED);
    EXPECT_EQ(receipt.stream.failed_batch_index, 1u);
    EXPECT_EQ(receipt.checkpoint.completed_batches, 1u);
    EXPECT_TRUE(producer_state.aborted);
    EXPECT_FALSE(producer_state.finished);
    EXPECT_TRUE(left.aborted);
    EXPECT_TRUE(right.aborted);
    EXPECT_FALSE(left.sealed);
    EXPECT_FALSE(right.sealed);
    EXPECT_TRUE(left.bytes.empty());
    EXPECT_TRUE(right.bytes.empty());
}

TEST(FrameworkProducer, RejectsInvalidAbiAndPlanBeforeOpeningAnySink) {
    auto context = Context();
    laplace_digest256 source_fingerprint{};
    laplace_digest256 recipe_fingerprint{};
    ProducerBindings(&source_fingerprint, &recipe_fingerprint);

    ProducerState abi_state{};
    auto invalid_abi = Producer(&abi_state);
    invalid_abi.abi_minor =
        static_cast<std::uint16_t>(LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR + 1u);
    ProducerControlState abi_control_state{};
    auto abi_control = ProducerControl(&abi_control_state);
    MemorySink abi_memory{};
    auto abi_sink = Sink(&abi_memory);
    laplace_framework_producer_receipt abi_receipt{};
    EXPECT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &invalid_abi, &abi_control, &abi_sink, 1u, &abi_receipt),
              LAPLACE_FRAMEWORK_PRODUCER_INVALID);
    EXPECT_FALSE(abi_state.prepared);
    EXPECT_FALSE(abi_state.aborted);
    EXPECT_FALSE(abi_memory.begun);
    EXPECT_EQ(abi_receipt.stream.effect_disposition,
              LAPLACE_FRAMEWORK_EFFECT_NONE);

    ProducerState plan_state{};
    plan_state.invalid_plan = true;
    auto invalid_plan = Producer(&plan_state);
    ProducerControlState plan_control_state{};
    auto plan_control = ProducerControl(&plan_control_state);
    MemorySink plan_memory{};
    auto plan_sink = Sink(&plan_memory);
    laplace_framework_producer_receipt plan_receipt{};
    EXPECT_EQ(laplace_framework_run_producer(
                  &context, &source_fingerprint, &recipe_fingerprint,
                  &invalid_plan, &plan_control, &plan_sink, 1u,
                  &plan_receipt),
              LAPLACE_FRAMEWORK_PRODUCER_INVALID);
    EXPECT_TRUE(plan_state.prepared);
    EXPECT_TRUE(plan_state.aborted);
    EXPECT_FALSE(plan_memory.begun);
    EXPECT_EQ(plan_receipt.stream.effect_disposition,
              LAPLACE_FRAMEWORK_EFFECT_NONE);
}

TEST(FrameworkActivation, PublishesOneAdmittedEpochTransition) {
    auto context = Context();
    context.flags = 0u;
    MemorySink memory{};
    const auto staged = StagedReceipt(&context, &memory);
    ActivationState state{};
    state.current_epoch = context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE];
    auto provider = ActivationProvider(&state);
    laplace_framework_activation_request request{};
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_DATABASE;
    request.expected_epoch = state.current_epoch;
    FillDigest(&request.next_epoch, 0xd0u);
    laplace_framework_activation_receipt receipt{};

    ASSERT_EQ(laplace_framework_activate_staged_stream(
                  &context, &staged, &request, &provider, &receipt),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(state.prepare_count, 1u);
    EXPECT_EQ(state.commit_count, 1u);
    EXPECT_EQ(state.abort_count, 0u);
    EXPECT_EQ(std::memcmp(state.current_epoch.bytes, request.next_epoch.bytes,
                          sizeof(state.current_epoch.bytes)), 0);
    EXPECT_EQ(receipt.effect_disposition, LAPLACE_FRAMEWORK_EFFECT_ACTIVATED);
    EXPECT_EQ(receipt.epoch_slot, LAPLACE_FRAMEWORK_EPOCH_DATABASE);
    EXPECT_EQ(std::memcmp(receipt.staged_receipt_id.bytes,
                          staged.receipt_id.bytes,
                          sizeof(receipt.staged_receipt_id.bytes)), 0);
    EXPECT_FALSE(std::all_of(
        std::begin(receipt.receipt_id.bytes), std::end(receipt.receipt_id.bytes),
        [](std::uint8_t value) { return value == 0u; }));
}

TEST(FrameworkActivation, RejectsStaleEpochBeforeProviderAdmission) {
    auto context = Context();
    context.flags = 0u;
    MemorySink memory{};
    const auto staged = StagedReceipt(&context, &memory);
    ActivationState state{};
    state.current_epoch = context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE];
    auto provider = ActivationProvider(&state);
    laplace_framework_activation_request request{};
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_DATABASE;
    FillDigest(&request.expected_epoch, 0x01u);
    FillDigest(&request.next_epoch, 0xd0u);
    laplace_framework_activation_receipt receipt{};

    EXPECT_EQ(laplace_framework_activate_staged_stream(
                  &context, &staged, &request, &provider, &receipt),
              LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID);
    EXPECT_EQ(state.prepare_count, 0u);
    EXPECT_EQ(state.commit_count, 0u);
    EXPECT_EQ(state.abort_count, 0u);
    EXPECT_EQ(receipt.effect_disposition, LAPLACE_FRAMEWORK_EFFECT_NONE);
    EXPECT_EQ(std::memcmp(receipt.expected_epoch.bytes,
                          request.expected_epoch.bytes,
                          sizeof(receipt.expected_epoch.bytes)), 0);
    EXPECT_FALSE(std::all_of(
        std::begin(receipt.request_fingerprint.bytes),
        std::end(receipt.request_fingerprint.bytes),
        [](std::uint8_t value) { return value == 0u; }));
}

TEST(FrameworkActivation, RejectsAlteredStageAndReadOnlyAuthority) {
    auto context = Context();
    MemorySink memory{};
    auto staged = StagedReceipt(&context, &memory);
    ActivationState state{};
    state.current_epoch = context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE];
    auto provider = ActivationProvider(&state);
    laplace_framework_activation_request request{};
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_DATABASE;
    request.expected_epoch = state.current_epoch;
    FillDigest(&request.next_epoch, 0xd0u);
    laplace_framework_activation_receipt receipt{};

    EXPECT_EQ(laplace_framework_activate_staged_stream(
                  &context, &staged, &request, &provider, &receipt),
              LAPLACE_FRAMEWORK_EFFECT_NOT_AUTHORIZED);
    EXPECT_EQ(state.prepare_count, 0u);

    context.flags = 0u;
    staged.total_records += 1u;
    EXPECT_EQ(laplace_framework_activate_staged_stream(
                  &context, &staged, &request, &provider, &receipt),
              LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID);
    EXPECT_EQ(state.prepare_count, 0u);
}

TEST(FrameworkActivation, FailedAtomicCommitRemainsOnlyAdmitted) {
    auto context = Context();
    context.flags = 0u;
    MemorySink memory{};
    const auto staged = StagedReceipt(&context, &memory);
    ActivationState state{};
    state.current_epoch = context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE];
    state.fail_commit = true;
    const auto original_epoch = state.current_epoch;
    auto provider = ActivationProvider(&state);
    laplace_framework_activation_request request{};
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_DATABASE;
    request.expected_epoch = state.current_epoch;
    FillDigest(&request.next_epoch, 0xd0u);
    laplace_framework_activation_receipt receipt{};

    EXPECT_EQ(laplace_framework_activate_staged_stream(
                  &context, &staged, &request, &provider, &receipt),
              LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED);
    EXPECT_EQ(state.prepare_count, 1u);
    EXPECT_EQ(state.commit_count, 1u);
    EXPECT_EQ(state.abort_count, 1u);
    EXPECT_EQ(std::memcmp(state.current_epoch.bytes, original_epoch.bytes,
                          sizeof(state.current_epoch.bytes)), 0);
    EXPECT_EQ(receipt.effect_disposition,
              LAPLACE_FRAMEWORK_EFFECT_ACTIVATION_ADMITTED);
}

}  // namespace
