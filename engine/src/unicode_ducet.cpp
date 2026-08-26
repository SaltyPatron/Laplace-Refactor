#include "laplace/unicode_root.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blake3.h"

namespace {

using PositionSequence = std::vector<std::uint32_t>;

struct PositionSequenceHash {
    std::size_t operator()(const PositionSequence& value) const noexcept {
        std::size_t result = UINT64_C(1469598103934665603);
        for (const std::uint32_t position : value) {
            result ^= static_cast<std::size_t>(position);
            result *= UINT64_C(1099511628211);
        }
        result ^= value.size();
        return result;
    }
};

struct Mapping {
    PositionSequence sequence;
    std::vector<laplace_unicode_collation_element> elements;
    std::uint32_t source_line_ordinal{};
};

struct ImplicitRange {
    std::uint32_t first{};
    std::uint32_t last{};
    std::uint16_t lead_primary{};
    std::uint32_t source_line_ordinal{};
};

constexpr std::string_view RetainedTableDomain{
    "laplace-unicode-ducet-retained-table-v1"};
constexpr std::string_view ReceiptDomain{
    "laplace-unicode-ducet-retained-table-receipt-v1"};

std::string_view Trim(std::string_view value) {
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t' ||
            value.front() == '\r')) {
        value.remove_prefix(1u);
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' ||
            value.back() == '\r')) {
        value.remove_suffix(1u);
    }
    return value;
}

std::string_view RemoveComment(std::string_view value) {
    const std::size_t hash = value.find('#');
    const std::size_t percent = value.find('%');
    const std::size_t comment =
        hash == std::string_view::npos
            ? percent
            : (percent == std::string_view::npos ? hash
                                                 : std::min(hash, percent));
    return Trim(comment == std::string_view::npos
                    ? value
                    : value.substr(0u, comment));
}

bool ParseHex(std::string_view value, std::uint32_t maximum,
              std::uint32_t& output) {
    value = Trim(value);
    if (value.empty()) {
        return false;
    }
    std::uint32_t parsed = 0u;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed, 16);
    if (result.ec != std::errc{} ||
        result.ptr != value.data() + value.size() || parsed > maximum) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParsePositionSequence(std::string_view value, PositionSequence& output) {
    output.clear();
    value = Trim(value);
    while (!value.empty()) {
        const std::size_t separator = value.find_first_of(" \t");
        const std::string_view token =
            separator == std::string_view::npos
                ? value
                : value.substr(0u, separator);
        std::uint32_t position = 0u;
        if (!ParseHex(token, LAPLACE_UNICODE_ROOT_POPULATION - 1u,
                      position)) {
            return false;
        }
        output.push_back(position);
        if (separator == std::string_view::npos) {
            break;
        }
        value = Trim(value.substr(separator + 1u));
    }
    return !output.empty();
}

bool ParseElements(
    std::string_view value,
    std::vector<laplace_unicode_collation_element>& output) {
    output.clear();
    value = Trim(value);
    while (!value.empty()) {
        if (value.front() != '[') {
            return false;
        }
        const std::size_t close = value.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        const std::string_view token = value.substr(1u, close - 1u);
        if (token.size() != 15u || (token.front() != '.' && token.front() != '*') ||
            token[5] != '.' || token[10] != '.') {
            return false;
        }
        std::array<std::uint32_t, 3> weights{};
        if (!ParseHex(token.substr(1u, 4u), UINT16_MAX, weights[0]) ||
            !ParseHex(token.substr(6u, 4u), UINT16_MAX, weights[1]) ||
            !ParseHex(token.substr(11u, 4u), UINT16_MAX, weights[2])) {
            return false;
        }
        laplace_unicode_collation_element element{};
        element.primary = static_cast<std::uint16_t>(weights[0]);
        element.secondary = static_cast<std::uint16_t>(weights[1]);
        element.tertiary = static_cast<std::uint16_t>(weights[2]);
        element.variable = token.front() == '*' ? 1u : 0u;
        output.push_back(element);
        value = Trim(value.substr(close + 1u));
    }
    return !output.empty();
}

