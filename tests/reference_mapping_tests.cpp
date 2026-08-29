#include "laplace/reference_mapping.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

template <typename T>
void Fill(T& value, const std::uint8_t seed) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(&value);
    for (std::size_t index = 0u; index < sizeof(value); ++index) {
        bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_highway_coordinate Coordinate(
    const std::uint8_t seed,
    const std::uint32_t kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE) {
    laplace_highway_key key{};
    key.kind = kind;
    Fill(key.authority, static_cast<std::uint8_t>(seed + 0x10u));
    Fill(key.release, static_cast<std::uint8_t>(seed + 0x20u));
    Fill(key.name_space, static_cast<std::uint8_t>(seed + 0x30u));
    Fill(key.local_identifier, static_cast<std::uint8_t>(seed + 0x40u));
    key.version = 1u;
    laplace_highway_coordinate result{};
    EXPECT_EQ(laplace_highway_coordinate_calculate(&key, &result),
              LAPLACE_HIGHWAY_OK);
    return result;
}

laplace_reference_mapping_candidate Candidate(
    const std::uint8_t witness,
    const std::uint8_t left,
    const std::uint8_t right,
    const std::uint32_t flags = LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED) {
    laplace_reference_mapping_candidate value{};
    Fill(value.boundary_id, 0x10u);
    Fill(value.source_profile_id, static_cast<std::uint8_t>(0x20u + witness));
    Fill(value.left_reference_id, static_cast<std::uint8_t>(0x30u + witness));
    Fill(value.right_reference_id, static_cast<std::uint8_t>(0x40u + witness));
    value.left_coordinate = Coordinate(left);
    value.right_coordinate = Coordinate(right);
    Fill(value.relation_id, 0x50u);
    Fill(value.row_entity_id, static_cast<std::uint8_t>(0x60u + witness));
    Fill(value.left_field_entity_id, static_cast<std::uint8_t>(0x70u + witness));
    Fill(value.left_value_entity_id, static_cast<std::uint8_t>(0x80u + witness));
    Fill(value.right_field_entity_id, static_cast<std::uint8_t>(0x90u + witness));
    Fill(value.right_value_entity_id, static_cast<std::uint8_t>(0xa0u + witness));
    value.source_ordinal = static_cast<std::uint64_t>(witness) + 1u;
    value.artifact_ordinal = 1u;
    value.row_ordinal = static_cast<std::uint64_t>(witness) + 1u;
    value.relation_version = 1u;
    value.relation_kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
    value.flags = flags;
    value.left_disposition = LAPLACE_REFERENCE_DISPOSITION_PRESENT;
    value.right_disposition = LAPLACE_REFERENCE_DISPOSITION_PRESENT;
    return value;
}

}  // namespace

