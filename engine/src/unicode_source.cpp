#include "laplace/unicode_root.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "blake3.h"
#if defined(LAPLACE_UNICODE_SOURCE_MANIFEST_HEADER)
#include LAPLACE_UNICODE_SOURCE_MANIFEST_HEADER
#else
#include "laplace/contract/unicode-source-manifest.h"
#endif

namespace {

constexpr std::string_view SourceDomain{"laplace-unicode-source-fingerprint-v1"};
constexpr std::string_view RecipeDomain{"laplace-unicode-recipe-fingerprint-v1"};
constexpr std::string_view FileSetDomain{"laplace-unicode-verified-file-set-v1"};
constexpr std::string_view ReceiptDomain{"laplace-unicode-source-receipt-v1"};

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) : value_(value) {}
    ~FileDescriptor() { Reset(); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.value_) {
        other.value_ = -1;
    }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            Reset();
            value_ = other.value_;
            other.value_ = -1;
        }
        return *this;
    }
    int Get() const { return value_; }
    bool Valid() const { return value_ >= 0; }
private:
    void Reset() {
        if (value_ >= 0) {
            (void)::close(value_);
            value_ = -1;
        }
    }
    int value_;
};

constexpr std::array<std::uint32_t, 64> Sha256Constants{{
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u}};

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count) {
    return (value >> count) | (value << (32u - count));
}

void Sha256Block(const std::uint8_t* block, std::array<std::uint32_t, 8>& state) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16u; ++index) {
        words[index] = (static_cast<std::uint32_t>(block[index * 4u]) << 24u) |
            (static_cast<std::uint32_t>(block[index * 4u + 1u]) << 16u) |
            (static_cast<std::uint32_t>(block[index * 4u + 2u]) << 8u) |
            static_cast<std::uint32_t>(block[index * 4u + 3u]);
    }
    for (std::size_t index = 16u; index < words.size(); ++index) {
        const std::uint32_t s0 = RotateRight(words[index - 15u], 7u) ^
            RotateRight(words[index - 15u], 18u) ^ (words[index - 15u] >> 3u);
        const std::uint32_t s1 = RotateRight(words[index - 2u], 17u) ^
            RotateRight(words[index - 2u], 19u) ^ (words[index - 2u] >> 10u);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const std::uint32_t upper = RotateRight(e, 6u) ^ RotateRight(e, 11u) ^ RotateRight(e, 25u);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 = h + upper + choose + Sha256Constants[index] + words[index];
        const std::uint32_t lower = RotateRight(a, 2u) ^ RotateRight(a, 13u) ^ RotateRight(a, 22u);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = lower + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::array<std::uint8_t, 32> Sha256(const std::vector<std::uint8_t>& bytes) {
    std::array<std::uint32_t, 8> state{{
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u}};
    std::size_t offset = 0u;
    while (bytes.size() - offset >= 64u) {
        Sha256Block(bytes.data() + offset, state);
        offset += 64u;
    }
    std::array<std::uint8_t, 128> tail{};
    const std::size_t remainder = bytes.size() - offset;
    if (remainder != 0u) {
        std::memcpy(tail.data(), bytes.data() + offset, remainder);
    }
    tail[remainder] = 0x80u;
    const std::size_t padded = remainder < 56u ? 64u : 128u;
    const std::uint64_t bit_count = static_cast<std::uint64_t>(bytes.size()) * 8u;
    for (std::size_t index = 0; index < 8u; ++index) {
        tail[padded - 1u - index] = static_cast<std::uint8_t>(bit_count >> (index * 8u));
    }
    Sha256Block(tail.data(), state);
    if (padded == 128u) {
        Sha256Block(tail.data() + 64u, state);
    }
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        digest[index * 4u] = static_cast<std::uint8_t>(state[index] >> 24u);
        digest[index * 4u + 1u] = static_cast<std::uint8_t>(state[index] >> 16u);
        digest[index * 4u + 2u] = static_cast<std::uint8_t>(state[index] >> 8u);
        digest[index * 4u + 3u] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

void HashU64(blake3_hasher& hasher, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashString(blake3_hasher& hasher, std::string_view value) {
    HashU64(hasher, value.size());
    blake3_hasher_update(&hasher, value.data(), value.size());
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

FileDescriptor OpenBeneath(int root, std::string_view path) {
    if (path.empty() || path.front() == '/' || path.back() == '/') {
        return FileDescriptor{};
    }
    FileDescriptor current(::dup(root));
    if (!current.Valid()) {
        return FileDescriptor{};
    }
    std::size_t offset = 0u;
    while (offset < path.size()) {
        const std::size_t separator = path.find('/', offset);
        const bool final = separator == std::string_view::npos;
        const std::size_t length = final ? path.size() - offset : separator - offset;
        const std::string_view component = path.substr(offset, length);
        if (component.empty() || component == "." || component == "..") {
            return FileDescriptor{};
        }
        const std::string name(component);
        const int flags = final
            ? O_RDONLY | O_CLOEXEC | O_NOFOLLOW
            : O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY;
        FileDescriptor next(::openat(current.Get(), name.c_str(), flags));
        if (!next.Valid()) {
            return FileDescriptor{};
        }
        current = std::move(next);
        if (final) {
            break;
        }
        offset = separator + 1u;
    }
    return current;
}

laplace_unicode_status ReadExactSource(
    int root,
    const laplace_unicode_generated_source_file& source,
    std::vector<std::uint8_t>& bytes) {
    FileDescriptor file = OpenBeneath(root, source.relative_path);
    struct stat information {};
    if (!file.Valid() || ::fstat(file.Get(), &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        static_cast<std::uint64_t>(information.st_size) != source.expected_bytes ||
        source.expected_bytes > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_UNICODE_SOURCE_FILE_INVALID;
    }
    bytes.resize(static_cast<std::size_t>(source.expected_bytes));
    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(file.Get(), bytes.data() + offset, bytes.size() - offset);
        if (count <= 0) {
            return LAPLACE_UNICODE_SOURCE_FILE_INVALID;
        }
        offset += static_cast<std::size_t>(count);
    }
    std::uint8_t extra = 0u;
    if (::read(file.Get(), &extra, 1u) != 0) {
        return LAPLACE_UNICODE_SOURCE_FILE_INVALID;
    }
    const auto digest = Sha256(bytes);
    const bool digest_matches =
        std::memcmp(digest.data(), source.expected_sha256, digest.size()) == 0;
#if defined(LAPLACE_TEST_SKIP_UNICODE_SOURCE_DIGEST_VALIDATION)
    (void)digest_matches;
#else
    if (!digest_matches) {
        return LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH;
    }
#endif
    const std::string_view marker(source.version_marker);
    if (!marker.empty() && std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) == bytes.end()) {
        return LAPLACE_UNICODE_SOURCE_VERSION_MISMATCH;
    }
    return LAPLACE_UNICODE_OK;
}

laplace_digest256 SourceFingerprint() {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, SourceDomain);
    HashString(hasher, LAPLACE_UNICODE_GENERATED_VERSION);
    HashU64(hasher, LAPLACE_UNICODE_GENERATED_SOURCE_COUNT);
    for (const auto& source : laplace_unicode_generated_sources) {
        HashString(hasher, source.relative_path);
        HashU64(hasher, source.expected_bytes);
        blake3_hasher_update(&hasher, source.expected_sha256, sizeof(source.expected_sha256));
    }
    return Finish(hasher);
}

laplace_digest256 RecipeFingerprint() {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, RecipeDomain);
    HashU64(hasher, LAPLACE_UNICODE_GENERATED_CONTRACT_COUNT);
    for (const auto& contract : laplace_unicode_generated_contracts) {
        HashString(hasher, contract.name);
        blake3_hasher_update(&hasher, contract.sha256, sizeof(contract.sha256));
    }
    return Finish(hasher);
}

}  // namespace

