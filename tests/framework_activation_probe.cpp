#include "laplace/framework.h"
#include "context_fixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

void Fill(laplace_digest256* digest, std::uint8_t value) {
    std::memset(digest->bytes, value, sizeof(digest->bytes));
}

struct State final {
    std::uint32_t prepare_count{};
};

laplace_framework_status Begin(
    void*, const laplace_framework_context*, std::uint32_t,
    std::uint64_t, std::uint64_t) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Stage(
    void*, const laplace_framework_canonical_batch*) {
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Seal(
    void*, const laplace_digest256* stream,
    laplace_digest256* artifact) {
    *artifact = *stream;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortSink(void*) {}

laplace_framework_status Prepare(
    void* opaque,
    const laplace_framework_context*,
    const laplace_framework_stream_receipt*,
    const laplace_framework_activation_request*,
    laplace_digest256* preparation) {
    ++static_cast<State*>(opaque)->prepare_count;
    Fill(preparation, UINT8_C(0xa0));
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Commit(
    void*,
    const laplace_framework_activation_request*,
    const laplace_digest256*,
    laplace_digest256* activation) {
    Fill(activation, UINT8_C(0xb0));
    return LAPLACE_FRAMEWORK_OK;
}

void AbortActivation(
    void*, const laplace_framework_activation_request*,
    const laplace_digest256*) {}

}  // namespace

int main() {
    auto context = laplace_test_context(UINT8_C(0));
    context.flags = UINT32_C(0);
    const std::array<std::uint8_t, 1> bytes{{UINT8_C(1)}};
    const laplace_framework_canonical_batch batch{
        bytes.data(), bytes.size(), UINT64_C(1), UINT64_C(0),
        LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    laplace_framework_canonical_stream stream{};
    stream.batches = &batch;
    stream.batch_count = UINT64_C(1);
    stream.flags = LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS;
    Fill(&stream.source_fingerprint, UINT8_C(0xc0));
    Fill(&stream.recipe_fingerprint, UINT8_C(0xe0));
    laplace_framework_sink_v1 sink{
        nullptr, Begin, Stage, Seal, AbortSink,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR, UINT32_C(0), UINT32_C(0)};
    laplace_framework_stream_receipt staged{};
    if (laplace_framework_stage_canonical_stream(
            &context, &stream, &sink, 1u, &staged) != LAPLACE_FRAMEWORK_OK) {
        return 3;
    }

    State state{};
    laplace_framework_activation_provider_v1 provider{
        &state, Prepare, Commit, AbortActivation,
        LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR,
        LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR,
        UINT32_C(0), UINT32_C(0)};
    laplace_framework_activation_request request{};
    request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_DATABASE;
    Fill(&request.expected_epoch, UINT8_C(0x33));
    Fill(&request.next_epoch, UINT8_C(0x44));
    laplace_framework_activation_receipt receipt{};
    const auto status = laplace_framework_activate_staged_stream(
        &context, &staged, &request, &provider, &receipt);
    if (status != LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID ||
        state.prepare_count != 0u ||
        receipt.effect_disposition != LAPLACE_FRAMEWORK_EFFECT_NONE) {
        std::fputs("framework-activation-stale-epoch\n", stderr);
        return 2;
    }
    return 0;
}
