#include "laplace/uax29.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string_view>
#include <vector>

struct laplace_uax29_tables {
    std::vector<std::uint8_t> grapheme;
    std::vector<std::uint8_t> word;
    std::vector<std::uint8_t> sentence;
    std::vector<std::uint8_t> incb;
    std::vector<std::uint8_t> extended_pictographic;
};

namespace {

constexpr std::uint32_t UnicodePopulation = LAPLACE_UNICODE_ROOT_POPULATION;

/* Internal compact property values. They deliberately are not public ABI. */
enum : std::uint8_t {
    GB_OTHER = 0, GB_CR, GB_LF, GB_CONTROL, GB_EXTEND, GB_ZWJ,
    GB_REGIONAL_INDICATOR, GB_PREPEND, GB_SPACINGMARK, GB_L, GB_V,
    GB_T, GB_LV, GB_LVT
};

enum : std::uint8_t {
    WB_OTHER = 0, WB_CR, WB_LF, WB_NEWLINE, WB_EXTEND, WB_ZWJ,
    WB_REGIONAL_INDICATOR, WB_FORMAT, WB_KATAKANA, WB_HEBREW_LETTER,
    WB_ALETTER, WB_SINGLE_QUOTE, WB_DOUBLE_QUOTE, WB_MIDNUMLET,
    WB_MIDLETTER, WB_MIDNUM, WB_NUMERIC, WB_EXTENDNUMLET, WB_WSEGSPACE
};

enum : std::uint8_t {
    SB_OTHER = 0, SB_CR, SB_LF, SB_EXTEND, SB_SEP, SB_FORMAT, SB_SP,
    SB_LOWER, SB_UPPER, SB_OLETTER, SB_NUMERIC, SB_ATERM, SB_SCONTINUE,
    SB_STERM, SB_CLOSE
};

enum : std::uint8_t {
    INCB_NONE = 0, INCB_EXTEND, INCB_LINKER, INCB_CONSONANT
};

struct DecodedText {
    std::vector<std::uint32_t> codepoints;
    std::vector<std::uint64_t> byte_offsets;
};

std::string_view Trim(std::string_view value) {
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1u);
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1u);
    }
    return value;
}

bool ParseHex(std::string_view value, std::uint32_t& output) {
    value = Trim(value);
    if (value.empty() || value.size() > 6u) return false;
    std::uint32_t result = 0u;
    for (const char ch : value) {
        std::uint32_t digit = 0u;
        if (ch >= '0' && ch <= '9') digit = static_cast<std::uint32_t>(ch - '0');
        else if (ch >= 'A' && ch <= 'F') digit = static_cast<std::uint32_t>(ch - 'A') + 10u;
        else if (ch >= 'a' && ch <= 'f') digit = static_cast<std::uint32_t>(ch - 'a') + 10u;
        else return false;
        if (result > (UnicodePopulation - 1u) / 16u) return false;
        result = result * 16u + digit;
    }
    if (result >= UnicodePopulation) return false;
    output = result;
    return true;
}

bool ParseRange(std::string_view value, std::uint32_t& first, std::uint32_t& last) {
    value = Trim(value);
    const std::size_t separator = value.find("..");
    if (separator == std::string_view::npos) {
        if (!ParseHex(value, first)) return false;
        last = first;
        return true;
    }
    if (value.find("..", separator + 2u) != std::string_view::npos) return false;
    return ParseHex(value.substr(0u, separator), first) &&
           ParseHex(value.substr(separator + 2u), last) && first <= last;
}

std::vector<std::string_view> SplitSemicolon(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t offset = 0u;
    for (;;) {
        const std::size_t next = line.find(';', offset);
        fields.push_back(Trim(line.substr(
            offset,
            next == std::string_view::npos ? line.size() - offset : next - offset)));
        if (next == std::string_view::npos) return fields;
        offset = next + 1u;
    }
}

