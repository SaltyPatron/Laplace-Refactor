#include "laplace/identity.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::array<std::uint8_t, 16> Bytes(const laplace_id128& id) {
    std::array<std::uint8_t, 16> result{};
    std::memcpy(result.data(), id.bytes, result.size());
    return result;
}

laplace_id128 Codepoint(std::uint32_t position) {
    laplace_id128 id{};
    EXPECT_EQ(laplace_identity_codepoint(position, &id), LAPLACE_IDENTITY_OK);
    return id;
}

std::array<std::uint8_t, 16> Hex(std::string_view text) {
    auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        return static_cast<std::uint8_t>(value - 'a' + 10);
    };
    std::array<std::uint8_t, 16> result{};
    EXPECT_EQ(text.size(), result.size() * 2u);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(nibble(text[index * 2u])) << 4u |
            nibble(text[index * 2u + 1u]));
    }
    return result;
}

TEST(IdentityAbi, IsExactly128Bits) {
    static_assert(sizeof(laplace_id128) == 16);
    EXPECT_EQ(sizeof(laplace_id128), 16u);
}

TEST(UnicodePositionEncoding, CoversTheCompletePositionUniverse) {
    struct Case {
        std::uint32_t position;
        std::array<std::uint8_t, 4> bytes;
        std::size_t length;
    };
    const std::array<Case, 10> cases{{
        {0x000000u, {0x00u, 0x00u, 0x00u, 0x00u}, 1u},
        {0x00007fu, {0x7fu, 0x00u, 0x00u, 0x00u}, 1u},
        {0x000080u, {0xc2u, 0x80u, 0x00u, 0x00u}, 2u},
        {0x0007ffu, {0xdfu, 0xbfu, 0x00u, 0x00u}, 2u},
        {0x000800u, {0xe0u, 0xa0u, 0x80u, 0x00u}, 3u},
        {0x00d800u, {0xedu, 0xa0u, 0x80u, 0x00u}, 3u},
        {0x00dfffu, {0xedu, 0xbfu, 0xbfu, 0x00u}, 3u},
        {0x00ffffu, {0xefu, 0xbfu, 0xbfu, 0x00u}, 3u},
        {0x010000u, {0xf0u, 0x90u, 0x80u, 0x80u}, 4u},
        {0x10ffffu, {0xf4u, 0x8fu, 0xbfu, 0xbfu}, 4u}
    }};

    for (const auto& test_case : cases) {
        std::array<std::uint8_t, 4> actual{};
        std::size_t actual_length = 0;
        ASSERT_EQ(laplace_unicode_position_encode(
                      test_case.position, actual.data(), &actual_length),
                  LAPLACE_IDENTITY_OK);
        EXPECT_EQ(actual_length, test_case.length);
        EXPECT_EQ(actual, test_case.bytes);
    }
}

TEST(UnicodePositionEncoding, RejectsOnlyPositionsBeyondTheUniverse) {
    std::array<std::uint8_t, 4> bytes{};
    std::size_t length = 0;
    EXPECT_EQ(laplace_unicode_position_encode(0x110000u, bytes.data(), &length),
              LAPLACE_IDENTITY_POSITION_OUT_OF_RANGE);
    EXPECT_EQ(laplace_unicode_position_encode(0u, nullptr, &length),
              LAPLACE_IDENTITY_INVALID_ARGUMENT);
    EXPECT_EQ(laplace_unicode_position_encode(0u, bytes.data(), nullptr),
              LAPLACE_IDENTITY_INVALID_ARGUMENT);
}

