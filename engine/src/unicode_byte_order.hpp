#ifndef LAPLACE_UNICODE_BYTE_ORDER_HPP
#define LAPLACE_UNICODE_BYTE_ORDER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace laplace::internal {

// Order serialized payloads by unsigned bytes, including embedded zeroes.
// Keep this explicit rather than using vector's C++20 three-way comparison.
inline bool UnicodePayloadBytesLess(
    const std::vector<std::uint8_t>& left,
    const std::vector<std::uint8_t>& right) noexcept {
    const std::size_t common = std::min(left.size(), right.size());
    for (std::size_t index = 0u; index < common; ++index) {
        if (left[index] != right[index]) {
            return left[index] < right[index];
        }
    }
    return left.size() < right.size();
}

}  // namespace laplace::internal

#endif