template <typename Function>
bool ForEachDataLine(const laplace_unicode_source_file_view& file, Function&& function) {
    if (file.bytes == nullptr || file.byte_count > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    const std::string_view contents(
        reinterpret_cast<const char*>(file.bytes), static_cast<std::size_t>(file.byte_count));
    std::size_t offset = 0u;
    while (offset < contents.size()) {
        const std::size_t newline = contents.find('\n', offset);
        std::string_view line = contents.substr(
            offset,
            newline == std::string_view::npos ? contents.size() - offset : newline - offset);
        const std::size_t comment = line.find('#');
        line = Trim(line.substr(0u, comment));
        if (!line.empty() && !function(line)) return false;
        if (newline == std::string_view::npos) return true;
        offset = newline + 1u;
    }
    return true;
}

bool MapGrapheme(std::string_view value, std::uint8_t& mapped) {
    struct Pair { std::string_view name; std::uint8_t value; };
    static constexpr Pair values[] = {
        {"Other", GB_OTHER}, {"CR", GB_CR}, {"LF", GB_LF}, {"Control", GB_CONTROL},
        {"Extend", GB_EXTEND}, {"ZWJ", GB_ZWJ}, {"Regional_Indicator", GB_REGIONAL_INDICATOR},
        {"Prepend", GB_PREPEND}, {"SpacingMark", GB_SPACINGMARK}, {"L", GB_L},
        {"V", GB_V}, {"T", GB_T}, {"LV", GB_LV}, {"LVT", GB_LVT}
    };
    for (const Pair& pair : values) {
        if (value == pair.name) { mapped = pair.value; return true; }
    }
    return false;
}

bool MapWord(std::string_view value, std::uint8_t& mapped) {
    struct Pair { std::string_view name; std::uint8_t value; };
    static constexpr Pair values[] = {
        {"Other", WB_OTHER}, {"CR", WB_CR}, {"LF", WB_LF}, {"Newline", WB_NEWLINE},
        {"Extend", WB_EXTEND}, {"ZWJ", WB_ZWJ}, {"Regional_Indicator", WB_REGIONAL_INDICATOR},
        {"Format", WB_FORMAT}, {"Katakana", WB_KATAKANA}, {"Hebrew_Letter", WB_HEBREW_LETTER},
        {"ALetter", WB_ALETTER}, {"Single_Quote", WB_SINGLE_QUOTE}, {"Double_Quote", WB_DOUBLE_QUOTE},
        {"MidNumLet", WB_MIDNUMLET}, {"MidLetter", WB_MIDLETTER}, {"MidNum", WB_MIDNUM},
        {"Numeric", WB_NUMERIC}, {"ExtendNumLet", WB_EXTENDNUMLET}, {"WSegSpace", WB_WSEGSPACE}
    };
    for (const Pair& pair : values) {
        if (value == pair.name) { mapped = pair.value; return true; }
    }
    return false;
}

bool MapSentence(std::string_view value, std::uint8_t& mapped) {
    struct Pair { std::string_view name; std::uint8_t value; };
    static constexpr Pair values[] = {
        {"Other", SB_OTHER}, {"CR", SB_CR}, {"LF", SB_LF}, {"Extend", SB_EXTEND},
        {"Sep", SB_SEP}, {"Format", SB_FORMAT}, {"Sp", SB_SP}, {"Lower", SB_LOWER},
        {"Upper", SB_UPPER}, {"OLetter", SB_OLETTER}, {"Numeric", SB_NUMERIC},
        {"ATerm", SB_ATERM}, {"SContinue", SB_SCONTINUE}, {"STerm", SB_STERM},
        {"Close", SB_CLOSE}
    };
    for (const Pair& pair : values) {
        if (value == pair.name) { mapped = pair.value; return true; }
    }
    return false;
}

bool MapIncb(std::string_view value, std::uint8_t& mapped) {
    if (value == "Extend") { mapped = INCB_EXTEND; return true; }
    if (value == "Linker") { mapped = INCB_LINKER; return true; }
    if (value == "Consonant") { mapped = INCB_CONSONANT; return true; }
    return false;
}

template <typename Mapper>
bool LoadSimpleProperty(
    const laplace_unicode_source_bundle* bundle,
    const char* path,
    std::vector<std::uint8_t>& destination,
    Mapper&& mapper) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(bundle, path, &file) != LAPLACE_UNICODE_OK) return false;
    return ForEachDataLine(file, [&](std::string_view line) {
        const std::vector<std::string_view> fields = SplitSemicolon(line);
        if (fields.size() < 2u) return false;
        std::uint32_t first = 0u;
        std::uint32_t last = 0u;
        std::uint8_t value = 0u;
        if (!ParseRange(fields[0], first, last) || !mapper(fields[1], value)) return false;
        std::fill(destination.begin() + static_cast<std::ptrdiff_t>(first),
                  destination.begin() + static_cast<std::ptrdiff_t>(last) + 1,
                  value);
        return true;
    });
}

