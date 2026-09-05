#include "laplace/cognition_observation_request.h"

#include "laplace/identity.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"

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
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool Zero(const laplace_digest256& value) {
    return std::all_of(
        std::begin(value.bytes), std::end(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

std::uint64_t Metadata(const std::uint8_t tier, const std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
        (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) |
        (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

struct Fixture final {
    laplace_id128 a{};
    laplace_id128 b{};
    laplace_id128 root{};
    std::array<laplace_trajectory_carrier, 2> carriers{};
    laplace_persistence_physicality_record physicality{};
    std::array<laplace_persistence_trajectory_segment_record, 2> segments{};
    laplace_digest256 boundary{};
    laplace_digest256 evidence_epoch{};
};

Fixture BuildFixture() {
    Fixture fixture{};
    fixture.a = Codepoint(0x41U);
    fixture.b = Codepoint(0x42U);
    fixture.root = Codepoint(0x52U);
    fixture.boundary = Digest(180U);
    fixture.evidence_epoch = Digest(200U);

    EXPECT_EQ(
        laplace_trajectory_composition_encode(
            &fixture.a, 1U, 1U, Metadata(2U, 0x41U), &fixture.carriers[0]),
        LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(
        laplace_trajectory_composition_encode(
            &fixture.b, 2U, 1U, Metadata(2U, 0x42U), &fixture.carriers[1]),
        LAPLACE_TRAJECTORY_OK);

    fixture.physicality.entity_id = fixture.root;
    fixture.physicality.physicality_type =
        LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    fixture.physicality.vertex_class =
        LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    fixture.physicality.recipe_version = 1U;
    fixture.physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    fixture.physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    fixture.physicality.recipe_fingerprint = Digest(20U);
    fixture.physicality.geometry_epoch = Digest(40U);
    fixture.physicality.centroid.component[0] = 0.125;
    fixture.physicality.centroid.component[1] = -0.25;
    fixture.physicality.centroid.component[2] = 0.5;
    fixture.physicality.centroid.component[3] = -0.75;
    fixture.physicality.radius = 0.875;
    fixture.physicality.logical_count = fixture.carriers.size();
    fixture.physicality.vertex_count = fixture.carriers.size();
    EXPECT_EQ(
        laplace_persistence_trajectory_fingerprint(
            fixture.carriers.data(), fixture.carriers.size(),
            &fixture.physicality.trajectory_fingerprint),
        LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(
        laplace_persistence_physicality_identify(
            &fixture.physicality, &fixture.physicality.physicality_id),
        LAPLACE_PERSISTENCE_OK);

    for (std::size_t index = 0U; index < fixture.segments.size(); ++index) {
        fixture.segments[index].physicality_id = fixture.physicality.physicality_id;
        fixture.segments[index].vertex_index = static_cast<std::uint64_t>(index);
        fixture.segments[index].carrier = fixture.carriers[index];
    }
    return fixture;
}

laplace_cognition_observation_request Request(const Fixture& fixture) {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = fixture.b;
    request.world_id = Digest(60U);
    request.time_fingerprint = Digest(61U);
    request.context_fingerprint = Digest(62U);
    request.evidence_boundary = fixture.boundary;
    request.evidence_epoch = fixture.evidence_epoch;
    request.authority_id = Digest(63U);
    request.result_contract_fingerprint = Digest(64U);
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    request.maximum_results = 1U;
    request.flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    request.search_budget.max_expanded_states = 1U;
    request.search_budget.max_transition_records = 8U;
    request.search_budget.max_emitted_states = 9U;
    request.search_budget.max_frontier_states = 9U;
    request.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.search_budget.max_io_operations = 8U;
    request.search_budget.max_database_operations = 8U;
    request.search_budget.max_provider_calls = 2U;
    request.search_budget.max_depth = 1U;
    request.search_budget.requested_path_count = request.maximum_results;
    request.search_budget.frontier_batch_width = 2U;
    request.search_budget.transition_batch_capacity = 8U;

    request.forward_limits.max_layers = 2U;
    request.forward_limits.max_provider_calls = 4U;
    request.forward_limits.max_projected_queries = 4U;
    request.forward_limits.max_candidate_operations = 4U;
    request.forward_limits.max_resolutions = 2U;
    request.forward_limits.max_resource_cost = 128U;
    request.forward_limits.max_io_operations = 8U;
    request.forward_limits.max_database_operations = 8U;
    request.forward_limits.candidate_operation_capacity = 4U;
    request.forward_limits.resolution_capacity = 2U;
    return request;
}

class CompiledHandle final {
public:
    ~CompiledHandle() {
        laplace_cognition_observation_compiled_request_destroy(&value);
    }
    laplace_cognition_observation_compiled_request value{};
};

class IndexHandle final {
public:
    ~IndexHandle() { laplace_observation_query_index_destroy(&value); }
    laplace_observation_query_index* value{};
};

class ForwardResultHandle final {
public:
    ~ForwardResultHandle() {
        laplace_cognition_forward_result_destroy(&value);
    }
    laplace_cognition_forward_result* value{};
};

TEST(CognitionObservationRequest, CompilesAndExecutesWithoutHandBuiltGuidanceOrSearchState) {
    const auto fixture = BuildFixture();
    const auto request = Request(fixture);

    CompiledHandle compiled;
    ASSERT_EQ(
        laplace_cognition_observation_request_compile(&request, &compiled.value),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(compiled.value.guidance_state, nullptr);
    EXPECT_FALSE(Zero(compiled.value.request_fingerprint));
    EXPECT_FALSE(Zero(compiled.value.binding.binding_fingerprint));
    EXPECT_FALSE(Zero(compiled.value.initial_search_state.state_id));
    EXPECT_FALSE(Zero(compiled.value.search_program.program_id));
    EXPECT_FALSE(Zero(compiled.value.forward_program.program_id));
    EXPECT_EQ(
        compiled.value.binding.relation_mask,
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR);

    laplace_observation_query_index_input index_input{};
    index_input.physicalities = &fixture.physicality;
    index_input.physicality_count = 1U;
    index_input.trajectory_segments = fixture.segments.data();
    index_input.trajectory_segment_count = fixture.segments.size();
    index_input.bindings = &compiled.value.binding;
    index_input.binding_count = 1U;
    index_input.boundary_id = fixture.boundary;
    index_input.evidence_epoch = fixture.evidence_epoch;
    index_input.maximum_candidate_records_per_expansion = 16U;
    IndexHandle index;
    ASSERT_EQ(
        laplace_observation_query_index_create(&index_input, &index.value),
        LAPLACE_OBSERVATION_QUERY_OK);

    laplace_cognition_forward_provider_v1 cognition_provider{};
    ASSERT_EQ(
        laplace_observation_query_cognition_provider(
            index.value, &cognition_provider),
        LAPLACE_OBSERVATION_QUERY_OK);
    ForwardResultHandle forward_result;
    laplace_cognition_forward_receipt forward_receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &compiled.value.forward_program,
            compiled.value.guidance_state,
            &cognition_provider,
            &forward_result.value,
            &forward_receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(forward_result.value, nullptr);
    EXPECT_EQ(forward_receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(forward_receipt.final_remaining_required_count, 0U);
    EXPECT_EQ(forward_receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);

    laplace_query_search_provider_v1 search_provider{};
    ASSERT_EQ(
        laplace_observation_query_search_provider(index.value, &search_provider),
        LAPLACE_OBSERVATION_QUERY_OK);
    laplace_query_search_result* search_result = nullptr;
    laplace_query_search_receipt search_receipt{};
    ASSERT_EQ(
        laplace_query_search_execute(
            &compiled.value.search_program,
            &compiled.value.initial_search_state,
            1U,
            &search_provider,
            &search_result,
            &search_receipt),
        LAPLACE_QUERY_SEARCH_OK);
    ASSERT_NE(search_result, nullptr);
    ASSERT_EQ(laplace_query_search_result_path_count(search_result), 1U);
    std::array<laplace_query_search_transition, 2> steps{};
    std::size_t step_count = 0U;
    ASSERT_EQ(
        laplace_query_search_result_path_steps(
            search_result, 0U, steps.data(), steps.size(), &step_count),
        LAPLACE_QUERY_SEARCH_OK);
    ASSERT_EQ(step_count, 1U);
    laplace_digest256 a_coordinate{};
    ASSERT_EQ(
        laplace_observation_query_entity_coordinate(&fixture.a, &a_coordinate),
        LAPLACE_OBSERVATION_QUERY_OK);
    EXPECT_TRUE(Same(steps[0].target.anchor_id, a_coordinate));
    laplace_query_search_result_destroy(&search_result);
}

TEST(CognitionObservationRequest, PublicExecuteOwnsCompileProviderAndForwardLifecycle) {
    const auto fixture = BuildFixture();
    const auto request = Request(fixture);

    CompiledHandle binding_source;
    ASSERT_EQ(
        laplace_cognition_observation_request_compile(
            &request, &binding_source.value),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);

    laplace_observation_query_index_input index_input{};
    index_input.physicalities = &fixture.physicality;
    index_input.physicality_count = 1U;
    index_input.trajectory_segments = fixture.segments.data();
    index_input.trajectory_segment_count = fixture.segments.size();
    index_input.bindings = &binding_source.value.binding;
    index_input.binding_count = 1U;
    index_input.boundary_id = fixture.boundary;
    index_input.evidence_epoch = fixture.evidence_epoch;
    index_input.maximum_candidate_records_per_expansion = 16U;
    IndexHandle index;
    ASSERT_EQ(
        laplace_observation_query_index_create(&index_input, &index.value),
        LAPLACE_OBSERVATION_QUERY_OK);

    ForwardResultHandle result;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute(
            index.value, &request, &result.value, &receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_FALSE(Zero(receipt.output_fingerprint));
}

TEST(CognitionObservationRequest, RequestIdentityChangesWithTypedRelationIntent) {
    const auto fixture = BuildFixture();
    auto predecessor = Request(fixture);
    auto successor = predecessor;
    successor.relation_mask = LAPLACE_OBSERVATION_QUERY_SUCCESSOR;

    laplace_digest256 predecessor_id{};
    laplace_digest256 successor_id{};
    ASSERT_EQ(
        laplace_cognition_observation_request_identify(
            &predecessor, &predecessor_id),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_EQ(
        laplace_cognition_observation_request_identify(
            &successor, &successor_id),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_FALSE(Same(predecessor_id, successor_id));
}

TEST(CognitionObservationRequest, RejectsPathMultiplicityThatContradictsRequestedResults) {
    const auto fixture = BuildFixture();
    auto request = Request(fixture);
    request.search_budget.requested_path_count = 2U;
    laplace_digest256 fingerprint{};
    EXPECT_EQ(
        laplace_cognition_observation_request_identify(&request, &fingerprint),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_LIMITS);
    EXPECT_TRUE(Zero(fingerprint));
}

TEST(CognitionObservationRequest, GoalPresenceIsExplicitNotInferredFromGoalBytes) {
    const auto fixture = BuildFixture();
    auto request = Request(fixture);
    request.goal_entity_id = fixture.a;
    laplace_digest256 fingerprint{};
    EXPECT_EQ(
        laplace_cognition_observation_request_identify(&request, &fingerprint),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_FLAGS);

    request.flags |= LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT;
    request.flags &= ~LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    EXPECT_EQ(
        laplace_cognition_observation_request_identify(&request, &fingerprint),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_FALSE(Zero(fingerprint));
}

}  // namespace