bool ParseImplicitRange(std::string_view line, std::uint32_t line_ordinal,
                        ImplicitRange& output) {
    constexpr std::string_view Prefix{"@implicitweights"};
    if (!line.starts_with(Prefix)) {
        return false;
    }
    line = Trim(line.substr(Prefix.size()));
    const std::size_t semicolon = line.find(';');
    if (semicolon == std::string_view::npos) {
        return false;
    }
    const std::string_view interval = Trim(line.substr(0u, semicolon));
    const std::size_t dots = interval.find("..");
    if (dots == std::string_view::npos) {
        return false;
    }
    std::uint32_t first = 0u;
    std::uint32_t last = 0u;
    std::uint32_t lead = 0u;
    if (!ParseHex(interval.substr(0u, dots),
                  LAPLACE_UNICODE_ROOT_POPULATION - 1u, first) ||
        !ParseHex(interval.substr(dots + 2u),
                  LAPLACE_UNICODE_ROOT_POPULATION - 1u, last) ||
        !ParseHex(line.substr(semicolon + 1u), UINT16_MAX, lead) ||
        first > last || lead == 0u) {
        return false;
    }
    output = ImplicitRange{
        first, last, static_cast<std::uint16_t>(lead), line_ordinal};
    return true;
}

void HashU16(blake3_hasher& hasher, std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU32(blake3_hasher& hasher, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, std::uint64_t value) {
    const std::array<std::uint8_t, 8> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u),
        static_cast<std::uint8_t>(value >> 32u),
        static_cast<std::uint8_t>(value >> 40u),
        static_cast<std::uint8_t>(value >> 48u),
        static_cast<std::uint8_t>(value >> 56u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashString(blake3_hasher& hasher, std::string_view value) {
    HashU64(hasher, value.size());
    blake3_hasher_update(&hasher, value.data(), value.size());
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool SameSequence(const PositionSequence& left, const std::uint32_t* right,
                  std::uint32_t right_count) {
    return left.size() == right_count &&
        std::equal(left.begin(), left.end(), right);
}

}  // namespace

struct laplace_unicode_ducet_table {
    std::vector<Mapping> mappings;
    std::vector<ImplicitRange> implicit_ranges;
    std::unordered_map<PositionSequence, std::size_t, PositionSequenceHash>
        mapping_index;
    laplace_unicode_ducet_summary summary{};
};

namespace {

laplace_unicode_ducet_mapping_view MappingView(const Mapping& mapping) {
    return laplace_unicode_ducet_mapping_view{
        mapping.sequence.data(), mapping.elements.data(),
        mapping.source_line_ordinal,
        static_cast<std::uint32_t>(mapping.sequence.size()),
        static_cast<std::uint32_t>(mapping.elements.size())};
}

bool FinalizeTable(laplace_unicode_ducet_table& table,
                   const laplace_unicode_source_receipt& source) {
    blake3_hasher retained{};
    blake3_hasher_init(&retained);
    HashString(retained, RetainedTableDomain);
    blake3_hasher_update(
        &retained, source.source_fingerprint.bytes,
        sizeof(source.source_fingerprint.bytes));
    blake3_hasher_update(
        &retained, source.recipe_fingerprint.bytes,
        sizeof(source.recipe_fingerprint.bytes));
    HashU64(retained, table.implicit_ranges.size());
    for (const ImplicitRange& range : table.implicit_ranges) {
        HashU32(retained, range.first);
        HashU32(retained, range.last);
        HashU16(retained, range.lead_primary);
        HashU32(retained, range.source_line_ordinal);
    }
    HashU64(retained, table.mappings.size());
    for (const Mapping& mapping : table.mappings) {
        HashU32(retained, mapping.source_line_ordinal);
        HashU32(retained, static_cast<std::uint32_t>(mapping.sequence.size()));
        for (const std::uint32_t position : mapping.sequence) {
            HashU32(retained, position);
        }
        HashU32(retained, static_cast<std::uint32_t>(mapping.elements.size()));
        for (const laplace_unicode_collation_element& element :
             mapping.elements) {
            HashU16(retained, element.primary);
            HashU16(retained, element.secondary);
            HashU16(retained, element.tertiary);
            HashU16(retained, element.variable);
        }
    }
    table.summary.source_fingerprint = source.source_fingerprint;
    table.summary.recipe_fingerprint = source.recipe_fingerprint;
    table.summary.retained_table_fingerprint = Finish(retained);

    blake3_hasher receipt{};
    blake3_hasher_init(&receipt);
    HashString(receipt, ReceiptDomain);
    blake3_hasher_update(
        &receipt, table.summary.retained_table_fingerprint.bytes,
        sizeof(table.summary.retained_table_fingerprint.bytes));
    HashU64(receipt, table.summary.explicit_mapping_count);
    HashU64(receipt, table.summary.explicit_single_position_count);
    HashU64(receipt, table.summary.contraction_count);
    HashU64(receipt, table.summary.expansion_mapping_count);
    HashU64(receipt, table.summary.collation_element_count);
    HashU64(receipt, table.summary.variable_collation_element_count);
    table.summary.receipt_id = Finish(receipt);
    table.summary.status = LAPLACE_UNICODE_OK;
    return true;
}

bool ParseTable(const laplace_unicode_source_bundle* bundle,
                laplace_unicode_ducet_table& table) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "uca/allkeys.txt", &file) != LAPLACE_UNICODE_OK ||
        file.bytes == nullptr || file.byte_count == 0u ||
        file.byte_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return false;
    }
    const std::string_view input(
        reinterpret_cast<const char*>(file.bytes),
        static_cast<std::size_t>(file.byte_count));
    bool version_seen = false;
    std::size_t offset = 0u;
    std::uint32_t line_ordinal = 0u;
    while (offset < input.size()) {
        const std::size_t end = input.find('\n', offset);
        const std::size_t length = end == std::string_view::npos
            ? input.size() - offset
            : end - offset;
        ++line_ordinal;
        const std::string_view line = RemoveComment(input.substr(offset, length));
        offset = end == std::string_view::npos ? input.size() : end + 1u;
        if (line.empty()) {
            continue;
        }
        if (line.starts_with("@version")) {
            if (version_seen || Trim(line.substr(8u)) != "17.0.0") {
                return false;
            }
            version_seen = true;
            continue;
        }
        if (line.starts_with("@implicitweights")) {
            ImplicitRange range{};
            if (!version_seen || !ParseImplicitRange(line, line_ordinal, range)) {
                return false;
            }
            table.implicit_ranges.push_back(range);
            continue;
        }
        const std::size_t semicolon = line.find(';');
        if (!version_seen || semicolon == std::string_view::npos) {
            return false;
        }
        Mapping mapping;
        mapping.source_line_ordinal = line_ordinal;
        if (!ParsePositionSequence(line.substr(0u, semicolon), mapping.sequence) ||
            !ParseElements(line.substr(semicolon + 1u), mapping.elements) ||
            mapping.sequence.size() > UINT32_MAX ||
            mapping.elements.size() > UINT32_MAX) {
            return false;
        }
#if defined(LAPLACE_TEST_DUCET_TRUNCATE_EXPANSIONS)
        if (mapping.elements.size() > 1u) {
            mapping.elements.resize(1u);
        }
#endif
        if (table.mapping_index.find(mapping.sequence) !=
            table.mapping_index.end()) {
            return false;
        }
        table.summary.explicit_mapping_count += 1u;
        table.summary.explicit_single_position_count +=
            mapping.sequence.size() == 1u ? 1u : 0u;
        table.summary.contraction_count +=
            mapping.sequence.size() > 1u ? 1u : 0u;
        table.summary.expansion_mapping_count +=
            mapping.elements.size() > 1u ? 1u : 0u;
        table.summary.collation_element_count += mapping.elements.size();
        table.summary.maximum_sequence_count = std::max(
            table.summary.maximum_sequence_count,
            static_cast<std::uint32_t>(mapping.sequence.size()));
        table.summary.maximum_element_count = std::max(
            table.summary.maximum_element_count,
            static_cast<std::uint32_t>(mapping.elements.size()));
        for (const laplace_unicode_collation_element& element :
             mapping.elements) {
            table.summary.variable_collation_element_count +=
                element.variable != 0u ? 1u : 0u;
        }
        const std::size_t mapping_index = table.mappings.size();
        table.mappings.push_back(std::move(mapping));
        table.mapping_index.emplace(
            table.mappings.back().sequence, mapping_index);
    }
    table.summary.implicit_range_count =
        static_cast<std::uint32_t>(table.implicit_ranges.size());
    if (!version_seen || table.mappings.empty() ||
        table.implicit_ranges.empty()) {
        return false;
    }
    std::sort(
        table.implicit_ranges.begin(), table.implicit_ranges.end(),
        [](const ImplicitRange& left, const ImplicitRange& right) {
            return left.first < right.first;
        });
    for (std::size_t index = 1u; index < table.implicit_ranges.size(); ++index) {
        if (table.implicit_ranges[index - 1u].last >=
            table.implicit_ranges[index].first) {
            return false;
        }
    }
    return true;
}

