#include "laplace/perfcache_modules.h"
#include "laplace/persistence.h"
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
    1u, 11u, 11u, 11u, 14u, 13u, 1u, 1u, 1u, 1u, 1u, 13u, 1u}};

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

void AppendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void AppendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    const std::size_t offset = output.size();
    output.resize(offset + 4u);
    WriteU32(output.data() + offset, value);
}

void AppendNormalizationEntry(
    std::vector<std::uint8_t>& output,
    const char* key,
    std::uint8_t value_kind,
    std::uint32_t value_count,
    const std::vector<std::uint8_t>& value) {
    const std::size_t key_bytes = std::strlen(key);
    ASSERT_LE(key_bytes, UINT16_MAX);
    AppendU16(output, static_cast<std::uint16_t>(key_bytes));
    output.push_back(value_kind);
    output.push_back(0u);
    AppendU32(output, value_count);
    output.insert(output.end(), key, key + key_bytes);
    output.insert(output.end(), value.begin(), value.end());
}

laplace_digest256 FilledDigest(const std::uint8_t seed) {
    laplace_digest256 digest{};
    for (std::size_t byte = 0U; byte < sizeof(digest.bytes); ++byte) {
        digest.bytes[byte] = static_cast<std::uint8_t>(seed + byte);
    }
    return digest;
}

laplace_digest256 TestPhysicalityRecipeFingerprint() {
    return FilledDigest(0xa0U);
}

laplace_digest256 TestGeometryEpoch() {
    return FilledDigest(0xc0U);
}

