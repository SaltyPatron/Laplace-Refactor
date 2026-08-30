#include "laplace/decomposition.h"
#include "laplace/decomposition_delimited.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace {

constexpr std::uint64_t EmbeddedKind = UINT64_C(0x454d424544444544);
constexpr std::uint64_t NestedKind = UINT64_C(0x4e45535445440001);
constexpr std::string_view HtmlMedia{"text/html"};
constexpr std::string_view JavascriptMedia{"application/javascript"};

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 digest{};
    digest.bytes[0] = marker;
    digest.bytes[31] = static_cast<std::uint8_t>(marker ^ 0x5au);
    return digest;
}

bool MediaEquals(
    const laplace_decomposition_content* content,
    const std::string_view expected) {
    return content != nullptr && content->media_type != nullptr &&
        content->media_type_byte_count == expected.size() &&
        std::memcmp(content->media_type, expected.data(), expected.size()) == 0;
}

struct RootProviderState {
    std::uint32_t emitted_flags{};
    std::uint32_t apply_count{};
};

laplace_decomposition_status RootApplicable(
    void*,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (content == nullptr || span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const bool grammar_input =
        (span->flags & LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT) != 0u;
    *applicable = span->depth == 0u && grammar_input &&
        MediaEquals(content, HtmlMedia) ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status RootApply(
    void* provider_state,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || span == nullptr || emit == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    auto& state = *static_cast<RootProviderState*>(provider_state);
    ++state.apply_count;
    return emit(
        emit_state,
        span->byte_start,
        span->byte_end,
        EmbeddedKind,
        state.emitted_flags) == 0
        ? LAPLACE_DECOMPOSITION_OK
        : LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
}

struct NestedProviderState {
    std::uint32_t apply_count{};
};

laplace_decomposition_status NestedApplicable(
    void*,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (content == nullptr || span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const bool grammar_input =
        (span->flags & LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT) != 0u;
    *applicable = grammar_input && MediaEquals(content, JavascriptMedia) ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status NestedApply(
    void* provider_state,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || span == nullptr || emit == nullptr ||
        span->byte_start >= span->byte_end) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    auto& state = *static_cast<NestedProviderState*>(provider_state);
    ++state.apply_count;
    const std::uint64_t end = span->byte_start + 1u;
    return emit(
        emit_state,
        span->byte_start,
        end,
        NestedKind,
        LAPLACE_DECOMPOSITION_SPAN_TEXT) == 0
        ? LAPLACE_DECOMPOSITION_OK
        : LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
}

laplace_decomposition_status ResolveEmbeddedMedia(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span*,
    const laplace_digest256*,
    std::uint64_t,
    std::uint64_t,
    const std::uint64_t child_kind,
    std::uint32_t,
    const char** media_type,
    std::uint64_t* media_type_byte_count) {
    if (media_type == nullptr || media_type_byte_count == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    if (child_kind == EmbeddedKind) {
        *media_type = JavascriptMedia.data();
        *media_type_byte_count = JavascriptMedia.size();
    } else {
        *media_type = nullptr;
        *media_type_byte_count = 0u;
    }
    return LAPLACE_DECOMPOSITION_OK;
}

struct Fixture {
    std::array<std::uint8_t, 3> bytes{{'x', 'y', 'z'}};
    RootProviderState root_state{};
    NestedProviderState nested_state{};
    std::array<laplace_decomposition_provider_v1, 2> providers{};

    explicit Fixture(const std::uint32_t child_flags) {
        root_state.emitted_flags = child_flags;
        providers[0].state = &root_state;
        providers[0].provider_fingerprint = Fingerprint(0x11u);
        providers[0].applicable = RootApplicable;
        providers[0].apply = RootApply;
        providers[0].abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
        providers[0].abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;

        providers[1].state = &nested_state;
        providers[1].provider_fingerprint = Fingerprint(0x22u);
        providers[1].applicable = NestedApplicable;
        providers[1].apply = NestedApply;
        providers[1].abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
        providers[1].abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    }

    laplace_decomposition_input Input() const {
        laplace_decomposition_input input{};
        input.content.bytes = bytes.data();
        input.content.byte_count = bytes.size();
        input.content.media_type = HtmlMedia.data();
        input.content.media_type_byte_count = HtmlMedia.size();
        input.providers = providers.data();
        input.provider_count = providers.size();
        input.maximum_spans = 16u;
        input.maximum_depth = 8u;
        return input;
    }
};

TEST(DecompositionOrchestration, RetypedRedispatchPreservesGrammarEligibility) {
    Fixture fixture(
        LAPLACE_DECOMPOSITION_SPAN_REDISPATCH |
        LAPLACE_DECOMPOSITION_SPAN_TEXT |
        LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT);
    const auto input = fixture.Input();
    laplace_decomposition_result* result = nullptr;

    ASSERT_EQ(
        laplace_decomposition_run_with_media_resolver(
            &input, ResolveEmbeddedMedia, nullptr, &result),
        LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(fixture.root_state.apply_count, 1u);
    EXPECT_EQ(fixture.nested_state.apply_count, 1u);

    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 3u);
    EXPECT_EQ(spans[1].kind, EmbeddedKind);
    EXPECT_NE(
        spans[1].flags & LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT,
        0u);
    EXPECT_EQ(spans[2].kind, NestedKind);
    EXPECT_EQ(spans[2].parent_span_index, 1u);

    std::size_t media_count = 0u;
    const char* media =
        laplace_decomposition_span_media_type(result, 1u, &media_count);
    ASSERT_NE(media, nullptr);
    EXPECT_EQ(
        std::string_view(media, media_count),
        JavascriptMedia);

    laplace_decomposition_summary summary{};
    ASSERT_EQ(
        laplace_decomposition_summary_get(result, &summary),
        LAPLACE_DECOMPOSITION_OK);
    EXPECT_EQ(summary.span_count, 3u);
    EXPECT_EQ(summary.applicable_execution_count, 2u);
    EXPECT_EQ(summary.redispatch_count, 1u);

    laplace_decomposition_result_destroy(&result);
    EXPECT_EQ(result, nullptr);
}

TEST(DecompositionOrchestration, TextOnlyRedispatchCannotMasqueradeAsGrammarInput) {
    Fixture fixture(
        LAPLACE_DECOMPOSITION_SPAN_REDISPATCH |
        LAPLACE_DECOMPOSITION_SPAN_TEXT);
    const auto input = fixture.Input();
    laplace_decomposition_result* result = nullptr;

    ASSERT_EQ(
        laplace_decomposition_run_with_media_resolver(
            &input, ResolveEmbeddedMedia, nullptr, &result),
        LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(fixture.root_state.apply_count, 1u);
    EXPECT_EQ(fixture.nested_state.apply_count, 0u);

    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 2u);
    EXPECT_EQ(spans[1].kind, EmbeddedKind);
    EXPECT_EQ(
        spans[1].flags & LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT,
        0u);

    std::size_t media_count = 0u;
    const char* media =
        laplace_decomposition_span_media_type(result, 1u, &media_count);
    ASSERT_NE(media, nullptr);
    EXPECT_EQ(
        std::string_view(media, media_count),
        JavascriptMedia);

    laplace_decomposition_result_destroy(&result);
}

TEST(DecompositionOrchestration, DelimitedRowsAndFieldsAreTerminalWitnesses) {
    constexpr std::array<std::uint8_t, 4> bytes{{'a', '\t', 'b', '\n'}};
    constexpr std::string_view media{"text/tab-separated-values"};
    laplace_decomposition_delimited_provider delimited{};
    const laplace_digest256 fingerprint = Fingerprint(0x33u);
    ASSERT_EQ(
        laplace_decomposition_delimited_provider_init(
            &delimited,
            '\t',
            LAPLACE_DECOMPOSITION_DELIMITED_LF,
            2u,
            0u,
            UINT64_C(0x5441424c00000000),
            &fingerprint),
        LAPLACE_DECOMPOSITION_OK);

    laplace_decomposition_input input{};
    input.content.bytes = bytes.data();
    input.content.byte_count = bytes.size();
    input.content.media_type = media.data();
    input.content.media_type_byte_count = media.size();
    input.providers = &delimited.provider;
    input.provider_count = 1u;
    input.maximum_spans = 16u;
    input.maximum_depth = 8u;

    laplace_decomposition_result* result = nullptr;
    ASSERT_EQ(laplace_decomposition_run(&input, &result),
              LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);

    laplace_decomposition_summary summary{};
    ASSERT_EQ(laplace_decomposition_summary_get(result, &summary),
              LAPLACE_DECOMPOSITION_OK);
    EXPECT_EQ(summary.provider_execution_count, 1u);
    EXPECT_EQ(summary.applicable_execution_count, 1u);
    EXPECT_EQ(summary.redispatch_count, 0u);
    EXPECT_EQ(summary.maximum_depth_reached, 1u);
    EXPECT_EQ(summary.span_count, 6u);

    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 6u);
    EXPECT_EQ(spans[1].flags, LAPLACE_DECOMPOSITION_SPAN_TEXT);
    EXPECT_EQ(spans[2].flags, LAPLACE_DECOMPOSITION_SPAN_TEXT);
    EXPECT_EQ(spans[3].flags, 0u);
    EXPECT_EQ(spans[4].flags, LAPLACE_DECOMPOSITION_SPAN_TEXT);
    EXPECT_EQ(spans[5].flags, 0u);

    laplace_decomposition_result_destroy(&result);
    EXPECT_EQ(result, nullptr);
}

}  // namespace