bool LoadExtendedPictographic(
    const laplace_unicode_source_bundle* bundle,
    std::vector<std::uint8_t>& destination) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(bundle, "ucd/emoji/emoji-data.txt", &file) != LAPLACE_UNICODE_OK) return false;
    return ForEachDataLine(file, [&](std::string_view line) {
        const std::vector<std::string_view> fields = SplitSemicolon(line);
        if (fields.size() < 2u || fields[1] != "Extended_Pictographic") return true;
        std::uint32_t first = 0u;
        std::uint32_t last = 0u;
        if (!ParseRange(fields[0], first, last)) return false;
        std::fill(destination.begin() + static_cast<std::ptrdiff_t>(first),
                  destination.begin() + static_cast<std::ptrdiff_t>(last) + 1,
                  static_cast<std::uint8_t>(1u));
        return true;
    });
}

bool LoadIncb(
    const laplace_unicode_source_bundle* bundle,
    std::vector<std::uint8_t>& destination) {
    laplace_unicode_source_file_view file{};
    if (laplace_unicode_source_bundle_file(bundle, "ucd/DerivedCoreProperties.txt", &file) != LAPLACE_UNICODE_OK) return false;
    return ForEachDataLine(file, [&](std::string_view line) {
        const std::vector<std::string_view> fields = SplitSemicolon(line);
        if (fields.size() < 2u) return false;
        std::string_view value;
        if (fields[1] == "InCB") {
            if (fields.size() < 3u) return false;
            value = fields[2];
        } else if (fields[1].starts_with("InCB=")) {
            value = fields[1].substr(5u);
        } else {
            return true;
        }
        std::uint8_t mapped = 0u;
        std::uint32_t first = 0u;
        std::uint32_t last = 0u;
        if (!MapIncb(value, mapped) || !ParseRange(fields[0], first, last)) return false;
        std::fill(destination.begin() + static_cast<std::ptrdiff_t>(first),
                  destination.begin() + static_cast<std::ptrdiff_t>(last) + 1,
                  mapped);
        return true;
    });
}

