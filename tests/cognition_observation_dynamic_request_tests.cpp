#include "laplace/cognition_observation_request.h"

#include "laplace/identity.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

laplace_digest256 DynamicDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

bool SameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

std::uint64_t DynamicMetadata(
    const std::uint8_t tier,
    const std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
        (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) |
        (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

laplace_id128 DynamicCodepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

struct DynamicFixture final {
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

DynamicFixture BuildDynamicFixture() {
    DynamicFixture fixture{};
    fixture.a = DynamicCodepoint(0x41U);
    fixture.b = DynamicCodepoint(0x42U);
    fixture.c = DynamicCodepoint(0x43U);
    fixture.root = DynamicCodepoint(0x52U);
    fixture.boundary = DynamicDigest(180U);
    fixture.evidence_epoch = DynamicDigest(200U);

    const std::array<laplace_id128, 3> entities{{
        fixture.a, fixture.b, fixture.c}};
    const std::array<std::uint32_t, 3> atoms{{0x41U, 0x42U, 0x43U}};
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        EXPECT_EQ(
            laplace_trajectory_composition_encode(
                &entities[index], static_cast<std::uint64_t>(index + 1U), 1U,
                DynamicMetadata(2U, atoms[index]), &fixture.carriers[index]),
            LAPLACE_TRAJECTORY_OK);
    }

    fixture.physicality.entity_id = fixture.root;
    fixture.physicality.physicality_type =
        LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    fixture.physicality.vertex_class =
        LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    fixture.physicality.recipe_version = 1U;
    fixture.physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    fixture.physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    fixture.physicality.recipe_fingerprint = DynamicDigest(20U);
    fixture.physicality.geometry_epoch = DynamicDigest(40U);
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

laplace_cognition_observation_request DynamicRequest(
    const DynamicFixture& fixture,
    const laplace_id128 anchor,
    const laplace_id128 goal,
    const std::uint32_t relation) {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = anchor;
    request.goal_entity_id = goal;
    request.world_id = DynamicDigest(60U);
    request.time_fingerprint = DynamicDigest(61U);
    request.context_fingerprint = DynamicDigest(62U);
    request.evidence_boundary = fixture.boundary;
    request.evidence_epoch = fixture.evidence_epoch;
    request.authority_id = DynamicDigest(63U);
    request.result_contract_fingerprint = DynamicDigest(64U);
    request.relation_mask = relation;
    request.maximum_results = 1U;
    request.flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;
    request.search_budget.max_expanded_states = 2U;
    request.search_budget.max_transition_records = 8U;
    request.search_budget.max_emitted_states = 8U;
    request.search_budget.max_frontier_states = 8U;
    request.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.search_budget.max_io_operations = 8U;
    request.search_budget.max_database_operations = 8U;
    request.search_budget.max_provider_calls = 2U;
    request.search_budget.max_depth = 2U;
    request.search_budget.requested_path_count = 1U;
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

class DynamicIndexHandle final {
public:
    ~DynamicIndexHandle() { laplace_observation_query_index_destroy(&value); }
    laplace_observation_query_index* value{};
};

class DynamicCompiledHandle final {
public:
    ~DynamicCompiledHandle() {
        laplace_cognition_observation_compiled_request_destroy(&value);
    }
    laplace_cognition_observation_compiled_request value{};
};

class DynamicProviderHandle final {
public:
    ~DynamicProviderHandle() {
        laplace_cognition_observation_request_provider_destroy(&value);
    }
    laplace_cognition_observation_request_provider* value{};
};

class DynamicForwardHandle final {
public:
    ~DynamicForwardHandle() {
        laplace_cognition_forward_result_destroy(&value);
    }
    laplace_cognition_forward_result* value{};
};

DynamicIndexHandle BuildUnboundIndex(const DynamicFixture& fixture) {
    laplace_observation_query_index_base_input input{};
    input.physicalities = &fixture.physicality;
    input.physicality_count = 1U;
    input.trajectory_segments = fixture.segments.data();
    input.trajectory_segment_count = fixture.segments.size();
    input.boundary_id = fixture.boundary;
    input.evidence_epoch = fixture.evidence_epoch;
    input.maximum_candidate_records_per_expansion = 8U;
    DynamicIndexHandle index;
    EXPECT_EQ(
        laplace_observation_query_index_create_base(&input, &index.value),
        LAPLACE_OBSERVATION_QUERY_OK);
    return index;
}

TEST(CognitionObservationRequest, NewRequestExecutesAgainstPreviouslyBuiltUnboundIndex) {
    const auto fixture = BuildDynamicFixture();

    /* The observation estate exists before this request or binding exists. */
    auto index = BuildUnboundIndex(fixture);
    ASSERT_NE(index.value, nullptr);
    laplace_observation_query_index_summary before{};
    ASSERT_EQ(
        laplace_observation_query_index_summary_get(index.value, &before),
        LAPLACE_OBSERVATION_QUERY_OK);
    ASSERT_EQ(before.binding_count, 0U);

    const auto request = DynamicRequest(
        fixture, fixture.c, fixture.a, LAPLACE_OBSERVATION_QUERY_PREDECESSOR);
    DynamicCompiledHandle compiled;
    ASSERT_EQ(
        laplace_cognition_observation_request_compile(&request, &compiled.value),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);

    DynamicProviderHandle provider_state;
    laplace_cognition_forward_provider_v1 provider{};
    ASSERT_EQ(
        laplace_cognition_observation_request_cognition_provider(
            index.value, &compiled.value, &provider_state.value, &provider),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);

    DynamicForwardHandle result;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &compiled.value.forward_program,
            compiled.value.guidance_state,
            &provider,
            &result.value,
            &receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);

    laplace_observation_query_index_summary after{};
    ASSERT_EQ(
        laplace_observation_query_index_summary_get(index.value, &after),
        LAPLACE_OBSERVATION_QUERY_OK);
    EXPECT_EQ(after.binding_count, 0U);
    EXPECT_TRUE(SameDigest(before.index_fingerprint, after.index_fingerprint));
}

TEST(CognitionObservationRequest, DifferentRuntimeRequestsDoNotChangeIndexIdentity) {
    const auto fixture = BuildDynamicFixture();
    auto index = BuildUnboundIndex(fixture);
    ASSERT_NE(index.value, nullptr);
    laplace_observation_query_index_summary before{};
    ASSERT_EQ(
        laplace_observation_query_index_summary_get(index.value, &before),
        LAPLACE_OBSERVATION_QUERY_OK);

    const auto predecessor = DynamicRequest(
        fixture, fixture.c, fixture.a, LAPLACE_OBSERVATION_QUERY_PREDECESSOR);
    const auto successor = DynamicRequest(
        fixture, fixture.a, fixture.c, LAPLACE_OBSERVATION_QUERY_SUCCESSOR);
    DynamicCompiledHandle first;
    DynamicCompiledHandle second;
    ASSERT_EQ(
        laplace_cognition_observation_request_compile(&predecessor, &first.value),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_EQ(
        laplace_cognition_observation_request_compile(&successor, &second.value),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_FALSE(SameDigest(
        first.value.request_fingerprint, second.value.request_fingerprint));

    DynamicProviderHandle first_provider;
    DynamicProviderHandle second_provider;
    laplace_cognition_forward_provider_v1 first_forward{};
    laplace_cognition_forward_provider_v1 second_forward{};
    ASSERT_EQ(
        laplace_cognition_observation_request_cognition_provider(
            index.value, &first.value, &first_provider.value, &first_forward),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_EQ(
        laplace_cognition_observation_request_cognition_provider(
            index.value, &second.value, &second_provider.value, &second_forward),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);

    laplace_observation_query_index_summary after{};
    ASSERT_EQ(
        laplace_observation_query_index_summary_get(index.value, &after),
        LAPLACE_OBSERVATION_QUERY_OK);
    EXPECT_EQ(after.binding_count, 0U);
    EXPECT_TRUE(SameDigest(before.index_fingerprint, after.index_fingerprint));
}

}  // namespace
