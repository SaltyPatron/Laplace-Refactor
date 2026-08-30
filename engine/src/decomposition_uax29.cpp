#include "laplace/decomposition_uax29.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

laplace_decomposition_status Applicable(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *applicable = (span->flags & LAPLACE_DECOMPOSITION_SPAN_TEXT) != 0u ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

struct EmitBridge {
    laplace_decomposition_emit_fn emit;
    void* emit_state;
    std::uint64_t byte_base;
    std::uint64_t kind;
    int failed;
};

int EmitUax(void* opaque, const laplace_uax29_span* span) {
    if (opaque == nullptr || span == nullptr) return 1;
    auto& bridge = *static_cast<EmitBridge*>(opaque);
    if (bridge.failed != 0) return 1;
    if (span->byte_start > UINT64_MAX - bridge.byte_base ||
        span->byte_end > UINT64_MAX - bridge.byte_base) {
        bridge.failed = 1;
        return 1;
    }
    const int result = bridge.emit(
        bridge.emit_state,
        bridge.byte_base + span->byte_start,
        bridge.byte_base + span->byte_end,
        bridge.kind,
        LAPLACE_DECOMPOSITION_SPAN_REDISPATCH |
            LAPLACE_DECOMPOSITION_SPAN_TEXT);
    if (result != 0) bridge.failed = 1;
    return result;
}

laplace_decomposition_status Apply(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || content == nullptr || span == nullptr ||
        emit == nullptr || span->byte_start >= span->byte_end ||
        span->byte_end > content->byte_count ||
        span->byte_end - span->byte_start > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const auto& storage =
        *static_cast<const laplace_decomposition_uax29_provider*>(provider_state);
    const std::size_t count = static_cast<std::size_t>(span->byte_end - span->byte_start);
    const std::uint8_t* bytes = content->bytes + static_cast<std::size_t>(span->byte_start);

    struct Requested {
        laplace_uax29_boundary_kind boundary;
        std::uint64_t kind;
    };
    static constexpr Requested requested[] = {
        {LAPLACE_UAX29_GRAPHEME, LAPLACE_DECOMPOSITION_KIND_UAX29_GRAPHEME},
        {LAPLACE_UAX29_WORD, LAPLACE_DECOMPOSITION_KIND_UAX29_WORD},
        {LAPLACE_UAX29_SENTENCE, LAPLACE_DECOMPOSITION_KIND_UAX29_SENTENCE}
    };
    for (const Requested& request : requested) {
        EmitBridge bridge{emit, emit_state, span->byte_start, request.kind, 0};
        laplace_uax29_summary summary{};
        const laplace_uax29_status status = laplace_uax29_segment(
            storage.tables,
            bytes,
            count,
            request.boundary,
            EmitUax,
            &bridge,
            &summary);
        if (status != LAPLACE_UAX29_OK || bridge.failed != 0) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
    }
    return LAPLACE_DECOMPOSITION_OK;
}

}  // namespace

extern "C" laplace_decomposition_status laplace_decomposition_uax29_provider_init(
    laplace_decomposition_uax29_provider* storage,
    const laplace_uax29_tables* tables,
    const laplace_digest256* provider_fingerprint) {
    if (storage == nullptr || tables == nullptr || provider_fingerprint == nullptr ||
        DigestZero(*provider_fingerprint)) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    std::memset(storage, 0, sizeof(*storage));
    storage->tables = tables;
    storage->provider.state = storage;
    storage->provider.provider_fingerprint = *provider_fingerprint;
    storage->provider.applicable = Applicable;
    storage->provider.apply = Apply;
    storage->provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    storage->provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return LAPLACE_DECOMPOSITION_OK;
}
