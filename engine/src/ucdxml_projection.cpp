#include "laplace/ucdxml_projection.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view ProjectionDomain{"laplace-ucdxml-projection-v1"};
constexpr std::string_view ObservationDomain{"laplace-ucdxml-property-observation-v1"};
constexpr std::uint64_t NoIndex = UINT64_MAX;

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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

void HashDigest(blake3_hasher& hasher, const laplace_digest256& value) {
    blake3_hasher_update(&hasher, value.bytes, sizeof(value.bytes));
}

void HashBytes(
    blake3_hasher& hasher,
    const std::uint8_t* const bytes,
    const std::size_t count) {
    HashU64(hasher, static_cast<std::uint64_t>(count));
    if (count != 0u) blake3_hasher_update(&hasher, bytes, count);
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

struct AttributeRef final {
    std::uint64_t name_start{};
    std::uint64_t name_end{};
    std::uint64_t value_start{};
    std::uint64_t value_end{};
};

struct AttributeSlice final {
    std::size_t begin{};
    std::size_t count{};
};

struct GroupRecord final {
    std::uint64_t element_span_index{};
    AttributeSlice attributes{};
};

struct CodepointRecord final {
    std::uint32_t first{};
    std::uint32_t last{};
    std::uint64_t element_span_index{};
    std::uint64_t group_index{NoIndex};
    AttributeSlice attributes{};
};

bool IsCoordinateName(const std::string_view name) {
    return name == "cp" || name == "first-cp" || name == "last-cp";
}

bool IsCodepointElement(const std::string_view name) {
    return name == "char" || name == "reserved" || name == "surrogate" ||
        name == "noncharacter";
}

bool ParseHexPosition(const std::string_view value, std::uint32_t* const output) {
    if (output == nullptr || value.empty() || value.size() > 6u) return false;
    std::uint32_t parsed = 0u;
    for (const char byte : value) {
        std::uint32_t digit = 0u;
        if (byte >= '0' && byte <= '9') {
            digit = static_cast<std::uint32_t>(byte - '0');
        } else if (byte >= 'A' && byte <= 'F') {
            digit = static_cast<std::uint32_t>(byte - 'A') + 10u;
        } else if (byte >= 'a' && byte <= 'f') {
            digit = static_cast<std::uint32_t>(byte - 'a') + 10u;
        } else {
            return false;
        }
        if (parsed > (UINT32_MAX - digit) / 16u) return false;
        parsed = parsed * 16u + digit;
    }
    if (parsed >= UINT32_C(0x110000)) return false;
    *output = parsed;
    return true;
}

int NameCompare(
    const laplace_decomposition_content& content,
    const AttributeRef& left,
    const AttributeRef& right) {
    const std::size_t left_size = static_cast<std::size_t>(left.name_end - left.name_start);
    const std::size_t right_size = static_cast<std::size_t>(right.name_end - right.name_start);
    const std::size_t shared = std::min(left_size, right_size);
    const int compare = shared == 0u ? 0 : std::memcmp(
        content.bytes + static_cast<std::size_t>(left.name_start),
        content.bytes + static_cast<std::size_t>(right.name_start), shared);
    if (compare != 0) return compare;
    if (left_size < right_size) return -1;
    if (left_size > right_size) return 1;
    return 0;
}

int NameCompare(
    const laplace_decomposition_content& content,
    const AttributeRef& left,
    const std::uint8_t* const right,
    const std::size_t right_size) {
    const std::size_t left_size = static_cast<std::size_t>(left.name_end - left.name_start);
    const std::size_t shared = std::min(left_size, right_size);
    const int compare = shared == 0u ? 0 : std::memcmp(
        content.bytes + static_cast<std::size_t>(left.name_start), right, shared);
    if (compare != 0) return compare;
    if (left_size < right_size) return -1;
    if (left_size > right_size) return 1;
    return 0;
}

std::string_view AttributeName(
    const laplace_decomposition_content& content,
    const AttributeRef& attribute) {
    return std::string_view(
        reinterpret_cast<const char*>(
            content.bytes + static_cast<std::size_t>(attribute.name_start)),
        static_cast<std::size_t>(attribute.name_end - attribute.name_start));
}

std::string_view AttributeRawValue(
    const laplace_decomposition_content& content,
    const AttributeRef& attribute) {
    return std::string_view(
        reinterpret_cast<const char*>(
            content.bytes + static_cast<std::size_t>(attribute.value_start)),
        static_cast<std::size_t>(attribute.value_end - attribute.value_start));
}

std::uint32_t HexDigit(const std::uint8_t byte) {
    if (byte >= static_cast<std::uint8_t>('0') &&
        byte <= static_cast<std::uint8_t>('9')) {
        return static_cast<std::uint32_t>(byte - static_cast<std::uint8_t>('0'));
    }
    if (byte >= static_cast<std::uint8_t>('A') &&
        byte <= static_cast<std::uint8_t>('F')) {
        return static_cast<std::uint32_t>(byte - static_cast<std::uint8_t>('A')) + 10u;
    }
    if (byte >= static_cast<std::uint8_t>('a') &&
        byte <= static_cast<std::uint8_t>('f')) {
        return static_cast<std::uint32_t>(byte - static_cast<std::uint8_t>('a')) + 10u;
    }
    return UINT32_MAX;
}

bool AppendUtf8(std::vector<std::uint8_t>* const output, const std::uint32_t codepoint) {
    if (output == nullptr || codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdfff))) {
        return false;
    }
    if (codepoint <= UINT32_C(0x7f)) {
        output->push_back(static_cast<std::uint8_t>(codepoint));
    } else if (codepoint <= UINT32_C(0x7ff)) {
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0xc0) | (codepoint >> 6u)));
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0x80) | (codepoint & 0x3fu)));
    } else if (codepoint <= UINT32_C(0xffff)) {
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0xe0) | (codepoint >> 12u)));
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0x80) | ((codepoint >> 6u) & 0x3fu)));
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0x80) | (codepoint & 0x3fu)));
    } else {
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0xf0) | (codepoint >> 18u)));
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0x80) | ((codepoint >> 12u) & 0x3fu)));
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0x80) | ((codepoint >> 6u) & 0x3fu)));
        output->push_back(static_cast<std::uint8_t>(UINT32_C(0x80) | (codepoint & 0x3fu)));
    }
    return true;
}

