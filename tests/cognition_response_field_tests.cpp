#include "laplace/cognition_response_field.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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
    if (relation == LAPLACE_OBSERVATION_QUERY_SEMANTIC) {
        candidate.relation_id = Id(static_cast<std::uint8_t>(witness_seed + 80U));
        candidate.flags |= LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;
    }
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
        if (SameId(responses[i].entity_id, shared)) shared_response = &responses[i];
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

TEST(CognitionResponseField, CanonicalizesCandidateOrderBeforeFingerprinting) {
    const auto target = Id(13U);
    std::array<laplace_cognition_observation_candidate, 3> first{{
        Candidate(target, 2U, LAPLACE_OBSERVATION_QUERY_SEMANTIC,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY, 1U, 0U, 20U, 70U),
        Candidate(target, 0U, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 1U, 2U, 30U),
        Candidate(target, 1U, LAPLACE_OBSERVATION_QUERY_SEMANTIC,
                  LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION, 1U, 0U, 40U),
    }};
    auto second = first;
    std::reverse(second.begin(), second.end());
    std::array<laplace_cognition_response_entry, 2> left{}, right{};
    std::size_t left_count = 0U, right_count = 0U;
    laplace_cognition_response_receipt left_receipt{}, right_receipt{};
    ASSERT_EQ(laplace_cognition_response_field_measure(
                  first.data(), first.size(), left.data(), left.size(), &left_count,
                  &left_receipt), LAPLACE_COGNITION_RESPONSE_OK);
    ASSERT_EQ(laplace_cognition_response_field_measure(
                  second.data(), second.size(), right.data(), right.size(), &right_count,
                  &right_receipt), LAPLACE_COGNITION_RESPONSE_OK);
    ASSERT_EQ(left_count, 1U);
    ASSERT_EQ(right_count, 1U);
    EXPECT_TRUE(SameDigest(left_receipt.input_fingerprint, right_receipt.input_fingerprint));
    EXPECT_TRUE(SameDigest(left_receipt.output_fingerprint, right_receipt.output_fingerprint));
    EXPECT_TRUE(SameDigest(left[0].response_fingerprint, right[0].response_fingerprint));
}

