#include "laplace/decomposition.h"
#include "laplace/decomposition_composition.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view MediaType{"text/plain"};
constexpr std::uint64_t FirstKind = UINT64_C(0x5350414e00000001);
constexpr std::uint64_t SecondKind = UINT64_C(0x5350414e00000002);
constexpr std::uint64_t AlternateFirstKind = UINT64_C(0x5350414e00000011);
constexpr std::uint64_t AlternateSecondKind = UINT64_C(0x5350414e00000012);

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 value{};
    value.bytes[0] = marker;
    value.bytes[31] = static_cast<std::uint8_t>(marker ^ 0xa5u);
    return value;
}

bool DigestEquals(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool ReferenceEquals(
    const laplace_composition_operand& left,
    const laplace_composition_operand& right) {
    return left.reference_index == right.reference_index &&
        left.multiplicity == right.multiplicity &&
        left.relationship_metadata == right.relationship_metadata &&
        left.reference_kind == right.reference_kind &&
        left.flags == right.flags;
}

struct ProviderState {
    std::uint64_t first_kind{FirstKind};
    std::uint64_t second_kind{SecondKind};
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

laplace_decomposition_status Apply(
    void* provider_state,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (provider_state == nullptr || span == nullptr || emit == nullptr ||
        span->byte_end - span->byte_start != 6u) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    const auto& state = *static_cast<const ProviderState*>(provider_state);
    const std::uint64_t midpoint = span->byte_start + 3u;
    if (emit(
            emit_state,
            span->byte_start,
            midpoint,
            state.first_kind,
            LAPLACE_DECOMPOSITION_SPAN_TEXT) != 0 ||
        emit(
            emit_state,
            midpoint,
            span->byte_end,
            state.second_kind,
            LAPLACE_DECOMPOSITION_SPAN_TEXT) != 0) {
        return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
    }
    return LAPLACE_DECOMPOSITION_OK;
}

struct Snapshot {
    laplace_digest256 trace_fingerprint{};
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::vector<laplace_composition_operand> span_references;
    laplace_composition_operand root_reference{};
    std::uint64_t root_result_index{};
};

std::optional<Snapshot> MakeSnapshot(
    const std::uint8_t provider_marker,
    const std::uint64_t first_kind,
    const std::uint64_t second_kind) {
    constexpr std::array<std::uint8_t, 6> Bytes{{'a', 'b', 'c', 'a', 'b', 'c'}};
    ProviderState state{first_kind, second_kind};
    laplace_decomposition_provider_v1 provider{};
    provider.state = &state;
    provider.provider_fingerprint = Fingerprint(provider_marker);
    provider.applicable = Applicable;
    provider.apply = Apply;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;

    laplace_decomposition_input decomposition_input{};
    decomposition_input.content.bytes = Bytes.data();
    decomposition_input.content.byte_count = Bytes.size();
    decomposition_input.content.media_type = MediaType.data();
    decomposition_input.content.media_type_byte_count = MediaType.size();
    decomposition_input.providers = &provider;
    decomposition_input.provider_count = 1u;
    decomposition_input.maximum_spans = 8u;
    decomposition_input.maximum_depth = 4u;

    laplace_decomposition_result* decomposition = nullptr;
    if (laplace_decomposition_run(&decomposition_input, &decomposition) !=
            LAPLACE_DECOMPOSITION_OK ||
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
            &composition_input, &plan) !=
            LAPLACE_DECOMPOSITION_COMPOSITION_OK ||
        plan == nullptr) {
        ADD_FAILURE() << "composition plan was not produced";
        laplace_decomposition_result_destroy(&decomposition);
        return std::nullopt;
    }

    laplace_decomposition_composition_plan_view view{};
    if (laplace_decomposition_composition_plan_view_get(plan, &view) !=
        LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
        ADD_FAILURE() << "composition plan view was not available";
        laplace_decomposition_composition_plan_destroy(&plan);
        laplace_decomposition_result_destroy(&decomposition);
        return std::nullopt;
    }

    Snapshot snapshot{};
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
    snapshot.span_references.assign(
        view.span_references,
        view.span_references + static_cast<std::size_t>(view.span_count));
    snapshot.root_reference = view.root_reference;
    snapshot.root_result_index = view.root_result_index;

    laplace_decomposition_composition_plan_destroy(&plan);
    laplace_decomposition_result_destroy(&decomposition);
    return snapshot;
}

TEST(DecompositionWitnessBinding,
     EqualSpanContentReusesCanonicalReferenceAcrossOffsetsAndWitnessMetadata) {
    const auto first = MakeSnapshot(0x11u, FirstKind, SecondKind);
    const auto second =
        MakeSnapshot(0x77u, AlternateFirstKind, AlternateSecondKind);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    EXPECT_FALSE(DigestEquals(
        first->trace_fingerprint, second->trace_fingerprint));
    EXPECT_EQ(first->atom_positions, second->atom_positions);
    EXPECT_EQ(first->atom_positions,
              (std::vector<std::uint32_t>{'a', 'b', 'c'}));

    ASSERT_EQ(first->span_references.size(), 3u);
    ASSERT_EQ(second->span_references.size(), 3u);
    ASSERT_EQ(first->requests.size(), 2u);
    ASSERT_EQ(second->requests.size(), 2u);

    EXPECT_EQ(
        first->root_reference.reference_kind,
        static_cast<std::uint32_t>(LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT));
    EXPECT_EQ(first->root_reference.reference_index, 0u);
    EXPECT_EQ(first->root_result_index, 0u);
    EXPECT_TRUE(ReferenceEquals(
        first->root_reference, first->span_references[0]));

    EXPECT_EQ(
        first->span_references[1].reference_kind,
        static_cast<std::uint32_t>(LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT));
    EXPECT_EQ(first->span_references[1].reference_index, 1u);
    EXPECT_TRUE(ReferenceEquals(
        first->span_references[1], first->span_references[2]));
    EXPECT_TRUE(ReferenceEquals(
        second->span_references[1], second->span_references[2]));

    for (std::size_t index = 0u; index < first->span_references.size(); ++index) {
        EXPECT_TRUE(ReferenceEquals(
            first->span_references[index], second->span_references[index]));
    }
    ASSERT_EQ(first->requests.size(), second->requests.size());
    for (std::size_t index = 0u; index < first->requests.size(); ++index) {
        EXPECT_EQ(first->requests[index].first_operand,
                  second->requests[index].first_operand);
        EXPECT_EQ(first->requests[index].operand_count,
                  second->requests[index].operand_count);
        EXPECT_EQ(first->requests[index].source_ordinal,
                  second->requests[index].source_ordinal);
    }
}

}  // namespace