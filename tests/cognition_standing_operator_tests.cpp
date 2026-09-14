#include "laplace/cognition_observation_request.h"
#include "laplace/cognition_operator.h"
#include "laplace/identity.h"
#include "laplace/standing_calculation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

laplace_digest256 StandingOperatorDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

bool StandingOperatorSameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool StandingOperatorDigestZero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

laplace_id128 StandingOperatorCodepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

laplace_standing_state StandingOperatorState() {
    laplace_standing_state state{};
    state.coordinate_id = StandingOperatorDigest(30U);
    state.arena_scope_id = StandingOperatorDigest(31U);
    state.prior_state_id = StandingOperatorDigest(32U);
    state.epoch_id = StandingOperatorDigest(33U);
    state.rating_recipe_id = StandingOperatorDigest(34U);
    state.rating = 1542.0;
    state.rating_deviation = 81.0;
    state.volatility = 0.061;
    state.eligible_match_count = 12U;
    state.period_ordinal = 4U;
    state.rating_recipe_version = 1U;
    state.flags = 0U;
    EXPECT_EQ(
        laplace_standing_state_identify(&state, &state.state_id),
        LAPLACE_STANDING_OK);
    return state;
}

laplace_cognition_observation_request StandingOperatorRequest(
    const laplace_id128& anchor) {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = anchor;
    request.world_id = StandingOperatorDigest(60U);
    request.time_fingerprint = StandingOperatorDigest(61U);
    request.context_fingerprint = StandingOperatorDigest(62U);
    request.evidence_boundary = StandingOperatorDigest(63U);
    request.evidence_epoch = StandingOperatorDigest(64U);
    request.authority_id = StandingOperatorDigest(65U);
    request.result_contract_fingerprint = StandingOperatorDigest(66U);
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    request.maximum_results = 1U;
    request.flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    request.search_budget.max_expanded_states = 8U;
    request.search_budget.max_transition_records = 16U;
    request.search_budget.max_emitted_states = 24U;
    request.search_budget.max_frontier_states = 16U;
    request.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.search_budget.max_io_operations = 16U;
    request.search_budget.max_database_operations = 16U;
    request.search_budget.max_provider_calls = 8U;
    request.search_budget.max_depth = 1U;
    request.search_budget.requested_path_count = 1U;
    request.search_budget.frontier_batch_width = 4U;
    request.search_budget.transition_batch_capacity = 16U;

    request.forward_limits.max_layers = 2U;
    request.forward_limits.max_provider_calls = 4U;
    request.forward_limits.max_projected_queries = 4U;
    request.forward_limits.max_candidate_operations = 4U;
    request.forward_limits.max_resolutions = 2U;
    request.forward_limits.max_resource_cost = 256U;
    request.forward_limits.max_io_operations = 16U;
    request.forward_limits.max_database_operations = 16U;
    request.forward_limits.candidate_operation_capacity = 4U;
    request.forward_limits.resolution_capacity = 2U;
    return request;
}

struct StandingOperatorBackend final {
    laplace_id128 source{};
    laplace_id128 target{};
    laplace_id128 relation{};
    laplace_standing_state standing{};
};

