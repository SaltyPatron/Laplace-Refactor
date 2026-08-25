#include "laplace/identity.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

std::array<std::uint8_t, 16> Hex(std::string_view text) {
    auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        return static_cast<std::uint8_t>(value - 'a' + 10);
    };
    std::array<std::uint8_t, 16> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(nibble(text[index * 2u])) << 4u |
            nibble(text[index * 2u + 1u]));
    }
    return result;
}

bool Matches(const laplace_id128& actual, std::string_view expected) {
    const auto bytes = Hex(expected);
    return std::memcmp(actual.bytes, bytes.data(), bytes.size()) == 0;
}

}  // namespace

int main() {
    laplace_id128 two{};
    laplace_id128 five{};
    laplace_id128 composite{};
    if (laplace_identity_codepoint(static_cast<std::uint32_t>('2'), &two) !=
            LAPLACE_IDENTITY_OK ||
        laplace_identity_codepoint(static_cast<std::uint32_t>('5'), &five) !=
            LAPLACE_IDENTITY_OK) {
        std::puts("contract-probe-codepoint-error");
        return 3;
    }
    const std::array<laplace_id128, 3> children{{two, five, five}};
    if (laplace_identity_composite(children.data(), children.size(), &composite) !=
        LAPLACE_IDENTITY_OK) {
        std::puts("contract-probe-composite-error");
        return 4;
    }
    if (!Matches(two, LAPLACE_VECTOR_U0032_HEX) ||
        !Matches(five, LAPLACE_VECTOR_U0035_HEX) ||
        !Matches(composite, LAPLACE_VECTOR_255_HEX)) {
        std::puts("contract-vector-mismatch");
        return 2;
    }
    std::puts("contract-vectors-match");
    return 0;
}