TEST(ReferenceMapping, FoldsWitnessOccurrencesWithoutMultiplyingPropositions) {
    std::array<laplace_reference_mapping_candidate, 3> candidates{{
        Candidate(0u, 0x01u, 0x02u),
        Candidate(1u, 0x01u, 0x02u),
        Candidate(2u, 0x01u, 0x03u),
    }};
    std::array<laplace_reference_mapping_record, 3> records{};
    laplace_reference_mapping_receipt receipt{};
    ASSERT_EQ(laplace_reference_mapping_resolve_batch(
                  candidates.data(), candidates.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_MAPPING_OK);
    EXPECT_EQ(receipt.occurrence_count, 3u);
    EXPECT_EQ(receipt.proposition_count, 2u);
    EXPECT_EQ(receipt.resolved_count, 3u);
    EXPECT_EQ(std::memcmp(records[0].proposition_id.bytes,
                          records[1].proposition_id.bytes, 32u), 0);
    EXPECT_NE(std::memcmp(records[0].occurrence_id.bytes,
                          records[1].occurrence_id.bytes, 32u), 0);
}

TEST(ReferenceMapping, PreservesDirectionAndCanonicalizesOnlySymmetricRelations) {
    std::array<laplace_reference_mapping_candidate, 4> candidates{{
        Candidate(0u, 0x01u, 0x02u),
        Candidate(1u, 0x02u, 0x01u),
        Candidate(2u, 0x01u, 0x02u, LAPLACE_REFERENCE_MAPPING_FLAG_SYMMETRIC),
        Candidate(3u, 0x02u, 0x01u, LAPLACE_REFERENCE_MAPPING_FLAG_SYMMETRIC),
    }};
    std::array<laplace_reference_mapping_record, 4> records{};
    laplace_reference_mapping_receipt receipt{};
    ASSERT_EQ(laplace_reference_mapping_resolve_batch(
                  candidates.data(), candidates.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_MAPPING_OK);
    EXPECT_NE(std::memcmp(records[0].proposition_id.bytes,
                          records[1].proposition_id.bytes, 32u), 0);
    EXPECT_EQ(std::memcmp(records[2].proposition_id.bytes,
                          records[3].proposition_id.bytes, 32u), 0);
    EXPECT_EQ(receipt.proposition_count, 3u);
}

TEST(ReferenceMapping, KeepsUnresolvedAndRetiredEndpointsDistinct) {
    std::array<laplace_reference_mapping_candidate, 4> candidates{{
        Candidate(0u, 0x01u, 0x02u),
        Candidate(1u, 0x03u, 0x04u),
        Candidate(2u, 0x05u, 0x06u),
        Candidate(3u, 0x07u, 0x08u),
    }};
    candidates[1].left_disposition = LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED;
    candidates[2].right_disposition = LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED;
    candidates[3].left_disposition = LAPLACE_REFERENCE_DISPOSITION_RETIRED;
    std::array<laplace_reference_mapping_record, 4> records{};
    laplace_reference_mapping_receipt receipt{};
    ASSERT_EQ(laplace_reference_mapping_resolve_batch(
                  candidates.data(), candidates.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_MAPPING_OK);
    EXPECT_EQ(records[0].disposition,
              LAPLACE_REFERENCE_MAPPING_DISPOSITION_RESOLVED);
    EXPECT_EQ(records[1].disposition,
              LAPLACE_REFERENCE_MAPPING_DISPOSITION_LEFT_UNRESOLVED);
    EXPECT_EQ(records[2].disposition,
              LAPLACE_REFERENCE_MAPPING_DISPOSITION_RIGHT_UNRESOLVED);
    EXPECT_EQ(records[3].disposition,
              LAPLACE_REFERENCE_MAPPING_DISPOSITION_RETIRED_ENDPOINT);
    EXPECT_EQ(receipt.resolved_count, 1u);
    EXPECT_EQ(receipt.unresolved_count, 2u);
    EXPECT_EQ(receipt.retired_count, 1u);
}

TEST(ReferenceMapping, RejectsDuplicateOccurrenceAndBoundaryMixing) {
    std::array<laplace_reference_mapping_candidate, 2> duplicate{{
        Candidate(0u, 0x01u, 0x02u),
        Candidate(0u, 0x03u, 0x04u),
    }};
    duplicate[1].row_entity_id = duplicate[0].row_entity_id;
    duplicate[1].left_field_entity_id = duplicate[0].left_field_entity_id;
    duplicate[1].left_value_entity_id = duplicate[0].left_value_entity_id;
    duplicate[1].right_field_entity_id = duplicate[0].right_field_entity_id;
    duplicate[1].right_value_entity_id = duplicate[0].right_value_entity_id;
    std::array<laplace_reference_mapping_record, 2> records{};
    laplace_reference_mapping_receipt receipt{};
    EXPECT_EQ(laplace_reference_mapping_resolve_batch(
                  duplicate.data(), duplicate.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_MAPPING_DUPLICATE_OCCURRENCE);

    auto mixed = duplicate;
    mixed[1] = Candidate(1u, 0x03u, 0x04u);
    Fill(mixed[1].boundary_id, 0xe0u);
    EXPECT_EQ(laplace_reference_mapping_resolve_batch(
                  mixed.data(), mixed.size(), records.data(), &receipt, nullptr),
              LAPLACE_REFERENCE_MAPPING_BOUNDARY_MISMATCH);
}
