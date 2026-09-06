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

std::uint32_t SyntaxFlags(const TSNode node) {
    std::uint32_t flags = 0u;
    if (ts_node_is_named(node)) {
        flags |= static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED);
    }
    if (ts_node_is_missing(node)) {
        flags |= static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_MISSING);
    }
    if (ts_node_is_error(node)) {
        flags |= static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_ERROR);
    }
    if (ts_node_is_extra(node)) {
        flags |= static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_EXTRA);
    }
    if (ts_node_has_error(node)) {
        flags |= static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_HAS_ERROR);
    }
    return flags;
}

struct PendingNode final {
    TSNode node{};
    std::uint64_t parent_event_index{UINT64_MAX};
    std::uint64_t field_kind{};
    std::uint64_t sibling_ordinal{};
};

std::uint64_t FieldKind(
    const TSLanguage* const language,
    const TSNode parent,
    const std::uint32_t child_index) {
    const char* const field_name = ts_node_field_name_for_child(parent, child_index);
    if (field_name == nullptr) return 0u;
    const std::size_t length = std::strlen(field_name);
    if (length > std::numeric_limits<std::uint32_t>::max()) return UINT64_MAX;
    return static_cast<std::uint64_t>(ts_language_field_id_for_name(
        language, field_name, static_cast<std::uint32_t>(length)));
}

laplace_decomposition_status ApplyEvents(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_event_fn emit,
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
        std::vector<PendingNode> stack;
        stack.push_back(PendingNode{ts_tree_root_node(tree), UINT64_MAX, 0u, 0u});
        std::uint64_t emitted_event_count = 0u;

        while (!stack.empty()) {
            const PendingNode pending = stack.back();
            stack.pop_back();
            const TSNode node = pending.node;
            if (ts_node_is_null(node)) {
                status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                break;
            }

            const std::uint32_t start = ts_node_start_byte(node);
            const std::uint32_t end = ts_node_end_byte(node);
            const std::uint32_t syntax_flags = SyntaxFlags(node);
            const bool missing =
                (syntax_flags & static_cast<std::uint32_t>(
                    LAPLACE_DECOMPOSITION_SYNTAX_MISSING)) != 0u;
            if ((!missing && start >= end) ||
                static_cast<std::uint64_t>(end) > span_bytes) {
                status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                break;
            }

            laplace_decomposition_event event{};
            event.byte_start = span->byte_start + start;
            event.byte_end = span->byte_start + end;
            event.parent_event_index = pending.parent_event_index;
            event.kind = storage.kind_base |
                static_cast<std::uint64_t>(ts_node_symbol(node));
            event.grammar_kind = storage.kind_base |
                static_cast<std::uint64_t>(ts_node_grammar_symbol(node));
            event.field_kind = pending.field_kind;
            event.sibling_ordinal = pending.sibling_ordinal;
            event.syntax_flags = syntax_flags;
            if (!missing) {
                event.flags =
                    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT) |
                    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT);
            }
            const std::uint64_t local_event_index = emitted_event_count;
            if (emitted_event_count == UINT64_MAX || emit(emit_state, &event) != 0) {
                status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                break;
            }
            ++emitted_event_count;

            const std::uint32_t child_count = ts_node_child_count(node);
            for (std::uint32_t index = child_count; index > 0u; --index) {
                const std::uint32_t child_index = index - 1u;
                const TSNode child = ts_node_child(node, child_index);
                const std::uint64_t field_kind =
                    FieldKind(storage.language, node, child_index);
                if (field_kind == UINT64_MAX) {
                    status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
                    break;
                }
                stack.push_back(PendingNode{
                    child,
                    local_event_index,
                    field_kind,
                    static_cast<std::uint64_t>(child_index) + 1u});
            }
            if (status != LAPLACE_DECOMPOSITION_OK) break;
        }
    } catch (const std::bad_alloc&) {
        status = LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
    } catch (...) {
        status = LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
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
    storage->provider.apply = nullptr;
    storage->provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    storage->provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    storage->provider.apply_events = ApplyEvents;
    return LAPLACE_DECOMPOSITION_OK;
}