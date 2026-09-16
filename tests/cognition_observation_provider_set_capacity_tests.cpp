#include "laplace/cognition_observation_request.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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
        LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);

    // A nested provider set must not turn its child's capacity refusal into an
    // epistemic abstention and publish another provider's candidate as complete.
    const std::array outer_providers{composite, Provider(&physical)};
    ProviderSetOwner outer_owner;
    laplace_cognition_observation_candidate_provider_v1 outer{};
    ASSERT_EQ(laplace_cognition_observation_candidate_provider_set_create(
        outer_providers.data(), outer_providers.size(), &outer_owner.value, &outer),
        LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK);
    ASSERT_EQ(outer.enumerate_candidates(
        outer.state, &binding, source.data(), frontier.data(), costs.data(),
        frontier.size(), output.data(), output.size(), &count, &usage), 0);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
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

using Candidate = laplace_cognition_observation_candidate;
struct BatchBackend {
    std::vector<Candidate> records;
    std::uint8_t provider_seed{};
    std::uint64_t maximum_records{1U};
    std::size_t calls{};
    bool exhausted{};
};

int EnumerateBatch(
    void* opaque, const laplace_observation_query_binding*,
    const laplace_id128*, const laplace_query_search_state*,
    const std::uint64_t*, std::size_t,
    Candidate* candidates, std::size_t capacity, std::size_t* count,
    laplace_cognition_observation_candidate_usage* usage) {
    auto& backend = *static_cast<BatchBackend*>(opaque);
    ++backend.calls;
    *count = 0U;
    *usage = {};
    usage->rows_examined = backend.records.size();
    usage->index_plan_count = 1U;
    usage->crossing_count = 1U;
    usage->io_operations = 2U;
    usage->database_operations = 3U;
    if (backend.exhausted || backend.records.size() > capacity) {
        usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED;
        return 0;
    }
    std::copy(backend.records.begin(), backend.records.end(), candidates);
    *count = backend.records.size();
    return 0;
}

Candidate Witness() {
    Candidate candidate{};
    candidate.target_entity_id = Id(0x41U);
    candidate.observation_fingerprint = Digest(0x30U);
    candidate.multiplicity = 1U;
    candidate.gap = 1U;
    candidate.relation_family = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
    candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE;
    return candidate;
}

struct UnionResult {
    int status{-1};
    std::size_t count{};
    std::vector<Candidate> candidates;
    laplace_cognition_observation_candidate_usage usage{};
};

UnionResult EnumerateUnion(
    const std::vector<BatchBackend*>& backends, const std::size_t capacity) {
    std::vector<laplace_cognition_observation_candidate_provider_v1> providers;
    for (auto* backend : backends) {
        laplace_cognition_observation_candidate_provider_v1 provider{};
        provider.state = backend;
        provider.provider_fingerprint = Digest(backend->provider_seed);
        provider.maximum_candidate_records_per_expansion = backend->maximum_records;
        provider.enumerate_candidates = EnumerateBatch;
        provider.abi_major = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
        provider.abi_minor = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
        providers.push_back(provider);
    }
    ProviderSetOwner owner;
    laplace_cognition_observation_candidate_provider_v1 composite{};
    UnionResult result;
    const auto create_status =
        laplace_cognition_observation_candidate_provider_set_create(
            providers.data(), providers.size(), &owner.value, &composite);
    EXPECT_EQ(create_status, LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK);
    if (create_status != LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK) return result;
    result.candidates.resize(capacity);
    laplace_observation_query_binding binding{};
    binding.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    const laplace_id128 source = Id(0x43U);
    const laplace_query_search_state frontier{};
    const std::uint64_t cost{};
    result.status = composite.enumerate_candidates(
        composite.state, &binding, &source, &frontier, &cost, 1U,
        result.candidates.data(), result.candidates.size(), &result.count,
        &result.usage);
    return result;
}

