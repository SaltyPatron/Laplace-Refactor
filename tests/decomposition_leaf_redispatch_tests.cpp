#include "laplace/decomposition.h"
#include "laplace/decomposition_delimited.h"
#include "laplace/decomposition_fixed_width.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace {

constexpr std::uint64_t SemanticLeafKind = UINT64_C(0x53454d414e544943);

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 digest{};
    digest.bytes[0] = marker;
    digest.bytes[31] = static_cast<std::uint8_t>(marker ^ 0x5au);
    return digest;
}

struct SemanticLeafState {
    std::uint32_t apply_count{};
};

laplace_decomposition_status SemanticLeafApplicable(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *applicable =
        (span->flags & LAPLACE_DECOMPOSITION_SPAN_TEXT) != 0u &&
        (span->flags & LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT) == 0u
        ? 1
        : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status SemanticLeafApply(
    void* provider_state,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || span == nullptr || emit == nullptr ||
        span->byte_start >= span->byte_end) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    auto& state = *static_cast<SemanticLeafState*>(provider_state);
    ++state.apply_count;
    return emit(
        emit_state,
        span->byte_start,
        span->byte_end,
        SemanticLeafKind,
        LAPLACE_DECOMPOSITION_SPAN_TEXT) == 0
        ? LAPLACE_DECOMPOSITION_OK
        : LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
}

laplace_decomposition_provider_v1 SemanticLeafProvider(
    SemanticLeafState& state,
    const std::uint8_t marker) {
    laplace_decomposition_provider_v1 provider{};
    provider.state = &state;
    provider.provider_fingerprint = Fingerprint(marker);
    provider.applicable = SemanticLeafApplicable;
    provider.apply = SemanticLeafApply;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return provider;
}

TEST(DecompositionLeafRedispatch,
     DelimitedFieldsReachNextTextProviderExactlyOnceWhileRowsStayTerminal) {
    constexpr std::array<std::uint8_t, 4> bytes{{'a', '\t', 'b', '\n'}};
    constexpr std::string_view media{"text/tab-separated-values"};

    laplace_decomposition_delimited_provider delimited{};
    const laplace_digest256 delimiter_fingerprint = Fingerprint(0x31u);
    ASSERT_EQ(
        laplace_decomposition_delimited_provider_init(
            &delimited,
            '\t',
            LAPLACE_DECOMPOSITION_DELIMITED_LF,
            2u,
            0u,
            UINT64_C(0x5441424c00000000),
            &delimiter_fingerprint),
        LAPLACE_DECOMPOSITION_OK);

    SemanticLeafState semantic_state{};
    const laplace_decomposition_provider_v1 semantic =
        SemanticLeafProvider(semantic_state, 0x32u);
    const std::array<laplace_decomposition_provider_v1, 2> providers{{
        delimited.provider,
        semantic}};

    laplace_decomposition_input input{};
    input.content.bytes = bytes.data();
    input.content.byte_count = bytes.size();
    input.content.media_type = media.data();
    input.content.media_type_byte_count = media.size();
    input.providers = providers.data();
    input.provider_count = providers.size();
    input.maximum_spans = 16u;
    input.maximum_depth = 8u;

    laplace_decomposition_result* result = nullptr;
    ASSERT_EQ(
        laplace_decomposition_run(&input, &result),
        LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);

    laplace_decomposition_summary summary{};
    ASSERT_EQ(
        laplace_decomposition_summary_get(result, &summary),
        LAPLACE_DECOMPOSITION_OK);
    EXPECT_EQ(semantic_state.apply_count, 2u);
    EXPECT_EQ(summary.provider_execution_count, 4u);
    EXPECT_EQ(summary.applicable_execution_count, 3u);
    EXPECT_EQ(summary.redispatch_count, 2u);
    EXPECT_EQ(summary.maximum_depth_reached, 2u);
    EXPECT_EQ(summary.span_count, 8u);

    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 8u);

    EXPECT_EQ(spans[1].kind, delimited.record_kind);
    EXPECT_EQ(spans[1].flags, LAPLACE_DECOMPOSITION_SPAN_TEXT);
    EXPECT_EQ(spans[2].kind, delimited.field_kind);
    EXPECT_NE(spans[2].flags & LAPLACE_DECOMPOSITION_SPAN_REDISPATCH, 0u);
    EXPECT_EQ(spans[4].kind, delimited.field_kind);
    EXPECT_NE(spans[4].flags & LAPLACE_DECOMPOSITION_SPAN_REDISPATCH, 0u);

    EXPECT_EQ(spans[6].kind, SemanticLeafKind);
    EXPECT_EQ(spans[6].parent_span_index, 2u);
    EXPECT_EQ(spans[6].byte_start, 0u);
    EXPECT_EQ(spans[6].byte_end, 1u);
    EXPECT_EQ(spans[7].kind, SemanticLeafKind);
    EXPECT_EQ(spans[7].parent_span_index, 4u);
    EXPECT_EQ(spans[7].byte_start, 2u);
    EXPECT_EQ(spans[7].byte_end, 3u);

    for (std::size_t index = 6u; index < span_count; ++index) {
        EXPECT_NE(spans[index].parent_span_index, 1u);
        EXPECT_EQ(
            spans[index].flags & LAPLACE_DECOMPOSITION_SPAN_REDISPATCH,
            0u);
    }

    laplace_decomposition_result_destroy(&result);
    EXPECT_EQ(result, nullptr);
}