struct CalculatedCollation {
    PositionSequence normalized;
    std::vector<laplace_unicode_collation_element> elements;
    std::vector<std::uint8_t> key;
    std::uint8_t provenance{LAPLACE_UNICODE_DUCET_EXPLICIT};
};

const laplace_unicode_atom_field* CoreField(
    const laplace_unicode_core_record_view& view, std::uint16_t field_id) {
    return field_id == 0u || field_id > LAPLACE_UNICODE_CORE_FIELD_COUNT
        ? nullptr
        : &view.fields[field_id - 1u];
}

std::uint32_t ReadU32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0]) |
        (static_cast<std::uint32_t>(value[1]) << 8u) |
        (static_cast<std::uint32_t>(value[2]) << 16u) |
        (static_cast<std::uint32_t>(value[3]) << 24u);
}

std::uint16_t ReadU16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(value[1]) << 8u);
}

bool CoreRecord(const laplace_unicode_core_table* core, std::uint32_t position,
                laplace_unicode_core_record_view& view) {
    return laplace_unicode_core_table_record(core, position, &view) ==
        LAPLACE_UNICODE_OK;
}

bool CanonicalCombiningClass(const laplace_unicode_core_table* core,
                             std::uint32_t position, std::uint8_t& value) {
    laplace_unicode_core_record_view record{};
    if (!CoreRecord(core, position, record)) {
        return false;
    }
    const laplace_unicode_atom_field* field = CoreField(record, 2u);
    if (field == nullptr || field->payload == nullptr ||
        field->payload_bytes != 1u) {
        return false;
    }
    value = field->payload[0];
    return true;
}

