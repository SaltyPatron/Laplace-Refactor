#include "laplace/unicode_root.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
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
constexpr std::string_view CompletePropertyDomain{
    "laplace-unicode-complete-property-table-v1"};
constexpr std::size_t CoreSourceFieldCount = 7u;
constexpr std::array<std::uint16_t, CoreSourceFieldCount>
    CoreFieldIds{{1u, 2u, 3u, 6u, 7u, 8u, 9u}};

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

struct NamedRange {
    std::uint32_t first;
    std::uint32_t last;
    std::string value;
};

struct SingleProperty {
    std::vector<NamedRange> ranges;
    std::string default_value;
    std::uint64_t explicit_positions = 0u;
    std::uint64_t source_rows = 0u;
};

struct PositionPayload {
    std::uint32_t position;
    Bytes payload;
};

struct SetPropertyPlane {
    std::vector<std::string> properties;
    std::vector<std::uint64_t> masks;
    std::map<std::uint64_t, Bytes> encoded_by_mask;
    std::uint64_t source_rows = 0u;
    std::uint64_t explicit_positions = 0u;
    std::uint64_t memberships = 0u;
};

struct SupplementalProperties {
    std::vector<PositionPayload> bidi_brackets;
    std::vector<PositionPayload> bidi_mirroring;
    std::vector<PositionPayload> full_case_mappings;
    std::vector<PositionPayload> case_folding;
    std::array<SingleProperty, LAPLACE_UNICODE_ATOM_FIELD_COUNT> single;
    std::vector<PositionPayload> script_extensions;
    std::map<std::string, Bytes> script_singletons;
    SetPropertyPlane prop_list;
    SetPropertyPlane derived_core;
    std::vector<PositionPayload> normalization;
    std::vector<NamedRange> full_composition_exclusion;
    std::vector<NamedRange> extended_pictographic;
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

std::string LooseKey(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char byte : value) {
        if (std::isalnum(byte) != 0) {
            result.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    return result;
}

class PropertyAliasTable {
public:
    bool Insert(
        const std::vector<std::string_view>& aliases,
        std::string_view canonical) {
        if (canonical.empty()) {
            return false;
        }
        for (const std::string_view alias : aliases) {
            if (alias.empty()) {
                continue;
            }
            const auto [iterator, inserted] = values_.emplace(
                LooseKey(alias), std::string(canonical));
            if (!inserted && iterator->second != canonical) {
                return false;
            }
        }
        return true;
    }

    bool Resolve(std::string_view alias, std::string& canonical) const {
        const auto iterator = values_.find(LooseKey(alias));
        if (iterator == values_.end()) {
            return false;
        }
        canonical = iterator->second;
        return true;
    }

private:
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

std::uint32_t ReadU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[3]) << 24u);
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

bool ParsePropertyAliases(
    const laplace_unicode_source_bundle* bundle,
    PropertyAliasTable& aliases) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/PropertyAliases.txt", &file) !=
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
        if (fields.size() < 2u || fields[0].empty() || fields[1].empty()) {
            return false;
        }
        return aliases.Insert(fields, fields[1]);
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
    std::uint64_t source_rows = 0u;
    std::uint64_t explicit_position_count = 0u;
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
                ++explicit_position_count;
            }
            std::fill(dense.begin() + first, dense.begin() + last + 1u, id);
            ++source_rows;
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
    summary.field_source_row_counts[2u] = source_rows;
    summary.field_explicit_position_counts[2u] = explicit_position_count;
    summary.field_membership_counts[2u] = explicit_position_count;
    return true;
}

const NamedRange* FindNamedRange(
    const std::vector<NamedRange>& ranges,
    std::uint32_t position) {
    const auto iterator = std::upper_bound(
        ranges.begin(), ranges.end(), position,
        [](std::uint32_t value, const NamedRange& range) {
            return value < range.first;
        });
    if (iterator == ranges.begin()) {
        return nullptr;
    }
    const NamedRange& candidate = *(iterator - 1);
    return position <= candidate.last ? &candidate : nullptr;
}

const PositionPayload* FindPositionPayload(
    const std::vector<PositionPayload>& values,
    std::uint32_t position) {
    const auto iterator = std::lower_bound(
        values.begin(), values.end(), position,
        [](const PositionPayload& value, std::uint32_t sought) {
            return value.position < sought;
        });
    return iterator != values.end() && iterator->position == position
        ? &*iterator
        : nullptr;
}

bool FinalizeNamedRanges(SingleProperty& property) {
    std::sort(
        property.ranges.begin(), property.ranges.end(),
        [](const NamedRange& left, const NamedRange& right) {
            return left.first < right.first ||
                (left.first == right.first && left.last < right.last);
        });
    property.explicit_positions = 0u;
    for (std::size_t index = 0u; index < property.ranges.size(); ++index) {
        const NamedRange& range = property.ranges[index];
        if (range.first > range.last ||
            (index != 0u &&
             range.first <= property.ranges[index - 1u].last)) {
            return false;
        }
        property.explicit_positions +=
            static_cast<std::uint64_t>(range.last) - range.first + 1u;
    }
    return !property.ranges.empty() && !property.default_value.empty();
}

