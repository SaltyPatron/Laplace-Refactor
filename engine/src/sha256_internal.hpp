#ifndef LAPLACE_ENGINE_SHA256_INTERNAL_HPP
#define LAPLACE_ENGINE_SHA256_INTERNAL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace laplace::internal {

inline constexpr std::array<std::uint32_t, 64> Sha256Constants{{
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u}};

inline std::uint32_t Sha256RotateRight(
    const std::uint32_t value, const std::uint32_t count) {
    return (value >> count) | (value << (32u - count));
}

inline void Sha256Block(
    const std::uint8_t* block,
    std::array<std::uint32_t, 8>& state) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0u; index < 16u; ++index) {
        words[index] = (static_cast<std::uint32_t>(block[index * 4u]) << 24u) |
            (static_cast<std::uint32_t>(block[index * 4u + 1u]) << 16u) |
            (static_cast<std::uint32_t>(block[index * 4u + 2u]) << 8u) |
            static_cast<std::uint32_t>(block[index * 4u + 3u]);
    }
    for (std::size_t index = 16u; index < words.size(); ++index) {
        const std::uint32_t s0 = Sha256RotateRight(words[index - 15u], 7u) ^
            Sha256RotateRight(words[index - 15u], 18u) ^
            (words[index - 15u] >> 3u);
        const std::uint32_t s1 = Sha256RotateRight(words[index - 2u], 17u) ^
            Sha256RotateRight(words[index - 2u], 19u) ^
            (words[index - 2u] >> 10u);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t index = 0u; index < words.size(); ++index) {
        const std::uint32_t upper = Sha256RotateRight(e, 6u) ^
            Sha256RotateRight(e, 11u) ^ Sha256RotateRight(e, 25u);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 = h + upper + choose +
            Sha256Constants[index] + words[index];
        const std::uint32_t lower = Sha256RotateRight(a, 2u) ^
            Sha256RotateRight(a, 13u) ^ Sha256RotateRight(a, 22u);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = lower + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

inline std::array<std::uint8_t, 32> Sha256(
    const std::uint8_t* bytes, const std::size_t byte_count) {
    std::array<std::uint32_t, 8> state{{
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u}};
    std::size_t offset = 0u;
    while (byte_count - offset >= 64u) {
        Sha256Block(bytes + offset, state);
        offset += 64u;
    }
    std::array<std::uint8_t, 128> tail{};
    const std::size_t remainder = byte_count - offset;
    if (remainder != 0u) {
        std::memcpy(tail.data(), bytes + offset, remainder);
    }
    tail[remainder] = 0x80u;
    const std::size_t padded = remainder < 56u ? 64u : 128u;
    const std::uint64_t bit_count =
        static_cast<std::uint64_t>(byte_count) * 8u;
    for (std::size_t index = 0u; index < 8u; ++index) {
        tail[padded - 1u - index] =
            static_cast<std::uint8_t>(bit_count >> (index * 8u));
    }
    Sha256Block(tail.data(), state);
    if (padded == 128u) {
        Sha256Block(tail.data() + 64u, state);
    }
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t index = 0u; index < state.size(); ++index) {
        digest[index * 4u] = static_cast<std::uint8_t>(state[index] >> 24u);
        digest[index * 4u + 1u] = static_cast<std::uint8_t>(state[index] >> 16u);
        digest[index * 4u + 2u] = static_cast<std::uint8_t>(state[index] >> 8u);
        digest[index * 4u + 3u] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

}  // namespace laplace::internal

#endif