TEST(CognitionObservationProviderSetCapacity,
     ReusesExactWitnessesWithinAndAcrossProvidersWithoutDiscountingWork) {
    BatchBackend first{{Witness()}, 0x10U};
    BatchBackend second{{Witness()}, 0x20U};
    const auto one = EnumerateUnion({&first, &second}, 1U);
    ASSERT_EQ(one.status, 0);
    ASSERT_EQ(one.count, 1U);
    EXPECT_EQ(one.usage.limiting_disposition, 0U);
    EXPECT_EQ(first.calls, 1U);
    EXPECT_EQ(second.calls, 1U);
    EXPECT_EQ(one.usage.rows_examined, 2U);
    EXPECT_EQ(one.usage.index_plan_count, 2U);
    EXPECT_EQ(one.usage.crossing_count, 2U);
    EXPECT_EQ(one.usage.io_operations, 4U);
    EXPECT_EQ(one.usage.database_operations, 6U);

    // One child repeats the same complete witness in its bounded contribution.
    // Reversing descriptor order and actual provider order must preserve reuse.
    first.records.push_back(Witness());
    first.maximum_records = 2U;
    std::swap(first.provider_seed, second.provider_seed);
    const auto repeated = EnumerateUnion({&second, &first}, 2U);
    ASSERT_EQ(repeated.status, 0);
    ASSERT_EQ(repeated.count, 1U);
    EXPECT_EQ(repeated.usage.limiting_disposition, 0U);
    EXPECT_EQ(first.calls, 2U);
    EXPECT_EQ(second.calls, 2U);
    EXPECT_EQ(repeated.usage.rows_examined, 3U);
    EXPECT_EQ(repeated.usage.index_plan_count, 2U);
    EXPECT_EQ(repeated.usage.crossing_count, 2U);
    EXPECT_EQ(repeated.usage.io_operations, 4U);
    EXPECT_EQ(repeated.usage.database_operations, 6U);
    EXPECT_EQ(std::memcmp(one.candidates[0].observation_fingerprint.bytes,
                          repeated.candidates[0].observation_fingerprint.bytes, 32U), 0);
}

