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
    EXPECT_EQ(first.total_source_bytes, UINT64_C(20805289));
    EXPECT_EQ(std::memcmp(&first, &replay, sizeof(first)), 0);
    EXPECT_TRUE(SameDigest(first.source_fingerprint, replay.source_fingerprint));
    EXPECT_TRUE(SameDigest(first.recipe_fingerprint, replay.recipe_fingerprint));
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
