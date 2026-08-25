#include "laplace/framework.h"

#include <array>
#include <cstddef>
#include <cstdint>

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
    context.epoch_mask = UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_IDENTITY;
    FillDigest(
        &context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY], 0x10u);
    FillDigest(&context.authority_fingerprint, 0x30u);
    context.resource_grant = laplace_execution_grant{4096u, 1u, 0u};
    return context;
}

struct ProducerState final {
    std::array<std::array<std::uint8_t, 2>, 2> bytes{{
        {{1u, 2u}}, {{3u, 4u}}}};
    bool aborted{};
};

laplace_framework_status Prepare(
    void*,
    const laplace_framework_context*,
    const laplace_digest256*,
    const laplace_digest256*,
    laplace_framework_producer_plan* plan) {
    *plan = laplace_framework_producer_plan{};
    FillDigest(&plan->producer_fingerprint, 0x50u);
    FillDigest(&plan->initial_cursor_fingerprint, 0x70u);
    plan->batch_count = 2u;
    plan->total_records = 2u;
    plan->total_bytes = 4u;
    plan->record_type = LAPLACE_ISA_VALUE_U32_VECTOR;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Next(
    void* opaque,
    std::uint64_t batch_index,
    laplace_framework_canonical_batch* batch,
    laplace_digest256* cursor) {
    auto* state = static_cast<ProducerState*>(opaque);
    const auto index = static_cast<std::size_t>(batch_index);
    *batch = laplace_framework_canonical_batch{
        state->bytes[index].data(), 2u, 1u, batch_index,
        LAPLACE_ISA_VALUE_U32_VECTOR, LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    FillDigest(cursor, static_cast<std::uint8_t>(0x90u + batch_index));
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Finish(void*, laplace_digest256* completion) {
    FillDigest(completion, 0xb0u);
    return LAPLACE_FRAMEWORK_OK;
}

void AbortProducer(void* opaque) {
    static_cast<ProducerState*>(opaque)->aborted = true;
}

struct ControlState final {
    bool cancelled{};
};

int CancelRequested(void* opaque) {
    return static_cast<ControlState*>(opaque)->cancelled ? 1 : 0;
}

void Observe(
    void* opaque,
    const laplace_framework_replay_checkpoint* checkpoint) {
    if (checkpoint->completed_batches == 1u) {
        static_cast<ControlState*>(opaque)->cancelled = true;
    }
}

struct SinkState final {
    bool sealed{};
    bool aborted{};
};

laplace_framework_status BeginSink(
    void*, const laplace_framework_context*, std::uint32_t,
    std::uint64_t, std::uint64_t) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StageSink(
    void*, const laplace_framework_canonical_batch*) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SealSink(
    void* opaque,
    const laplace_digest256* stream,
    laplace_digest256* artifact) {
    static_cast<SinkState*>(opaque)->sealed = true;
    *artifact = *stream;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void* opaque) {
    static_cast<SinkState*>(opaque)->aborted = true;
}

}  // namespace

int main() {
    auto context = Context();
    laplace_digest256 source{};
    laplace_digest256 recipe{};
    FillDigest(&source, 0xc0u);
    FillDigest(&recipe, 0xe0u);
    ProducerState producer_state{};
    const laplace_framework_producer_v1 producer{
        &producer_state, Prepare, Next, Finish, AbortProducer,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS, 0u};
    ControlState control_state{};
    const laplace_framework_producer_control_v1 control{
        &control_state, nullptr, CancelRequested, Observe,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS, 0u};
    SinkState sink_state{};
    laplace_framework_sink_v1 sink{
        &sink_state, BeginSink, StageSink, SealSink, AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR, 0u, 0u};
    laplace_framework_producer_receipt receipt{};
    const auto status = laplace_framework_run_producer(
        &context, &source, &recipe, &producer, &control, &sink, 1u, &receipt);
    if (status != LAPLACE_FRAMEWORK_PRODUCER_CANCELLED ||
        receipt.checkpoint.completed_batches != 1u ||
        !producer_state.aborted || !sink_state.aborted || sink_state.sealed) {
        return 1;
    }

    ProducerState altered_state{};
    altered_state.bytes[0][0] ^= 0x80u;
    const laplace_framework_producer_v1 altered_producer{
        &altered_state, Prepare, Next, Finish, AbortProducer,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS, 0u};
    ControlState replay_control_state{};
    const laplace_framework_producer_control_v1 replay_control{
        &replay_control_state, &receipt.checkpoint, CancelRequested, Observe,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS, 0u};
    SinkState replay_sink_state{};
    laplace_framework_sink_v1 replay_sink{
        &replay_sink_state, BeginSink, StageSink, SealSink, AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR, 0u, 0u};
    laplace_framework_producer_receipt replay_receipt{};
    const auto replay_status = laplace_framework_run_producer(
        &context, &source, &recipe, &altered_producer, &replay_control,
        &replay_sink, 1u, &replay_receipt);
    return replay_status == LAPLACE_FRAMEWORK_REPLAY_MISMATCH &&
                   altered_state.aborted && replay_sink_state.aborted &&
                   !replay_sink_state.sealed
               ? 0
               : 1;
}
