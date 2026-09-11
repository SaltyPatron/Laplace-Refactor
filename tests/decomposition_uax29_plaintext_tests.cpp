#include "laplace/decomposition.h"
#include "laplace/decomposition_uax29.h"
#include "laplace/uax29.h"
#include "laplace/unicode_root.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace {

laplace_digest256 ProviderFingerprint() {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(0x41U + index);
    }
    return value;
}

struct UnicodeOwners final {
    laplace_unicode_source_bundle* bundle{};
    laplace_uax29_tables* tables{};

    ~UnicodeOwners() {
        laplace_uax29_tables_destroy(&tables);
        laplace_unicode_source_bundle_close(&bundle);
    }
};

bool KindPresent(
    const laplace_decomposition_span* spans,
    const std::size_t count,
    const std::uint64_t kind) {
    return std::any_of(
        spans,
        spans + count,
        [kind](const laplace_decomposition_span& span) {
            return span.kind == kind;
        });
}

TEST(DecompositionUax29PlainText,
     CanonicalTextPlainRootProducesGraphemeWordAndSentenceObservations) {
    const char* const unicode_root = std::getenv("LAPLACE_UNICODE_SOURCE_ROOT");
    if (unicode_root == nullptr || unicode_root[0] == '\0' ||
        !std::filesystem::is_directory(unicode_root)) {
        GTEST_SKIP() << "pinned Unicode source root unavailable";
    }

    UnicodeOwners owners;
    laplace_unicode_source_receipt source_receipt{};
    ASSERT_EQ(
        laplace_unicode_source_bundle_open(
            unicode_root, &owners.bundle, &source_receipt),
        LAPLACE_UNICODE_OK);
    ASSERT_NE(owners.bundle, nullptr);
    ASSERT_EQ(
        laplace_uax29_tables_create(owners.bundle, &owners.tables),
        LAPLACE_UAX29_OK);
    ASSERT_NE(owners.tables, nullptr);

    laplace_decomposition_uax29_provider storage{};
    const laplace_digest256 provider_fingerprint = ProviderFingerprint();
    ASSERT_EQ(
        laplace_decomposition_uax29_provider_init(
            &storage, owners.tables, &provider_fingerprint),
        LAPLACE_DECOMPOSITION_OK);

    constexpr std::string_view Text{"alpha beta."};
    constexpr std::string_view Media{"text/plain"};
    laplace_decomposition_input input{};
    input.content.bytes = reinterpret_cast<const std::uint8_t*>(Text.data());
    input.content.byte_count = static_cast<std::uint64_t>(Text.size());
    input.content.media_type = Media.data();
    input.content.media_type_byte_count = static_cast<std::uint64_t>(Media.size());
    input.providers = &storage.provider;
    input.provider_count = 1U;
    input.maximum_spans = 64U;
    input.maximum_depth = 1U;

    laplace_decomposition_result* result = nullptr;
    ASSERT_EQ(
        laplace_decomposition_run(&input, &result),
        LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);

    std::size_t span_count = 0U;
    const laplace_decomposition_span* const spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_GT(span_count, 1U);
    EXPECT_TRUE(KindPresent(
        spans, span_count, LAPLACE_DECOMPOSITION_KIND_UAX29_GRAPHEME));
    EXPECT_TRUE(KindPresent(
        spans, span_count, LAPLACE_DECOMPOSITION_KIND_UAX29_WORD));
    EXPECT_TRUE(KindPresent(
        spans, span_count, LAPLACE_DECOMPOSITION_KIND_UAX29_SENTENCE));

    const auto word_alpha = std::find_if(
        spans,
        spans + span_count,
        [](const laplace_decomposition_span& span) {
            return span.kind == LAPLACE_DECOMPOSITION_KIND_UAX29_WORD &&
                span.byte_start == 0U && span.byte_end == 5U;
        });
    const auto word_beta = std::find_if(
        spans,
        spans + span_count,
        [](const laplace_decomposition_span& span) {
            return span.kind == LAPLACE_DECOMPOSITION_KIND_UAX29_WORD &&
                span.byte_start == 6U && span.byte_end == 10U;
        });
    EXPECT_NE(word_alpha, spans + span_count);
    EXPECT_NE(word_beta, spans + span_count);

    laplace_decomposition_summary summary{};
    ASSERT_EQ(
        laplace_decomposition_summary_get(result, &summary),
        LAPLACE_DECOMPOSITION_OK);
    EXPECT_GT(summary.provider_execution_count, 0U);
    EXPECT_GT(summary.applicable_execution_count, 0U);
    EXPECT_EQ(summary.maximum_depth_reached, 1U);

    laplace_decomposition_result_destroy(&result);
    EXPECT_EQ(result, nullptr);
}

TEST(DecompositionUax29PlainText,
     StructuredGrammarMediaStillRequiresRedispatchedTextInsteadOfRootUax29) {
    const char* const unicode_root = std::getenv("LAPLACE_UNICODE_SOURCE_ROOT");
    if (unicode_root == nullptr || unicode_root[0] == '\0' ||
        !std::filesystem::is_directory(unicode_root)) {
        GTEST_SKIP() << "pinned Unicode source root unavailable";
    }

    UnicodeOwners owners;
    laplace_unicode_source_receipt source_receipt{};
    ASSERT_EQ(
        laplace_unicode_source_bundle_open(
            unicode_root, &owners.bundle, &source_receipt),
        LAPLACE_UNICODE_OK);
    ASSERT_EQ(
        laplace_uax29_tables_create(owners.bundle, &owners.tables),
        LAPLACE_UAX29_OK);

    laplace_decomposition_uax29_provider storage{};
    const laplace_digest256 provider_fingerprint = ProviderFingerprint();
    ASSERT_EQ(
        laplace_decomposition_uax29_provider_init(
            &storage, owners.tables, &provider_fingerprint),
        LAPLACE_DECOMPOSITION_OK);

    constexpr std::string_view Text{"alpha,beta\n"};
    constexpr std::string_view Media{"text/csv"};
    laplace_decomposition_input input{};
    input.content.bytes = reinterpret_cast<const std::uint8_t*>(Text.data());
    input.content.byte_count = static_cast<std::uint64_t>(Text.size());
    input.content.media_type = Media.data();
    input.content.media_type_byte_count = static_cast<std::uint64_t>(Media.size());
    input.providers = &storage.provider;
    input.provider_count = 1U;
    input.maximum_spans = 64U;
    input.maximum_depth = 1U;

    laplace_decomposition_result* result = nullptr;
    ASSERT_EQ(
        laplace_decomposition_run(&input, &result),
        LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);

    std::size_t span_count = 0U;
    const laplace_decomposition_span* const spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 1U);
    EXPECT_FALSE(KindPresent(
        spans, span_count, LAPLACE_DECOMPOSITION_KIND_UAX29_WORD));

    laplace_decomposition_summary summary{};
    ASSERT_EQ(
        laplace_decomposition_summary_get(result, &summary),
        LAPLACE_DECOMPOSITION_OK);
    EXPECT_EQ(summary.applicable_execution_count, 0U);

    laplace_decomposition_result_destroy(&result);
}

}  // namespace