void AppendPositionHex(
    std::vector<std::uint8_t>* const output,
    const std::uint32_t position) {
    static constexpr char Hex[] = "0123456789ABCDEF";
    std::array<char, 6> digits{};
    std::size_t count = 0u;
    std::uint32_t value = position;
    do {
        digits[count++] = Hex[value & 0x0fu];
        value >>= 4u;
    } while (value != 0u);
    while (count < 4u) digits[count++] = '0';
    while (count != 0u) {
        output->push_back(static_cast<std::uint8_t>(digits[--count]));
    }
}

bool DecodeEntity(
    const std::uint8_t* const bytes,
    const std::size_t count,
    std::vector<std::uint8_t>* const output) {
    const std::string_view entity(reinterpret_cast<const char*>(bytes), count);
    if (entity == "amp") {
        output->push_back(static_cast<std::uint8_t>('&'));
        return true;
    }
    if (entity == "lt") {
        output->push_back(static_cast<std::uint8_t>('<'));
        return true;
    }
    if (entity == "gt") {
        output->push_back(static_cast<std::uint8_t>('>'));
        return true;
    }
    if (entity == "quot") {
        output->push_back(static_cast<std::uint8_t>('"'));
        return true;
    }
    if (entity == "apos") {
        output->push_back(static_cast<std::uint8_t>('\''));
        return true;
    }
    if (count < 2u || bytes[0] != static_cast<std::uint8_t>('#')) return false;
    std::size_t offset = 1u;
    std::uint32_t radix = 10u;
    if (offset < count &&
        (bytes[offset] == static_cast<std::uint8_t>('x') ||
         bytes[offset] == static_cast<std::uint8_t>('X'))) {
        radix = 16u;
        ++offset;
    }
    if (offset == count) return false;
    std::uint32_t codepoint = 0u;
    for (; offset < count; ++offset) {
        std::uint32_t digit = UINT32_MAX;
        if (radix == 16u) {
            digit = HexDigit(bytes[offset]);
        } else if (bytes[offset] >= static_cast<std::uint8_t>('0') &&
                   bytes[offset] <= static_cast<std::uint8_t>('9')) {
            digit = static_cast<std::uint32_t>(
                bytes[offset] - static_cast<std::uint8_t>('0'));
        }
        if (digit == UINT32_MAX || digit >= radix ||
            codepoint > (UINT32_C(0x10ffff) - digit) / radix) {
            return false;
        }
        codepoint = codepoint * radix + digit;
    }
    return AppendUtf8(output, codepoint);
}

bool DecodeValue(
    const laplace_decomposition_content& content,
    const laplace_ucdxml_property_view& property,
    std::vector<std::uint8_t>* const output) {
    output->clear();
    const std::uint64_t first = property.property_value_byte_start;
    const std::uint64_t last = property.property_value_byte_end;
    if (first > last || last > content.byte_count ||
        last > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    const std::uint8_t* const bytes =
        content.bytes + static_cast<std::size_t>(first);
    const std::size_t count = static_cast<std::size_t>(last - first);
    try {
        output->reserve(count);
        for (std::size_t index = 0u; index < count;) {
            const std::uint8_t byte = bytes[index];
            if (byte == static_cast<std::uint8_t>('#')) {
                AppendPositionHex(output, property.codepoint_position);
                ++index;
                continue;
            }
            if (byte != static_cast<std::uint8_t>('&')) {
                output->push_back(byte);
                ++index;
                continue;
            }
            std::size_t semicolon = index + 1u;
            while (semicolon < count &&
                   bytes[semicolon] != static_cast<std::uint8_t>(';')) {
                ++semicolon;
            }
            if (semicolon == count || semicolon == index + 1u ||
                !DecodeEntity(
                    bytes + index + 1u, semicolon - index - 1u, output)) {
                return false;
            }
            index = semicolon + 1u;
        }
    } catch (const std::bad_alloc&) {
        throw;
    }
    return true;
}

}  // namespace

