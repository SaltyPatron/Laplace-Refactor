#include "laplace/decomposition_xml.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

namespace {

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool XmlMediaType(const laplace_decomposition_content& content) {
    if (content.media_type == nullptr || content.media_type_byte_count == 0u ||
        content.media_type_byte_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    std::string_view value(
        content.media_type,
        static_cast<std::size_t>(content.media_type_byte_count));
    const std::size_t semicolon = value.find(';');
    if (semicolon != std::string_view::npos) value = value.substr(0u, semicolon);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1u);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1u);
    }
    if (value == "application/xml" || value == "text/xml") return true;
    if (value.size() <= 4u || value.substr(value.size() - 4u) != "+xml") return false;
    return value.find('/') != std::string_view::npos;
}

laplace_decomposition_status Applicable(
    void*,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (content == nullptr || span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const bool grammar_input =
        (span->flags & static_cast<std::uint32_t>(
            LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT)) != 0u;
    *applicable = grammar_input && XmlMediaType(*content) ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

bool IsSpace(const std::uint8_t byte) {
    return byte == static_cast<std::uint8_t>(' ') ||
        byte == static_cast<std::uint8_t>('\t') ||
        byte == static_cast<std::uint8_t>('\r') ||
        byte == static_cast<std::uint8_t>('\n');
}

bool IsNameTerminator(const std::uint8_t byte) {
    return IsSpace(byte) || byte == static_cast<std::uint8_t>('/') ||
        byte == static_cast<std::uint8_t>('>') ||
        byte == static_cast<std::uint8_t>('=') ||
        byte == static_cast<std::uint8_t>('<') ||
        byte == static_cast<std::uint8_t>('?') ||
        byte == static_cast<std::uint8_t>('!') ||
        byte == static_cast<std::uint8_t>('[') ||
        byte == static_cast<std::uint8_t>(']') ||
        byte == static_cast<std::uint8_t>('"') ||
        byte == static_cast<std::uint8_t>('\'');
}

struct Node final {
    std::uint64_t byte_start{};
    std::uint64_t byte_end{};
    std::uint64_t parent_event_index{UINT64_MAX};
    std::uint64_t kind{};
    std::uint64_t field_kind{};
    std::uint64_t sibling_ordinal{};
    std::uint64_t child_count{};
    std::uint32_t flags{};
    std::uint32_t syntax_flags{};
};

struct ElementFrame final {
    std::uint64_t node_index{};
    std::uint64_t name_start{};
    std::uint64_t name_end{};
};

class Parser final {
public:
    Parser(
        const laplace_decomposition_xml_provider& provider,
        const laplace_decomposition_content& content,
        const laplace_decomposition_span& span)
        : provider_(provider), content_(content), begin_(span.byte_start), end_(span.byte_end) {}

    bool Parse() {
        std::uint64_t document = 0u;
        if (!AddNode(
                UINT64_MAX, begin_, end_, provider_.document_kind, 0u,
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT),
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                &document) || document != 0u) {
            return false;
        }
        document_index_ = document;
        cursor_ = begin_;
        if (Has(cursor_, 3u) && Byte(cursor_) == 0xefu &&
            Byte(cursor_ + 1u) == 0xbbu && Byte(cursor_ + 2u) == 0xbfu) {
            if (!AddNode(
                    document_index_, cursor_, cursor_ + 3u, provider_.bom_kind, 0u,
                    0u,
                    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_EXTRA),
                    nullptr)) {
                return false;
            }
            cursor_ += 3u;
        }

        while (cursor_ < end_) {
            if (Byte(cursor_) != static_cast<std::uint8_t>('<')) {
                if (!ParseText()) return false;
                continue;
            }
            if (Starts(cursor_, "<!--")) {
                if (!ParseDelimitedMarkup("-->", provider_.comment_kind, 4u, 0u)) {
                    return false;
                }
            } else if (Starts(cursor_, "<![CDATA[")) {
                if (!ParseCdata()) return false;
            } else if (Starts(cursor_, "<?")) {
                if (!ParseDelimitedMarkup(
                        "?>", provider_.processing_instruction_kind, 2u, 0u)) {
                    return false;
                }
            } else if (Starts(cursor_, "<!DOCTYPE")) {
                if (!ParseDoctype()) return false;
            } else if (Starts(cursor_, "</")) {
                if (!ParseEndTag()) return false;
            } else if (Starts(cursor_, "<!")) {
                return false;
            } else {
                if (!ParseStartTag()) return false;
            }
        }
        return frames_.empty() && root_element_count_ == 1u;
    }

    const std::vector<Node>& Nodes() const { return nodes_; }

private:
    bool Has(const std::uint64_t offset, const std::uint64_t count) const {
        return offset <= end_ && count <= end_ - offset;
    }

    std::uint8_t Byte(const std::uint64_t offset) const {
        return content_.bytes[static_cast<std::size_t>(offset)];
    }

    bool Starts(const std::uint64_t offset, const std::string_view literal) const {
        if (literal.size() > static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max()) ||
            !Has(offset, static_cast<std::uint64_t>(literal.size()))) {
            return false;
        }
        return std::memcmp(
                   content_.bytes + static_cast<std::size_t>(offset),
                   literal.data(), literal.size()) == 0;
    }

    void SkipSpace(std::uint64_t* const offset) const {
        while (*offset < end_ && IsSpace(Byte(*offset))) ++*offset;
    }

    bool ParseName(
        std::uint64_t* const offset,
        std::uint64_t* const name_start,
        std::uint64_t* const name_end) const {
        if (*offset >= end_ || IsNameTerminator(Byte(*offset))) return false;
        *name_start = *offset;
        while (*offset < end_ && !IsNameTerminator(Byte(*offset))) ++*offset;
        *name_end = *offset;
        return *name_end > *name_start;
    }

    bool SameName(
        const std::uint64_t left_start,
        const std::uint64_t left_end,
        const std::uint64_t right_start,
        const std::uint64_t right_end) const {
        if (left_end < left_start || right_end < right_start ||
            left_end - left_start != right_end - right_start) {
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(left_end - left_start);
        return std::memcmp(
                   content_.bytes + static_cast<std::size_t>(left_start),
                   content_.bytes + static_cast<std::size_t>(right_start),
                   count) == 0;
    }

    bool AllSpace(const std::uint64_t first, const std::uint64_t last) const {
        for (std::uint64_t offset = first; offset < last; ++offset) {
            if (!IsSpace(Byte(offset))) return false;
        }
        return true;
    }

    std::uint64_t CurrentParent() const {
        return frames_.empty() ? document_index_ : frames_.back().node_index;
    }

    bool AddNode(
        const std::uint64_t parent,
        const std::uint64_t first,
        const std::uint64_t last,
        const std::uint64_t kind,
        const std::uint64_t field_kind,
        const std::uint32_t flags,
        const std::uint32_t syntax_flags,
        std::uint64_t* const index) {
        if (first >= last || first < begin_ || last > end_ || kind == 0u ||
            nodes_.size() >= static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }
        std::uint64_t sibling = 0u;
        if (parent != UINT64_MAX) {
            if (parent >= static_cast<std::uint64_t>(nodes_.size())) return false;
            Node& parent_node = nodes_[static_cast<std::size_t>(parent)];
            if (parent_node.child_count == UINT64_MAX) return false;
            ++parent_node.child_count;
            sibling = parent_node.child_count;
        }
        Node node{};
        node.byte_start = first;
        node.byte_end = last;
        node.parent_event_index = parent;
        node.kind = kind;
        node.field_kind = field_kind;
        node.sibling_ordinal = sibling;
        node.flags = flags;
        node.syntax_flags = syntax_flags;
        const std::uint64_t created = static_cast<std::uint64_t>(nodes_.size());
        nodes_.push_back(node);
        if (index != nullptr) *index = created;
        return true;
    }

    bool ParseText() {
        const std::uint64_t first = cursor_;
        while (cursor_ < end_ && Byte(cursor_) != static_cast<std::uint8_t>('<')) {
            ++cursor_;
        }
        if (cursor_ == first) return false;
        const bool outside_root = frames_.empty();
        if (outside_root && !AllSpace(first, cursor_)) return false;
        const std::uint32_t flags = outside_root
            ? static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT)
            : static_cast<std::uint32_t>(
                  LAPLACE_DECOMPOSITION_SPAN_TEXT |
                  LAPLACE_DECOMPOSITION_SPAN_REDISPATCH);
        return AddNode(
            CurrentParent(), first, cursor_, provider_.text_kind,
            LAPLACE_DECOMPOSITION_XML_FIELD_CONTENT, flags,
            static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
            nullptr);
    }

    bool ParseDelimitedMarkup(
        const std::string_view terminator,
        const std::uint64_t kind,
        const std::uint64_t prefix_bytes,
        const std::uint64_t field_kind) {
        const std::uint64_t first = cursor_;
        std::uint64_t search = cursor_ + prefix_bytes;
        while (search < end_) {
            if (Starts(search, terminator)) {
                const std::uint64_t last = search +
                    static_cast<std::uint64_t>(terminator.size());
                if (!AddNode(
                        CurrentParent(), first, last, kind, field_kind, 0u,
                        static_cast<std::uint32_t>(
                            LAPLACE_DECOMPOSITION_SYNTAX_NAMED |
                            LAPLACE_DECOMPOSITION_SYNTAX_EXTRA),
                        nullptr)) {
                    return false;
                }
                cursor_ = last;
                return true;
            }
            ++search;
        }
        return false;
    }

    bool ParseCdata() {
        if (frames_.empty()) return false;
        const std::uint64_t first = cursor_;
        std::uint64_t search = cursor_ + UINT64_C(9);
        while (search < end_) {
            if (Starts(search, "]]>") ) {
                const std::uint64_t last = search + 3u;
                std::uint64_t cdata = 0u;
                if (!AddNode(
                        CurrentParent(), first, last, provider_.cdata_kind,
                        LAPLACE_DECOMPOSITION_XML_FIELD_CONTENT, 0u,
                        static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                        &cdata)) {
                    return false;
                }
                const std::uint64_t text_first = first + 9u;
                if (text_first < search &&
                    !AddNode(
                        cdata, text_first, search, provider_.cdata_text_kind,
                        LAPLACE_DECOMPOSITION_XML_FIELD_CONTENT,
                        static_cast<std::uint32_t>(
                            LAPLACE_DECOMPOSITION_SPAN_TEXT |
                            LAPLACE_DECOMPOSITION_SPAN_REDISPATCH),
                        static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                        nullptr)) {
                    return false;
                }
                cursor_ = last;
                return true;
            }
            ++search;
        }
        return false;
    }

    bool ParseDoctype() {
        if (!frames_.empty() || root_element_count_ != 0u) return false;
        const std::uint64_t first = cursor_;
        std::uint64_t offset = cursor_ + UINT64_C(9);
        std::uint64_t bracket_depth = 0u;
        std::uint8_t quote = 0u;
        for (; offset < end_; ++offset) {
            const std::uint8_t byte = Byte(offset);
            if (quote != 0u) {
                if (byte == quote) quote = 0u;
                continue;
            }
            if (byte == static_cast<std::uint8_t>('"') ||
                byte == static_cast<std::uint8_t>('\'')) {
                quote = byte;
            } else if (byte == static_cast<std::uint8_t>('[')) {
                if (bracket_depth == UINT64_MAX) return false;
                ++bracket_depth;
            } else if (byte == static_cast<std::uint8_t>(']')) {
                if (bracket_depth == 0u) return false;
                --bracket_depth;
            } else if (byte == static_cast<std::uint8_t>('>') &&
                       bracket_depth == 0u) {
                const std::uint64_t last = offset + 1u;
                if (!AddNode(
                        document_index_, first, last, provider_.doctype_kind, 0u, 0u,
                        static_cast<std::uint32_t>(
                            LAPLACE_DECOMPOSITION_SYNTAX_NAMED |
                            LAPLACE_DECOMPOSITION_SYNTAX_EXTRA),
                        nullptr)) {
                    return false;
                }
                cursor_ = last;
                return true;
            }
        }
        return false;
    }

    bool ParseStartTag() {
        const std::uint64_t first = cursor_;
        std::uint64_t offset = cursor_ + 1u;
        std::uint64_t name_start = 0u;
        std::uint64_t name_end = 0u;
        if (!ParseName(&offset, &name_start, &name_end)) return false;

        const std::uint64_t parent = CurrentParent();
        if (frames_.empty()) {
            if (root_element_count_ == UINT64_MAX) return false;
            ++root_element_count_;
            if (root_element_count_ != 1u) return false;
        }

        std::uint64_t element = 0u;
        if (!AddNode(
                parent, first, first + 1u, provider_.element_kind, 0u, 0u,
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                &element)) {
            return false;
        }
        std::uint64_t start_tag = 0u;
        if (!AddNode(
                element, first, first + 1u, provider_.start_tag_kind, 0u, 0u,
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                &start_tag) ||
            !AddNode(
                start_tag, name_start, name_end, provider_.element_name_kind,
                LAPLACE_DECOMPOSITION_XML_FIELD_ELEMENT_NAME,
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT),
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                nullptr)) {
            return false;
        }

        bool self_closing = false;
        for (;;) {
            SkipSpace(&offset);
            if (offset >= end_) return false;
            if (Byte(offset) == static_cast<std::uint8_t>('>')) {
                ++offset;
                break;
            }
            if (Byte(offset) == static_cast<std::uint8_t>('/')) {
                if (!Has(offset, 2u) ||
                    Byte(offset + 1u) != static_cast<std::uint8_t>('>')) {
                    return false;
                }
                offset += 2u;
                self_closing = true;
                break;
            }

            const std::uint64_t attribute_start = offset;
            std::uint64_t attribute_name_start = 0u;
            std::uint64_t attribute_name_end = 0u;
            if (!ParseName(
                    &offset, &attribute_name_start, &attribute_name_end)) {
                return false;
            }
            SkipSpace(&offset);
            if (offset >= end_ ||
                Byte(offset) != static_cast<std::uint8_t>('=')) {
                return false;
            }
            ++offset;
            SkipSpace(&offset);
            if (offset >= end_) return false;
            const std::uint8_t quote = Byte(offset);
            if (quote != static_cast<std::uint8_t>('"') &&
                quote != static_cast<std::uint8_t>('\'')) {
                return false;
            }
            ++offset;
            const std::uint64_t value_start = offset;
            while (offset < end_ && Byte(offset) != quote) {
                if (Byte(offset) == static_cast<std::uint8_t>('<')) return false;
                ++offset;
            }
            if (offset >= end_) return false;
            const std::uint64_t value_end = offset;
            ++offset;
            const std::uint64_t attribute_end = offset;

            std::uint64_t attribute = 0u;
            if (!AddNode(
                    start_tag, attribute_start, attribute_end,
                    provider_.attribute_kind,
                    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE, 0u,
                    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                    &attribute) ||
                !AddNode(
                    attribute, attribute_name_start, attribute_name_end,
                    provider_.attribute_name_kind,
                    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_NAME,
                    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT),
                    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                    nullptr)) {
                return false;
            }
            if (value_start < value_end &&
                !AddNode(
                    attribute, value_start, value_end,
                    provider_.attribute_value_kind,
                    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_VALUE,
                    static_cast<std::uint32_t>(
                        LAPLACE_DECOMPOSITION_SPAN_TEXT |
                        LAPLACE_DECOMPOSITION_SPAN_REDISPATCH),
                    static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                    nullptr)) {
                return false;
            }
        }

        nodes_[static_cast<std::size_t>(start_tag)].byte_end = offset;
        if (self_closing) {
            nodes_[static_cast<std::size_t>(element)].byte_end = offset;
        } else {
            frames_.push_back(ElementFrame{element, name_start, name_end});
        }
        cursor_ = offset;
        return true;
    }

    bool ParseEndTag() {
        if (frames_.empty()) return false;
        const std::uint64_t first = cursor_;
        std::uint64_t offset = cursor_ + 2u;
        std::uint64_t name_start = 0u;
        std::uint64_t name_end = 0u;
        if (!ParseName(&offset, &name_start, &name_end)) return false;
        SkipSpace(&offset);
        if (offset >= end_ || Byte(offset) != static_cast<std::uint8_t>('>')) {
            return false;
        }
        ++offset;

        const ElementFrame frame = frames_.back();
        if (!SameName(frame.name_start, frame.name_end, name_start, name_end)) {
            return false;
        }
        nodes_[static_cast<std::size_t>(frame.node_index)].byte_end = offset;
        std::uint64_t end_tag = 0u;
        if (!AddNode(
                frame.node_index, first, offset, provider_.end_tag_kind, 0u, 0u,
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                &end_tag) ||
            !AddNode(
                end_tag, name_start, name_end, provider_.element_name_kind,
                LAPLACE_DECOMPOSITION_XML_FIELD_END_NAME,
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SPAN_TEXT),
                static_cast<std::uint32_t>(LAPLACE_DECOMPOSITION_SYNTAX_NAMED),
                nullptr)) {
            return false;
        }
        frames_.pop_back();
        cursor_ = offset;
        return true;
    }

    const laplace_decomposition_xml_provider& provider_;
    const laplace_decomposition_content& content_;
    std::uint64_t begin_{};
    std::uint64_t end_{};
    std::uint64_t cursor_{};
    std::uint64_t document_index_{};
    std::uint64_t root_element_count_{};
    std::vector<Node> nodes_;
    std::vector<ElementFrame> frames_;
};

