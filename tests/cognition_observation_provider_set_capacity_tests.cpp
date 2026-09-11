#include "laplace/cognition_observation_request.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 Id(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

struct Backend {
    laplace_id128 target{};
    std::uint32_t source_layer{};
    std::uint8_t provider_seed{};
    std::uint8_t observation_seed{};
    std::size_t calls{};
};

int Enumerate(
    void* const opaque,
    const laplace_observation_query_binding* const binding,
    const laplace_id128* const source_entity_ids,
    const laplace_query_search_state* const frontier_states,
    const std::uint64_t* const accumulated_costs,
    const std::size_t frontier_state_count,
    laplace_cognition_observation_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_observation_candidate_usage* const usage) {
    if (opaque == nullptr || binding == nullptr || source_entity_ids == nullptr ||
        frontier_states == nullptr || accumulated_costs == nullptr ||
        frontier_state_count == 0U || candidates == nullptr ||
        candidate_capacity == 0U || candidate_count == nullptr || usage == nullptr) {
        return 1;
    }
    auto& backend = *static_cast<Backend*>(opaque);
    ++backend.calls;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = 1U;
    usage->index_plan_count = 1U;
    usage->crossing_count = 1U;
    (void)binding;
    (void)source_entity_ids;
    (void)frontier_states;
    (void)accumulated_costs;

    auto& candidate = candidates[0];
    candidate = laplace_cognition_observation_candidate{};
    candidate.target_entity_id = backend.target;
    candidate.observation_fingerprint = Digest(backend.observation_seed);
    candidate.source_state_index = 0U;
    candidate.multiplicity = 1U;
    candidate.gap = 1U;
    candidate.relation_family = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    candidate.source_layer = backend.source_layer;
    candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE;
    *candidate_count = 1U;
    return 0;
}

laplace_cognition_observation_candidate_provider_v1 Provider(Backend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(backend->provider_seed);
    provider.maximum_candidate_records_per_expansion = 1U;
    provider.enumerate_candidates = Enumerate;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}

struct ProviderSetOwner {
    ~ProviderSetOwner() {
        laplace_cognition_observation_candidate_provider_set_destroy(&value);
    }
    laplace_cognition_observation_candidate_provider_set* value{};
};

TEST(CognitionObservationProviderSetCapacity,
     QueriesEveryPlaneAndPublishesNoPrefixWhenAggregateCapacityIsInsufficient) {
    Backend physical{};
    physical.target = Id(0x41U);
    physical.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
    physical.provider_seed = 0x10U;
    physical.observation_seed = 0x30U;
    Backend testimony{};
    testimony.target = Id(0x42U);
    testimony.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
    testimony.provider_seed = 0x20U;
    testimony.observation_seed = 0x40U;

    const std::array providers{Provider(&physical), Provider(&testimony)};
    ProviderSetOwner owner;
    laplace_cognition_observation_candidate_provider_v1 composite{};
    ASSERT_EQ(
        laplace_cognition_observation_candidate_provider_set_create(
            providers.data(), providers.size(), &owner.value, &composite),
        LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK);

    laplace_observation_query_binding binding{};
    binding.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    std::array<laplace_id128, 1> source{{Id(0x43U)}};
    std::array<laplace_query_search_state, 1> frontier{};
    std::array<std::uint64_t, 1> costs{};
    std::array<laplace_cognition_observation_candidate, 1> output{};
    std::size_t count = 0U;
    laplace_cognition_observation_candidate_usage usage{};

    ASSERT_EQ(
        composite.enumerate_candidates(
            composite.state, &binding, source.data(), frontier.data(), costs.data(),
            frontier.size(), output.data(), output.size(), &count, &usage),
        0);
    EXPECT_EQ(physical.calls, 1U);
    EXPECT_EQ(testimony.calls, 1U);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(usage.rows_examined, 2U);
    EXPECT_EQ(usage.index_plan_count, 2U);
    EXPECT_EQ(usage.crossing_count, 2U);
    EXPECT_EQ(
        usage.limiting_disposition,
        LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN);
}

TEST(CognitionObservationProviderSetCapacity,
     PublishesAllTypedPlanesWhenTheCompleteAggregateFits) {
    Backend physical{};
    physical.target = Id(0x41U);
    physical.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
    physical.provider_seed = 0x10U;
    physical.observation_seed = 0x30U;
    Backend testimony{};
    testimony.target = Id(0x42U);
    testimony.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
    testimony.provider_seed = 0x20U;
    testimony.observation_seed = 0x40U;

    const std::array providers{Provider(&testimony), Provider(&physical)};
    ProviderSetOwner owner;
    laplace_cognition_observation_candidate_provider_v1 composite{};
    ASSERT_EQ(
        laplace_cognition_observation_candidate_provider_set_create(
            providers.data(), providers.size(), &owner.value, &composite),
        LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK);

    laplace_observation_query_binding binding{};
    binding.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    std::array<laplace_id128, 1> source{{Id(0x43U)}};
    std::array<laplace_query_search_state, 1> frontier{};
    std::array<std::uint64_t, 1> costs{};
    std::array<laplace_cognition_observation_candidate, 2> output{};
    std::size_t count = 0U;
    laplace_cognition_observation_candidate_usage usage{};

    ASSERT_EQ(
        composite.enumerate_candidates(
            composite.state, &binding, source.data(), frontier.data(), costs.data(),
            frontier.size(), output.data(), output.size(), &count, &usage),
        0);
    ASSERT_EQ(count, 2U);
    EXPECT_EQ(usage.limiting_disposition, 0U);
    EXPECT_EQ(physical.calls, 1U);
    EXPECT_EQ(testimony.calls, 1U);
    const std::array layers{output[0].source_layer, output[1].source_layer};
    EXPECT_NE(
        std::find(layers.begin(), layers.end(),
                  LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY),
        layers.end());
    EXPECT_NE(
        std::find(layers.begin(), layers.end(),
                  LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY),
        layers.end());
}

}  // namespace