struct laplace_ucdxml_projection {
    laplace_decomposition_content content{};
    laplace_digest256 xml_provider_fingerprint{};
    laplace_digest256 source_fingerprint{};
    laplace_digest256 recipe_fingerprint{};
    laplace_decomposition_xml_provider xml_provider{};
    std::vector<AttributeRef> attributes;
    std::vector<GroupRecord> groups;
    std::vector<CodepointRecord> records;
    laplace_ucdxml_projection_summary summary{};
};

namespace {

class Builder final {
public:
    Builder(
        const laplace_ucdxml_projection_input& input,
        laplace_ucdxml_projection* const output)
        : input_(input), output_(*output) {}

    laplace_ucdxml_status Build() {
        std::size_t span_count = 0u;
        spans_ = laplace_decomposition_spans(input_.decomposition, &span_count);
        if (spans_ == nullptr || span_count == 0u) {
            return LAPLACE_UCDXML_DECOMPOSITION_INVALID;
        }
        span_count_ = span_count;
        try {
            first_child_.assign(span_count_, NoIndex);
            last_child_.assign(span_count_, NoIndex);
            next_sibling_.assign(span_count_, NoIndex);
        } catch (const std::bad_alloc&) {
            return LAPLACE_UCDXML_MEMORY_FAILURE;
        }
        for (std::size_t index = 1u; index < span_count_; ++index) {
            const std::uint64_t parent = spans_[index].parent_span_index;
            if (parent >= static_cast<std::uint64_t>(span_count_)) {
                return LAPLACE_UCDXML_DECOMPOSITION_INVALID;
            }
            if (first_child_[static_cast<std::size_t>(parent)] == NoIndex) {
                first_child_[static_cast<std::size_t>(parent)] =
                    static_cast<std::uint64_t>(index);
            } else {
                next_sibling_[static_cast<std::size_t>(
                    last_child_[static_cast<std::size_t>(parent)])] =
                    static_cast<std::uint64_t>(index);
            }
            last_child_[static_cast<std::size_t>(parent)] =
                static_cast<std::uint64_t>(index);
        }

        std::unordered_map<std::uint64_t, std::uint64_t> group_by_span;
        try {
            for (std::size_t index = 0u; index < span_count_; ++index) {
                if (!IsXmlElement(index)) continue;
                const auto name = ElementName(index);
                if (name != "group") continue;
                const std::uint64_t parent_element = ParentXmlElement(index);
                if (parent_element != NoIndex && ElementName(
                        static_cast<std::size_t>(parent_element)) == "group") {
                    return LAPLACE_UCDXML_STRUCTURE_INVALID;
                }
                GroupRecord group{};
                group.element_span_index = static_cast<std::uint64_t>(index);
                const auto status = ExtractAttributes(index, &group.attributes);
                if (status != LAPLACE_UCDXML_OK) return status;
                const std::uint64_t group_index =
                    static_cast<std::uint64_t>(output_.groups.size());
                output_.groups.push_back(group);
                group_by_span.emplace(
                    static_cast<std::uint64_t>(index), group_index);
            }

            for (std::size_t index = 0u; index < span_count_; ++index) {
                if (!IsXmlElement(index)) continue;
                const auto name = ElementName(index);
                if (!IsCodepointElement(name)) continue;
                CodepointRecord record{};
                record.element_span_index = static_cast<std::uint64_t>(index);
                const auto status = ExtractAttributes(index, &record.attributes);
                if (status != LAPLACE_UCDXML_OK) return status;
                const AttributeRef* cp = FindAttribute(record.attributes, "cp");
                const AttributeRef* first = FindAttribute(record.attributes, "first-cp");
                const AttributeRef* last = FindAttribute(record.attributes, "last-cp");
                if (cp != nullptr) {
                    if (first != nullptr || last != nullptr ||
                        !ParseHexPosition(AttributeRawValue(output_.content, *cp),
                                          &record.first)) {
                        return LAPLACE_UCDXML_RANGE_INVALID;
                    }
                    record.last = record.first;
                } else {
                    if (first == nullptr || last == nullptr ||
                        !ParseHexPosition(AttributeRawValue(output_.content, *first),
                                          &record.first) ||
                        !ParseHexPosition(AttributeRawValue(output_.content, *last),
                                          &record.last) ||
                        record.first > record.last) {
                        return LAPLACE_UCDXML_RANGE_INVALID;
                    }
                }
                const std::uint64_t parent_element = ParentXmlElement(index);
                const auto group = group_by_span.find(parent_element);
                if (group != group_by_span.end()) record.group_index = group->second;
                output_.records.push_back(record);
            }
        } catch (const std::bad_alloc&) {
            return LAPLACE_UCDXML_MEMORY_FAILURE;
        }

        if (output_.records.empty()) return LAPLACE_UCDXML_STRUCTURE_INVALID;
        std::sort(
            output_.records.begin(), output_.records.end(),
            [](const CodepointRecord& left, const CodepointRecord& right) {
                return left.first < right.first ||
                    (left.first == right.first && left.last < right.last);
            });
        std::uint64_t covered = 0u;
        for (std::size_t index = 0u; index < output_.records.size(); ++index) {
            const auto& record = output_.records[index];
            if (index != 0u && record.first <= output_.records[index - 1u].last) {
                return LAPLACE_UCDXML_RANGE_INVALID;
            }
            covered += static_cast<std::uint64_t>(record.last) - record.first + 1u;
            if (covered > UINT32_MAX + UINT64_C(1)) {
                return LAPLACE_UCDXML_RANGE_INVALID;
            }
        }
        output_.summary.codepoint_declaration_count = output_.records.size();
        output_.summary.group_count = output_.groups.size();
        output_.summary.source_attribute_count = output_.attributes.size();
        output_.summary.covered_position_count = static_cast<std::uint32_t>(covered);
        output_.summary.status = LAPLACE_UCDXML_OK;
        CalculateProjectionFingerprint();
        return LAPLACE_UCDXML_OK;
    }

private:
    bool IsXmlSpan(const std::size_t index) const {
        return SameDigest(
            spans_[index].provider_fingerprint,
            output_.xml_provider_fingerprint);
    }