bool AppendCanonicalDecomposition(
    const laplace_unicode_core_table* core, std::uint32_t position,
    PositionSequence& output, std::uint32_t depth) {
    constexpr std::uint32_t HangulBase = 0xac00u;
    constexpr std::uint32_t HangulLast = 0xd7a3u;
    constexpr std::uint32_t LeadingBase = 0x1100u;
    constexpr std::uint32_t VowelBase = 0x1161u;
    constexpr std::uint32_t TrailingBase = 0x11a7u;
    constexpr std::uint32_t VowelCount = 21u;
    constexpr std::uint32_t TrailingCount = 28u;
    constexpr std::uint32_t SyllablesPerLeading = VowelCount * TrailingCount;
    if (depth > 64u) {
        return false;
    }
    if (position >= HangulBase && position <= HangulLast) {
        const std::uint32_t syllable = position - HangulBase;
        output.push_back(LeadingBase + syllable / SyllablesPerLeading);
        output.push_back(
            VowelBase + (syllable % SyllablesPerLeading) / TrailingCount);
        const std::uint32_t trailing = syllable % TrailingCount;
        if (trailing != 0u) {
            output.push_back(TrailingBase + trailing);
        }
        return true;
    }
    laplace_unicode_core_record_view record{};
    if (!CoreRecord(core, position, record)) {
        return false;
    }
    const laplace_unicode_atom_field* field = CoreField(record, 6u);
    if (field == nullptr || field->payload_bytes == 0u) {
        output.push_back(position);
        return true;
    }
    if (field->payload == nullptr || (field->payload_bytes % 4u) != 0u) {
        return false;
    }
    for (std::uint32_t offset = 0u; offset < field->payload_bytes;
         offset += 4u) {
        const std::uint32_t child = ReadU32(field->payload + offset);
        if (child >= LAPLACE_UNICODE_ROOT_POPULATION ||
            !AppendCanonicalDecomposition(core, child, output, depth + 1u)) {
            return false;
        }
    }
    return true;
}

bool NormalizeNfd(const laplace_unicode_core_table* core,
                  const std::uint32_t* sequence, std::uint32_t sequence_count,
                  PositionSequence& output) {
    output.clear();
    for (std::uint32_t index = 0u; index < sequence_count; ++index) {
        if (sequence[index] >= LAPLACE_UNICODE_ROOT_POPULATION ||
            !AppendCanonicalDecomposition(core, sequence[index], output, 0u)) {
            return false;
        }
    }
    for (std::size_t index = 1u; index < output.size(); ++index) {
        std::uint8_t current_class = 0u;
        if (!CanonicalCombiningClass(core, output[index], current_class)) {
            return false;
        }
        if (current_class == 0u) {
            continue;
        }
        std::size_t insertion = index;
        while (insertion > 0u) {
            std::uint8_t previous_class = 0u;
            if (!CanonicalCombiningClass(
                    core, output[insertion - 1u], previous_class)) {
                return false;
            }
            if (previous_class == 0u || previous_class <= current_class) {
                break;
            }
            std::swap(output[insertion], output[insertion - 1u]);
            --insertion;
        }
    }
    return true;
}

