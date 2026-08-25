#include "laplace/unicode_root.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blake3.h"

namespace {

constexpr std::string_view NormalizedDomain{
    "laplace-unicode-core-normalized-v1"};
constexpr std::string_view ReceiptDomain{
    "laplace-unicode-core-receipt-v1"};
constexpr std::array<std::uint16_t, LAPLACE_UNICODE_CORE_FIELD_COUNT>
    CoreFieldIds{{1u, 2u, 3u, 6u, 7u, 8u, 9u}};
constexpr std::array<std::uint8_t, LAPLACE_UNICODE_CORE_FIELD_COUNT>
    CorePayloadKinds{{
        LAPLACE_UNICODE_PAYLOAD_ASCII_PROPERTY,
        LAPLACE_UNICODE_PAYLOAD_U8,
        LAPLACE_UNICODE_PAYLOAD_ASCII_PROPERTY,
        LAPLACE_UNICODE_PAYLOAD_POSITION_SEQUENCE,
        LAPLACE_UNICODE_PAYLOAD_TAGGED_POSITION_SEQUENCE,
        LAPLACE_UNICODE_PAYLOAD_ASCII_RATIONAL,
        LAPLACE_UNICODE_PAYLOAD_SORTED_TAGGED_POSITIONS}};

using Bytes = std::vector<std::uint8_t>;

struct CoreValue {
    std::array<Bytes, LAPLACE_UNICODE_CORE_FIELD_COUNT> payloads;
    std::string source_bidi;
};

struct CoreRange {
    std::uint32_t first;
    std::uint32_t last;
    CoreValue value;
};

struct BidiRange {
    std::uint32_t first;
    std::uint32_t last;
    std::string value;
};

class AliasTable {
public:
    bool Insert(
        std::string_view property,
        const std::vector<std::string_view>& aliases,
        std::string_view canonical) {
        if (canonical.empty()) {
            return false;
        }
        for (const std::string_view alias : aliases) {
            if (alias.empty()) {
                continue;
            }
            const std::string key = Key(property, alias);
            const auto [iterator, inserted] = values_.emplace(
                key, std::string(canonical));
            if (!inserted && iterator->second != canonical) {
                return false;
            }
        }
        return true;
    }

    bool Resolve(
        std::string_view property,
        std::string_view value,
        std::string& canonical) const {
        const auto iterator = values_.find(Key(property, value));
        if (iterator == values_.end()) {
            return false;
        }
        canonical = iterator->second;
        return true;
    }

private:
    static std::string Normalize(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (const unsigned char byte : value) {
            if (std::isalnum(byte) != 0) {
                result.push_back(static_cast<char>(std::tolower(byte)));
            }
        }
        return result;
    }

    static std::string Key(
        std::string_view property,
        std::string_view value) {
        std::string result = Normalize(property);
        result.push_back(':');
        result += Normalize(value);
        return result;
    }

    std::unordered_map<std::string, std::string> values_;
};

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

std::vector<std::string_view> Split(
    std::string_view value,
    char separator) {
    std::vector<std::string_view> result;
    std::size_t offset = 0u;
    for (;;) {
        const std::size_t next = value.find(separator, offset);
        result.push_back(value.substr(
            offset,
            next == std::string_view::npos
                ? value.size() - offset
                : next - offset));
        if (next == std::string_view::npos) {
            return result;
        }
        offset = next + 1u;
    }
}

template <typename Function>
bool ForEachLine(
    const laplace_unicode_source_file_view& file,
    Function&& function) {
    if (file.bytes == nullptr ||
        file.byte_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return false;
    }
    const std::string_view contents(
        reinterpret_cast<const char*>(file.bytes),
        static_cast<std::size_t>(file.byte_count));
    std::size_t offset = 0u;
    std::uint32_t line_number = 1u;
    while (offset < contents.size()) {
        const std::size_t next = contents.find('\n', offset);
        std::string_view line = contents.substr(
            offset,
            next == std::string_view::npos
                ? contents.size() - offset
                : next - offset);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1u);
        }
        if (!function(line, line_number)) {
            return false;
        }
        if (next == std::string_view::npos) {
            return true;
        }
        offset = next + 1u;
        if (line_number == std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        ++line_number;
    }
    return true;
}

