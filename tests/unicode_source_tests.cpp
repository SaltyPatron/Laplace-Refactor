#include "laplace/unicode_root.h"
#include "laplace/contract/unicode-source-manifest.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

const fs::path& SourceRoot() {
    static const fs::path root = [] {
        const char* configured = std::getenv("LAPLACE_UNICODE_SOURCE_ROOT");
        return fs::path(configured != nullptr && configured[0] != '\0'
                            ? configured
                            : "/vault/Data/UCD/Public/UCD/latest");
    }();
    return root;
}

const fs::path& CollationTestRoot() {
    static const fs::path root = [] {
        const char* configured = std::getenv(
            "LAPLACE_UNICODE_COLLATION_TEST_ROOT");
        if (configured != nullptr && configured[0] != '\0') {
            return fs::path(configured);
        }
#if defined(LAPLACE_UNICODE_COLLATION_TEST_ROOT)
        return fs::path(LAPLACE_UNICODE_COLLATION_TEST_ROOT);
#else
        return fs::path{};
#endif
    }();
    return root;
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::array<char, 64> pattern{};
        const std::string value = "/tmp/laplace-unicode-source-test-XXXXXX";
        std::memcpy(pattern.data(), value.c_str(), value.size() + 1u);
        char* created = ::mkdtemp(pattern.data());
        if (created != nullptr) {
            path_ = created;
        }
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

std::string Hex(const laplace_digest256& value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t byte : value.bytes) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

const laplace_unicode_atom_field* CoreField(
    const laplace_unicode_core_record_view& view,
    std::uint16_t field_id) {
    for (const auto& field : view.fields) {
        if (field.field_id == field_id) {
            return &field;
        }
    }
    return nullptr;
}

std::string FieldText(const laplace_unicode_atom_field* field) {
    if (field == nullptr || field->payload_bytes == 0u) {
        return {};
    }
    return std::string(
        reinterpret_cast<const char*>(field->payload), field->payload_bytes);
}

std::uint16_t ReadU16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

std::vector<std::string> FieldAsciiSet(
    const laplace_unicode_atom_field* field) {
    std::vector<std::string> result;
    if (field == nullptr || field->payload_bytes < 4u) {
        return result;
    }
    std::size_t offset = 4u;
    const std::uint32_t count = ReadU32(field->payload);
    for (std::uint32_t index = 0u; index < count; ++index) {
        if (offset + 2u > field->payload_bytes) {
            return {};
        }
        const std::uint16_t bytes = ReadU16(field->payload + offset);
        offset += 2u;
        if (offset + bytes > field->payload_bytes) {
            return {};
        }
        result.emplace_back(
            reinterpret_cast<const char*>(field->payload + offset), bytes);
        offset += bytes;
    }
    return offset == field->payload_bytes ? result
                                          : std::vector<std::string>{};
}

struct NormalizationValue {
    std::string property;
    std::uint8_t kind;
    std::uint32_t count;
    std::vector<std::uint8_t> value;
};

std::vector<NormalizationValue> FieldNormalizationValues(
    const laplace_unicode_atom_field* field) {
    std::vector<NormalizationValue> result;
    if (field == nullptr || field->payload_bytes < 4u) {
        return result;
    }
    std::size_t offset = 4u;
    const std::uint32_t count = ReadU32(field->payload);
    for (std::uint32_t index = 0u; index < count; ++index) {
        if (offset + 8u > field->payload_bytes) {
            return {};
        }
        const std::uint16_t key_bytes = ReadU16(field->payload + offset);
        const std::uint8_t kind = field->payload[offset + 2u];
        const std::uint8_t reserved = field->payload[offset + 3u];
        const std::uint32_t value_count = ReadU32(field->payload + offset + 4u);
        offset += 8u;
        std::size_t value_bytes = 0u;
        if (kind == LAPLACE_UNICODE_NORMALIZATION_ASCII_PROPERTY_VALUE) {
            value_bytes = value_count;
        } else if (kind == LAPLACE_UNICODE_NORMALIZATION_POSITION_SEQUENCE) {
            value_bytes = static_cast<std::size_t>(value_count) * 4u;
        } else if (kind != LAPLACE_UNICODE_NORMALIZATION_BINARY_TRUE &&
                   kind != LAPLACE_UNICODE_NORMALIZATION_EMPTY_POSITION_SEQUENCE) {
            return {};
        }
        if (reserved != 0u || offset + key_bytes + value_bytes >
                field->payload_bytes) {
            return {};
        }
        NormalizationValue value;
        value.property.assign(
            reinterpret_cast<const char*>(field->payload + offset), key_bytes);
        value.kind = kind;
        value.count = value_count;
        offset += key_bytes;
        value.value.assign(
            field->payload + offset, field->payload + offset + value_bytes);
        offset += value_bytes;
        result.push_back(std::move(value));
    }
    return offset == field->payload_bytes ? result
                                          : std::vector<NormalizationValue>{};
}

const NormalizationValue* FindNormalizationValue(
    const std::vector<NormalizationValue>& values,
    const std::string& property) {
    const auto found = std::find_if(
        values.begin(), values.end(), [&](const NormalizationValue& value) {
            return value.property == property;
        });
    return found == values.end() ? nullptr : &*found;
}

struct DucetCalculation {
    std::vector<std::uint32_t> normalized;
    std::vector<laplace_unicode_collation_element> elements;
    std::vector<std::uint8_t> key;
    std::uint8_t provenance{};
};

DucetCalculation CalculateDucet(
    const laplace_unicode_ducet_table* table,
    const laplace_unicode_core_table* core,
    const std::vector<std::uint32_t>& sequence,
    std::uint8_t alternate_handling) {
    std::uint32_t normalized_count = 0u;
    std::uint32_t element_count = 0u;
    std::size_t key_bytes = 0u;
    EXPECT_EQ(laplace_unicode_ducet_sort_key_measure(
                  table, core, sequence.data(),
                  static_cast<std::uint32_t>(sequence.size()),
                  alternate_handling, &normalized_count, &element_count,
                  &key_bytes),
              LAPLACE_UNICODE_OK);
    DucetCalculation result;
    result.normalized.resize(normalized_count);
    result.elements.resize(element_count);
    result.key.resize(key_bytes);
    std::size_t written = 0u;
    EXPECT_EQ(laplace_unicode_ducet_sort_key_calculate(
                  table, core, sequence.data(),
                  static_cast<std::uint32_t>(sequence.size()),
                  alternate_handling, result.normalized.data(),
                  static_cast<std::uint32_t>(result.normalized.size()),
                  result.elements.data(),
                  static_cast<std::uint32_t>(result.elements.size()),
                  result.key.data(), result.key.size(),
                  &result.provenance, &written),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(written, result.key.size());
    return result;
}

int CompareBytes(const std::uint8_t* left, std::uint32_t left_bytes,
                 const std::uint8_t* right, std::uint32_t right_bytes) {
    const std::size_t common = std::min<std::size_t>(left_bytes, right_bytes);
    const int compared = common == 0u ? 0 : std::memcmp(left, right, common);
    if (compared != 0) {
        return compared;
    }
    if (left_bytes == right_bytes) {
        return 0;
    }
    return left_bytes < right_bytes ? -1 : 1;
}

int CompareLupPositions(std::uint32_t left, std::uint32_t right) {
    std::array<std::uint8_t, 4> left_bytes{};
    std::array<std::uint8_t, 4> right_bytes{};
    std::size_t left_count = 0u;
    std::size_t right_count = 0u;
    EXPECT_EQ(laplace_unicode_position_encode(
                  left, left_bytes.data(), &left_count),
              LAPLACE_IDENTITY_OK);
    EXPECT_EQ(laplace_unicode_position_encode(
                  right, right_bytes.data(), &right_count),
              LAPLACE_IDENTITY_OK);
    const std::size_t common = std::min(left_count, right_count);
    const int compared = common == 0u
        ? 0
        : std::memcmp(left_bytes.data(), right_bytes.data(), common);
    if (compared != 0) {
        return compared;
    }
    if (left_count == right_count) {
        return 0;
    }
    return left_count < right_count ? -1 : 1;
}

bool ParseCollationSequence(std::string_view line,
                            std::vector<std::uint32_t>& sequence) {
    const std::size_t semicolon = line.find(';');
    if (semicolon == std::string_view::npos) {
        return false;
    }
    line = line.substr(0u, semicolon);
    sequence.clear();
    while (!line.empty()) {
        while (!line.empty() &&
               (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1u);
        }
        if (line.empty()) {
            break;
        }
        const std::size_t end = line.find_first_of(" \t");
        const std::string_view token = end == std::string_view::npos
            ? line
            : line.substr(0u, end);
        std::uint32_t position = 0u;
        const auto parsed = std::from_chars(
            token.data(), token.data() + token.size(), position, 16);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != token.data() + token.size() ||
            position >= LAPLACE_UNICODE_ROOT_POPULATION) {
            return false;
        }
        sequence.push_back(position);
        if (end == std::string_view::npos) {
            break;
        }
        line.remove_prefix(end + 1u);
    }
    return !sequence.empty();
}

bool VerifyFullCollationSuite(
    const fs::path& path, const laplace_unicode_ducet_table* table,
    const laplace_unicode_core_table* core,
    std::uint8_t alternate_handling, std::uint64_t expected_cases) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return false;
    }
    bool uca_version = false;
    bool ucd_version = false;
    std::uint64_t cases = 0u;
    std::uint64_t line_ordinal = 0u;
    std::vector<std::uint8_t> previous_key;
    std::vector<std::uint32_t> sequence;
    std::string line;
    while (std::getline(input, line)) {
        ++line_ordinal;
        if (line.find("# UCA Version: 17.0.0") != std::string::npos) {
            uca_version = true;
        }
        if (line.find("# UCD Version: 17.0.0") != std::string::npos) {
            ucd_version = true;
        }
        const std::string_view view(line);
        const std::size_t first = view.find_first_not_of(" \t\r");
        if (first == std::string_view::npos || view[first] == '#') {
            continue;
        }
        if (!ParseCollationSequence(view.substr(first), sequence)) {
            ADD_FAILURE() << path << ':' << line_ordinal
                          << " has invalid conformance input syntax";
            return false;
        }
        const DucetCalculation current = CalculateDucet(
            table, core, sequence, alternate_handling);
        if (current.key.empty()) {
            ADD_FAILURE() << path << ':' << line_ordinal
                          << " did not produce a complete key";
            return false;
        }
        if (!previous_key.empty() && std::lexicographical_compare(
                current.key.begin(), current.key.end(),
                previous_key.begin(), previous_key.end())) {
            ADD_FAILURE() << path << ':' << line_ordinal
                          << " sorts before the preceding official case";
            return false;
        }
        previous_key = current.key;
        ++cases;
    }
    EXPECT_TRUE(uca_version) << path;
    EXPECT_TRUE(ucd_version) << path;
    EXPECT_EQ(cases, expected_cases) << path;
    return uca_version && ucd_version && cases == expected_cases;
}

