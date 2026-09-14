#include "laplace/cognition_response_field.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

laplace_id128 Id(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t i = 0U; i < sizeof(value.bytes); ++i) {
        value.bytes[i] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
    }
    return value;
}

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t i = 0U; i < sizeof(value.bytes); ++i) {
        value.bytes[i] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
    }
    return value;
}

laplace_cognition_observation_candidate Candidate(
    const laplace_id128& target,
    const std::uint64_t source_state,
    const std::uint32_t relation,
    const std::uint32_t layer,
    const std::uint64_t multiplicity,
    const std::uint64_t gap,
    const std::uint8_t witness_seed,
    const std::uint8_t evidence_seed = 0U) {
    laplace_cognition_observation_candidate candidate{};
    candidate.target_entity_id = target;
    candidate.observation_fingerprint = Digest(witness_seed);
    if (evidence_seed != 0U) candidate.evidence_root_fingerprint = Digest(evidence_seed);
    candidate.source_state_index = source_state;
    candidate.multiplicity = multiplicity;
    candidate.gap = gap;
    candidate.relation_family = relation;
    candidate.source_layer = layer;
    candidate.direction = relation == LAPLACE_OBSERVATION_QUERY_CONTAINER
        ? LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE
        : LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
    return candidate;
}

TEST(CognitionResponseField, PreservesJointBackReactionAcrossTypedChannels) {
    const auto shared = Id(11U);
    const auto other = Id(61U);
    std::array<laplace_cognition_observation_candidate, 4> candidates{{
        Candidate(shared, 0U, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 2U, 3U, 21U),
        Candidate(shared, 1U, LAPLACE_OBSERVATION_QUERY_SEMANTIC,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY, 1U, 0U, 31U, 91U),
        Candidate(shared, 2U, LAPLACE_OBSERVATION_QUERY_SEMANTIC,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION, 4U, 0U, 41U),
        Candidate(other, 0U, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 1U, 8U, 51U),
    }};

    std::array<laplace_cognition_response_entry, 4> responses{};
    std::size_t response_count = 0U;
    laplace_cognition_response_receipt receipt{};
    ASSERT_EQ(laplace_cognition_response_field_measure(
                  candidates.data(), candidates.size(), responses.data(), responses.size(),
                  &response_count, &receipt),
              LAPLACE_COGNITION_RESPONSE_OK);
    ASSERT_EQ(response_count, 2U);
    EXPECT_EQ(receipt.candidate_count, 4U);
    EXPECT_EQ(receipt.response_count, 2U);
    EXPECT_EQ(receipt.distinct_source_state_count, 3U);
    EXPECT_EQ(receipt.independent_evidence_root_count, 1U);

    const laplace_cognition_response_entry* shared_response = nullptr;
    for (std::size_t i = 0U; i < response_count; ++i) {
        if (responses[i].entity_id.bytes[0] == shared.bytes[0]) shared_response = &responses[i];
    }
    ASSERT_NE(shared_response, nullptr);
    EXPECT_EQ(shared_response->source_state_count, 3U);
    EXPECT_EQ(shared_response->candidate_count, 3U);
    EXPECT_EQ(shared_response->multiplicity_mass, 7U);
    EXPECT_EQ(shared_response->minimum_gap, 0U);
    EXPECT_EQ(shared_response->independent_evidence_root_count, 1U);
    EXPECT_EQ(shared_response->relation_channel_mass[1], 2U);
    EXPECT_EQ(shared_response->relation_channel_mass[5], 5U);
    EXPECT_EQ(shared_response->source_channel_mass[0], 2U);
    EXPECT_EQ(shared_response->source_channel_mass[1], 1U);
    EXPECT_EQ(shared_response->source_channel_mass[2], 4U);
    EXPECT_EQ(shared_response->relation_family_mask,
              LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
                  LAPLACE_OBSERVATION_QUERY_SEMANTIC);
    EXPECT_EQ(shared_response->source_layer_mask,
              LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY |
                  LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY |
                  LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION);
}

TEST(CognitionResponseField, DoesNotPublishPartialOutputWhenCapacityIsTooSmall) {
    const auto first = Id(10U);
    const auto second = Id(40U);
    std::array<laplace_cognition_observation_candidate, 2> candidates{{
        Candidate(first, 0U, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 1U, 0U, 20U),
        Candidate(second, 1U, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 1U, 0U, 30U),
    }};
    std::array<laplace_cognition_response_entry, 1> responses{};
    std::size_t response_count = 0U;
    laplace_cognition_response_receipt receipt{};
    EXPECT_EQ(laplace_cognition_response_field_measure(
                  candidates.data(), candidates.size(), responses.data(), responses.size(),
                  &response_count, &receipt),
              LAPLACE_COGNITION_RESPONSE_RANGE);
    EXPECT_EQ(response_count, 2U);
    EXPECT_EQ(receipt.response_count, 2U);
    EXPECT_EQ(responses[0].candidate_count, 0U);
}

}  // namespace
