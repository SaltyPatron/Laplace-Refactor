#include "laplace/observation_query.h"

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

struct ObservationFixture final {
    laplace_id128 a{};
    laplace_id128 b{};
    laplace_id128 c{};
    laplace_id128 root{};
    std::array<laplace_trajectory_carrier, 3> carriers{};
    laplace_persistence_physicality_record physicality{};
    std::array<laplace_persistence_trajectory_segment_record, 3> segments{};
    laplace_digest256 boundary{};
    laplace_digest256 evidence_epoch{};
};

ObservationFixture BuildFixture() {
    ObservationFixture fixture{};
    fixture.a = Codepoint(0x41U);
    fixture.b = Codepoint(0x42U);
    fixture.c = Codepoint(0x43U);
    fixture.root = Codepoint(0x52U);
    fixture.boundary = Digest(180U);
    fixture.evidence_epoch = Digest(200U);

    EXPECT_EQ(
        laplace_trajectory_composition_encode(
            &fixture.a, 1U, 2U, Metadata(2U, 0x41U), &fixture.carriers[0]),
        LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(
        laplace_trajectory_composition_encode(
            &fixture.b, 3U, 1U, Metadata(2U, 0x42U), &fixture.carriers[1]),
        LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(
        laplace_trajectory_composition_encode(
            &fixture.c, 4U, 1U, Metadata(2U, 0x43U), &fixture.carriers[2]),
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
    EXPECT_EQ(
        laplace_persistence_trajectory_fingerprint(
            fixture.carriers.data(), fixture.carriers.size(),
            &fixture.physicality.trajectory_fingerprint),
        LAPLACE_PERSISTENCE_OK);
    fixture.physicality.centroid.component[0] = 0.125;
    fixture.physicality.centroid.component[1] = -0.25;
    fixture.physicality.centroid.component[2] = 0.5;
    fixture.physicality.centroid.component[3] = -0.75;
    fixture.physicality.radius = 0.875;
    fixture.physicality.logical_count = 4U;
    fixture.physicality.vertex_count = fixture.carriers.size();
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

laplace_observation_query_binding Binding(
    const laplace_id128& anchor,
    const std::uint32_t relation_mask,
    const std::uint32_t maximum_results = 1U) {
    laplace_observation_query_binding binding{};
    binding.anchor_entity_id = anchor;
    binding.relation_mask = relation_mask;
    binding.maximum_results = maximum_results;
    binding.flags = LAPLACE_OBSERVATION_QUERY_BINDING_TERMINAL_RESULTS;
    EXPECT_EQ(
        laplace_observation_query_binding_identify(
            &binding, &binding.binding_fingerprint),
        LAPLACE_OBSERVATION_QUERY_OK);
    return binding;
}

class IndexHandle final {
public:
    IndexHandle() = default;
    IndexHandle(const IndexHandle&) = delete;
    IndexHandle& operator=(const IndexHandle&) = delete;
    IndexHandle(IndexHandle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    IndexHandle& operator=(IndexHandle&& other) noexcept {
        if (this != &other) {
            laplace_observation_query_index_destroy(&value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~IndexHandle() { laplace_observation_query_index_destroy(&value); }
    laplace_observation_query_index* value{};
};

IndexHandle CreateIndex(
    const ObservationFixture& fixture,
    const std::vector<laplace_observation_query_binding>& bindings,
    const std::uint64_t maximum_candidate_records = 32U) {
    laplace_observation_query_index_input input{};
    input.physicalities = &fixture.physicality;
    input.physicality_count = 1U;
    input.trajectory_segments = fixture.segments.data();
    input.trajectory_segment_count = fixture.segments.size();
    input.bindings = bindings.data();
    input.binding_count = bindings.size();
    input.boundary_id = fixture.boundary;
    input.evidence_epoch = fixture.evidence_epoch;
    input.maximum_candidate_records_per_expansion = maximum_candidate_records;
    IndexHandle index;
    EXPECT_EQ(
        laplace_observation_query_index_create(&input, &index.value),
        LAPLACE_OBSERVATION_QUERY_OK);
    return index;
}

laplace_query_search_state SearchState(
    const ObservationFixture& fixture,
    const laplace_observation_query_binding& binding) {
    laplace_query_search_state state{};
    state.state_id = Digest(1U);
    EXPECT_EQ(
        laplace_observation_query_entity_coordinate(
            &binding.anchor_entity_id, &state.anchor_id),
        LAPLACE_OBSERVATION_QUERY_OK);
    state.dominance_key = Digest(2U);
    state.binding_fingerprint = binding.binding_fingerprint;
    state.obligation_fingerprint = Digest(3U);
    state.context_fingerprint = Digest(4U);
    state.evidence_epoch = fixture.evidence_epoch;
    state.boundary_id = fixture.boundary;
    return state;
}

laplace_query_search_program SearchProgram(
    const ObservationFixture& fixture,
    const std::uint32_t requested_paths,
    const std::uint32_t transition_capacity = 32U) {
    laplace_query_search_program program{};
    program.program_id = Digest(10U);
    program.goal_id = Digest(11U);
    program.boundary_id = fixture.boundary;
    program.evidence_epoch = fixture.evidence_epoch;
    program.result_contract_fingerprint = Digest(12U);
    program.cost_weights[0] = 1U;
    program.budget.max_expanded_states = 1U;
    program.budget.max_transition_records = transition_capacity;
    program.budget.max_emitted_states = static_cast<std::uint64_t>(transition_capacity) + 1U;
    program.budget.max_frontier_states = static_cast<std::uint64_t>(transition_capacity) + 1U;
    program.budget.max_memory_bytes = UINT64_C(1048576);
    program.budget.max_io_operations = 1U;
    program.budget.max_database_operations = 1U;
    program.budget.max_provider_calls = 1U;
    program.budget.max_depth = 1U;
    program.budget.requested_path_count = requested_paths;
    program.budget.frontier_batch_width = 2U;
    program.budget.transition_batch_capacity = transition_capacity;
    program.flags =
        LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE |
        LAPLACE_QUERY_SEARCH_FLAG_REQUIRE_SET_ORIENTED;
    return program;
}

laplace_cognition_guidance_header GuidanceHeader(
    const ObservationFixture& fixture) {
    laplace_cognition_guidance_header header{};
    header.program_id = Digest(60U);
    header.goal_id = Digest(61U);
    header.bindings_fingerprint = Digest(62U);
    header.scope_fingerprint = Digest(63U);
    header.world_id = Digest(64U);
    header.time_fingerprint = Digest(65U);
    header.context_fingerprint = Digest(66U);
    header.evidence_epoch = fixture.evidence_epoch;
    header.authority_id = Digest(67U);
    header.result_contract_fingerprint = Digest(68U);
    header.version = LAPLACE_COGNITION_GUIDANCE_VERSION;
    return header;
}

laplace_cognition_forward_program ForwardProgram(
    const laplace_cognition_guidance_header& header) {
    laplace_cognition_forward_program program{};
    program.program_id = Digest(90U);
    program.result_contract_fingerprint = header.result_contract_fingerprint;
    program.max_layers = 2U;
    program.max_provider_calls = 4U;
    program.max_projected_queries = 4U;
    program.max_candidate_operations = 4U;
    program.max_resolutions = 2U;
    program.max_resource_cost = 128U;
    program.max_io_operations = 8U;
    program.max_database_operations = 8U;
    program.candidate_operation_capacity = 4U;
    program.resolution_capacity = 2U;
    program.flags =
        LAPLACE_COGNITION_FORWARD_REQUIRE_STATE_PROGRESS |
        LAPLACE_COGNITION_FORWARD_REQUIRE_PROJECTED_QUERY_CONSUMPTION |
        LAPLACE_COGNITION_FORWARD_REQUIRE_RECEIPTED_EXECUTION;
    program.version = LAPLACE_COGNITION_FORWARD_VERSION;
    return program;
}

laplace_cognition_obligation OpenObservationObligation(
    const ObservationFixture& fixture,
    const laplace_observation_query_binding& binding,
    const laplace_cognition_guidance_header& header,
    const std::uint32_t flags = LAPLACE_COGNITION_OBLIGATION_REQUIRED) {
    laplace_cognition_obligation obligation{};
    obligation.obligation_id = Digest(80U);
    obligation.binding_fingerprint = binding.binding_fingerprint;
    obligation.world_id = header.world_id;
    obligation.time_fingerprint = header.time_fingerprint;
    obligation.context_fingerprint = header.context_fingerprint;
    obligation.evidence_boundary = fixture.boundary;
    obligation.authority_id = header.authority_id;
    obligation.result_contract_fingerprint = header.result_contract_fingerprint;
    obligation.kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    obligation.disposition = LAPLACE_COGNITION_OBLIGATION_OPEN;
    obligation.flags = flags;
    return obligation;
}

class GuidanceStateHandle final {
public:
    ~GuidanceStateHandle() {
        laplace_cognition_guidance_state_destroy(&value);
    }
    laplace_cognition_guidance_state* value{};
};

class ForwardResultHandle final {
public:
    ~ForwardResultHandle() {
        laplace_cognition_forward_result_destroy(&value);
    }
    laplace_cognition_forward_result* value{};
};

TEST(ObservationQuery, PreservesLogicalOccurrencesBeyondCarrierCount) {
    const auto fixture = BuildFixture();
    const std::vector bindings{
        Binding(fixture.a, LAPLACE_OBSERVATION_QUERY_SUCCESSOR, 2U)};
    auto index = CreateIndex(fixture, bindings);
    ASSERT_NE(index.value, nullptr);
    laplace_observation_query_index_summary summary{};
    ASSERT_EQ(
        laplace_observation_query_index_summary_get(index.value, &summary),
        LAPLACE_OBSERVATION_QUERY_OK);
    EXPECT_EQ(summary.physicality_count, 1U);
    EXPECT_EQ(summary.trajectory_segment_count, 3U);
    EXPECT_EQ(summary.occurrence_run_count, 3U);
    EXPECT_EQ(summary.logical_occurrence_count, 4U);
    EXPECT_GT(summary.logical_occurrence_count, summary.trajectory_segment_count);
    EXPECT_FALSE(Zero(summary.index_fingerprint));
}

TEST(ObservationQuery, RejectsCarrierDriftAgainstCanonicalPhysicality) {
    auto fixture = BuildFixture();
    const auto binding = Binding(
        fixture.b, LAPLACE_OBSERVATION_QUERY_PREDECESSOR, 1U);
    laplace_trajectory_carrier altered{};
    ASSERT_EQ(
        laplace_trajectory_composition_encode(
            &fixture.c, 3U, 1U, Metadata(2U, 0x43U), &altered),
        LAPLACE_TRAJECTORY_OK);
    fixture.segments[1].carrier = altered;
    laplace_observation_query_index_input input{};
    input.physicalities = &fixture.physicality;
    input.physicality_count = 1U;
    input.trajectory_segments = fixture.segments.data();
    input.trajectory_segment_count = fixture.segments.size();
    input.bindings = &binding;
    input.binding_count = 1U;
    input.boundary_id = fixture.boundary;
    input.evidence_epoch = fixture.evidence_epoch;
    input.maximum_candidate_records_per_expansion = 32U;
    laplace_observation_query_index* index = nullptr;
    EXPECT_EQ(
        laplace_observation_query_index_create(&input, &index),
        LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID);
    EXPECT_EQ(index, nullptr);
}

TEST(ObservationQuery, ExpandsImmediateSuccessorFromPhysicalityWithoutTestimony) {
    const auto fixture = BuildFixture();
    const auto binding = Binding(
        fixture.a, LAPLACE_OBSERVATION_QUERY_SUCCESSOR, 2U);
    const std::vector bindings{binding};
    auto index = CreateIndex(fixture, bindings);
    ASSERT_NE(index.value, nullptr);
    laplace_query_search_provider_v1 provider{};
    ASSERT_EQ(
        laplace_observation_query_search_provider(index.value, &provider),
        LAPLACE_OBSERVATION_QUERY_OK);
    const auto state = SearchState(fixture, binding);
    const std::uint64_t accumulated_cost = 0U;
    std::array<laplace_query_search_transition, 8> transitions{};
    std::size_t transition_count = 0U;
    laplace_query_search_expansion_receipt receipt{};
    ASSERT_EQ(
        provider.expand_batch(
            provider.state, &state, &accumulated_cost, 1U,
            transitions.data(), transitions.size(), &transition_count, &receipt),
        0);
    ASSERT_EQ(transition_count, 2U);
    EXPECT_EQ(receipt.frontier_state_count, 1U);
    EXPECT_EQ(receipt.emitted_transition_count, 2U);
    EXPECT_EQ(receipt.io_operations, 0U);
    EXPECT_EQ(receipt.database_operations, 0U);

    laplace_digest256 a_coordinate{};
    laplace_digest256 b_coordinate{};
    ASSERT_EQ(
        laplace_observation_query_entity_coordinate(&fixture.a, &a_coordinate),
        LAPLACE_OBSERVATION_QUERY_OK);
    ASSERT_EQ(
        laplace_observation_query_entity_coordinate(&fixture.b, &b_coordinate),
        LAPLACE_OBSERVATION_QUERY_OK);
    bool saw_internal_run_successor = false;
    bool saw_next_run_successor = false;
    for (std::size_t transition_index = 0U;
         transition_index < transition_count; ++transition_index) {
        EXPECT_EQ(
            transitions[transition_index].relation_family,
            LAPLACE_OBSERVATION_QUERY_SUCCESSOR);
        EXPECT_EQ(
            transitions[transition_index].source_layer,
            LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY);
        EXPECT_TRUE(Zero(transitions[transition_index].evidence_root_fingerprint));
        saw_internal_run_successor = saw_internal_run_successor ||
            Same(transitions[transition_index].target.anchor_id, a_coordinate);
        saw_next_run_successor = saw_next_run_successor ||
            Same(transitions[transition_index].target.anchor_id, b_coordinate);
    }
    EXPECT_TRUE(saw_internal_run_successor);
    EXPECT_TRUE(saw_next_run_successor);
}

TEST(ObservationQuery, CognitionForwardPassExecutesSharedSearchOverObservationState) {
    const auto fixture = BuildFixture();
    const auto binding = Binding(
        fixture.b, LAPLACE_OBSERVATION_QUERY_PREDECESSOR, 1U);
    const std::vector bindings{binding};
    auto index = CreateIndex(fixture, bindings, 16U);
    ASSERT_NE(index.value, nullptr);

    const auto header = GuidanceHeader(fixture);
    const auto obligation = OpenObservationObligation(fixture, binding, header);
    GuidanceStateHandle guidance;
    ASSERT_EQ(
        laplace_cognition_guidance_state_create(
            &header, &obligation, 1U, &guidance.value),
        LAPLACE_COGNITION_GUIDANCE_OK);

    laplace_cognition_forward_provider_v1 provider{};
    ASSERT_EQ(
        laplace_observation_query_cognition_provider(index.value, &provider),
        LAPLACE_OBSERVATION_QUERY_OK);
    const auto program = ForwardProgram(header);

    ForwardResultHandle result;
    laplace_cognition_forward_receipt forward_receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, guidance.value, &provider,
            &result.value, &forward_receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(
        forward_receipt.disposition,
        LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(forward_receipt.layer_count, 1U);
    EXPECT_EQ(forward_receipt.final_remaining_required_count, 0U);
    EXPECT_EQ(
        forward_receipt.final_completion,
        LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_GT(forward_receipt.resource_cost, 0U);
    EXPECT_EQ(forward_receipt.io_operations, 0U);
    EXPECT_EQ(forward_receipt.database_operations, 0U);

    GuidanceStateHandle final_state;
    ASSERT_EQ(
        laplace_cognition_forward_result_final_state_clone(
            result.value, &final_state.value),
        LAPLACE_COGNITION_FORWARD_OK);
    laplace_cognition_obligation resolved{};
    ASSERT_EQ(
        laplace_cognition_guidance_state_obligation(
            final_state.value, 0U, &resolved),
        LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_EQ(
        resolved.disposition,
        LAPLACE_COGNITION_OBLIGATION_SATISFIED);
    EXPECT_FALSE(Zero(resolved.value_id));
    EXPECT_FALSE(Zero(resolved.resolution_receipt_id));

    laplace_query_search_provider_v1 search_provider{};
    ASSERT_EQ(
        laplace_observation_query_search_provider(index.value, &search_provider),
        LAPLACE_OBSERVATION_QUERY_OK);
    const auto search_state = SearchState(fixture, binding);
    const auto search_program = SearchProgram(fixture, 1U, 16U);
    laplace_query_search_result* search_result = nullptr;
    laplace_query_search_receipt search_receipt{};
    ASSERT_EQ(
        laplace_query_search_execute(
            &search_program, &search_state, 1U, &search_provider,
            &search_result, &search_receipt),
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
    EXPECT_TRUE(Zero(steps[0].evidence_root_fingerprint));
    laplace_query_search_result_destroy(&search_result);
}

TEST(ObservationQuery, PartialRequestedResultSetCannotSatisfyCognition) {
    const auto fixture = BuildFixture();
    const auto binding = Binding(
        fixture.b, LAPLACE_OBSERVATION_QUERY_PREDECESSOR, 2U);
    const std::vector bindings{binding};
    auto index = CreateIndex(fixture, bindings, 16U);
    ASSERT_NE(index.value, nullptr);

    const auto header = GuidanceHeader(fixture);
    const auto obligation = OpenObservationObligation(
        fixture, binding, header,
        LAPLACE_COGNITION_OBLIGATION_REQUIRED |
            LAPLACE_COGNITION_OBLIGATION_ALLOW_TYPED_UNRESOLVED);
    GuidanceStateHandle guidance;
    ASSERT_EQ(
        laplace_cognition_guidance_state_create(
            &header, &obligation, 1U, &guidance.value),
        LAPLACE_COGNITION_GUIDANCE_OK);
    laplace_cognition_forward_provider_v1 provider{};
    ASSERT_EQ(
        laplace_observation_query_cognition_provider(index.value, &provider),
        LAPLACE_OBSERVATION_QUERY_OK);
    const auto program = ForwardProgram(header);
    ForwardResultHandle result;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, guidance.value, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);

    GuidanceStateHandle final_state;
    ASSERT_EQ(
        laplace_cognition_forward_result_final_state_clone(
            result.value, &final_state.value),
        LAPLACE_COGNITION_FORWARD_OK);
    laplace_cognition_obligation resolved{};
    ASSERT_EQ(
        laplace_cognition_guidance_state_obligation(
            final_state.value, 0U, &resolved),
        LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_EQ(resolved.disposition, LAPLACE_COGNITION_OBLIGATION_UNKNOWN);
    EXPECT_TRUE(Zero(resolved.value_id));
    EXPECT_FALSE(Zero(resolved.resolution_receipt_id));
}

}  // namespace
