#include "laplace/trajectory.h"

#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

std::uint8_t HexNibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    return static_cast<std::uint8_t>(value - 'a' + 10);
}

laplace_id128 ParseId(const char* hex) {
    laplace_id128 id{};
    for (std::size_t index = 0; index < sizeof(id.bytes); ++index) {
        id.bytes[index] = static_cast<std::uint8_t>(
            (HexNibble(hex[index * 2]) << 4) | HexNibble(hex[index * 2 + 1]));
    }
    return id;
}

std::uint64_t SlotBits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::uint64_t ContentMetadata(
    std::uint8_t tier,
    bool has_atom,
    std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
           (has_atom ? (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) : 0u) |
           (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

TEST(TrajectoryCarrier, AbiAndPinnedDatabaseVectorAreStable) {
    static_assert(sizeof(laplace_trajectory_carrier) == 32u);
    static_assert(LAPLACE_TRAJECTORY_PAYLOAD_BITS == 212u);
    const auto id = ParseId(LAPLACE_TRAJECTORY_VECTOR_ENTITY_HEX);
    laplace_trajectory_payload payload{};
    std::memcpy(payload.lane128, id.bytes, sizeof(payload.lane128));
    payload.ordinal = LAPLACE_TRAJECTORY_VECTOR_ORDINAL;
    payload.run_length = LAPLACE_TRAJECTORY_VECTOR_RUN;
    payload.metadata = LAPLACE_TRAJECTORY_VECTOR_METADATA_BITS;
    laplace_trajectory_carrier carrier{};

    ASSERT_EQ(laplace_trajectory_carrier_encode(&payload, &carrier),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(SlotBits(carrier.slots[0]), LAPLACE_TRAJECTORY_VECTOR_X_BITS);
    EXPECT_EQ(SlotBits(carrier.slots[1]), LAPLACE_TRAJECTORY_VECTOR_Y_BITS);
    EXPECT_EQ(SlotBits(carrier.slots[2]), LAPLACE_TRAJECTORY_VECTOR_Z_BITS);
    EXPECT_EQ(SlotBits(carrier.slots[3]), LAPLACE_TRAJECTORY_VECTOR_M_BITS);
}

TEST(TrajectoryCarrier, ArbitraryTypedPayloadRoundTripsAll212Bits) {
    laplace_trajectory_payload payload{};
    for (std::size_t index = 0; index < sizeof(payload.lane128); ++index) {
        payload.lane128[index] = static_cast<std::uint8_t>(index * 17u + 3u);
    }
    payload.ordinal = 0xabcd;
    payload.run_length = 0xfedc;
    payload.metadata = UINT64_C(0x000fffffffffffff);
    laplace_trajectory_carrier carrier{};
    laplace_trajectory_payload decoded{};

    ASSERT_EQ(laplace_trajectory_carrier_encode(&payload, &carrier),
              LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_carrier_decode(&carrier, &decoded),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(std::memcmp(payload.lane128, decoded.lane128,
                          sizeof(payload.lane128)), 0);
    EXPECT_EQ(decoded.ordinal, payload.ordinal);
    EXPECT_EQ(decoded.run_length, payload.run_length);
    EXPECT_EQ(decoded.metadata, payload.metadata);
}

TEST(TrajectoryCarrier, InvalidExponentAndOversizedMetadataCannotMutateOutput) {
    laplace_trajectory_payload payload{};
    payload.metadata = UINT64_C(1) << LAPLACE_TRAJECTORY_METADATA_BITS;
    laplace_trajectory_carrier carrier{};
    std::memset(&carrier, 0xa5, sizeof(carrier));
    const auto carrier_before = carrier;
    EXPECT_EQ(laplace_trajectory_carrier_encode(&payload, &carrier),
              LAPLACE_TRAJECTORY_METADATA_OUT_OF_RANGE);
    EXPECT_EQ(std::memcmp(&carrier, &carrier_before, sizeof(carrier)), 0);

    laplace_trajectory_payload output{};
    std::memset(&output, 0x5a, sizeof(output));
    const auto output_before = output;
    carrier = {};
    EXPECT_EQ(laplace_trajectory_carrier_decode(&carrier, &output),
              LAPLACE_TRAJECTORY_INVALID_CARRIER);
    EXPECT_EQ(std::memcmp(&output, &output_before, sizeof(output)), 0);
}

TEST(TrajectoryComposition, DecodingCalculatesOrderRunsMetadataAndRelations) {
    laplace_id128 a{};
    laplace_id128 b{};
    ASSERT_EQ(laplace_identity_codepoint(0x41u, &a), LAPLACE_IDENTITY_OK);
    ASSERT_EQ(laplace_identity_codepoint(0x42u, &b), LAPLACE_IDENTITY_OK);
    const auto metadata_a = ContentMetadata(2u, true, 0x41u);
    const auto metadata_b = ContentMetadata(3u, false, 0u);
    std::array<laplace_trajectory_carrier, 3> carriers{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &a, 1u, 1u, metadata_a, &carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &b, 2u, 3u, metadata_b, &carriers[1]),
              LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &a, 5u, 1u, metadata_a, &carriers[2]),
              LAPLACE_TRAJECTORY_OK);

    std::array<laplace_composition_occurrence, 3> occurrences{};
    std::uint64_t logical_count = 0;
    ASSERT_EQ(laplace_trajectory_composition_decode(
                  carriers.data(), carriers.size(), occurrences.data(),
                  occurrences.size(), &logical_count),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(logical_count, 5u);
    EXPECT_EQ(occurrences[0].logical_ordinal, 1u);
    EXPECT_EQ(occurrences[1].logical_ordinal, 2u);
    EXPECT_EQ(occurrences[1].run_length, 3u);
    EXPECT_EQ(occurrences[2].logical_ordinal, 5u);
    EXPECT_EQ(occurrences[0].tier, 2u);
    EXPECT_EQ(occurrences[0].has_atom, 1u);
    EXPECT_EQ(occurrences[0].atom, 0x41u);

    std::uint64_t a_count = 0;
    ASSERT_EQ(laplace_trajectory_entity_count(
                  occurrences.data(), occurrences.size(), &a, &a_count),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(a_count, 2u);
    int relation = 0;
    ASSERT_EQ(laplace_trajectory_occurrence_precedes(
                  occurrences.data(), occurrences.size(), 0u, 1u, &relation),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(relation, 1);
    ASSERT_EQ(laplace_trajectory_occurrence_precedes(
                  occurrences.data(), occurrences.size(), 1u, 2u, &relation),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(relation, 1);
    ASSERT_EQ(laplace_trajectory_entities_cooccur(
                  occurrences.data(), occurrences.size(), &a, &b, &relation),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(relation, 1);
}

TEST(TrajectoryComposition, PackedOrdinalIsExactOrExplicitlyUnrepresentable) {
    laplace_id128 id{};
    ASSERT_EQ(laplace_identity_codepoint(0x61u, &id), LAPLACE_IDENTITY_OK);
    std::array<laplace_trajectory_carrier, 2> carriers{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &id, 1u, UINT16_MAX, 0u, &carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &id, 65536u, 1u, 0u, &carriers[1]),
              LAPLACE_TRAJECTORY_OK);
    std::array<laplace_composition_occurrence, 2> occurrences{};
    std::uint64_t logical_count = 0;
    ASSERT_EQ(laplace_trajectory_composition_decode(
                  carriers.data(), carriers.size(), occurrences.data(),
                  occurrences.size(), &logical_count),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(logical_count, 65536u);
    EXPECT_EQ(occurrences[0].packed_ordinal, 1u);
    EXPECT_EQ(occurrences[1].packed_ordinal, 0u);
    EXPECT_EQ(occurrences[1].logical_ordinal, 65536u);
}

TEST(TrajectoryComposition, InvalidLaterOrdinalCannotPartiallyDecode) {
    laplace_id128 id{};
    ASSERT_EQ(laplace_identity_codepoint(0x61u, &id), LAPLACE_IDENTITY_OK);
    std::array<laplace_trajectory_carrier, 2> carriers{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &id, 1u, 1u, 0u, &carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &id, 3u, 1u, 0u, &carriers[1]),
              LAPLACE_TRAJECTORY_OK);
    std::array<laplace_composition_occurrence, 2> occurrences{};
    std::memset(occurrences.data(), 0xa5, sizeof(occurrences));
    const auto before = occurrences;
    std::uint64_t logical_count = UINT64_C(0xfeedface);

    EXPECT_EQ(laplace_trajectory_composition_decode(
                  carriers.data(), carriers.size(), occurrences.data(),
                  occurrences.size(), &logical_count),
              LAPLACE_TRAJECTORY_ORDINAL_MISMATCH);
    EXPECT_EQ(std::memcmp(occurrences.data(), before.data(), sizeof(occurrences)), 0);
    EXPECT_EQ(logical_count, UINT64_C(0xfeedface));
}

}  // namespace