TEST(UnicodePositionEncoding, EnumeratesTheCompleteUniverseWithoutObservedCollision) {
    using Encoding = std::array<std::uint8_t, 5>;
    using Identity = std::array<std::uint8_t, LAPLACE_IDENTITY_BYTES>;

    std::vector<Encoding> encodings;
    std::vector<Identity> identities;
    encodings.reserve(LAPLACE_UNICODE_POSITION_COUNT);
    identities.reserve(LAPLACE_UNICODE_POSITION_COUNT);

    for (std::uint32_t position = 0;
         position <= LAPLACE_UNICODE_POSITION_MAXIMUM;
         ++position) {
        Encoding encoding{};
        std::size_t encoded_length = 0;
        ASSERT_EQ(laplace_unicode_position_encode(
                      position, encoding.data(), &encoded_length),
                  LAPLACE_IDENTITY_OK)
            << "position=" << position;
        ASSERT_GE(encoded_length, 1u);
        ASSERT_LE(encoded_length, 4u);
        encoding[4] = static_cast<std::uint8_t>(encoded_length);
        encodings.push_back(encoding);

        laplace_id128 identity{};
        ASSERT_EQ(laplace_identity_codepoint(position, &identity),
                  LAPLACE_IDENTITY_OK)
            << "position=" << position;
        identities.push_back(Bytes(identity));
    }

    ASSERT_EQ(encodings.size(), LAPLACE_UNICODE_POSITION_COUNT);
    ASSERT_EQ(identities.size(), LAPLACE_UNICODE_POSITION_COUNT);
    std::sort(encodings.begin(), encodings.end());
    std::sort(identities.begin(), identities.end());
    EXPECT_TRUE(std::adjacent_find(encodings.begin(), encodings.end()) == encodings.end());
    EXPECT_TRUE(std::adjacent_find(identities.begin(), identities.end()) == identities.end());
}

TEST(CodepointIdentity, NullPositionIsNotEmptyContent) {
    const auto id = Bytes(Codepoint(0u));
    const std::array<std::uint8_t, 16> empty_blake3{{
        0xafu, 0x13u, 0x49u, 0xb9u, 0xf5u, 0xf9u, 0xa1u, 0xa6u,
        0xa0u, 0x40u, 0x4du, 0xeau, 0x36u, 0xdcu, 0xc9u, 0x49u
    }};
    EXPECT_NE(id, empty_blake3);
}

TEST(CodepointIdentity, MatchesPinnedCrossRuntimeVectors) {
    EXPECT_EQ(Bytes(Codepoint(0x0032u)), Hex(LAPLACE_VECTOR_U0032_HEX));
    EXPECT_EQ(Bytes(Codepoint(0x0035u)), Hex(LAPLACE_VECTOR_U0035_HEX));
    EXPECT_EQ(Bytes(Codepoint(0xd800u)), Hex(LAPLACE_VECTOR_UD800_HEX));
}

TEST(CompositeIdentity, RejectsEmptyAndCollapsesOneChild) {
    laplace_id128 result{};
    const auto child = Codepoint(static_cast<std::uint32_t>('2'));
    EXPECT_EQ(laplace_identity_composite(nullptr, 0u, &result),
              LAPLACE_IDENTITY_EMPTY_COMPOSITION);
    ASSERT_EQ(laplace_identity_composite(&child, 1u, &result),
              LAPLACE_IDENTITY_OK);
    EXPECT_TRUE(laplace_identity_equal(&child, &result));
}

TEST(CompositeIdentity, OrderChangesTheContentId) {
    const std::array<laplace_id128, 3> forward{{
        Codepoint(static_cast<std::uint32_t>('2')),
        Codepoint(static_cast<std::uint32_t>('5')),
        Codepoint(static_cast<std::uint32_t>('5'))
    }};
    const std::array<laplace_id128, 3> reverse{{forward[2], forward[1], forward[0]}};
    laplace_id128 forward_id{};
    laplace_id128 reverse_id{};
    ASSERT_EQ(laplace_identity_composite(forward.data(), forward.size(), &forward_id),
              LAPLACE_IDENTITY_OK);
    ASSERT_EQ(laplace_identity_composite(reverse.data(), reverse.size(), &reverse_id),
              LAPLACE_IDENTITY_OK);
    EXPECT_FALSE(laplace_identity_equal(&forward_id, &reverse_id));
}

