#include "laplace/perfcache_modules.h"
#include "laplace/unicode_root.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr std::array<std::uint8_t, LAPLACE_UNICODE_ATOM_FIELD_COUNT> PayloadKinds{{
    1u, 2u, 1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 1u, 1u,
    1u, 11u, 11u, 11u, 12u, 13u, 1u, 1u, 1u, 1u, 1u, 13u, 1u}};

void WriteU32(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8u);
    output[2] = static_cast<std::uint8_t>(value >> 16u);
    output[3] = static_cast<std::uint8_t>(value >> 24u);
}

void WriteU64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8u; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
}

laplace_unicode_atom_record Atom(std::uint32_t position) {
    laplace_unicode_atom_record record{};
    std::array<std::uint32_t, 4> axes{};
    static constexpr std::uint8_t Zero = 0u;
    record.codepoint_position = position;
    record.placement_rank = position;
    record.position_class =
        position >= 0xd800u && position <= 0xdfffu
        ? LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS
        : LAPLACE_UNICODE_ASSIGNED_SCALAR;
    std::size_t lup_length = 0u;
    EXPECT_EQ(laplace_unicode_position_encode(
                  position, record.lup_v1_bytes, &lup_length),
              LAPLACE_IDENTITY_OK);
    record.lup_v1_length = static_cast<std::uint8_t>(lup_length);
    EXPECT_EQ(laplace_identity_codepoint_witness(
                  position, &record.content_id,
                  &record.identity_preimage_fingerprint),
              LAPLACE_IDENTITY_OK);
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        record.coordinate.component[axis] = 0.0;
        EXPECT_EQ(laplace_unicode_quantize_component_u32(
                      record.coordinate.component[axis], &axes[axis]),
                  LAPLACE_UNICODE_OK);
    }
    EXPECT_EQ(laplace_unicode_hilbert4_encode(axes.data(), record.hilbert_key),
              LAPLACE_UNICODE_OK);
    for (std::size_t field = 0; field < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field) {
        record.fields[field].field_id = static_cast<std::uint16_t>(field + 1u);
        record.fields[field].payload_kind = PayloadKinds[field];
        if (PayloadKinds[field] == LAPLACE_UNICODE_PAYLOAD_U8 ||
            PayloadKinds[field] == LAPLACE_UNICODE_PAYLOAD_BOOLEAN) {
            record.fields[field].payload = &Zero;
            record.fields[field].payload_bytes = 1u;
        }
    }
    return record;
}

