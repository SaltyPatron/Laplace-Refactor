#include "laplace/unicode_root.h"
#include "laplace/contract/unicode-source-manifest.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