bool DecodeUtf8(const std::uint8_t* bytes, std::size_t count, DecodedText& decoded) {
    decoded.codepoints.clear();
    decoded.byte_offsets.clear();
    decoded.codepoints.reserve(count);
    decoded.byte_offsets.reserve(count + 1u);
    std::size_t offset = 0u;
    while (offset < count) {
        decoded.byte_offsets.push_back(static_cast<std::uint64_t>(offset));
        const std::uint8_t first = bytes[offset];
        std::uint32_t cp = 0u;
        std::size_t width = 0u;
        if (first <= 0x7fu) { cp = first; width = 1u; }
        else if ((first & 0xe0u) == 0xc0u) { cp = static_cast<std::uint32_t>(first & 0x1fu); width = 2u; }
        else if ((first & 0xf0u) == 0xe0u) { cp = static_cast<std::uint32_t>(first & 0x0fu); width = 3u; }
        else if ((first & 0xf8u) == 0xf0u) { cp = static_cast<std::uint32_t>(first & 0x07u); width = 4u; }
        else return false;
        if (width > count - offset) return false;
        for (std::size_t index = 1u; index < width; ++index) {
            const std::uint8_t continuation = bytes[offset + index];
            if ((continuation & 0xc0u) != 0x80u) return false;
            cp = (cp << 6u) | static_cast<std::uint32_t>(continuation & 0x3fu);
        }
        if ((width == 2u && cp < 0x80u) ||
            (width == 3u && cp < 0x800u) ||
            (width == 4u && cp < 0x10000u) ||
            cp >= UnicodePopulation || (cp >= 0xd800u && cp <= 0xdfffu)) return false;
        decoded.codepoints.push_back(cp);
        offset += width;
    }
    decoded.byte_offsets.push_back(static_cast<std::uint64_t>(count));
    return true;
}

std::uint8_t G(const laplace_uax29_tables& table, std::uint32_t cp) { return table.grapheme[cp]; }
std::uint8_t W(const laplace_uax29_tables& table, std::uint32_t cp) { return table.word[cp]; }
std::uint8_t S(const laplace_uax29_tables& table, std::uint32_t cp) { return table.sentence[cp]; }
std::uint8_t I(const laplace_uax29_tables& table, std::uint32_t cp) { return table.incb[cp]; }
bool EP(const laplace_uax29_tables& table, std::uint32_t cp) { return table.extended_pictographic[cp] != 0u; }

std::size_t GraphemeNext(
    const laplace_uax29_tables& table,
    const std::vector<std::uint32_t>& cps,
    std::size_t from) {
    const std::size_t count = cps.size();
    if (from >= count || from + 1u >= count) return count;

    std::size_t ri_run = 0u;
    if (G(table, cps[from]) == GB_REGIONAL_INDICATOR) {
        ri_run = 1u;
        std::size_t cursor = from;
        while (cursor > 0u && G(table, cps[cursor - 1u]) == GB_REGIONAL_INDICATOR) {
            ++ri_run;
            --cursor;
        }
    }

    bool saw_pictograph = EP(table, cps[from]);
    bool saw_incb_consonant = I(table, cps[from]) == INCB_CONSONANT;
    bool saw_incb_linker = false;

    for (std::size_t index = from + 1u; index < count; ++index) {
        const std::uint32_t previous_cp = cps[index - 1u];
        const std::uint32_t current_cp = cps[index];
        const std::uint8_t previous = G(table, previous_cp);
        const std::uint8_t current = G(table, current_cp);
        bool no_break = false;

        if (previous == GB_CR && current == GB_LF) no_break = true;                         /* GB3 */
        else if (previous == GB_CONTROL || previous == GB_CR || previous == GB_LF) return index; /* GB4 */
        else if (current == GB_CONTROL || current == GB_CR || current == GB_LF) return index;     /* GB5 */
        else if (previous == GB_L && (current == GB_L || current == GB_V || current == GB_LV || current == GB_LVT)) no_break = true; /* GB6 */
        else if ((previous == GB_LV || previous == GB_V) && (current == GB_V || current == GB_T)) no_break = true; /* GB7 */
        else if ((previous == GB_LVT || previous == GB_T) && current == GB_T) no_break = true;     /* GB8 */
        else if (current == GB_EXTEND || current == GB_ZWJ) no_break = true;                        /* GB9 */
        else if (current == GB_SPACINGMARK) no_break = true;                                        /* GB9a */
        else if (previous == GB_PREPEND) no_break = true;                                            /* GB9b */
        else if (saw_incb_consonant && saw_incb_linker && I(table, current_cp) == INCB_CONSONANT) no_break = true; /* GB9c */
        else if (saw_pictograph && previous == GB_ZWJ && EP(table, current_cp)) no_break = true;      /* GB11 */
        else if (previous == GB_REGIONAL_INDICATOR && current == GB_REGIONAL_INDICATOR && (ri_run % 2u) == 1u) no_break = true; /* GB12/13 */

        if (!no_break) return index;

        if (current == GB_REGIONAL_INDICATOR) ++ri_run;
        else ri_run = 0u;

        if (EP(table, current_cp)) saw_pictograph = true;
        else if (current != GB_EXTEND && current != GB_ZWJ) saw_pictograph = false;

        const std::uint8_t incb = I(table, current_cp);
        if (incb == INCB_CONSONANT) { saw_incb_consonant = true; saw_incb_linker = false; }
        else if (incb == INCB_LINKER) { if (saw_incb_consonant) saw_incb_linker = true; }
        else if (incb != INCB_EXTEND) { saw_incb_consonant = false; saw_incb_linker = false; }
    }
    return count;
}

