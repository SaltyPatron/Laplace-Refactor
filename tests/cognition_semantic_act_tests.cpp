#include "laplace/cognition_semantic_act.h"

#include "laplace/identity.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

bool Zero(const laplace_digest256& value) {
    return std::all_of(
        value.bytes, value.bytes + sizeof(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

laplace_cognition_observation_request Request(const laplace_id128& anchor) {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = anchor;
    request.world_id = Digest(30U);
    request.time_fingerprint = Digest(31U);
    request.context_fingerprint = Digest(32U);
    request.evidence_boundary = Digest(33U);
    request.evidence_epoch = Digest(34U);
    request.authority_id = Digest(35U);
    request.result_contract_fingerprint = Digest(36U);
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    request.maximum_results = 1U;
    request.flags = LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    request.search_budget.max_expanded_states = 4U;
    request.search_budget.max_transition_records = 8U;
    request.search_budget.max_emitted_states = 8U;
    request.search_budget.max_frontier_states = 8U;
    request.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.search_budget.max_io_operations = 4U;
    request.search_budget.max_database_operations = 4U;
    request.search_budget.max_provider_calls = 4U;
    request.search_budget.max_depth = 1U;
    request.search_budget.requested_path_count = 1U;
    request.search_budget.frontier_batch_width = 2U;
    request.search_budget.transition_batch_capacity = 8U;

    request.forward_limits.max_layers = 1U;
    request.forward_limits.max_provider_calls = 2U;
    request.forward_limits.max_projected_queries = 2U;
    request.forward_limits.max_candidate_operations = 2U;
    request.forward_limits.max_resolutions = 1U;
    request.forward_limits.max_resource_cost = 128U;
    request.forward_limits.max_io_operations = 4U;
    request.forward_limits.max_database_operations = 4U;
    request.forward_limits.candidate_operation_capacity = 2U;
    request.forward_limits.resolution_capacity = 1U;
    return request;
}

struct Backend final {
    laplace_id128 source{};
    laplace_id128 target{};
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
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;
    (void)accumulated_costs;

    for (std::size_t source_index = 0U;
         source_index < frontier_state_count; ++source_index) {
        if (!SameId(source_entity_ids[source_index], backend.source)) continue;
        if (*candidate_count >= candidate_capacity) return 2;
        auto& candidate = candidates[*candidate_count];
        candidate = laplace_cognition_observation_candidate{};
        candidate.target_entity_id = backend.target;
        candidate.observation_fingerprint = Digest(90U);
        candidate.source_state_index = static_cast<std::uint64_t>(source_index);
        candidate.source_logical_ordinal = frontier_states[source_index].depth;
        candidate.target_logical_ordinal =
            static_cast<std::uint64_t>(frontier_states[source_index].depth) + 1U;
        candidate.multiplicity = 1U;
        candidate.gap = 1U;
        candidate.relation = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
        candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
        ++*candidate_count;
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

laplace_cognition_observation_candidate_provider_v1 Provider(Backend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(91U);
    provider.maximum_candidate_records_per_expansion = 2U;
    provider.enumerate_candidates = Enumerate;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}

class ForwardResultHandle final {
public:
    ~ForwardResultHandle() { laplace_cognition_forward_result_destroy(&value); }
    laplace_cognition_forward_result* value{};
};

class ObservationResultHandle final {
public:
    ~ObservationResultHandle() {
        laplace_cognition_observation_result_destroy(&value);
    }
    laplace_cognition_observation_result* value{};
};

struct CompletedFixture final {
    Backend backend{};
    laplace_cognition_observation_request request{};
    ObservationResultHandle observation;
    ForwardResultHandle forward;
    laplace_cognition_forward_receipt receipt{};
};

void Execute(CompletedFixture& fixture) {
    fixture.backend.source = Codepoint(0x42U);
    fixture.backend.target = Codepoint(0x41U);
    fixture.request = Request(fixture.backend.source);
    const auto provider = Provider(&fixture.backend);
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &fixture.request, &provider, &fixture.observation.value,
            &fixture.forward.value, &fixture.receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(fixture.observation.value, nullptr);
    ASSERT_NE(fixture.forward.value, nullptr);
    ASSERT_EQ(fixture.receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    ASSERT_EQ(
        fixture.receipt.final_completion,
        LAPLACE_COGNITION_COMPLETION_COMPLETE);
    ASSERT_EQ(fixture.receipt.final_remaining_required_count, 0U);
}

TEST(CognitionSemanticAct, CompletedRequestSelectsNativeTerminalActWithoutTextIntentLookup) {
    CompletedFixture fixture;
    Execute(fixture);

    laplace_cognition_semantic_act act{};
    ASSERT_EQ(
        laplace_cognition_observation_semantic_act_select(
            &fixture.request, fixture.observation.value, fixture.forward.value,
            &fixture.receipt, &act),
        LAPLACE_COGNITION_SEMANTIC_ACT_OK);

    laplace_digest256 request_fingerprint{};
    ASSERT_EQ(
        laplace_cognition_observation_request_identify(
            &fixture.request, &request_fingerprint),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_FALSE(Zero(act.act_id));
    EXPECT_TRUE(SameDigest(act.request_fingerprint, request_fingerprint));
    EXPECT_TRUE(SameDigest(
        act.result_contract_fingerprint,
        fixture.request.result_contract_fingerprint));
    EXPECT_TRUE(SameDigest(act.forward_receipt_id, fixture.receipt.receipt_id));
    EXPECT_TRUE(SameDigest(
        act.forward_output_fingerprint, fixture.receipt.output_fingerprint));
    EXPECT_TRUE(SameDigest(act.final_state_id, fixture.receipt.final_state_id));
    EXPECT_FALSE(Zero(act.answer_set_fingerprint));
    EXPECT_TRUE(SameId(act.primary_answer.entity_id, fixture.backend.target));
    EXPECT_EQ(act.answer_count, 1U);
    EXPECT_EQ(act.operation_kind, LAPLACE_COGNITION_OPERATION_ANSWER);
    EXPECT_EQ(
        act.flags,
        LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
            LAPLACE_COGNITION_SEMANTIC_ACT_TERMINAL_OPERATION_PRESENT);
    EXPECT_EQ(act.version, LAPLACE_COGNITION_SEMANTIC_ACT_VERSION);
}

TEST(CognitionSemanticAct, IncompleteReceiptCannotBePromotedToSemanticAct) {
    CompletedFixture fixture;
    Execute(fixture);
    auto incomplete = fixture.receipt;
    incomplete.final_completion = LAPLACE_COGNITION_COMPLETION_INCOMPLETE;
    incomplete.final_remaining_required_count = 1U;

    laplace_cognition_semantic_act act{};
    act.act_id = Digest(200U);
    EXPECT_EQ(
        laplace_cognition_observation_semantic_act_select(
            &fixture.request, fixture.observation.value, fixture.forward.value,
            &incomplete, &act),
        LAPLACE_COGNITION_SEMANTIC_ACT_INCOMPLETE);
    EXPECT_TRUE(Zero(act.act_id));
    EXPECT_EQ(act.answer_count, 0U);
    EXPECT_EQ(act.operation_kind, 0U);
}

TEST(CognitionSemanticAct, ReceiptAndForwardResultMustDescribeSameFinalState) {
    CompletedFixture fixture;
    Execute(fixture);
    auto drifted = fixture.receipt;
    drifted.final_state_id = Digest(201U);

    laplace_cognition_semantic_act act{};
    EXPECT_EQ(
        laplace_cognition_observation_semantic_act_select(
            &fixture.request, fixture.observation.value, fixture.forward.value,
            &drifted, &act),
        LAPLACE_COGNITION_SEMANTIC_ACT_FORWARD_RESULT_FAILURE);
    EXPECT_TRUE(Zero(act.act_id));
}

}  // namespace
