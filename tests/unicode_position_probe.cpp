#include "laplace/identity.h"

#include <array>
#include <cstdint>
#include <cstdio>

int main() {
    std::array<std::uint8_t, 4> bytes{};
    std::size_t length = 0;
    const auto status = laplace_unicode_position_encode(
        UINT32_C(0xd800), bytes.data(), &length);
    if (status != LAPLACE_IDENTITY_OK) {
        std::fputs("unicode-surrogate-rejected\n", stderr);
        return 2;
    }
    const std::array<std::uint8_t, 4> expected{{0xedu, 0xa0u, 0x80u, 0x00u}};
    if (length != 3u || bytes != expected) {
        std::fputs("unicode-surrogate-encoding-mismatch\n", stderr);
        return 3;
    }
    return 0;
}