bool WordIgnored(std::uint8_t value) {
    return value == WB_EXTEND || value == WB_FORMAT || value == WB_ZWJ;
}
bool AhLetter(std::uint8_t value) { return value == WB_ALETTER || value == WB_HEBREW_LETTER; }
bool MidLetterOrQuote(std::uint8_t value) { return value == WB_MIDLETTER || value == WB_MIDNUMLET || value == WB_SINGLE_QUOTE; }
bool MidNumOrQuote(std::uint8_t value) { return value == WB_MIDNUM || value == WB_MIDNUMLET || value == WB_SINGLE_QUOTE; }
bool Wb13aLeft(std::uint8_t value) { return AhLetter(value) || value == WB_NUMERIC || value == WB_KATAKANA || value == WB_EXTENDNUMLET; }
bool Wb13bRight(std::uint8_t value) { return AhLetter(value) || value == WB_NUMERIC || value == WB_KATAKANA; }

std::size_t PreviousSignificant(
    const laplace_uax29_tables& table,
    const std::vector<std::uint32_t>& cps,
    std::size_t index) {
    if (index == 0u) return SIZE_MAX;
    std::size_t cursor = index - 1u;
    for (;;) {
        if (!WordIgnored(W(table, cps[cursor]))) return cursor;
        if (cursor == 0u) return SIZE_MAX;
        --cursor;
    }
}

