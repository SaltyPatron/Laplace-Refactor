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
    std::uint32_t commit_count{};
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
    void* opaque,
    const laplace_framework_activation_request*,
    const laplace_digest256*,
    laplace_digest256* activation) {
    ++static_cast<State*>(opaque)->commit_count;
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
    const auto retained_stream = staged;
    if (laplace_framework_stream_receipt_identity_validate(&staged) != LAPLACE_FRAMEWORK_OK ||
        std::memcmp(&staged, &retained_stream, sizeof(staged)) != 0 ||
        laplace_framework_stream_receipt_identity_validate(nullptr) != LAPLACE_FRAMEWORK_STREAM_INVALID) {
        std::fputs("framework-stored-stream-identity\n", stderr);
        return 2;
    }
    const std::array<laplace_digest256 laplace_framework_stream_receipt::*, 6> stream_digests{{
        &laplace_framework_stream_receipt::receipt_id,
        &laplace_framework_stream_receipt::context_fingerprint,
        &laplace_framework_stream_receipt::source_fingerprint,
        &laplace_framework_stream_receipt::recipe_fingerprint,
        &laplace_framework_stream_receipt::stream_fingerprint,
        &laplace_framework_stream_receipt::sink_artifacts_fingerprint}};
    for (const auto field : stream_digests) {
        for (std::size_t byte = 0u; byte < sizeof(staged.receipt_id.bytes); ++byte) {
            auto corrupted = staged;
            (corrupted.*field).bytes[byte] ^= UINT8_C(1);
            if (laplace_framework_stream_receipt_identity_validate(&corrupted) != LAPLACE_FRAMEWORK_STREAM_INVALID) {
                std::fputs("framework-stored-stream-digest-corruption\n", stderr);
                return 2;
            }
        }
    }
    for (std::size_t field = 0u; field < 10u; ++field) {
        auto corrupted = staged;
        switch (field) {
            case 0: corrupted.status = LAPLACE_FRAMEWORK_STREAM_INVALID; break;
            case 1: ++corrupted.record_type; break;
            case 2: corrupted.effect_disposition = LAPLACE_FRAMEWORK_EFFECT_NONE; break;
            case 3: ++corrupted.total_records; break;
            case 4: ++corrupted.total_bytes; break;
            case 5: ++corrupted.batch_count; break;
            case 6: ++corrupted.sink_count; break;
            case 7: corrupted.failed_batch_index = 0u; break;
            case 8: corrupted.failed_sink_index = 0u; break;
            default: corrupted.reserved = 1u; break;
        }
        if (laplace_framework_stream_receipt_identity_validate(&corrupted) != LAPLACE_FRAMEWORK_STREAM_INVALID) {
            std::fputs("framework-stored-stream-metadata-corruption\n", stderr);
            return 2;
        }
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

    request.expected_epoch =
        context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE];
    const auto admission_status = laplace_framework_admit_staged_stream(
        &context, &staged, &request, &provider, &receipt);
    if (admission_status != LAPLACE_FRAMEWORK_OK ||
        state.prepare_count != 1u || state.commit_count != 0u ||
        receipt.effect_disposition !=
            LAPLACE_FRAMEWORK_EFFECT_ACTIVATION_ADMITTED) {
        std::fputs("framework-activation-admission-not-inert\n", stderr);
        return 2;
    }
    if (laplace_framework_abort_admitted_stream(
            &context, &request, &provider, &receipt) != LAPLACE_FRAMEWORK_OK) {
        return 3;
    }

    auto other_context = context;
    other_context.authority_fingerprint.bytes[0] ^= UINT8_C(1);
    if (laplace_framework_stream_receipt_identity_validate(&staged) != LAPLACE_FRAMEWORK_OK ||
        laplace_framework_stream_receipt_validate(&other_context, &staged) != LAPLACE_FRAMEWORK_STREAM_INVALID) {
        std::fputs("framework-stored-identity-does-not-authorize-current-context\n", stderr);
        return 2;
    }
    const auto cross_context_status = laplace_framework_admit_staged_stream(
        &other_context, &staged, &request, &provider, &receipt);
    if (cross_context_status != LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID ||
        state.prepare_count != 1u) {
        std::fputs("framework-activation-cross-context\n", stderr);
        return 2;
    }

    // A final committed receipt has a different identity from its admission.
    // Validate stored evidence without another provider call or side effect.
    if (laplace_framework_admit_staged_stream(
            &context, &staged, &request, &provider, &receipt) != LAPLACE_FRAMEWORK_OK) {
        return 3;
    }
    const auto admission = receipt.receipt_id;
    if (laplace_framework_commit_admitted_stream(
            &context, &request, &provider, &receipt) != LAPLACE_FRAMEWORK_OK ||
        std::memcmp(admission.bytes, receipt.receipt_id.bytes, sizeof(admission.bytes)) == 0) {
        std::fputs("framework-distinct-admission-final-receipts\n", stderr);
        return 2;
    }
    const std::array<laplace_digest256, 6> evidence{{
        receipt.context_fingerprint, receipt.staged_receipt_id,
        receipt.preparation_fingerprint, receipt.activation_fingerprint,
        admission, receipt.receipt_id}};
    const auto verify = [](const auto& fields, const auto& input) {
        return laplace_framework_committed_receipts_validate(
            &fields[0], &fields[1], &input, &fields[2], &fields[3],
            &fields[4], &fields[5]);
    };
    if (verify(evidence, request) != LAPLACE_FRAMEWORK_OK) {
        std::fputs("framework-committed-receipt-revalidation\n", stderr);
        return 2;
    }
    for (std::size_t index = 0; index < evidence.size(); ++index) {
        auto corrupted = evidence;
        corrupted[index].bytes[0] ^= UINT8_C(1);
        if (verify(corrupted, request) != LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID) {
            std::fputs("framework-committed-receipt-corruption\n", stderr);
            return 2;
        }
    }
    for (std::size_t index = 0; index < 5; ++index) {
        auto corrupted = request;
        switch (index) {
            case 0: corrupted.expected_epoch.bytes[0] ^= UINT8_C(1); break;
            case 1: corrupted.next_epoch.bytes[0] ^= UINT8_C(1); break;
            case 2: corrupted.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_NUMERIC; break;
            case 3: corrupted.flags = UINT32_C(1); break;
            default: corrupted.reserved = UINT64_C(1); break;
        }
        if (verify(evidence, corrupted) != LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID) {
            std::fputs("framework-committed-request-corruption\n", stderr);
            return 2;
        }
    }
    auto false_final = evidence;
    false_final[5] = admission;
    if (verify(false_final, request) != LAPLACE_FRAMEWORK_ACTIVATION_REQUEST_INVALID ||
        state.prepare_count != 2u || state.commit_count != 1u) {
        std::fputs("framework-committed-proof-changed-activation\n", stderr);
        return 2;
    }
    return 0;
}
