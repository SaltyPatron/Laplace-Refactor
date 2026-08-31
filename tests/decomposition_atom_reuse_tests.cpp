#include "laplace/decomposition.h"
#include "laplace/decomposition_composition.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace {

constexpr std::uint64_t AtomWitnessKind = UINT64_C(0x41544f4d00000001);
constexpr std::string_view TextMedia{"text/plain"};

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 digest{};
    digest.bytes[0] = marker;
    digest.bytes[31] = static_cast<std::uint8_t>(marker ^ 0x5au);
    return digest;
}

laplace_decomposition_status RootOnlyApplicable(
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

laplace_decomposition_status EmitTerminalWitness(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    if (span == nullptr || emit == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    return emit(
        emit_state,
        span->byte_start,
        span->byte_end,
        AtomWitnessKind,
        LAPLACE_DECOMPOSITION_SPAN_TEXT) == 0
        ? LAPLACE_DECOMPOSITION_OK
        : LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
}

TEST(DecompositionComposition, SingleCodepointRootReusesTierZeroReference) {
    constexpr std::array<std::uint8_t, 1> bytes{{'A'}};
    laplace_decomposition_provider_v1 provider{};
    provider.provider_fingerprint = Fingerprint(0x11u);
    provider.applicable = RootOnlyApplicable;
    provider.apply = EmitTerminalWitness;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;

    laplace_decomposition_input decomposition_input{};
    decomposition_input.content.bytes = bytes.data();
    decomposition_input.content.byte_count = bytes.size();
    decomposition_input.content.media_type = TextMedia.data();
    decomposition_input.content.media_type_byte_count = TextMedia.size();
    decomposition_input.providers = &provider;
    decomposition_input.provider_count = 1u;
    decomposition_input.maximum_spans = 4u;
    decomposition_input.maximum_depth = 2u;

    laplace_decomposition_result* decomposition = nullptr;
    ASSERT_EQ(
        laplace_decomposition_run(&decomposition_input, &decomposition),
        LAPLACE_DECOMPOSITION_OK);
    ASSERT_NE(decomposition, nullptr);

    laplace_decomposition_composition_input composition_input{};
    composition_input.content = &decomposition_input.content;
    composition_input.decomposition = decomposition;
    composition_input.recipe_fingerprint = Fingerprint(0x22u);
    composition_input.geometry_epoch = Fingerprint(0x33u);
    composition_input.occurrence_context_fingerprint = Fingerprint(0x44u);
    composition_input.source_ordinal_base = 100u;

    laplace_decomposition_composition_plan* plan = nullptr;
    ASSERT_EQ(
        laplace_decomposition_composition_plan_create(&composition_input, &plan),
        LAPLACE_DECOMPOSITION_COMPOSITION_OK);
    ASSERT_NE(plan, nullptr);

    laplace_decomposition_composition_plan_view view{};
    ASSERT_EQ(
        laplace_decomposition_composition_plan_view_get(plan, &view),
        LAPLACE_DECOMPOSITION_COMPOSITION_OK);

    ASSERT_EQ(view.atom_count, 1u);
    ASSERT_NE(view.atom_positions, nullptr);
    EXPECT_EQ(view.atom_positions[0], static_cast<std::uint32_t>('A'));
    EXPECT_EQ(view.operand_count, 0u);
    EXPECT_EQ(view.operands, nullptr);
    EXPECT_EQ(view.request_count, 0u);
    EXPECT_EQ(view.requests, nullptr);
    EXPECT_EQ(
        view.root_reference.reference_kind,
        static_cast<std::uint32_t>(LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY));
    EXPECT_EQ(view.root_reference.reference_index, 0u);
    EXPECT_EQ(view.root_reference.multiplicity, 1u);
    EXPECT_EQ(view.root_reference.relationship_metadata, 0u);
    EXPECT_EQ(view.root_reference.flags, 0u);
    EXPECT_EQ(view.root_result_index, std::numeric_limits<std::uint64_t>::max());

    laplace_decomposition_composition_plan_destroy(&plan);
    laplace_decomposition_result_destroy(&decomposition);
    EXPECT_EQ(plan, nullptr);
    EXPECT_EQ(decomposition, nullptr);
}

}  // namespace