bool ParseHex(std::string_view value, std::uint32_t& output) {
    value = Trim(value);
    if (value.empty() || value.size() > 6u) {
        return false;
    }
    std::uint32_t parsed = 0u;
    for (const char byte : value) {
        std::uint32_t digit = 0u;
        if (byte >= '0' && byte <= '9') {
            digit = static_cast<std::uint32_t>(byte - '0');
        } else if (byte >= 'A' && byte <= 'F') {
            digit = static_cast<std::uint32_t>(byte - 'A') + 10u;
        } else {
            return false;
        }
        parsed = parsed * 16u + digit;
    }
    if (parsed >= LAPLACE_UNICODE_ROOT_POPULATION) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParseRange(
    std::string_view value,
    std::uint32_t& first,
    std::uint32_t& last) {
    value = Trim(value);
    const std::size_t separator = value.find("..");
    if (separator == std::string_view::npos) {
        return ParseHex(value, first) && (last = first, true);
    }
    return value.find("..", separator + 2u) == std::string_view::npos &&
        ParseHex(value.substr(0u, separator), first) &&
        ParseHex(value.substr(separator + 2u), last) && first <= last;
}

bool ParseU8(std::string_view value, std::uint8_t& output) {
    value = Trim(value);
    if (value.empty()) {
        return false;
    }
    std::uint32_t parsed = 0u;
    for (const char byte : value) {
        if (byte < '0' || byte > '9') {
            return false;
        }
        parsed = parsed * 10u + static_cast<std::uint32_t>(byte - '0');
        if (parsed > 255u) {
            return false;
        }
    }
    output = static_cast<std::uint8_t>(parsed);
    return true;
}

void AppendU16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void AppendU32(Bytes& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
    output.push_back(static_cast<std::uint8_t>(value >> 16u));
    output.push_back(static_cast<std::uint8_t>(value >> 24u));
}

bool AppendPositions(Bytes& output, std::string_view value) {
    value = Trim(value);
    if (value.empty()) {
        return true;
    }
    for (const std::string_view part : Split(value, ' ')) {
        if (part.empty()) {
            continue;
        }
        std::uint32_t position = 0u;
        if (!ParseHex(part, position)) {
            return false;
        }
        AppendU32(output, position);
    }
    return true;
}

bool CanonicalRational(std::string_view value) {
    if (value.empty()) {
        return true;
    }
    std::size_t offset = value.front() == '-' ? 1u : 0u;
    const std::size_t numerator = offset;
    while (offset < value.size() && value[offset] >= '0' &&
           value[offset] <= '9') {
        ++offset;
    }
    if (offset == numerator ||
        (offset - numerator > 1u && value[numerator] == '0')) {
        return false;
    }
    if (offset == value.size()) {
        return true;
    }
    if (value[offset] != '/') {
        return false;
    }
    ++offset;
    const std::size_t denominator = offset;
    while (offset < value.size() && value[offset] >= '0' &&
           value[offset] <= '9') {
        ++offset;
    }
    return offset == value.size() && offset != denominator &&
        !(offset - denominator > 1u && value[denominator] == '0') &&
        !(offset - denominator == 1u && value[denominator] == '0');
}

void AssignAscii(Bytes& output, std::string_view value) {
    output.assign(value.begin(), value.end());
}

bool ParseAliases(
    const laplace_unicode_source_bundle* bundle,
    AliasTable& aliases) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/PropertyValueAliases.txt", &file) !=
        LAPLACE_UNICODE_OK) {
        return false;
    }
    return ForEachLine(file, [&](std::string_view line, std::uint32_t) {
        const std::size_t comment = line.find('#');
        line = Trim(line.substr(0u, comment));
        if (line.empty()) {
            return true;
        }
        auto fields = Split(line, ';');
        for (std::string_view& field : fields) {
            field = Trim(field);
        }
        if (fields.size() < 3u || fields[0].empty() || fields[1].empty() ||
            fields[2].empty()) {
            return false;
        }
        std::vector<std::string_view> value_aliases;
        value_aliases.reserve(fields.size() - 1u);
        for (std::size_t index = 1u; index < fields.size(); ++index) {
            if (!fields[index].empty()) {
                value_aliases.push_back(fields[index]);
            }
        }
        return aliases.Insert(fields[0], value_aliases, fields[2]);
    });
}