    bool IsXmlElement(const std::size_t index) const {
        return IsXmlSpan(index) &&
            spans_[index].kind == output_.xml_provider.element_kind;
    }

    std::uint64_t ParentXmlElement(const std::size_t index) const {
        std::uint64_t current = spans_[index].parent_span_index;
        while (current < static_cast<std::uint64_t>(span_count_)) {
            if (IsXmlElement(static_cast<std::size_t>(current))) return current;
            current = spans_[static_cast<std::size_t>(current)].parent_span_index;
        }
        return NoIndex;
    }

    std::string_view ElementName(const std::size_t element) const {
        const std::uint64_t start_tag = FindDirectChildKind(
            element, output_.xml_provider.start_tag_kind, 0u);
        if (start_tag == NoIndex) return {};
        const std::uint64_t name = FindDirectChildKind(
            static_cast<std::size_t>(start_tag),
            output_.xml_provider.element_name_kind,
            LAPLACE_DECOMPOSITION_XML_FIELD_ELEMENT_NAME);
        if (name == NoIndex) return {};
        const auto& span = spans_[static_cast<std::size_t>(name)];
        return std::string_view(
            reinterpret_cast<const char*>(
                output_.content.bytes + static_cast<std::size_t>(span.byte_start)),
            static_cast<std::size_t>(span.byte_end - span.byte_start));
    }

    std::uint64_t FindDirectChildKind(
        const std::size_t parent,
        const std::uint64_t kind,
        const std::uint64_t field_kind) const {
        for (std::uint64_t child = first_child_[parent]; child != NoIndex;
             child = next_sibling_[static_cast<std::size_t>(child)]) {
            const auto& span = spans_[static_cast<std::size_t>(child)];
            if (IsXmlSpan(static_cast<std::size_t>(child)) && span.kind == kind &&
                (field_kind == 0u || span.field_kind == field_kind)) {
                return child;
            }
        }
        return NoIndex;
    }

