#include "laplace/uax29.h"

#include <cstddef>
#include <cstdint>
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

struct laplace_uax29_atom_table_builder {
    laplace_uax29_tables* tables = nullptr;
    std::uint32_t next_position = 0u;
};

namespace {

constexpr std::uint32_t UnicodePopulation = LAPLACE_UNICODE_ROOT_POPULATION;

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

bool MapGrapheme(const std::string_view value, std::uint8_t& mapped) {
    struct Pair { std::string_view name; std::uint8_t value; };
    static constexpr Pair values[] = {
        {"Other", GB_OTHER}, {"CR", GB_CR}, {"LF", GB_LF},
        {"Control", GB_CONTROL}, {"Extend", GB_EXTEND}, {"ZWJ", GB_ZWJ},
        {"Regional_Indicator", GB_REGIONAL_INDICATOR},
        {"Prepend", GB_PREPEND}, {"SpacingMark", GB_SPACINGMARK},
        {"L", GB_L}, {"V", GB_V}, {"T", GB_T}, {"LV", GB_LV},
        {"LVT", GB_LVT}
    };
    for (const Pair& pair : values) {
        if (value == pair.name) {
            mapped = pair.value;
            return true;
        }
    }
    return false;
}

bool MapWord(const std::string_view value, std::uint8_t& mapped) {
    struct Pair { std::string_view name; std::uint8_t value; };
    static constexpr Pair values[] = {
        {"Other", WB_OTHER}, {"CR", WB_CR}, {"LF", WB_LF},
        {"Newline", WB_NEWLINE}, {"Extend", WB_EXTEND}, {"ZWJ", WB_ZWJ},
        {"Regional_Indicator", WB_REGIONAL_INDICATOR}, {"Format", WB_FORMAT},
        {"Katakana", WB_KATAKANA}, {"Hebrew_Letter", WB_HEBREW_LETTER},
        {"ALetter", WB_ALETTER}, {"Single_Quote", WB_SINGLE_QUOTE},
        {"Double_Quote", WB_DOUBLE_QUOTE}, {"MidNumLet", WB_MIDNUMLET},
        {"MidLetter", WB_MIDLETTER}, {"MidNum", WB_MIDNUM},
        {"Numeric", WB_NUMERIC}, {"ExtendNumLet", WB_EXTENDNUMLET},
        {"WSegSpace", WB_WSEGSPACE}
    };
    for (const Pair& pair : values) {
        if (value == pair.name) {
            mapped = pair.value;
            return true;
        }
    }
    return false;
}

bool MapSentence(const std::string_view value, std::uint8_t& mapped) {
    struct Pair { std::string_view name; std::uint8_t value; };
    static constexpr Pair values[] = {
        {"Other", SB_OTHER}, {"CR", SB_CR}, {"LF", SB_LF},
        {"Extend", SB_EXTEND}, {"Sep", SB_SEP}, {"Format", SB_FORMAT},
        {"Sp", SB_SP}, {"Lower", SB_LOWER}, {"Upper", SB_UPPER},
        {"OLetter", SB_OLETTER}, {"Numeric", SB_NUMERIC},
        {"ATerm", SB_ATERM}, {"SContinue", SB_SCONTINUE},
        {"STerm", SB_STERM}, {"Close", SB_CLOSE}
    };
    for (const Pair& pair : values) {
        if (value == pair.name) {
            mapped = pair.value;
            return true;
        }
    }
    return false;
}

bool MapIncb(const std::string_view value, std::uint8_t& mapped) {
    if (value == "None") {
        mapped = INCB_NONE;
        return true;
    }
    if (value == "Extend") {
        mapped = INCB_EXTEND;
        return true;
    }
    if (value == "Linker") {
        mapped = INCB_LINKER;
        return true;
    }
    if (value == "Consonant") {
        mapped = INCB_CONSONANT;
        return true;
    }
    return false;
}

bool ReadAsciiProperty(
    const laplace_unicode_atom_field& field,
    const std::uint16_t expected_field_id,
    std::string_view& value) {
    if (field.field_id != expected_field_id ||
        field.payload_kind != LAPLACE_UNICODE_PAYLOAD_ASCII_PROPERTY ||
        field.flags != 0u || field.payload == nullptr || field.payload_bytes == 0u) {
        return false;
    }
    value = std::string_view(
        reinterpret_cast<const char*>(field.payload),
        static_cast<std::size_t>(field.payload_bytes));
    return true;
}

bool ReadBoolean(
    const laplace_unicode_atom_field& field,
    const std::uint16_t expected_field_id,
    std::uint8_t& value) {
    if (field.field_id != expected_field_id ||
        field.payload_kind != LAPLACE_UNICODE_PAYLOAD_BOOLEAN ||
        field.flags != 0u || field.payload == nullptr || field.payload_bytes != 1u ||
        field.payload[0] > 1u) {
        return false;
    }
    value = field.payload[0];
    return true;
}

laplace_uax29_status AllocateTables(laplace_uax29_tables** output) {
    if (output == nullptr) {
        return LAPLACE_UAX29_INVALID_ARGUMENT;
    }
    *output = nullptr;
    laplace_uax29_tables* tables = new (std::nothrow) laplace_uax29_tables{};
    if (tables == nullptr) {
        return LAPLACE_UAX29_MEMORY_FAILURE;
    }
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
    *output = tables;
    return LAPLACE_UAX29_OK;
}

}  // namespace