std::size_t WordNext(
    const laplace_uax29_tables& table,
    const std::vector<std::uint32_t>& cps,
    std::size_t from) {
    const std::size_t count = cps.size();
    if (from >= count || from + 1u >= count) return count;
    std::size_t ri_run = W(table, cps[from]) == WB_REGIONAL_INDICATOR ? 1u : 0u;
    if (ri_run != 0u) {
        std::size_t cursor = from;
        while (cursor > 0u && W(table, cps[cursor - 1u]) == WB_REGIONAL_INDICATOR) {
            ++ri_run;
            --cursor;
        }
    }
    std::uint8_t previous_literal = W(table, cps[from]);
    for (std::size_t index = from + 1u; index < count; ++index) {
        const std::uint32_t current_cp = cps[index];
        const std::uint8_t current = W(table, current_cp);
        bool no_break = false;
        if (previous_literal == WB_CR && current == WB_LF) no_break = true;
        else if (previous_literal == WB_NEWLINE || previous_literal == WB_CR || previous_literal == WB_LF) return index;
        else if (current == WB_NEWLINE || current == WB_CR || current == WB_LF) return index;
        else if (previous_literal == WB_ZWJ && EP(table, current_cp)) no_break = true;
        else if (previous_literal == WB_WSEGSPACE && current == WB_WSEGSPACE) no_break = true;
        else if (WordIgnored(current)) no_break = true;
        else {
            const std::size_t previous_index = PreviousSignificant(table, cps, index);
            if (previous_index == SIZE_MAX) return index;
            const std::uint8_t previous = W(table, cps[previous_index]);
            if (AhLetter(previous) && AhLetter(current)) no_break = true;
            else if (AhLetter(previous) && MidLetterOrQuote(current)) {
                std::size_t look = index + 1u;
                while (look < count && WordIgnored(W(table, cps[look]))) ++look;
                no_break = look < count && AhLetter(W(table, cps[look]));
            } else if (MidLetterOrQuote(previous) && AhLetter(current)) {
                const std::size_t before = PreviousSignificant(table, cps, previous_index);
                no_break = before != SIZE_MAX && AhLetter(W(table, cps[before]));
            } else if (previous == WB_HEBREW_LETTER && current == WB_SINGLE_QUOTE) no_break = true;
            else if (previous == WB_HEBREW_LETTER && current == WB_DOUBLE_QUOTE) {
                std::size_t look = index + 1u;
                while (look < count && WordIgnored(W(table, cps[look]))) ++look;
                no_break = look < count && W(table, cps[look]) == WB_HEBREW_LETTER;
            } else if (previous == WB_DOUBLE_QUOTE && current == WB_HEBREW_LETTER) {
                const std::size_t before = PreviousSignificant(table, cps, previous_index);
                no_break = before != SIZE_MAX && W(table, cps[before]) == WB_HEBREW_LETTER;
            } else if (previous == WB_NUMERIC && current == WB_NUMERIC) no_break = true;
            else if (AhLetter(previous) && current == WB_NUMERIC) no_break = true;
            else if (previous == WB_NUMERIC && AhLetter(current)) no_break = true;
            else if (MidNumOrQuote(previous) && current == WB_NUMERIC) {
                const std::size_t before = PreviousSignificant(table, cps, previous_index);
                no_break = before != SIZE_MAX && W(table, cps[before]) == WB_NUMERIC;
            } else if (previous == WB_NUMERIC && MidNumOrQuote(current)) {
                std::size_t look = index + 1u;
                while (look < count && WordIgnored(W(table, cps[look]))) ++look;
                no_break = look < count && W(table, cps[look]) == WB_NUMERIC;
            } else if (previous == WB_KATAKANA && current == WB_KATAKANA) no_break = true;
            else if (Wb13aLeft(previous) && current == WB_EXTENDNUMLET) no_break = true;
            else if (previous == WB_EXTENDNUMLET && Wb13bRight(current)) no_break = true;
            else if (previous == WB_REGIONAL_INDICATOR && current == WB_REGIONAL_INDICATOR && (ri_run % 2u) == 1u) no_break = true;
        }
        if (!no_break) return index;
        if (current == WB_REGIONAL_INDICATOR) ++ri_run;
        else if (!WordIgnored(current)) ri_run = 0u;
        previous_literal = current;
    }
    return count;
}

bool SentenceIgnored(std::uint8_t value) { return value == SB_EXTEND || value == SB_FORMAT; }

bool TrailingTerm(
    const laplace_uax29_tables& table,
    const std::vector<std::uint32_t>& cps,
    std::size_t index,
    bool& is_aterm,
    bool& had_spaces) {
    if (index == 0u) return false;
    bool allow_spaces = true;
    bool allow_close = true;
    had_spaces = false;
    std::size_t cursor = index;
    while (cursor > 0u) {
        --cursor;
        const std::uint8_t value = S(table, cps[cursor]);
        if (SentenceIgnored(value)) continue;
        if (allow_spaces && value == SB_SP) { had_spaces = true; continue; }
        allow_spaces = false;
        if (allow_close && value == SB_CLOSE) continue;
        allow_close = false;
        if (value == SB_ATERM) { is_aterm = true; return true; }
        if (value == SB_STERM) { is_aterm = false; return true; }
        return false;
    }
    return false;
}

bool Sb8FindsLower(
    const laplace_uax29_tables& table,
    const std::vector<std::uint32_t>& cps,
    std::size_t index) {
    for (std::size_t cursor = index; cursor < cps.size(); ++cursor) {
        const std::uint8_t value = S(table, cps[cursor]);
        if (SentenceIgnored(value)) continue;
        if (value == SB_LOWER) return true;
        if (value == SB_OLETTER || value == SB_UPPER || value == SB_SEP ||
            value == SB_CR || value == SB_LF || value == SB_STERM || value == SB_ATERM) return false;
    }
    return false;
}