TEST(CognitionObservationProviderSetCapacity,
     DistinctPhysicalitiesRemainDistinctAndUniqueOverflowCannotBeRepairedByCopies) {
    BatchBackend first{{Witness()}, 0x10U};
    BatchBackend second{{Witness()}, 0x20U};
    second.records[0].observation_fingerprint = Digest(0x31U);
    BatchBackend last{{Witness()}, 0x30U};
    const auto overflow = EnumerateUnion({&last, &second, &first}, 1U);
    ASSERT_EQ(overflow.status, 0);
    EXPECT_EQ(overflow.count, 0U);
    EXPECT_EQ(overflow.usage.limiting_disposition,
              LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
    EXPECT_EQ(first.calls, 1U);
    EXPECT_EQ(second.calls, 1U);
    EXPECT_EQ(last.calls, 1U);
    EXPECT_EQ(overflow.usage.rows_examined, 3U);
    EXPECT_EQ(overflow.usage.io_operations, 6U);
    EXPECT_EQ(overflow.usage.database_operations, 9U);

    const auto complete = EnumerateUnion({&first, &second, &last}, 2U);
    ASSERT_EQ(complete.status, 0);
    ASSERT_EQ(complete.count, 2U);
    EXPECT_EQ(complete.usage.limiting_disposition, 0U);
    EXPECT_EQ(std::memcmp(complete.candidates[0].target_entity_id.bytes,
                          complete.candidates[1].target_entity_id.bytes, 16U), 0);
    EXPECT_NE(std::memcmp(complete.candidates[0].observation_fingerprint.bytes,
                          complete.candidates[1].observation_fingerprint.bytes, 32U), 0);
    std::swap(first.provider_seed, second.provider_seed);
    const auto reordered = EnumerateUnion({&last, &second, &first}, 2U);
    ASSERT_EQ(reordered.status, 0);
    ASSERT_EQ(reordered.count, 2U);
    for (std::size_t index = 0U; index < complete.count; ++index) {
        EXPECT_EQ(std::memcmp(complete.candidates[index].observation_fingerprint.bytes,
                              reordered.candidates[index].observation_fingerprint.bytes, 32U), 0);
    }
}

TEST(CognitionObservationProviderSetCapacity,
     DuplicateReuseNeverHidesRawProviderBoundsOrChildExhaustion) {
    BatchBackend first{{Witness()}, 0x10U};
    BatchBackend second{{Witness(), Witness()}, 0x20U};
    // The provider's raw count/row declaration is invalid even though its exact
    // duplicate output would fit after reuse.
    const auto invalid = EnumerateUnion({&first, &second}, 2U);
    EXPECT_NE(invalid.status, 0);
    second.maximum_records = 2U;
    second.exhausted = true;
    const auto exhausted = EnumerateUnion({&first, &second}, 2U);
    ASSERT_EQ(exhausted.status, 0);
    EXPECT_EQ(exhausted.count, 0U);
    EXPECT_EQ(exhausted.usage.limiting_disposition,
              LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
    EXPECT_EQ(exhausted.usage.rows_examined, 3U);
    EXPECT_EQ(exhausted.usage.index_plan_count, 2U);
    EXPECT_EQ(exhausted.usage.crossing_count, 2U);
    EXPECT_EQ(exhausted.usage.io_operations, 4U);
    EXPECT_EQ(exhausted.usage.database_operations, 6U);
}

TEST(CognitionObservationProviderSetCapacity,
     ReuseRequiresEveryTypedCandidateFieldIncludingStandingFloatBits) {
    struct Change {
        const char* field;
        void (*apply)(Candidate&);
    };
    const Change changes[] = {
        {"target_entity_id", [](Candidate& c) { c.target_entity_id.bytes[0] ^= 1U; }},
        {"relation_id", [](Candidate& c) { c.relation_id.bytes[0] ^= 1U; }},
        {"observation_fingerprint", [](Candidate& c) { c.observation_fingerprint.bytes[0] ^= 1U; }},
        {"evidence_root_fingerprint", [](Candidate& c) { c.evidence_root_fingerprint.bytes[0] ^= 1U; }},
        {"source_state_index", [](Candidate& c) { ++c.source_state_index; }},
        {"source_logical_ordinal", [](Candidate& c) { ++c.source_logical_ordinal; }},
        {"target_logical_ordinal", [](Candidate& c) { ++c.target_logical_ordinal; }},
        {"multiplicity", [](Candidate& c) { ++c.multiplicity; }},
        {"gap", [](Candidate& c) { ++c.gap; }},
        {"evidence_uncertainty_numerator", [](Candidate& c) { ++c.evidence_uncertainty_numerator; }},
        {"evidence_uncertainty_denominator", [](Candidate& c) { ++c.evidence_uncertainty_denominator; }},
        {"relation_family", [](Candidate& c) { ++c.relation_family; }},
        {"direction", [](Candidate& c) { ++c.direction; }},
        {"flags", [](Candidate& c) { ++c.flags; }},
        {"reserved", [](Candidate& c) { ++c.reserved; }},
        {"source_layer", [](Candidate& c) { c.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY; }},
        {"standing.state_id", [](Candidate& c) { c.standing.state_id.bytes[0] ^= 1U; }},
        {"standing.coordinate_id", [](Candidate& c) { c.standing.coordinate_id.bytes[0] ^= 1U; }},
        {"standing.arena_scope_id", [](Candidate& c) { c.standing.arena_scope_id.bytes[0] ^= 1U; }},
        {"standing.prior_state_id", [](Candidate& c) { c.standing.prior_state_id.bytes[0] ^= 1U; }},
        {"standing.epoch_id", [](Candidate& c) { c.standing.epoch_id.bytes[0] ^= 1U; }},
        {"standing.rating_recipe_id", [](Candidate& c) { c.standing.rating_recipe_id.bytes[0] ^= 1U; }},
        {"standing.rating", [](Candidate& c) { c.standing.rating = -0.0; }},
        {"standing.rating_deviation", [](Candidate& c) { c.standing.rating_deviation = -0.0; }},
        {"standing.volatility", [](Candidate& c) { c.standing.volatility = -0.0; }},
        {"standing.eligible_match_count", [](Candidate& c) { ++c.standing.eligible_match_count; }},
        {"standing.period_ordinal", [](Candidate& c) { ++c.standing.period_ordinal; }},
        {"standing.rating_recipe_version", [](Candidate& c) { ++c.standing.rating_recipe_version; }},
        {"standing.flags", [](Candidate& c) { ++c.standing.flags; }},
    };
    for (const auto& change : changes) {
        SCOPED_TRACE(change.field);
        BatchBackend first{{Witness()}, 0x10U};
        BatchBackend second{{Witness()}, 0x20U};
        change.apply(second.records[0]);
        // This is aggregation only. Malformed payloads must remain visible to
        // the owning candidate validator; reuse cannot launder them into the
        // valid candidate merely because their observation fingerprint agrees.
        const auto result = EnumerateUnion({&first, &second}, 2U);
        ASSERT_EQ(result.status, 0);
        EXPECT_EQ(result.count, 2U);
        EXPECT_EQ(result.usage.limiting_disposition, 0U);
        EXPECT_EQ(result.usage.rows_examined, 2U);
    }
}

}  // namespace
