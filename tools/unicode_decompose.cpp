#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "laplace/uax29.h"
#include "laplace/unicode_root.h"

namespace {

struct EmitContext {
    const std::uint8_t* bytes{};
    std::size_t byte_count{};
    const char* kind{};
};

void PrintJsonString(const std::uint8_t* bytes, std::size_t count) {
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
                if (byte < 0x20u) std::printf("\\u%04x", static_cast<unsigned int>(byte));
                else std::putchar(static_cast<int>(byte));
                break;
        }
    }
    std::putchar('"');
}

int Emit(void* opaque, const laplace_uax29_span* span) {
    if (opaque == nullptr || span == nullptr) return 1;
    auto* context = static_cast<EmitContext*>(opaque);
    if (span->byte_start > span->byte_end ||
        span->byte_end > static_cast<std::uint64_t>(context->byte_count)) return 1;
    const std::size_t start = static_cast<std::size_t>(span->byte_start);
    const std::size_t end = static_cast<std::size_t>(span->byte_end);
    std::printf(
        "{\"kind\":\"%s\",\"byte_start\":%llu,\"byte_end\":%llu,"
        "\"codepoint_start\":%llu,\"codepoint_end\":%llu,\"text\":",
        context->kind,
        static_cast<unsigned long long>(span->byte_start),
        static_cast<unsigned long long>(span->byte_end),
        static_cast<unsigned long long>(span->codepoint_start),
        static_cast<unsigned long long>(span->codepoint_end));
    PrintJsonString(context->bytes + start, end - start);
    std::fputs("}\n", stdout);
    return 0;
}

int Run(
    const laplace_uax29_tables* tables,
    const std::uint8_t* bytes,
    std::size_t byte_count,
    laplace_uax29_boundary_kind kind,
    const char* name) {
    EmitContext context{bytes, byte_count, name};
    laplace_uax29_summary summary{};
    const laplace_uax29_status status = laplace_uax29_segment(
        tables, bytes, byte_count, kind, Emit, &context, &summary);
    if (status != LAPLACE_UAX29_OK) {
        std::fprintf(stderr, "laplace_uax29_segment(%s) failed: %u\n",
                     name, static_cast<unsigned int>(status));
        return 1;
    }
    std::fprintf(
        stderr,
        "%s: bytes=%llu codepoints=%llu graphemes=%llu words=%llu sentences=%llu\n",
        name,
        static_cast<unsigned long long>(summary.input_bytes),
        static_cast<unsigned long long>(summary.codepoint_count),
        static_cast<unsigned long long>(summary.grapheme_count),
        static_cast<unsigned long long>(summary.word_count),
        static_cast<unsigned long long>(summary.sentence_count));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(
            stderr,
            "usage: %s <verified-unicode-source-root> <grapheme|word|sentence|all> <utf8-text>\n",
            argc > 0 ? argv[0] : "laplace_unicode_decompose");
        return 2;
    }

    const char* source_root = argv[1];
    const std::string_view mode(argv[2]);
    const auto* text = reinterpret_cast<const std::uint8_t*>(argv[3]);
    const std::size_t text_bytes = std::strlen(argv[3]);

    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt receipt{};
    const laplace_unicode_status source_status =
        laplace_unicode_source_bundle_open(source_root, &bundle, &receipt);
    if (source_status != LAPLACE_UNICODE_OK) {
        std::fprintf(stderr, "verified Unicode source open failed: %u\n",
                     static_cast<unsigned int>(source_status));
        return 1;
    }

    laplace_uax29_tables* tables = nullptr;
    const laplace_uax29_status table_status =
        laplace_uax29_tables_create(bundle, &tables);
    if (table_status != LAPLACE_UAX29_OK) {
        std::fprintf(stderr, "UAX29 table build failed: %u\n",
                     static_cast<unsigned int>(table_status));
        laplace_unicode_source_bundle_close(&bundle);
        return 1;
    }

    int result = 0;
    if (mode == "grapheme") {
        result = Run(tables, text, text_bytes, LAPLACE_UAX29_GRAPHEME, "grapheme");
    } else if (mode == "word") {
        result = Run(tables, text, text_bytes, LAPLACE_UAX29_WORD, "word");
    } else if (mode == "sentence") {
        result = Run(tables, text, text_bytes, LAPLACE_UAX29_SENTENCE, "sentence");
    } else if (mode == "all") {
        result |= Run(tables, text, text_bytes, LAPLACE_UAX29_GRAPHEME, "grapheme");
        result |= Run(tables, text, text_bytes, LAPLACE_UAX29_WORD, "word");
        result |= Run(tables, text, text_bytes, LAPLACE_UAX29_SENTENCE, "sentence");
    } else {
        std::fprintf(stderr, "unknown decomposition mode: %s\n", argv[2]);
        result = 2;
    }

    laplace_uax29_tables_destroy(&tables);
    laplace_unicode_source_bundle_close(&bundle);
    return result;
}
