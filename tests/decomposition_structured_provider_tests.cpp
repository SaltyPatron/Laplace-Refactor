#include "laplace/decomposition.h"
#include "laplace/decomposition_composition.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 value{};
    value.bytes[0] = marker;
    value.bytes[31] = static_cast<std::uint8_t>(marker ^ 0x5au);
    return value;
}

struct StructuredState {
    std::uint64_t first_field{7u};
};

laplace_decomposition_status Applicable(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    int* applicable) {
    if (span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *applicable = span->depth == 0u ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status ApplyStructured(
    void* opaque,
    const laplace_decomposition_content*,
    const laplace_decomposition_span*,
    laplace_decomposition_emit_event_fn emit,
    void* emit_state) {
    if (opaque == nullptr || emit == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const auto& state = *static_cast<const StructuredState*>(opaque);
    const std::array<laplace_decomposition_event, 4> events{{
        {0u, 3u, UINT64_MAX,
         UINT64_C(0x1000000000000001), UINT64_C(0x1000000000000011),
         0u, 0u,
         LAPLACE_DECOMPOSITION_SPAN_TEXT,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED},
        {0u, 2u, 0u,
         UINT64_C(0x1000000000000002), UINT64_C(0x1000000000000012),
         state.first_field, 1u,
         LAPLACE_DECOMPOSITION_SPAN_TEXT,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED},
        {2u, 2u, 0u,
         UINT64_C(0x1000000000000003), UINT64_C(0x1000000000000013),
         8u, 2u,
         0u,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED |
             LAPLACE_DECOMPOSITION_SYNTAX_MISSING},
        {2u, 3u, 0u,
         UINT64_C(0x1000000000000004), UINT64_C(0x1000000000000014),
         9u, 3u,
         LAPLACE_DECOMPOSITION_SPAN_TEXT,
         LAPLACE_DECOMPOSITION_SYNTAX_NAMED |
             LAPLACE_DECOMPOSITION_SYNTAX_ERROR |
             LAPLACE_DECOMPOSITION_SYNTAX_HAS_ERROR}
    }};
    for (const auto& event : events) {
        if (emit(emit_state, &event) != 0) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
    }
    return LAPLACE_DECOMPOSITION_OK;
}

struct RunResult {
    laplace_decomposition_result* decomposition{};
    laplace_decomposition_composition_plan* plan{};
    laplace_decomposition_composition_plan_view view{};
};

RunResult Run(const std::uint64_t first_field) {
    static constexpr std::array<std::uint8_t, 3> bytes{{'a', 'b', 'c'}};
    static constexpr char media[] = "text/plain";
    StructuredState* state = new StructuredState{first_field};

    laplace_decomposition_provider_v1 provider{};
    provider.state = state;
    provider.provider_fingerprint = Fingerprint(0x21u);
    provider.applicable = Applicable;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    provider.apply_events = ApplyStructured;

    laplace_decomposition_input input{};
    input.content.bytes = bytes.data();
    input.content.byte_count = bytes.size();
    input.content.media_type = media;
    input.content.media_type_byte_count = sizeof(media) - 1u;
    input.providers = &provider;
    input.provider_count = 1u;
    input.maximum_spans = 16u;
    input.maximum_depth = 8u;

    RunResult result{};
    EXPECT_EQ(laplace_decomposition_run(&input, &result.decomposition),
              LAPLACE_DECOMPOSITION_OK);
    if (result.decomposition == nullptr) {
        delete state;
        return result;
    }

    laplace_decomposition_composition_input composition{};
    composition.content = &input.content;
    composition.decomposition = result.decomposition;
    composition.recipe_fingerprint = Fingerprint(0x31u);
    composition.geometry_epoch = Fingerprint(0x41u);
    composition.occurrence_context_fingerprint = Fingerprint(0x51u);
    composition.source_ordinal_base = 1u;
    EXPECT_EQ(laplace_decomposition_composition_plan_create(
                  &composition, &result.plan),
              LAPLACE_DECOMPOSITION_COMPOSITION_OK);
    if (result.plan != nullptr) {
        EXPECT_EQ(laplace_decomposition_composition_plan_view_get(
                      result.plan, &result.view),
                  LAPLACE_DECOMPOSITION_COMPOSITION_OK);
    }
    delete state;
    return result;
}

void Destroy(RunResult& result) {
    laplace_decomposition_composition_plan_destroy(&result.plan);
    laplace_decomposition_result_destroy(&result.decomposition);
}

bool SameReference(
    const laplace_composition_operand& left,
    const laplace_composition_operand& right) {
    return left.reference_index == right.reference_index &&
        left.multiplicity == right.multiplicity &&
        left.relationship_metadata == right.relationship_metadata &&
        left.reference_kind == right.reference_kind &&
        left.flags == right.flags;
}

bool ZeroReference(const laplace_composition_operand& value) {
    const laplace_composition_operand zero{};
    return std::memcmp(&value, &zero, sizeof(value)) == 0;
}

TEST(DecompositionStructuredProvider, PreservesParentFieldOrdinalAliasAndErrorState) {
    auto run = Run(7u);
    ASSERT_NE(run.decomposition, nullptr);
    ASSERT_NE(run.plan, nullptr);

    std::size_t span_count = 0u;
    const auto* spans = laplace_decomposition_spans(run.decomposition, &span_count);
    ASSERT_NE(spans, nullptr);
    ASSERT_EQ(span_count, 5u);

    EXPECT_EQ(spans[1].parent_span_index, 0u);
    EXPECT_EQ(spans[1].depth, 1u);
    EXPECT_EQ(spans[1].kind, UINT64_C(0x1000000000000001));
    EXPECT_EQ(spans[1].grammar_kind, UINT64_C(0x1000000000000011));
    EXPECT_EQ(spans[1].field_kind, 0u);
    EXPECT_EQ(spans[1].sibling_ordinal, 0u);

    for (std::size_t index = 2u; index < span_count; ++index) {
        EXPECT_EQ(spans[index].parent_span_index, 1u) << index;
        EXPECT_EQ(spans[index].depth, 2u) << index;
    }
    EXPECT_EQ(spans[2].field_kind, 7u);
    EXPECT_EQ(spans[2].sibling_ordinal, 1u);
    EXPECT_EQ(spans[3].field_kind, 8u);
    EXPECT_EQ(spans[3].sibling_ordinal, 2u);
    EXPECT_NE(spans[3].syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_MISSING, 0u);
    EXPECT_EQ(spans[3].byte_start, spans[3].byte_end);
    EXPECT_EQ(spans[3].flags, 0u);
    EXPECT_EQ(spans[4].field_kind, 9u);
    EXPECT_EQ(spans[4].sibling_ordinal, 3u);
    EXPECT_NE(spans[4].syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_ERROR, 0u);
    EXPECT_NE(spans[4].syntax_flags & LAPLACE_DECOMPOSITION_SYNTAX_HAS_ERROR, 0u);

    ASSERT_EQ(run.view.span_count, 5u);
    ASSERT_NE(run.view.span_has_content, nullptr);
    EXPECT_EQ(run.view.span_has_content[0], 1u);
    EXPECT_EQ(run.view.span_has_content[1], 1u);
    EXPECT_EQ(run.view.span_has_content[2], 1u);
    EXPECT_EQ(run.view.span_has_content[3], 0u);
    EXPECT_EQ(run.view.span_has_content[4], 1u);
    EXPECT_TRUE(ZeroReference(run.view.span_references[3]));

    /* The decomposition root and provider root cover the same bytes. Syntax
     * identity may differ, but canonical content must not be reminted. */
    EXPECT_TRUE(SameReference(
        run.view.span_references[0], run.view.span_references[1]));

    Destroy(run);
}

TEST(DecompositionStructuredProvider, FieldWitnessChangesTraceButNotCanonicalContent) {
    auto first = Run(7u);
    auto second = Run(77u);
    ASSERT_NE(first.plan, nullptr);
    ASSERT_NE(second.plan, nullptr);
    EXPECT_NE(std::memcmp(
                  first.view.trace_fingerprint.bytes,
                  second.view.trace_fingerprint.bytes,
                  sizeof(first.view.trace_fingerprint.bytes)),
              0);
    ASSERT_EQ(first.view.span_count, second.view.span_count);
    for (std::uint64_t index = 0u; index < first.view.span_count; ++index) {
        EXPECT_EQ(first.view.span_has_content[index],
                  second.view.span_has_content[index]);
        if (first.view.span_has_content[index] != 0u) {
            EXPECT_TRUE(SameReference(
                first.view.span_references[index],
                second.view.span_references[index])) << index;
        }
    }
    Destroy(first);
    Destroy(second);
}

}  // namespace