    laplace_ucdxml_status ExtractAttributes(
        const std::size_t element,
        AttributeSlice* const slice) {
        const std::uint64_t start_tag = FindDirectChildKind(
            element, output_.xml_provider.start_tag_kind, 0u);
        if (start_tag == NoIndex) return LAPLACE_UCDXML_STRUCTURE_INVALID;
        std::vector<AttributeRef> pending;
        try {
            for (std::uint64_t child =
                    first_child_[static_cast<std::size_t>(start_tag)];
                 child != NoIndex;
                 child = next_sibling_[static_cast<std::size_t>(child)]) {
                const auto& attribute_span = spans_[static_cast<std::size_t>(child)];
                if (!IsXmlSpan(static_cast<std::size_t>(child)) ||
                    attribute_span.kind != output_.xml_provider.attribute_kind) {
                    continue;
                }
                const std::uint64_t name = FindDirectChildKind(
                    static_cast<std::size_t>(child),
                    output_.xml_provider.attribute_name_kind,
                    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_NAME);
                if (name == NoIndex) return LAPLACE_UCDXML_STRUCTURE_INVALID;
                const auto& name_span = spans_[static_cast<std::size_t>(name)];
                AttributeRef ref{};
                ref.name_start = name_span.byte_start;
                ref.name_end = name_span.byte_end;
                const std::uint64_t value = FindDirectChildKind(
                    static_cast<std::size_t>(child),
                    output_.xml_provider.attribute_value_kind,
                    LAPLACE_DECOMPOSITION_XML_FIELD_ATTRIBUTE_VALUE);
                if (value != NoIndex) {
                    const auto& value_span = spans_[static_cast<std::size_t>(value)];
                    ref.value_start = value_span.byte_start;
                    ref.value_end = value_span.byte_end;
                } else if (!EmptyAttributeValue(attribute_span, &ref.value_start,
                                                &ref.value_end)) {
                    return LAPLACE_UCDXML_STRUCTURE_INVALID;
                }
                pending.push_back(ref);
            }
            std::sort(
                pending.begin(), pending.end(),
                [this](const AttributeRef& left, const AttributeRef& right) {
                    return NameCompare(output_.content, left, right) < 0;
                });
            for (std::size_t index = 1u; index < pending.size(); ++index) {
                if (NameCompare(output_.content, pending[index - 1u], pending[index]) == 0) {
                    return LAPLACE_UCDXML_STRUCTURE_INVALID;
                }
            }
            slice->begin = output_.attributes.size();
            slice->count = pending.size();
            output_.attributes.insert(
                output_.attributes.end(), pending.begin(), pending.end());
            return LAPLACE_UCDXML_OK;
        } catch (const std::bad_alloc&) {
            return LAPLACE_UCDXML_MEMORY_FAILURE;
        }
    }

    bool EmptyAttributeValue(
        const laplace_decomposition_span& span,
        std::uint64_t* const value_start,
        std::uint64_t* const value_end) const {
        std::uint64_t offset = span.byte_start;
        while (offset < span.byte_end &&
               output_.content.bytes[static_cast<std::size_t>(offset)] !=
                   static_cast<std::uint8_t>('=')) {
            ++offset;
        }
        if (offset >= span.byte_end) return false;
        ++offset;
        while (offset < span.byte_end) {
            const std::uint8_t byte =
                output_.content.bytes[static_cast<std::size_t>(offset)];
            if (byte != static_cast<std::uint8_t>(' ') &&
                byte != static_cast<std::uint8_t>('\t') &&
                byte != static_cast<std::uint8_t>('\r') &&
                byte != static_cast<std::uint8_t>('\n')) {
                break;
            }
            ++offset;
        }
        if (offset >= span.byte_end) return false;
        const std::uint8_t quote = output_.content.bytes[static_cast<std::size_t>(offset)];
        if (quote != static_cast<std::uint8_t>('"') &&
            quote != static_cast<std::uint8_t>('\'')) {
            return false;
        }
        ++offset;
        if (offset >= span.byte_end ||
            output_.content.bytes[static_cast<std::size_t>(offset)] != quote) {
            return false;
        }
        *value_start = offset;
        *value_end = offset;
        return true;
    }

    const AttributeRef* FindAttribute(
        const AttributeSlice& slice,
        const std::string_view name) const {
        std::size_t first = 0u;
        std::size_t last = slice.count;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(name.data());
        while (first < last) {
            const std::size_t middle = first + (last - first) / 2u;
            const AttributeRef& candidate =
                output_.attributes[slice.begin + middle];
            const int compare = NameCompare(
                output_.content, candidate, bytes, name.size());
            if (compare < 0) {
                first = middle + 1u;
            } else {
                last = middle;
            }
        }
        if (first >= slice.count) return nullptr;
        const AttributeRef& candidate = output_.attributes[slice.begin + first];
        return NameCompare(output_.content, candidate, bytes, name.size()) == 0
            ? &candidate
            : nullptr;
    }

    void CalculateProjectionFingerprint() {
        blake3_hasher hasher{};
        blake3_hasher_init(&hasher);
        HashBytes(
            hasher,
            reinterpret_cast<const std::uint8_t*>(ProjectionDomain.data()),
            ProjectionDomain.size());
        HashDigest(hasher, output_.source_fingerprint);
        HashDigest(hasher, output_.recipe_fingerprint);
        HashDigest(hasher, output_.xml_provider_fingerprint);
        HashU64(hasher, output_.summary.codepoint_declaration_count);
        HashU64(hasher, output_.summary.group_count);
        HashU64(hasher, output_.summary.source_attribute_count);
        HashU32(hasher, output_.summary.covered_position_count);
        for (const auto& record : output_.records) {
            HashU32(hasher, record.first);
            HashU32(hasher, record.last);
            HashU64(hasher, record.element_span_index);
            HashU64(hasher, record.group_index);
            HashU64(hasher, record.attributes.begin);
            HashU64(hasher, record.attributes.count);
        }
        output_.summary.projection_fingerprint = Finish(hasher);
    }

