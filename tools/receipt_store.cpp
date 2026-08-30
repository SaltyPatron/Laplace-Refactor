#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "blake3.h"

namespace {

constexpr std::uint64_t kMaximumReceiptBytes = 16U * 1024U * 1024U;
constexpr std::size_t kDigestBytes = 32U;
constexpr std::size_t kDigestHexBytes = kDigestBytes * 2U;

bool IsLowerHexDigest(const std::string_view value) {
    if (value.size() != kDigestHexBytes) return false;
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::string Hex(const std::array<std::uint8_t, kDigestBytes>& digest) {
    constexpr std::array<char, 16> alphabet{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string output(kDigestHexBytes, '0');
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        output[index * 2U] = alphabet[digest[index] >> 4U];
        output[index * 2U + 1U] = alphabet[digest[index] & 0x0fU];
    }
    return output;
}

std::string Digest(const std::vector<std::uint8_t>& bytes) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    if (!bytes.empty()) {
        blake3_hasher_update(&hasher, bytes.data(), bytes.size());
    }
    std::array<std::uint8_t, kDigestBytes> digest{};
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());
    return Hex(digest);
}

bool ReadRegularFile(const std::filesystem::path& path,
                     std::vector<std::uint8_t>& output,
                     const char* const description) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        std::cerr << description << " is not a regular non-symlink file\n";
        return false;
    }
    const auto byte_count = std::filesystem::file_size(path, error);
    if (error || byte_count == 0U || byte_count > kMaximumReceiptBytes ||
        byte_count > static_cast<std::uintmax_t>(
                         std::numeric_limits<std::size_t>::max())) {
        std::cerr << description << " size is outside the accepted boundary\n";
        return false;
    }
    output.resize(static_cast<std::size_t>(byte_count));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(output.data()),
                    static_cast<std::streamsize>(output.size())) ||
        input.peek() != std::char_traits<char>::eof()) {
        std::cerr << description << " cannot be read exactly\n";
        return false;
    }
    return true;
}

bool OpenStoreRoot(const std::filesystem::path& root, int& descriptor) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(status)) {
        std::cerr << "receipt store root is not a physical directory\n";
        return false;
    }
    descriptor = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        std::cerr << "receipt store root cannot be opened: " << std::strerror(errno)
                  << '\n';
        return false;
    }
    return true;
}

bool WriteAll(const int descriptor, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const ssize_t written = ::write(descriptor, bytes.data() + offset, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool VerifyStored(const std::filesystem::path& root,
                  const std::string_view expected_digest,
                  const std::vector<std::uint8_t>* const expected_bytes = nullptr) {
    const auto stored = root / (std::string(expected_digest) + ".receipt");
    std::vector<std::uint8_t> bytes;
    if (!ReadRegularFile(stored, bytes, "stored receipt")) return false;
    if (Digest(bytes) != expected_digest) {
        std::cerr << "stored receipt BLAKE3 identity mismatch\n";
        return false;
    }
    if (expected_bytes != nullptr && bytes != *expected_bytes) {
        std::cerr << "stored receipt replay bytes differ from input\n";
        return false;
    }
    return true;
}

int Put(const std::filesystem::path& receipt,
        const std::filesystem::path& root) {
    std::vector<std::uint8_t> bytes;
    if (!ReadRegularFile(receipt, bytes, "receipt")) return 65;
    const std::string digest = Digest(bytes);

    int root_descriptor = -1;
    if (!OpenStoreRoot(root, root_descriptor)) return 73;

    const auto target = root / (digest + ".receipt");
    std::error_code error;
    const auto existing = std::filesystem::symlink_status(target, error);
    if (!error && existing.type() != std::filesystem::file_type::not_found) {
        const bool valid = VerifyStored(root, digest, &bytes);
        ::close(root_descriptor);
        if (!valid) return 65;
        std::cout << digest << '\n';
        return std::cout.good() ? 0 : 74;
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        std::cerr << "cannot inspect receipt destination\n";
        ::close(root_descriptor);
        return 73;
    }

    std::filesystem::path temporary;
    int temporary_descriptor = -1;
    for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
        temporary = root / ("." + digest + "." + std::to_string(::getpid()) + "." +
                            std::to_string(attempt) + ".tmp");
        temporary_descriptor = ::open(temporary.c_str(),
                                      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                          O_NOFOLLOW,
                                      S_IRUSR | S_IWUSR | S_IRGRP);
        if (temporary_descriptor >= 0) break;
        if (errno != EEXIST) break;
    }
    if (temporary_descriptor < 0) {
        std::cerr << "cannot create receipt temporary file: " << std::strerror(errno)
                  << '\n';
        ::close(root_descriptor);
        return 73;
    }

    bool wrote = WriteAll(temporary_descriptor, bytes);
    if (wrote && ::fsync(temporary_descriptor) != 0) wrote = false;
    const int close_result = ::close(temporary_descriptor);
    if (close_result != 0) wrote = false;
    if (!wrote) {
        const int saved = errno;
        ::unlink(temporary.c_str());
        ::close(root_descriptor);
        std::cerr << "cannot durably write receipt: " << std::strerror(saved) << '\n';
        return 74;
    }

    if (::link(temporary.c_str(), target.c_str()) != 0) {
        const int link_error = errno;
        ::unlink(temporary.c_str());
        if (link_error != EEXIST || !VerifyStored(root, digest, &bytes)) {
            ::close(root_descriptor);
            std::cerr << "cannot publish receipt without replacement: "
                      << std::strerror(link_error) << '\n';
            return 73;
        }
    } else {
        if (::unlink(temporary.c_str()) != 0 || ::fsync(root_descriptor) != 0) {
            ::close(root_descriptor);
            std::cerr << "cannot commit receipt directory metadata\n";
            return 74;
        }
    }
    ::close(root_descriptor);

    if (!VerifyStored(root, digest, &bytes)) return 65;
    std::cout << digest << '\n';
    return std::cout.good() ? 0 : 74;
}

int Verify(const std::string_view digest, const std::filesystem::path& root) {
    if (!IsLowerHexDigest(digest)) {
        std::cerr << "receipt digest must be 64 lowercase hexadecimal characters\n";
        return 64;
    }
    int root_descriptor = -1;
    if (!OpenStoreRoot(root, root_descriptor)) return 73;
    ::close(root_descriptor);
    return VerifyStored(root, digest) ? 0 : 65;
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc != 6) {
        std::cerr << "usage: laplace_receipt_store put --receipt FILE --root DIRECTORY\n"
                  << "       laplace_receipt_store verify --digest BLAKE3 --root DIRECTORY\n";
        return 64;
    }
    const std::string_view command(argv[1]);
    if (command == "put" && std::string_view(argv[2]) == "--receipt" &&
        std::string_view(argv[4]) == "--root") {
        return Put(argv[3], argv[5]);
    }
    if (command == "verify" && std::string_view(argv[2]) == "--digest" &&
        std::string_view(argv[4]) == "--root") {
        return Verify(argv[3], argv[5]);
    }
    std::cerr << "invalid receipt-store command\n";
    return 64;
}
