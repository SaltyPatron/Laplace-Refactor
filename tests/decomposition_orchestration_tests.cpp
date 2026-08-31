#include "laplace/decomposition.h"
#include "laplace/decomposition_composition.h"
#include "laplace/decomposition_delimited.h"
#include "laplace/decomposition_fixed_width.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t EmbeddedKind = UINT64_C(0x454d424544444544);
constexpr std::uint64_t AlternateEmbeddedKind = UINT64_C(0x454d424544444545);
constexpr std::uint64_t NestedKind = UINT64_C(0x4e45535445440001);
constexpr std::string_view HtmlMedia{"text/html"};
constexpr std::string_view JavascriptMedia{"application/javascript"};

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 digest{};
    digest.bytes[0] = marker;
    digest.bytes[31] = static_cast<std::uint8_t>(marker ^ 0x5au);
    return digest;
}

bool DigestEquals(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool MediaEquals(
    const laplace_decomposition_content* content,
    const std::string_view expected) {
    return content != nullptr && content->media_type != nullptr &&
        content->media_type_byte_count == expected.size() &&
        std::memcmp(content->media_type, expected.data(), expected.size()) == 0;
}

struct RootProviderState {
    std::uint64_t emitted_kind{EmbeddedKind};
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
        state.emitted_kind,
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
    if (child_kind == EmbeddedKind || child_kind == AlternateEmbeddedKind) {
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

struct PlanSnapshot {
    laplace_digest256 trace_fingerprint{};
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::uint64_t span_count{};
    std::uint64_t root_result_index{};
};

std::optional<PlanSnapshot> SnapshotPlan(Fixture& fixture) {
    const auto decomposition_input = fixture.Input();
    laplace_decomposition_result* decomposition = nullptr;
    if (laplace_decomposition_run_with_media_resolver(
            &decomposition_input,
            ResolveEmbeddedMedia,
            nullptr,
            &decomposition) != LAPLACE_DECOMPOSITION_OK ||
        decomposition == nullptr) {
        ADD_FAILURE() << "decomposition did not produce a result";
        return std::nullopt;
    }

    laplace_decomposition_composition_input composition_input{};
    composition_input.content = &decomposition_input.content;
    composition_input.decomposition = decomposition;
    composition_input.recipe_fingerprint = Fingerprint(0x44u);
    composition_input.geometry_epoch = Fingerprint(0x55u);
    composition_input.occurrence_context_fingerprint = Fingerprint(0x66u);
    composition_input.source_ordinal_base = 100u;

    laplace_decomposition_composition_plan* plan = nullptr;
    if (laplace_decomposition_composition_plan_create(
            &composition_input, &plan) != LAPLACE_DECOMPOSITION_COMPOSITION_OK ||
        plan == nullptr) {
        ADD_FAILURE() << "decomposition composition plan was not created";
        laplace_decomposition_result_destroy(&decomposition);
        return std::nullopt;
    }

    laplace_decomposition_composition_plan_view view{};
    if (laplace_decomposition_composition_plan_view_get(plan, &view) !=
        LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
        ADD_FAILURE() << "decomposition composition plan view was not available";
        laplace_decomposition_composition_plan_destroy(&plan);
        laplace_decomposition_result_destroy(&decomposition);
        return std::nullopt;
    }

    PlanSnapshot snapshot{};
    snapshot.trace_fingerprint = view.trace_fingerprint;
    snapshot.atom_positions.assign(
        view.atom_positions,
        view.atom_positions + static_cast<std::size_t>(view.atom_count));
    snapshot.operands.assign(
        view.operands,
        view.operands + static_cast<std::size_t>(view.operand_count));
    snapshot.requests.assign(
        view.requests,
        view.requests + static_cast<std::size_t>(view.request_count));
    snapshot.span_count = view.span_count;
    snapshot.root_result_index = view.root_result_index;

    laplace_decomposition_composition_plan_destroy(&plan);
    laplace_decomposition_result_destroy(&decomposition);
    return snapshot;
}

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

TEST(DecompositionOrchestration, FixedWidthOverflowIsExplicitAndDoesNotShiftLaterFields) {
    constexpr std::string_view bytes{"001AB  ZQ\r\n002ABCDEQR\r\n"};
    constexpr std::string_view media{"text/plain; charset=us-ascii"};
    constexpr std::uint32_t trim =
        LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_LEFT |
        LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_RIGHT;
    constexpr std::array<laplace_decomposition_fixed_width_field, 3> fields{{
        {3u, trim}, {4u, trim}, {2u, trim}}};
    laplace_decomposition_fixed_width_provider fixed{};
    const laplace_digest256 fingerprint = Fingerprint(0x34u);
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
            &fingerprint),
        LAPLACE_DECOMPOSITION_OK);

    laplace_decomposition_input input{};
    input.content.bytes =
        reinterpret_cast<const std::uint8_t*>(bytes.data());
    input.content.byte_count = bytes.size();
    input.content.media_type = media.data();
    input.content.media_type_byte_count = media.size();
    input.providers = &fixed.provider;
    input.provider_count = 1u;
    input.maximum_spans = 32u;
    input.maximum_depth = 8u;

    laplace_decomposition_result* result = nullptr;
    ASSERT_EQ(laplace_decomposition_run(&input, &result),
              LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(result, nullptr);
    std::size_t span_count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(result, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 18u);

    EXPECT_EQ(spans[12].kind, fixed.field_kind);
    EXPECT_EQ(spans[12].byte_start, 14u);
    EXPECT_EQ(spans[12].byte_end, 19u);
    EXPECT_EQ(spans[14].kind, fixed.overflow_kind);
    EXPECT_EQ(spans[14].byte_start, 18u);
    EXPECT_EQ(spans[14].byte_end, 19u);
    EXPECT_EQ(spans[15].kind, fixed.field_kind);
    EXPECT_EQ(spans[15].byte_start, 19u);
    EXPECT_EQ(spans[15].byte_end, 21u);
    EXPECT_EQ(
        std::string_view(
            reinterpret_cast<const char*>(input.content.bytes +
                                           spans[13].byte_start),
            spans[13].byte_end - spans[13].byte_start),
        "ABCDE");
    EXPECT_EQ(
        std::string_view(
            reinterpret_cast<const char*>(input.content.bytes +
                                           spans[16].byte_start),
            spans[16].byte_end - spans[16].byte_start),
        "QR");

    laplace_decomposition_result_destroy(&result);
    EXPECT_EQ(result, nullptr);
}

TEST(DecompositionOrchestration, FixedWidthOverflowDeclarationMutationIsObservable) {
    constexpr std::string_view bytes{"002ABCDEQR\r\n"};
    constexpr std::string_view media{"text/plain"};
    constexpr std::uint32_t trim =
        LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_LEFT |
        LAPLACE_DECOMPOSITION_FIXED_WIDTH_TRIM_RIGHT;
    constexpr std::array<laplace_decomposition_fixed_width_field, 3> fields{{
        {3u, trim}, {4u, trim}, {2u, trim}}};
    const laplace_digest256 correct_fingerprint = Fingerprint(0x35u);
    const laplace_digest256 mutant_fingerprint = Fingerprint(0x36u);
    laplace_decomposition_fixed_width_provider correct{};
    laplace_decomposition_fixed_width_provider mutant{};
    ASSERT_EQ(laplace_decomposition_fixed_width_provider_init(
                  &correct, fields.data(), fields.size(), 0u,
                  LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF, ' ', 1u, 1u,
                  UINT64_C(0x4657445500000000), &correct_fingerprint),
              LAPLACE_DECOMPOSITION_OK);
    ASSERT_EQ(laplace_decomposition_fixed_width_provider_init(
                  &mutant, fields.data(), fields.size(), 0u,
                  LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF, ' ', 2u, 1u,
                  UINT64_C(0x4657445600000000), &mutant_fingerprint),
              LAPLACE_DECOMPOSITION_OK);

    auto run = [&](laplace_decomposition_fixed_width_provider& provider) {
        laplace_decomposition_input input{};
        input.content.bytes =
            reinterpret_cast<const std::uint8_t*>(bytes.data());
        input.content.byte_count = bytes.size();
        input.content.media_type = media.data();
        input.content.media_type_byte_count = media.size();
        input.providers = &provider.provider;
        input.provider_count = 1u;
        input.maximum_spans = 24u;
        input.maximum_depth = 8u;
        laplace_decomposition_result* result = nullptr;
        EXPECT_EQ(laplace_decomposition_run(&input, &result),
                  LAPLACE_DECOMPOSITION_OK);
        return result;
    };

    laplace_decomposition_result* correct_result = run(correct);
    laplace_decomposition_result* mutant_result = run(mutant);
    ASSERT_NE(correct_result, nullptr);
    ASSERT_NE(mutant_result, nullptr);
    std::size_t correct_count = 0u;
    std::size_t mutant_count = 0u;
    const auto* correct_spans =
        laplace_decomposition_spans(correct_result, &correct_count);
    const auto* mutant_spans =
        laplace_decomposition_spans(mutant_result, &mutant_count);
    ASSERT_EQ(correct_count, 10u);
    ASSERT_EQ(mutant_count, 10u);
    EXPECT_EQ(correct_spans[7].kind, correct.field_kind);
    EXPECT_EQ(mutant_spans[6].kind, mutant.field_kind);
    EXPECT_EQ(correct_spans[7].byte_start, 8u);
    EXPECT_EQ(mutant_spans[6].byte_start, 7u);
    EXPECT_NE(correct_spans[7].byte_start, mutant_spans[6].byte_start);

    laplace_decomposition_result_destroy(&correct_result);
    laplace_decomposition_result_destroy(&mutant_result);
}

TEST(DecompositionOrchestration, FixedWidthRejectsOverflowBeyondRecipeBound) {
    constexpr std::string_view bytes{"002ABCDEFQR\r\n"};
    constexpr std::string_view media{"text/plain"};
    constexpr std::array<laplace_decomposition_fixed_width_field, 3> fields{{
        {3u, 0u}, {4u, 0u}, {2u, 0u}}};
    const laplace_digest256 fingerprint = Fingerprint(0x37u);
    laplace_decomposition_fixed_width_provider fixed{};
    ASSERT_EQ(laplace_decomposition_fixed_width_provider_init(
                  &fixed, fields.data(), fields.size(), 0u,
                  LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF, ' ', 1u, 1u,
                  UINT64_C(0x4657445700000000), &fingerprint),
              LAPLACE_DECOMPOSITION_OK);
    laplace_decomposition_input input{};
    input.content.bytes =
        reinterpret_cast<const std::uint8_t*>(bytes.data());
    input.content.byte_count = bytes.size();
    input.content.media_type = media.data();
    input.content.media_type_byte_count = media.size();
    input.providers = &fixed.provider;
    input.provider_count = 1u;
    input.maximum_spans = 24u;
    input.maximum_depth = 8u;
    laplace_decomposition_result* result = nullptr;
    EXPECT_EQ(laplace_decomposition_run(&input, &result),
              LAPLACE_DECOMPOSITION_PROVIDER_FAILURE);
    EXPECT_EQ(result, nullptr);
}

TEST(DecompositionComposition, CanonicalRootExcludesTraceWitnessMetadata) {
    constexpr std::uint32_t child_flags =
        LAPLACE_DECOMPOSITION_SPAN_REDISPATCH |
        LAPLACE_DECOMPOSITION_SPAN_TEXT |
        LAPLACE_DECOMPOSITION_SPAN_GRAMMAR_INPUT;
    Fixture first(child_flags);
    Fixture second(child_flags);
    second.providers[0].provider_fingerprint = Fingerprint(0x77u);
    second.root_state.emitted_kind = AlternateEmbeddedKind;

    const auto first_plan = SnapshotPlan(first);
    const auto second_plan = SnapshotPlan(second);
    ASSERT_TRUE(first_plan.has_value());
    ASSERT_TRUE(second_plan.has_value());

    EXPECT_FALSE(DigestEquals(
        first_plan->trace_fingerprint,
        second_plan->trace_fingerprint));
    EXPECT_EQ(first_plan->span_count, second_plan->span_count);
    EXPECT_EQ(first_plan->atom_positions, second_plan->atom_positions);
    ASSERT_EQ(first_plan->atom_positions.size(), 3u);
    EXPECT_EQ(first_plan->atom_positions[0], static_cast<std::uint32_t>('x'));
    EXPECT_EQ(first_plan->atom_positions[1], static_cast<std::uint32_t>('y'));
    EXPECT_EQ(first_plan->atom_positions[2], static_cast<std::uint32_t>('z'));

    ASSERT_EQ(first_plan->requests.size(), 1u);
    ASSERT_EQ(second_plan->requests.size(), 1u);
    EXPECT_EQ(first_plan->root_result_index, 0u);
    EXPECT_EQ(second_plan->root_result_index, 0u);
    EXPECT_EQ(first_plan->requests[0].first_operand, 0u);
    EXPECT_EQ(first_plan->requests[0].operand_count, 3u);
    EXPECT_EQ(second_plan->requests[0].first_operand, 0u);
    EXPECT_EQ(second_plan->requests[0].operand_count, 3u);

    ASSERT_EQ(first_plan->operands.size(), 3u);
    ASSERT_EQ(second_plan->operands.size(), 3u);
    for (std::size_t index = 0u; index < first_plan->operands.size(); ++index) {
        const auto& left = first_plan->operands[index];
        const auto& right = second_plan->operands[index];
        EXPECT_EQ(left.reference_index, index);
        EXPECT_EQ(left.reference_index, right.reference_index);
        EXPECT_EQ(left.multiplicity, 1u);
        EXPECT_EQ(left.multiplicity, right.multiplicity);
        EXPECT_EQ(left.relationship_metadata, 0u);
        EXPECT_EQ(left.relationship_metadata, right.relationship_metadata);
        EXPECT_EQ(
            left.reference_kind,
            static_cast<std::uint32_t>(LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY));
        EXPECT_EQ(left.reference_kind, right.reference_kind);
        EXPECT_EQ(left.flags, 0u);
        EXPECT_EQ(left.flags, right.flags);
    }
}

}  // namespace