struct FullCaseMapping {
    std::array<std::vector<std::uint32_t>, 3> mappings;
    std::vector<std::string> conditions;
};

std::vector<FullCaseMapping> FieldFullCaseMappings(
    const laplace_unicode_atom_field* field) {
    std::vector<FullCaseMapping> result;
    if (field == nullptr || field->payload_bytes < 4u) {
        return result;
    }
    std::size_t offset = 4u;
    const std::uint32_t count = ReadU32(field->payload);
    for (std::uint32_t index = 0u; index < count; ++index) {
        if (offset + 16u > field->payload_bytes) {
            return {};
        }
        const std::uint16_t condition_count = ReadU16(field->payload + offset);
        if (ReadU16(field->payload + offset + 2u) != 0u) {
            return {};
        }
        std::array<std::uint32_t, 3> mapping_counts{{
            ReadU32(field->payload + offset + 4u),
            ReadU32(field->payload + offset + 8u),
            ReadU32(field->payload + offset + 12u)}};
        offset += 16u;
        FullCaseMapping mapping;
        for (std::size_t role = 0u; role < mapping_counts.size(); ++role) {
            for (std::uint32_t item = 0u; item < mapping_counts[role]; ++item) {
                if (offset + 4u > field->payload_bytes) {
                    return {};
                }
                mapping.mappings[role].push_back(ReadU32(field->payload + offset));
                offset += 4u;
            }
        }
        for (std::uint16_t condition = 0u;
             condition < condition_count; ++condition) {
            if (offset + 2u > field->payload_bytes) {
                return {};
            }
            const std::uint16_t condition_bytes = ReadU16(field->payload + offset);
            offset += 2u;
            if (offset + condition_bytes > field->payload_bytes) {
                return {};
            }
            mapping.conditions.emplace_back(
                reinterpret_cast<const char*>(field->payload + offset),
                condition_bytes);
            offset += condition_bytes;
        }
        result.push_back(std::move(mapping));
    }
    return offset == field->payload_bytes ? result
                                          : std::vector<FullCaseMapping>{};
}

