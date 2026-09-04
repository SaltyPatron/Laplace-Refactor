#include "laplace/composition.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "context_fixture.h"

namespace {

constexpr std::uint64_t RequestCount = 8U;

void Fill(laplace_digest256& digest, std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(digest.bytes); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_composition_known_entity Atom(
    std::uint32_t position,
    const laplace_point4d& point,
    std::uint8_t physicality_seed) {
    laplace_composition_known_entity result{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(
            position, &result.entity_id, &result.identity_witness),
        LAPLACE_IDENTITY_OK);
    Fill(result.physicality_id, physicality_seed);
    result.centroid = point;
    result.atom = position;
    result.has_atom = 1U;
    return result;
}

laplace_composition_working_set_summary Scenario(
    bool chain,
    bool emit_occurrences) {
    auto context = laplace_test_context(0x31U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;

    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x21U);
    Fill(calculation_recipe, 0x41U);

    const std::array<laplace_composition_known_entity, 2> known{{
        Atom(0x41U, laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x61U),
        Atom(0x42U, laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x81U)}};

    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    operands.reserve(static_cast<std::size_t>(RequestCount * 2U));
    requests.reserve(static_cast<std::size_t>(RequestCount));
    for (std::uint64_t index = 0U; index < RequestCount; ++index) {
        laplace_composition_operand first{};
        first.reference_index = chain && index != 0U ? index - 1U : 0U;
        first.multiplicity = 1U;
        first.reference_kind = chain && index != 0U
            ? LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT
            : LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        laplace_composition_operand second{};
        second.reference_index = 1U;
        second.multiplicity = 1U;
        second.reference_kind = LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        operands.push_back(first);
        operands.push_back(second);

        laplace_composition_request request{};
        request.first_operand = index * 2U;
        request.operand_count = 2U;
        request.source_ordinal = index + 1U;
        request.recipe_version = 1U;
        request.flags = emit_occurrences
            ? LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE
            : 0U;
        Fill(request.recipe_fingerprint, 0xA1U);
        Fill(request.geometry_epoch, 0xB1U);
        Fill(request.occurrence_context_fingerprint, 0xC1U);
        requests.push_back(request);
    }

    laplace_composition_working_set_input input{};
    input.context = &context;
    input.source_fingerprint = &source;
    input.calculation_recipe_fingerprint = &calculation_recipe;
    input.known_entities = known.data();
    input.known_entity_count = known.size();
    input.operands = operands.data();
    input.operand_count = operands.size();
    input.requests = requests.data();
    input.request_count = requests.size();

    laplace_composition_working_set* working_set = nullptr;
    const auto status = laplace_composition_working_set_create(&input, &working_set);
    EXPECT_EQ(status, LAPLACE_COMPOSITION_OK);
    laplace_composition_working_set_summary summary{};
    if (status == LAPLACE_COMPOSITION_OK) {
        EXPECT_EQ(
            laplace_composition_working_set_summary_get(working_set, &summary),
            LAPLACE_COMPOSITION_OK);
    }
    laplace_composition_working_set_destroy(&working_set);
    return summary;
}

TEST(
    CompositionResourceAccounting,
    CanonicalReuseBoundsPhysicalWorkAndSeparatesOccurrences) {
    const auto duplicate = Scenario(false, false);
    const auto chain = Scenario(true, false);
    const auto observed_duplicate = Scenario(false, true);

    EXPECT_EQ(duplicate.request_count, RequestCount);
    EXPECT_EQ(chain.request_count, RequestCount);
    EXPECT_EQ(observed_duplicate.request_count, RequestCount);
    EXPECT_EQ(duplicate.operand_count, RequestCount * 2U);
    EXPECT_EQ(chain.operand_count, RequestCount * 2U);

    EXPECT_EQ(duplicate.unique_entity_count, 3U);
    EXPECT_EQ(duplicate.unique_physicality_count, 1U);
    EXPECT_EQ(duplicate.trajectory_vertex_count, 2U);
    EXPECT_EQ(duplicate.occurrence_count, 0U);

    EXPECT_EQ(chain.unique_entity_count, RequestCount + 2U);
    EXPECT_EQ(chain.unique_physicality_count, RequestCount);
    EXPECT_EQ(chain.trajectory_vertex_count, RequestCount * 2U);
    EXPECT_EQ(chain.occurrence_count, 0U);

    EXPECT_EQ(observed_duplicate.unique_entity_count, duplicate.unique_entity_count);
    EXPECT_EQ(
        observed_duplicate.unique_physicality_count,
        duplicate.unique_physicality_count);
    EXPECT_EQ(
        observed_duplicate.trajectory_vertex_count,
        duplicate.trajectory_vertex_count);
    EXPECT_EQ(observed_duplicate.occurrence_count, RequestCount);

    EXPECT_EQ(duplicate.semantic_calculation_count, RequestCount);
    EXPECT_EQ(chain.semantic_calculation_count, RequestCount);
    EXPECT_EQ(observed_duplicate.semantic_calculation_count, RequestCount);

    EXPECT_EQ(duplicate.planned_entity_upper_bound, RequestCount + 2U);
    EXPECT_EQ(chain.planned_entity_upper_bound, RequestCount + 2U);
    EXPECT_EQ(duplicate.planned_physicality_upper_bound, RequestCount);
    EXPECT_EQ(chain.planned_physicality_upper_bound, RequestCount);
    EXPECT_EQ(duplicate.planned_trajectory_carrier_upper_bound, RequestCount * 2U);
    EXPECT_EQ(chain.planned_trajectory_carrier_upper_bound, RequestCount * 2U);
    EXPECT_EQ(duplicate.planned_occurrence_upper_bound, 0U);
    EXPECT_EQ(chain.planned_occurrence_upper_bound, 0U);
    EXPECT_EQ(observed_duplicate.planned_occurrence_upper_bound, RequestCount);

    EXPECT_EQ(
        duplicate.estimated_peak_working_bytes,
        chain.estimated_peak_working_bytes);
    EXPECT_LT(
        duplicate.estimated_peak_working_bytes,
        observed_duplicate.estimated_peak_working_bytes);
}

}  // namespace