laplace_decomposition_status ApplyEvents(
    void* provider_state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_event_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || content == nullptr || span == nullptr ||
        emit == nullptr || content->bytes == nullptr ||
        span->byte_start >= span->byte_end || span->byte_end > content->byte_count ||
        span->byte_end > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const auto& provider =
        *static_cast<const laplace_decomposition_xml_provider*>(provider_state);
    try {
        Parser parser(provider, *content, *span);
        if (!parser.Parse()) return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        const auto& nodes = parser.Nodes();
        if (nodes.empty()) return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        for (std::size_t index = 0u; index < nodes.size(); ++index) {
            const Node& node = nodes[index];
            if (node.byte_start >= node.byte_end || node.byte_end > span->byte_end ||
                (node.parent_event_index != UINT64_MAX &&
                 node.parent_event_index >= static_cast<std::uint64_t>(index))) {
                return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            }
            laplace_decomposition_event event{};
            event.byte_start = node.byte_start;
            event.byte_end = node.byte_end;
            event.parent_event_index = node.parent_event_index;
            event.kind = node.kind;
            event.grammar_kind = node.kind;
            event.field_kind = node.field_kind;
            event.sibling_ordinal = node.sibling_ordinal;
            event.flags = node.flags;
            event.syntax_flags = node.syntax_flags;
            if (emit(emit_state, &event) != 0) {
                return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
            }
        }
        return LAPLACE_DECOMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_DECOMPOSITION_MEMORY_FAILURE;
    } catch (...) {
        return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
    }
}

}  // namespace

