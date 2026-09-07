#include "laplace/perfcache.h"
#include "laplace/perfcache_modules.h"
#include "laplace/perfcache_registry.h"
#include "laplace/unicode_root.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void Require(const bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Decode(const std::string_view input, std::uint8_t* output, const std::size_t count) {
    Require(input.size() == count * 2U, "digest has wrong width");
    const auto digit = [](const char c) -> unsigned int {
        if (c >= '0' && c <= '9') return static_cast<unsigned int>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned int>(c - 'a') + 10U;
        throw std::runtime_error("digest is not canonical lowercase hexadecimal");
    };
    for (std::size_t i = 0U; i < count; ++i) {
        output[i] = static_cast<std::uint8_t>((digit(input[i * 2U]) << 4U) | digit(input[i * 2U + 1U]));
    }
}
std::string Hex(const std::uint8_t* data, const std::size_t count) {
    const char alphabet[] = "0123456789abcdef";
    std::string output(count * 2U, '0');
    for (std::size_t i = 0U; i < count; ++i) {
        output[i * 2U] = alphabet[data[i] >> 4U];
        output[i * 2U + 1U] = alphabet[data[i] & 15U];
    }
    return output;
}
bool Same(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}
struct Mapping final {
    laplace_perfcache_mapping value{};
    bool opened{};
    ~Mapping() { if (opened) laplace_perfcache_mapping_close(&value); }
    void Open(const char* path, const laplace_perfcache_module_v2& module,
              const laplace_unicode_source_receipt& source, const laplace_id128& epoch,
              const laplace_digest256& fingerprint, const laplace_digest256& dependency) {
        laplace_perfcache_contract expected{};
        expected.module_id = module.module_id;
        expected.key_schema_id = module.key_schema_id;
        expected.value_schema_id = module.value_schema_id;
        expected.module_contract_fingerprint = module.module_contract_fingerprint;
        expected.key_bytes = module.key_bytes;
        expected.value_bytes = module.value_bytes;
        expected.access_law = module.access_law;
        expected.activation_epoch_id = epoch;
        expected.activation_epoch_fingerprint = fingerprint;
        expected.source_fingerprint = source.source_fingerprint;
        expected.recipe_fingerprint = source.recipe_fingerprint;
        expected.dependency_fingerprint = dependency;
        std::uint64_t invalid = UINT64_MAX;
        Require(laplace_perfcache_mapping_open(path, &expected, module.validate_record,
                    module.state, &invalid, &value) == LAPLACE_PERFCACHE_OK,
                "actual perfcache artifact failed native epoch/dependency/checksum/record validation");
        opened = true;
        Require(module.validate_view != nullptr &&
                module.validate_view(module.state, &value.view, &invalid) == LAPLACE_PERFCACHE_OK,
                "actual perfcache artifact failed native whole-view validation");
    }
};
std::uintmax_t ConfinedSize(const char* name, const std::filesystem::path& root) {
    const std::filesystem::path path(name);
    Require(path.is_absolute() && path == path.lexically_normal(), "artifact path must be canonical absolute");
    const auto relative = path.lexically_relative(root);
    Require(!relative.empty() && relative != "." && *relative.begin() != "..", "artifact is outside declared cache root");
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        Require(!std::filesystem::is_symlink(std::filesystem::symlink_status(current)), "artifact path contains a symlink");
    }
    Require(std::filesystem::is_regular_file(path), "artifact is not a regular file");
    return std::filesystem::file_size(path);
}
void VerifyEpoch(const Mapping& mapping, const laplace_id128& id, const laplace_digest256& fp) {
    Require(std::memcmp(mapping.value.view.contract.activation_epoch_id.bytes, id.bytes, sizeof(id.bytes)) == 0 &&
            Same(mapping.value.view.contract.activation_epoch_fingerprint, fp), "actual artifact activation epoch differs");
}
laplace_digest256 Project(const Mapping& mapping, const laplace_id128& id,
                         const laplace_digest256& fp, const laplace_digest256& dependency) {
    laplace_digest256 output{};
    Require(laplace_perfcache_reference_epoch_digest(mapping.value.view.artifact,
                mapping.value.view.artifact_bytes, &mapping.value.view.contract,
                &id, &fp, &dependency, &output) == LAPLACE_PERFCACHE_OK,
            "native reference-epoch projection failed");
    return output;
}
} // namespace