bool ParseSingleProperty(
    const laplace_unicode_source_bundle* bundle,
    const AliasTable& value_aliases,
    const char* relative_path,
    std::string_view value_namespace,
    std::string_view default_alias,
    std::string_view required_property,
    SingleProperty& property) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, relative_path, &file) != LAPLACE_UNICODE_OK ||
        !value_aliases.Resolve(
            value_namespace, default_alias, property.default_value)) {
        return false;
    }
    std::uint32_t missing_rules = 0u;
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t missing = line.find("# @missing:");
            if (missing != std::string_view::npos) {
                auto fields = Split(Trim(line.substr(missing + 11u)), ';');
                for (std::string_view& field : fields) {
                    field = Trim(field);
                }
                if (!required_property.empty() && fields.size() >= 2u &&
                    LooseKey(fields[1]) != LooseKey(required_property)) {
                    return true;
                }
                const std::size_t value_index =
                    required_property.empty() ? 1u : 2u;
                std::uint32_t first = 0u;
                std::uint32_t last = 0u;
                std::string canonical;
                if (fields.size() != value_index + 1u ||
                    !ParseRange(fields[0], first, last) || first != 0u ||
                    last != LAPLACE_UNICODE_ROOT_POPULATION - 1u ||
                    (!required_property.empty() &&
                     LooseKey(fields[1]) != LooseKey(required_property)) ||
                    !value_aliases.Resolve(
                        value_namespace, fields[value_index], canonical) ||
                    canonical != property.default_value ||
                    missing_rules == std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                ++missing_rules;
                return true;
            }
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            if (!required_property.empty() && fields.size() >= 2u &&
                LooseKey(fields[1]) != LooseKey(required_property)) {
                return true;
            }
            const std::size_t value_index =
                required_property.empty() ? 1u : 2u;
            std::uint32_t first = 0u;
            std::uint32_t last = 0u;
            std::string canonical;
            if (fields.size() != value_index + 1u ||
                (!required_property.empty() &&
                 LooseKey(fields[1]) != LooseKey(required_property)) ||
                !ParseRange(fields[0], first, last) ||
                !value_aliases.Resolve(
                    value_namespace, fields[value_index], canonical) ||
                property.source_rows ==
                    std::numeric_limits<std::uint64_t>::max()) {
                return false;
            }
            ++property.source_rows;
            property.ranges.push_back(
                NamedRange{first, last, std::move(canonical)});
            return true;
        });
    return parsed && missing_rules == 1u && FinalizeNamedRanges(property);
}

const std::string& PropertyAt(
    const SingleProperty& property,
    std::uint32_t position) {
    const NamedRange* range = FindNamedRange(property.ranges, position);
    return range != nullptr ? range->value : property.default_value;
}

bool EncodeAsciiSet(
    const std::vector<std::string>& values,
    Bytes& payload) {
    if (values.empty()) {
        payload.clear();
        return true;
    }
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    payload.clear();
    AppendU32(payload, static_cast<std::uint32_t>(values.size()));
    std::string prior;
    for (const std::string& value : values) {
        if (value.empty() || value.size() >
                std::numeric_limits<std::uint16_t>::max() ||
            (!prior.empty() && prior >= value)) {
            return false;
        }
        AppendU16(payload, static_cast<std::uint16_t>(value.size()));
        payload.insert(payload.end(), value.begin(), value.end());
        prior = value;
    }
    return true;
}

bool FinalizePositionPayloads(std::vector<PositionPayload>& values) {
    std::sort(
        values.begin(), values.end(),
        [](const PositionPayload& left, const PositionPayload& right) {
            return left.position < right.position;
        });
    for (std::size_t index = 1u; index < values.size(); ++index) {
        if (values[index - 1u].position == values[index].position) {
            return false;
        }
    }
    return true;
}

bool ParseBidiBrackets(
    const laplace_unicode_source_bundle* bundle,
    const AliasTable& value_aliases,
    std::vector<PositionPayload>& values) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/BidiBrackets.txt", &file) != LAPLACE_UNICODE_OK) {
        return false;
    }
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            std::uint32_t position = 0u;
            std::uint32_t paired = 0u;
            std::string canonical;
            if (fields.size() != 3u || !ParseHex(fields[0], position) ||
                !ParseHex(fields[1], paired) || fields[2].size() != 1u ||
                (fields[2][0] != 'o' && fields[2][0] != 'c') ||
                !value_aliases.Resolve("bpt", fields[2], canonical) ||
                (canonical != "Open" && canonical != "Close")) {
                return false;
            }
            Bytes payload;
            AppendU32(payload, paired);
            payload.push_back(static_cast<std::uint8_t>(fields[2][0]));
            values.push_back(PositionPayload{position, std::move(payload)});
            return true;
        });
    return parsed && !values.empty() && FinalizePositionPayloads(values);
}

bool ParseBidiMirroring(
    const laplace_unicode_source_bundle* bundle,
    std::vector<PositionPayload>& values) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/BidiMirroring.txt", &file) != LAPLACE_UNICODE_OK) {
        return false;
    }
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            std::uint32_t position = 0u;
            std::uint32_t mirrored = 0u;
            if (fields.size() != 2u || !ParseHex(fields[0], position) ||
                !ParseHex(fields[1], mirrored)) {
                return false;
            }
            Bytes payload;
            AppendU32(payload, mirrored);
            values.push_back(PositionPayload{position, std::move(payload)});
            return true;
        });
    return parsed && !values.empty() && FinalizePositionPayloads(values);
}

