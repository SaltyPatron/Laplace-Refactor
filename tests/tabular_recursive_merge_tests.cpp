#include "tabular_source_recursive_merge.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 value{};
    value.bytes[0] = marker;
    value.bytes[31] = static_cast<std::uint8_t>(marker ^ 0xa5u);
    return value;
}

bool DigestEqual(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_composition_operand Known(const std::uint64_t index) {
    return laplace_composition_operand{
        index,
        1u,
        0u,
        LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,
        0u};
}

laplace_composition_operand Prior(const std::uint64_t index) {
    return laplace_composition_operand{
        index,
        1u,
        0u,
        LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT,
        0u};
}

laplace_composition_request Request(
    const std::uint64_t first,
    const std::uint64_t count,
    const std::uint64_t ordinal,
    const std::uint8_t marker) {
    return laplace_composition_request{
        first,
        count,
        ordinal,
        1u,
        0u,
        Fingerprint(marker),
        Fingerprint(static_cast<std::uint8_t>(marker + 1u)),
        Fingerprint(static_cast<std::uint8_t>(marker + 2u))};
}

TEST(TabularRecursiveMerge,
     InsertsCanonicalPlanBeforeFinalSourceRootWithGlobalReferences) {
    std::vector<std::uint32_t> destination_atoms{'a', 'x'};
    std::vector<laplace_composition_operand> destination_operands{
        Known(1u), Prior(0u), Known(0u)};
    std::vector<laplace_composition_request> destination_requests{
        Request(0u, 1u, 1u, 0x10u),
        Request(1u, 2u, 2u, 0x11u)};
    const laplace_composition_request original_root = destination_requests.back();
    constexpr std::uint64_t RootIndex = 1u;

    constexpr std::array<std::uint32_t, 3> SourceAtoms{{'a', 'b', 'c'}};
    const std::array<laplace_composition_operand, 5> SourceOperands{{
        Known(0u), Known(1u), Known(2u), Prior(0u), Known(0u)}};
    const std::array<laplace_composition_request, 2> SourceRequests{{
        Request(0u, 3u, 99u, 0x20u),
        Request(3u, 2u, 100u, 0x30u)}};

    laplace_decomposition_composition_plan_view source{};
    source.atom_positions = SourceAtoms.data();
    source.operands = SourceOperands.data();
    source.requests = SourceRequests.data();
    source.atom_count = SourceAtoms.size();
    source.operand_count = SourceOperands.size();
    source.request_count = SourceRequests.size();

    ASSERT_EQ(
        laplace::internal::MergeRecursiveCanonicalComposition(
            destination_atoms,
            destination_operands,
            destination_requests,
            RootIndex,
            source),
        LAPLACE_TABULAR_SOURCE_OK);

    EXPECT_EQ(
        destination_atoms,
        (std::vector<std::uint32_t>{'a', 'x', 'b', 'c'}));
    if (destination_operands.size() != 8u ||
        destination_requests.size() != 4u) {
        ADD_FAILURE()
            << "recursive canonical merge did not materialize its exact global plan";
        return;
    }

    EXPECT_EQ(destination_operands[3].reference_kind,
              LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY);
    EXPECT_EQ(destination_operands[3].reference_index, 0u);
    EXPECT_EQ(destination_operands[4].reference_index, 2u);
    EXPECT_EQ(destination_operands[5].reference_index, 3u);
    EXPECT_EQ(destination_operands[6].reference_kind,
              LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT);
    EXPECT_EQ(destination_operands[6].reference_index, 1u);
    EXPECT_EQ(destination_operands[7].reference_index, 0u);

    EXPECT_EQ(destination_requests[0].source_ordinal, 1u);
    EXPECT_EQ(destination_requests[1].first_operand, 3u);
    EXPECT_EQ(destination_requests[1].operand_count, 3u);
    EXPECT_EQ(destination_requests[1].source_ordinal, 2u);
    EXPECT_EQ(destination_requests[2].first_operand, 6u);
    EXPECT_EQ(destination_requests[2].operand_count, 2u);
    EXPECT_EQ(destination_requests[2].source_ordinal, 3u);
    EXPECT_EQ(destination_requests[1].flags, 0u);
    EXPECT_EQ(destination_requests[2].flags, 0u);

    const auto& shifted_root = destination_requests.back();
    EXPECT_EQ(shifted_root.first_operand, original_root.first_operand);
    EXPECT_EQ(shifted_root.operand_count, original_root.operand_count);
    EXPECT_EQ(shifted_root.recipe_version, original_root.recipe_version);
    EXPECT_EQ(shifted_root.flags, original_root.flags);
    EXPECT_EQ(shifted_root.source_ordinal, 4u);
    EXPECT_EQ(destination_operands[1].reference_kind,
              LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT);
    EXPECT_EQ(destination_operands[1].reference_index, 0u);
}

TEST(TabularRecursiveMerge, RetainsWitnessMetadataBoundToCanonicalContent) {
    const std::vector<std::uint32_t> destination_atoms{'x', 'a', 'b'};
    constexpr std::array<std::uint32_t, 2> SourceAtoms{{'a', 'b'}};
    const std::array<laplace_composition_operand, 2> SourceOperands{{
        Known(0u), Known(1u)}};
    const std::array<laplace_composition_request, 1> SourceRequests{{
        Request(0u, 2u, 99u, 0x20u)}};
    const std::array<laplace_composition_operand, 2> SpanReferences{{
        Prior(0u), Known(1u)}};

    laplace_decomposition_composition_plan_view source{};
    source.atom_positions = SourceAtoms.data();
    source.operands = SourceOperands.data();
    source.requests = SourceRequests.data();
    source.span_references = SpanReferences.data();
    source.atom_count = SourceAtoms.size();
    source.operand_count = SourceOperands.size();
    source.request_count = SourceRequests.size();
    source.span_count = SpanReferences.size();

    constexpr char MediaType[] = "text/plain";
    std::array<laplace::internal::RecursiveDecompositionWitnessInput, 2> spans{};
    spans[0].provider_fingerprint = Fingerprint(0x41u);
    spans[0].media_type = MediaType;
    spans[0].byte_start = 0u;
    spans[0].byte_end = 2u;
    spans[0].parent_span_index = std::numeric_limits<std::uint64_t>::max();
    spans[0].kind = 0x101u;
    spans[0].media_type_byte_count = sizeof(MediaType) - 1u;
    spans[0].depth = 0u;
    spans[0].flags = 0u;
    spans[1].provider_fingerprint = Fingerprint(0x42u);
    spans[1].media_type = MediaType;
    spans[1].byte_start = 1u;
    spans[1].byte_end = 2u;
    spans[1].parent_span_index = 0u;
    spans[1].kind = 0x102u;
    spans[1].media_type_byte_count = sizeof(MediaType) - 1u;
    spans[1].depth = 1u;
    spans[1].flags = 3u;

    std::vector<laplace_tabular_decomposition_witness> witnesses;
    std::vector<std::uint8_t> media_types;
    const laplace_digest256 trace = Fingerprint(0x77u);
    constexpr std::uint64_t RequestBase = 4u;
    ASSERT_EQ(
        laplace::internal::AppendRecursiveDecompositionWitnesses(
            destination_atoms,
            RequestBase,
            source,
            3u,
            trace,
            spans.data(),
            spans.size(),
            witnesses,
            media_types),
        LAPLACE_TABULAR_SOURCE_OK);

    ASSERT_EQ(witnesses.size(), 2u);
    ASSERT_EQ(media_types.size(), 2u * (sizeof(MediaType) - 1u));
    EXPECT_TRUE(DigestEqual(witnesses[0].trace_fingerprint, trace));
    EXPECT_TRUE(DigestEqual(
        witnesses[1].provider_fingerprint, spans[1].provider_fingerprint));
    EXPECT_EQ(witnesses[0].artifact_index, 3u);
    EXPECT_EQ(witnesses[0].span_index, 0u);
    EXPECT_EQ(witnesses[0].parent_span_index,
              std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(witnesses[1].parent_span_index, 0u);
    EXPECT_EQ(witnesses[1].byte_start, 1u);
    EXPECT_EQ(witnesses[1].byte_end, 2u);
    EXPECT_EQ(witnesses[1].kind, 0x102u);
    EXPECT_EQ(witnesses[1].depth, 1u);
    EXPECT_EQ(witnesses[1].flags, 3u);

    EXPECT_EQ(witnesses[0].canonical_content.reference_kind,
              LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT);
    EXPECT_EQ(witnesses[0].canonical_content.reference_index, RequestBase);
    EXPECT_EQ(witnesses[1].canonical_content.reference_kind,
              LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY);
    EXPECT_EQ(witnesses[1].canonical_content.reference_index, 2u);
    EXPECT_EQ(witnesses[0].canonical_content.relationship_metadata, 0u);
    EXPECT_EQ(witnesses[1].canonical_content.relationship_metadata, 0u);

    const auto first_offset =
        static_cast<std::size_t>(witnesses[0].media_type_byte_offset);
    const auto second_offset =
        static_cast<std::size_t>(witnesses[1].media_type_byte_offset);
    EXPECT_EQ(
        std::string(
            reinterpret_cast<const char*>(media_types.data() + first_offset),
            static_cast<std::size_t>(witnesses[0].media_type_byte_count)),
        MediaType);
    EXPECT_EQ(
        std::string(
            reinterpret_cast<const char*>(media_types.data() + second_offset),
            static_cast<std::size_t>(witnesses[1].media_type_byte_count)),
        MediaType);
}

TEST(TabularRecursiveMerge, RejectsForwardCanonicalResultReferences) {
    std::vector<std::uint32_t> destination_atoms{'x'};
    std::vector<laplace_composition_operand> destination_operands{Known(0u)};
    std::vector<laplace_composition_request> destination_requests{
        Request(0u, 1u, 1u, 0x10u)};

    constexpr std::array<std::uint32_t, 1> SourceAtoms{{'a'}};
    const std::array<laplace_composition_operand, 1> SourceOperands{{Prior(0u)}};
    const std::array<laplace_composition_request, 1> SourceRequests{{
        Request(0u, 1u, 1u, 0x40u)}};

    laplace_decomposition_composition_plan_view source{};
    source.atom_positions = SourceAtoms.data();
    source.operands = SourceOperands.data();
    source.requests = SourceRequests.data();
    source.atom_count = SourceAtoms.size();
    source.operand_count = SourceOperands.size();
    source.request_count = SourceRequests.size();

    EXPECT_EQ(
        laplace::internal::MergeRecursiveCanonicalComposition(
            destination_atoms,
            destination_operands,
            destination_requests,
            0u,
            source),
        LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID);
}

TEST(TabularRecursiveMerge, RejectsWitnessMetadataAndImplicitOccurrenceFlags) {
    std::vector<std::uint32_t> destination_atoms{'x'};
    std::vector<laplace_composition_operand> destination_operands{Known(0u)};
    std::vector<laplace_composition_request> destination_requests{
        Request(0u, 1u, 1u, 0x10u)};
    const auto original_atoms = destination_atoms;
    const auto original_operands = destination_operands;
    const auto original_requests = destination_requests;

    constexpr std::array<std::uint32_t, 1> SourceAtoms{{'a'}};
    std::array<laplace_composition_operand, 1> source_operands{{Known(0u)}};
    std::array<laplace_composition_request, 1> source_requests{{
        Request(0u, 1u, 1u, 0x50u)}};

    laplace_decomposition_composition_plan_view source{};
    source.atom_positions = SourceAtoms.data();
    source.operands = source_operands.data();
    source.requests = source_requests.data();
    source.atom_count = SourceAtoms.size();
    source.operand_count = source_operands.size();
    source.request_count = source_requests.size();

    source_operands[0].relationship_metadata = 1u;
    EXPECT_EQ(
        laplace::internal::MergeRecursiveCanonicalComposition(
            destination_atoms,
            destination_operands,
            destination_requests,
            0u,
            source),
        LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID);
    EXPECT_EQ(destination_atoms, original_atoms);
    EXPECT_EQ(destination_operands.size(), original_operands.size());
    EXPECT_EQ(destination_requests.size(), original_requests.size());

    source_operands[0].relationship_metadata = 0u;
    source_requests[0].flags = 1u;
    EXPECT_EQ(
        laplace::internal::MergeRecursiveCanonicalComposition(
            destination_atoms,
            destination_operands,
            destination_requests,
            0u,
            source),
        LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID);
    EXPECT_EQ(destination_atoms, original_atoms);
    EXPECT_EQ(destination_operands.size(), original_operands.size());
    EXPECT_EQ(destination_requests.size(), original_requests.size());
}

}  // namespace