std::vector<std::uint8_t> Encode(const laplace_unicode_atom_record& record) {
    std::size_t bytes = 0u;
    EXPECT_EQ(laplace_unicode_atom_record_measure(&record, &bytes),
              LAPLACE_UNICODE_OK);
    std::vector<std::uint8_t> output(bytes);
    EXPECT_EQ(laplace_unicode_atom_record_encode(
                  &record, output.data(), output.size(), &bytes),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(bytes, output.size());
    return output;
}

TEST(UnicodeNumeric, ExactRationalQuantizationClosesBoundariesAndZero) {
    struct Vector { double input; std::uint32_t output; };
    const std::array<Vector, 8> vectors{{
        {-1.0, 0u},
        {-0.5, 1073741824u},
        {-std::numeric_limits<double>::denorm_min(), 2147483647u},
        {-0.0, 2147483648u},
        {0.0, 2147483648u},
        {std::numeric_limits<double>::denorm_min(), 2147483648u},
        {0.5, 3221225471u},
        {1.0, 4294967295u}
    }};
    for (const auto& vector : vectors) {
        std::uint32_t output = 0u;
        ASSERT_EQ(laplace_unicode_quantize_component_u32(vector.input, &output),
                  LAPLACE_UNICODE_OK);
        EXPECT_EQ(output, vector.output) << vector.input;
    }
    std::uint32_t ignored = 0u;
    EXPECT_EQ(laplace_unicode_quantize_component_u32(
                  std::numeric_limits<double>::quiet_NaN(), &ignored),
              LAPLACE_UNICODE_NUMERIC_OUT_OF_RANGE);
}

TEST(UnicodeHilbert, MatchesAllContractOrientationVectors) {
    struct Vector {
        std::array<std::uint32_t, 4> axes;
        std::array<std::uint8_t, 16> key;
    };
    const std::array<Vector, 6> vectors{{
        {{{0u, 0u, 0u, 0u}}, {{0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u}}},
        {{{1u, 0u, 0u, 0u}}, {{0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x01u}}},
        {{{0u, 0u, 0u, 1u}}, {{0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u}}},
        {{{0x80000000u,0x80000000u,0x80000000u,0x80000000u}}, {{0xa0u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u}}},
        {{{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}}, {{0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau,0xaau}}},
        {{{0xffffffffu,0u,0u,0u}}, {{0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu}}}
    }};
    for (const auto& vector : vectors) {
        std::array<std::uint8_t, 16> output{};
        ASSERT_EQ(laplace_unicode_hilbert4_encode(vector.axes.data(), output.data()),
                  LAPLACE_UNICODE_OK);
        EXPECT_EQ(output, vector.key);
    }
}

TEST(UnicodeAtomCodec, RoundTripsOneCanonicalVariableRecordExactly) {
    const auto record = Atom(0x41u);
    const auto encoded = Encode(record);
    laplace_unicode_atom_record_view view{};
    std::size_t consumed = 0u;
    ASSERT_EQ(laplace_unicode_atom_record_open(
                  encoded.data(), encoded.size(), &view, &consumed),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(consumed, encoded.size());
    EXPECT_EQ(view.value.codepoint_position, 0x41u);
    EXPECT_EQ(view.value.placement_rank, 0x41u);
    EXPECT_EQ(std::memcmp(view.value.content_id.bytes, record.content_id.bytes, 16u), 0);
    const auto replay = Encode(view.value);
    EXPECT_EQ(replay, encoded);
}

TEST(UnicodeAtomCodec, RejectsIdentityAndFieldKindCorruption) {
    const auto record = Atom(0x41u);
    auto encoded = Encode(record);
    laplace_unicode_atom_record_view view{};
    std::size_t consumed = 0u;
    encoded[44] ^= 0x01u;
    EXPECT_EQ(laplace_unicode_atom_record_open(
                  encoded.data(), encoded.size(), &view, &consumed),
              LAPLACE_UNICODE_RECORD_INVALID);
    encoded = Encode(record);
    encoded[LAPLACE_UNICODE_ATOM_HEADER_BYTES + 2u] =
        LAPLACE_UNICODE_PAYLOAD_BOOLEAN;
    EXPECT_EQ(laplace_unicode_atom_record_open(
                  encoded.data(), encoded.size(), &view, &consumed),
              LAPLACE_UNICODE_RECORD_INVALID);
}

TEST(UnicodeTier0Module, ValidatesDirectAddressIdentityGeometryAndLocality) {
    const auto atom = Atom(0x41u);
    const auto encoded = Encode(atom);
    std::array<std::uint8_t, 124> entry{};
    laplace_perfcache_module_v1 module{};
    WriteU32(entry.data(), atom.codepoint_position);
    auto* value = entry.data() + 4u;
    WriteU64(value, 0u);
    WriteU32(value + 8u, static_cast<std::uint32_t>(encoded.size()));
    WriteU32(value + 12u, atom.placement_rank);
    value[16] = atom.position_class;
    value[17] = atom.lup_v1_length;
    std::memcpy(value + 20u, atom.lup_v1_bytes, 4u);
    std::memcpy(value + 24u, atom.content_id.bytes, 16u);
    std::memcpy(value + 40u, atom.identity_preimage_fingerprint.bytes, 32u);
    for (std::size_t axis = 0u; axis < 4u; ++axis) {
        std::uint64_t bits = 0u;
        std::memcpy(&bits, &atom.coordinate.component[axis], sizeof(bits));
        WriteU64(value + 72u + axis * 8u, bits);
    }
    std::memcpy(value + 104u, atom.hilbert_key, 16u);
    ASSERT_EQ(laplace_perfcache_unicode_tier0_module(&module),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(module.access_law, LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED);
    EXPECT_EQ(module.validate_record(
                  module.state, atom.codepoint_position, entry.data(), entry.size()),
              LAPLACE_PERFCACHE_OK);
    value[104] ^= 0x80u;
    EXPECT_EQ(module.validate_record(
                  module.state, atom.codepoint_position, entry.data(), entry.size()),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);
}

}  // namespace
