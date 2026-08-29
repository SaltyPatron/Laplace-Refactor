#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blake3.h"
#include "laplace/decomposition.h"
#include "laplace/decomposition_delimited.h"
#include "laplace/decomposition_tree_sitter.h"
#include "laplace/decomposition_uax29.h"
#include "laplace/tree_sitter_grammar.h"
#include "laplace/uax29.h"
#include "laplace/unicode_root.h"

namespace {

constexpr std::string_view Uax29ProviderDomain{"laplace-uax29-r47-provider-v1"};

struct GrammarHandleDeleter {
    void operator()(laplace_tree_sitter_grammar* value) const noexcept {
        if (value == nullptr) return;
        laplace_tree_sitter_grammar* mutable_value = value;
        laplace_tree_sitter_grammar_close(&mutable_value);
    }
};

using GrammarHandle = std::unique_ptr<laplace_tree_sitter_grammar, GrammarHandleDeleter>;

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> encoded{};
    for (std::size_t index = 0u; index < encoded.size(); ++index) {
        encoded[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, encoded.data(), encoded.size());
}

laplace_digest256 Uax29Fingerprint(const laplace_unicode_source_receipt& receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashU64(hasher, static_cast<std::uint64_t>(Uax29ProviderDomain.size()));
    blake3_hasher_update(&hasher, Uax29ProviderDomain.data(), Uax29ProviderDomain.size());
    blake3_hasher_update(&hasher, receipt.receipt_id.bytes, sizeof(receipt.receipt_id.bytes));
    blake3_hasher_update(&hasher, receipt.source_fingerprint.bytes, sizeof(receipt.source_fingerprint.bytes));
    blake3_hasher_update(&hasher, receipt.verified_file_set_fingerprint.bytes,
                         sizeof(receipt.verified_file_set_fingerprint.bytes));
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool ReadFile(const char* path, std::vector<std::uint8_t>& output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        size > static_cast<std::streamoff>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    try {
        output.resize(static_cast<std::size_t>(size));
    } catch (...) {
        return false;
    }
    input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(size));
    return input.good() || input.eof();
}

int HexDigit(const char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool ParseDigest(const char* text, laplace_digest256& output) {
    if (text == nullptr || std::strlen(text) != 64u) return false;
    for (std::size_t index = 0u; index < 32u; ++index) {
        const int high = HexDigit(text[index * 2u]);
        const int low = HexDigit(text[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        output.bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool ParseU64(const char* text, std::uint64_t& output) {
    if (text == nullptr || *text == '\0') return false;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 0);
    if (end == text || *end != '\0') return false;
    output = static_cast<std::uint64_t>(parsed);
    return true;
}

bool ParseU32(const char* text, std::uint32_t& output) {
    std::uint64_t parsed = 0u;
    if (!ParseU64(text, parsed) || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    output = static_cast<std::uint32_t>(parsed);
    return true;
}

void PrintDigest(const laplace_digest256& digest) {
    static constexpr char Hex[] = "0123456789abcdef";
    for (const std::uint8_t byte : digest.bytes) {
        std::putchar(Hex[byte >> 4u]);
        std::putchar(Hex[byte & 0x0fu]);
    }
}

void PrintJsonString(const std::uint8_t* bytes, const std::size_t count) {
    std::putchar('"');
    for (std::size_t index = 0u; index < count; ++index) {
        const std::uint8_t byte = bytes[index];
        switch (byte) {
            case '"': std::fputs("\\\"", stdout); break;
            case '\\': std::fputs("\\\\", stdout); break;
            case '\b': std::fputs("\\b", stdout); break;
            case '\f': std::fputs("\\f", stdout); break;
            case '\n': std::fputs("\\n", stdout); break;
            case '\r': std::fputs("\\r", stdout); break;
            case '\t': std::fputs("\\t", stdout); break;
            default:
                if (byte < 0x20u) {
                    std::printf("\\u%04x", static_cast<unsigned int>(byte));
                } else {
                    std::putchar(static_cast<int>(byte));
                }
                break;
        }
    }
    std::putchar('"');
}

void Usage(const char* executable) {
    std::fprintf(
        stderr,
        "usage: %s <verified-unicode-source-root> <media-type> <input-file> "
        "[--grammar <shared-object> <tree_sitter_symbol> <kind-base> <provider-fingerprint>] "
        "[--delimited <delimiter-byte> <lf|crlf> <columns> <header-rows> <kind-base> <provider-fingerprint>]...\n",
        executable);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        Usage(argc > 0 ? argv[0] : "laplace_decompose");
        return 2;
    }

    const std::string_view media_type(argv[2]);
    const std::string_view file_name(argv[3]);
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(argv[3], bytes)) {
        std::fprintf(stderr, "cannot read non-empty input file: %s\n", argv[3]);
        return 1;
    }

    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt unicode_receipt{};
    const laplace_unicode_status source_status =
        laplace_unicode_source_bundle_open(argv[1], &bundle, &unicode_receipt);
    if (source_status != LAPLACE_UNICODE_OK) {
        std::fprintf(stderr, "verified Unicode source open failed: %u\n",
                     static_cast<unsigned int>(source_status));
        return 1;
    }

    laplace_uax29_tables* uax29 = nullptr;
    const laplace_uax29_status table_status = laplace_uax29_tables_create(bundle, &uax29);
    if (table_status != LAPLACE_UAX29_OK) {
        std::fprintf(stderr, "UAX29 table build failed: %u\n",
                     static_cast<unsigned int>(table_status));
        laplace_unicode_source_bundle_close(&bundle);
        return 1;
    }

    laplace_decomposition_uax29_provider uax_provider{};
    const laplace_digest256 uax_fingerprint = Uax29Fingerprint(unicode_receipt);
    if (laplace_decomposition_uax29_provider_init(
            &uax_provider, uax29, &uax_fingerprint) != LAPLACE_DECOMPOSITION_OK) {
        std::fputs("cannot initialize UAX29 decomposition provider\n", stderr);
        laplace_uax29_tables_destroy(&uax29);
        laplace_unicode_source_bundle_close(&bundle);
        return 1;
    }

    std::vector<GrammarHandle> grammars;
    std::vector<std::unique_ptr<laplace_decomposition_delimited_provider>> delimited;
    std::vector<laplace_decomposition_provider_v1> providers;
    providers.push_back(uax_provider.provider);

    for (int index = 4; index < argc;) {
        const std::string_view option(argv[index]);
        if (option == "--grammar") {
            if (index + 4 >= argc) {
                Usage(argv[0]);
                return 2;
            }
            std::uint64_t kind_base = 0u;
            laplace_digest256 fingerprint{};
            if (!ParseU64(argv[index + 3], kind_base) ||
                !ParseDigest(argv[index + 4], fingerprint)) {
                std::fputs("invalid grammar kind-base or provider fingerprint\n", stderr);
                return 2;
            }
            laplace_tree_sitter_grammar* raw = nullptr;
            const laplace_tree_sitter_grammar_status grammar_status =
                laplace_tree_sitter_grammar_open(
                    argv[index + 1],
                    argv[index + 2],
                    media_type.data(),
                    static_cast<std::uint64_t>(media_type.size()),
                    kind_base,
                    &fingerprint,
                    &raw);
            if (grammar_status != LAPLACE_TREE_SITTER_GRAMMAR_OK) {
                std::fprintf(stderr, "grammar provider load failed for %s: %u\n",
                             argv[index + 1], static_cast<unsigned int>(grammar_status));
                return 1;
            }
            GrammarHandle handle(raw);
            const laplace_decomposition_provider_v1* provider =
                laplace_tree_sitter_grammar_provider(handle.get());
            if (provider == nullptr) {
                std::fputs("grammar provider unexpectedly absent\n", stderr);
                return 1;
            }
            providers.push_back(*provider);
            grammars.push_back(std::move(handle));
            index += 5;
            continue;
        }
        if (option == "--delimited") {
            if (index + 6 >= argc) {
                Usage(argv[0]);
                return 2;
            }
            std::uint32_t delimiter = 0u;
            std::uint32_t columns = 0u;
            std::uint32_t headers = 0u;
            std::uint64_t kind_base = 0u;
            laplace_digest256 fingerprint{};
            const std::string_view terminator_name(argv[index + 2]);
            const std::uint32_t terminator = terminator_name == "lf"
                ? static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_DELIMITED_LF)
                : terminator_name == "crlf"
                    ? static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_DELIMITED_CRLF)
                    : 0u;
            if (!ParseU32(argv[index + 1], delimiter) || terminator == 0u ||
                !ParseU32(argv[index + 3], columns) ||
                !ParseU32(argv[index + 4], headers) ||
                !ParseU64(argv[index + 5], kind_base) ||
                !ParseDigest(argv[index + 6], fingerprint)) {
                std::fputs("invalid delimited provider declaration\n", stderr);
                return 2;
            }
            auto storage = std::make_unique<laplace_decomposition_delimited_provider>();
            if (laplace_decomposition_delimited_provider_init(
                    storage.get(), delimiter, terminator, columns, headers,
                    kind_base, &fingerprint) != LAPLACE_DECOMPOSITION_OK) {
                std::fputs("cannot initialize delimited provider\n", stderr);
                return 2;
            }
            providers.push_back(storage->provider);
            delimited.push_back(std::move(storage));
            index += 7;
            continue;
        }
        Usage(argv[0]);
        return 2;
    }

    laplace_decomposition_input input{};
    input.content.bytes = bytes.data();
    input.content.byte_count = static_cast<std::uint64_t>(bytes.size());
    input.content.media_type = media_type.data();
    input.content.media_type_byte_count = static_cast<std::uint64_t>(media_type.size());
    input.content.name = file_name.data();
    input.content.name_byte_count = static_cast<std::uint64_t>(file_name.size());
    input.providers = providers.data();
    input.provider_count = static_cast<std::uint64_t>(providers.size());
    if (bytes.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint64_t>::max() / UINT64_C(16))) {
        std::fputs("input too large for decomposition span budget\n", stderr);
        return 1;
    }
    input.maximum_spans = std::max<std::uint64_t>(
        UINT64_C(4096), static_cast<std::uint64_t>(bytes.size()) * UINT64_C(16));
    input.maximum_depth = 32u;

    laplace_decomposition_result* result = nullptr;
    const laplace_decomposition_status status = laplace_decomposition_run(&input, &result);
    if (status != LAPLACE_DECOMPOSITION_OK) {
        std::fprintf(stderr, "recursive decomposition failed: %u\n",
                     static_cast<unsigned int>(status));
        laplace_uax29_tables_destroy(&uax29);
        laplace_unicode_source_bundle_close(&bundle);
        return 1;
    }

    laplace_decomposition_summary summary{};
    (void)laplace_decomposition_summary_get(result, &summary);
    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(result, &span_count);
    for (std::size_t index = 0u; index < span_count; ++index) {
        const laplace_decomposition_span& span = spans[index];
        std::printf(
            "{\"index\":%llu,\"parent\":%llu,\"depth\":%u,\"kind\":%llu,"
            "\"flags\":%u,\"byte_start\":%llu,\"byte_end\":%llu,\"provider\":\"",
            static_cast<unsigned long long>(index),
            static_cast<unsigned long long>(span.parent_span_index),
            static_cast<unsigned int>(span.depth),
            static_cast<unsigned long long>(span.kind),
            static_cast<unsigned int>(span.flags),
            static_cast<unsigned long long>(span.byte_start),
            static_cast<unsigned long long>(span.byte_end));
        PrintDigest(span.provider_fingerprint);
        std::fputs("\",\"text\":", stdout);
        const std::size_t start = static_cast<std::size_t>(span.byte_start);
        const std::size_t end = static_cast<std::size_t>(span.byte_end);
        PrintJsonString(bytes.data() + start, end - start);
        std::fputs("}\n", stdout);
    }
    std::fprintf(
        stderr,
        "spans=%llu provider_executions=%llu applicable=%llu redispatch=%llu max_depth=%u\n",
        static_cast<unsigned long long>(summary.span_count),
        static_cast<unsigned long long>(summary.provider_execution_count),
        static_cast<unsigned long long>(summary.applicable_execution_count),
        static_cast<unsigned long long>(summary.redispatch_count),
        static_cast<unsigned int>(summary.maximum_depth_reached));

    laplace_decomposition_result_destroy(&result);
    laplace_uax29_tables_destroy(&uax29);
    laplace_unicode_source_bundle_close(&bundle);
    return 0;
}