bool ParseSpecialCasing(
    const laplace_unicode_source_bundle* bundle,
    std::vector<PositionPayload>& values,
    std::uint64_t& source_rows) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/SpecialCasing.txt", &file) != LAPLACE_UNICODE_OK) {
        return false;
    }
    std::map<std::uint32_t, std::vector<Bytes>> entries;
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            if (fields.size() == 6u && fields.back().empty()) {
                fields.pop_back();
            }
            if (fields.size() != 5u) {
                return false;
            }
            std::uint32_t position = 0u;
            Bytes lower;
            Bytes title;
            Bytes upper;
            if (!ParseHex(fields[0], position) ||
                !AppendPositions(lower, fields[1]) ||
                !AppendPositions(title, fields[2]) ||
                !AppendPositions(upper, fields[3])) {
                return false;
            }
            std::vector<std::string> conditions;
            for (std::string_view condition : Split(fields[4], ' ')) {
                condition = Trim(condition);
                if (condition.empty()) {
                    continue;
                }
                for (const unsigned char byte : condition) {
                    if (std::isalnum(byte) == 0 && byte != '_' && byte != '-') {
                        return false;
                    }
                }
                conditions.emplace_back(condition);
            }
            std::sort(conditions.begin(), conditions.end());
            if (std::adjacent_find(conditions.begin(), conditions.end()) !=
                conditions.end() || conditions.size() >
                    std::numeric_limits<std::uint16_t>::max()) {
                return false;
            }
            Bytes entry;
            AppendU16(entry, static_cast<std::uint16_t>(conditions.size()));
            AppendU16(entry, 0u);
            AppendU32(entry, static_cast<std::uint32_t>(lower.size() / 4u));
            AppendU32(entry, static_cast<std::uint32_t>(title.size() / 4u));
            AppendU32(entry, static_cast<std::uint32_t>(upper.size() / 4u));
            entry.insert(entry.end(), lower.begin(), lower.end());
            entry.insert(entry.end(), title.begin(), title.end());
            entry.insert(entry.end(), upper.begin(), upper.end());
            for (const std::string& condition : conditions) {
                if (condition.size() >
                    std::numeric_limits<std::uint16_t>::max()) {
                    return false;
                }
                AppendU16(entry, static_cast<std::uint16_t>(condition.size()));
                entry.insert(entry.end(), condition.begin(), condition.end());
            }
            entries[position].push_back(std::move(entry));
            ++source_rows;
            return true;
        });
    if (!parsed || entries.empty()) {
        return false;
    }
    for (auto& [position, position_entries] : entries) {
        std::sort(position_entries.begin(), position_entries.end());
        if (std::adjacent_find(
                position_entries.begin(), position_entries.end()) !=
            position_entries.end()) {
            return false;
        }
        Bytes payload;
        AppendU32(
            payload, static_cast<std::uint32_t>(position_entries.size()));
        for (const Bytes& entry : position_entries) {
            payload.insert(payload.end(), entry.begin(), entry.end());
        }
        values.push_back(PositionPayload{position, std::move(payload)});
    }
    return FinalizePositionPayloads(values);
}

bool ParseCaseFolding(
    const laplace_unicode_source_bundle* bundle,
    std::vector<PositionPayload>& values,
    std::uint64_t& source_rows) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/CaseFolding.txt", &file) != LAPLACE_UNICODE_OK) {
        return false;
    }
    std::map<std::uint32_t, std::vector<Bytes>> entries;
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            if (fields.size() == 4u && fields.back().empty()) {
                fields.pop_back();
            }
            std::uint32_t position = 0u;
            Bytes mapping;
            if (fields.size() != 3u || !ParseHex(fields[0], position) ||
                fields[1].size() != 1u ||
                (fields[1][0] != 'C' && fields[1][0] != 'F' &&
                 fields[1][0] != 'S' && fields[1][0] != 'T') ||
                !AppendPositions(mapping, fields[2]) || mapping.empty()) {
                return false;
            }
            Bytes entry;
            entry.push_back(static_cast<std::uint8_t>(fields[1][0]));
            entry.insert(entry.end(), 3u, 0u);
            AppendU32(entry, static_cast<std::uint32_t>(mapping.size() / 4u));
            entry.insert(entry.end(), mapping.begin(), mapping.end());
            entries[position].push_back(std::move(entry));
            ++source_rows;
            return true;
        });
    if (!parsed || entries.empty()) {
        return false;
    }
    for (auto& [position, position_entries] : entries) {
        std::sort(
            position_entries.begin(), position_entries.end(),
            [](const Bytes& left, const Bytes& right) {
                return left.front() < right.front();
            });
        for (std::size_t index = 1u; index < position_entries.size(); ++index) {
            if (position_entries[index - 1u].front() ==
                position_entries[index].front()) {
                return false;
            }
        }
        Bytes payload;
        AppendU32(
            payload, static_cast<std::uint32_t>(position_entries.size()));
        for (const Bytes& entry : position_entries) {
            payload.insert(payload.end(), entry.begin(), entry.end());
        }
        values.push_back(PositionPayload{position, std::move(payload)});
    }
    return FinalizePositionPayloads(values);
}

struct MembershipRow {
    std::uint32_t first;
    std::uint32_t last;
    std::string property;
};

std::uint32_t CountBits(std::uint64_t value) {
    std::uint32_t count = 0u;
    while (value != 0u) {
        value &= value - 1u;
        ++count;
    }
    return count;
}