    const laplace_ucdxml_projection_input& input_;
    laplace_ucdxml_projection& output_;
    const laplace_decomposition_span* spans_{};
    std::size_t span_count_{};
    std::vector<std::uint64_t> first_child_;
    std::vector<std::uint64_t> last_child_;
    std::vector<std::uint64_t> next_sibling_;
};

const CodepointRecord* FindRecord(
    const laplace_ucdxml_projection& projection,
    const std::uint32_t position) {
    const auto iterator = std::upper_bound(
        projection.records.begin(), projection.records.end(), position,
        [](const std::uint32_t sought, const CodepointRecord& record) {
            return sought < record.first;
        });
    if (iterator == projection.records.begin()) return nullptr;
    const auto& candidate = *(iterator - 1);
    return position <= candidate.last ? &candidate : nullptr;
}

const AttributeRef* FindInSlice(
    const laplace_ucdxml_projection& projection,
    const AttributeSlice& slice,
    const std::uint8_t* const name,
    const std::size_t name_bytes) {
    std::size_t first = 0u;
    std::size_t last = slice.count;
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2u;
        const auto& candidate = projection.attributes[slice.begin + middle];
        if (NameCompare(projection.content, candidate, name, name_bytes) < 0) {
            first = middle + 1u;
        } else {
            last = middle;
        }
    }
    if (first >= slice.count) return nullptr;
    const auto& candidate = projection.attributes[slice.begin + first];
    return NameCompare(projection.content, candidate, name, name_bytes) == 0
        ? &candidate
        : nullptr;
}

bool ContainsHash(
    const laplace_decomposition_content& content,
    const AttributeRef& attribute) {
    for (std::uint64_t offset = attribute.value_start;
         offset < attribute.value_end; ++offset) {
        if (content.bytes[static_cast<std::size_t>(offset)] ==
            static_cast<std::uint8_t>('#')) {
            return true;
        }
    }
    return false;
}

void ObservationFingerprint(
    const laplace_ucdxml_projection& projection,
    const CodepointRecord& record,
    const AttributeRef& attribute,
    const bool inherited,
    laplace_ucdxml_property_view* const property) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashBytes(
        hasher,
        reinterpret_cast<const std::uint8_t*>(ObservationDomain.data()),
        ObservationDomain.size());
    HashDigest(hasher, projection.source_fingerprint);
    HashDigest(hasher, projection.recipe_fingerprint);
    HashDigest(hasher, projection.xml_provider_fingerprint);
    HashU32(hasher, property->codepoint_position);
    HashU32(hasher, record.first);
    HashU32(hasher, record.last);
    HashU64(hasher, record.element_span_index);
    HashU64(hasher, property->declaration_element_span_index);
    HashBytes(
        hasher,
        projection.content.bytes + static_cast<std::size_t>(attribute.name_start),
        static_cast<std::size_t>(attribute.name_end - attribute.name_start));
    HashBytes(
        hasher,
        projection.content.bytes + static_cast<std::size_t>(attribute.value_start),
        static_cast<std::size_t>(attribute.value_end - attribute.value_start));
    HashU32(hasher, inherited ? 1u : 0u);
    property->observation_fingerprint = Finish(hasher);
}

void FillProperty(
    const laplace_ucdxml_projection& projection,
    const CodepointRecord& record,
    const AttributeRef& attribute,
    const bool inherited,
    const std::uint32_t position,
    laplace_ucdxml_property_view* const property) {
    *property = laplace_ucdxml_property_view{};
    property->element_span_index = record.element_span_index;
    property->declaration_element_span_index = inherited
        ? projection.groups[static_cast<std::size_t>(record.group_index)].element_span_index
        : record.element_span_index;
    property->property_name_byte_start = attribute.name_start;
    property->property_name_byte_end = attribute.name_end;
    property->property_value_byte_start = attribute.value_start;
    property->property_value_byte_end = attribute.value_end;
    property->codepoint_position = position;
    property->range_first = record.first;
    property->range_last = record.last;
    if (inherited) {
        property->flags |= LAPLACE_UCDXML_PROPERTY_INHERITED_FROM_GROUP;
    }
    if (record.first != record.last) {
        property->flags |= LAPLACE_UCDXML_PROPERTY_RANGE_DECLARATION;
    }
    if (ContainsHash(projection.content, attribute)) {
        property->flags |= LAPLACE_UCDXML_PROPERTY_HASH_SUBSTITUTION;
    }
    ObservationFingerprint(projection, record, attribute, inherited, property);
}

