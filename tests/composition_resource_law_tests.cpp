#include "laplace/composition.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "context_fixture.h"

namespace {

void Fill(laplace_digest256& digest, const std::uint8_t seed) {
    for (std::size_t index = 0u; index < sizeof(digest.bytes); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_composition_known_entity Atom(
    const std::uint32_t position,
    const laplace_point4d& point,
    const std::uint8_t physicality_seed) {
    laplace_composition_known_entity result{};
    EXPECT_EQ(laplace_identity_codepoint_witness(
                  position, &result.entity_id, &result.identity_witness),
              LAPLACE_IDENTITY_OK);
    Fill(result.physicality_id, physicality_seed);
    result.centroid = point;
    result.atom = position;
    result.has_atom = 1u;
    return result;
}

laplace_composition_request Request(
    const std::uint64_t source_ordinal,
    const std::uint8_t recipe_seed) {
    laplace_composition_request request{};
    request.first_operand = 0u;
    request.operand_count = 2u;
    request.source_ordinal = source_ordinal;
    request.recipe_version = 1u;
    request.flags = 0u;
    Fill(request.recipe_fingerprint, recipe_seed);
    Fill(request.geometry_epoch,
         static_cast<std::uint8_t>(recipe_seed + 0x20u));
    Fill(request.occurrence_context_fingerprint,
         static_cast<std::uint8_t>(recipe_seed + 0x40u));
    return request;
}

laplace_composition_working_set_summary BuildSummary(
    const std::vector<laplace_composition_request>& requests) {
    auto context = laplace_test_context(3u);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024u * 1024u;

    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x11u);
    Fill(calculation_recipe, 0x31u);

    const std::array<laplace_composition_known_entity, 2> known{{
        Atom('a', laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x51u),
        Atom('b', laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x71u)}};
    const std::array<laplace_composition_operand, 2> operands{{
        {0u, 1u, 0u, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0u},
        {1u, 1u, 0u, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0u}}};

    const laplace_composition_working_set_input input{
        &context,
        &source,
        &calculation_recipe,
        known.data(),
        known.size(),
        operands.data(),
        operands.size(),
        requests.data(),
        requests.size(),
        256u,
        0u};

    laplace_composition_working_set* working_set = nullptr;
    EXPECT_EQ(laplace_composition_working_set_create(&input, &working_set),
              LAPLACE_COMPOSITION_OK);
    EXPECT_NE(working_set, nullptr);
    laplace_composition_working_set_summary summary{};
    if (working_set != nullptr) {
        EXPECT_EQ(laplace_composition_working_set_summary_get(
                      working_set, &summary),
                  LAPLACE_COMPOSITION_OK);
    }
    laplace_composition_working_set_destroy(&working_set);
    return summary;
}

TEST(CompositionResourceLaw,
     DuplicateRequestsDoNotReserveDistinctPhysicalityAndWitnessMemory) {
    constexpr std::size_t RequestCount = 32u;
    std::vector<laplace_composition_request> duplicated;
    std::vector<laplace_composition_request> distinct_physicalities;
    duplicated.reserve(RequestCount);
    distinct_physicalities.reserve(RequestCount);

    const laplace_composition_request repeated = Request(1u, 0x21u);
    for (std::size_t index = 0u; index < RequestCount; ++index) {
        laplace_composition_request duplicate = repeated;
        duplicate.source_ordinal = static_cast<std::uint64_t>(index) + 1u;
        duplicated.push_back(duplicate);

        distinct_physicalities.push_back(Request(
            static_cast<std::uint64_t>(index) + 1u,
            static_cast<std::uint8_t>(0x41u + index)));
    }

    const laplace_composition_working_set_summary duplicate_summary =
        BuildSummary(duplicated);
    const laplace_composition_working_set_summary distinct_summary =
        BuildSummary(distinct_physicalities);

    ASSERT_EQ(duplicate_summary.request_count, RequestCount);
    ASSERT_EQ(distinct_summary.request_count, RequestCount);
    EXPECT_EQ(duplicate_summary.unique_entity_count,
              distinct_summary.unique_entity_count);
    EXPECT_EQ(duplicate_summary.unique_physicality_count, 1u);
    EXPECT_EQ(distinct_summary.unique_physicality_count, RequestCount);
    EXPECT_EQ(duplicate_summary.occurrence_count, 0u);
    EXPECT_EQ(distinct_summary.occurrence_count, 0u);
    EXPECT_LT(duplicate_summary.estimated_peak_working_bytes,
              distinct_summary.estimated_peak_working_bytes);
}

}  // namespace