extern "C" laplace_uax29_status laplace_uax29_atom_table_builder_create(
    laplace_uax29_atom_table_builder** builder) {
    if (builder == nullptr) {
        return LAPLACE_UAX29_INVALID_ARGUMENT;
    }
    *builder = nullptr;
    auto* created = new (std::nothrow) laplace_uax29_atom_table_builder{};
    if (created == nullptr) {
        return LAPLACE_UAX29_MEMORY_FAILURE;
    }
    const laplace_uax29_status status = AllocateTables(&created->tables);
    if (status != LAPLACE_UAX29_OK) {
        delete created;
        return status;
    }
    *builder = created;
    return LAPLACE_UAX29_OK;
}

extern "C" laplace_uax29_status laplace_uax29_atom_table_builder_consume(
    laplace_uax29_atom_table_builder* builder,
    const laplace_unicode_atom_record_view* records,
    const std::size_t record_count) {
    if (builder == nullptr || builder->tables == nullptr || records == nullptr ||
        record_count == 0u ||
        record_count > static_cast<std::size_t>(UnicodePopulation - builder->next_position)) {
        return LAPLACE_UAX29_INVALID_ARGUMENT;
    }

    for (std::size_t index = 0u; index < record_count; ++index) {
        const laplace_unicode_atom_record& record = records[index].value;
        const std::uint32_t expected = builder->next_position;
        if (record.codepoint_position != expected) {
            return LAPLACE_UAX29_SOURCE_SYNTAX_INVALID;
        }

        std::string_view grapheme;
        std::string_view word;
        std::string_view sentence;
        std::string_view incb;
        std::uint8_t pictographic = 0u;
        std::uint8_t grapheme_value = 0u;
        std::uint8_t word_value = 0u;
        std::uint8_t sentence_value = 0u;
        std::uint8_t incb_value = 0u;

        if (!ReadAsciiProperty(record.fields[20], 21u, grapheme) ||
            !ReadAsciiProperty(record.fields[21], 22u, word) ||
            !ReadAsciiProperty(record.fields[22], 23u, sentence) ||
            !ReadAsciiProperty(record.fields[23], 24u, incb) ||
            !ReadBoolean(record.fields[24], 25u, pictographic) ||
            !MapGrapheme(grapheme, grapheme_value) ||
            !MapWord(word, word_value) ||
            !MapSentence(sentence, sentence_value) ||
            !MapIncb(incb, incb_value)) {
            return LAPLACE_UAX29_SOURCE_SYNTAX_INVALID;
        }

        builder->tables->grapheme[expected] = grapheme_value;
        builder->tables->word[expected] = word_value;
        builder->tables->sentence[expected] = sentence_value;
        builder->tables->incb[expected] = incb_value;
        builder->tables->extended_pictographic[expected] = pictographic;
        ++builder->next_position;
    }
    return LAPLACE_UAX29_OK;
}

extern "C" laplace_uax29_status laplace_uax29_atom_table_builder_finish(
    laplace_uax29_atom_table_builder** builder,
    laplace_uax29_tables** tables) {
    if (builder == nullptr || *builder == nullptr || tables == nullptr) {
        return LAPLACE_UAX29_INVALID_ARGUMENT;
    }
    *tables = nullptr;
    if ((*builder)->tables == nullptr ||
        (*builder)->next_position != UnicodePopulation) {
        return LAPLACE_UAX29_SOURCE_INCOMPLETE;
    }
    *tables = (*builder)->tables;
    (*builder)->tables = nullptr;
    delete *builder;
    *builder = nullptr;
    return LAPLACE_UAX29_OK;
}

extern "C" void laplace_uax29_atom_table_builder_destroy(
    laplace_uax29_atom_table_builder** builder) {
    if (builder == nullptr || *builder == nullptr) {
        return;
    }
    delete (*builder)->tables;
    (*builder)->tables = nullptr;
    delete *builder;
    *builder = nullptr;
}