TEST(CognitionResponseField, AllZeroCanonicalEntityRemainsAddressable) {
    const laplace_id128 legal_zero{};
    const auto candidate = Candidate(
        legal_zero, 0U, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
        LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 1U, 0U, 71U);
    std::array<laplace_cognition_response_entry, 1> responses{};
    std::size_t response_count = 0U;
    laplace_cognition_response_receipt receipt{};
    ASSERT_EQ(laplace_cognition_response_field_measure(
                  &candidate, 1U, responses.data(), responses.size(), &response_count,
                  &receipt), LAPLACE_COGNITION_RESPONSE_OK);
    ASSERT_EQ(response_count, 1U);
    EXPECT_TRUE(SameId(responses[0].entity_id, legal_zero));
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

struct Edge final {
    laplace_id128 source{};
    laplace_id128 target{};
    std::uint32_t relation{};
    std::uint32_t layer{};
    std::uint8_t witness{};
    std::uint8_t evidence{};
};

struct ScanFixture final {
    std::array<Edge, 4> edges{};
    std::size_t calls{};
    std::size_t widest_batch{};
};

int EnumerateScan(
    void* const opaque,
    const laplace_observation_query_binding* const binding,
    const laplace_id128* const source_entity_ids,
    const laplace_query_search_state*,
    const std::uint64_t*,
    const std::size_t frontier_state_count,
    laplace_cognition_observation_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_observation_candidate_usage* const usage) {
    if (opaque == nullptr || binding == nullptr || source_entity_ids == nullptr ||
        frontier_state_count == 0U || candidates == nullptr || candidate_count == nullptr ||
        usage == nullptr) return 1;
    auto& fixture = *static_cast<ScanFixture*>(opaque);
    ++fixture.calls;
    fixture.widest_batch = std::max(fixture.widest_batch, frontier_state_count);
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->index_plan_count = 1U;
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    for (std::size_t source_index = 0U; source_index < frontier_state_count; ++source_index) {
        for (const auto& edge : fixture.edges) {
            if (!SameId(edge.source, source_entity_ids[source_index]) ||
                (binding->relation_mask & edge.relation) == 0U) continue;
            if (*candidate_count >= candidate_capacity) return 2;
            auto candidate = Candidate(
                edge.target, static_cast<std::uint64_t>(source_index), edge.relation,
                edge.layer, 1U, edge.relation == LAPLACE_OBSERVATION_QUERY_SEMANTIC ? 0U : 1U,
                edge.witness, edge.evidence);
            candidates[*candidate_count] = candidate;
            ++*candidate_count;
        }
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

TEST(CognitionResponseField, ScanPropagatesOneTugAcrossSetWiseTypedFrontiers) {
    const auto root = Id(1U);
    const auto left = Id(21U);
    const auto right = Id(41U);
    const auto shared = Id(61U);
    ScanFixture fixture{{
        Edge{root, left, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
             LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 80U, 0U},
        Edge{root, right, LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
             LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY, 81U, 0U},
        Edge{left, shared, LAPLACE_OBSERVATION_QUERY_SEMANTIC,
             LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY, 82U, 100U},
        Edge{right, shared, LAPLACE_OBSERVATION_QUERY_SEMANTIC,
             LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION, 83U, 0U},
    }, 0U, 0U};
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = &fixture;
    provider.provider_fingerprint = Digest(120U);
    provider.maximum_candidate_records_per_expansion = 2U;
    provider.enumerate_candidates = EnumerateScan;
    provider.abi_major = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;

    laplace_cognition_response_scan_program program{};
    program.program_id = Digest(130U);
    program.context_fingerprint = Digest(131U);
    program.evidence_boundary = Digest(132U);
    program.evidence_epoch = Digest(133U);
    program.max_frontier_states = 8U;
    program.max_candidate_records = 16U;
    program.max_provider_calls = 4U;
    program.max_depth = 2U;
    program.relation_mask = LAPLACE_OBSERVATION_QUERY_RELATION_MASK;
    program.version = LAPLACE_COGNITION_RESPONSE_SCAN_VERSION;

    std::array<laplace_cognition_observation_candidate, 16> candidates{};
    std::array<laplace_cognition_response_entry, 16> responses{};
    std::size_t candidate_count = 0U, response_count = 0U;
    laplace_cognition_response_scan_receipt receipt{};
    ASSERT_EQ(laplace_cognition_response_field_scan(
                  &program, &root, 1U, &provider,
                  candidates.data(), candidates.size(), &candidate_count,
                  responses.data(), responses.size(), &response_count, &receipt),
              LAPLACE_COGNITION_RESPONSE_OK);
    EXPECT_EQ(candidate_count, 4U);
    EXPECT_EQ(response_count, 3U);
    EXPECT_EQ(fixture.calls, 2U);
    EXPECT_EQ(fixture.widest_batch, 2U);
    EXPECT_EQ(receipt.provider_call_count, 2U);
    EXPECT_EQ(receipt.expanded_state_count, 3U);
    EXPECT_EQ(receipt.relation_family_mask,
              LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
                  LAPLACE_OBSERVATION_QUERY_SEMANTIC);
    EXPECT_EQ(receipt.source_layer_mask,
              LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY |
                  LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY |
                  LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION);

    const laplace_cognition_response_entry* shared_response = nullptr;
    for (std::size_t index = 0U; index < response_count; ++index) {
        if (SameId(responses[index].entity_id, shared)) {
            shared_response = &responses[index];
            break;
        }
    }
    ASSERT_NE(shared_response, nullptr);
    EXPECT_EQ(shared_response->source_state_count, 2U);
    EXPECT_EQ(shared_response->candidate_count, 2U);
    EXPECT_EQ(shared_response->source_channel_mass[1], 1U);
    EXPECT_EQ(shared_response->source_channel_mass[2], 1U);
}

}  // namespace