bool ParseSetPropertyPlane(
    const laplace_unicode_source_bundle* bundle,
    const PropertyAliasTable& property_aliases,
    const char* relative_path,
    bool exclude_incb,
    SetPropertyPlane& plane) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, relative_path, &file) != LAPLACE_UNICODE_OK) {
        return false;
    }
    std::vector<MembershipRow> rows;
    std::set<std::string> names;
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            if (fields.size() != 2u) {
                return exclude_incb && fields.size() == 3u &&
                    LooseKey(fields[1]) == LooseKey("InCB");
            }
            if (exclude_incb && LooseKey(fields[1]) == LooseKey("InCB")) {
                return true;
            }
            std::uint32_t first = 0u;
            std::uint32_t last = 0u;
            std::string canonical;
            if (!ParseRange(fields[0], first, last) ||
                !property_aliases.Resolve(fields[1], canonical)) {
                return false;
            }
            names.insert(canonical);
            rows.push_back(MembershipRow{first, last, std::move(canonical)});
            ++plane.source_rows;
            return true;
        });
    if (!parsed || rows.empty() || names.empty() || names.size() > 64u) {
        return false;
    }
    plane.properties.assign(names.begin(), names.end());
    std::unordered_map<std::string, std::uint32_t> ids;
    for (std::size_t index = 0u; index < plane.properties.size(); ++index) {
        ids.emplace(
            plane.properties[index], static_cast<std::uint32_t>(index));
    }
    plane.masks.assign(LAPLACE_UNICODE_ROOT_POPULATION, 0u);
    for (const MembershipRow& row : rows) {
        const auto found = ids.find(row.property);
        if (found == ids.end()) {
            return false;
        }
        const std::uint64_t bit = UINT64_C(1) << found->second;
        for (std::uint32_t position = row.first;
             position <= row.last; ++position) {
            if ((plane.masks[position] & bit) != 0u) {
                return false;
            }
            plane.masks[position] |= bit;
            ++plane.memberships;
        }
    }
    for (const std::uint64_t mask : plane.masks) {
        plane.explicit_positions += mask == 0u ? 0u : 1u;
        auto [iterator, inserted] = plane.encoded_by_mask.try_emplace(mask);
        if (inserted && mask != 0u) {
            std::vector<std::string> selected;
            for (std::size_t index = 0u;
                 index < plane.properties.size(); ++index) {
                if ((mask & (UINT64_C(1) << index)) != 0u) {
                    selected.push_back(plane.properties[index]);
                }
            }
            if (static_cast<std::size_t>(CountBits(mask)) != selected.size() ||
                !EncodeAsciiSet(selected, iterator->second)) {
                return false;
            }
        }
    }
    return true;
}

bool ParseScriptExtensions(
    const laplace_unicode_source_bundle* bundle,
    const AliasTable& value_aliases,
    std::vector<PositionPayload>& values,
    std::uint64_t& source_rows,
    std::uint64_t& memberships) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/ScriptExtensions.txt", &file) !=
        LAPLACE_UNICODE_OK) {
        return false;
    }
    std::uint32_t missing_rules = 0u;
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t missing = line.find("# @missing:");
            if (missing != std::string_view::npos) {
                const auto fields = Split(
                    Trim(line.substr(missing + 11u)), ';');
                std::uint32_t first = 0u;
                std::uint32_t last = 0u;
                if (fields.size() != 2u ||
                    !ParseRange(fields[0], first, last) || first != 0u ||
                    last != LAPLACE_UNICODE_ROOT_POPULATION - 1u ||
                    Trim(fields[1]) != "<script>") {
                    return false;
                }
                ++missing_rules;
                return true;
            }
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            std::uint32_t first = 0u;
            std::uint32_t last = 0u;
            if (fields.size() != 2u ||
                !ParseRange(fields[0], first, last)) {
                return false;
            }
            std::vector<std::string> scripts;
            for (std::string_view alias : Split(fields[1], ' ')) {
                alias = Trim(alias);
                if (alias.empty()) {
                    continue;
                }
                std::string canonical;
                if (!value_aliases.Resolve("sc", alias, canonical)) {
                    return false;
                }
                scripts.push_back(std::move(canonical));
            }
            std::sort(scripts.begin(), scripts.end());
            scripts.erase(
                std::unique(scripts.begin(), scripts.end()), scripts.end());
            Bytes payload;
            if (!EncodeAsciiSet(scripts, payload)) {
                return false;
            }
            for (std::uint32_t position = first;
                 position <= last; ++position) {
                values.push_back(PositionPayload{position, payload});
                memberships += scripts.size();
            }
            ++source_rows;
            return true;
        });
    return parsed && missing_rules == 1u && !values.empty() &&
        FinalizePositionPayloads(values);
}

bool ParseBooleanProperty(
    const laplace_unicode_source_bundle* bundle,
    const char* relative_path,
    std::string_view selected_property,
    std::vector<NamedRange>& ranges,
    std::uint64_t& source_rows,
    std::uint64_t& explicit_positions) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, relative_path, &file) != LAPLACE_UNICODE_OK) {
        return false;
    }
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            if (fields.size() < 2u ||
                LooseKey(fields[1]) != LooseKey(selected_property)) {
                return true;
            }
            std::uint32_t first = 0u;
            std::uint32_t last = 0u;
            if (!ParseRange(fields[0], first, last)) {
                return false;
            }
            ranges.push_back(NamedRange{first, last, "1"});
            ++source_rows;
            return true;
        });
    if (!parsed || ranges.empty()) {
        return false;
    }
    std::sort(
        ranges.begin(), ranges.end(),
        [](const NamedRange& left, const NamedRange& right) {
            return left.first < right.first ||
                (left.first == right.first && left.last < right.last);
        });
    for (std::size_t index = 0u; index < ranges.size(); ++index) {
        if (index != 0u && ranges[index].first <= ranges[index - 1u].last) {
            return false;
        }
        explicit_positions +=
            static_cast<std::uint64_t>(ranges[index].last) -
            ranges[index].first + 1u;
    }
    return true;
}

struct NormalizationEntry {
    std::string key;
    std::uint8_t value_kind;
    std::uint32_t value_count;
    Bytes value;
};