bool SameCoreValue(const CoreValue& left, const CoreValue& right) {
    return left.payloads == right.payloads && left.source_bidi == right.source_bidi;
}

bool MakeCoreValue(
    const std::vector<std::string_view>& fields,
    const AliasTable& aliases,
    CoreValue& output) {
    if (fields.size() != 15u) {
        return false;
    }
    std::string canonical;
    if (!aliases.Resolve("gc", Trim(fields[2]), canonical)) {
        return false;
    }
    AssignAscii(output.payloads[0], canonical);
    std::uint8_t combining_class = 0u;
    if (!ParseU8(fields[3], combining_class)) {
        return false;
    }
    output.payloads[1].assign(1u, combining_class);
    if (!aliases.Resolve("bc", Trim(fields[4]), output.source_bidi)) {
        return false;
    }

    const std::string_view decomposition = Trim(fields[5]);
    if (!decomposition.empty() && decomposition.front() == '<') {
        const std::size_t close = decomposition.find('>');
        if (close == std::string_view::npos || close == 1u) {
            return false;
        }
        std::string tag;
        if (!aliases.Resolve("dt", decomposition.substr(1u, close - 1u), tag) ||
            tag.size() > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        Bytes positions;
        if (!AppendPositions(positions, decomposition.substr(close + 1u)) ||
            positions.empty()) {
            return false;
        }
        Bytes& encoded = output.payloads[4];
        AppendU16(encoded, static_cast<std::uint16_t>(tag.size()));
        AppendU16(encoded, 0u);
        AppendU32(encoded, static_cast<std::uint32_t>(positions.size() / 4u));
        encoded.insert(encoded.end(), tag.begin(), tag.end());
        encoded.insert(encoded.end(), positions.begin(), positions.end());
    } else if (!AppendPositions(output.payloads[3], decomposition)) {
        return false;
    }

    const std::string_view numeric = Trim(fields[8]);
    if (!CanonicalRational(numeric)) {
        return false;
    }
    AssignAscii(output.payloads[5], numeric);

    struct Mapping {
        std::uint8_t tag;
        std::string_view value;
    };
    const std::array<Mapping, 3> mappings{{
        {1u, Trim(fields[13])},
        {2u, Trim(fields[14])},
        {3u, Trim(fields[12])}}};
    std::uint32_t count = 0u;
    for (const Mapping& mapping : mappings) {
        count += mapping.value.empty() ? 0u : 1u;
    }
    if (count != 0u) {
        Bytes& encoded = output.payloads[6];
        AppendU32(encoded, count);
        for (const Mapping& mapping : mappings) {
            if (mapping.value.empty()) {
                continue;
            }
            std::uint32_t position = 0u;
            if (!ParseHex(mapping.value, position)) {
                return false;
            }
            encoded.push_back(mapping.tag);
            encoded.insert(encoded.end(), 3u, 0u);
            AppendU32(encoded, position);
        }
    }
    return true;
}

std::string RangeName(std::string_view name, std::string_view suffix) {
    if (name.size() <= suffix.size() + 1u || name.front() != '<' ||
        !name.ends_with(suffix)) {
        return {};
    }
    return std::string(name.substr(1u, name.size() - suffix.size() - 1u));
}

bool AddCoreRange(
    std::vector<CoreRange>& ranges,
    std::uint32_t first,
    std::uint32_t last,
    CoreValue&& value) {
    if (first > last || (!ranges.empty() && first <= ranges.back().last)) {
        return false;
    }
    ranges.push_back(CoreRange{first, last, std::move(value)});
    return true;
}

bool ParseUnicodeData(
    const laplace_unicode_source_bundle* bundle,
    const AliasTable& aliases,
    std::vector<CoreRange>& ranges,
    laplace_unicode_core_summary& summary) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/UnicodeData.txt", &file) != LAPLACE_UNICODE_OK) {
        return false;
    }
    struct PendingRange {
        std::uint32_t first = 0u;
        std::string name;
        CoreValue value;
        bool active = false;
    } pending;
    return ForEachLine(file, [&](std::string_view line, std::uint32_t) {
        if (line.empty() || summary.unicode_data_row_count ==
                                std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        ++summary.unicode_data_row_count;
        auto fields = Split(line, ';');
        if (fields.size() != 15u) {
            return false;
        }
        std::uint32_t position = 0u;
        if (!ParseHex(fields[0], position)) {
            return false;
        }
        CoreValue value;
        if (!MakeCoreValue(fields, aliases, value)) {
            return false;
        }
        const std::string first_name = RangeName(fields[1], ", First>");
        const std::string last_name = RangeName(fields[1], ", Last>");
        if (!first_name.empty()) {
            if (pending.active) {
                return false;
            }
            pending.first = position;
            pending.name = first_name;
            pending.value = std::move(value);
            pending.active = true;
            return true;
        }
        if (!last_name.empty()) {
            if (!pending.active || pending.name != last_name ||
                position <= pending.first || !SameCoreValue(pending.value, value) ||
                !AddCoreRange(ranges, pending.first, position,
                              std::move(pending.value))) {
                return false;
            }
            pending = PendingRange{};
            return true;
        }
        return !pending.active &&
            AddCoreRange(ranges, position, position, std::move(value));
    }) && !pending.active;
}