const Mapping* FindMapping(const laplace_unicode_ducet_table& table,
                           const PositionSequence& sequence) {
    const auto found = table.mapping_index.find(sequence);
    return found == table.mapping_index.end()
        ? nullptr
        : &table.mappings[found->second];
}

bool FieldAsciiEquals(const laplace_unicode_atom_field* field,
                      std::string_view expected) {
    return field != nullptr && field->payload_bytes == expected.size() &&
        (expected.empty() ||
         (field->payload != nullptr &&
          std::memcmp(field->payload, expected.data(), expected.size()) == 0));
}

bool SetFieldContains(const laplace_unicode_atom_field* field,
                      std::string_view expected) {
    if (field == nullptr || field->payload == nullptr ||
        field->payload_bytes < 4u) {
        return false;
    }
    const std::uint32_t count = ReadU32(field->payload);
    std::size_t offset = 4u;
    for (std::uint32_t index = 0u; index < count; ++index) {
        if (offset + 2u > field->payload_bytes) {
            return false;
        }
        const std::uint16_t bytes = ReadU16(field->payload + offset);
        offset += 2u;
        if (offset + bytes > field->payload_bytes) {
            return false;
        }
        if (bytes == expected.size() &&
            std::memcmp(field->payload + offset, expected.data(), bytes) == 0) {
            return true;
        }
        offset += bytes;
    }
    return false;
}

bool ImplicitElements(const laplace_unicode_ducet_table& table,
                      const laplace_unicode_core_table* core,
                      std::uint32_t position,
                      std::vector<laplace_unicode_collation_element>& output) {
    laplace_unicode_core_record_view record{};
    if (!CoreRecord(core, position, record)) {
        return false;
    }
    std::uint32_t lead = 0u;
    std::uint32_t trail_source = position;
    if (record.position_class == LAPLACE_UNICODE_ASSIGNED_SCALAR) {
        const auto range = std::find_if(
            table.implicit_ranges.begin(), table.implicit_ranges.end(),
            [&](const ImplicitRange& candidate) {
                return position >= candidate.first && position <= candidate.last;
            });
        if (range != table.implicit_ranges.end()) {
            lead = range->lead_primary;
            std::uint32_t family_base = range->first;
            for (const ImplicitRange& candidate : table.implicit_ranges) {
                if (candidate.lead_primary == range->lead_primary) {
                    family_base = std::min(family_base, candidate.first);
                }
            }
            trail_source = position - family_base;
        }
    }
    if (lead == 0u &&
        SetFieldContains(CoreField(record, 16u), "Unified_Ideograph")) {
        const laplace_unicode_atom_field* block = CoreField(record, 12u);
        const bool core_han =
            FieldAsciiEquals(block, "CJK_Unified_Ideographs") ||
            FieldAsciiEquals(block, "CJK_Compatibility_Ideographs");
        lead = (core_han ? 0xfb40u : 0xfb80u) + (position >> 15u);
        trail_source = position;
    }
    if (lead == 0u) {
        lead = 0xfbc0u + (position >> 15u);
        trail_source = position;
    }
    const std::uint32_t trail = (trail_source & 0x7fffu) | 0x8000u;
    if (lead > UINT16_MAX || trail > UINT16_MAX) {
        return false;
    }
    output.push_back(laplace_unicode_collation_element{
        static_cast<std::uint16_t>(lead), 0x0020u, 0x0002u, 0u, 0u});
    output.push_back(laplace_unicode_collation_element{
        static_cast<std::uint16_t>(trail), 0u, 0u, 0u, 0u});
    return true;
}