extern "C" laplace_decomposition_status laplace_decomposition_xml_provider_init(
    laplace_decomposition_xml_provider* storage,
    const std::uint64_t kind_base,
    const laplace_digest256* provider_fingerprint) {
    if (storage == nullptr || provider_fingerprint == nullptr ||
        DigestZero(*provider_fingerprint) || kind_base == 0u ||
        (kind_base & UINT64_C(0xFFFF)) != 0u) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    std::memset(storage, 0, sizeof(*storage));
    storage->kind_base = kind_base;
    storage->document_kind = kind_base | UINT64_C(1);
    storage->element_kind = kind_base | UINT64_C(2);
    storage->start_tag_kind = kind_base | UINT64_C(3);
    storage->end_tag_kind = kind_base | UINT64_C(4);
    storage->attribute_kind = kind_base | UINT64_C(5);
    storage->attribute_name_kind = kind_base | UINT64_C(6);
    storage->attribute_value_kind = kind_base | UINT64_C(7);
    storage->text_kind = kind_base | UINT64_C(8);
    storage->comment_kind = kind_base | UINT64_C(9);
    storage->cdata_kind = kind_base | UINT64_C(10);
    storage->processing_instruction_kind = kind_base | UINT64_C(11);
    storage->doctype_kind = kind_base | UINT64_C(12);
    storage->element_name_kind = kind_base | UINT64_C(13);
    storage->cdata_text_kind = kind_base | UINT64_C(14);
    storage->bom_kind = kind_base | UINT64_C(15);
    storage->provider.state = storage;
    storage->provider.provider_fingerprint = *provider_fingerprint;
    storage->provider.applicable = Applicable;
    storage->provider.apply = nullptr;
    storage->provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    storage->provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    storage->provider.apply_events = ApplyEvents;
    return LAPLACE_DECOMPOSITION_OK;
}