extern "C" laplace_unicode_status laplace_unicode_source_verify(
    const char* source_root,
    laplace_unicode_source_receipt* receipt) {
    if (source_root == nullptr || source_root[0] == '\0' || receipt == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    std::memset(receipt, 0, sizeof(*receipt));
    FileDescriptor root(::open(source_root, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY));
    if (!root.Valid()) {
        receipt->status = LAPLACE_UNICODE_SOURCE_ROOT_INVALID;
        return LAPLACE_UNICODE_SOURCE_ROOT_INVALID;
    }
    blake3_hasher file_set_hasher{};
    blake3_hasher_init(&file_set_hasher);
    HashString(file_set_hasher, FileSetDomain);
    HashU64(file_set_hasher, LAPLACE_UNICODE_GENERATED_SOURCE_COUNT);
    std::vector<std::uint8_t> bytes;
    for (const auto& source : laplace_unicode_generated_sources) {
        const laplace_unicode_status status = ReadExactSource(root.Get(), source, bytes);
        if (status != LAPLACE_UNICODE_OK) {
            receipt->status = status;
            return status;
        }
        HashString(file_set_hasher, source.relative_path);
        HashU64(file_set_hasher, source.expected_bytes);
        blake3_hasher_update(&file_set_hasher, source.expected_sha256,
                             sizeof(source.expected_sha256));
        receipt->total_source_bytes += source.expected_bytes;
        receipt->verified_file_count += 1u;
    }
    receipt->source_fingerprint = SourceFingerprint();
    receipt->recipe_fingerprint = RecipeFingerprint();
    receipt->verified_file_set_fingerprint = Finish(file_set_hasher);
    blake3_hasher receipt_hasher{};
    blake3_hasher_init(&receipt_hasher);
    HashString(receipt_hasher, ReceiptDomain);
    blake3_hasher_update(&receipt_hasher, receipt->source_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->recipe_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher,
                         receipt->verified_file_set_fingerprint.bytes, 32u);
    HashU64(receipt_hasher, receipt->total_source_bytes);
    HashU64(receipt_hasher, receipt->verified_file_count);
    receipt->receipt_id = Finish(receipt_hasher);
    receipt->status = LAPLACE_UNICODE_OK;
    return LAPLACE_UNICODE_OK;
}