bool EncodeNormalizationEntries(
    const std::vector<NormalizationEntry>& entries,
    Bytes& payload) {
    if (entries.empty() ||
        entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    payload.clear();
    AppendU32(payload, static_cast<std::uint32_t>(entries.size()));
    std::string prior;
    for (const NormalizationEntry& entry : entries) {
        if (entry.key.empty() || entry.key.size() >
                std::numeric_limits<std::uint16_t>::max() ||
            (!prior.empty() && prior >= entry.key)) {
            return false;
        }
        AppendU16(payload, static_cast<std::uint16_t>(entry.key.size()));
        payload.push_back(entry.value_kind);
        payload.push_back(0u);
        AppendU32(payload, entry.value_count);
        payload.insert(payload.end(), entry.key.begin(), entry.key.end());
        payload.insert(payload.end(), entry.value.begin(), entry.value.end());
        prior = entry.key;
    }
    return true;
}

bool ParseNormalizationProperties(
    const laplace_unicode_source_bundle* bundle,
    const AliasTable& value_aliases,
    const PropertyAliasTable& property_aliases,
    std::vector<PositionPayload>& values,
    std::vector<NamedRange>& full_exclusion,
    std::uint64_t& normalization_rows,
    std::uint64_t& normalization_memberships,
    std::uint64_t& full_exclusion_rows,
    std::uint64_t& full_exclusion_positions) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/DerivedNormalizationProps.txt", &file) !=
        LAPLACE_UNICODE_OK) {
        return false;
    }
    std::map<std::uint32_t, std::map<std::string, NormalizationEntry>> by_position;
    const bool parsed = ForEachLine(
        file, [&](std::string_view line, std::uint32_t) {
            if (line.find("# @missing:") != std::string_view::npos) {
                return true;
            }
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            auto fields = Split(line, ';');
            for (std::string_view& field : fields) {
                field = Trim(field);
            }
            if (fields.size() < 2u || fields.size() > 3u) {
                return false;
            }
            std::uint32_t first = 0u;
            std::uint32_t last = 0u;
            std::string property;
            if (!ParseRange(fields[0], first, last) ||
                !property_aliases.Resolve(fields[1], property)) {
                return false;
            }
            if (LooseKey(property) ==
                LooseKey("Full_Composition_Exclusion")) {
                if (fields.size() == 3u && !fields[2].empty()) {
                    return false;
                }
                full_exclusion.push_back(NamedRange{first, last, "1"});
                ++full_exclusion_rows;
                full_exclusion_positions +=
                    static_cast<std::uint64_t>(last) - first + 1u;
                return true;
            }
            NormalizationEntry entry{};
            entry.key = property;
            const std::string property_key = LooseKey(fields[1]);
            const bool quick_check = property_key == LooseKey("NFC_QC") ||
                property_key == LooseKey("NFD_QC") ||
                property_key == LooseKey("NFKC_QC") ||
                property_key == LooseKey("NFKD_QC");
            const bool mapping = property_key == LooseKey("FC_NFKC") ||
                property_key == LooseKey("NFKC_CF") ||
                property_key == LooseKey("NFKC_SCF");
            if (quick_check) {
                std::string canonical;
                if (fields.size() != 3u || fields[2].empty() ||
                    !value_aliases.Resolve(fields[1], fields[2], canonical)) {
                    return false;
                }
                entry.value_kind =
                    LAPLACE_UNICODE_NORMALIZATION_ASCII_PROPERTY_VALUE;
                entry.value.assign(canonical.begin(), canonical.end());
                entry.value_count =
                    static_cast<std::uint32_t>(entry.value.size());
            } else if (mapping) {
                if (fields.size() != 3u ||
                    !AppendPositions(entry.value, fields[2])) {
                    return false;
                }
                if (entry.value.empty()) {
                    entry.value_kind =
                        LAPLACE_UNICODE_NORMALIZATION_EMPTY_POSITION_SEQUENCE;
                } else {
                    entry.value_kind =
                        LAPLACE_UNICODE_NORMALIZATION_POSITION_SEQUENCE;
                    entry.value_count =
                        static_cast<std::uint32_t>(entry.value.size() / 4u);
                }
            } else {
                if (fields.size() == 3u && !fields[2].empty()) {
                    return false;
                }
                entry.value_kind =
                    LAPLACE_UNICODE_NORMALIZATION_BINARY_TRUE;
            }
            for (std::uint32_t position = first;
                 position <= last; ++position) {
                auto [iterator, inserted] = by_position[position].emplace(
                    entry.key, entry);
                if (!inserted) {
                    (void)iterator;
                    return false;
                }
                ++normalization_memberships;
            }
            ++normalization_rows;
            return true;
        });
    if (!parsed || by_position.empty() || full_exclusion.empty()) {
        return false;
    }
    std::sort(
        full_exclusion.begin(), full_exclusion.end(),
        [](const NamedRange& left, const NamedRange& right) {
            return left.first < right.first ||
                (left.first == right.first && left.last < right.last);
        });
    for (std::size_t index = 1u; index < full_exclusion.size(); ++index) {
        if (full_exclusion[index].first <= full_exclusion[index - 1u].last) {
            return false;
        }
    }
    for (auto& [position, keyed] : by_position) {
        std::vector<NormalizationEntry> entries;
        entries.reserve(keyed.size());
        for (auto& [key, entry] : keyed) {
            (void)key;
            entries.push_back(std::move(entry));
        }
        Bytes payload;
        if (!EncodeNormalizationEntries(entries, payload)) {
            return false;
        }
        values.push_back(PositionPayload{position, std::move(payload)});
    }
    if (!FinalizePositionPayloads(values)) {
        return false;
    }

    laplace_unicode_source_file_view raw{};
    if (laplace_unicode_source_bundle_file(
            bundle, "ucd/CompositionExclusions.txt", &raw) !=
        LAPLACE_UNICODE_OK) {
        return false;
    }
    std::uint64_t raw_count = 0u;
    const bool raw_valid = ForEachLine(
        raw, [&](std::string_view line, std::uint32_t) {
            const std::size_t comment = line.find('#');
            line = Trim(line.substr(0u, comment));
            if (line.empty()) {
                return true;
            }
            std::uint32_t position = 0u;
            if (!ParseHex(line, position) ||
                FindNamedRange(full_exclusion, position) == nullptr) {
                return false;
            }
            ++raw_count;
            return true;
        });
    return raw_valid && raw_count == 81u &&
        raw_count < full_exclusion_positions;
}