laplace_unicode_atom_record Atom(std::uint32_t position) {
    laplace_unicode_atom_record record{};
    std::array<std::uint32_t, 4> axes{};
    static constexpr std::uint8_t Zero = 0u;
    static constexpr std::uint8_t DefaultProperty[] = {'U', 'n', 'k', 'n', 'o', 'w', 'n'};
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
    record.geometry_epoch = TestGeometryEpoch();
    laplace_persistence_physicality_record physicality{};
    const auto physicality_recipe = TestPhysicalityRecipeFingerprint();
    EXPECT_EQ(laplace_persistence_atomic_point_physicality(
                  &record.content_id, 1U, &physicality_recipe,
                  &record.geometry_epoch, &record.coordinate, &physicality),
              LAPLACE_PERSISTENCE_OK);
    record.physicality_id = physicality.physicality_id;
    for (std::size_t field = 0; field < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field) {
        record.fields[field].field_id = static_cast<std::uint16_t>(field + 1u);
        record.fields[field].payload_kind = PayloadKinds[field];
        if (PayloadKinds[field] == LAPLACE_UNICODE_PAYLOAD_U8 ||
            PayloadKinds[field] == LAPLACE_UNICODE_PAYLOAD_BOOLEAN) {
            record.fields[field].payload = &Zero;
            record.fields[field].payload_bytes = 1u;
        } else if (PayloadKinds[field] ==
                   LAPLACE_UNICODE_PAYLOAD_ASCII_PROPERTY) {
            record.fields[field].payload = DefaultProperty;
            record.fields[field].payload_bytes =
                static_cast<std::uint32_t>(sizeof(DefaultProperty));
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

std::vector<std::uint8_t> Frame(
    std::uint16_t kind,
    std::uint64_t ordinal,
    const std::vector<std::uint8_t>& payload) {
    const laplace_unicode_root_frame frame{
        payload.data(), ordinal, static_cast<std::uint32_t>(payload.size()),
        kind, 0u};
    std::size_t bytes = 0u;
    EXPECT_EQ(laplace_unicode_root_frame_measure(&frame, &bytes),
              LAPLACE_UNICODE_OK);
    std::vector<std::uint8_t> output(bytes);
    EXPECT_EQ(laplace_unicode_root_frame_encode(
                  &frame, output.data(), output.size(), &bytes),
              LAPLACE_UNICODE_OK);
    return output;
}

laplace_unicode_root_stream_expectation RootExpectation() {
    laplace_unicode_root_stream_expectation expectation{};
    const std::array<laplace_digest256*, 8> bindings{{
        &expectation.source_fingerprint,
        &expectation.recipe_fingerprint,
        &expectation.numeric_provider_receipt,
        &expectation.stream_contract_fingerprint,
        &expectation.algorithmic_hangul_rule_fingerprint,
        &expectation.atom_record_contract_fingerprint,
        &expectation.placement_rank_permutation_fingerprint,
        &expectation.coordinate_table_fingerprint}};
    for (std::size_t binding = 0u; binding < bindings.size(); ++binding) {
        for (std::size_t byte = 0u; byte < sizeof(bindings[binding]->bytes);
             ++byte) {
            bindings[binding]->bytes[byte] = static_cast<std::uint8_t>(
                1u + binding * sizeof(bindings[binding]->bytes) + byte);
        }
    }
    expectation.physicality_recipe_fingerprint =
        TestPhysicalityRecipeFingerprint();
    expectation.geometry_epoch = TestGeometryEpoch();
    expectation.physicality_recipe_version = 1U;
    return expectation;
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

TEST(UnicodeAtomPersistence,
     ProjectsEmittedIdentityAndRejectsPhysicalityReminting) {
    auto atom = Atom(0x41U);
    const auto expectation = RootExpectation();
    laplace_persistence_entity_record entity{};
    laplace_persistence_physicality_record physicality{};
    ASSERT_EQ(laplace_unicode_atom_persistence_project(
                  &atom, &expectation, &entity, &physicality),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(std::memcmp(&entity.entity_id, &atom.content_id,
                          sizeof(entity.entity_id)), 0);
    EXPECT_EQ(std::memcmp(&entity.identity_witness,
                          &atom.identity_preimage_fingerprint,
                          sizeof(entity.identity_witness)), 0);
    EXPECT_EQ(std::memcmp(&physicality.physicality_id,
                          &atom.physicality_id,
                          sizeof(physicality.physicality_id)), 0);
    EXPECT_EQ(std::memcmp(&physicality.centroid, &atom.coordinate,
                          sizeof(physicality.centroid)), 0);

    atom.physicality_id.bytes[0] ^= 0x80U;
    const auto prior_entity = entity;
    const auto prior_physicality = physicality;
    EXPECT_EQ(laplace_unicode_atom_persistence_project(
                  &atom, &expectation, &entity, &physicality),
              LAPLACE_UNICODE_IDENTITY_MISMATCH);
    EXPECT_EQ(std::memcmp(&entity, &prior_entity, sizeof(entity)), 0);
    EXPECT_EQ(std::memcmp(&physicality, &prior_physicality,
                          sizeof(physicality)), 0);
}

TEST(UnicodePlacementFingerprint,
     RejectsDuplicateRankBeforeCoordinateTableIdentity) {
    std::vector<std::uint32_t> ranks(LAPLACE_UNICODE_ROOT_POPULATION);
    for (std::uint32_t position = 0U;
         position < LAPLACE_UNICODE_ROOT_POPULATION; ++position) {
        ranks[position] = position;
    }
    laplace_digest256 first{};
    laplace_digest256 replay{};
    ASSERT_EQ(laplace_unicode_placement_rank_permutation_identify(
                  ranks.data(), static_cast<std::uint32_t>(ranks.size()),
                  &first),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(laplace_unicode_placement_rank_permutation_identify(
                  ranks.data(), static_cast<std::uint32_t>(ranks.size()),
                  &replay),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(std::memcmp(&first, &replay, sizeof(first)), 0);

    ranks[1] = 0U;
    EXPECT_EQ(laplace_unicode_placement_rank_permutation_identify(
                  ranks.data(), static_cast<std::uint32_t>(ranks.size()),
                  &replay),
              LAPLACE_UNICODE_RECORD_INVALID);
    const std::vector<laplace_point4d> coordinates(
        LAPLACE_UNICODE_ROOT_POPULATION);
    EXPECT_EQ(laplace_unicode_coordinate_table_identify(
                  ranks.data(), coordinates.data(),
                  static_cast<std::uint32_t>(ranks.size()), &replay),
              LAPLACE_UNICODE_RECORD_INVALID);
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

TEST(UnicodeAtomPayload, RejectsNonCanonicalPayloadsAndAcceptsSortedSets) {
    auto atom = Atom(0x41u);
    static constexpr std::array<std::uint8_t, 3> InvalidProperty{{
        'L', 'u', ' '}};
    atom.fields[0].payload = InvalidProperty.data();
    atom.fields[0].payload_bytes =
        static_cast<std::uint32_t>(InvalidProperty.size());
    std::size_t measured = 0u;
    EXPECT_EQ(laplace_unicode_atom_record_measure(&atom, &measured),
              LAPLACE_UNICODE_RECORD_INVALID);

    atom = Atom(0x41u);
    static constexpr std::array<std::uint8_t, 10> SortedSet{{
        2u, 0u, 0u, 0u,
        1u, 0u, 'A',
        1u, 0u, 'B'}};
    atom.fields[14].payload = SortedSet.data();
    atom.fields[14].payload_bytes =
        static_cast<std::uint32_t>(SortedSet.size());
    EXPECT_EQ(laplace_unicode_atom_record_measure(&atom, &measured),
              LAPLACE_UNICODE_OK);

    auto unsorted = SortedSet;
    unsorted[6] = 'B';
    unsorted[9] = 'A';
    atom.fields[14].payload = unsorted.data();
    EXPECT_EQ(laplace_unicode_atom_record_measure(&atom, &measured),
              LAPLACE_UNICODE_RECORD_INVALID);

    atom = Atom(0x41u);
    static constexpr std::array<std::uint8_t, 2> LeadingZero{{'0', '1'}};
    atom.fields[7].payload = LeadingZero.data();
    atom.fields[7].payload_bytes =
        static_cast<std::uint32_t>(LeadingZero.size());
    EXPECT_EQ(laplace_unicode_atom_record_measure(&atom, &measured),
              LAPLACE_UNICODE_RECORD_INVALID);

    atom = Atom(0x41u);
    std::array<std::uint8_t, 4> out_of_range{};
    WriteU32(out_of_range.data(), LAPLACE_UNICODE_ROOT_POPULATION);
    atom.fields[5].payload = out_of_range.data();
    atom.fields[5].payload_bytes =
        static_cast<std::uint32_t>(out_of_range.size());
    EXPECT_EQ(laplace_unicode_atom_record_measure(&atom, &measured),
              LAPLACE_UNICODE_RECORD_INVALID);
}

TEST(UnicodeAtomPayload, PreservesTypedNormalizationValuesAndExplicitEmpty) {
    std::vector<std::uint8_t> payload;
    AppendU32(payload, 3u);
    AppendNormalizationEntry(
        payload, "NFC_Quick_Check",
        LAPLACE_UNICODE_NORMALIZATION_ASCII_PROPERTY_VALUE,
        5u, std::vector<std::uint8_t>{'M', 'a', 'y', 'b', 'e'});
    std::vector<std::uint8_t> mapping;
    AppendU32(mapping, 0x0061u);
    AppendU32(mapping, 0x0062u);
    AppendNormalizationEntry(
        payload, "NFKC_Casefold",
        LAPLACE_UNICODE_NORMALIZATION_POSITION_SEQUENCE,
        2u, mapping);
    const std::size_t explicit_empty_kind = payload.size() + 2u;
    AppendNormalizationEntry(
        payload, "NFKC_Simple_Casefold",
        LAPLACE_UNICODE_NORMALIZATION_EMPTY_POSITION_SEQUENCE,
        0u, {});

    auto atom = Atom(0x41u);
    atom.fields[17].payload = payload.data();
    atom.fields[17].payload_bytes =
        static_cast<std::uint32_t>(payload.size());
    std::size_t measured = 0u;
    EXPECT_EQ(laplace_unicode_atom_record_measure(&atom, &measured),
              LAPLACE_UNICODE_OK);

    auto collapsed_empty = payload;
    collapsed_empty[explicit_empty_kind] =
        LAPLACE_UNICODE_NORMALIZATION_POSITION_SEQUENCE;
    atom.fields[17].payload = collapsed_empty.data();
    EXPECT_EQ(laplace_unicode_atom_record_measure(&atom, &measured),
              LAPLACE_UNICODE_RECORD_INVALID);
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
    EXPECT_EQ(std::memcmp(
                  view.value.geometry_epoch.bytes,
                  record.geometry_epoch.bytes, 32u), 0);
    EXPECT_EQ(std::memcmp(
                  view.value.physicality_id.bytes,
                  record.physicality_id.bytes, 32u), 0);
    const auto replay = Encode(view.value);
    EXPECT_EQ(replay, encoded);
}

TEST(UnicodeAtomCodec, PhysicalityChangesWithGeometryEpochWhileContentDoesNot) {
    const auto first = Atom(0x41u);
    auto second = first;
    second.geometry_epoch.bytes[0] ^= 0x01U;
    const auto recipe = TestPhysicalityRecipeFingerprint();
    laplace_persistence_physicality_record physicality{};
    ASSERT_EQ(laplace_persistence_atomic_point_physicality(
                  &second.content_id, 1U, &recipe, &second.geometry_epoch,
                  &second.coordinate, &physicality),
              LAPLACE_PERSISTENCE_OK);
    second.physicality_id = physicality.physicality_id;
    EXPECT_EQ(std::memcmp(
                  first.content_id.bytes, second.content_id.bytes,
                  sizeof(first.content_id.bytes)), 0);
    EXPECT_NE(std::memcmp(
                  first.physicality_id.bytes, second.physicality_id.bytes,
                  sizeof(first.physicality_id.bytes)), 0);
    EXPECT_FALSE(Encode(second).empty());
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

TEST(UnicodeRootFrameCodec, WrapsAtomWithoutChangingIdentityOrBytes) {
    const auto atom = Encode(Atom(0x41u));
    const auto encoded = Frame(LAPLACE_UNICODE_ROOT_FRAME_ATOM, 0x41u, atom);
    laplace_unicode_root_frame_view frame{};
    laplace_unicode_atom_record_view decoded_atom{};
    std::size_t consumed = 0u;
    ASSERT_EQ(laplace_unicode_root_frame_open(
                  encoded.data(), encoded.size(), &frame, &consumed),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(consumed, encoded.size());
    EXPECT_EQ(frame.value.kind, LAPLACE_UNICODE_ROOT_FRAME_ATOM);
    EXPECT_EQ(frame.value.section_ordinal, 0x41u);
    EXPECT_EQ(frame.value.payload_bytes, atom.size());
    EXPECT_EQ(std::memcmp(frame.value.payload, atom.data(), atom.size()), 0);
    ASSERT_EQ(laplace_unicode_atom_record_open(
                  frame.value.payload, frame.value.payload_bytes,
                  &decoded_atom, &consumed),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(decoded_atom.value.codepoint_position, 0x41u);
}

TEST(UnicodeRootFrameCodec, RejectsOrdinalOrNestedAtomCorruption) {
    const auto atom = Encode(Atom(0x41u));
    laplace_unicode_root_frame mismatched{
        atom.data(), 0x42u, static_cast<std::uint32_t>(atom.size()),
        LAPLACE_UNICODE_ROOT_FRAME_ATOM, 0u};
    std::size_t bytes = 0u;
    EXPECT_EQ(laplace_unicode_root_frame_measure(&mismatched, &bytes),
              LAPLACE_UNICODE_RECORD_INVALID);

    auto encoded = Frame(LAPLACE_UNICODE_ROOT_FRAME_ATOM, 0x41u, atom);
    encoded[LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES + 44u] ^= 0x01u;
    laplace_unicode_root_frame_view view{};
    EXPECT_EQ(laplace_unicode_root_frame_open(
                  encoded.data(), encoded.size(), &view, &bytes),
              LAPLACE_UNICODE_RECORD_INVALID);
}

TEST(UnicodeRootStreamValidator, RejectsCrossBatchOrdinalGapAndTruncation) {
    const auto frame_zero = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_ATOM, 0u, Encode(Atom(0u)));
    const auto frame_two = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_ATOM, 2u, Encode(Atom(2u)));
    const auto expectation = RootExpectation();
    laplace_unicode_root_stream_validator* validator = nullptr;
    ASSERT_EQ(laplace_unicode_root_stream_validator_create(
                  &expectation, &validator),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(validator, nullptr);
    EXPECT_EQ(laplace_unicode_root_stream_validator_consume(
                  validator, frame_zero.data(), frame_zero.size(), 1u, 0u),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(laplace_unicode_root_stream_validator_consume(
                  validator, frame_two.data(), frame_two.size(), 1u, 1u),
              LAPLACE_UNICODE_STREAM_ORDER_INVALID);
    laplace_unicode_root_stream_summary summary{};
    EXPECT_EQ(laplace_unicode_root_stream_validator_finish(
                  validator, &summary),
              LAPLACE_UNICODE_STREAM_ORDER_INVALID);
    laplace_unicode_root_stream_validator_destroy(validator);

    validator = nullptr;
    ASSERT_EQ(laplace_unicode_root_stream_validator_create(
                  &expectation, &validator),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(validator, nullptr);
    EXPECT_EQ(laplace_unicode_root_stream_validator_consume(
                  validator, frame_zero.data(), frame_zero.size(), 1u, 0u),
              LAPLACE_UNICODE_OK);
    laplace_unicode_root_stream_section_snapshot snapshot{};
    EXPECT_EQ(laplace_unicode_root_stream_validator_seal_sections(
                  validator, &snapshot),
              LAPLACE_UNICODE_STREAM_INCOMPLETE);
    EXPECT_EQ(laplace_unicode_root_stream_validator_finish(
                  validator, &summary), LAPLACE_UNICODE_STREAM_INCOMPLETE);
    laplace_unicode_root_stream_validator_destroy(validator);
}

TEST(UnicodeRootStreamValidator, RejectsAtomPhysicalityOutsideBoundGeometryEpoch) {
    auto atom = Atom(0U);
    atom.physicality_id.bytes[0] ^= 0x01U;
    const auto frame = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_ATOM, 0U, Encode(atom));
    const auto expectation = RootExpectation();
    laplace_unicode_root_stream_validator* validator = nullptr;
    ASSERT_EQ(laplace_unicode_root_stream_validator_create(
                  &expectation, &validator),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(validator, nullptr);
    EXPECT_EQ(laplace_unicode_root_stream_validator_consume(
                  validator, frame.data(), frame.size(), 1U, 0U),
              LAPLACE_UNICODE_IDENTITY_MISMATCH);
    laplace_unicode_root_stream_validator_destroy(validator);
}

TEST(UnicodeRootStreamValidator, RejectsDuplicateObservedPlacementRank) {
    const auto first = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_ATOM, 0U, Encode(Atom(0U)));
    auto duplicate_rank = Atom(1U);
    duplicate_rank.placement_rank = 0U;
    const auto second = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_ATOM, 1U, Encode(duplicate_rank));
    const auto expectation = RootExpectation();
    laplace_unicode_root_stream_validator* validator = nullptr;
    ASSERT_EQ(laplace_unicode_root_stream_validator_create(
                  &expectation, &validator),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(validator, nullptr);
    ASSERT_EQ(laplace_unicode_root_stream_validator_consume(
                  validator, first.data(), first.size(), 1U, 0U),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(laplace_unicode_root_stream_validator_consume(
                  validator, second.data(), second.size(), 1U, 1U),
              LAPLACE_UNICODE_STREAM_ORDER_INVALID);
    laplace_unicode_root_stream_validator_destroy(validator);
}

TEST(UnicodeDucetPositionCodec, PreservesEveryWeightMarkerAndKeyByte) {
    const std::array<laplace_unicode_collation_element, 2> elements{{
        {0x1234u, 0x0020u, 0x0002u, 0u, 0u},
        {0xabceu, 0x0030u, 0x0003u, 1u, 0u}}};
    const std::array<std::uint8_t, 9> key{{
        0x12u, 0x34u, 0xabu, 0xceu, 0x00u, 0x00u, 0x41u, 0x01u, 0x42u}};
    const laplace_unicode_ducet_position_record record{
        elements.data(), key.data(), 0x41u,
        static_cast<std::uint32_t>(elements.size()),
        static_cast<std::uint32_t>(key.size()),
        LAPLACE_UNICODE_DUCET_EXPLICIT, {0u, 0u, 0u}};
    std::size_t bytes = 0u;
    ASSERT_EQ(laplace_unicode_ducet_position_measure(&record, &bytes),
              LAPLACE_UNICODE_OK);
    std::vector<std::uint8_t> encoded(bytes);
    ASSERT_EQ(laplace_unicode_ducet_position_encode(
                  &record, encoded.data(), encoded.size(), &bytes),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_position_view view{};
    ASSERT_EQ(laplace_unicode_ducet_position_open(
                  encoded.data(), encoded.size(), &view, &bytes),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(view.codepoint_position, 0x41u);
    EXPECT_EQ(view.provenance, LAPLACE_UNICODE_DUCET_EXPLICIT);
    EXPECT_EQ(view.element_count, elements.size());
    EXPECT_EQ(view.equivalence_key_bytes, key.size());
    EXPECT_EQ(std::memcmp(view.equivalence_key, key.data(), key.size()), 0);
    for (std::uint32_t index = 0u; index < view.element_count; ++index) {
        laplace_unicode_collation_element decoded{};
        ASSERT_EQ(laplace_unicode_ducet_position_element(
                      &view, index, &decoded), LAPLACE_UNICODE_OK);
        EXPECT_EQ(decoded.primary, elements[index].primary);
        EXPECT_EQ(decoded.secondary, elements[index].secondary);
        EXPECT_EQ(decoded.tertiary, elements[index].tertiary);
        EXPECT_EQ(decoded.variable, elements[index].variable);
    }
    const auto framed = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_DUCET_POSITION, 0x41u, encoded);
    laplace_unicode_root_frame_view frame{};
    EXPECT_EQ(laplace_unicode_root_frame_open(
                  framed.data(), framed.size(), &frame, &bytes),
              LAPLACE_UNICODE_OK);
}

TEST(UnicodeDucetPositionCodec, RejectsTruncationAndMarkerCorruption) {
    const std::array<laplace_unicode_collation_element, 1> elements{{
        {0x1234u, 0x0020u, 0x0002u, 0u, 0u}}};
    const std::array<std::uint8_t, 2> key{{0x12u, 0x34u}};
    const laplace_unicode_ducet_position_record record{
        elements.data(), key.data(), 0x41u, 1u, 2u,
        LAPLACE_UNICODE_DUCET_EXPLICIT, {0u, 0u, 0u}};
    std::size_t bytes = 0u;
    ASSERT_EQ(laplace_unicode_ducet_position_measure(&record, &bytes),
              LAPLACE_UNICODE_OK);
    std::vector<std::uint8_t> encoded(bytes);
    ASSERT_EQ(laplace_unicode_ducet_position_encode(
                  &record, encoded.data(), encoded.size(), &bytes),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_position_view view{};
    EXPECT_EQ(laplace_unicode_ducet_position_open(
                  encoded.data(), encoded.size() - 1u, &view, &bytes),
              LAPLACE_UNICODE_RECORD_INVALID);
    encoded[LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES] = 2u;
    EXPECT_EQ(laplace_unicode_ducet_position_open(
                  encoded.data(), encoded.size(), &view, &bytes),
              LAPLACE_UNICODE_RECORD_INVALID);
}

TEST(UnicodeNormalizationCompositionCodec, RoundTripsTypedPositionsAndFrame) {
    const laplace_unicode_normalization_composition composition{
        0x0041u, 0x030au, 0x00c5u};
    std::array<std::uint8_t, LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES>
        encoded{};
    ASSERT_EQ(laplace_unicode_normalization_composition_encode(
                  &composition, encoded.data()), LAPLACE_UNICODE_OK);
    laplace_unicode_normalization_composition decoded{};
    std::size_t consumed = 0u;
    ASSERT_EQ(laplace_unicode_normalization_composition_open(
                  encoded.data(), encoded.size(), &decoded, &consumed),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(consumed, encoded.size());
    EXPECT_EQ(decoded.starter_position, composition.starter_position);
    EXPECT_EQ(decoded.combining_position, composition.combining_position);
    EXPECT_EQ(decoded.composite_position, composition.composite_position);
    const std::vector<std::uint8_t> payload(encoded.begin(), encoded.end());
    const auto framed = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_NORMALIZATION_COMPOSITION, 0u, payload);
    laplace_unicode_root_frame_view frame{};
    EXPECT_EQ(laplace_unicode_root_frame_open(
                  framed.data(), framed.size(), &frame, &consumed),
              LAPLACE_UNICODE_OK);
}

TEST(UnicodeDucetContractionCodec, PreservesSequenceWeightsAndSourceProvenance) {
    const std::array<std::uint32_t, 3> sequence{{0x0063u, 0x0068u, 0x0061u}};
    const std::array<laplace_unicode_collation_element, 2> elements{{
        {0x1234u, 0x0020u, 0x0002u, 0u, 0u},
        {0x5678u, 0x0030u, 0x0003u, 1u, 0u}}};
    const laplace_unicode_ducet_contraction_record record{
        sequence.data(), elements.data(), 812u,
        static_cast<std::uint32_t>(sequence.size()),
        static_cast<std::uint32_t>(elements.size())};
    std::size_t bytes = 0u;
    ASSERT_EQ(laplace_unicode_ducet_contraction_measure(&record, &bytes),
              LAPLACE_UNICODE_OK);
    std::vector<std::uint8_t> encoded(bytes);
    ASSERT_EQ(laplace_unicode_ducet_contraction_encode(
                  &record, encoded.data(), encoded.size(), &bytes),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_contraction_view view{};
    ASSERT_EQ(laplace_unicode_ducet_contraction_open(
                  encoded.data(), encoded.size(), &view, &bytes),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(view.source_line_ordinal, 812u);
    EXPECT_EQ(view.sequence_count, sequence.size());
    EXPECT_EQ(view.element_count, elements.size());
    for (std::uint32_t index = 0u; index < view.sequence_count; ++index) {
        std::uint32_t position = 0u;
        ASSERT_EQ(laplace_unicode_ducet_contraction_position(
                      &view, index, &position), LAPLACE_UNICODE_OK);
        EXPECT_EQ(position, sequence[index]);
    }
    for (std::uint32_t index = 0u; index < view.element_count; ++index) {
        laplace_unicode_collation_element element{};
        ASSERT_EQ(laplace_unicode_ducet_contraction_element(
                      &view, index, &element), LAPLACE_UNICODE_OK);
        EXPECT_EQ(element.primary, elements[index].primary);
        EXPECT_EQ(element.secondary, elements[index].secondary);
        EXPECT_EQ(element.tertiary, elements[index].tertiary);
        EXPECT_EQ(element.variable, elements[index].variable);
    }
    const auto framed = Frame(
        LAPLACE_UNICODE_ROOT_FRAME_DUCET_CONTRACTION, 0u, encoded);
    laplace_unicode_root_frame_view frame{};
    EXPECT_EQ(laplace_unicode_root_frame_open(
                  framed.data(), framed.size(), &frame, &bytes),
              LAPLACE_UNICODE_OK);
}

TEST(UnicodeRootManifestCodec, BindsEveryRequiredSectionWithoutCircularReceipt) {
    laplace_unicode_root_manifest manifest{};
    manifest.atom_count = LAPLACE_UNICODE_ROOT_POPULATION;
    manifest.ducet_position_count = LAPLACE_UNICODE_ROOT_POPULATION;
    manifest.ducet_contraction_count = 17u;
    manifest.normalization_composition_count = 941u;
    manifest.total_frame_count = manifest.atom_count +
        manifest.ducet_position_count + manifest.ducet_contraction_count +
        manifest.normalization_composition_count + 1u;
    manifest.physicality_recipe_version = 1U;
    const std::array<laplace_digest256*, 14> fingerprints{{
        &manifest.source_fingerprint,
        &manifest.recipe_fingerprint,
        &manifest.numeric_provider_receipt,
        &manifest.stream_contract_fingerprint,
        &manifest.atom_section_fingerprint,
        &manifest.ducet_position_section_fingerprint,
        &manifest.ducet_contraction_section_fingerprint,
        &manifest.normalization_composition_section_fingerprint,
        &manifest.algorithmic_hangul_rule_fingerprint,
        &manifest.atom_record_contract_fingerprint,
        &manifest.physicality_recipe_fingerprint,
        &manifest.placement_rank_permutation_fingerprint,
        &manifest.coordinate_table_fingerprint,
        &manifest.geometry_epoch}};
    for (std::size_t digest = 0u; digest < fingerprints.size(); ++digest) {
        for (std::size_t byte = 0u; byte < 32u; ++byte) {
            fingerprints[digest]->bytes[byte] = static_cast<std::uint8_t>(
                1u + digest * 32u + byte);
        }
    }
    std::array<std::uint8_t, LAPLACE_UNICODE_ROOT_MANIFEST_BYTES> encoded{};
    ASSERT_EQ(laplace_unicode_root_manifest_encode(&manifest, encoded.data()),
              LAPLACE_UNICODE_OK);
    laplace_unicode_root_manifest decoded{};
    std::size_t consumed = 0u;
    ASSERT_EQ(laplace_unicode_root_manifest_open(
                  encoded.data(), encoded.size(), &decoded, &consumed),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(consumed, encoded.size());
    EXPECT_EQ(decoded.total_frame_count, manifest.total_frame_count);
    EXPECT_EQ(std::memcmp(&decoded, &manifest, sizeof(manifest)), 0);
    const std::vector<std::uint8_t> payload(encoded.begin(), encoded.end());
    const auto framed = Frame(LAPLACE_UNICODE_ROOT_FRAME_MANIFEST, 0u, payload);
    laplace_unicode_root_frame_view frame{};
    EXPECT_EQ(laplace_unicode_root_frame_open(
                  framed.data(), framed.size(), &frame, &consumed),
              LAPLACE_UNICODE_OK);
}

TEST(UnicodeRootManifestCodec, RejectsPartialPopulationAndWrongTotal) {
    laplace_unicode_root_manifest manifest{};
    manifest.physicality_recipe_version = 1U;
    manifest.atom_count = LAPLACE_UNICODE_ROOT_POPULATION - 1u;
    manifest.ducet_position_count = LAPLACE_UNICODE_ROOT_POPULATION;
    manifest.total_frame_count = manifest.atom_count +
        manifest.ducet_position_count + 1u;
    std::array<std::uint8_t, LAPLACE_UNICODE_ROOT_MANIFEST_BYTES> encoded{};
    EXPECT_EQ(laplace_unicode_root_manifest_encode(&manifest, encoded.data()),
              LAPLACE_UNICODE_RECORD_INVALID);
    manifest.atom_count = LAPLACE_UNICODE_ROOT_POPULATION;
    EXPECT_EQ(laplace_unicode_root_manifest_encode(&manifest, encoded.data()),
              LAPLACE_UNICODE_RECORD_INVALID);
}

TEST(UnicodeRootManifestCodec, RejectsUnsupportedPhysicalityRecipeVersion) {
    laplace_unicode_root_manifest manifest{};
    manifest.physicality_recipe_version = 2U;
    manifest.atom_count = LAPLACE_UNICODE_ROOT_POPULATION;
    manifest.ducet_position_count = LAPLACE_UNICODE_ROOT_POPULATION;
    manifest.total_frame_count = manifest.atom_count +
        manifest.ducet_position_count + 1U;
    std::array<std::uint8_t, LAPLACE_UNICODE_ROOT_MANIFEST_BYTES> encoded{};
    EXPECT_EQ(laplace_unicode_root_manifest_encode(&manifest, encoded.data()),
              LAPLACE_UNICODE_RECORD_INVALID);

    manifest.physicality_recipe_version = 1U;
    ASSERT_EQ(laplace_unicode_root_manifest_encode(&manifest, encoded.data()),
              LAPLACE_UNICODE_OK);
    WriteU32(encoded.data() + 12U, 2U);
    laplace_unicode_root_manifest decoded{};
    std::size_t consumed = 0U;
    EXPECT_EQ(laplace_unicode_root_manifest_open(
                  encoded.data(), encoded.size(), &decoded, &consumed),
              LAPLACE_UNICODE_RECORD_INVALID);

    auto expectation = RootExpectation();
    expectation.physicality_recipe_version = 2U;
    laplace_unicode_root_stream_validator* validator = nullptr;
    EXPECT_EQ(laplace_unicode_root_stream_validator_create(
                  &expectation, &validator),
              LAPLACE_UNICODE_INVALID_ARGUMENT);
    EXPECT_EQ(validator, nullptr);
    laplace_unicode_root_stream_validator_destroy(validator);
}

TEST(UnicodeTier0Module, ValidatesDirectAddressIdentityGeometryAndLocality) {
    const auto atom = Atom(0x41u);
    const auto encoded = Encode(atom);
    std::array<std::uint8_t, 156> entry{};
    laplace_perfcache_module_v2 module{};
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
    std::memcpy(value + 120u, atom.physicality_id.bytes, 32u);
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

TEST(UnicodeTier0Module, ExactPersistedContractSelectsNativeImplementation) {
    laplace_perfcache_module_v2 declared{};
    laplace_perfcache_module_v2 resolved{};
    laplace_perfcache_contract contract{};
    ASSERT_EQ(laplace_perfcache_unicode_tier0_module(&declared),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    contract.module_id = declared.module_id;
    contract.key_schema_id = declared.key_schema_id;
    contract.value_schema_id = declared.value_schema_id;
    contract.module_contract_fingerprint =
        declared.module_contract_fingerprint;
    contract.key_bytes = declared.key_bytes;
    contract.value_bytes = declared.value_bytes;
    contract.access_law = declared.access_law;

    ASSERT_EQ(laplace_perfcache_builtin_module_resolve(&contract, &resolved),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    EXPECT_EQ(std::memcmp(resolved.module_id.bytes, declared.module_id.bytes,
                          sizeof(declared.module_id.bytes)), 0);
    EXPECT_EQ(resolved.validate_record, declared.validate_record);
    EXPECT_EQ(resolved.validate_view, declared.validate_view);
    EXPECT_EQ(resolved.key_bytes, declared.key_bytes);
    EXPECT_EQ(resolved.value_bytes, declared.value_bytes);

    contract.value_bytes += 1U;
    EXPECT_EQ(laplace_perfcache_builtin_module_resolve(&contract, &resolved),
              LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH);
    contract.value_bytes = declared.value_bytes;
    contract.module_id.bytes[0] ^= 0x80U;
    EXPECT_EQ(laplace_perfcache_builtin_module_resolve(&contract, &resolved),
              LAPLACE_PERFCACHE_REGISTRY_MODULE_NOT_FOUND);
}

TEST(UnicodeIdentityReverseModule, ProbesCollisionChainsAndPreservesTypedEmpty) {
    laplace_perfcache_module_v2 module{};
    ASSERT_EQ(laplace_perfcache_unicode_identity_reverse_module(&module),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    ASSERT_EQ(module.access_law, LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED);
    ASSERT_NE(module.lookup_batch, nullptr);
    ASSERT_EQ(module.key_bytes, 48U);
    ASSERT_EQ(module.value_bytes, 8U);

    constexpr std::size_t Stride = 56U;
    std::array<std::uint8_t, Stride * 3U> records{};
    std::array<std::uint8_t, 48U * 3U> keys{};
    keys[0] = 0x11U;
    keys[48U] = 0x22U;
    std::memcpy(records.data(), keys.data(), 48U);
    WriteU32(records.data() + 48U, 7U);
    records[52U] = 1U;
    std::memcpy(records.data() + Stride, keys.data() + 48U, 48U);
    WriteU32(records.data() + Stride + 48U, 9U);
    records[Stride + 52U] = 1U;

    laplace_perfcache_view view{};
    view.records = records.data();
    view.record_count = LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY;
    view.record_stride = Stride;
    std::array<std::uint64_t, 3U> indexes{};
    std::array<std::uint8_t, 3U> found{};
    std::array<std::uint8_t, 48U * 3U> lookup_keys{};
    std::memcpy(lookup_keys.data(), keys.data() + 48U, 48U);
    std::memcpy(lookup_keys.data() + 48U, keys.data(), 48U);
    ASSERT_EQ(module.lookup_batch(
                  module.state, &view, lookup_keys.data(), 3U,
                  indexes.data(), found.data()),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(found, (std::array<std::uint8_t, 3U>{{1U, 1U, 0U}}));
    EXPECT_EQ(indexes[0], 1U);
    EXPECT_EQ(indexes[1], 0U);
    EXPECT_EQ(indexes[2], UINT64_MAX);

    records[Stride + 52U] = 2U;
    EXPECT_EQ(module.lookup_batch(
                  module.state, &view, lookup_keys.data(), 1U,
                  indexes.data(), found.data()),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);
}

TEST(UnicodeIdentityReverseModule, ReidentifiesOccupiedRecordsExactly) {
    laplace_perfcache_module_v2 module{};
    ASSERT_EQ(laplace_perfcache_unicode_identity_reverse_module(&module),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    const auto atom = Atom(0x41U);
    std::array<std::uint8_t, 56U> record{};
    std::memcpy(record.data(), atom.content_id.bytes, 16U);
    std::memcpy(
        record.data() + 16U,
        atom.identity_preimage_fingerprint.bytes, 32U);
    WriteU32(record.data() + 48U, atom.codepoint_position);
    record[52U] = 1U;
    EXPECT_EQ(module.validate_record(
                  module.state, 0U, record.data(), record.size()),
              LAPLACE_PERFCACHE_OK);
    record[16U] ^= 0x01U;
    EXPECT_EQ(module.validate_record(
                  module.state, 0U, record.data(), record.size()),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);

    record.fill(0U);
    EXPECT_EQ(module.validate_record(
                  module.state, 0U, record.data(), record.size()),
              LAPLACE_PERFCACHE_OK);
    record[0U] = 1U;
    EXPECT_EQ(module.validate_record(
                  module.state, 0U, record.data(), record.size()),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);
}

TEST(UnicodeTier0Module, WholeViewRejectsPartialPopulation) {
    const auto atom = Atom(0U);
    const auto metadata = Encode(atom);
    std::array<std::uint8_t, 156> entry{};
    laplace_perfcache_module_v2 module{};
    ASSERT_EQ(laplace_perfcache_unicode_tier0_module(&module),
              LAPLACE_PERFCACHE_REGISTRY_OK);
    WriteU32(entry.data(), atom.codepoint_position);
    auto* value = entry.data() + 4U;
    WriteU64(value, 0U);
    WriteU32(value + 8U, static_cast<std::uint32_t>(metadata.size()));
    WriteU32(value + 12U, atom.placement_rank);
    value[16] = atom.position_class;
    value[17] = atom.lup_v1_length;
    std::memcpy(value + 20U, atom.lup_v1_bytes, 4U);
    std::memcpy(value + 24U, atom.content_id.bytes, 16U);
    std::memcpy(
        value + 40U, atom.identity_preimage_fingerprint.bytes, 32U);
    for (std::size_t axis = 0U; axis < 4U; ++axis) {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &atom.coordinate.component[axis], sizeof(bits));
        WriteU64(value + 72U + axis * 8U, bits);
    }
    std::memcpy(value + 104U, atom.hilbert_key, 16U);
    std::memcpy(value + 120U, atom.physicality_id.bytes, 32U);

    laplace_perfcache_view view{};
    view.contract.module_id = module.module_id;
    view.contract.key_schema_id = module.key_schema_id;
    view.contract.value_schema_id = module.value_schema_id;
    view.contract.module_contract_fingerprint =
        module.module_contract_fingerprint;
    view.contract.key_bytes = module.key_bytes;
    view.contract.value_bytes = module.value_bytes;
    view.contract.access_law = module.access_law;
    view.records = entry.data();
    view.record_count = 1U;
    view.record_stride = static_cast<std::uint32_t>(entry.size());
    view.metadata = metadata.data();
    view.metadata_bytes = metadata.size();
    std::uint64_t invalid = UINT64_MAX;
    EXPECT_EQ(module.validate_record(
                  module.state, 0U, entry.data(), entry.size()),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(laplace_perfcache_unicode_tier0_validate_view(
                  nullptr, &view, &invalid),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);
    EXPECT_EQ(invalid, UINT64_MAX);
}

}  // namespace
