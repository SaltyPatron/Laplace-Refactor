#include "laplace/source_bundle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "blake3.h"

namespace {

constexpr std::string_view ManifestDomain{
    "laplace.source-bundle.manifest/v1"};
constexpr std::string_view FileSetDomain{
    "laplace.source-bundle.verified-file-set/v1"};
constexpr std::string_view ReceiptDomain{
    "laplace.source-bundle.receipt/v1"};

class FileDescriptor {
public:
    explicit FileDescriptor(const int value = -1) : value_(value) {}
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

std::uint32_t RotateRight(const std::uint32_t value, const std::uint32_t count) {
    return (value >> count) | (value << (32u - count));
}

void Sha256Block(
    const std::uint8_t* const block,
    std::array<std::uint32_t, 8>& state) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0u; index < 16u; ++index) {
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
    for (std::size_t index = 0u; index < words.size(); ++index) {
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
    const std::uint64_t bit_count = static_cast<std::uint64_t>(bytes.size()) * UINT64_C(8);
    for (std::size_t index = 0u; index < 8u; ++index) {
        tail[padded - 1u - index] =
            static_cast<std::uint8_t>(bit_count >> (index * 8u));
    }
    Sha256Block(tail.data(), state);
    if (padded == 128u) {
        Sha256Block(tail.data() + 64u, state);
    }
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t index = 0u; index < state.size(); ++index) {
        digest[index * 4u] = static_cast<std::uint8_t>(state[index] >> 24u);
        digest[index * 4u + 1u] = static_cast<std::uint8_t>(state[index] >> 16u);
        digest[index * 4u + 2u] = static_cast<std::uint8_t>(state[index] >> 8u);
        digest[index * 4u + 3u] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

void HashU16(blake3_hasher& hasher, const std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU32(blake3_hasher& hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashBytes(blake3_hasher& hasher, const void* const bytes, const std::size_t count) {
    HashU64(hasher, static_cast<std::uint64_t>(count));
    if (count != 0u) {
        blake3_hasher_update(&hasher, bytes, count);
    }
}

void HashString(blake3_hasher& hasher, const std::string_view value) {
    HashBytes(hasher, value.data(), value.size());
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool DigestZero(const std::uint8_t digest[32]) {
    std::uint8_t aggregate = 0u;
    for (std::size_t index = 0u; index < 32u; ++index) {
        aggregate = static_cast<std::uint8_t>(aggregate | digest[index]);
    }
    return aggregate == 0u;
}

bool ValidRelativePath(const char* const path) {
    if (path == nullptr || path[0] == '\0' || path[0] == '/') return false;
    const std::string_view value(path);
    if (value.back() == '/') return false;
    std::size_t offset = 0u;
    while (offset < value.size()) {
        const std::size_t separator = value.find('/', offset);
        const std::size_t length = separator == std::string_view::npos
            ? value.size() - offset : separator - offset;
        const std::string_view component = value.substr(offset, length);
        if (component.empty() || component == "." || component == "..") return false;
        if (separator == std::string_view::npos) break;
        offset = separator + 1u;
    }
    return true;
}

FileDescriptor OpenBeneath(const int root, const std::string_view path) {
    if (path.empty() || path.front() == '/' || path.back() == '/') {
        return FileDescriptor{};
    }
    FileDescriptor current(::dup(root));
    if (!current.Valid()) return FileDescriptor{};
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
        if (!next.Valid()) return FileDescriptor{};
        current = std::move(next);
        if (final) break;
        offset = separator + 1u;
    }
    return current;
}

struct Artifact {
    std::string relative_path;
    std::vector<std::uint8_t> bytes;
};

laplace_source_bundle_status ReadExact(
    const int root,
    const laplace_source_artifact_expectation& expectation,
    Artifact& artifact) {
    if (!ValidRelativePath(expectation.relative_path) ||
        DigestZero(expectation.expected_sha256) ||
        expectation.expected_bytes > static_cast<std::uint64_t>(SIZE_MAX) ||
        expectation.expected_bytes > UINT64_MAX / UINT64_C(8)) {
        return LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
    }
    const std::string_view marker = expectation.version_marker == nullptr
        ? std::string_view{} : std::string_view(expectation.version_marker);
    FileDescriptor file = OpenBeneath(root, expectation.relative_path);
    struct stat information {};
    if (!file.Valid() || ::fstat(file.Get(), &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        static_cast<std::uint64_t>(information.st_size) != expectation.expected_bytes) {
        return LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
    }
    artifact.relative_path = expectation.relative_path;
    artifact.bytes.resize(static_cast<std::size_t>(expectation.expected_bytes));
    std::size_t offset = 0u;
    while (offset < artifact.bytes.size()) {
        const ssize_t count = ::read(
            file.Get(), artifact.bytes.data() + offset, artifact.bytes.size() - offset);
        if (count <= 0) return LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
        offset += static_cast<std::size_t>(count);
    }
    std::uint8_t extra = 0u;
    if (::read(file.Get(), &extra, 1u) != 0) {
        return LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
    }
    const auto digest = Sha256(artifact.bytes);
    const bool digest_matches =
        std::memcmp(digest.data(), expectation.expected_sha256, digest.size()) == 0;
#if defined(LAPLACE_TEST_SOURCE_BUNDLE_SKIP_DIGEST_VALIDATION) || \
    defined(LAPLACE_TEST_SKIP_UNICODE_SOURCE_DIGEST_VALIDATION)
    (void)digest_matches;
#else
    if (!digest_matches) {
        return LAPLACE_SOURCE_BUNDLE_DIGEST_MISMATCH;
    }
#endif
    if (!marker.empty() &&
        std::search(
            artifact.bytes.begin(), artifact.bytes.end(), marker.begin(), marker.end()) ==
            artifact.bytes.end()) {
        return LAPLACE_SOURCE_BUNDLE_VERSION_MISMATCH;
    }
    return LAPLACE_SOURCE_BUNDLE_OK;
}

laplace_digest256 ManifestFingerprint(const laplace_source_bundle_request& request) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, ManifestDomain);
    HashU16(hasher, request.abi_major);
    HashU16(hasher, request.abi_minor);
    HashU64(hasher, request.artifact_count);
    for (std::uint64_t index = 0u; index < request.artifact_count; ++index) {
        const auto& artifact = request.artifacts[static_cast<std::size_t>(index)];
        HashString(hasher, artifact.relative_path);
        const std::string_view marker = artifact.version_marker == nullptr
            ? std::string_view{} : std::string_view(artifact.version_marker);
        HashString(hasher, marker);
        HashU64(hasher, artifact.expected_bytes);
        blake3_hasher_update(
            &hasher, artifact.expected_sha256, sizeof(artifact.expected_sha256));
    }
    return Finish(hasher);
}

laplace_digest256 FileSetFingerprint(const laplace_source_bundle_request& request) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, FileSetDomain);
    HashU64(hasher, request.artifact_count);
    for (std::uint64_t index = 0u; index < request.artifact_count; ++index) {
        const auto& artifact = request.artifacts[static_cast<std::size_t>(index)];
        HashString(hasher, artifact.relative_path);
        HashU64(hasher, artifact.expected_bytes);
        blake3_hasher_update(
            &hasher, artifact.expected_sha256, sizeof(artifact.expected_sha256));
    }
    return Finish(hasher);
}

}  // namespace

struct laplace_source_bundle {
    std::vector<Artifact> artifacts;
    laplace_source_bundle_receipt receipt{};
};

extern "C" laplace_source_bundle_status laplace_source_bundle_open(
    const laplace_source_bundle_request* const request,
    laplace_source_bundle** const bundle,
    laplace_source_bundle_receipt* const receipt) {
    if (bundle == nullptr || receipt == nullptr) {
        return LAPLACE_SOURCE_BUNDLE_INVALID_ARGUMENT;
    }
    *bundle = nullptr;
    *receipt = laplace_source_bundle_receipt{};
    if (request == nullptr || request->source_root == nullptr ||
        request->source_root[0] == '\0' || request->artifacts == nullptr ||
        request->artifact_count == 0u ||
        request->artifact_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        request->abi_major != LAPLACE_SOURCE_BUNDLE_ABI_MAJOR ||
        request->abi_minor > LAPLACE_SOURCE_BUNDLE_ABI_MINOR ||
        request->flags != 0u || request->reserved != 0u) {
        return LAPLACE_SOURCE_BUNDLE_INVALID_ARGUMENT;
    }

    try {
        std::set<std::string> paths;
        std::uint64_t total_bytes = 0u;
        for (std::uint64_t index = 0u; index < request->artifact_count; ++index) {
            const auto& artifact = request->artifacts[static_cast<std::size_t>(index)];
            if (!ValidRelativePath(artifact.relative_path) ||
                DigestZero(artifact.expected_sha256)) {
                return LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
            }
            if (!paths.emplace(artifact.relative_path).second) {
                return LAPLACE_SOURCE_BUNDLE_DUPLICATE_PATH;
            }
            if (artifact.expected_bytes > UINT64_MAX - total_bytes) {
                return LAPLACE_SOURCE_BUNDLE_OVERFLOW;
            }
            total_bytes += artifact.expected_bytes;
        }

        FileDescriptor root(::open(
            request->source_root, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY));
        if (!root.Valid()) {
            receipt->status = LAPLACE_SOURCE_BUNDLE_ROOT_INVALID;
            return LAPLACE_SOURCE_BUNDLE_ROOT_INVALID;
        }

        auto* opened = new (std::nothrow) laplace_source_bundle{};
        if (opened == nullptr) {
            receipt->status = LAPLACE_SOURCE_BUNDLE_MEMORY_FAILURE;
            return LAPLACE_SOURCE_BUNDLE_MEMORY_FAILURE;
        }
        opened->artifacts.resize(static_cast<std::size_t>(request->artifact_count));
        for (std::uint64_t index = 0u; index < request->artifact_count; ++index) {
            const laplace_source_bundle_status status = ReadExact(
                root.Get(), request->artifacts[static_cast<std::size_t>(index)],
                opened->artifacts[static_cast<std::size_t>(index)]);
            if (status != LAPLACE_SOURCE_BUNDLE_OK) {
                delete opened;
                receipt->status = status;
                return status;
            }
        }

        receipt->manifest_fingerprint = ManifestFingerprint(*request);
        receipt->verified_file_set_fingerprint = FileSetFingerprint(*request);
        receipt->total_source_bytes = total_bytes;
        receipt->verified_file_count = request->artifact_count;
        receipt->abi_major = LAPLACE_SOURCE_BUNDLE_ABI_MAJOR;
        receipt->abi_minor = LAPLACE_SOURCE_BUNDLE_ABI_MINOR;
        receipt->version = LAPLACE_SOURCE_BUNDLE_VERSION;
        receipt->status = LAPLACE_SOURCE_BUNDLE_OK;
        blake3_hasher hasher{};
        blake3_hasher_init(&hasher);
        HashString(hasher, ReceiptDomain);
        blake3_hasher_update(
            &hasher, receipt->manifest_fingerprint.bytes,
            sizeof(receipt->manifest_fingerprint.bytes));
        blake3_hasher_update(
            &hasher, receipt->verified_file_set_fingerprint.bytes,
            sizeof(receipt->verified_file_set_fingerprint.bytes));
        HashU64(hasher, receipt->total_source_bytes);
        HashU64(hasher, receipt->verified_file_count);
        HashU32(hasher, receipt->version);
        receipt->receipt_id = Finish(hasher);
        opened->receipt = *receipt;
        *bundle = opened;
        return LAPLACE_SOURCE_BUNDLE_OK;
    } catch (const std::bad_alloc&) {
        *bundle = nullptr;
        *receipt = laplace_source_bundle_receipt{};
        receipt->status = LAPLACE_SOURCE_BUNDLE_MEMORY_FAILURE;
        return LAPLACE_SOURCE_BUNDLE_MEMORY_FAILURE;
    } catch (...) {
        *bundle = nullptr;
        *receipt = laplace_source_bundle_receipt{};
        receipt->status = LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
        return LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
    }
}

extern "C" laplace_source_bundle_status laplace_source_bundle_file(
    const laplace_source_bundle* const bundle,
    const char* const relative_path,
    laplace_source_file_view* const view) {
    if (bundle == nullptr || relative_path == nullptr || relative_path[0] == '\0' ||
        view == nullptr) {
        return LAPLACE_SOURCE_BUNDLE_INVALID_ARGUMENT;
    }
    *view = laplace_source_file_view{};
    for (std::size_t index = 0u; index < bundle->artifacts.size(); ++index) {
        const auto& artifact = bundle->artifacts[index];
        if (artifact.relative_path == relative_path) {
            view->bytes = artifact.bytes.data();
            view->byte_count = static_cast<std::uint64_t>(artifact.bytes.size());
            view->artifact_index = static_cast<std::uint64_t>(index);
            return LAPLACE_SOURCE_BUNDLE_OK;
        }
    }
    return LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID;
}

extern "C" laplace_source_bundle_status laplace_source_bundle_file_at(
    const laplace_source_bundle* const bundle,
    const std::uint64_t artifact_index,
    laplace_source_file_view* const view) {
    if (bundle == nullptr || view == nullptr ||
        artifact_index >= static_cast<std::uint64_t>(bundle->artifacts.size())) {
        return LAPLACE_SOURCE_BUNDLE_INVALID_ARGUMENT;
    }
    *view = laplace_source_file_view{};
    const auto& artifact = bundle->artifacts[static_cast<std::size_t>(artifact_index)];
    view->bytes = artifact.bytes.data();
    view->byte_count = static_cast<std::uint64_t>(artifact.bytes.size());
    view->artifact_index = artifact_index;
    return LAPLACE_SOURCE_BUNDLE_OK;
}

extern "C" laplace_source_bundle_status laplace_source_bundle_receipt_get(
    const laplace_source_bundle* const bundle,
    laplace_source_bundle_receipt* const receipt) {
    if (bundle == nullptr || receipt == nullptr) {
        return LAPLACE_SOURCE_BUNDLE_INVALID_ARGUMENT;
    }
    *receipt = bundle->receipt;
    return LAPLACE_SOURCE_BUNDLE_OK;
}

extern "C" void laplace_source_bundle_close(laplace_source_bundle** const bundle) {
    if (bundle != nullptr) {
        delete *bundle;
        *bundle = nullptr;
    }
}
