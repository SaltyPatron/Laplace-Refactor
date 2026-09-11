#include "laplace/text_identity.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace {

bool DecodeUtf8(
    const std::uint8_t* const bytes,
    const std::size_t byte_count,
    std::vector<std::uint32_t>* const positions) {
    std::size_t offset = 0u;
    while (offset < byte_count) {
        const std::uint8_t first = bytes[offset];
        std::uint32_t value = 0u;
        std::size_t width = 0u;
        std::uint32_t minimum = 0u;
        if (first <= 0x7fu) {
            value = first;
            width = 1u;
        } else if ((first & 0xe0u) == 0xc0u) {
            value = static_cast<std::uint32_t>(first & 0x1fu);
            width = 2u;
            minimum = 0x80u;
        } else if ((first & 0xf0u) == 0xe0u) {
            value = static_cast<std::uint32_t>(first & 0x0fu);
            width = 3u;
            minimum = 0x800u;
        } else if ((first & 0xf8u) == 0xf0u) {
            value = static_cast<std::uint32_t>(first & 0x07u);
            width = 4u;
            minimum = 0x10000u;
        } else {
            return false;
        }
        if (width > byte_count - offset) return false;
        for (std::size_t index = 1u; index < width; ++index) {
            const std::uint8_t continuation = bytes[offset + index];
            if ((continuation & 0xc0u) != 0x80u) return false;
            value = (value << 6u) |
                static_cast<std::uint32_t>(continuation & 0x3fu);
        }
        if (value < minimum || value > 0x10ffffu ||
            (value >= 0xd800u && value <= 0xdfffu)) {
            return false;
        }
        positions->push_back(value);
        offset += width;
    }
    return true;
}

}  // namespace

extern "C" laplace_text_identity_status laplace_text_identity_utf8(
    const std::uint8_t* const bytes,
    const size_t byte_count,
    laplace_text_identity* const identity) {
    if (identity != nullptr) *identity = laplace_text_identity{};
    if (bytes == nullptr || identity == nullptr) {
        return LAPLACE_TEXT_IDENTITY_INVALID_ARGUMENT;
    }
    if (byte_count == 0u) return LAPLACE_TEXT_IDENTITY_EMPTY;
    try {
        std::vector<std::uint32_t> positions;
        positions.reserve(byte_count);
        if (!DecodeUtf8(bytes, byte_count, &positions)) {
            return LAPLACE_TEXT_IDENTITY_UTF8_INVALID;
        }
        if (positions.empty()) return LAPLACE_TEXT_IDENTITY_EMPTY;
        if (positions.size() > static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max())) {
            return LAPLACE_TEXT_IDENTITY_OVERFLOW;
        }
        if (positions.size() == 1u) {
            if (laplace_identity_codepoint_witness(
                    positions[0], &identity->entity_id,
                    &identity->identity_witness) != LAPLACE_IDENTITY_OK) {
                return LAPLACE_TEXT_IDENTITY_IDENTITY_FAILURE;
            }
            identity->codepoint_count = 1u;
            return LAPLACE_TEXT_IDENTITY_OK;
        }

        std::vector<laplace_id_run> runs;
        runs.reserve(positions.size());
        laplace_id128 current{};
        if (laplace_identity_codepoint(positions[0], &current) != LAPLACE_IDENTITY_OK) {
            return LAPLACE_TEXT_IDENTITY_IDENTITY_FAILURE;
        }
        std::uint64_t run_count = 1u;
        for (std::size_t index = 1u; index < positions.size(); ++index) {
            laplace_id128 next{};
            if (laplace_identity_codepoint(positions[index], &next) !=
                LAPLACE_IDENTITY_OK) {
                return LAPLACE_TEXT_IDENTITY_IDENTITY_FAILURE;
            }
            if (laplace_identity_equal(&current, &next) != 0) {
                if (run_count == std::numeric_limits<std::uint64_t>::max()) {
                    return LAPLACE_TEXT_IDENTITY_OVERFLOW;
                }
                ++run_count;
            } else {
                runs.push_back(laplace_id_run{current, run_count});
                current = next;
                run_count = 1u;
            }
        }
        runs.push_back(laplace_id_run{current, run_count});

        std::uint64_t logical_count = 0u;
        if (laplace_identity_composite_runs_witness(
                runs.data(), runs.size(), nullptr, &logical_count,
                &identity->entity_id, &identity->identity_witness) !=
                LAPLACE_IDENTITY_OK ||
            logical_count != static_cast<std::uint64_t>(positions.size())) {
            return LAPLACE_TEXT_IDENTITY_IDENTITY_FAILURE;
        }
        identity->codepoint_count = logical_count;
        return LAPLACE_TEXT_IDENTITY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_TEXT_IDENTITY_MEMORY_FAILURE;
    }
}
