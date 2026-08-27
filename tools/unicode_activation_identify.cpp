#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "blake3.h"

namespace {

constexpr std::uint64_t kMaximumRequestBytes = 4U * 1024U * 1024U;

struct Field final {
    std::string_view name;
    std::string_view domain;
    std::size_t output_bytes;
};

constexpr std::array<Field, 14> kFields{{
    {"request_fingerprint", "laplace.unicode-product-activation.request/v1", 32U},
    {"activation_epoch_id", "laplace.unicode-product-activation.epoch-id/v1", 16U},
    {"activation_epoch_fingerprint", "laplace.unicode-product-activation.epoch-fingerprint/v1", 32U},
    {"authority_fingerprint", "laplace.unicode-product-activation.authority/v1", 32U},
    {"source_epoch", "laplace.unicode-product-activation.context.source/v1", 32U},
    {"identity_epoch", "laplace.unicode-product-activation.context.identity/v1", 32U},
    {"geometry_epoch", "laplace.unicode-product-activation.context.geometry/v1", 32U},
    {"evidence_epoch", "laplace.unicode-product-activation.context.evidence/v1", 32U},
    {"firmware_epoch", "laplace.unicode-product-activation.context.firmware/v1", 32U},
    {"dependency_epoch", "laplace.unicode-product-activation.context.dependency/v1", 32U},
    {"database_epoch", "laplace.unicode-product-activation.context.database/v1", 32U},
    {"perfcache_epoch", "laplace.unicode-product-activation.context.perfcache/v1", 32U},
    {"numeric_epoch", "laplace.unicode-product-activation.context.numeric/v1", 32U},
    {"package_epoch", "laplace.unicode-product-activation.context.package/v1", 32U},
}};

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> encoded{};
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        encoded[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(&hasher, encoded.data(), encoded.size());
}

std::string Hex(const std::array<std::uint8_t, 32>& digest,
                const std::size_t output_bytes) {
    constexpr std::array<char, 16> alphabet{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string output(output_bytes * 2U, '0');
    for (std::size_t index = 0; index < output_bytes; ++index) {
        output[index * 2U] = alphabet[digest[index] >> 4U];
        output[index * 2U + 1U] = alphabet[digest[index] & 0x0fU];
    }
    return output;
}

std::string Identify(const Field& field, const std::vector<std::uint8_t>& request) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashU64(hasher, static_cast<std::uint64_t>(field.domain.size()));
    blake3_hasher_update(&hasher, field.domain.data(), field.domain.size());
    HashU64(hasher, static_cast<std::uint64_t>(request.size()));
    if (!request.empty()) {
        blake3_hasher_update(&hasher, request.data(), request.size());
    }
    std::array<std::uint8_t, 32> digest{};
    blake3_hasher_finalize(&hasher, digest.data(), digest.size());
    return Hex(digest, field.output_bytes);
}

bool ReadRequest(const std::filesystem::path& path,
                 std::vector<std::uint8_t>& output) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        std::cerr << "activation request is not a regular file\n";
        return false;
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes == 0U || bytes > kMaximumRequestBytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        std::cerr << "activation request size is outside the accepted boundary\n";
        return false;
    }
    output.resize(static_cast<std::size_t>(bytes));
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(output.data()),
                     static_cast<std::streamsize>(output.size())) ||
        stream.peek() != std::char_traits<char>::eof()) {
        std::cerr << "activation request cannot be read exactly\n";
        return false;
    }
    return true;
}

}  // namespace

int main(const int argc, char** const argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--request") {
        std::cerr << "usage: laplace_unicode_activation_identify --request <canonical-json>\n";
        return 64;
    }
    std::vector<std::uint8_t> request;
    if (!ReadRequest(argv[2], request)) {
        return 65;
    }
    std::cout << "{\n  \"schema\": \"laplace.unicode-activation-identities/v1\"";
    for (const auto& field : kFields) {
        std::cout << ",\n  \"" << field.name << "\": \""
                  << Identify(field, request) << '"';
    }
    std::cout << "\n}\n";
    return std::cout.good() ? 0 : 74;
}
