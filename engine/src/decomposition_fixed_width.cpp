#include "laplace/decomposition_fixed_width.h"

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

bool EmitSpan(
    laplace_decomposition_emit_fn emit,
    void* emit_state,
    const std::size_t begin,
    const std::size_t end,
    const std::uint64_t kind,
    const std::uint32_t flags) {
    return begin < end &&
        emit(emit_state,
             static_cast<std::uint64_t>(begin),
             static_cast<std::uint64_t>(end),
             kind,
             flags) == 0;
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
        *static_cast<const laplace_decomposition_fixed_width_provider*>(
            provider_state);
    const std::size_t begin = static_cast<std::size_t>(span->byte_start);
    const std::size_t end = static_cast<std::size_t>(span->byte_end);
    const std::size_t terminator_bytes =
        provider.terminator == LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF ? 2u : 1u;
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

        const std::uint64_t physical_width =
            static_cast<std::uint64_t>(content_end - line_start);
        if (physical_width < provider.nominal_record_bytes) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
        const std::uint64_t overflow =
            physical_width - provider.nominal_record_bytes;
        if (overflow > provider.maximum_overflow_bytes ||
            (overflow != 0u &&
             provider.overflow_field_index ==
                 LAPLACE_DECOMPOSITION_FIXED_WIDTH_NO_OVERFLOW_FIELD)) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }

        const std::uint64_t row_kind =
            row_ordinal < provider.header_record_count
                ? provider.header_record_kind
                : provider.record_kind;
        if (!EmitSpan(emit, emit_state, line_start, content_end, row_kind,
                      LAPLACE_DECOMPOSITION_SPAN_TEXT)) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }

        std::size_t field_start = line_start;
        for (std::uint32_t field_index = 0u;
             field_index < provider.field_count;
             ++field_index) {
            const auto& declaration = provider.fields[field_index];
            std::uint64_t field_width = declaration.width;
            if (field_index == provider.overflow_field_index) {
                field_width += overflow;
            }
            if (field_width > static_cast<std::uint64_t>(content_end - field_start)) {
                return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            }
            const std::size_t field_end =
                field_start + static_cast<std::size_t>(field_width);
            if (!EmitSpan(emit, emit_state, field_start, field_end,
                          provider.field_kind, 0u)) {
                return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            }

            std::size_t value_start = field_start;
            std::size_t value_end = field_end;
            if ((declaration.flags &
                 LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_LEFT) != 0u) {
                while (value_start < value_end &&
                       content->bytes[value_start] == provider.padding_byte) {
                    ++value_start;
                }
            }
            if ((declaration.flags &
                 LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_RIGHT) != 0u) {
                while (value_end > value_start &&
                       content->bytes[value_end - 1u] == provider.padding_byte) {
                    --value_end;
                }
            }
            if (value_start < value_end &&
                !EmitSpan(emit, emit_state, value_start, value_end,
                          provider.value_kind,
                          LAPLACE_DECOMPOSITION_SPAN_TEXT |
                              LAPLACE_DECOMPOSITION_SPAN_REDISPATCH)) {
                return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            }
            if (field_index == provider.overflow_field_index && overflow != 0u) {
                const std::size_t overflow_start =
                    field_end - static_cast<std::size_t>(overflow);
                if (!EmitSpan(emit, emit_state, overflow_start, field_end,
                              provider.overflow_kind, 0u)) {
                    return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                }
            }
            field_start = field_end;
        }
        if (field_start != content_end ||
            !EmitSpan(emit, emit_state, content_end, line_feed + 1u,
                      provider.terminator_kind, 0u)) {
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

extern "C" laplace_decomposition_status
laplace_decomposition_fixed_width_provider_init(
    laplace_decomposition_fixed_width_provider* storage,
    const laplace_decomposition_fixed_width_field* fields,
    const std::uint32_t field_count,
    const std::uint32_t header_record_count,
    const std::uint32_t terminator,
    const std::uint32_t padding_byte,
    const std::uint32_t overflow_field_index,
    const std::uint32_t maximum_overflow_bytes,
    const std::uint64_t kind_base,
    const laplace_digest256* provider_fingerprint) {
    if (storage == nullptr || fields == nullptr || provider_fingerprint == nullptr ||
        DigestZero(*provider_fingerprint) || field_count == 0u ||
        field_count > 65535u || header_record_count > 1u ||
        (terminator != LAPLACE_DECOMPOSITION_FIXED_WIDTH_LF &&
         terminator != LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF) ||
        padding_byte > 0x7fu || kind_base == 0u ||
        (kind_base & UINT64_C(0xFFFF)) != 0u ||
        (overflow_field_index !=
             LAPLACE_DECOMPOSITION_FIXED_WIDTH_NO_OVERFLOW_FIELD &&
         overflow_field_index >= field_count) ||
        ((overflow_field_index ==
              LAPLACE_DECOMPOSITION_FIXED_WIDTH_NO_OVERFLOW_FIELD) !=
         (maximum_overflow_bytes == 0u))) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }

    std::uint64_t nominal_record_bytes = 0u;
    for (std::uint32_t index = 0u; index < field_count; ++index) {
        if (fields[index].width == 0u ||
            (fields[index].flags &
             ~LAPLACE_DECOMPOSITION_FIXED_WIDTH_KNOWN_FIELD_FLAGS) != 0u ||
            nominal_record_bytes >
                std::numeric_limits<std::uint64_t>::max() - fields[index].width) {
            return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
        }
        nominal_record_bytes += fields[index].width;
    }
    if (nominal_record_bytes == 0u ||
        nominal_record_bytes >
            std::numeric_limits<std::uint64_t>::max() - maximum_overflow_bytes) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }

    std::memset(storage, 0, sizeof(*storage));
    storage->fields = fields;
    storage->record_kind = kind_base | UINT64_C(1);
    storage->header_record_kind = kind_base | UINT64_C(2);
    storage->field_kind = kind_base | UINT64_C(3);
    storage->value_kind = kind_base | UINT64_C(4);
    storage->terminator_kind = kind_base | UINT64_C(5);
    storage->overflow_kind = kind_base | UINT64_C(6);
    storage->nominal_record_bytes = nominal_record_bytes;
    storage->field_count = field_count;
    storage->header_record_count = header_record_count;
    storage->terminator = terminator;
    storage->padding_byte = padding_byte;
    storage->overflow_field_index = overflow_field_index;
    storage->maximum_overflow_bytes = maximum_overflow_bytes;
    storage->provider.state = storage;
    storage->provider.provider_fingerprint = *provider_fingerprint;
    storage->provider.applicable = Applicable;
    storage->provider.apply = Apply;
    storage->provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    storage->provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return LAPLACE_DECOMPOSITION_OK;
}