bool PositionClass(
    std::uint32_t position,
    bool explicitly_assigned,
    std::uint8_t& position_class) {
    if (position >= LAPLACE_UNICODE_ROOT_POPULATION) {
        return false;
    }
    if (position >= 0xd800u && position <= 0xdfffu) {
        position_class = LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS;
    } else if ((position >= 0xe000u && position <= 0xf8ffu) ||
               (position >= 0xf0000u && position <= 0xffffdu) ||
               (position >= 0x100000u && position <= 0x10fffdu)) {
        position_class = LAPLACE_UNICODE_PRIVATE_USE_SCALAR;
    } else if ((position >= 0xfdd0u && position <= 0xfdefu) ||
               (position & 0xffffu) == 0xfffeu ||
               (position & 0xffffu) == 0xffffu) {
        position_class = LAPLACE_UNICODE_NONCHARACTER_SCALAR;
    } else {
        position_class = explicitly_assigned
            ? LAPLACE_UNICODE_ASSIGNED_SCALAR
            : LAPLACE_UNICODE_UNASSIGNED_OR_RESERVED_SCALAR;
    }
    return true;
}

bool ParseBidi(
    const laplace_unicode_source_bundle* bundle,
    const AliasTable& aliases,
    std::vector<BidiRange>& ranges,
    laplace_unicode_core_summary& summary) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/extracted/DerivedBidiClass.txt", &file) !=
        LAPLACE_UNICODE_OK) {
        return false;
    }
    std::vector<std::string> values;
    std::unordered_map<std::string, std::uint16_t> value_ids;
    const auto value_id = [&](const std::string& value, std::uint16_t& id) {
        const auto found = value_ids.find(value);
        if (found != value_ids.end()) {
            id = found->second;
            return true;
        }
        if (values.size() >= std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        id = static_cast<std::uint16_t>(values.size());
        values.push_back(value);
        value_ids.emplace(value, id);
        return true;
    };
    std::string default_value;
    if (!aliases.Resolve("bc", "L", default_value)) {
        return false;
    }
    std::uint16_t default_id = 0u;
    if (!value_id(default_value, default_id)) {
        return false;
    }
    std::vector<std::uint16_t> dense(
        LAPLACE_UNICODE_ROOT_POPULATION, default_id);
    std::vector<std::uint8_t> explicit_positions(
        LAPLACE_UNICODE_ROOT_POPULATION, 0u);
    bool has_explicit = false;
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t missing = line.find("# @missing:");
            if (missing != std::string_view::npos) {
                std::string_view rule = Trim(line.substr(missing + 11u));
                const auto fields = Split(rule, ';');
                if (fields.size() != 2u ||
                    summary.bidi_missing_rule_count ==
                        std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                std::uint32_t first = 0u;
                std::uint32_t last = 0u;
                std::string canonical;
                std::uint16_t id = 0u;
                if (!ParseRange(fields[0], first, last) ||
                    !aliases.Resolve("bc", Trim(fields[1]), canonical) ||
                    !value_id(canonical, id)) {
                    return false;
                }
                ++summary.bidi_missing_rule_count;
#if defined(LAPLACE_TEST_UNICODE_CORE_IGNORE_SPECIFIC_BIDI_DEFAULTS)
                if (first != 0u ||
                    last != LAPLACE_UNICODE_ROOT_POPULATION - 1u) {
                    return true;
                }
#endif
                std::fill(dense.begin() + first, dense.begin() + last + 1u, id);
                return true;
            }
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            const auto fields = Split(line, ';');
            std::uint32_t first = 0u;
            std::uint32_t last = 0u;
            std::string canonical;
            std::uint16_t id = 0u;
            if (fields.size() != 2u || !ParseRange(fields[0], first, last) ||
                !aliases.Resolve("bc", Trim(fields[1]), canonical) ||
                !value_id(canonical, id)) {
                return false;
            }
            for (std::uint32_t position = first; position <= last; ++position) {
                if (explicit_positions[position] != 0u) {
                    return false;
                }
                explicit_positions[position] = 1u;
            }
            std::fill(dense.begin() + first, dense.begin() + last + 1u, id);
            has_explicit = true;
            return true;
        });
    if (!parsed || summary.bidi_missing_rule_count == 0u || !has_explicit) {
        return false;
    }
    std::uint32_t first = 0u;
    for (std::uint32_t position = 1u;
         position < LAPLACE_UNICODE_ROOT_POPULATION;
         ++position) {
        if (dense[position] != dense[first]) {
            ranges.push_back(BidiRange{
                first, position - 1u, values[dense[first]]});
            first = position;
        }
    }
    ranges.push_back(BidiRange{
        first, LAPLACE_UNICODE_ROOT_POPULATION - 1u, values[dense[first]]});
    if (ranges.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    summary.bidi_range_count = static_cast<std::uint32_t>(ranges.size());
    return true;
}