bool SourceAvailable() {
    return fs::is_directory(SourceRoot()) &&
        fs::is_regular_file(SourceRoot() / "ucd/UnicodeData.txt");
}

void CopyPinnedSourceSet(const fs::path& destination) {
    for (const auto& source : laplace_unicode_generated_sources) {
        const fs::path relative(source.relative_path);
        const fs::path target = destination / relative;
        fs::create_directories(target.parent_path());
        fs::copy_file(SourceRoot() / relative, target,
                      fs::copy_options::overwrite_existing);
    }
}

TEST(UnicodeSource, RejectsMissingRootAndInvalidArguments) {
    laplace_unicode_source_receipt receipt{};
    EXPECT_EQ(laplace_unicode_source_verify(nullptr, &receipt),
              LAPLACE_UNICODE_INVALID_ARGUMENT);
    EXPECT_EQ(laplace_unicode_source_verify("", &receipt),
              LAPLACE_UNICODE_INVALID_ARGUMENT);
    EXPECT_EQ(laplace_unicode_source_verify(
                  "/this/path/is/intentionally/not/a/unicode/source/root", &receipt),
              LAPLACE_UNICODE_SOURCE_ROOT_INVALID);
    EXPECT_EQ(receipt.status, LAPLACE_UNICODE_SOURCE_ROOT_INVALID);
}

