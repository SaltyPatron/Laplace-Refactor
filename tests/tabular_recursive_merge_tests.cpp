#include "tabular_source_recursive_merge.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

laplace_digest256 Fingerprint(const std::uint8_t marker) {
    laplace_digest256 value{};
    value.bytes[0] = marker;
    value.bytes[31] = static_cast<std::uint8_t>(marker ^ 0xa5u);
    return value;
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
     AppendsCanonicalPlanWithGlobalAtomAndPriorResultReferences) {
    std::vector<std::uint32_t> destination_atoms{'a', 'x'};
    std::vector<laplace_composition_operand> destination_operands{Known(1u)};
    std::vector<laplace_composition_request> destination_requests{
        Request(0u, 1u, 1u, 0x10u)};

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
            source),
        LAPLACE_TABULAR_SOURCE_OK);

    EXPECT_EQ(
        destination_atoms,
        (std::vector<std::uint32_t>{'a', 'x', 'b', 'c'}));
    ASSERT_EQ(destination_operands.size(), 6u);
    ASSERT_EQ(destination_requests.size(), 3u);

    EXPECT_EQ(destination_operands[1].reference_kind,
              LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY);
    EXPECT_EQ(destination_operands[1].reference_index, 0u);
    EXPECT_EQ(destination_operands[2].reference_index, 2u);
    EXPECT_EQ(destination_operands[3].reference_index, 3u);
    EXPECT_EQ(destination_operands[4].reference_kind,
              LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT);
    EXPECT_EQ(destination_operands[4].reference_index, 1u);
    EXPECT_EQ(destination_operands[5].reference_index, 0u);

    EXPECT_EQ(destination_requests[1].first_operand, 1u);
    EXPECT_EQ(destination_requests[1].operand_count, 3u);
    EXPECT_EQ(destination_requests[1].source_ordinal, 2u);
    EXPECT_EQ(destination_requests[2].first_operand, 4u);
    EXPECT_EQ(destination_requests[2].operand_count, 2u);
    EXPECT_EQ(destination_requests[2].source_ordinal, 3u);
    EXPECT_EQ(destination_requests[1].flags, 0u);
    EXPECT_EQ(destination_requests[2].flags, 0u);
}

TEST(TabularRecursiveMerge, RejectsForwardCanonicalResultReferences) {
    std::vector<std::uint32_t> destination_atoms;
    std::vector<laplace_composition_operand> destination_operands;
    std::vector<laplace_composition_request> destination_requests;

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
            source),
        LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID);
}

}  // namespace