struct MergeCursor final {
    std::size_t group_index{};
    std::size_t local_index{};
};

bool NextEffective(
    const laplace_ucdxml_projection& projection,
    const CodepointRecord& record,
    MergeCursor* const cursor,
    const AttributeRef** const attribute,
    bool* const inherited) {
    const AttributeSlice empty{};
    const AttributeSlice& group = record.group_index == NoIndex
        ? empty
        : projection.groups[static_cast<std::size_t>(record.group_index)].attributes;
    const AttributeSlice& local = record.attributes;
    for (;;) {
        while (cursor->group_index < group.count &&
               IsCoordinateName(AttributeName(
                   projection.content,
                   projection.attributes[group.begin + cursor->group_index]))) {
            ++cursor->group_index;
        }
        while (cursor->local_index < local.count &&
               IsCoordinateName(AttributeName(
                   projection.content,
                   projection.attributes[local.begin + cursor->local_index]))) {
            ++cursor->local_index;
        }
        if (cursor->group_index >= group.count &&
            cursor->local_index >= local.count) {
            return false;
        }
        if (cursor->group_index >= group.count) {
            *attribute = &projection.attributes[local.begin + cursor->local_index++];
            *inherited = false;
            return true;
        }
        if (cursor->local_index >= local.count) {
            *attribute = &projection.attributes[group.begin + cursor->group_index++];
            *inherited = true;
            return true;
        }
        const auto& group_attribute =
            projection.attributes[group.begin + cursor->group_index];
        const auto& local_attribute =
            projection.attributes[local.begin + cursor->local_index];
        const int compare = NameCompare(
            projection.content, group_attribute, local_attribute);
        if (compare < 0) {
            *attribute = &group_attribute;
            ++cursor->group_index;
            *inherited = true;
            return true;
        }
        if (compare > 0) {
            *attribute = &local_attribute;
            ++cursor->local_index;
            *inherited = false;
            return true;
        }
        *attribute = &local_attribute;
        ++cursor->group_index;
        ++cursor->local_index;
        *inherited = false;
        return true;
    }
}

}  // namespace

extern "C" laplace_ucdxml_status laplace_ucdxml_projection_create(
    const laplace_ucdxml_projection_input* const input,
    laplace_ucdxml_projection** const projection,
    laplace_ucdxml_projection_summary* const summary) {
    if (projection != nullptr) *projection = nullptr;
    if (summary != nullptr) *summary = laplace_ucdxml_projection_summary{};
    if (input == nullptr || projection == nullptr || summary == nullptr ||
        input->content == nullptr || input->decomposition == nullptr ||
        input->xml_provider == nullptr || input->content->bytes == nullptr ||
        input->content->byte_count == 0u ||
        input->content->byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        DigestZero(input->source_fingerprint) || DigestZero(input->recipe_fingerprint) ||
        DigestZero(input->xml_provider->provider.provider_fingerprint) ||
        input->flags != 0u || input->reserved != 0u) {
        return LAPLACE_UCDXML_INVALID_ARGUMENT;
    }
    auto* output = new (std::nothrow) laplace_ucdxml_projection{};
    if (output == nullptr) return LAPLACE_UCDXML_MEMORY_FAILURE;
    output->content = *input->content;
    output->xml_provider = *input->xml_provider;
    output->xml_provider_fingerprint =
        input->xml_provider->provider.provider_fingerprint;
    output->source_fingerprint = input->source_fingerprint;
    output->recipe_fingerprint = input->recipe_fingerprint;
    Builder builder(*input, output);
    const auto status = builder.Build();
    if (status != LAPLACE_UCDXML_OK) {
        delete output;
        return status;
    }
    *summary = output->summary;
    *projection = output;
    return LAPLACE_UCDXML_OK;
}

extern "C" void laplace_ucdxml_projection_destroy(
    laplace_ucdxml_projection** const projection) {
    if (projection != nullptr) {
        delete *projection;
        *projection = nullptr;
    }
}

extern "C" laplace_ucdxml_status laplace_ucdxml_property_count(
    const laplace_ucdxml_projection* const projection,
    const std::uint32_t codepoint_position,
    size_t* const property_count) {
    if (property_count != nullptr) *property_count = 0u;
    if (projection == nullptr || property_count == nullptr ||
        codepoint_position >= UINT32_C(0x110000)) {
        return LAPLACE_UCDXML_INVALID_ARGUMENT;
    }
    const CodepointRecord* const record = FindRecord(*projection, codepoint_position);
    if (record == nullptr) return LAPLACE_UCDXML_PROPERTY_NOT_FOUND;
    MergeCursor cursor{};
    const AttributeRef* attribute = nullptr;
    bool inherited = false;
    std::size_t count = 0u;
    while (NextEffective(
        *projection, *record, &cursor, &attribute, &inherited)) {
        (void)attribute;
        (void)inherited;
        if (count == std::numeric_limits<std::size_t>::max()) {
            return LAPLACE_UCDXML_PROPERTY_RANGE;
        }
        ++count;
    }
    *property_count = count;
    return LAPLACE_UCDXML_OK;
}

