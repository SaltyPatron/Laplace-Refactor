#include "laplace/reference_topology.h"

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

laplace_reference_candidate Candidate(
    const std::uint8_t seed,
    const std::uint8_t local,
    const std::uint8_t name_space,
    const std::uint32_t flags) {
    laplace_reference_candidate value{};
    Fill(value.source_profile_id, 0x10u);
    value.key.kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
    Fill(value.key.authority, 0x30u);
    Fill(value.key.release, 0x50u);
    Fill(value.key.name_space, name_space);
    Fill(value.key.local_identifier, local);
    value.key.version = 1u;
    Fill(value.row_entity_id, static_cast<std::uint8_t>(0x70u + seed));
    Fill(value.field_entity_id, static_cast<std::uint8_t>(0x80u + seed));
    value.value_entity_id = value.key.local_identifier;
    value.source_ordinal = static_cast<std::uint64_t>(seed) + 1u;
    value.artifact_ordinal = 1u;
    value.row_ordinal = static_cast<std::uint64_t>(seed) + 1u;
    value.column_ordinal = 1u;
    value.rule_flags = flags;
    return value;
}

}  // namespace

TEST(ReferenceTopology, ResolvesScopedDeclarationsAndPreservesOccurrences) {
    const std::uint32_t endpoint = LAPLACE_REFERENCE_RULE_ENDPOINT;
    std::array<laplace_reference_candidate, 5> candidates{{
        Candidate(0u, 0x91u, 0x81u,
                  endpoint | LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION),
        Candidate(1u, 0x91u, 0x81u, endpoint),
        Candidate(2u, 0x92u, 0x81u, endpoint),
        Candidate(3u, 0x93u, 0x81u,
                  endpoint | LAPLACE_REFERENCE_RULE_RETIRED_DECLARATION),
        Candidate(4u, 0x93u, 0x81u, endpoint),
    }};
    std::array<laplace_reference_record, 5> records{};
    laplace_reference_topology_receipt receipt{};
    ASSERT_EQ(laplace_reference_topology_resolve_batch(
                  candidates.data(), candidates.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_TOPOLOGY_OK);
    EXPECT_EQ(receipt.occurrence_count, 5u);
    EXPECT_EQ(receipt.coordinate_count, 3u);
    EXPECT_EQ(receipt.present_count, 2u);
    EXPECT_EQ(receipt.retired_count, 2u);
    EXPECT_EQ(receipt.unresolved_count, 1u);
    EXPECT_EQ(records[0].disposition, LAPLACE_REFERENCE_DISPOSITION_PRESENT);
    EXPECT_EQ(records[1].disposition, LAPLACE_REFERENCE_DISPOSITION_PRESENT);
    EXPECT_EQ(records[2].disposition, LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED);
    EXPECT_EQ(records[3].disposition, LAPLACE_REFERENCE_DISPOSITION_RETIRED);
    EXPECT_EQ(records[4].disposition, LAPLACE_REFERENCE_DISPOSITION_RETIRED);
    EXPECT_EQ(std::memcmp(records[0].coordinate.coordinate.bytes,
                          records[1].coordinate.coordinate.bytes, 16u), 0);
    EXPECT_NE(std::memcmp(records[0].occurrence_id.bytes,
                          records[1].occurrence_id.bytes, 32u), 0);
}

TEST(ReferenceTopology, NamespaceRemainsInCoordinateScope) {
    const std::uint32_t endpoint = LAPLACE_REFERENCE_RULE_ENDPOINT;
    std::array<laplace_reference_candidate, 2> candidates{{
        Candidate(0u, 0x91u, 0x81u,
                  endpoint | LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION),
        Candidate(1u, 0x91u, 0x82u, endpoint),
    }};
    std::array<laplace_reference_record, 2> records{};
    laplace_reference_topology_receipt receipt{};
    ASSERT_EQ(laplace_reference_topology_resolve_batch(
                  candidates.data(), candidates.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_TOPOLOGY_OK);
    EXPECT_EQ(records[0].disposition, LAPLACE_REFERENCE_DISPOSITION_PRESENT);
    EXPECT_EQ(records[1].disposition, LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED);
    EXPECT_NE(std::memcmp(records[0].coordinate.coordinate.bytes,
                          records[1].coordinate.coordinate.bytes, 16u), 0);
}

TEST(ReferenceTopology, RejectsConflictAndDuplicateOccurrence) {
    const std::uint32_t endpoint = LAPLACE_REFERENCE_RULE_ENDPOINT;
    std::array<laplace_reference_candidate, 2> conflict{{
        Candidate(0u, 0x91u, 0x81u,
                  endpoint | LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION),
        Candidate(1u, 0x91u, 0x81u,
                  endpoint | LAPLACE_REFERENCE_RULE_RETIRED_DECLARATION),
    }};
    std::array<laplace_reference_record, 2> records{};
    laplace_reference_topology_receipt receipt{};
    EXPECT_EQ(laplace_reference_topology_resolve_batch(
                  conflict.data(), conflict.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_TOPOLOGY_DECLARATION_CONFLICT);

    std::array<laplace_reference_candidate, 2> duplicate{{
        Candidate(0u, 0x91u, 0x81u, endpoint),
        Candidate(0u, 0x92u, 0x82u, endpoint),
    }};
    duplicate[1].row_entity_id = duplicate[0].row_entity_id;
    duplicate[1].field_entity_id = duplicate[0].field_entity_id;
    duplicate[1].value_entity_id = duplicate[0].value_entity_id;
    duplicate[1].key.local_identifier = duplicate[0].key.local_identifier;
    EXPECT_EQ(laplace_reference_topology_resolve_batch(
                  duplicate.data(), duplicate.size(), records.data(),
                  &receipt, nullptr),
              LAPLACE_REFERENCE_TOPOLOGY_DUPLICATE_OCCURRENCE);
}