void SetFieldStats(
    laplace_unicode_core_summary& summary,
    std::uint16_t field_id,
    std::uint64_t rows,
    std::uint64_t explicit_positions,
    std::uint64_t memberships) {
    const std::size_t index = static_cast<std::size_t>(field_id - 1u);
    summary.field_source_row_counts[index] = rows;
    summary.field_explicit_position_counts[index] = explicit_positions;
    summary.field_membership_counts[index] = memberships;
}

bool BuildSupplementalProperties(
    const laplace_unicode_source_bundle* bundle,
    const AliasTable& value_aliases,
    SupplementalProperties& properties,
    laplace_unicode_core_summary& summary) {
    PropertyAliasTable property_aliases;
    if (!ParsePropertyAliases(bundle, property_aliases)) {
        return false;
    }
    if (!ParseBidiBrackets(
            bundle, value_aliases, properties.bidi_brackets)) {
        return false;
    }
    if (!ParseBidiMirroring(bundle, properties.bidi_mirroring)) {
        return false;
    }
    SetFieldStats(
        summary, 4u, properties.bidi_brackets.size(),
        properties.bidi_brackets.size(), properties.bidi_brackets.size());
    SetFieldStats(
        summary, 5u, properties.bidi_mirroring.size(),
        properties.bidi_mirroring.size(), properties.bidi_mirroring.size());

    std::uint64_t special_rows = 0u;
    std::uint64_t folding_rows = 0u;
    if (!ParseSpecialCasing(
            bundle, properties.full_case_mappings, special_rows) ||
        !ParseCaseFolding(
            bundle, properties.case_folding, folding_rows)) {
        return false;
    }
    SetFieldStats(
        summary, 10u, special_rows, properties.full_case_mappings.size(),
        special_rows);
    SetFieldStats(
        summary, 11u, folding_rows, properties.case_folding.size(),
        folding_rows);

    struct Config {
        std::uint16_t field_id;
        const char* path;
        const char* value_namespace;
        const char* default_alias;
        const char* required_property;
    };
    static constexpr std::array<Config, 10> Configs{{
        {12u, "ucd/Blocks.txt", "blk", "No_Block", ""},
        {13u, "ucd/EastAsianWidth.txt", "ea", "Neutral", ""},
        {14u, "ucd/Scripts.txt", "sc", "Unknown", ""},
        {20u, "ucd/HangulSyllableType.txt", "hst", "Not_Applicable", ""},
        {21u, "ucd/auxiliary/GraphemeBreakProperty.txt", "gcb", "Other", ""},
        {22u, "ucd/auxiliary/WordBreakProperty.txt", "wb", "Other", ""},
        {23u, "ucd/auxiliary/SentenceBreakProperty.txt", "sb", "Other", ""},
        {24u, "ucd/DerivedCoreProperties.txt", "InCB", "None", "InCB"},
        {26u, "ucd/LineBreak.txt", "lb", "Unknown", ""},
        {0u, nullptr, nullptr, nullptr, nullptr}}};
    for (const Config& config : Configs) {
        if (config.field_id == 0u) {
            continue;
        }
        SingleProperty& property =
            properties.single[config.field_id - 1u];
        if (!ParseSingleProperty(
                bundle, value_aliases, config.path,
                config.value_namespace, config.default_alias,
                config.required_property, property)) {
            return false;
        }
        SetFieldStats(
            summary, config.field_id, property.source_rows,
            property.explicit_positions, property.explicit_positions);
    }

    std::set<std::string> scripts;
    const SingleProperty& script_property = properties.single[13u];
    scripts.insert(script_property.default_value);
    for (const NamedRange& range : script_property.ranges) {
        scripts.insert(range.value);
    }
    for (const std::string& script : scripts) {
        Bytes payload;
        if (!EncodeAsciiSet(std::vector<std::string>{script}, payload)) {
            return false;
        }
        properties.script_singletons.emplace(script, std::move(payload));
    }
    std::uint64_t script_extension_rows = 0u;
    std::uint64_t script_extension_memberships = 0u;
    if (!ParseScriptExtensions(
            bundle, value_aliases, properties.script_extensions,
            script_extension_rows, script_extension_memberships)) {
        return false;
    }
    SetFieldStats(
        summary, 15u, script_extension_rows,
        properties.script_extensions.size(), script_extension_memberships);

    if (!ParseSetPropertyPlane(
            bundle, property_aliases, "ucd/PropList.txt", false,
            properties.prop_list) ||
        !ParseSetPropertyPlane(
            bundle, property_aliases, "ucd/DerivedCoreProperties.txt", true,
            properties.derived_core)) {
        return false;
    }
    SetFieldStats(
        summary, 16u, properties.prop_list.source_rows,
        properties.prop_list.explicit_positions,
        properties.prop_list.memberships);
    SetFieldStats(
        summary, 17u, properties.derived_core.source_rows,
        properties.derived_core.explicit_positions,
        properties.derived_core.memberships);

    std::uint64_t normalization_rows = 0u;
    std::uint64_t normalization_memberships = 0u;
    std::uint64_t exclusion_rows = 0u;
    std::uint64_t exclusion_positions = 0u;
    if (!ParseNormalizationProperties(
            bundle, value_aliases, property_aliases,
            properties.normalization,
            properties.full_composition_exclusion,
            normalization_rows, normalization_memberships,
            exclusion_rows, exclusion_positions)) {
        return false;
    }
    SetFieldStats(
        summary, 18u, normalization_rows, properties.normalization.size(),
        normalization_memberships);
    SetFieldStats(
        summary, 19u, exclusion_rows, exclusion_positions,
        exclusion_positions);

    std::uint64_t pictographic_rows = 0u;
    std::uint64_t pictographic_positions = 0u;
    if (!ParseBooleanProperty(
            bundle, "ucd/emoji/emoji-data.txt", "Extended_Pictographic",
            properties.extended_pictographic,
            pictographic_rows, pictographic_positions)) {
        return false;
    }
    SetFieldStats(
        summary, 25u, pictographic_rows, pictographic_positions,
        pictographic_positions);
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
    std::uint64_t numeric_position_count = 0u;
    std::uint64_t simple_mapping_entry_count = 0u;
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
            numeric_position_count += value.payloads[5].empty() ? 0u : 1u;
            if (!value.payloads[6].empty()) {
                simple_mapping_entry_count += ReadU32(
                    value.payloads[6].data());
            }
        }
        std::uint8_t position_class = 0u;
        if (!PositionClass(position, explicit_position, position_class) ||
            position_class >= 5u) {
            return false;
        }
        ++summary.position_class_counts[position_class];
    }
    for (const std::size_t index : std::array<std::size_t, 2>{{0u, 1u}}) {
        summary.field_source_row_counts[index] = summary.unicode_data_row_count;
        summary.field_explicit_position_counts[index] =
            summary.explicit_position_count;
        summary.field_membership_counts[index] =
            summary.explicit_position_count;
    }
    summary.field_source_row_counts[5u] =
        summary.canonical_decomposition_count;
    summary.field_explicit_position_counts[5u] =
        summary.canonical_decomposition_count;
    summary.field_membership_counts[5u] =
        summary.canonical_decomposition_count;
    summary.field_source_row_counts[6u] =
        summary.compatibility_decomposition_count;
    summary.field_explicit_position_counts[6u] =
        summary.compatibility_decomposition_count;
    summary.field_membership_counts[6u] =
        summary.compatibility_decomposition_count;
    summary.field_source_row_counts[7u] = numeric_position_count;
    summary.field_explicit_position_counts[7u] = numeric_position_count;
    summary.field_membership_counts[7u] = numeric_position_count;
    summary.field_source_row_counts[8u] =
        summary.simple_case_mapping_position_count;
    summary.field_explicit_position_counts[8u] =
        summary.simple_case_mapping_position_count;
    summary.field_membership_counts[8u] = simple_mapping_entry_count;

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
    SupplementalProperties supplemental;
    laplace_unicode_core_summary summary{};
};