TEST(CompositeIdentity, Numeric255MatchesPinnedCrossRuntimeVector) {
    const std::array<laplace_id128, 3> children{{
        Codepoint(static_cast<std::uint32_t>('2')),
        Codepoint(static_cast<std::uint32_t>('5')),
        Codepoint(static_cast<std::uint32_t>('5'))
    }};
    laplace_id128 id{};
    ASSERT_EQ(laplace_identity_composite(children.data(), children.size(), &id),
              LAPLACE_IDENTITY_OK);
    EXPECT_EQ(Bytes(id), Hex(LAPLACE_VECTOR_255_HEX));
}

TEST(RunIdentity, MatchesTheExpandedSequence) {
    const auto two = Codepoint(static_cast<std::uint32_t>('2'));
    const auto five = Codepoint(static_cast<std::uint32_t>('5'));
    const std::array<laplace_id128, 3> expanded{{two, five, five}};
    const std::array<laplace_id_run, 2> runs{{{two, 1u}, {five, 2u}}};
    laplace_id128 expanded_id{};
    laplace_id128 run_id{};
    std::uint64_t logical_count = 0;
    ASSERT_EQ(laplace_identity_composite(expanded.data(), expanded.size(), &expanded_id),
              LAPLACE_IDENTITY_OK);
    ASSERT_EQ(laplace_identity_composite_runs(
                  runs.data(), runs.size(), &logical_count, &run_id),
              LAPLACE_IDENTITY_OK);
    EXPECT_EQ(logical_count, 3u);
    EXPECT_TRUE(laplace_identity_equal(&expanded_id, &run_id));
}

TEST(RunIdentity, LargeRunMatchesExpandedSequence) {
    constexpr std::size_t count = 100000u;
    const auto blue = Codepoint(static_cast<std::uint32_t>('B'));
    const std::vector<laplace_id128> expanded(count, blue);
    const laplace_id_run run{blue, count};
    laplace_id128 expanded_id{};
    laplace_id128 run_id{};
    std::uint64_t logical_count = 0;
    ASSERT_EQ(laplace_identity_composite(expanded.data(), expanded.size(), &expanded_id),
              LAPLACE_IDENTITY_OK);
    ASSERT_EQ(laplace_identity_composite_runs(&run, 1u, &logical_count, &run_id),
              LAPLACE_IDENTITY_OK);
    EXPECT_EQ(logical_count, count);
    EXPECT_TRUE(laplace_identity_equal(&expanded_id, &run_id));
}

TEST(RunIdentity, RejectsZeroAndOverflowingRuns) {
    const auto child = Codepoint(static_cast<std::uint32_t>('x'));
    laplace_id128 result{};
    std::uint64_t logical_count = 0;
    const laplace_id_run zero{child, 0u};
    EXPECT_EQ(laplace_identity_composite_runs(&zero, 1u, &logical_count, &result),
              LAPLACE_IDENTITY_ZERO_RUN);

    const std::array<laplace_id_run, 2> overflow{{
        {child, std::numeric_limits<std::uint64_t>::max()},
        {child, 1u}
    }};
    EXPECT_EQ(laplace_identity_composite_runs(
                  overflow.data(), overflow.size(), &logical_count, &result),
              LAPLACE_IDENTITY_COUNT_OVERFLOW);
}

TEST(IdentityComparison, UsesCanonicalByteOrder) {
    laplace_id128 low{};
    laplace_id128 high{};
    high.bytes[15] = 1u;
    EXPECT_LT(laplace_identity_compare(&low, &high), 0);
    EXPECT_GT(laplace_identity_compare(&high, &low), 0);
    EXPECT_EQ(laplace_identity_compare(&low, &low), 0);
}

}  // namespace
