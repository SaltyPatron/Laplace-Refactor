#include "laplace/unicode_root.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string_view>
#include <vector>

#include "blake3.h"
#include "laplace/source_bundle.h"
#if defined(LAPLACE_UNICODE_SOURCE_MANIFEST_HEADER)
#include LAPLACE_UNICODE_SOURCE_MANIFEST_HEADER
/* The fixture/mutation targets compile this adapter directly rather than link the
 * product engine. Compile the exact same generic source owner into that test TU;
 * production builds own it once through engine/src/source_bundle.cpp. */
#include "source_bundle.cpp"
#else
#include "laplace/contract/unicode-source-manifest.h"
#endif

struct laplace_unicode_source_bundle {
    laplace_source_bundle* source = nullptr;
    laplace_unicode_source_receipt receipt{};
};

namespace {

constexpr std::string_view SourceDomain{"laplace-unicode-source-fingerprint-v1"};
constexpr std::string_view RecipeDomain{"laplace-unicode-recipe-fingerprint-v1"};
constexpr std::string_view FileSetDomain{"laplace-unicode-verified-file-set-v1"};
constexpr std::string_view ReceiptDomain{"laplace-unicode-source-receipt-v1"};

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (std::size_t index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes, sizeof(bytes));
}

void HashString(blake3_hasher& hasher, const std::string_view value) {
    HashU64(hasher, static_cast<std::uint64_t>(value.size()));
    if (!value.empty()) {
        blake3_hasher_update(&hasher, value.data(), value.size());
    }
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
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
        blake3_hasher_update(
            &hasher, source.expected_sha256, sizeof(source.expected_sha256));
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

laplace_digest256 VerifiedFileSetFingerprint() {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, FileSetDomain);
    HashU64(hasher, LAPLACE_UNICODE_GENERATED_SOURCE_COUNT);
    for (const auto& source : laplace_unicode_generated_sources) {
        HashString(hasher, source.relative_path);
        HashU64(hasher, source.expected_bytes);
        blake3_hasher_update(
            &hasher, source.expected_sha256, sizeof(source.expected_sha256));
    }
    return Finish(hasher);
}