TEST(UnicodeSource, VerifiesPinnedUnicode17CorpusAndReplaysBitExactly) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_receipt first{};
    laplace_unicode_source_receipt replay{};
    ASSERT_EQ(laplace_unicode_source_verify(SourceRoot().c_str(), &first),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(laplace_unicode_source_verify(SourceRoot().c_str(), &replay),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(first.status, LAPLACE_UNICODE_OK);
    EXPECT_EQ(first.verified_file_count, LAPLACE_UNICODE_GENERATED_SOURCE_COUNT);
    EXPECT_EQ(first.total_source_bytes, UINT64_C(20978722));
    EXPECT_EQ(std::memcmp(&first, &replay, sizeof(first)), 0);
    EXPECT_TRUE(SameDigest(first.source_fingerprint, replay.source_fingerprint));
    EXPECT_TRUE(SameDigest(first.recipe_fingerprint, replay.recipe_fingerprint));
}

TEST(UnicodeSource, RetainsTheExactVerifiedBytesForProducerParsing) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt retained{};
    laplace_unicode_source_receipt verified{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &retained),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(bundle, nullptr);
    ASSERT_EQ(laplace_unicode_source_verify(SourceRoot().c_str(), &verified),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(std::memcmp(&retained, &verified, sizeof(retained)), 0);

    laplace_unicode_source_file_view unicode_data{};
    ASSERT_EQ(laplace_unicode_source_bundle_file(
                  bundle, "ucd/UnicodeData.txt", &unicode_data),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(unicode_data.byte_count, UINT64_C(2198209));
    ASSERT_NE(unicode_data.bytes, nullptr);
    static constexpr std::array<std::uint8_t, 5> Prefix{{
        '0', '0', '0', '0', ';'}};
    EXPECT_EQ(std::memcmp(
                  unicode_data.bytes, Prefix.data(), Prefix.size()),
              0);

    EXPECT_EQ(laplace_unicode_source_bundle_file(
                  bundle, "ucd/not-authoritative.txt", &unicode_data),
              LAPLACE_UNICODE_SOURCE_FILE_INVALID);
    laplace_unicode_source_bundle_close(&bundle);
    EXPECT_EQ(bundle, nullptr);
}

TEST(UnicodeCoreProperties, ParsesCompleteSourceAndCanonicalizesPayloads) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* table = nullptr;
    laplace_unicode_core_summary summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(bundle, &table, &summary),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(summary.status, LAPLACE_UNICODE_OK);
    EXPECT_TRUE(SameDigest(summary.source_fingerprint, source.source_fingerprint));
    EXPECT_TRUE(SameDigest(summary.recipe_fingerprint, source.recipe_fingerprint));
    EXPECT_EQ(summary.unicode_data_row_count, 40575u);
    EXPECT_EQ(summary.unicode_data_range_count, 40555u);
    EXPECT_EQ(summary.bidi_missing_rule_count, 24u);
    EXPECT_GT(summary.bidi_range_count, 1u);
    EXPECT_EQ(summary.explicit_position_count, UINT64_C(299382));
    EXPECT_EQ(summary.canonical_decomposition_count, UINT64_C(2081));
    EXPECT_EQ(summary.compatibility_decomposition_count, UINT64_C(3833));
    EXPECT_EQ(summary.simple_case_mapping_position_count, UINT64_C(2989));
    struct ExpectedFieldStatistics {
        std::uint16_t field_id;
        std::uint64_t source_rows;
        std::uint64_t explicit_positions;
        std::uint64_t memberships;
    };
    static constexpr std::array<ExpectedFieldStatistics, 26> FieldStatistics{{
        {1u, 40575u, 299382u, 299382u},
        {2u, 40575u, 299382u, 299382u},
        {3u, 2323u, 301169u, 301169u},
        {4u, 128u, 128u, 128u},
        {5u, 428u, 428u, 428u},
        {6u, 2081u, 2081u, 2081u},
        {7u, 3833u, 3833u, 3833u},
        {8u, 1940u, 1940u, 1940u},
        {9u, 2989u, 2989u, 4502u},
        {10u, 119u, 112u, 119u},
        {11u, 1618u, 1585u, 1618u},
        {12u, 346u, 303808u, 303808u},
        {13u, 2678u, 355548u, 355548u},
        {14u, 2287u, 159866u, 159866u},
        {15u, 206u, 669u, 1660u},
        {16u, 1744u, 122697u, 225784u},
        {17u, 12840u, 163568u, 925182u},
        {18u, 16098u, 22417u, 96031u},
        {19u, 74u, 1120u, 1120u},
        {20u, 804u, 11529u, 11529u},
        {21u, 1429u, 18101u, 18101u},
        {22u, 1432u, 37958u, 37958u},
        {23u, 2930u, 149949u, 149949u},
        {24u, 505u, 3148u, 3148u},
        {25u, 451u, 2848u, 2848u},
        {26u, 3654u, 356930u, 356930u}}};
    for (const ExpectedFieldStatistics& expected : FieldStatistics) {
        const std::size_t index = expected.field_id - 1u;
        EXPECT_EQ(summary.field_source_row_counts[index], expected.source_rows)
            << "field " << expected.field_id;
        EXPECT_EQ(summary.field_explicit_position_counts[index],
                  expected.explicit_positions)
            << "field " << expected.field_id;
        EXPECT_EQ(summary.field_membership_counts[index], expected.memberships)
            << "field " << expected.field_id;
    }
    const laplace_digest256 zero_digest{};
    EXPECT_FALSE(SameDigest(
        summary.complete_property_fingerprint, zero_digest));

    laplace_unicode_core_record_view letter{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x41u, &letter),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(letter.position_class, LAPLACE_UNICODE_ASSIGNED_SCALAR);
    EXPECT_EQ(FieldText(CoreField(letter, 1u)), "Uppercase_Letter");
    ASSERT_NE(CoreField(letter, 2u), nullptr);
    ASSERT_EQ(CoreField(letter, 2u)->payload_bytes, 1u);
    EXPECT_EQ(CoreField(letter, 2u)->payload[0], 0u);
    EXPECT_EQ(FieldText(CoreField(letter, 3u)), "Left_To_Right");
    ASSERT_NE(CoreField(letter, 9u), nullptr);
    const std::array<std::uint8_t, 12> expected_lower{{
        1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0x61u, 0u, 0u, 0u}};
    ASSERT_EQ(CoreField(letter, 9u)->payload_bytes, expected_lower.size());
    EXPECT_EQ(std::memcmp(
                  CoreField(letter, 9u)->payload,
                  expected_lower.data(), expected_lower.size()),
              0);
    for (std::uint16_t field_id = 1u;
         field_id <= LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field_id) {
        const laplace_unicode_atom_field* field = CoreField(letter, field_id);
        std::uint8_t expected_kind = 0u;
        ASSERT_NE(field, nullptr);
        ASSERT_EQ(laplace_unicode_atom_field_payload_kind(
                      field_id, &expected_kind),
                  LAPLACE_UNICODE_OK);
        EXPECT_EQ(field->payload_kind, expected_kind) << "field " << field_id;
    }
    EXPECT_EQ(FieldText(CoreField(letter, 12u)), "Basic_Latin");
    EXPECT_EQ(FieldText(CoreField(letter, 13u)), "Narrow");
    EXPECT_EQ(FieldText(CoreField(letter, 14u)), "Latin");
    EXPECT_EQ(FieldAsciiSet(CoreField(letter, 15u)),
              std::vector<std::string>{"Latin"});
    EXPECT_EQ(FieldAsciiSet(CoreField(letter, 16u)),
              (std::vector<std::string>{"ASCII_Hex_Digit", "Hex_Digit"}));
    const std::vector<std::string> derived_letter =
        FieldAsciiSet(CoreField(letter, 17u));
    EXPECT_NE(std::find(derived_letter.begin(), derived_letter.end(),
                        "Alphabetic"),
              derived_letter.end());
    EXPECT_NE(std::find(derived_letter.begin(), derived_letter.end(),
                        "Uppercase"),
              derived_letter.end());
    EXPECT_EQ(FieldText(CoreField(letter, 22u)), "ALetter");
    EXPECT_EQ(FieldText(CoreField(letter, 23u)), "Upper");
    EXPECT_EQ(FieldText(CoreField(letter, 26u)), "Alphabetic");

    laplace_unicode_core_record_view bracket{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x28u, &bracket),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(CoreField(bracket, 4u)->payload_bytes, 5u);
    EXPECT_EQ(ReadU32(CoreField(bracket, 4u)->payload), 0x29u);
    EXPECT_EQ(CoreField(bracket, 4u)->payload[4], 'o');
    laplace_unicode_core_record_view mirrored{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x3cu, &mirrored),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(CoreField(mirrored, 5u)->payload_bytes, 4u);
    EXPECT_EQ(ReadU32(CoreField(mirrored, 5u)->payload), 0x3eu);

    laplace_unicode_core_record_view context_casing{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x0307u,
                                                 &context_casing),
              LAPLACE_UNICODE_OK);
    const std::vector<FullCaseMapping> context_mappings =
        FieldFullCaseMappings(CoreField(context_casing, 10u));
    ASSERT_EQ(context_mappings.size(), 3u);
    EXPECT_NE(std::find_if(
                  context_mappings.begin(), context_mappings.end(),
                  [](const FullCaseMapping& mapping) {
                      return mapping.mappings[0].empty() &&
                          mapping.conditions ==
                              std::vector<std::string>{"After_I", "az"};
                  }),
              context_mappings.end());
    ASSERT_EQ(CoreField(letter, 11u)->payload_bytes, 16u);
    EXPECT_EQ(ReadU32(CoreField(letter, 11u)->payload), 1u);
    EXPECT_EQ(CoreField(letter, 11u)->payload[4], 'C');
    EXPECT_EQ(ReadU32(CoreField(letter, 11u)->payload + 8u), 1u);
    EXPECT_EQ(ReadU32(CoreField(letter, 11u)->payload + 12u), 0x61u);

    laplace_unicode_core_record_view multi_script{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x30fcu,
                                                 &multi_script),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(FieldText(CoreField(multi_script, 14u)), "Common");
    EXPECT_EQ(FieldAsciiSet(CoreField(multi_script, 15u)),
              (std::vector<std::string>{"Hiragana", "Katakana"}));

    laplace_unicode_core_record_view soft_hyphen{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x00adu,
                                                 &soft_hyphen),
              LAPLACE_UNICODE_OK);
    const std::vector<NormalizationValue> soft_hyphen_normalization =
        FieldNormalizationValues(CoreField(soft_hyphen, 18u));
    const NormalizationValue* full_fold = FindNormalizationValue(
        soft_hyphen_normalization, "NFKC_Casefold");
    const NormalizationValue* simple_fold = FindNormalizationValue(
        soft_hyphen_normalization, "NFKC_Simple_Casefold");
    ASSERT_NE(full_fold, nullptr);
    ASSERT_NE(simple_fold, nullptr);
    EXPECT_EQ(full_fold->kind,
              LAPLACE_UNICODE_NORMALIZATION_EMPTY_POSITION_SEQUENCE);
    EXPECT_EQ(simple_fold->kind,
              LAPLACE_UNICODE_NORMALIZATION_EMPTY_POSITION_SEQUENCE);
    ASSERT_EQ(CoreField(soft_hyphen, 19u)->payload_bytes, 1u);
    EXPECT_EQ(CoreField(soft_hyphen, 19u)->payload[0], 0u);

    laplace_unicode_core_record_view excluded{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x0340u, &excluded),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(CoreField(excluded, 19u)->payload[0], 1u);

    laplace_unicode_core_record_view hangul{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0xac00u, &hangul),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(FieldText(CoreField(hangul, 20u)), "LV_Syllable");
    EXPECT_EQ(FieldText(CoreField(hangul, 21u)), "LV");

    laplace_unicode_core_record_view indic{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x0951u, &indic),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(FieldText(CoreField(indic, 24u)), "Extend");

    laplace_unicode_core_record_view emoji{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x1f600u, &emoji),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(CoreField(emoji, 25u)->payload[0], 1u);
    EXPECT_EQ(CoreField(letter, 25u)->payload[0], 0u);

    laplace_unicode_core_record_view canonical{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x00c0u, &canonical),
              LAPLACE_UNICODE_OK);
    const std::array<std::uint8_t, 8> expected_decomposition{{
        0x41u, 0u, 0u, 0u, 0u, 0x03u, 0u, 0u}};
    ASSERT_EQ(CoreField(canonical, 6u)->payload_bytes,
              expected_decomposition.size());
    EXPECT_EQ(std::memcmp(
                  CoreField(canonical, 6u)->payload,
                  expected_decomposition.data(), expected_decomposition.size()),
              0);

    laplace_unicode_core_record_view compatibility{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0x00a0u, &compatibility),
              LAPLACE_UNICODE_OK);
    const std::array<std::uint8_t, 19> expected_compatibility{{
        7u, 0u, 0u, 0u, 1u, 0u, 0u, 0u,
        'N', 'o', 'b', 'r', 'e', 'a', 'k',
        0x20u, 0u, 0u, 0u}};
    ASSERT_EQ(CoreField(compatibility, 7u)->payload_bytes,
              expected_compatibility.size());
    EXPECT_EQ(std::memcmp(
                  CoreField(compatibility, 7u)->payload,
                  expected_compatibility.data(), expected_compatibility.size()),
              0);

    laplace_unicode_core_table_destroy(&table);
    EXPECT_EQ(table, nullptr);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeCoreProperties, ClassifiesEveryPositionAndAppliesBidiDefaults) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* table = nullptr;
    laplace_unicode_core_summary summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(bundle, &table, &summary),
              LAPLACE_UNICODE_OK);
    const std::array<std::uint64_t, 5> expected_classes{{
        UINT64_C(159866), UINT64_C(814664), UINT64_C(137468),
        UINT64_C(66), UINT64_C(2048)}};
    EXPECT_EQ(std::memcmp(
                  summary.position_class_counts, expected_classes.data(),
                  sizeof(summary.position_class_counts)),
              0);

    laplace_unicode_core_record_view unassigned_rtl{};
    ASSERT_EQ(laplace_unicode_core_table_record(
                  table, 0x0590u, &unassigned_rtl),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(unassigned_rtl.position_class,
              LAPLACE_UNICODE_UNASSIGNED_OR_RESERVED_SCALAR);
    EXPECT_EQ(FieldText(CoreField(unassigned_rtl, 1u)), "Unassigned");
    EXPECT_EQ(FieldText(CoreField(unassigned_rtl, 3u)), "Right_To_Left");

    laplace_unicode_core_record_view surrogate{};
    ASSERT_EQ(laplace_unicode_core_table_record(table, 0xd800u, &surrogate),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(surrogate.position_class,
              LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS);
    EXPECT_EQ(FieldText(CoreField(surrogate, 1u)), "Surrogate");

    laplace_unicode_core_record_view invalid{};
    EXPECT_EQ(laplace_unicode_core_table_record(
                  table, LAPLACE_UNICODE_ROOT_POPULATION, &invalid),
              LAPLACE_UNICODE_POSITION_OUT_OF_RANGE);
    laplace_unicode_core_table_destroy(&table);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeDucet, RetainsCompleteMappingsAndImplicitRanges) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_table* table = nullptr;
    laplace_unicode_ducet_summary summary{};
    ASSERT_EQ(laplace_unicode_ducet_table_create(bundle, &table, &summary),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(summary.status, LAPLACE_UNICODE_OK);
    EXPECT_TRUE(SameDigest(summary.source_fingerprint,
                           source.source_fingerprint));
    EXPECT_TRUE(SameDigest(summary.recipe_fingerprint,
                           source.recipe_fingerprint));
    EXPECT_EQ(summary.explicit_mapping_count, UINT64_C(39749));
    EXPECT_EQ(summary.explicit_single_position_count, UINT64_C(38785));
    EXPECT_EQ(summary.contraction_count, UINT64_C(964));
    EXPECT_EQ(summary.expansion_mapping_count, UINT64_C(4873));
    EXPECT_EQ(summary.collation_element_count, UINT64_C(45860));
    EXPECT_EQ(summary.variable_collation_element_count, UINT64_C(8756));
    EXPECT_EQ(summary.implicit_range_count, 6u);
    EXPECT_EQ(summary.maximum_sequence_count, 3u);
    EXPECT_EQ(summary.maximum_element_count, 18u);
    const laplace_digest256 zero{};
    EXPECT_FALSE(SameDigest(summary.retained_table_fingerprint, zero));
    EXPECT_FALSE(SameDigest(summary.receipt_id, zero));

    const std::uint32_t square_au_position = 0x3373u;
    laplace_unicode_ducet_mapping_view square_au{};
    ASSERT_EQ(laplace_unicode_ducet_table_lookup(
                  table, &square_au_position, 1u, &square_au),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(square_au.sequence_count, 1u);
    ASSERT_EQ(square_au.element_count, 2u);
    EXPECT_EQ(square_au.elements[0].primary, 0x23ecu);
    EXPECT_EQ(square_au.elements[1].primary, 0x2680u);
    EXPECT_EQ(square_au.elements[0].tertiary, 0x001du);
    EXPECT_GT(square_au.source_line_ordinal, 0u);

    const std::array<std::uint32_t, 2> tibetan{{0x0f71u, 0x0f72u}};
    laplace_unicode_ducet_mapping_view contraction{};
    ASSERT_EQ(laplace_unicode_ducet_table_lookup(
                  table, tibetan.data(), tibetan.size(), &contraction),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(contraction.sequence_count, 2u);
    ASSERT_EQ(contraction.element_count, 1u);
    EXPECT_EQ(contraction.elements[0].primary, 0x384fu);

    const std::uint32_t space_position = 0x20u;
    laplace_unicode_ducet_mapping_view space{};
    ASSERT_EQ(laplace_unicode_ducet_table_lookup(
                  table, &space_position, 1u, &space),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(space.element_count, 1u);
    EXPECT_EQ(space.elements[0].variable, 1u);
    EXPECT_EQ(space.elements[0].primary, 0x0209u);

    laplace_unicode_ducet_implicit_range_view tangut{};
    ASSERT_EQ(laplace_unicode_ducet_table_implicit_range(
                  table, 0u, &tangut),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(tangut.first_position, 0x17000u);
    EXPECT_EQ(tangut.last_position, 0x187ffu);
    EXPECT_EQ(tangut.lead_primary, 0xfb00u);
    EXPECT_GT(tangut.source_line_ordinal, 0u);

    const std::uint32_t hangul_syllable = 0xac00u;
    laplace_unicode_ducet_mapping_view absent{};
    EXPECT_EQ(laplace_unicode_ducet_table_lookup(
                  table, &hangul_syllable, 1u, &absent),
              LAPLACE_UNICODE_SOURCE_INCOMPLETE);
    laplace_unicode_ducet_table_destroy(&table);
    EXPECT_EQ(table, nullptr);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeDucet, CalculatesNfdCompleteWeightsAndAlternateKeys) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* core = nullptr;
    laplace_unicode_core_summary core_summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(
                  bundle, &core, &core_summary),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_table* table = nullptr;
    laplace_unicode_ducet_summary table_summary{};
    ASSERT_EQ(laplace_unicode_ducet_table_create(
                  bundle, &table, &table_summary),
              LAPLACE_UNICODE_OK);

    const DucetCalculation composed = CalculateDucet(
        table, core, {0x00c5u}, LAPLACE_UNICODE_UCA_SHIFTED);
    const DucetCalculation decomposed = CalculateDucet(
        table, core, {0x0041u, 0x030au}, LAPLACE_UNICODE_UCA_SHIFTED);
    EXPECT_EQ(composed.normalized,
              (std::vector<std::uint32_t>{0x0041u, 0x030au}));
    EXPECT_EQ(composed.normalized, decomposed.normalized);
    EXPECT_EQ(composed.key, decomposed.key);
    ASSERT_EQ(composed.elements.size(), 2u);
    EXPECT_EQ(composed.elements[0].primary, 0x23ecu);
    EXPECT_EQ(composed.elements[1].secondary, 0x0029u);

    const DucetCalculation expansion = CalculateDucet(
        table, core, {0x3373u}, LAPLACE_UNICODE_UCA_SHIFTED);
    ASSERT_EQ(expansion.elements.size(), 2u);
    EXPECT_EQ(expansion.elements[0].primary, 0x23ecu);
    EXPECT_EQ(expansion.elements[1].primary, 0x2680u);

    const DucetCalculation hangul = CalculateDucet(
        table, core, {0xac00u}, LAPLACE_UNICODE_UCA_SHIFTED);
    EXPECT_EQ(hangul.normalized,
              (std::vector<std::uint32_t>{0x1100u, 0x1161u}));
    EXPECT_EQ(hangul.provenance, LAPLACE_UNICODE_DUCET_HANGUL);
    ASSERT_EQ(hangul.elements.size(), 2u);
    EXPECT_EQ(hangul.elements[0].primary, 0x4771u);
    EXPECT_EQ(hangul.elements[1].primary, 0x47efu);

    struct ImplicitVector {
        std::uint32_t position;
        std::uint16_t lead;
        std::uint16_t trail;
        std::uint8_t provenance;
    };
    static constexpr std::array<ImplicitVector, 10> ImplicitVectors{{
        {0x17000u, 0xfb00u, 0x8000u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x18d00u, 0xfb00u, 0x9d00u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x18800u, 0xfb01u, 0x8000u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x18d80u, 0xfb01u, 0x8580u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x1b170u, 0xfb02u, 0x8000u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x18b00u, 0xfb03u, 0x8000u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x4e00u, 0xfb40u, 0xce00u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x30000u, 0xfb86u, 0x8000u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0x0378u, 0xfbc0u, 0x8378u, LAPLACE_UNICODE_DUCET_IMPLICIT},
        {0xd800u, 0xfbc1u, 0xd800u,
         LAPLACE_UNICODE_DUCET_LUP_SURROGATE_EXTENSION}}};
    for (const ImplicitVector& expected : ImplicitVectors) {
        const DucetCalculation value = CalculateDucet(
            table, core, {expected.position},
            LAPLACE_UNICODE_UCA_SHIFTED);
        ASSERT_EQ(value.elements.size(), 2u)
            << "position " << std::hex << expected.position;
        EXPECT_EQ(value.elements[0].primary, expected.lead);
        EXPECT_EQ(value.elements[0].secondary, 0x0020u);
        EXPECT_EQ(value.elements[0].tertiary, 0x0002u);
        EXPECT_EQ(value.elements[1].primary, expected.trail);
        EXPECT_EQ(value.elements[1].secondary, 0u);
        EXPECT_EQ(value.elements[1].tertiary, 0u);
        EXPECT_EQ(value.provenance, expected.provenance);
    }

    const DucetCalculation shifted_space = CalculateDucet(
        table, core, {0x20u}, LAPLACE_UNICODE_UCA_SHIFTED);
    const DucetCalculation non_ignorable_space = CalculateDucet(
        table, core, {0x20u}, LAPLACE_UNICODE_UCA_NON_IGNORABLE);
    const std::vector<std::uint8_t> expected_shifted_space{
        0u, 0u, 0u, 0u, 0u, 0u, 0x02u, 0x09u,
        0u, 0u, 1u, 0x20u};
    const std::vector<std::uint8_t> expected_non_ignorable_space{
        0x02u, 0x09u, 0u, 0u, 0u, 0x20u, 0u, 0u,
        0u, 0x02u, 0u, 0u, 1u, 0x20u};
    EXPECT_EQ(shifted_space.key, expected_shifted_space);
    EXPECT_EQ(non_ignorable_space.key, expected_non_ignorable_space);

    const DucetCalculation shifted_mixed = CalculateDucet(
        table, core, {0x0385u}, LAPLACE_UNICODE_UCA_SHIFTED);
    ASSERT_EQ(shifted_mixed.elements.size(), 2u);
    EXPECT_EQ(shifted_mixed.elements[0].variable, 1u);
    EXPECT_EQ(shifted_mixed.elements[1].variable, 0u);
    EXPECT_EQ(shifted_mixed.key,
              (std::vector<std::uint8_t>{
                  0u, 0u, 0u, 0u, 0u, 0u, 0x04u, 0xe8u,
                  0u, 0u, 2u, 0xc2u, 0xa8u, 2u, 0xccu, 0x81u}));

    laplace_unicode_ducet_table_destroy(&table);
    laplace_unicode_core_table_destroy(&core);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeDucet, TotalizesEveryPositionIntoUniquePlacementPermutation) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* core = nullptr;
    laplace_unicode_core_summary core_summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(
                  bundle, &core, &core_summary),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_table* ducet = nullptr;
    laplace_unicode_ducet_summary ducet_summary{};
    ASSERT_EQ(laplace_unicode_ducet_table_create(
                  bundle, &ducet, &ducet_summary),
              LAPLACE_UNICODE_OK);
    laplace_unicode_placement_table* placement = nullptr;
    laplace_unicode_placement_summary summary{};
    ASSERT_EQ(laplace_unicode_placement_table_create(
                  ducet, core, &placement, &summary),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(placement, nullptr);

    EXPECT_EQ(summary.status, LAPLACE_UNICODE_OK);
    EXPECT_EQ(summary.position_count,
              static_cast<std::uint64_t>(LAPLACE_UNICODE_ROOT_POPULATION));
    EXPECT_TRUE(SameDigest(summary.source_fingerprint,
                           source.source_fingerprint));
    EXPECT_TRUE(SameDigest(summary.recipe_fingerprint,
                           source.recipe_fingerprint));
    EXPECT_EQ(summary.minimum_rank, 0u);
    EXPECT_EQ(summary.maximum_rank,
              LAPLACE_UNICODE_ROOT_POPULATION - 1u);
    EXPECT_EQ(summary.collation_element_count, UINT64_C(2205438));
    EXPECT_EQ(summary.equivalence_key_bytes, UINT64_C(27821481));
    EXPECT_EQ(summary.maximum_element_count, 18u);
    EXPECT_EQ(summary.maximum_equivalence_key_bytes, 138u);
    EXPECT_EQ(summary.provenance_counts[0], UINT64_C(37783));
    EXPECT_EQ(summary.provenance_counts[1], UINT64_C(1063109));
    EXPECT_EQ(summary.provenance_counts[2], UINT64_C(11172));
    EXPECT_EQ(summary.provenance_counts[3], UINT64_C(2048));
    EXPECT_EQ(summary.provenance_counts[0] + summary.provenance_counts[1] +
                  summary.provenance_counts[2] + summary.provenance_counts[3],
              summary.position_count);
    const laplace_digest256 expected_equivalence{{
        0x8eu, 0x30u, 0xcbu, 0x01u, 0x34u, 0x5cu, 0x30u, 0xf7u,
        0x69u, 0x0bu, 0x84u, 0x12u, 0x46u, 0xe1u, 0x11u, 0xf9u,
        0x24u, 0x24u, 0x5fu, 0x3au, 0xfau, 0x2fu, 0x1eu, 0xaeu,
        0xc5u, 0xbdu, 0x2eu, 0x4bu, 0x78u, 0x58u, 0xccu, 0x88u}};
    const laplace_digest256 expected_permutation{{
        0x87u, 0x3eu, 0x6cu, 0x09u, 0xb1u, 0xbdu, 0x3eu, 0x45u,
        0x66u, 0x62u, 0x95u, 0xf8u, 0xa9u, 0x99u, 0x7au, 0x92u,
        0xa3u, 0x0eu, 0xf7u, 0x37u, 0x8au, 0x4eu, 0xa8u, 0x52u,
        0xf2u, 0xf5u, 0x21u, 0xc2u, 0x09u, 0x68u, 0xf3u, 0x8eu}};
    const laplace_digest256 expected_receipt{{
        0xf3u, 0xf5u, 0x70u, 0x7au, 0x00u, 0x0bu, 0xe2u, 0x2du,
        0xdeu, 0xc6u, 0x30u, 0x78u, 0x39u, 0x4au, 0x53u, 0xcbu,
        0x75u, 0xdfu, 0x67u, 0x39u, 0xd9u, 0x6au, 0xceu, 0x11u,
        0x8bu, 0xabu, 0xb1u, 0x0au, 0x51u, 0xd2u, 0x31u, 0x4au}};
    EXPECT_TRUE(SameDigest(
        summary.equivalence_fingerprint, expected_equivalence))
        << Hex(summary.equivalence_fingerprint);
    EXPECT_TRUE(SameDigest(
        summary.rank_permutation_fingerprint, expected_permutation))
        << Hex(summary.rank_permutation_fingerprint);
    EXPECT_TRUE(SameDigest(summary.receipt_id, expected_receipt))
        << Hex(summary.receipt_id);

    std::vector<std::uint8_t> seen(
        LAPLACE_UNICODE_ROOT_POPULATION, std::uint8_t{0});
    for (std::uint32_t position = 0u;
         position < LAPLACE_UNICODE_ROOT_POPULATION; ++position) {
        laplace_unicode_placement_position_view view{};
        if (laplace_unicode_placement_table_position(
                placement, position, &view) != LAPLACE_UNICODE_OK) {
            ADD_FAILURE() << "position lookup failed at " << position;
            break;
        }
        if (view.codepoint_position != position ||
            view.placement_rank >= LAPLACE_UNICODE_ROOT_POPULATION ||
            view.element_count == 0u || view.equivalence_key_bytes == 0u ||
            view.elements == nullptr || view.equivalence_key == nullptr) {
            ADD_FAILURE() << "incomplete placement at position " << position;
            break;
        }
        if (seen[view.placement_rank] != 0u) {
            ADD_FAILURE() << "duplicate placement rank " << view.placement_rank
                          << " at position " << position;
            break;
        }
        seen[view.placement_rank] = 1u;
        std::uint32_t inverse = 0u;
        if (laplace_unicode_placement_table_rank_position(
                placement, view.placement_rank, &inverse) !=
                LAPLACE_UNICODE_OK || inverse != position) {
            ADD_FAILURE() << "rank inverse mismatch at position " << position;
            break;
        }
    }
    EXPECT_EQ(std::count(seen.begin(), seen.end(), std::uint8_t{1}),
              static_cast<std::ptrdiff_t>(LAPLACE_UNICODE_ROOT_POPULATION));

    laplace_unicode_placement_position_view previous{};
    bool have_previous = false;
    for (std::uint32_t rank = 0u;
         rank < LAPLACE_UNICODE_ROOT_POPULATION; ++rank) {
        std::uint32_t position = 0u;
        ASSERT_EQ(laplace_unicode_placement_table_rank_position(
                      placement, rank, &position),
                  LAPLACE_UNICODE_OK);
        laplace_unicode_placement_position_view current{};
        ASSERT_EQ(laplace_unicode_placement_table_position(
                      placement, position, &current),
                  LAPLACE_UNICODE_OK);
        ASSERT_EQ(current.placement_rank, rank);
        if (have_previous) {
            const int compared = CompareBytes(
                previous.equivalence_key, previous.equivalence_key_bytes,
                current.equivalence_key, current.equivalence_key_bytes);
            ASSERT_LE(compared, 0) << "rank " << rank;
            if (compared == 0) {
                EXPECT_LT(CompareLupPositions(
                              previous.codepoint_position,
                              current.codepoint_position),
                          0)
                    << "rank " << rank;
            }
        }
        previous = current;
        have_previous = true;
    }

    laplace_unicode_placement_position_view angstrom_letter{};
    laplace_unicode_placement_position_view angstrom_sign{};
    ASSERT_EQ(laplace_unicode_placement_table_position(
                  placement, 0x00c5u, &angstrom_letter),
              LAPLACE_UNICODE_OK);
    ASSERT_EQ(laplace_unicode_placement_table_position(
                  placement, 0x212bu, &angstrom_sign),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(CompareBytes(
                  angstrom_letter.equivalence_key,
                  angstrom_letter.equivalence_key_bytes,
                  angstrom_sign.equivalence_key,
                  angstrom_sign.equivalence_key_bytes),
              0);
    EXPECT_LT(angstrom_letter.placement_rank, angstrom_sign.placement_rank);

    laplace_unicode_placement_table_destroy(&placement);
    laplace_unicode_ducet_table_destroy(&ducet);
    laplace_unicode_core_table_destroy(&core);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeDucet, HonorsLongestDiscontiguousAndStarterBlockingMatches) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* core = nullptr;
    laplace_unicode_core_summary core_summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(
                  bundle, &core, &core_summary),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_table* table = nullptr;
    laplace_unicode_ducet_summary table_summary{};
    ASSERT_EQ(laplace_unicode_ducet_table_create(
                  bundle, &table, &table_summary),
              LAPLACE_UNICODE_OK);

    const DucetCalculation contiguous = CalculateDucet(
        table, core, {0x0438u, 0x0306u},
        LAPLACE_UNICODE_UCA_NON_IGNORABLE);
    ASSERT_EQ(contiguous.elements.size(), 1u);
    EXPECT_EQ(contiguous.elements[0].primary, 0x2861u);

    const DucetCalculation discontiguous = CalculateDucet(
        table, core, {0x0438u, 0x0591u, 0x0306u},
        LAPLACE_UNICODE_UCA_NON_IGNORABLE);
    ASSERT_EQ(discontiguous.elements.size(), 2u);
    EXPECT_EQ(discontiguous.elements[0].primary, 0x2861u);
    EXPECT_EQ(discontiguous.elements[1].primary, 0u);
    EXPECT_EQ(discontiguous.elements[1].secondary, 0u);
    EXPECT_EQ(discontiguous.elements[1].tertiary, 0u);

    const DucetCalculation starter_blocked = CalculateDucet(
        table, core, {0x0438u, 0x0001u, 0x0306u},
        LAPLACE_UNICODE_UCA_NON_IGNORABLE);
    ASSERT_EQ(starter_blocked.elements.size(), 3u);
    EXPECT_EQ(starter_blocked.elements[0].primary, 0x2854u);
    EXPECT_EQ(starter_blocked.elements[2].primary, 0u);
    EXPECT_EQ(starter_blocked.elements[2].tertiary, 0x0002u);

    const DucetCalculation length_three = CalculateDucet(
        table, core, {0x1611eu, 0x1611eu, 0x1611fu},
        LAPLACE_UNICODE_UCA_NON_IGNORABLE);
    ASSERT_EQ(length_three.elements.size(), 1u);
    EXPECT_EQ(length_three.elements[0].primary, 0x543cu);

    laplace_unicode_ducet_table_destroy(&table);
    laplace_unicode_core_table_destroy(&core);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeDucet, PassesBothOfficialFullUnicode17ConformanceSuites) {
    if (!SourceAvailable() || CollationTestRoot().empty()) {
        GTEST_SKIP() << "pinned Unicode source or extracted conformance suites "
                        "are unavailable";
    }
    const fs::path non_ignorable = CollationTestRoot() /
        "CollationTest/CollationTest_NON_IGNORABLE.txt";
    const fs::path shifted = CollationTestRoot() /
        "CollationTest/CollationTest_SHIFTED.txt";
    if (!fs::is_regular_file(non_ignorable) ||
        !fs::is_regular_file(shifted)) {
        GTEST_SKIP() << "full Unicode 17 collation suites were not extracted";
    }
    laplace_unicode_source_bundle* bundle = nullptr;
    laplace_unicode_source_receipt source{};
    ASSERT_EQ(laplace_unicode_source_bundle_open(
                  SourceRoot().c_str(), &bundle, &source),
              LAPLACE_UNICODE_OK);
    laplace_unicode_core_table* core = nullptr;
    laplace_unicode_core_summary core_summary{};
    ASSERT_EQ(laplace_unicode_core_table_create(
                  bundle, &core, &core_summary),
              LAPLACE_UNICODE_OK);
    laplace_unicode_ducet_table* table = nullptr;
    laplace_unicode_ducet_summary table_summary{};
    ASSERT_EQ(laplace_unicode_ducet_table_create(
                  bundle, &table, &table_summary),
              LAPLACE_UNICODE_OK);

    EXPECT_TRUE(VerifyFullCollationSuite(
        non_ignorable, table, core,
        LAPLACE_UNICODE_UCA_NON_IGNORABLE, UINT64_C(208070)));
    EXPECT_TRUE(VerifyFullCollationSuite(
        shifted, table, core,
        LAPLACE_UNICODE_UCA_SHIFTED, UINT64_C(229860)));

    laplace_unicode_ducet_table_destroy(&table);
    laplace_unicode_core_table_destroy(&core);
    laplace_unicode_source_bundle_close(&bundle);
}

TEST(UnicodeSource, RejectsOneByteChangeInPinnedSourceSet) {
    if (!SourceAvailable()) {
        GTEST_SKIP() << "pinned Unicode source root is not installed at "
                     << SourceRoot();
    }
    TemporaryDirectory fixture;
    ASSERT_FALSE(fixture.Path().empty());
    ASSERT_NO_THROW(CopyPinnedSourceSet(fixture.Path()));

    const fs::path target = fixture.Path() / "uca/allkeys.txt";
    std::fstream stream(target, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(stream.good());
    char byte = '\0';
    stream.read(&byte, 1);
    ASSERT_EQ(stream.gcount(), 1);
    byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x01u);
    stream.seekp(0);
    stream.write(&byte, 1);
    stream.close();

    laplace_unicode_source_receipt receipt{};
    EXPECT_EQ(laplace_unicode_source_verify(fixture.Path().c_str(), &receipt),
              LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH);
    EXPECT_EQ(receipt.status, LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH);
}

}  // namespace