namespace {

void AssignFieldPayload(
    laplace_unicode_atom_field& field,
    const Bytes* payload) {
    if (payload == nullptr || payload->empty()) {
        field.payload = nullptr;
        field.payload_bytes = 0u;
        return;
    }
    field.payload = payload->data();
    field.payload_bytes = static_cast<std::uint32_t>(payload->size());
}

void AssignFieldText(
    laplace_unicode_atom_field& field,
    const std::string& payload) {
    field.payload = reinterpret_cast<const std::uint8_t*>(payload.data());
    field.payload_bytes = static_cast<std::uint32_t>(payload.size());
}

bool FillCompleteRecord(
    const laplace_unicode_core_table* table,
    std::uint32_t codepoint_position,
    laplace_unicode_core_record_view* view) {
    *view = laplace_unicode_core_record_view{};
    if (codepoint_position >= LAPLACE_UNICODE_ROOT_POPULATION) {
        return false;
    }
    for (std::uint16_t field_id = 1u;
         field_id <= LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field_id) {
        laplace_unicode_atom_field& field = view->fields[field_id - 1u];
        field.field_id = field_id;
        if (laplace_unicode_atom_field_payload_kind(
                field_id, &field.payload_kind) != LAPLACE_UNICODE_OK) {
            return false;
        }
    }
    const CoreRange* core = FindCoreRange(
        table->core_ranges, codepoint_position);
    const BidiRange* bidi = FindBidiRange(
        table->bidi_ranges, codepoint_position);
    if (bidi == nullptr ||
        !PositionClass(codepoint_position, core != nullptr,
                       view->position_class)) {
        return false;
    }
    view->codepoint_position = codepoint_position;
    const CoreValue& core_value =
        core != nullptr ? core->value : table->default_value;
    for (std::size_t index = 0u; index < CoreFieldIds.size(); ++index) {
        laplace_unicode_atom_field& field =
            view->fields[CoreFieldIds[index] - 1u];
        if (index == 2u) {
            AssignFieldText(field, bidi->value);
        } else {
            AssignFieldPayload(field, &core_value.payloads[index]);
        }
    }

    const SupplementalProperties& properties = table->supplemental;
    const auto assign_position = [&](std::uint16_t field_id,
                                     const std::vector<PositionPayload>& source) {
        const PositionPayload* value = FindPositionPayload(
            source, codepoint_position);
        AssignFieldPayload(
            view->fields[field_id - 1u],
            value != nullptr ? &value->payload : nullptr);
    };
    assign_position(4u, properties.bidi_brackets);
    assign_position(5u, properties.bidi_mirroring);
    assign_position(10u, properties.full_case_mappings);
    assign_position(11u, properties.case_folding);
    for (const std::uint16_t field_id :
         std::array<std::uint16_t, 9>{{
             12u, 13u, 14u, 20u, 21u, 22u, 23u, 24u, 26u}}) {
        AssignFieldText(
            view->fields[field_id - 1u],
            PropertyAt(properties.single[field_id - 1u], codepoint_position));
    }

    const PositionPayload* explicit_scripts = FindPositionPayload(
        properties.script_extensions, codepoint_position);
    if (explicit_scripts != nullptr) {
        AssignFieldPayload(view->fields[14u], &explicit_scripts->payload);
    } else {
#if defined(LAPLACE_TEST_UNICODE_SCRIPT_EXTENSIONS_EMPTY_FALLBACK)
        AssignFieldPayload(view->fields[14u], nullptr);
#else
        const std::string& script = PropertyAt(
            properties.single[13u], codepoint_position);
        const auto singleton = properties.script_singletons.find(script);
        if (singleton == properties.script_singletons.end()) {
            return false;
        }
        AssignFieldPayload(view->fields[14u], &singleton->second);
#endif
    }

    const auto assign_set_plane = [&](std::uint16_t field_id,
                                      const SetPropertyPlane& plane) {
        const std::uint64_t mask = plane.masks[codepoint_position];
        const auto found = plane.encoded_by_mask.find(mask);
        if (found == plane.encoded_by_mask.end()) {
            return false;
        }
        AssignFieldPayload(view->fields[field_id - 1u], &found->second);
        return true;
    };
    if (!assign_set_plane(16u, properties.prop_list) ||
        !assign_set_plane(17u, properties.derived_core)) {
        return false;
    }
    assign_position(18u, properties.normalization);

    static const std::uint8_t FalseValue = 0u;
    static const std::uint8_t TrueValue = 1u;
    laplace_unicode_atom_field& exclusion = view->fields[18u];
    exclusion.payload = FindNamedRange(
        properties.full_composition_exclusion, codepoint_position) != nullptr
        ? &TrueValue
        : &FalseValue;
    exclusion.payload_bytes = 1u;
    laplace_unicode_atom_field& pictographic = view->fields[24u];
    pictographic.payload = FindNamedRange(
        properties.extended_pictographic, codepoint_position) != nullptr
        ? &TrueValue
        : &FalseValue;
    pictographic.payload_bytes = 1u;
    return true;
}

bool FinalizeCompletePropertyFingerprint(laplace_unicode_core_table* table) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, CompletePropertyDomain);
    blake3_hasher_update(
        &hasher, table->summary.source_fingerprint.bytes,
        sizeof(table->summary.source_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, table->summary.recipe_fingerprint.bytes,
        sizeof(table->summary.recipe_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, table->summary.normalized_fingerprint.bytes,
        sizeof(table->summary.normalized_fingerprint.bytes));
    for (std::uint32_t position = 0u;
         position < LAPLACE_UNICODE_ROOT_POPULATION; ++position) {
        laplace_unicode_core_record_view view{};
        if (!FillCompleteRecord(table, position, &view)) {
            return false;
        }
        HashU32(hasher, position);
        for (std::uint16_t field_id = 4u;
             field_id <= LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field_id) {
            if (field_id == 6u || field_id == 7u || field_id == 8u ||
                field_id == 9u) {
                continue;
            }
            const laplace_unicode_atom_field& field =
                view.fields[field_id - 1u];
            HashU32(hasher, field_id);
            HashU32(hasher, field.payload_kind);
            HashBytes(
                hasher, field.payload,
                static_cast<std::size_t>(field.payload_bytes));
        }
    }
    table->summary.complete_property_fingerprint = Finish(hasher);

    blake3_hasher receipt{};
    blake3_hasher_init(&receipt);
    HashString(receipt, ReceiptDomain);
    blake3_hasher_update(
        &receipt, table->summary.complete_property_fingerprint.bytes,
        sizeof(table->summary.complete_property_fingerprint.bytes));
    for (std::size_t index = 0u;
         index < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++index) {
        HashU64(receipt, table->summary.field_source_row_counts[index]);
        HashU64(receipt, table->summary.field_explicit_position_counts[index]);
        HashU64(receipt, table->summary.field_membership_counts[index]);
    }
    table->summary.receipt_id = Finish(receipt);
    return true;
}

}  // namespace

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
        if (!BuildSupplementalProperties(
                bundle, aliases, created->supplemental, created->summary) ||
            !FinalizeCompletePropertyFingerprint(created)) {
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
    if (codepoint_position >= LAPLACE_UNICODE_ROOT_POPULATION) {
        *view = laplace_unicode_core_record_view{};
        return LAPLACE_UNICODE_POSITION_OUT_OF_RANGE;
    }
    return FillCompleteRecord(table, codepoint_position, view)
        ? LAPLACE_UNICODE_OK
        : LAPLACE_UNICODE_SOURCE_INCOMPLETE;
}

extern "C" void laplace_unicode_core_table_destroy(
    laplace_unicode_core_table** table) {
    if (table != nullptr) {
        delete *table;
        *table = nullptr;
    }
}