bool Sb7Matches(
    const laplace_uax29_tables& table,
    const std::vector<std::uint32_t>& cps,
    std::size_t previous_index,
    std::uint8_t current) {
    if (current != SB_UPPER || previous_index == SIZE_MAX ||
        S(table, cps[previous_index]) != SB_ATERM || previous_index == 0u) return false;
    std::size_t cursor = previous_index;
    while (cursor > 0u) {
        --cursor;
        const std::uint8_t value = S(table, cps[cursor]);
        if (SentenceIgnored(value)) continue;
        return value == SB_UPPER || value == SB_LOWER;
    }
    return false;
}

std::size_t SentenceNext(
    const laplace_uax29_tables& table,
    const std::vector<std::uint32_t>& cps,
    std::size_t from) {
    const std::size_t count = cps.size();
    if (from >= count || from + 1u >= count) return count;
    for (std::size_t index = from + 1u; index < count; ++index) {
        const std::uint8_t previous_literal = S(table, cps[index - 1u]);
        const std::uint8_t current = S(table, cps[index]);
        if (previous_literal == SB_CR && current == SB_LF) continue;
        if (previous_literal == SB_SEP || previous_literal == SB_CR || previous_literal == SB_LF) return index;
        if (SentenceIgnored(current)) continue;

        std::size_t previous_index = SIZE_MAX;
        std::uint8_t previous = 0xffu;
        std::size_t cursor = index;
        while (cursor > 0u) {
            --cursor;
            const std::uint8_t value = S(table, cps[cursor]);
            if (!SentenceIgnored(value)) { previous_index = cursor; previous = value; break; }
        }
        if (previous == SB_ATERM && current == SB_NUMERIC) continue;
        if (Sb7Matches(table, cps, previous_index, current)) continue;

        bool is_aterm = false;
        bool had_spaces = false;
        if (TrailingTerm(table, cps, index, is_aterm, had_spaces)) {
            if (is_aterm && Sb8FindsLower(table, cps, index)) continue;
            if (current == SB_SCONTINUE || current == SB_STERM || current == SB_ATERM) continue;
            if (!had_spaces && (current == SB_CLOSE || current == SB_SP || current == SB_SEP || current == SB_CR || current == SB_LF)) continue;
            if (current == SB_SP || current == SB_SEP || current == SB_CR || current == SB_LF) continue;
            return index;
        }
    }
    return count;
}

using BoundaryFn = std::size_t (*)(
    const laplace_uax29_tables&,
    const std::vector<std::uint32_t>&,
    std::size_t);

laplace_uax29_status EmitSegmentation(
    const laplace_uax29_tables& tables,
    const DecodedText& text,
    laplace_uax29_boundary_kind kind,
    BoundaryFn next,
    laplace_uax29_emit_fn emit,
    void* emit_state,
    std::uint64_t& emitted) {
    emitted = 0u;
    std::size_t start = 0u;
    while (start < text.codepoints.size()) {
        const std::size_t end = next(tables, text.codepoints, start);
        if (end <= start || end > text.codepoints.size()) return LAPLACE_UAX29_SOURCE_SYNTAX_INVALID;
        laplace_uax29_span span{};
        span.byte_start = text.byte_offsets[start];
        span.byte_end = text.byte_offsets[end];
        span.codepoint_start = static_cast<std::uint64_t>(start);
        span.codepoint_end = static_cast<std::uint64_t>(end);
        span.kind = static_cast<std::uint8_t>(kind);
        if (emit(emit_state, &span) != 0) return LAPLACE_UAX29_EMIT_FAILURE;
        ++emitted;
        start = end;
    }
    return LAPLACE_UAX29_OK;
}

}  // namespace