extern "C" laplace_ucdxml_status laplace_ucdxml_property(
    const laplace_ucdxml_projection* const projection,
    const std::uint32_t codepoint_position,
    const size_t property_index,
    laplace_ucdxml_property_view* const property) {
    if (property != nullptr) *property = laplace_ucdxml_property_view{};
    if (projection == nullptr || property == nullptr ||
        codepoint_position >= UINT32_C(0x110000)) {
        return LAPLACE_UCDXML_INVALID_ARGUMENT;
    }
    const CodepointRecord* const record = FindRecord(*projection, codepoint_position);
    if (record == nullptr) return LAPLACE_UCDXML_PROPERTY_NOT_FOUND;
    MergeCursor cursor{};
    const AttributeRef* attribute = nullptr;
    bool inherited = false;
    std::size_t index = 0u;
    while (NextEffective(
        *projection, *record, &cursor, &attribute, &inherited)) {
        if (index == property_index) {
            FillProperty(
                *projection, *record, *attribute, inherited,
                codepoint_position, property);
            return LAPLACE_UCDXML_OK;
        }
        ++index;
    }
    return LAPLACE_UCDXML_PROPERTY_RANGE;
}

extern "C" laplace_ucdxml_status laplace_ucdxml_property_find(
    const laplace_ucdxml_projection* const projection,
    const std::uint32_t codepoint_position,
    const std::uint8_t* const property_name,
    const size_t property_name_bytes,
    laplace_ucdxml_property_view* const property) {
    if (property != nullptr) *property = laplace_ucdxml_property_view{};
    if (projection == nullptr || property_name == nullptr ||
        property_name_bytes == 0u || property == nullptr ||
        codepoint_position >= UINT32_C(0x110000)) {
        return LAPLACE_UCDXML_INVALID_ARGUMENT;
    }
    const CodepointRecord* const record = FindRecord(*projection, codepoint_position);
    if (record == nullptr) return LAPLACE_UCDXML_PROPERTY_NOT_FOUND;
    const AttributeRef* attribute = FindInSlice(
        *projection, record->attributes, property_name, property_name_bytes);
    bool inherited = false;
    if (attribute == nullptr && record->group_index != NoIndex) {
        attribute = FindInSlice(
            *projection,
            projection->groups[static_cast<std::size_t>(record->group_index)].attributes,
            property_name, property_name_bytes);
        inherited = attribute != nullptr;
    }
    if (attribute == nullptr || IsCoordinateName(AttributeName(
            projection->content, *attribute))) {
        return LAPLACE_UCDXML_PROPERTY_NOT_FOUND;
    }
    FillProperty(
        *projection, *record, *attribute, inherited,
        codepoint_position, property);
    return LAPLACE_UCDXML_OK;
}

extern "C" laplace_ucdxml_status laplace_ucdxml_property_value(
    const laplace_ucdxml_projection* const projection,
    const laplace_ucdxml_property_view* const property,
    std::uint8_t* const output,
    const size_t output_capacity,
    size_t* const required_bytes) {
    if (required_bytes != nullptr) *required_bytes = 0u;
    if (projection == nullptr || property == nullptr || required_bytes == nullptr ||
        (output == nullptr && output_capacity != 0u) ||
        property->codepoint_position >= UINT32_C(0x110000) ||
        property->property_value_byte_start > property->property_value_byte_end ||
        property->property_value_byte_end > projection->content.byte_count ||
        (property->flags & ~LAPLACE_UCDXML_PROPERTY_KNOWN_FLAGS) != 0u) {
        return LAPLACE_UCDXML_INVALID_ARGUMENT;
    }
    try {
        std::vector<std::uint8_t> decoded;
        if (!DecodeValue(projection->content, *property, &decoded)) {
            return LAPLACE_UCDXML_VALUE_INVALID;
        }
        *required_bytes = decoded.size();
        if (output == nullptr) {
            return output_capacity == 0u
                ? LAPLACE_UCDXML_OK
                : LAPLACE_UCDXML_INVALID_ARGUMENT;
        }
        if (output_capacity < decoded.size()) {
            return LAPLACE_UCDXML_CAPACITY_INSUFFICIENT;
        }
        if (!decoded.empty()) std::memcpy(output, decoded.data(), decoded.size());
        return LAPLACE_UCDXML_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_UCDXML_MEMORY_FAILURE;
    }
}
