#include "laplace/perfcache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

void Fill(std::uint8_t* destination, std::size_t count, std::uint8_t seed) {
    for (std::size_t index = 0; index < count; ++index) {
        destination[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_perfcache_contract Contract(std::uint8_t epoch_seed) {
    laplace_perfcache_contract contract{};
    Fill(contract.module_id.bytes, sizeof(contract.module_id.bytes), 0x10u);
    Fill(contract.key_schema_id.bytes, sizeof(contract.key_schema_id.bytes), 0x20u);
    Fill(contract.value_schema_id.bytes, sizeof(contract.value_schema_id.bytes), 0x30u);
    Fill(contract.activation_epoch_id.bytes,
         sizeof(contract.activation_epoch_id.bytes), epoch_seed);
    Fill(contract.activation_epoch_fingerprint.bytes,
         sizeof(contract.activation_epoch_fingerprint.bytes), epoch_seed);
    Fill(contract.module_contract_fingerprint.bytes,
         sizeof(contract.module_contract_fingerprint.bytes), 0x50u);
    Fill(contract.source_fingerprint.bytes,
         sizeof(contract.source_fingerprint.bytes), epoch_seed);
    Fill(contract.recipe_fingerprint.bytes,
         sizeof(contract.recipe_fingerprint.bytes), 0x60u);
    Fill(contract.dependency_fingerprint.bytes,
         sizeof(contract.dependency_fingerprint.bytes), 0x70u);
    contract.key_bytes = 1u;
    contract.value_bytes = 1u;
    contract.access_law = LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED;
    return contract;
}

std::vector<std::uint8_t> Build(
    const laplace_perfcache_contract& contract,
    std::uint8_t value) {
    const std::array<std::uint8_t, 4> records{{1u, value, 2u, 20u}};
    const std::array<std::uint8_t, 2> metadata{{'{', '}'}};
    const laplace_perfcache_spec spec{
        contract, records.data(), 2u, metadata.data(), metadata.size()};
    std::size_t artifact_bytes = 0;
    EXPECT_EQ(laplace_perfcache_measure(&spec, &artifact_bytes),
              LAPLACE_PERFCACHE_OK);
    std::vector<std::uint8_t> artifact(artifact_bytes, 0u);
    EXPECT_EQ(laplace_perfcache_write(
                  &spec, artifact.data(), artifact.size(), &artifact_bytes),
              LAPLACE_PERFCACHE_OK);
    return artifact;
}

laplace_perfcache_status AcceptRecord(
    void*, std::uint64_t, const std::uint8_t*, std::uint32_t) {
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status RejectSecondRecord(
    void*, std::uint64_t record_index, const std::uint8_t*, std::uint32_t) {
    return record_index == 1u
        ? LAPLACE_PERFCACHE_SEMANTIC_MISMATCH
        : LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status Publish(
    const std::string& path,
    const std::vector<std::uint8_t>& artifact,
    const laplace_perfcache_contract& contract) {
    std::uint64_t invalid_record_index = std::numeric_limits<std::uint64_t>::max();
    return laplace_perfcache_publish_file(
        path.c_str(),
        artifact.data(),
        artifact.size(),
        &contract,
        AcceptRecord,
        nullptr,
        &invalid_record_index);
}

laplace_perfcache_status Open(
    const std::string& path,
    const laplace_perfcache_contract& contract,
    laplace_perfcache_mapping* mapping) {
    std::uint64_t invalid_record_index = std::numeric_limits<std::uint64_t>::max();
    return laplace_perfcache_mapping_open(
        path.c_str(),
        &contract,
        AcceptRecord,
        nullptr,
        &invalid_record_index,
        mapping);
}

std::string NewDirectory() {
    std::array<char, 48> path{};
    const char pattern[] = "/tmp/laplace-perfcache-test.XXXXXX";
    static_assert(sizeof(pattern) <= path.size());
    std::memcpy(path.data(), pattern, sizeof(pattern));
    char* created = mkdtemp(path.data());
    EXPECT_NE(created, nullptr);
    return created == nullptr ? std::string{} : std::string(created);
}

void RemoveDirectory(const std::string& directory, const std::string& file) {
    if (!file.empty()) {
        EXPECT_EQ(unlink(file.c_str()), 0);
    }
    EXPECT_EQ(rmdir(directory.c_str()), 0);
}

std::size_t DirectoryEntryCount(const std::string& directory) {
    DIR* stream = opendir(directory.c_str());
    EXPECT_NE(stream, nullptr);
    if (stream == nullptr) {
        return 0u;
    }
    std::size_t count = 0;
    while (const dirent* entry = readdir(stream)) {
        if (std::strcmp(entry->d_name, ".") != 0 &&
            std::strcmp(entry->d_name, "..") != 0) {
            ++count;
        }
    }
    EXPECT_EQ(closedir(stream), 0);
    return count;
}

TEST(PerfcacheFile, PublishesMapsAndClosesVerifiedArtifact) {
    const auto contract = Contract(0x40u);
    const auto artifact = Build(contract, 10u);
    const auto directory = NewDirectory();
    const auto path = directory + "/plane.bin";
    ASSERT_FALSE(directory.empty());
    ASSERT_EQ(Publish(path, artifact, contract),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(DirectoryEntryCount(directory), 1u);

    laplace_perfcache_mapping mapping{};
    ASSERT_EQ(Open(path, contract, &mapping),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(mapping.mapped_bytes, artifact.size());
    EXPECT_EQ(std::memcmp(mapping.mapped_address, artifact.data(), artifact.size()), 0);
    EXPECT_EQ(mapping.view.record_count, 2u);
    EXPECT_EQ(mapping.view.records[1], 10u);
    EXPECT_EQ(laplace_perfcache_mapping_close(&mapping), LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(mapping.mapped_address, nullptr);
    EXPECT_EQ(mapping.native_handle, -1);
    RemoveDirectory(directory, path);
}

TEST(PerfcacheFile, WrittenTemporaryInodeIsValidatedBeforeImmutableLink) {
    const auto contract = Contract(0x40u);
    const auto artifact = Build(contract, 10u);
    const auto directory = NewDirectory();
    const auto path = directory + "/verified-write.bin";
#if defined(LAPLACE_TEST_CORRUPT_PERFCACHE_TEMPORARY_WRITE)
    ASSERT_EQ(Publish(path, artifact, contract),
              LAPLACE_PERFCACHE_DIGEST_MISMATCH);
    EXPECT_NE(access(path.c_str(), F_OK), 0);
    EXPECT_EQ(DirectoryEntryCount(directory), 0u);
    RemoveDirectory(directory, std::string{});
#else
    ASSERT_EQ(Publish(path, artifact, contract), LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(DirectoryEntryCount(directory), 1u);
    RemoveDirectory(directory, path);
#endif
}

TEST(PerfcacheFile, PublishedArtifactIsImmutableAndExactReplayIsIdempotent) {
    const auto first_contract = Contract(0x40u);
    const auto second_contract = Contract(0x80u);
    const auto first = Build(first_contract, 10u);
    const auto second = Build(second_contract, 11u);
    const auto directory = NewDirectory();
    const auto path = directory + "/plane.bin";
    ASSERT_EQ(Publish(path, first, first_contract),
              LAPLACE_PERFCACHE_OK);
    laplace_perfcache_mapping existing{};
    ASSERT_EQ(Open(path, first_contract, &existing),
              LAPLACE_PERFCACHE_OK);

    ASSERT_EQ(Publish(path, first, first_contract),
              LAPLACE_PERFCACHE_OK);
    ASSERT_EQ(Publish(path, second, second_contract),
              LAPLACE_PERFCACHE_ARTIFACT_CONFLICT);
    laplace_perfcache_mapping current{};
    ASSERT_EQ(Open(path, first_contract, &current),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(existing.view.records[1], 10u);
    EXPECT_EQ(current.view.records[1], 10u);
    EXPECT_EQ(std::memcmp(existing.view.artifact_digest.bytes,
                          current.view.artifact_digest.bytes,
                          LAPLACE_PERFCACHE_DIGEST_BYTES),
              0);
    EXPECT_EQ(DirectoryEntryCount(directory), 1u);
    EXPECT_EQ(laplace_perfcache_mapping_close(&existing), LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(laplace_perfcache_mapping_close(&current), LAPLACE_PERFCACHE_OK);
    RemoveDirectory(directory, path);
}

TEST(PerfcacheFile, InvalidArtifactCannotReplacePublishedArtifact) {
    const auto contract = Contract(0x40u);
    const auto valid = Build(contract, 10u);
    auto corrupt = valid;
    corrupt[LAPLACE_PERFCACHE_HEADER_BYTES] ^= 0x01u;
    const auto directory = NewDirectory();
    const auto path = directory + "/plane.bin";
    ASSERT_EQ(Publish(path, valid, contract),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(Publish(path, corrupt, contract),
              LAPLACE_PERFCACHE_DIGEST_MISMATCH);
    laplace_perfcache_mapping mapping{};
    ASSERT_EQ(Open(path, contract, &mapping),
              LAPLACE_PERFCACHE_OK);
    EXPECT_EQ(std::memcmp(mapping.mapped_address, valid.data(), valid.size()), 0);
    EXPECT_EQ(DirectoryEntryCount(directory), 1u);
    EXPECT_EQ(laplace_perfcache_mapping_close(&mapping), LAPLACE_PERFCACHE_OK);
    RemoveDirectory(directory, path);
}

TEST(PerfcacheFile, MappingRejectsMissingAndNonRegularPaths) {
    const auto contract = Contract(0x40u);
    const auto directory = NewDirectory();
    laplace_perfcache_mapping mapping{};
    EXPECT_EQ(Open(directory + "/missing.bin", contract, &mapping),
              LAPLACE_PERFCACHE_FILE_OPEN_FAILED);
    EXPECT_EQ(Open(directory, contract, &mapping),
              LAPLACE_PERFCACHE_FILE_TYPE_INVALID);
    RemoveDirectory(directory, std::string{});
}

TEST(PerfcacheFile, SemanticFailureBlocksPublicationAndActivation) {
    const auto contract = Contract(0x40u);
    const auto artifact = Build(contract, 10u);
    const auto directory = NewDirectory();
    const auto path = directory + "/plane.bin";
    std::uint64_t invalid_record_index = std::numeric_limits<std::uint64_t>::max();
    EXPECT_EQ(laplace_perfcache_publish_file(
                  path.c_str(),
                  artifact.data(),
                  artifact.size(),
                  &contract,
                  RejectSecondRecord,
                  nullptr,
                  &invalid_record_index),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);
    EXPECT_EQ(invalid_record_index, 1u);
    EXPECT_NE(access(path.c_str(), F_OK), 0);

    ASSERT_EQ(Publish(path, artifact, contract), LAPLACE_PERFCACHE_OK);
    laplace_perfcache_mapping mapping{};
    invalid_record_index = std::numeric_limits<std::uint64_t>::max();
    EXPECT_EQ(laplace_perfcache_mapping_open(
                  path.c_str(),
                  &contract,
                  RejectSecondRecord,
                  nullptr,
                  &invalid_record_index,
                  &mapping),
              LAPLACE_PERFCACHE_SEMANTIC_MISMATCH);
    EXPECT_EQ(invalid_record_index, 1u);
    EXPECT_EQ(mapping.mapped_address, nullptr);
    RemoveDirectory(directory, path);
}

}  // namespace
