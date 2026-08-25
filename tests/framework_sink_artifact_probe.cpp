#include "laplace/framework.h"
#include "context_fixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

struct SinkState final {
    std::uint8_t tag{};
    bool begun{};
    bool staged{};
    bool sealed{};
};

laplace_framework_status Begin(
    void* opaque,
    const laplace_framework_context*,
    std::uint32_t,
    std::uint64_t,
    std::uint64_t) {
    static_cast<SinkState*>(opaque)->begun = true;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Stage(
    void* opaque,
    const laplace_framework_canonical_batch*) {
    auto* state = static_cast<SinkState*>(opaque);
    if (!state->begun) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    state->staged = true;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status Seal(
    void* opaque,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    auto* state = static_cast<SinkState*>(opaque);
    if (!state->staged) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    *artifact_fingerprint = *stream_fingerprint;
    artifact_fingerprint->bytes[0] = static_cast<std::uint8_t>(
        artifact_fingerprint->bytes[0] ^ state->tag);
    state->sealed = true;
    return LAPLACE_FRAMEWORK_OK;
}

void Abort(void* opaque) {
    auto* state = static_cast<SinkState*>(opaque);
    state->begun = false;
    state->staged = false;
    state->sealed = false;
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
        0u,
        0u};
}

void Fill(laplace_digest256* digest, std::uint8_t value) {
    std::memset(digest->bytes, value, sizeof(digest->bytes));
}

}  // namespace

int main() {
    const auto context = laplace_test_context(0u);
    const std::array<std::uint8_t, 3> bytes{{1u, 2u, 3u}};
    const laplace_framework_canonical_batch batch{
        bytes.data(), bytes.size(), 1u, 0u, LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    laplace_framework_canonical_stream stream{};
    stream.batches = &batch;
    stream.batch_count = 1u;
    stream.flags = LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS;
    Fill(&stream.source_fingerprint, 0xc0u);
    Fill(&stream.recipe_fingerprint, 0xe0u);

    SinkState left{0x11u};
    SinkState right{0x22u};
    std::array<laplace_framework_sink_v1, 2> sinks{{Sink(&left), Sink(&right)}};
    std::array<laplace_digest256, 2> artifacts{};
    laplace_framework_sink_artifact_output output{
        artifacts.data(), artifacts.size(), 0u, 0u};
    laplace_framework_stream_receipt receipt{};
    const auto status = laplace_framework_stage_canonical_stream_with_artifacts(
        &context, &stream, sinks.data(), sinks.size(), &output, &receipt);

    auto expected_left = receipt.stream_fingerprint;
    auto expected_right = receipt.stream_fingerprint;
    expected_left.bytes[0] = static_cast<std::uint8_t>(
        expected_left.bytes[0] ^ left.tag);
    expected_right.bytes[0] = static_cast<std::uint8_t>(
        expected_right.bytes[0] ^ right.tag);
    laplace_digest256 aggregate{};
    const auto aggregate_status = laplace_framework_sink_artifacts_fingerprint(
        artifacts.data(), artifacts.size(), &aggregate);

    if (status != LAPLACE_FRAMEWORK_OK || output.count != artifacts.size() ||
        !left.sealed || !right.sealed ||
        std::memcmp(
            artifacts[0].bytes, expected_left.bytes,
            sizeof(expected_left.bytes)) != 0 ||
        std::memcmp(
            artifacts[1].bytes, expected_right.bytes,
            sizeof(expected_right.bytes)) != 0 ||
        aggregate_status != LAPLACE_FRAMEWORK_OK ||
        std::memcmp(
            aggregate.bytes, receipt.sink_artifacts_fingerprint.bytes,
            sizeof(aggregate.bytes)) != 0) {
        std::fputs("framework-sink-artifact-output\n", stderr);
        return 2;
    }
    return 0;
}
