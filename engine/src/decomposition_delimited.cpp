#include "laplace/decomposition_delimited.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

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
    *applicable = span->depth == 0u &&
        (span->flags & static_cast<std::uint32_t>(
            LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT)) != 0u ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status Apply(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || content == nullptr || span == nullptr ||
        emit == nullptr || span->byte_start >= span->byte_end ||
        span->byte_end > content->byte_count) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const auto& provider =
        *static_cast<const laplace_decomposition_delimited_provider*>(provider_state);
    const std::uint8_t delimiter = static_cast<std::uint8_t>(provider.delimiter);
    const std::size_t begin = static_cast<std::size_t>(span->byte_start);
    const std::size_t end = static_cast<std::size_t>(span->byte_end);
    const std::size_t terminator_bytes =
        provider.terminator == LAPLACE_DECOMPOSITION_DELIMITED_CRLF ? 2u : 1u;
    std::size_t line_start = begin;
    std::uint64_t row_ordinal = 0u;

    while (line_start < end) {
        std::size_t line_feed = line_start;
        while (line_feed < end && content->bytes[line_feed] != '\n') ++line_feed;
        if (line_feed == end) return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        std::size_t content_end = line_feed;
        if (terminator_bytes == 2u) {
            if (line_feed == line_start || content->bytes[line_feed - 1u] != '\r') {
                return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            }
            content_end = line_feed - 1u;
        } else if (line_feed > line_start && content->bytes[line_feed - 1u] == '\r') {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }

        const bool header = row_ordinal < provider.header_record_count;
        const std::uint64_t row_kind =
            header ? provider.header_record_kind : provider.record_kind;
        if (content_end > line_start &&
            emit(emit_state,
                 static_cast<std::uint64_t>(line_start),
                 static_cast<std::uint64_t>(content_end),
                 row_kind,
                 static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT)) != 0) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }

        std::size_t field_start = line_start;
        std::uint32_t column_count = 0u;
        for (std::size_t cursor = line_start; cursor <= content_end; ++cursor) {
            const bool at_end = cursor == content_end;
            if (!at_end && content->bytes[cursor] != delimiter) continue;
            if (cursor > field_start &&
                emit(emit_state,
                     static_cast<std::uint64_t>(field_start),
                     static_cast<std::uint64_t>(cursor),
                     provider.field_kind,
                     static_cast<std::uint32_t>(
                         LAPLACE_DECOMPOSITION_SPAN_TEXT |
                         LAPLACE_DECOMPOSITION_SPAN_REDISPATCH)) != 0) {
                return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            }
            ++column_count;
            if (!at_end) {
                if (emit(emit_state,
                         static_cast<std::uint64_t>(cursor),
                         static_cast<std::uint64_t>(cursor + 1u),
                         provider.delimiter_kind,
                         0u) != 0) {
                    return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                }
                field_start = cursor + 1u;
            }
        }
        if (column_count != provider.expected_column_count) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
        const std::uint64_t terminator_start = static_cast<std::uint64_t>(content_end);
        const std::uint64_t terminator_end = static_cast<std::uint64_t>(line_feed + 1u);
        if (emit(emit_state,
                 terminator_start,
                 terminator_end,
                 provider.terminator_kind,
                 0u) != 0) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
        line_start = line_feed + 1u;
        if (row_ordinal == std::numeric_limits<std::uint64_t>::max()) {
            return LAPLACE_DECOMPOSITION_LIMIT_EXCEEDED;
        }
        ++row_ordinal;
    }
    return LAPLACE_DECOMPOSITION_OK;
}

}  // namespace

extern "C" laplace_decomposition_status laplace_decomposition_delimited_provider_init(
    laplace_decomposition_delimited_provider* storage,
    const std::uint32_t delimiter,
    const std::uint32_t terminator,
    const std::uint32_t expected_column_count,
    const std::uint32_t header_record_count,
    const std::uint64_t kind_base,
    const laplace_digest256* provider_fingerprint) {
    if (storage == nullptr || provider_fingerprint == nullptr ||
        DigestZero(*provider_fingerprint) || delimiter == 0u || delimiter > 0x7fu ||
        delimiter == '\r' || delimiter == '\n' ||
        (terminator != LAPLACE_DECOMPOSITION_DELIMITED_LF &&
         terminator != LAPLACE_DECOMPOSITION_DELIMITED_CRLF) ||
        expected_column_count == 0u || expected_column_count > 65535u ||
        header_record_count > 1u || kind_base == 0u ||
        (kind_base & UINT64_C(0xFFFF)) != 0u) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    std::memset(storage, 0, sizeof(*storage));
    storage->record_kind = kind_base | UINT64_C(1);
    storage->header_record_kind = kind_base | UINT64_C(2);
    storage->field_kind = kind_base | UINT64_C(3);
    storage->delimiter_kind = kind_base | UINT64_C(4);
    storage->terminator_kind = kind_base | UINT64_C(5);
    storage->delimiter = delimiter;
    storage->terminator = terminator;
    storage->expected_column_count = expected_column_count;
    storage->header_record_count = header_record_count;
    storage->provider.state = storage;
    storage->provider.provider_fingerprint = *provider_fingerprint;
    storage->provider.applicable = Applicable;
    storage->provider.apply = Apply;
    storage->provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    storage->provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return LAPLACE_DECOMPOSITION_OK;
}