bool ProduceCollationElements(
    const laplace_unicode_ducet_table& table,
    const laplace_unicode_core_table* core,
    const PositionSequence& normalized,
    std::vector<laplace_unicode_collation_element>& elements,
    bool& used_implicit) {
    PositionSequence remaining = normalized;
    elements.clear();
    used_implicit = false;
    while (!remaining.empty()) {
        const Mapping* mapping = nullptr;
        std::size_t contiguous_count = 0u;
        const std::size_t maximum = std::min<std::size_t>(
            table.summary.maximum_sequence_count, remaining.size());
        for (std::size_t count = maximum; count > 0u; --count) {
            PositionSequence candidate(remaining.begin(),
                                       remaining.begin() + count);
            mapping = FindMapping(table, candidate);
            if (mapping != nullptr) {
                contiguous_count = count;
                break;
            }
        }
        if (mapping == nullptr) {
            if (!ImplicitElements(table, core, remaining.front(), elements)) {
                return false;
            }
            used_implicit = true;
            remaining.erase(remaining.begin());
            continue;
        }

        PositionSequence matched_sequence(
            remaining.begin(), remaining.begin() + contiguous_count);
        std::vector<std::size_t> matched_indices;
        matched_indices.reserve(table.summary.maximum_sequence_count);
        for (std::size_t index = 0u; index < contiguous_count; ++index) {
            matched_indices.push_back(index);
        }
        std::uint8_t blocking_class = 0u;
        for (std::size_t index = contiguous_count;
             index < remaining.size(); ++index) {
            std::uint8_t combining_class = 0u;
            if (!CanonicalCombiningClass(
                    core, remaining[index], combining_class)) {
                return false;
            }
            if (combining_class == 0u) {
#if defined(LAPLACE_TEST_DUCET_SKIP_IGNORABLE_STARTER_BLOCKER)
                continue;
#else
                break;
#endif
            }
            if (blocking_class >= combining_class) {
                break;
            }
            PositionSequence candidate = matched_sequence;
            candidate.push_back(remaining[index]);
            const Mapping* extended = FindMapping(table, candidate);
            if (extended != nullptr) {
                mapping = extended;
                matched_sequence = std::move(candidate);
                matched_indices.push_back(index);
            } else {
                blocking_class = std::max(blocking_class, combining_class);
            }
        }
        elements.insert(
            elements.end(), mapping->elements.begin(), mapping->elements.end());
        for (auto index = matched_indices.rbegin();
             index != matched_indices.rend(); ++index) {
            remaining.erase(remaining.begin() +
                            static_cast<std::ptrdiff_t>(*index));
        }
    }
    return true;
}

void AppendU16Be(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
    output.push_back(static_cast<std::uint8_t>(value));
}

bool AppendIdenticalPositions(const PositionSequence& normalized,
                              std::vector<std::uint8_t>& output) {
    for (const std::uint32_t position : normalized) {
        std::array<std::uint8_t, 4> encoded{};
        std::size_t length = 0u;
        if (laplace_unicode_position_encode(
                position, encoded.data(), &length) != LAPLACE_IDENTITY_OK ||
            length == 0u || length > encoded.size()) {
            return false;
        }
        output.push_back(static_cast<std::uint8_t>(length));
        output.insert(output.end(), encoded.begin(), encoded.begin() + length);
    }
    return true;
}

bool FormSortKey(
    const PositionSequence& normalized,
    const std::vector<laplace_unicode_collation_element>& source_elements,
    std::uint8_t alternate_handling, std::vector<std::uint8_t>& output) {
    if (alternate_handling != LAPLACE_UNICODE_UCA_NON_IGNORABLE &&
        alternate_handling != LAPLACE_UNICODE_UCA_SHIFTED) {
        return false;
    }
    struct WeightedElement {
        std::uint16_t weight[4]{};
    };
    std::vector<WeightedElement> weighted;
    weighted.reserve(source_elements.size());
    bool following_variable = false;
    for (const laplace_unicode_collation_element& source : source_elements) {
        WeightedElement value{{
            source.primary, source.secondary, source.tertiary, 0u}};
        if (alternate_handling == LAPLACE_UNICODE_UCA_SHIFTED) {
            if (source.variable != 0u && source.primary != 0u) {
                value.weight[0] = 0u;
                value.weight[1] = 0u;
                value.weight[2] = 0u;
                value.weight[3] = source.primary;
                following_variable = true;
            } else if (source.primary == 0u && following_variable) {
                value = WeightedElement{};
            } else {
                value.weight[3] =
                    source.primary != 0u || source.secondary != 0u ||
                            source.tertiary != 0u
                        ? UINT16_MAX
                        : 0u;
                if (source.primary != 0u) {
                    following_variable = false;
                }
            }
        }
        weighted.push_back(value);
    }
    output.clear();
    const std::size_t levels =
        alternate_handling == LAPLACE_UNICODE_UCA_SHIFTED ? 4u : 3u;
    for (std::size_t level = 0u; level < levels; ++level) {
        if (level != 0u) {
            AppendU16Be(output, 0u);
        }
        for (const WeightedElement& element : weighted) {
            if (element.weight[level] != 0u) {
                AppendU16Be(output, element.weight[level]);
            }
        }
    }
    AppendU16Be(output, 0u);
    return AppendIdenticalPositions(normalized, output);
}