const CoreRange* FindCoreRange(
    const std::vector<CoreRange>& ranges,
    std::uint32_t position) {
    const auto iterator = std::upper_bound(
        ranges.begin(), ranges.end(), position,
        [](std::uint32_t value, const CoreRange& range) {
            return value < range.first;
        });
    if (iterator == ranges.begin()) {
        return nullptr;
    }
    const CoreRange& candidate = *(iterator - 1);
    return position <= candidate.last ? &candidate : nullptr;
}

const BidiRange* FindBidiRange(
    const std::vector<BidiRange>& ranges,
    std::uint32_t position) {
    const auto iterator = std::upper_bound(
        ranges.begin(), ranges.end(), position,
        [](std::uint32_t value, const BidiRange& range) {
            return value < range.first;
        });
    if (iterator == ranges.begin()) {
        return nullptr;
    }
    const BidiRange& candidate = *(iterator - 1);
    return position <= candidate.last ? &candidate : nullptr;
}

void HashU32(blake3_hasher& hasher, std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashBytes(blake3_hasher& hasher, const std::uint8_t* bytes, std::size_t count) {
    HashU64(hasher, count);
    if (count != 0u) {
        blake3_hasher_update(&hasher, bytes, count);
    }
}

void HashString(blake3_hasher& hasher, std::string_view value) {
    HashBytes(
        hasher,
        reinterpret_cast<const std::uint8_t*>(value.data()),
        value.size());
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool FinalizeSummary(
    const std::vector<CoreRange>& core_ranges,
    const std::vector<BidiRange>& bidi_ranges,
    laplace_unicode_core_summary& summary) {
    if (core_ranges.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    summary.unicode_data_range_count =
        static_cast<std::uint32_t>(core_ranges.size());
    std::size_t core_index = 0u;
    std::size_t bidi_index = 0u;
    for (std::uint32_t position = 0u;
         position < LAPLACE_UNICODE_ROOT_POPULATION;
         ++position) {
        while (core_index < core_ranges.size() &&
               position > core_ranges[core_index].last) {
            ++core_index;
        }
        while (bidi_index < bidi_ranges.size() &&
               position > bidi_ranges[bidi_index].last) {
            ++bidi_index;
        }
        if (bidi_index == bidi_ranges.size() ||
            position < bidi_ranges[bidi_index].first) {
            return false;
        }
        const bool explicit_position =
            core_index < core_ranges.size() &&
            position >= core_ranges[core_index].first &&
            position <= core_ranges[core_index].last;
        if (explicit_position) {
            const CoreValue& value = core_ranges[core_index].value;
            if (value.source_bidi != bidi_ranges[bidi_index].value) {
                return false;
            }
            ++summary.explicit_position_count;
            summary.canonical_decomposition_count +=
                value.payloads[3].empty() ? 0u : 1u;
            summary.compatibility_decomposition_count +=
                value.payloads[4].empty() ? 0u : 1u;
            summary.simple_case_mapping_position_count +=
                value.payloads[6].empty() ? 0u : 1u;
        }
        std::uint8_t position_class = 0u;
        if (!PositionClass(position, explicit_position, position_class) ||
            position_class >= 5u) {
            return false;
        }
        ++summary.position_class_counts[position_class];
    }

    blake3_hasher normalized{};
    blake3_hasher_init(&normalized);
    HashString(normalized, NormalizedDomain);
    blake3_hasher_update(
        &normalized, summary.source_fingerprint.bytes,
        sizeof(summary.source_fingerprint.bytes));
    blake3_hasher_update(
        &normalized, summary.recipe_fingerprint.bytes,
        sizeof(summary.recipe_fingerprint.bytes));
    HashU64(normalized, core_ranges.size());
    for (const CoreRange& range : core_ranges) {
        HashU32(normalized, range.first);
        HashU32(normalized, range.last);
        for (const Bytes& payload : range.value.payloads) {
            HashBytes(normalized, payload.data(), payload.size());
        }
        HashString(normalized, range.value.source_bidi);
    }
    HashU64(normalized, bidi_ranges.size());
    for (const BidiRange& range : bidi_ranges) {
        HashU32(normalized, range.first);
        HashU32(normalized, range.last);
        HashString(normalized, range.value);
    }
    summary.normalized_fingerprint = Finish(normalized);

    blake3_hasher receipt{};
    blake3_hasher_init(&receipt);
    HashString(receipt, ReceiptDomain);
    blake3_hasher_update(
        &receipt, summary.normalized_fingerprint.bytes,
        sizeof(summary.normalized_fingerprint.bytes));
    HashU64(receipt, summary.explicit_position_count);
    HashU64(receipt, summary.canonical_decomposition_count);
    HashU64(receipt, summary.compatibility_decomposition_count);
    HashU64(receipt, summary.simple_case_mapping_position_count);
    for (const std::uint64_t count : summary.position_class_counts) {
        HashU64(receipt, count);
    }
    HashU32(receipt, summary.unicode_data_row_count);
    HashU32(receipt, summary.unicode_data_range_count);
    HashU32(receipt, summary.bidi_range_count);
    HashU32(receipt, summary.bidi_missing_rule_count);
    summary.receipt_id = Finish(receipt);
    summary.status = LAPLACE_UNICODE_OK;
    return true;
}

CoreValue DefaultCoreValue(const AliasTable& aliases) {
    CoreValue result;
    std::string general_category;
    if (!aliases.Resolve("gc", "Cn", general_category)) {
        return result;
    }
    AssignAscii(result.payloads[0], general_category);
    result.payloads[1].assign(1u, 0u);
    return result;
}

}  // namespace

struct laplace_unicode_core_table {
    std::vector<CoreRange> core_ranges;
    std::vector<BidiRange> bidi_ranges;
    CoreValue default_value;
    laplace_unicode_core_summary summary{};
};

extern "C" laplace_unicode_status laplace_unicode_core_table_create(
    const laplace_unicode_source_bundle* bundle,
    laplace_unicode_core_table** table,
    laplace_unicode_core_summary* summary) {
    if (bundle == nullptr || table == nullptr || summary == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *table = nullptr;
    *summary = laplace_unicode_core_summary{};
    laplace_unicode_source_receipt source{};
    if (laplace_unicode_source_bundle_receipt(bundle, &source) !=
        LAPLACE_UNICODE_OK) {
        summary->status = LAPLACE_UNICODE_INVALID_ARGUMENT;
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    auto* created = new (std::nothrow) laplace_unicode_core_table{};
    if (created == nullptr) {
        summary->status = LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
        return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
    }
    try {
        created->summary.source_fingerprint = source.source_fingerprint;
        created->summary.recipe_fingerprint = source.recipe_fingerprint;
        AliasTable aliases;
        if (!ParseAliases(bundle, aliases)) {
            delete created;
            summary->status = LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
            return LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
        }
        created->default_value = DefaultCoreValue(aliases);
        if (created->default_value.payloads[0].empty() ||
            !ParseUnicodeData(
                bundle, aliases, created->core_ranges, created->summary) ||
            !ParseBidi(
                bundle, aliases, created->bidi_ranges, created->summary)) {
            delete created;
            summary->status = LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
            return LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID;
        }
        if (!FinalizeSummary(
                created->core_ranges, created->bidi_ranges,
                created->summary)) {
            delete created;
            summary->status = LAPLACE_UNICODE_SOURCE_CONFLICT;
            return LAPLACE_UNICODE_SOURCE_CONFLICT;
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

extern "C" laplace_unicode_status laplace_unicode_core_table_record(
    const laplace_unicode_core_table* table,
    std::uint32_t codepoint_position,
    laplace_unicode_core_record_view* view) {
    if (table == nullptr || view == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *view = laplace_unicode_core_record_view{};
    if (codepoint_position >= LAPLACE_UNICODE_ROOT_POPULATION) {
        return LAPLACE_UNICODE_POSITION_OUT_OF_RANGE;
    }
    const CoreRange* core = FindCoreRange(
        table->core_ranges, codepoint_position);
    const BidiRange* bidi = FindBidiRange(
        table->bidi_ranges, codepoint_position);
    if (bidi == nullptr) {
        return LAPLACE_UNICODE_SOURCE_INCOMPLETE;
    }
    const CoreValue& value = core != nullptr ? core->value : table->default_value;
    if (!PositionClass(codepoint_position, core != nullptr,
                       view->position_class)) {
        return LAPLACE_UNICODE_POSITION_OUT_OF_RANGE;
    }
    view->codepoint_position = codepoint_position;
    for (std::size_t index = 0u; index < CoreFieldIds.size(); ++index) {
        laplace_unicode_atom_field& field = view->fields[index];
        field.field_id = CoreFieldIds[index];
        field.payload_kind = CorePayloadKinds[index];
        if (index == 2u) {
            field.payload = reinterpret_cast<const std::uint8_t*>(
                bidi->value.data());
            field.payload_bytes = static_cast<std::uint32_t>(bidi->value.size());
        } else {
            field.payload = value.payloads[index].data();
            field.payload_bytes =
                static_cast<std::uint32_t>(value.payloads[index].size());
        }
    }
    return LAPLACE_UNICODE_OK;
}

extern "C" void laplace_unicode_core_table_destroy(
    laplace_unicode_core_table** table) {
    if (table != nullptr) {
        delete *table;
        *table = nullptr;
    }
}