TEST(DecompositionLeafRedispatch,
     FixedWidthTrimmedValuesReachNextTextProviderWithoutRedispatchingStructure) {
    constexpr std::string_view bytes{"001AB  ZQ\r\n002ABCDEQR\r\n"};
    constexpr std::string_view media{"text/plain; charset=us-ascii"};
    constexpr std::uint32_t trim =
        LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_LEFT |
        LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_RIGHT;
    constexpr std::array<laplace_decomposition_fixed_width_field, 3> fields{{
        {3u, trim}, {4u, trim}, {2u, trim}}};

    laplace_decomposition_fixed_width_provider fixed{};
    const laplace_digest256 fixed_fingerprint = Fingerprint(0x41u);
    ASSERT_EQ(
        laplace_decomposition_fixed_width_provider_init(
            &fixed,
            fields.data(),
            fields.size(),
            0u,
            LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF,
            ' ',
            1u,
            1u,
            UINT64_C(0x4657445400000000),
            &fixed_fingerprint),
        LAPLACE_DECOMPOSITION_OK);

    SemanticLeafState semantic_state{};
    const laplace_decomposition_provider_v1 semantic =
        SemanticLeafProvider(semantic_state, 0x42u);
    const std::array<laplace_decomposition_provider_v1, 2> providers{{
        fixed.provider,
        semantic}};

    laplace_decomposition_input input{};
    input.content.bytes =
        reinterpret_cast<const std::uint8_t*>(bytes.data());
    input.content.byte_count = bytes.size();
    input.content.media_type = media.data();
    input.content.media_type_byte_count = media.size();
    input.providers = providers.data();
    input.provider_count = providers.size();
    input.maximum_spans = 32u;
    input.maximum_depth = 8u;

    laplace_decomposition_result* result = nullptr;
    ASSERT_EQ(
        laplace_decomposition_run(&input, &result),
        LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);

    laplace_decomposition_summary summary{};
    ASSERT_EQ(
        laplace_decomposition_summary_get(result, &summary),
        LAPLACE_DECOMPOSITION_OK);
    EXPECT_EQ(semantic_state.apply_count, 6u);
    EXPECT_EQ(summary.redispatch_count, 6u);
    EXPECT_EQ(summary.maximum_depth_reached, 2u);
    EXPECT_EQ(summary.span_count, 24u);

    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 24u);

    constexpr std::array<std::uint64_t, 6> expected_parents{{3u, 5u, 7u, 11u, 13u, 16u}};
    for (std::size_t index = 0u; index < expected_parents.size(); ++index) {
        const laplace_decomposition_span& leaf = spans[18u + index];
        EXPECT_EQ(leaf.kind, SemanticLeafKind);
        EXPECT_EQ(leaf.parent_span_index, expected_parents[index]);
        EXPECT_EQ(leaf.depth, 2u);
        EXPECT_EQ(leaf.flags, LAPLACE_DECOMPOSITION_SPAN_TEXT);
    }

    EXPECT_EQ(spans[1].kind, fixed.record_kind);
    EXPECT_EQ(spans[1].flags, LAPLACE_DECOMPOSITION_SPAN_TEXT);
    EXPECT_EQ(spans[2].kind, fixed.field_kind);
    EXPECT_EQ(spans[2].flags, 0u);
    EXPECT_EQ(spans[14].kind, fixed.overflow_kind);
    EXPECT_EQ(spans[14].flags, 0u);
    EXPECT_EQ(spans[17].kind, fixed.terminator_kind);
    EXPECT_EQ(spans[17].flags, 0u);

    laplace_decomposition_result_destroy(&result);
    EXPECT_EQ(result, nullptr);
}

}  // namespace
