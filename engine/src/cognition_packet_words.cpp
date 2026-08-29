#include "laplace/cognition_packet.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace {

bool ByteCount(const std::size_t word_count, std::size_t* const byte_count) {
    if (byte_count == nullptr ||
        word_count > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
        return false;
    }
    *byte_count = word_count * sizeof(std::uint32_t);
    return true;
}

void WordsToCanonicalBytes(
    const std::uint32_t* const words,
    const std::size_t word_count,
    std::uint8_t* const bytes) {
    for (std::size_t index = 0U; index < word_count; ++index) {
        const std::uint32_t word = words[index];
        const std::size_t offset = index * 4U;
        bytes[offset] = static_cast<std::uint8_t>(word);
        bytes[offset + 1U] = static_cast<std::uint8_t>(word >> 8U);
        bytes[offset + 2U] = static_cast<std::uint8_t>(word >> 16U);
        bytes[offset + 3U] = static_cast<std::uint8_t>(word >> 24U);
    }
}

void CanonicalBytesToWords(
    const std::uint8_t* const bytes,
    const std::size_t word_count,
    std::uint32_t* const words) {
    for (std::size_t index = 0U; index < word_count; ++index) {
        const std::size_t offset = index * 4U;
        words[index] = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }
}

}  // namespace

extern "C" laplace_cognition_packet_status
laplace_cognition_packet_required_result_words(
    const std::uint32_t* const request_words,
    const std::size_t request_word_count,
    std::size_t* const required_result_words) {
    if (request_words == nullptr || request_word_count == 0U ||
        required_result_words == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    *required_result_words = 0U;
    std::size_t request_bytes = 0U;
    if (!ByteCount(request_word_count, &request_bytes)) {
        return LAPLACE_COGNITION_PACKET_RANGE;
    }
    try {
        std::vector<std::uint8_t> canonical_request(request_bytes);
        WordsToCanonicalBytes(
            request_words, request_word_count, canonical_request.data());
        std::size_t result_bytes = 0U;
        const auto status = laplace_cognition_packet_required_result_bytes(
            canonical_request.data(), canonical_request.size(), &result_bytes);
        if (status != LAPLACE_COGNITION_PACKET_OK) return status;
        if ((result_bytes & 3U) != 0U) return LAPLACE_COGNITION_PACKET_RANGE;
        *required_result_words = result_bytes / 4U;
        return LAPLACE_COGNITION_PACKET_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
}

extern "C" laplace_cognition_packet_status
laplace_cognition_packet_execute_words(
    const std::uint32_t* const request_words,
    const std::size_t request_word_count,
    std::uint32_t* const result_words,
    const std::size_t result_word_capacity,
    std::size_t* const result_word_count) {
    if (request_words == nullptr || request_word_count == 0U ||
        result_words == nullptr || result_word_count == nullptr) {
        return LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT;
    }
    *result_word_count = 0U;
    std::size_t request_bytes = 0U;
    std::size_t result_capacity_bytes = 0U;
    if (!ByteCount(request_word_count, &request_bytes) ||
        !ByteCount(result_word_capacity, &result_capacity_bytes)) {
        return LAPLACE_COGNITION_PACKET_RANGE;
    }
    try {
        std::vector<std::uint8_t> canonical_request(request_bytes);
        std::vector<std::uint8_t> canonical_result(result_capacity_bytes);
        WordsToCanonicalBytes(
            request_words, request_word_count, canonical_request.data());
        std::size_t result_bytes = 0U;
        const auto status = laplace_cognition_packet_execute(
            canonical_request.data(), canonical_request.size(),
            canonical_result.data(), canonical_result.size(), &result_bytes);
        if (status != LAPLACE_COGNITION_PACKET_OK) return status;
        if ((result_bytes & 3U) != 0U || result_bytes > canonical_result.size()) {
            return LAPLACE_COGNITION_PACKET_RANGE;
        }
        const std::size_t words = result_bytes / 4U;
        CanonicalBytesToWords(canonical_result.data(), words, result_words);
        *result_word_count = words;
        return LAPLACE_COGNITION_PACKET_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PACKET_MEMORY_FAILURE;
    }
}
