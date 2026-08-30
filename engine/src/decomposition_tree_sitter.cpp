#include "laplace/decomposition_tree_sitter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

#include <tree_sitter/api.h>

namespace {

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

laplace_decomposition_status Applicable(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (provider_state == nullptr || content == nullptr || span == nullptr ||
        applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const auto& storage =
        *static_cast<const laplace_decomposition_tree_sitter_provider*>(provider_state);
    const bool grammar_input =
        (span->flags & static_cast<std::uint32_t>(
            LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT)) != 0u;
    const bool media_match = content->media_type != nullptr &&
        content->media_type_byte_count == storage.media_type_byte_count &&
        std::memcmp(
            content->media_type,
            storage.media_type,
            static_cast<std::size_t>(storage.media_type_byte_count)) == 0;
    *applicable = grammar_input && media_match ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status Apply(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || content == nullptr || span == nullptr ||
        emit == nullptr || span->byte_start >= span->byte_end ||
        span->byte_end > content->byte_count) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const std::uint64_t span_bytes = span->byte_end - span->byte_start;
    if (span_bytes > std::numeric_limits<std::uint32_t>::max()) {
        return LAPLACE_DECOMPOSITION_LIMIT_EXCEEDED;
    }
    const auto& storage =
        *static_cast<const laplace_decomposition_tree_sitter_provider*>(provider_state);
    TSParser* parser = ts_parser_new();
    if (parser == nullptr) return LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
    if (!ts_parser_set_language(parser, storage.language)) {
        ts_parser_delete(parser);
        return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
    }
    const char* source = reinterpret_cast<const char*>(
        content->bytes + static_cast<std::size_t>(span->byte_start));
    TSTree* tree = ts_parser_parse_string_encoding(
        parser,
        nullptr,
        source,
        static_cast<std::uint32_t>(span_bytes),
        TSInputEncodingUTF8);
    if (tree == nullptr) {
        ts_parser_delete(parser);
        return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
    }

    laplace_decomposition_status status = LAPLACE_DECOMPOSITION_OK;
    try {
        std::vector<TSNode> stack;
        const TSNode root = ts_tree_root_node(tree);
        const std::uint32_t root_children = ts_node_child_count(root);
        stack.reserve(static_cast<std::size_t>(root_children));
        for (std::uint32_t index = root_children; index > 0u; --index) {
            stack.push_back(ts_node_child(root, index - 1u));
        }
        while (!stack.empty()) {
            const TSNode node = stack.back();
            stack.pop_back();
            const std::uint32_t start = ts_node_start_byte(node);
            const std::uint32_t end = ts_node_end_byte(node);
            if (!ts_node_is_null(node) && !ts_node_is_missing(node) && start < end) {
                const std::uint64_t absolute_start = span->byte_start + start;
                const std::uint64_t absolute_end = span->byte_start + end;
                const std::uint64_t kind = storage.kind_base |
                    static_cast<std::uint64_t>(ts_node_symbol(node));
                if (emit(
                        emit_state,
                        absolute_start,
                        absolute_end,
                        kind,
                        static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT) |
                            static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT)) != 0) {
                    status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                    break;
                }
            }
            const std::uint32_t child_count = ts_node_child_count(node);
            for (std::uint32_t index = child_count; index > 0u; --index) {
                stack.push_back(ts_node_child(node, index - 1u));
            }
        }
    } catch (...) {
        status = LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return status;
}

}  // namespace

extern "C" laplace_decomposition_status laplace_decomposition_tree_sitter_provider_init(
    laplace_decomposition_tree_sitter_provider* storage,
    const TSLanguage* language,
    const char* media_type,
    const std::uint64_t media_type_byte_count,
    const std::uint64_t kind_base,
    const laplace_digest256* provider_fingerprint) {
    if (storage == nullptr || language == nullptr || media_type == nullptr ||
        media_type_byte_count == 0u ||
        media_type_byte_count >= LAPLACE_TREE_SITTER_MEDIA_TYPE_CAPACITY ||
        provider_fingerprint == nullptr || DigestZero(*provider_fingerprint) ||
        (kind_base & UINT64_C(0xFFFF)) != 0u || kind_base == 0u) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    std::memset(storage, 0, sizeof(*storage));
    storage->language = language;
    storage->kind_base = kind_base;
    storage->media_type_byte_count = static_cast<std::uint32_t>(media_type_byte_count);
    std::memcpy(storage->media_type, media_type,
                static_cast<std::size_t>(media_type_byte_count));
    storage->provider.state = storage;
    storage->provider.provider_fingerprint = *provider_fingerprint;
    storage->provider.applicable = Applicable;
    storage->provider.apply = Apply;
    storage->provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    storage->provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return LAPLACE_DECOMPOSITION_OK;
}
