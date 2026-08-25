#include "laplace/framework.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

static_assert(LAPLACE_FRAMEWORK_MAJOR == 1);
static_assert(LAPLACE_FRAMEWORK_MINOR == 6);
static_assert(LAPLACE_FRAMEWORK_DIGEST_ALL_BIT_PATTERNS_VALID == 1);
static_assert(LAPLACE_FRAMEWORK_OPTIONAL_PRESENCE_TYPED_STATE_ONLY == 1);
static_assert(LAPLACE_FRAMEWORK_ABSENT_EPOCH_PAYLOAD_CANONICAL_ZERO == 1);

constexpr std::array<std::uint8_t, 1> RECORD{{0x5au}};

struct SinkState {
    std::uint64_t begin_calls{};
    std::uint64_t stage_calls{};
    std::uint64_t seal_calls{};
    std::uint64_t abort_calls{};
};

struct ControlState {
    std::uint64_t progress_calls{};
};

bool IsZero(const laplace_digest256& digest) {
    std::uint8_t aggregate = 0;
    for (const std::uint8_t byte : digest.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0;
}

laplace_framework_context Context(bool zero_present_epoch) {
    laplace_framework_context context{};
    context.epoch_mask = UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_IDENTITY;
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.resource_grant.memory_bytes = 1;
    context.resource_grant.cpu_slots = 1;
    if (!zero_present_epoch) {
        context.epochs[LAPLACE_FRAMEWORK_EPOCH_IDENTITY].bytes[0] = 1;
    }
    return context;
}

laplace_framework_status Begin(
    void* state,
    const laplace_framework_context*,
    std::uint32_t,
    std::uint64_t,
    std::uint64_t) {
    ++static_cast<SinkState*>(state)->begin_calls;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Stage(
    void* state,
    const laplace_framework_canonical_batch*) {
    ++static_cast<SinkState*>(state)->stage_calls;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Seal(
    void* state,
    const laplace_digest256*,
    laplace_digest256*) {
    ++static_cast<SinkState*>(state)->seal_calls;
    return LAPLACE_FRAMEWORK_OK;
}

void Abort(void* state) {
    ++static_cast<SinkState*>(state)->abort_calls;
}

laplace_framework_sink_v1 Sink(SinkState* state) {
    return laplace_framework_sink_v1{
        state,
        Begin,
        Stage,
        Seal,
        Abort,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR,
        0,
        0};
}

laplace_framework_canonical_stream Stream(
    const laplace_framework_canonical_batch* batch) {
    return laplace_framework_canonical_stream{
        batch,
        {},
        {},
        1,
        LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS,
        0};
}

bool StageZeroStream(
    const laplace_framework_context& context,
    laplace_framework_stream_receipt* receipt) {
    const laplace_framework_canonical_batch batch{
        RECORD.data(),
        RECORD.size(),
        1,
        0,
        71,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    const laplace_framework_canonical_stream stream = Stream(&batch);
    SinkState state{};
    laplace_framework_sink_v1 sink = Sink(&state);
    const laplace_framework_status status =
        laplace_framework_stage_canonical_stream(
            &context, &stream, &sink, 1, receipt);
    return status == LAPLACE_FRAMEWORK_OK &&
        receipt->status == LAPLACE_FRAMEWORK_OK &&
        IsZero(receipt->source_fingerprint) &&
        IsZero(receipt->recipe_fingerprint) &&
        state.begin_calls == 1 && state.stage_calls == 1 &&
        state.seal_calls == 1 && state.abort_calls == 0;
}

laplace_framework_status Prepare(
    void*,
    const laplace_framework_context*,
    const laplace_digest256*,
    const laplace_digest256*,
    laplace_framework_producer_plan* plan) {
    plan->batch_count = 1;
    plan->total_records = 1;
    plan->total_bytes = RECORD.size();
    plan->record_type = 71;
    plan->flags = LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Next(
    void*,
    std::uint64_t batch_index,
    laplace_framework_canonical_batch* batch,
    laplace_digest256*) {
    if (batch_index != 0) {
        return LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
    }
    *batch = laplace_framework_canonical_batch{
        RECORD.data(),
        RECORD.size(),
        1,
        0,
        71,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Finish(void*, laplace_digest256*) {
    return LAPLACE_FRAMEWORK_OK;
}

void ProducerAbort(void*) {}

int CancelRequested(void*) {
    return 0;
}

void ObserveProgress(
    void* state,
    const laplace_framework_replay_checkpoint*) {
    ++static_cast<ControlState*>(state)->progress_calls;
}

laplace_framework_producer_v1 Producer() {
    return laplace_framework_producer_v1{
        nullptr,
        Prepare,
        Next,
        Finish,
        ProducerAbort,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS,
        0};
}

laplace_framework_producer_control_v1 Control(
    ControlState* state,
    const laplace_framework_replay_checkpoint* replay) {
    return laplace_framework_producer_control_v1{
        state,
        replay,
        CancelRequested,
        ObserveProgress,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS,
        0};
}

bool RunZeroProducer(
    const laplace_framework_context& context,
    const laplace_framework_replay_checkpoint* replay,
    laplace_framework_producer_receipt* receipt) {
    const laplace_digest256 source{};
    const laplace_digest256 recipe{};
    SinkState sink_state{};
    ControlState control_state{};
    laplace_framework_sink_v1 sink = Sink(&sink_state);
    laplace_framework_producer_v1 producer = Producer();
    laplace_framework_producer_control_v1 control =
        Control(&control_state, replay);
    const laplace_framework_status status = laplace_framework_run_producer(
        &context,
        &source,
        &recipe,
        &producer,
        &control,
        &sink,
        1,
        receipt);
    return status == LAPLACE_FRAMEWORK_OK &&
        receipt->status == LAPLACE_FRAMEWORK_OK &&
        IsZero(receipt->stream.source_fingerprint) &&
        IsZero(receipt->stream.recipe_fingerprint) &&
        IsZero(receipt->checkpoint.producer_fingerprint) &&
        IsZero(receipt->checkpoint.cursor_fingerprint) &&
        IsZero(receipt->completion_fingerprint) &&
        sink_state.begin_calls == 1 && sink_state.stage_calls == 1 &&
        sink_state.seal_calls == 1 && sink_state.abort_calls == 0 &&
        control_state.progress_calls == 2 &&
        (replay == nullptr || receipt->replay_verified == 1);
}

laplace_framework_status ActivationPrepare(
    void*,
    const laplace_framework_context*,
    const laplace_framework_stream_receipt*,
    const laplace_framework_activation_request*,
    laplace_digest256*) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status ActivationCommit(
    void*,
    const laplace_framework_activation_request*,
    const laplace_digest256*,
    laplace_digest256*) {
    return LAPLACE_FRAMEWORK_OK;
}

void ActivationAbort(
    void*,
    const laplace_framework_activation_request*,
    const laplace_digest256*) {}

laplace_framework_activation_provider_v1 ActivationProvider() {
    return laplace_framework_activation_provider_v1{
        nullptr,
        ActivationPrepare,
        ActivationCommit,
        ActivationAbort,
        LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR,
        LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR,
        0,
        0};
}

bool Activate(
    const laplace_framework_context& context,
    const laplace_digest256& expected,
    const laplace_digest256& next) {
    laplace_framework_stream_receipt staged{};
    if (!StageZeroStream(context, &staged)) {
        return false;
    }
    laplace_framework_activation_request request{};
    request.expected_epoch = expected;
    request.next_epoch = next;
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_IDENTITY;
    laplace_framework_activation_provider_v1 provider = ActivationProvider();
    laplace_framework_activation_receipt receipt{};
    const laplace_framework_status status =
        laplace_framework_activate_staged_stream(
            &context, &staged, &request, &provider, &receipt);
    return status == LAPLACE_FRAMEWORK_OK &&
        receipt.status == LAPLACE_FRAMEWORK_OK &&
        receipt.effect_disposition == LAPLACE_FRAMEWORK_EFFECT_ACTIVATED &&
        IsZero(receipt.preparation_fingerprint) &&
        IsZero(receipt.activation_fingerprint);
}

}  // namespace

int main() {
    const laplace_framework_context zero_context = Context(true);
    if (laplace_framework_context_validate(&zero_context) !=
        LAPLACE_FRAMEWORK_OK) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    laplace_framework_context noncanonical_absence = zero_context;
    noncanonical_absence.epochs[LAPLACE_FRAMEWORK_EPOCH_SOURCE].bytes[0] = 1;
    if (laplace_framework_context_validate(&noncanonical_absence) !=
        LAPLACE_FRAMEWORK_CONTEXT_INVALID) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }

    laplace_framework_stream_receipt stream_receipt{};
    if (!StageZeroStream(zero_context, &stream_receipt)) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }

    laplace_framework_producer_receipt producer_receipt{};
    if (!RunZeroProducer(zero_context, nullptr, &producer_receipt)) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    laplace_framework_producer_receipt replay_receipt{};
    if (!RunZeroProducer(
            zero_context, &producer_receipt.checkpoint, &replay_receipt)) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }

    laplace_digest256 zero_epoch{};
    laplace_digest256 nonzero_epoch{};
    nonzero_epoch.bytes[0] = 1;
    if (!Activate(zero_context, zero_epoch, nonzero_epoch)) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    const laplace_framework_context nonzero_context = Context(false);
    if (!Activate(nonzero_context, nonzero_epoch, zero_epoch)) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    return 0;
}
