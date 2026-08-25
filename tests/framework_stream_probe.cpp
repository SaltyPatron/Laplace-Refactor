#include "laplace/framework.h"
#include "context_fixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

void Fill(laplace_digest256* digest, std::uint8_t value) {
    std::memset(digest->bytes, value, sizeof(digest->bytes));
}

struct SinkState final {
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
    state->sealed = true;
    *artifact_fingerprint = *stream_fingerprint;
    return LAPLACE_FRAMEWORK_OK;
}

void Abort(void* opaque) {
    *static_cast<SinkState*>(opaque) = SinkState{};
}

laplace_framework_stream_receipt Run(
    const laplace_framework_context* context,
    std::uint8_t source_seed,
    std::uint8_t recipe_seed,
    laplace_framework_status* status,
    bool* sink_begun) {
    const std::array<std::uint8_t, 3> bytes{{1u, 2u, 3u}};
    const laplace_framework_canonical_batch batch{
        bytes.data(),
        bytes.size(),
        1u,
        0u,
        LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
    laplace_framework_canonical_stream stream{};
    stream.batches = &batch;
    stream.batch_count = 1u;
    stream.flags = LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS;
    Fill(&stream.source_fingerprint, source_seed);
    Fill(&stream.recipe_fingerprint, recipe_seed);
    SinkState state{};
    laplace_framework_sink_v1 sink{
        &state,
        Begin,
        Stage,
        Seal,
        Abort,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR,
        0u,
        0u};
    laplace_framework_stream_receipt receipt{};
    *status = laplace_framework_stage_canonical_stream(
        context, &stream, &sink, 1u, &receipt);
    *sink_begun = state.begun;
    return receipt;
}

}  // namespace

int main() {
    const auto context = laplace_test_context(0u);
    const laplace_digest256 zero_digest{};
    laplace_framework_status status_a{};
    laplace_framework_status status_b{};
    laplace_framework_status status_missing{};
    laplace_framework_status status_recipe{};
    bool begun_a = false;
    bool begun_b = false;
    bool begun_missing = false;
    bool begun_recipe = false;
    const auto receipt_a = Run(
        &context, UINT8_C(0xc0), UINT8_C(0xe0), &status_a, &begun_a);
    const auto receipt_b = Run(
        &context, UINT8_C(0xc1), UINT8_C(0xe0), &status_b, &begun_b);
    const auto receipt_recipe = Run(
        &context, UINT8_C(0xc0), UINT8_C(0xe1),
        &status_recipe, &begun_recipe);
    const auto receipt_missing =
        Run(&context, UINT8_C(0), UINT8_C(0xe0),
            &status_missing, &begun_missing);

    if (status_a != LAPLACE_FRAMEWORK_OK ||
        status_b != LAPLACE_FRAMEWORK_OK ||
        status_recipe != LAPLACE_FRAMEWORK_OK ||
        !begun_a || !begun_b || !begun_recipe ||
        receipt_a.effect_disposition != LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT ||
        receipt_b.effect_disposition != LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT ||
        std::memcmp(receipt_a.stream_fingerprint.bytes,
                    receipt_b.stream_fingerprint.bytes,
                    sizeof(receipt_a.stream_fingerprint.bytes)) != 0 ||
        std::memcmp(receipt_a.source_fingerprint.bytes,
                    receipt_b.source_fingerprint.bytes,
                    sizeof(receipt_a.source_fingerprint.bytes)) == 0 ||
        std::memcmp(receipt_a.receipt_id.bytes,
                    receipt_b.receipt_id.bytes,
                    sizeof(receipt_a.receipt_id.bytes)) == 0 ||
        std::memcmp(receipt_a.recipe_fingerprint.bytes,
                    receipt_recipe.recipe_fingerprint.bytes,
                    sizeof(receipt_a.recipe_fingerprint.bytes)) == 0 ||
        std::memcmp(receipt_a.receipt_id.bytes,
                    receipt_recipe.receipt_id.bytes,
                    sizeof(receipt_a.receipt_id.bytes)) == 0 ||
        status_missing != LAPLACE_FRAMEWORK_OK || !begun_missing ||
        receipt_missing.effect_disposition !=
            LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT ||
        std::memcmp(receipt_missing.source_fingerprint.bytes,
                    zero_digest.bytes,
                    sizeof(receipt_missing.source_fingerprint.bytes)) != 0 ||
        std::memcmp(receipt_missing.receipt_id.bytes,
                    receipt_a.receipt_id.bytes,
                    sizeof(receipt_missing.receipt_id.bytes)) == 0) {
        std::fputs("framework-stream-binding\n", stderr);
        return 2;
    }
    return 0;
}