int StandingOperatorEnumerate(
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
    auto& backend = *static_cast<StandingOperatorBackend*>(opaque);
    if (binding->relation_mask != LAPLACE_OBSERVATION_QUERY_SEMANTIC) return 2;
    (void)accumulated_costs;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;
    for (std::size_t index = 0U; index < frontier_state_count; ++index) {
        if (std::memcmp(
                source_entity_ids[index].bytes,
                backend.source.bytes,
                sizeof(backend.source.bytes)) != 0) {
            continue;
        }
        if (*candidate_count >= candidate_capacity) return 3;
        auto& candidate = candidates[*candidate_count];
        candidate = laplace_cognition_observation_candidate{};
        candidate.target_entity_id = backend.target;
        candidate.relation_id = backend.relation;
        candidate.observation_fingerprint = StandingOperatorDigest(120U);
        candidate.source_state_index = static_cast<std::uint64_t>(index);
        candidate.source_logical_ordinal = frontier_states[index].depth;
        candidate.target_logical_ordinal =
            static_cast<std::uint64_t>(frontier_states[index].depth) + 1U;
        candidate.multiplicity = 1U;
        candidate.gap = 1U;
        candidate.relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
        candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING;
        candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
        candidate.flags =
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT |
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT;
        candidate.standing = backend.standing;
        ++*candidate_count;
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

struct StandingObservationHandle {
    laplace_cognition_observation_result* value{};
    ~StandingObservationHandle() {
        laplace_cognition_observation_result_destroy(&value);
    }
};

struct StandingForwardHandle {
    laplace_cognition_forward_result* value{};
    ~StandingForwardHandle() {
        laplace_cognition_forward_result_destroy(&value);
    }
};

struct StandingOperatorHandle {
    laplace_cognition_operator* value{};
    ~StandingOperatorHandle() {
        laplace_cognition_operator_destroy(&value);
    }
};

TEST(CognitionStandingOperator, PreservesExactStandingAsItsOwnTypedPlane) {
    StandingOperatorBackend backend{};
    backend.source = StandingOperatorCodepoint(0x42U);
    backend.target = StandingOperatorCodepoint(0x41U);
    backend.relation = StandingOperatorCodepoint(0x3DU);
    backend.standing = StandingOperatorState();

    const auto request = StandingOperatorRequest(backend.source);
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = &backend;
    provider.provider_fingerprint = StandingOperatorDigest(220U);
    provider.maximum_candidate_records_per_expansion = 8U;
    provider.enumerate_candidates = StandingOperatorEnumerate;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;

    StandingObservationHandle observation;
    StandingForwardHandle forward;
    laplace_cognition_forward_receipt forward_receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &observation.value, &forward.value, &forward_receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(observation.value, nullptr);
    ASSERT_EQ(laplace_cognition_observation_result_answer_count(observation.value), 1U);

    laplace_cognition_observation_operator_view view{};
    ASSERT_EQ(
        laplace_cognition_observation_result_operator_view(
            observation.value, 0U, &view),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(view.constraints, nullptr);
    ASSERT_EQ(view.constraint_count, 1U);
    const auto& constraint = view.constraints[0];
    EXPECT_EQ(constraint.relation_family, LAPLACE_OBSERVATION_QUERY_SEMANTIC);
    EXPECT_EQ(constraint.source_class, LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING);
    EXPECT_EQ(
        constraint.direction,
        LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET);
    EXPECT_TRUE(StandingOperatorDigestZero(constraint.evidence_root_id));
    EXPECT_DOUBLE_EQ(constraint.precision, 1.0);
    EXPECT_TRUE(StandingOperatorSameDigest(
        constraint.standing.state_id, backend.standing.state_id));
    EXPECT_TRUE(StandingOperatorSameDigest(
        constraint.standing.coordinate_id, backend.standing.coordinate_id));
    EXPECT_TRUE(StandingOperatorSameDigest(
        constraint.standing.arena_scope_id, backend.standing.arena_scope_id));
    EXPECT_TRUE(StandingOperatorSameDigest(
        constraint.standing.prior_state_id, backend.standing.prior_state_id));
    EXPECT_TRUE(StandingOperatorSameDigest(
        constraint.standing.epoch_id, backend.standing.epoch_id));
    EXPECT_TRUE(StandingOperatorSameDigest(
        constraint.standing.rating_recipe_id, backend.standing.rating_recipe_id));
    EXPECT_DOUBLE_EQ(constraint.standing.rating, backend.standing.rating);
    EXPECT_DOUBLE_EQ(
        constraint.standing.rating_deviation,
        backend.standing.rating_deviation);
    EXPECT_DOUBLE_EQ(constraint.standing.volatility, backend.standing.volatility);
    EXPECT_EQ(
        constraint.standing.eligible_match_count,
        backend.standing.eligible_match_count);
    EXPECT_EQ(constraint.standing.period_ordinal, backend.standing.period_ordinal);
    EXPECT_EQ(
        constraint.standing.rating_recipe_version,
        backend.standing.rating_recipe_version);

    StandingOperatorHandle rebuilt;
    laplace_cognition_operator_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_operator_create(
            &view.program,
            view.fields,
            view.field_count,
            view.constraints,
            view.constraint_count,
            &rebuilt.value,
            &receipt),
        LAPLACE_COGNITION_OPERATOR_OK);
    EXPECT_EQ(receipt.selected_constraint_count, 1U);
    EXPECT_EQ(receipt.testimony_constraint_count, 0U);
    EXPECT_EQ(receipt.derived_constraint_count, 0U);
    EXPECT_EQ(receipt.physicality_constraint_count, 0U);
}

}  // namespace