extern "C" laplace_uax29_status laplace_uax29_tables_create(
    const laplace_unicode_source_bundle* bundle,
    laplace_uax29_tables** output) {
    if (bundle == nullptr || output == nullptr) return LAPLACE_UAX29_INVALID_ARGUMENT;
    *output = nullptr;
    laplace_uax29_tables* tables = new (std::nothrow) laplace_uax29_tables{};
    if (tables == nullptr) return LAPLACE_UAX29_MEMORY_FAILURE;
    try {
        tables->grapheme.assign(UnicodePopulation, GB_OTHER);
        tables->word.assign(UnicodePopulation, WB_OTHER);
        tables->sentence.assign(UnicodePopulation, SB_OTHER);
        tables->incb.assign(UnicodePopulation, INCB_NONE);
        tables->extended_pictographic.assign(UnicodePopulation, 0u);
    } catch (...) {
        delete tables;
        return LAPLACE_UAX29_MEMORY_FAILURE;
    }
    const bool ok =
        LoadSimpleProperty(bundle, "ucd/auxiliary/GraphemeBreakProperty.txt", tables->grapheme, MapGrapheme) &&
        LoadSimpleProperty(bundle, "ucd/auxiliary/WordBreakProperty.txt", tables->word, MapWord) &&
        LoadSimpleProperty(bundle, "ucd/auxiliary/SentenceBreakProperty.txt", tables->sentence, MapSentence) &&
        LoadIncb(bundle, tables->incb) &&
        LoadExtendedPictographic(bundle, tables->extended_pictographic);
    if (!ok) {
        delete tables;
        return LAPLACE_UAX29_SOURCE_SYNTAX_INVALID;
    }
    *output = tables;
    return LAPLACE_UAX29_OK;
}

extern "C" void laplace_uax29_tables_destroy(laplace_uax29_tables** tables) {
    if (tables == nullptr) return;
    delete *tables;
    *tables = nullptr;
}

extern "C" laplace_uax29_status laplace_uax29_segment(
    const laplace_uax29_tables* tables,
    const std::uint8_t* utf8,
    std::size_t byte_count,
    laplace_uax29_boundary_kind kind,
    laplace_uax29_emit_fn emit,
    void* emit_state,
    laplace_uax29_summary* summary) {
    if (tables == nullptr || emit == nullptr || summary == nullptr ||
        (utf8 == nullptr && byte_count != 0u)) return LAPLACE_UAX29_INVALID_ARGUMENT;
    *summary = laplace_uax29_summary{};
    summary->input_bytes = static_cast<std::uint64_t>(byte_count);
    if (byte_count == 0u) return LAPLACE_UAX29_OK;

    DecodedText decoded;
    try {
        if (!DecodeUtf8(utf8, byte_count, decoded)) return LAPLACE_UAX29_INVALID_UTF8;
    } catch (...) {
        return LAPLACE_UAX29_MEMORY_FAILURE;
    }
    summary->codepoint_count = static_cast<std::uint64_t>(decoded.codepoints.size());
    std::uint64_t emitted = 0u;
    laplace_uax29_status status = LAPLACE_UAX29_INVALID_ARGUMENT;
    switch (kind) {
        case LAPLACE_UAX29_GRAPHEME:
            status = EmitSegmentation(*tables, decoded, kind, GraphemeNext, emit, emit_state, emitted);
            summary->grapheme_count = emitted;
            break;
        case LAPLACE_UAX29_WORD:
            status = EmitSegmentation(*tables, decoded, kind, WordNext, emit, emit_state, emitted);
            summary->word_count = emitted;
            break;
        case LAPLACE_UAX29_SENTENCE:
            status = EmitSegmentation(*tables, decoded, kind, SentenceNext, emit, emit_state, emitted);
            summary->sentence_count = emitted;
            break;
        default:
            return LAPLACE_UAX29_INVALID_ARGUMENT;
    }
    return status;
}
