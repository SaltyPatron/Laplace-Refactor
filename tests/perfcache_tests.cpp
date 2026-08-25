#include "laplace/perfcache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <blake3.h>
#include <gtest/gtest.h>

namespace {

void Fill(std::uint8_t* destination, std::size_t count, std::uint8_t seed) {
    for (std::size_t index = 0; index < count; ++index) {
        destination[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_perfcache_contract Contract() {
    laplace_perfcache_contract contract{};
    Fill(contract.module_id.bytes, sizeof(contract.module_id.bytes), 0x10u);
    Fill(contract.key_schema_id.bytes, sizeof(contract.key_schema_id.bytes), 0x20u);
    Fill(contract.value_schema_id.bytes, sizeof(contract.value_schema_id.bytes), 0x30u);
    Fill(contract.activation_epoch_id.bytes,
         sizeof(contract.activation_epoch_id.bytes), 0x40u);
    Fill(contract.source_fingerprint.bytes,
         sizeof(contract.source_fingerprint.bytes), 0x50u);
    Fill(contract.recipe_fingerprint.bytes,
         sizeof(contract.recipe_fingerprint.bytes), 0x60u);
    Fill(contract.dependency_fingerprint.bytes,
         sizeof(contract.dependency_fingerprint.bytes), 0x70u);
    contract.key_bytes = 4u;
    contract.value_bytes = 8u;
    contract.flags = LAPLACE_PERFCACHE_FLAG_SORTED_UNIQUE_KEYS;
    return contract;
}

void WriteU64(std::uint8_t* destination, std::uint64_t value) {
    for (std::size_t index = 0; index < 8u; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
}

std::uint64_t ReadU64(const std::uint8_t* source) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8u; ++index) {
        value |= static_cast<std::uint64_t>(source[index]) << (index * 8u);
    }
    return value;
}

std::vector<std::uint8_t> Records() {
    std::vector<std::uint8_t> records(3u * 12u, 0u);
    for (std::size_t index = 0; index < 3u; ++index) {
        records[index * 12u + 3u] = static_cast<std::uint8_t>(index + 1u);
        WriteU64(records.data() + index * 12u + 4u,
                 static_cast<std::uint64_t>(index + 1u) * 10u);
    }
    return records;
}

const std::array<std::uint8_t, 18> Metadata{{
    '{', '"', 'p', 'l', 'a', 'n', 'e', '"', ':', '"', 't', 'e', 's', 't', '"', '}', '\n', '\0'
}};

laplace_perfcache_spec Spec(const std::vector<std::uint8_t>& records) {
    return laplace_perfcache_spec{
        Contract(),
        records.empty() ? nullptr : records.data(),
        static_cast<std::uint64_t>(records.size() / 12u),
        Metadata.data(),
        Metadata.size()
    };
}

std::vector<std::uint8_t> Build(const laplace_perfcache_spec& spec) {
    std::size_t measured = 0;
    EXPECT_EQ(laplace_perfcache_measure(&spec, &measured), LAPLACE_PERFCACHE_OK);
    std::vector<std::uint8_t> artifact(measured, 0u);
    std::size_t written = 0;
    EXPECT_EQ(laplace_perfcache_write(
                  &spec, artifact.data(), artifact.size(), &written),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(written, measured);
    return artifact;
}

void RecomputeDigest(std::vector<std::uint8_t>* artifact) {
    const std::uint64_t digest_offset = ReadU64(artifact->data() + 240u);
    ASSERT_LE(digest_offset + LAPLACE_PERFCACHE_DIGEST_BYTES, artifact->size());
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, artifact->data(), digest_offset);
    blake3_hasher_finalize(
        &hasher,
        artifact->data() + static_cast<std::size_t>(digest_offset),
        LAPLACE_PERFCACHE_DIGEST_BYTES);
}

laplace_perfcache_view Validate(
    const std::vector<std::uint8_t>& artifact,
    const laplace_perfcache_contract& contract) {
    laplace_perfcache_view view{};
    EXPECT_EQ(laplace_perfcache_validate(
                  artifact.data(), artifact.size(), &contract, &view),
              LAPLACE_PERFCACHE_OK);
    return view;
}

laplace_perfcache_status ValidateNumberRecord(
    void*,
    std::uint64_t,
    const std::uint8_t* record,
    std::uint32_t record_stride) {
    if (record_stride != 12u) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    const std::uint64_t key = record[3];
    const std::uint64_t value = ReadU64(record + 4u);
    return value == key * 10u
        ? LAPLACE_PERFCACHE_OK
        : LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
}

TEST(PerfcacheFormat, WritesAndValidatesOneExactArtifact) {
    const auto records = Records();
    const auto spec = Spec(records);
    const auto artifact = Build(spec);
    const auto view = Validate(artifact, spec.contract);
    EXPECT_EQ(view.artifact_bytes, artifact.size());
    EXPECT_EQ(view.record_count, 3u);
    EXPECT_EQ(view.record_stride, 12u);
    EXPECT_EQ(view.metadata_bytes, Metadata.size());
    EXPECT_EQ(std::memcmp(view.metadata, Metadata.data(), Metadata.size()), 0);
    EXPECT_EQ(std::memcmp(view.records, records.data(), records.size()), 0);
    EXPECT_EQ(std::memcmp(view.artifact_digest.bytes,
                          artifact.data() + artifact.size() - LAPLACE_PERFCACHE_DIGEST_BYTES,
                          LAPLACE_PERFCACHE_DIGEST_BYTES),
              0);
}

TEST(PerfcacheFormat, SerializationIsByteDeterministic) {
    const auto records = Records();
    const auto spec = Spec(records);
    EXPECT_EQ(Build(spec), Build(spec));
}

TEST(PerfcacheFormat, SmallOutputBufferIsNotModified) {
    const auto records = Records();
    const auto spec = Spec(records);
    std::size_t measured = 0;
    ASSERT_EQ(laplace_perfcache_measure(&spec, &measured), LAPLACE_PERFCACHE_OK);
    std::vector<std::uint8_t> buffer(measured - 1u, 0xa5u);
    const auto before = buffer;
    std::size_t required = 0;
    EXPECT_EQ(laplace_perfcache_write(
                  &spec, buffer.data(), buffer.size(), &required),
              LAPLACE_PERFCACHE_BUFFER_TOO_SMALL);
    EXPECT_EQ(required, measured);
    EXPECT_EQ(buffer, before);
}

TEST(PerfcacheFormat, RejectsSizeOverflowBeforeReadingRecords) {
    const auto records = Records();
    auto spec = Spec(records);
    spec.record_count = std::numeric_limits<std::uint64_t>::max();
    std::size_t measured = 0;
    EXPECT_EQ(laplace_perfcache_measure(&spec, &measured),
              LAPLACE_PERFCACHE_SIZE_OVERFLOW);
}

TEST(PerfcacheValidation, RejectsChangedContentWithoutChangedDigest) {
    const auto records = Records();
    const auto spec = Spec(records);
    auto artifact = Build(spec);
    artifact[LAPLACE_PERFCACHE_HEADER_BYTES + 4u] ^= 0x01u;
    laplace_perfcache_view view{};
    EXPECT_EQ(laplace_perfcache_validate(
                  artifact.data(), artifact.size(), &spec.contract, &view),
              LAPLACE_PERFCACHE_DIGEST_MISMATCH);
}

TEST(PerfcacheValidation, RejectsWrongExactContract) {
    const auto records = Records();
    const auto spec = Spec(records);
    const auto artifact = Build(spec);
    auto wrong_contract = spec.contract;
    wrong_contract.recipe_fingerprint.bytes[0] ^= 0x01u;
    laplace_perfcache_view view{};
    EXPECT_EQ(laplace_perfcache_validate(
                  artifact.data(), artifact.size(), &wrong_contract, &view),
              LAPLACE_PERFCACHE_CONTRACT_MISMATCH);
}

TEST(PerfcacheValidation, RejectsUnknownFlagsAndNonzeroReservedBytes) {
    const auto records = Records();
    const auto spec = Spec(records);
    auto unknown_flag = Build(spec);
    unknown_flag[16u] |= 0x02u;
    RecomputeDigest(&unknown_flag);
    laplace_perfcache_view view{};
    EXPECT_EQ(laplace_perfcache_validate(
                  unknown_flag.data(), unknown_flag.size(), &spec.contract, &view),
              LAPLACE_PERFCACHE_HEADER_INVALID);

    auto reserved = Build(spec);
    reserved[248u] = 1u;
    RecomputeDigest(&reserved);
    EXPECT_EQ(laplace_perfcache_validate(
                  reserved.data(), reserved.size(), &spec.contract, &view),
              LAPLACE_PERFCACHE_HEADER_INVALID);
}

TEST(PerfcacheValidation, RejectsTruncationExtensionAndSectionMismatch) {
    const auto records = Records();
    const auto spec = Spec(records);
    const auto artifact = Build(spec);
    laplace_perfcache_view view{};
    EXPECT_EQ(laplace_perfcache_validate(
                  artifact.data(), artifact.size() - 1u, &spec.contract, &view),
              LAPLACE_PERFCACHE_SECTION_INVALID);
    auto extended = artifact;
    extended.push_back(0u);
    EXPECT_EQ(laplace_perfcache_validate(
                  extended.data(), extended.size(), &spec.contract, &view),
              LAPLACE_PERFCACHE_SECTION_INVALID);
    auto section = artifact;
    section[209u] = 0u;
    EXPECT_EQ(laplace_perfcache_validate(
                  section.data(), section.size(), &spec.contract, &view),
              LAPLACE_PERFCACHE_SECTION_INVALID);
}

TEST(PerfcacheValidation, RejectsRechecksummedDuplicateKey) {
    const auto records = Records();
    const auto spec = Spec(records);
    auto artifact = Build(spec);
    std::memcpy(artifact.data() + LAPLACE_PERFCACHE_HEADER_BYTES + 12u,
                artifact.data() + LAPLACE_PERFCACHE_HEADER_BYTES,
                4u);
    RecomputeDigest(&artifact);
    laplace_perfcache_view view{};
    EXPECT_EQ(laplace_perfcache_validate(
                  artifact.data(), artifact.size(), &spec.contract, &view),
              LAPLACE_PERFCACHE_KEYS_NOT_SORTED_UNIQUE);
}

TEST(PerfcacheValidation, ModuleSemanticsRejectRechecksummedWrongValue) {
    const auto records = Records();
    const auto spec = Spec(records);
    auto artifact = Build(spec);
    artifact[LAPLACE_PERFCACHE_HEADER_BYTES + 12u + 4u] ^= 0x01u;
    RecomputeDigest(&artifact);
    const auto view = Validate(artifact, spec.contract);
    std::uint64_t invalid_index = std::numeric_limits<std::uint64_t>::max();
    EXPECT_EQ(laplace_perfcache_validate_records(
                  &view, ValidateNumberRecord, nullptr, &invalid_index),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);
    EXPECT_EQ(invalid_index, 1u);
}

TEST(PerfcacheLookup, BatchPreservesInputOrderHitsAndMisses) {
    const auto records = Records();
    const auto spec = Spec(records);
    const auto artifact = Build(spec);
    const auto view = Validate(artifact, spec.contract);
    const std::array<std::uint8_t, 20> keys{{
        0u, 0u, 0u, 3u,
        0u, 0u, 0u, 0u,
        0u, 0u, 0u, 1u,
        0u, 0u, 0u, 4u,
        0u, 0u, 0u, 2u
    }};
    std::array<std::uint64_t, 5> indexes{};
    std::array<std::uint8_t, 5> found{};
    ASSERT_EQ(laplace_perfcache_lookup_batch(
                  &view, keys.data(), found.size(), indexes.data(), found.data()),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(found, (std::array<std::uint8_t, 5>{{1u, 0u, 1u, 0u, 1u}}));
    EXPECT_EQ(indexes[0], 2u);
    EXPECT_EQ(indexes[1], std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(indexes[2], 0u);
    EXPECT_EQ(indexes[3], std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(indexes[4], 1u);
}

}  // namespace
