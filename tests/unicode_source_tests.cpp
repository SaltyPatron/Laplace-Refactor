#include "laplace/unicode_root.h"
#include "laplace/contract/unicode-source-manifest.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

const fs::path& SourceRoot() {
    static const fs::path root = [] {
        const char* configured = std::getenv("LAPLACE_UNICODE_SOURCE_ROOT");
        return fs::path(configured != nullptr && configured[0] != '\0'
                            ? configured
                            : "/vault/Data/UCD/Public/UCD/latest");
    }();
    return root;
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::array<char, 64> pattern{};
        const std::string value = "/tmp/laplace-unicode-source-test-XXXXXX";
        std::memcpy(pattern.data(), value.c_str(), value.size() + 1u);
        char* created = ::mkdtemp(pattern.data());
        if (created != nullptr) {
            path_ = created;
        }
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

const laplace_unicode_atom_field* CoreField(
    const laplace_unicode_core_record_view& view,
    std::uint16_t field_id) {
    for (const auto& field : view.fields) {
        if (field.field_id == field_id) {
            return &field;
        }
    }
    return nullptr;
}

std::string FieldText(const laplace_unicode_atom_field* field) {
    if (field == nullptr || field->payload_bytes == 0u) {
        return {};
    }
    return std::string(
        reinterpret_cast<const char*>(field->payload), field->payload_bytes);
}

bool SourceAvailable() {
    return fs::is_directory(SourceRoot()) &&
        fs::is_regular_file(SourceRoot() / "ucd/UnicodeData.txt");
}

void CopyPinnedSourceSet(const fs::path& destination) {
    for (const auto& source : laplace_unicode_generated_sources) {
        const fs::path relative(source.relative_path);
        const fs::path target = destination / relative;
        fs::create_directories(target.parent_path());
        fs::copy_file(SourceRoot() / relative, target,
                      fs::copy_options::overwrite_existing);
    }
}

TEST(UnicodeSource, RejectsMissingRootAndInvalidArguments) {
    laplace_unicode_source_receipt receipt{};
    EXPECT_EQ(laplace_unicode_source_verify(nullptr, &receipt),
              LAPLACE_UNICODE_INVALID_ARGUMENT);
    EXPECT_EQ(laplace_unicode_source_verify("", &receipt),
              LAPLACE_UNICODE_INVALID_ARGUMENT);
    EXPECT_EQ(laplace_unicode_source_verify(
                  "/this/path/is/intentionally/not/a/unicode/source/root", &receipt),
              LAPLACE_UNICODE_SOURCE_ROOT_INVALID);
    EXPECT_EQ(receipt.status, LAPLACE_UNICODE_SOURCE_ROOT_INVALID);
}

TEST(UnicodeSource, VerifiesPinnedUnicode17CorpusAndReplaysBitExactly) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_receipt first{};
    laplace_unicode_source_receipt replay{};
    ASSERT_EQ(laplace_unicode_source_verify(SourceRoot().c_str(), &first),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(laplace_unicode_source_verify(SourceRoot().c_str(), &replay),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(first.status, LAPLACE_UNICODE_OK);
    EXPECT_EQ(first.verified_file_count, LAPLACE_UNICODE_GENERATED_SOURCE_COUNT);
    EXPECT_EQ(first.total_source_bytes, UINT64_C(20978722));
    EXPECT_EQ(std::memcmp(&first, &replay, sizeof(first)), 0);
    EXPECT_TRUE(SameDigest(first.source_fingerprint, replay.source_fingerprint));
    EXPECT_TRUE(SameDigest(first.recipe_fingerprint, replay.recipe_fingerprint));
}

TEST(UnicodeSource, RetainsTheExactVerifiedBytesForProducerParsing) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt retained{};
    laplace_unicode_source_receipt verified{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &retained),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(bundle, nullptr);
    ASSERT_EQ(laplace_unicode_source_verify(SourceRoot().c_str(), &verified),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(std::memcmp(&retained, &verified, sizeof(retained)), 0);

    laplace_unicode_source_file_view unicode_data{};
    ASSERT_EQ(laplace_unicode_source_bundle_file(
                  bundle, "ucd/UnicodeData.txt", &unicode_data),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(unicode_data.byte_count, UINT64_C(2198209));
    ASSERT_NE(unicode_data.bytes, nullptr);
    static constexpr std::array<std::uint8_t, 5> Prefix{{
        '0', '0', '0', '0', ';'}};
    EXPECT_EQ(std::memcmp(
                  unicode_data.bytes, Prefix.data(), Prefix.size()),
              0);

    EXPECT_EQ(laplace_unicode_source_bundle_file(
                  bundle, "ucd/not-authoritative.txt", &unicode_data),
              LAPLACE_UNICODE_SOURCE_FILE_INVALID);
    laplace_unicode_source_bundle_close(&bundle);
    EXPECT_EQ(bundle, nullptr);
}

TEST(UnicodeCoreProperties, ParsesCompleteSourceAndCanonicalizesPayloads) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* table = nullptr;
    laplace_unicode_core_summary summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(bundle, &table, &summary),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(summary.status, LAPLACE_UNICODE_OK);
    EXPECT_TRUE(SameDigest(summary.source_fingerprint, source.source_fingerprint));
    EXPECT_TRUE(SameDigest(summary.recipe_fingerprint, source.recipe_fingerprint));
    EXPECT_EQ(summary.unicode_data_row_count, 40575u);
    EXPECT_EQ(summary.unicode_data_range_count, 40555u);
    EXPECT_EQ(summary.bidi_missing_rule_count, 24u);
    EXPECT_GT(summary.bidi_range_count, 1u);
    EXPECT_EQ(summary.explicit_position_count, UINT64_C(299382));
    EXPECT_EQ(summary.canonical_decomposition_count, UINT64_C(2081));
    EXPECT_EQ(summary.compatibility_decomposition_count, UINT64_C(3833));
    EXPECT_EQ(summary.simple_case_mapping_position_count, UINT64_C(2989));

    laplace_unicode_core_record_view letter{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x41u, &letter),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(letter.position_class, LAPLACE_UNICODE_ASSIGNED_SCALAR);
    EXPECT_EQ(FieldText(CoreField(letter, 1u)), "Uppercase_Letter");
    ASSERT_NE(CoreField(letter, 2u), nullptr);
    ASSERT_EQ(CoreField(letter, 2u)->payload_bytes, 1u);
    EXPECT_EQ(CoreField(letter, 2u)->payload[0], 0u);
    EXPECT_EQ(FieldText(CoreField(letter, 3u)), "Left_To_Right");
    ASSERT_NE(CoreField(letter, 9u), nullptr);
    const std::array<std::uint8_t, 12> expected_lower{{
        1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0x61u, 0u, 0u, 0u}};
    ASSERT_EQ(CoreField(letter, 9u)->payload_bytes, expected_lower.size());
    EXPECT_EQ(std::memcmp(
                  CoreField(letter, 9u)->payload,
                  expected_lower.data(), expected_lower.size()),
              0);

    laplace_unicode_core_record_view canonical{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x00c0u, &canonical),
              LAPLACE_UNICODE_OK);
    const std::array<std::uint8_t, 8> expected_decomposition{{
        0x41u, 0u, 0u, 0u, 0u, 0x03u, 0u, 0u}};
    ASSERT_EQ(CoreField(canonical, 6u)->payload_bytes,
              expected_decomposition.size());
    EXPECT_EQ(std::memcmp(
                  CoreField(canonical, 6u)->payload,
                  expected_decomposition.data(), expected_decomposition.size()),
              0);

    laplace_unicode_core_record_view compatibility{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x00a0u, &compatibility),
              LAPLACE_UNICODE_OK);
    const std::array<std::uint8_t, 19> expected_compatibility{{
        7u, 0u, 0u, 0u, 1u, 0u, 0u, 0u,
        'N', 'o', 'b', 'r', 'e', 'a', 'k',
        0x20u, 0u, 0u, 0u}};
    ASSERT_EQ(CoreField(compatibility, 7u)->payload_bytes,
              expected_compatibility.size());
    EXPECT_EQ(std::memcmp(
                  CoreField(compatibility, 7u)->payload,
                  expected_compatibility.data(), expected_compatibility.size()),
              0);

    laplace_unicode_core_table_destroy(&table);
    EXPECT_EQ(table, nullptr);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeCoreProperties, ClassifiesEveryPositionAndAppliesBidiDefaults) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* table = nullptr;
    laplace_unicode_core_summary summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(bundle, &table, &summary),
              LAPLACE_UNICODE_OK);
    const std::array<std::uint64_t, 5> expected_classes{{
        UINT64_C(159866), UINT64_C(814664), UINT64_C(137468),
        UINT64_C(66), UINT64_C(2048)}};
    EXPECT_EQ(std::memcmp(
                  summary.position_class_counts, expected_classes.data(),
                  sizeof(summary.position_class_counts)),
              0);

    laplace_unicode_core_record_view unassigned_rtl{};
    ASSERT_EQ(laplace_unicode_core_table_record(
                  table, 0x0590u, &unassigned_rtl),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(unassigned_rtl.position_class,
              LAPLACE_UNICODE_UNASSIGNED_OR_RESERVED_SCALAR);
    EXPECT_EQ(FieldText(CoreField(unassigned_rtl, 1u)), "Unassigned");
    EXPECT_EQ(FieldText(CoreField(unassigned_rtl, 3u)), "Right_To_Left");

    laplace_unicode_core_record_view surrogate{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0xd800u, &surrogate),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(surrogate.position_class,
              LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS);
    EXPECT_EQ(FieldText(CoreField(surrogate, 1u)), "Surrogate");

    laplace_unicode_core_record_view invalid{};
    EXPECT_EQ(laplace_unicode_core_table_record(
                  table, LAPLACE_UNICODE_ROOT_POPULATION, &invalid),
              LAPLACE_UNICODE_POSITION_OUT_OF_RANGE);
    laplace_unicode_core_table_destroy(&table);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeSource, RejectsOneByteChangeInPinnedSourceSet) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    TemporaryDirectory fixture;
    ASSERT_FALSE(fixture.Path().empty());
    ASSERT_NO_THROW(CopyPinnedSourceSet(fixture.Path()));

    const fs::path target = fixture.Path() / "uca/allkeys.txt";
    std::fstream stream(target, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(stream.good());
    char byte = '\0';
    stream.read(&byte, 1);
    ASSERT_EQ(stream.gcount(), 1);
    byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x01u);
    stream.seekp(0);
    stream.write(&byte, 1);
    stream.close();

    laplace_unicode_source_receipt receipt{};
    EXPECT_EQ(laplace_unicode_source_verify(fixture.Path().c_str(), &receipt),
              LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH);
    EXPECT_EQ(receipt.status, LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH);
}

}  // namespace