bool CalculateCollation(
    const laplace_unicode_ducet_table& table,
    const laplace_unicode_core_table* core, const std::uint32_t* sequence,
    std::uint32_t sequence_count, std::uint8_t alternate_handling,
    CalculatedCollation& calculation) {
    if (!NormalizeNfd(core, sequence, sequence_count, calculation.normalized)) {
        return false;
    }
    bool used_implicit = false;
    if (!ProduceCollationElements(
            table, core, calculation.normalized, calculation.elements,
            used_implicit) ||
        !FormSortKey(
            calculation.normalized, calculation.elements,
            alternate_handling, calculation.key)) {
        return false;
    }
    calculation.provenance = used_implicit
        ? LAPLACE_UNICODE_DUCET_IMPLICIT
        : LAPLACE_UNICODE_DUCET_EXPLICIT;
    if (sequence_count == 1u && sequence[0] >= 0xac00u &&
        sequence[0] <= 0xd7a3u) {
        calculation.provenance = LAPLACE_UNICODE_DUCET_HANGUL;
    } else if (sequence_count == 1u && sequence[0] >= 0xd800u &&
               sequence[0] <= 0xdfffu) {
        calculation.provenance =
            LAPLACE_UNICODE_DUCET_LUP_SURROGATE_EXTENSION;
    }
    return calculation.normalized.size() <= UINT32_MAX &&
        calculation.elements.size() <= UINT32_MAX;
}

}  // namespace