int main(const int argc, char** argv) {
    if (argc != 11) {
        std::cerr << "usage: laplace_unicode_artifact_verify <cache-root> <tier0> <reverse> <actual-id> <actual-fingerprint> <reference-id> <reference-fingerprint> <tier0-reference-digest> <reverse-reference-digest> <verified-unicode-source-root>\n";
        return 64;
    }
    try {
        const std::filesystem::path root(argv[1]);
        Require(root.is_absolute() && root == root.lexically_normal() && root != root.root_path(), "invalid declared cache root");
        const auto tier0_size = ConfinedSize(argv[2], root);
        const auto reverse_size = ConfinedSize(argv[3], root);
        constexpr std::uintmax_t memory_limit = UINT64_C(1073741824);
        Require(tier0_size <= memory_limit && reverse_size <= memory_limit - tier0_size,
                "artifact verification exceeds the declared 1 GiB mapping boundary");
        laplace_id128 actual_id{}, reference_id{};
        laplace_digest256 actual_fp{}, reference_fp{}, expected_tier0{}, expected_reverse{};
        Decode(argv[4], actual_id.bytes, sizeof(actual_id.bytes));
        Decode(argv[5], actual_fp.bytes, sizeof(actual_fp.bytes));
        Decode(argv[6], reference_id.bytes, sizeof(reference_id.bytes));
        Decode(argv[7], reference_fp.bytes, sizeof(reference_fp.bytes));
        Decode(argv[8], expected_tier0.bytes, sizeof(expected_tier0.bytes));
        Decode(argv[9], expected_reverse.bytes, sizeof(expected_reverse.bytes));
        laplace_perfcache_module_v2 tier0_module{}, reverse_module{};
        Require(laplace_perfcache_unicode_tier0_module(&tier0_module) == LAPLACE_PERFCACHE_REGISTRY_OK &&
                laplace_perfcache_unicode_identity_reverse_module(&reverse_module) == LAPLACE_PERFCACHE_REGISTRY_OK,
                "native Unicode module contracts unavailable");
        laplace_unicode_source_receipt source{};
        Require(laplace_unicode_source_verify(argv[10], &source) == LAPLACE_UNICODE_OK,
                "locked Unicode source verification failed");
        Mapping tier0, reverse;
        laplace_digest256 no_dependencies{}, live_dependency{}, reference_dependency{};
        Require(laplace_perfcache_dependency_fingerprint(nullptr, 0U, &no_dependencies) == LAPLACE_PERFCACHE_REGISTRY_OK,
                "native empty dependency fingerprint failed");
        tier0.Open(argv[2], tier0_module, source, actual_id, actual_fp, no_dependencies);
        laplace_perfcache_generation_dependency dependency{tier0_module.module_id, tier0.value.view.artifact_digest};
        Require(laplace_perfcache_dependency_fingerprint(&dependency, 1U, &live_dependency) == LAPLACE_PERFCACHE_REGISTRY_OK,
                "native dependency fingerprint failed");
        reverse.Open(argv[3], reverse_module, source, actual_id, actual_fp, live_dependency);
        VerifyEpoch(tier0, actual_id, actual_fp); VerifyEpoch(reverse, actual_id, actual_fp);
        const auto projected_tier0 = Project(tier0, reference_id, reference_fp, no_dependencies);
        Require(Same(projected_tier0, expected_tier0), "Tier0 reference content digest differs");
        dependency.artifact_digest = projected_tier0;
        Require(laplace_perfcache_dependency_fingerprint(&dependency, 1U, &reference_dependency) == LAPLACE_PERFCACHE_REGISTRY_OK,
                "reference dependency fingerprint failed");
        const auto projected_reverse = Project(reverse, reference_id, reference_fp, reference_dependency);
        Require(Same(projected_reverse, expected_reverse), "reverse reference content digest differs");
        std::cout << "{\"schema\":\"laplace.unicode-artifact-verification/v1\","
            << "\"activation_epoch_id\":\"" << Hex(actual_id.bytes, sizeof(actual_id.bytes)) << "\","
            << "\"activation_epoch_fingerprint\":\"" << Hex(actual_fp.bytes, sizeof(actual_fp.bytes)) << "\","
            << "\"tier0_artifact_digest\":\"" << Hex(tier0.value.view.artifact_digest.bytes, 32U) << "\","
            << "\"reverse_artifact_digest\":\"" << Hex(reverse.value.view.artifact_digest.bytes, 32U) << "\","
            << "\"tier0_reference_digest\":\"" << Hex(projected_tier0.bytes, 32U) << "\","
            << "\"reverse_reference_digest\":\"" << Hex(projected_reverse.bytes, 32U) << "\","
            << "\"reverse_dependency_module_id\":\"" << Hex(dependency.module_id.bytes, 16U) << "\","
            << "\"reverse_dependency_artifact_digest\":\"" << Hex(tier0.value.view.artifact_digest.bytes, 32U) << "\","
            << "\"tier0_artifact_bytes\":" << tier0.value.view.artifact_bytes << ","
            << "\"reverse_artifact_bytes\":" << reverse.value.view.artifact_bytes << ","
            << "\"perfcache_artifact_count\":2,\"perfcache_dependency_count\":1,\"artifact_bytes_modified\":false}\n";
        return std::cout.good() ? 0 : 74;
    } catch (const std::exception& error) {
        std::cerr << "unicode artifact verification: " << error.what() << '\n';
        return 65;
    }
}
