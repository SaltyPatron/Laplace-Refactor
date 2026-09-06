#include "laplace/cognition_turn.h"

#include "laplace/cognition_semantic_act.h"
#include "laplace/identity.h"

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
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool ZeroDigest(const laplace_digest256& value) {
    return std::all_of(
        value.bytes, value.bytes + sizeof(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

laplace_cognition_turn_input TurnInput(
    const laplace_id128& observation,
    const std::uint64_t ordinal,
    const std::uint32_t flags) {
    laplace_cognition_turn_input input{};
    input.discourse_id = Digest(10U);
    input.observation_entity_id = observation;
    input.observation_occurrence_id = Digest(
        static_cast<std::uint8_t>(20U + ordinal));
    input.world_id = Digest(30U);
    input.time_fingerprint = Digest(
        static_cast<std::uint8_t>(40U + ordinal));
    input.context_fingerprint = Digest(
        static_cast<std::uint8_t>(50U + ordinal));
    input.turn_ordinal = ordinal;
    input.flags = flags;
    input.version = LAPLACE_COGNITION_TURN_VERSION;
    return input;
}

laplace_cognition_turn_policy Policy() {
    laplace_cognition_turn_policy policy{};
    policy.evidence_boundary = Digest(60U);
    policy.evidence_epoch = Digest(61U);
    policy.authority_id = Digest(62U);
    policy.result_contract_fingerprint = Digest(63U);
    policy.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    policy.maximum_results = 1U;
    policy.request_flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    policy.version = LAPLACE_COGNITION_TURN_POLICY_VERSION;

    policy.search_budget.max_expanded_states = 4U;
    policy.search_budget.max_transition_records = 8U;
    policy.search_budget.max_emitted_states = 8U;
    policy.search_budget.max_frontier_states = 8U;
    policy.search_budget.max_memory_bytes = UINT64_C(1048576);
    policy.search_budget.max_io_operations = 4U;
    policy.search_budget.max_database_operations = 4U;
    policy.search_budget.max_provider_calls = 4U;
    policy.search_budget.max_depth = 1U;
    policy.search_budget.requested_path_count = 1U;
    policy.search_budget.frontier_batch_width = 2U;
    policy.search_budget.transition_batch_capacity = 8U;

    policy.forward_limits.max_layers = 1U;
    policy.forward_limits.max_provider_calls = 2U;
    policy.forward_limits.max_projected_queries = 2U;
    policy.forward_limits.max_candidate_operations = 2U;
    policy.forward_limits.max_resolutions = 1U;
    policy.forward_limits.max_resource_cost = 128U;
    policy.forward_limits.max_io_operations = 4U;
    policy.forward_limits.max_database_operations = 4U;
    policy.forward_limits.candidate_operation_capacity = 2U;
    policy.forward_limits.resolution_capacity = 1U;
    return policy;
}

struct Backend final {
    laplace_id128 source{};
    laplace_id128 target{};
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
        candidate.relation_family = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
        candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
        candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE;
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

laplace_cognition_semantic_act SyntheticSemanticAct() {
    laplace_cognition_semantic_act act{};
    act.act_id = Digest(100U);
    act.request_fingerprint = Digest(101U);
    act.result_contract_fingerprint = Digest(102U);
    act.forward_receipt_id = Digest(103U);
    act.forward_output_fingerprint = Digest(104U);
    act.final_state_id = Digest(105U);
    act.answer_set_fingerprint = Digest(106U);
    act.primary_answer.entity_id = Codepoint(0x41U);
    act.answer_count = 1U;
    act.act_kind = LAPLACE_COGNITION_OPERATION_ANSWER;
    act.producer_operation_kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    act.flags = LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_PRODUCER_OPERATION_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT;
    act.version = LAPLACE_COGNITION_SEMANTIC_ACT_VERSION;
    return act;
}

laplace_cognition_discourse_state PreviousState() {
    const auto turn = TurnInput(Codepoint(0x42U), 0U, 0U);
    const auto act = SyntheticSemanticAct();
    laplace_cognition_discourse_input input{};
    input.discourse_id = turn.discourse_id;
    input.observation_entity_id = turn.observation_entity_id;
    input.observation_occurrence_id = turn.observation_occurrence_id;
    input.world_id = turn.world_id;
    input.time_fingerprint = turn.time_fingerprint;
    input.context_fingerprint = turn.context_fingerprint;
    input.turn_ordinal = turn.turn_ordinal;
    input.version = LAPLACE_COGNITION_DISCOURSE_VERSION;
    laplace_cognition_discourse_state state{};
    EXPECT_EQ(
        laplace_cognition_discourse_state_create(&input, &act, &state),
        LAPLACE_COGNITION_DISCOURSE_OK);
    return state;
}

TEST(CognitionTurn, FirstTurnCompilesFiniteCanonicalRequestWithoutPromptInspection) {
    const auto turn = TurnInput(Codepoint(0x42U), 0U, 0U);
    const auto policy = Policy();
    laplace_cognition_observation_request request{};
    laplace_cognition_turn_receipt receipt{};

    ASSERT_EQ(
        laplace_cognition_turn_compile(
            &turn, &policy, nullptr, 0U, &request, &receipt),
        LAPLACE_COGNITION_TURN_OK);

    EXPECT_TRUE(SameId(request.anchor_entity_id, turn.observation_entity_id));
    EXPECT_TRUE(SameDigest(request.world_id, turn.world_id));
    EXPECT_TRUE(SameDigest(request.time_fingerprint, turn.time_fingerprint));
    EXPECT_FALSE(SameDigest(request.context_fingerprint, turn.context_fingerprint));
    EXPECT_EQ(request.relation_mask, policy.relation_mask);
    EXPECT_EQ(request.maximum_results, policy.maximum_results);
    EXPECT_EQ(request.search_budget.max_expanded_states,
              policy.search_budget.max_expanded_states);
    EXPECT_EQ(request.forward_limits.max_layers, policy.forward_limits.max_layers);
    EXPECT_EQ(receipt.turn_ordinal, 0U);
    EXPECT_EQ(receipt.previous_frame_bytes, 0U);
    EXPECT_TRUE(ZeroDigest(receipt.previous_state_id));
    EXPECT_TRUE(ZeroDigest(receipt.previous_frame_fingerprint));
    EXPECT_FALSE(ZeroDigest(receipt.turn_fingerprint));

    laplace_digest256 identified{};
    ASSERT_EQ(
        laplace_cognition_observation_request_identify(&request, &identified),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameDigest(identified, receipt.request_fingerprint));
}

TEST(CognitionTurn, CompletedCognitionPersistsAndResumesAsNextFiniteRequest) {
    const auto first_turn = TurnInput(Codepoint(0x42U), 0U, 0U);
    const auto policy = Policy();
    laplace_cognition_observation_request first_request{};
    laplace_cognition_turn_receipt first_turn_receipt{};
    ASSERT_EQ(
        laplace_cognition_turn_compile(
            &first_turn, &policy, nullptr, 0U,
            &first_request, &first_turn_receipt),
        LAPLACE_COGNITION_TURN_OK);

    Backend backend{};
    backend.source = first_turn.observation_entity_id;
    backend.target = Codepoint(0x41U);
    auto provider = Provider(&backend);
    ObservationResultHandle observation{};
    ForwardResultHandle forward{};
    laplace_cognition_forward_receipt forward_receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &first_request, &provider, &observation.value,
            &forward.value, &forward_receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);

    laplace_cognition_semantic_act act{};
    ASSERT_EQ(
        laplace_cognition_observation_semantic_act_select(
            &first_request, observation.value, forward.value,
            &forward_receipt, &act),
        LAPLACE_COGNITION_SEMANTIC_ACT_OK);
    ASSERT_EQ(act.answer_count, 1U);
    EXPECT_TRUE(SameId(act.primary_answer.entity_id, backend.target));

    laplace_cognition_discourse_input discourse_input{};
    discourse_input.discourse_id = first_turn.discourse_id;
    discourse_input.observation_entity_id = first_turn.observation_entity_id;
    discourse_input.observation_occurrence_id =
        first_turn.observation_occurrence_id;
    discourse_input.world_id = first_turn.world_id;
    discourse_input.time_fingerprint = first_turn.time_fingerprint;
    discourse_input.context_fingerprint = first_request.context_fingerprint;
    discourse_input.turn_ordinal = first_turn.turn_ordinal;
    discourse_input.version = LAPLACE_COGNITION_DISCOURSE_VERSION;
    laplace_cognition_discourse_state state{};
    ASSERT_EQ(
        laplace_cognition_discourse_state_create(
            &discourse_input, &act, &state),
        LAPLACE_COGNITION_DISCOURSE_OK);

    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    std::size_t written = 0U;
    laplace_cognition_discourse_frame_receipt frame_receipt{};
    ASSERT_EQ(
        laplace_cognition_discourse_frame_encode(
            &state, frame.data(), frame.size(), &written, &frame_receipt),
        LAPLACE_COGNITION_DISCOURSE_FRAME_OK);
    ASSERT_EQ(written, frame.size());

    auto second_turn = TurnInput(
        Codepoint(0x43U), 1U,
        LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE);
    laplace_cognition_observation_request second_request{};
    laplace_cognition_turn_receipt second_turn_receipt{};
    ASSERT_EQ(
        laplace_cognition_turn_compile(
            &second_turn, &policy, frame.data(), frame.size(),
            &second_request, &second_turn_receipt),
        LAPLACE_COGNITION_TURN_OK);

    EXPECT_TRUE(SameId(second_request.anchor_entity_id,
                       second_turn.observation_entity_id));
    EXPECT_TRUE(SameDigest(second_turn_receipt.previous_state_id, state.state_id));
    EXPECT_TRUE(SameDigest(
        second_turn_receipt.previous_frame_fingerprint,
        frame_receipt.frame_fingerprint));
    EXPECT_EQ(second_turn_receipt.previous_frame_bytes, frame.size());
    EXPECT_EQ(second_turn_receipt.turn_ordinal, 1U);
    EXPECT_FALSE(SameDigest(
        first_turn_receipt.request_fingerprint,
        second_turn_receipt.request_fingerprint));
    EXPECT_FALSE(SameDigest(
        first_request.context_fingerprint,
        second_request.context_fingerprint));

    laplace_digest256 identified{};
    ASSERT_EQ(
        laplace_cognition_observation_request_identify(
            &second_request, &identified),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameDigest(identified, second_turn_receipt.request_fingerprint));
}

TEST(CognitionTurn, CorruptOrOutOfSequenceDurableStateFailsClosed) {
    const auto state = PreviousState();
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    std::size_t written = 0U;
    laplace_cognition_discourse_frame_receipt frame_receipt{};
    ASSERT_EQ(
        laplace_cognition_discourse_frame_encode(
            &state, frame.data(), frame.size(), &written, &frame_receipt),
        LAPLACE_COGNITION_DISCOURSE_FRAME_OK);
    ASSERT_EQ(written, frame.size());

    auto next_turn = TurnInput(
        Codepoint(0x43U), 1U,
        LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE);
    const auto policy = Policy();
    laplace_cognition_observation_request request{};
    laplace_cognition_turn_receipt receipt{};

    auto corrupt = frame;
    corrupt[LAPLACE_COGNITION_DISCOURSE_FRAME_HEADER_BYTES + 7U] ^= UINT8_C(1);
    EXPECT_EQ(
        laplace_cognition_turn_compile(
            &next_turn, &policy, corrupt.data(), corrupt.size(),
            &request, &receipt),
        LAPLACE_COGNITION_TURN_PREVIOUS_FRAME_INVALID);
    EXPECT_TRUE(ZeroDigest(request.context_fingerprint));
    EXPECT_TRUE(ZeroDigest(receipt.turn_fingerprint));

    next_turn.turn_ordinal = 2U;
    EXPECT_EQ(
        laplace_cognition_turn_compile(
            &next_turn, &policy, frame.data(), frame.size(),
            &request, &receipt),
        LAPLACE_COGNITION_TURN_SEQUENCE_MISMATCH);
    EXPECT_TRUE(ZeroDigest(request.context_fingerprint));
    EXPECT_TRUE(ZeroDigest(receipt.turn_fingerprint));
}

}  // namespace