extern "C" laplace_unicode_status laplace_unicode_ducet_table_create(
    const laplace_unicode_source_bundle* bundle,
    laplace_unicode_ducet_table** table,
    laplace_unicode_ducet_summary* summary) {
    if (bundle == nullptr || table == nullptr || summary == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *table = nullptr;
    *summary = laplace_unicode_ducet_summary{};
    laplace_unicode_source_receipt source{};
    if (laplace_unicode_source_bundle_receipt(bundle, &source) !=
        LAPLACE_UNICODE_OK) {
        summary->status = LAPLACE_UNICODE_INVALID_ARGUMENT;
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    auto* created = new (std::nothrow) laplace_unicode_ducet_table{};
    if (created == nullptr) {
        summary->status = LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
        return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
    }
    try {
        if (!ParseTable(bundle, *created) || !FinalizeTable(*created, source)) {
            delete created;
            summary->status = LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
            return LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
        }
    } catch (const std::bad_alloc&) {
        delete created;
        summary->status = LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
        return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
    } catch (...) {
        delete created;
        summary->status = LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
        return LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
    }
    *summary = created->summary;
    *table = created;
    return LAPLACE_UNICODE_OK;
}

extern "C" laplace_unicode_status laplace_unicode_ducet_table_mapping(
    const laplace_unicode_ducet_table* table, std::uint64_t mapping_ordinal,
    laplace_unicode_ducet_mapping_view* view) {
    if (table == nullptr || view == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *view = laplace_unicode_ducet_mapping_view{};
    if (mapping_ordinal >= table->mappings.size()) {
        return LAPLACE_UNICODE_POSITION_OUT_OF_RANGE;
    }
    *view = MappingView(table->mappings[static_cast<std::size_t>(mapping_ordinal)]);
    return LAPLACE_UNICODE_OK;
}

extern "C" laplace_unicode_status laplace_unicode_ducet_table_lookup(
    const laplace_unicode_ducet_table* table, const std::uint32_t* sequence,
    std::uint32_t sequence_count, laplace_unicode_ducet_mapping_view* view) {
    if (table == nullptr || sequence == nullptr || sequence_count == 0u ||
        view == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *view = laplace_unicode_ducet_mapping_view{};
    PositionSequence key(sequence, sequence + sequence_count);
    const auto found = table->mapping_index.find(key);
    if (found == table->mapping_index.end() ||
        !SameSequence(table->mappings[found->second].sequence,
                      sequence, sequence_count)) {
        return LAPLACE_UNICODE_SOURCE_INCOMPLETE;
    }
    *view = MappingView(table->mappings[found->second]);
    return LAPLACE_UNICODE_OK;
}

extern "C" laplace_unicode_status
laplace_unicode_ducet_table_implicit_range(
    const laplace_unicode_ducet_table* table, std::uint32_t range_ordinal,
    laplace_unicode_ducet_implicit_range_view* view) {
    if (table == nullptr || view == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *view = laplace_unicode_ducet_implicit_range_view{};
    if (range_ordinal >= table->implicit_ranges.size()) {
        return LAPLACE_UNICODE_POSITION_OUT_OF_RANGE;
    }
    const ImplicitRange& range = table->implicit_ranges[range_ordinal];
    *view = laplace_unicode_ducet_implicit_range_view{
        range.first, range.last, range.lead_primary, 0u,
        range.source_line_ordinal};
    return LAPLACE_UNICODE_OK;
}

extern "C" laplace_unicode_status laplace_unicode_ducet_sort_key_measure(
    const laplace_unicode_ducet_table* table,
    const laplace_unicode_core_table* core, const std::uint32_t* sequence,
    std::uint32_t sequence_count, std::uint8_t alternate_handling,
    std::uint32_t* normalized_position_count,
    std::uint32_t* collation_element_count, std::size_t* key_bytes) {
    if (table == nullptr || core == nullptr || sequence == nullptr ||
        sequence_count == 0u || normalized_position_count == nullptr ||
        collation_element_count == nullptr || key_bytes == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    CalculatedCollation calculation;
    if (!CalculateCollation(
            *table, core, sequence, sequence_count, alternate_handling,
            calculation)) {
        return alternate_handling > LAPLACE_UNICODE_UCA_SHIFTED
            ? LAPLACE_UNICODE_INVALID_ARGUMENT
            : LAPLACE_UNICODE_SOURCE_INCOMPLETE;
    }
    *normalized_position_count =
        static_cast<std::uint32_t>(calculation.normalized.size());
    *collation_element_count =
        static_cast<std::uint32_t>(calculation.elements.size());
    *key_bytes = calculation.key.size();
    return LAPLACE_UNICODE_OK;
}

extern "C" laplace_unicode_status laplace_unicode_ducet_sort_key_calculate(
    const laplace_unicode_ducet_table* table,
    const laplace_unicode_core_table* core, const std::uint32_t* sequence,
    std::uint32_t sequence_count, std::uint8_t alternate_handling,
    std::uint32_t* normalized_positions, std::uint32_t normalized_capacity,
    laplace_unicode_collation_element* elements, std::uint32_t element_capacity,
    std::uint8_t* key, std::size_t key_capacity, std::uint8_t* provenance,
    std::size_t* key_bytes) {
    if (table == nullptr || core == nullptr || sequence == nullptr ||
        sequence_count == 0u || normalized_positions == nullptr ||
        elements == nullptr || key == nullptr || provenance == nullptr ||
        key_bytes == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    CalculatedCollation calculation;
    if (!CalculateCollation(
            *table, core, sequence, sequence_count, alternate_handling,
            calculation)) {
        return alternate_handling > LAPLACE_UNICODE_UCA_SHIFTED
            ? LAPLACE_UNICODE_INVALID_ARGUMENT
            : LAPLACE_UNICODE_SOURCE_INCOMPLETE;
    }
    *key_bytes = calculation.key.size();
    if (normalized_capacity < calculation.normalized.size() ||
        element_capacity < calculation.elements.size() ||
        key_capacity < calculation.key.size()) {
        return LAPLACE_UNICODE_BUFFER_TOO_SMALL;
    }
    std::copy(calculation.normalized.begin(), calculation.normalized.end(),
              normalized_positions);
    std::copy(calculation.elements.begin(), calculation.elements.end(), elements);
    std::copy(calculation.key.begin(), calculation.key.end(), key);
    *provenance = calculation.provenance;
    return LAPLACE_UNICODE_OK;
}

extern "C" void laplace_unicode_ducet_table_destroy(
    laplace_unicode_ducet_table** table) {
    if (table != nullptr) {
        delete *table;
        *table = nullptr;
    }
}
