#include "laplace/unicode_root.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::array<char, 72> pattern{};
        const std::string value = "/tmp/laplace-unicode-source-fixture-XXXXXX";
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

TEST(UnicodeSourceFixture, RejectsOneByteChange) {
    TemporaryDirectory fixture;
    ASSERT_FALSE(fixture.Path().empty());
    const fs::path file = fixture.Path() / "fixture.txt";
    std::ofstream(file, std::ios::binary).write("abd", 3);

    laplace_unicode_source_receipt receipt{};
    EXPECT_EQ(laplace_unicode_source_verify(fixture.Path().c_str(), &receipt),
              LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH);
    EXPECT_EQ(receipt.status, LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH);
}

}  // namespace