laplace_digest256 ReceiptFingerprint(
    const laplace_unicode_source_receipt& receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, ReceiptDomain);
    blake3_hasher_update(
        &hasher, receipt.source_fingerprint.bytes,
        sizeof(receipt.source_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt.recipe_fingerprint.bytes,
        sizeof(receipt.recipe_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt.verified_file_set_fingerprint.bytes,
        sizeof(receipt.verified_file_set_fingerprint.bytes));
    HashU64(hasher, receipt.total_source_bytes);
    HashU64(hasher, receipt.verified_file_count);
    return Finish(hasher);
}

laplace_unicode_status MapStatus(const laplace_source_bundle_status status) {
    switch (status) {
        case LAPLACE_SOURCE_BUNDLE_OK:
            return LAPLACE_UNICODE_OK;
        case LAPLACE_SOURCE_BUNDLE_ROOT_INVALID:
            return LAPLACE_UNICODE_SOURCE_ROOT_INVALID;
        case LAPLACE_SOURCE_BUNDLE_DIGEST_MISMATCH:
            return LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH;
        case LAPLACE_SOURCE_BUNDLE_VERSION_MISMATCH:
            return LAPLACE_UNICODE_SOURCE_VERSION_MISMATCH;
        case LAPLACE_SOURCE_BUNDLE_MEMORY_FAILURE:
            return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
        case LAPLACE_SOURCE_BUNDLE_OVERFLOW:
            return LAPLACE_UNICODE_SIZE_OVERFLOW;
        case LAPLACE_SOURCE_BUNDLE_ARTIFACT_INVALID:
        case LAPLACE_SOURCE_BUNDLE_DUPLICATE_PATH:
            return LAPLACE_UNICODE_SOURCE_FILE_INVALID;
        case LAPLACE_SOURCE_BUNDLE_INVALID_ARGUMENT:
        default:
            return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
}

}  // namespace

extern "C" laplace_unicode_status laplace_unicode_source_bundle_open(
    const char* const source_root,
    laplace_unicode_source_bundle** const bundle,
    laplace_unicode_source_receipt* const receipt) {
    if (source_root == nullptr || source_root[0] == '\0' || bundle == nullptr ||
        receipt == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *bundle = nullptr;
    std::memset(receipt, 0, sizeof(*receipt));

    if (LAPLACE_UNICODE_GENERATED_SOURCE_COUNT > UINT32_MAX) {
        receipt->status = LAPLACE_UNICODE_SIZE_OVERFLOW;
        return LAPLACE_UNICODE_SIZE_OVERFLOW;
    }

    try {
        std::vector<laplace_source_artifact_expectation> artifacts;
        artifacts.resize(LAPLACE_UNICODE_GENERATED_SOURCE_COUNT);
        for (std::size_t index = 0u;
             index < LAPLACE_UNICODE_GENERATED_SOURCE_COUNT; ++index) {
            const auto& source = laplace_unicode_generated_sources[index];
            auto& artifact = artifacts[index];
            artifact.relative_path = source.relative_path;
            artifact.version_marker = source.version_marker;
            artifact.expected_bytes = source.expected_bytes;
            std::memcpy(
                artifact.expected_sha256, source.expected_sha256,
                sizeof(artifact.expected_sha256));
        }

        laplace_source_bundle_request request{};
        request.source_root = source_root;
        request.artifacts = artifacts.data();
        request.artifact_count = LAPLACE_UNICODE_GENERATED_SOURCE_COUNT;
        request.abi_major = LAPLACE_SOURCE_BUNDLE_ABI_MAJOR;
        request.abi_minor = LAPLACE_SOURCE_BUNDLE_ABI_MINOR;

        laplace_source_bundle* exact = nullptr;
        laplace_source_bundle_receipt exact_receipt{};
        const laplace_source_bundle_status source_status =
            laplace_source_bundle_open(&request, &exact, &exact_receipt);
        if (source_status != LAPLACE_SOURCE_BUNDLE_OK || exact == nullptr) {
            laplace_source_bundle_close(&exact);
            receipt->status = MapStatus(source_status);
            return static_cast<laplace_unicode_status>(receipt->status);
        }
        if (exact_receipt.verified_file_count !=
                LAPLACE_UNICODE_GENERATED_SOURCE_COUNT) {
            laplace_source_bundle_close(&exact);
            receipt->status = LAPLACE_UNICODE_SOURCE_INCOMPLETE;
            return LAPLACE_UNICODE_SOURCE_INCOMPLETE;
        }

        auto* opened = new (std::nothrow) laplace_unicode_source_bundle{};
        if (opened == nullptr) {
            laplace_source_bundle_close(&exact);
            receipt->status = LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
            return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
        }
        opened->source = exact;

        receipt->source_fingerprint = SourceFingerprint();
        receipt->recipe_fingerprint = RecipeFingerprint();
        receipt->verified_file_set_fingerprint = VerifiedFileSetFingerprint();
        receipt->total_source_bytes = exact_receipt.total_source_bytes;
        receipt->verified_file_count =
            static_cast<std::uint32_t>(exact_receipt.verified_file_count);
        receipt->status = LAPLACE_UNICODE_OK;
        receipt->receipt_id = ReceiptFingerprint(*receipt);
        opened->receipt = *receipt;
        *bundle = opened;
        return LAPLACE_UNICODE_OK;
    } catch (const std::bad_alloc&) {
        *bundle = nullptr;
        std::memset(receipt, 0, sizeof(*receipt));
        receipt->status = LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
        return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
    }
}

extern "C" laplace_unicode_status laplace_unicode_source_bundle_file(
    const laplace_unicode_source_bundle* const bundle,
    const char* const relative_path,
    laplace_unicode_source_file_view* const view) {
    if (bundle == nullptr || bundle->source == nullptr || relative_path == nullptr ||
        relative_path[0] == '\0' || view == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *view = laplace_unicode_source_file_view{};
    laplace_source_file_view source_view{};
    const laplace_source_bundle_status status = laplace_source_bundle_file(
        bundle->source, relative_path, &source_view);
    if (status != LAPLACE_SOURCE_BUNDLE_OK) {
        return MapStatus(status);
    }
    view->bytes = source_view.bytes;
    view->byte_count = source_view.byte_count;
    return LAPLACE_UNICODE_OK;
}

extern "C" laplace_unicode_status laplace_unicode_source_bundle_receipt(
    const laplace_unicode_source_bundle* const bundle,
    laplace_unicode_source_receipt* const receipt) {
    if (bundle == nullptr || bundle->source == nullptr || receipt == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *receipt = bundle->receipt;
    return LAPLACE_UNICODE_OK;
}

extern "C" void laplace_unicode_source_bundle_close(
    laplace_unicode_source_bundle** const bundle) {
    if (bundle != nullptr && *bundle != nullptr) {
        laplace_source_bundle_close(&(*bundle)->source);
        delete *bundle;
        *bundle = nullptr;
    }
}

extern "C" laplace_unicode_status laplace_unicode_source_verify(
    const char* const source_root,
    laplace_unicode_source_receipt* const receipt) {
    laplace_unicode_source_bundle* bundle = nullptr;
    const laplace_unicode_status status = laplace_unicode_source_bundle_open(
        source_root, &bundle, receipt);
    laplace_unicode_source_bundle_close(&bundle);
    return status;
}
