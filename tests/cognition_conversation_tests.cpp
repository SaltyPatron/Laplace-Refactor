#include "laplace/cognition_conversation.h"

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

laplace_cognition_turn_input Turn(
    const laplace_id128& observation,
    const std::uint64_t ordinal,
    const std::uint32_t flags) {
    laplace_cognition_turn_input turn{};
    turn.discourse_id = Digest(10U);
    turn.observation_entity_id = observation;
    turn.observation_occurrence_id = Digest(
        static_cast<std::uint8_t>(20U + ordinal));
    turn.world_id = Digest(30U);
    turn.time_fingerprint = Digest(
        static_cast<std::uint8_t>(40U + ordinal));
    turn.context_fingerprint = Digest(
        static_cast<std::uint8_t>(50U + ordinal));
    turn.turn_ordinal = ordinal;
    turn.flags = flags;
    turn.version = LAPLACE_COGNITION_TURN_VERSION;
    return turn;
}

laplace_cognition_turn_policy CognitionPolicy() {
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

laplace_cognition_conversation_request ConversationRequest(
    const laplace_id128& observation,
    const std::uint64_t ordinal,
    const std::uint32_t turn_flags) {
    laplace_cognition_conversation_request request{};
    request.turn = Turn(observation, ordinal, turn_flags);
    request.cognition_policy = CognitionPolicy();
    request.realization_policy.modality_id = Codepoint(0x54U);
    request.realization_policy.language_id = Codepoint(0x4AU);
    request.realization_policy.realization_recipe_epoch = Digest(70U);
    request.realization_policy.maximum_candidates = 2U;
    request.realization_policy.flags =
        LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT;
    request.realization_policy.version =
        LAPLACE_COGNITION_CONVERSATION_REALIZATION_POLICY_VERSION;
    request.materialization.maximum_nodes = 8U;
    request.materialization.maximum_trajectory_carriers = 8U;
    request.materialization.maximum_output_bytes = 64U;
    request.materialization.maximum_depth = 4U;
    request.materialization.version = LAPLACE_COGNITION_MATERIALIZATION_VERSION;
    request.discourse_roots.active_entity_set_fingerprint = Digest(
        static_cast<std::uint8_t>(80U + ordinal));
    request.discourse_roots.flags =
        LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES;
    request.discourse_roots.version =
        LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_VERSION;
    request.version = LAPLACE_COGNITION_CONVERSATION_VERSION;
    return request;
}

struct CognitionBackend final {
    laplace_id128 source{};
    laplace_id128 target{};
    std::size_t calls{};
};

int EnumerateCandidates(
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
    auto& backend = *static_cast<CognitionBackend*>(opaque);
    ++backend.calls;
    if (binding->relation_mask != LAPLACE_OBSERVATION_QUERY_PREDECESSOR) return 2;
    (void)accumulated_costs;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;

    for (std::size_t source_index = 0U;
         source_index < frontier_state_count; ++source_index) {
        if (!SameId(source_entity_ids[source_index], backend.source)) continue;
        if (*candidate_count >= candidate_capacity) return 3;
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

laplace_cognition_observation_candidate_provider_v1 CognitionProvider(
    CognitionBackend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(91U);
    provider.maximum_candidate_records_per_expansion = 2U;
    provider.enumerate_candidates = EnumerateCandidates;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}

struct RealizationBackend final {
    laplace_id128 content{};
    bool unsupported{};
    std::size_t calls{};
};

int EnumerateRealization(
    void* const opaque,
    const laplace_cognition_semantic_act* const semantic_act,
    const laplace_cognition_realization_request* const request,
    laplace_cognition_realization_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_realization_usage* const usage) {
    if (opaque == nullptr || semantic_act == nullptr || request == nullptr ||
        candidates == nullptr || candidate_capacity == 0U ||
        candidate_count == nullptr || usage == nullptr) {
        return 1;
    }
    auto& backend = *static_cast<RealizationBackend*>(opaque);
    ++backend.calls;
    *candidate_count = 0U;
    *usage = laplace_cognition_realization_usage{};
    usage->provider_receipt_id = Digest(100U);
    if (backend.unsupported) {
        usage->missing_obligation_fingerprint = Digest(101U);
        usage->missing_obligation_count = 1U;
        usage->disposition =
            LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED;
        return 0;
    }

    auto& candidate = candidates[0];
    candidate = laplace_cognition_realization_candidate{};
    candidate.content_id = backend.content;
    candidate.language_id = request->language_id;
    candidate.candidate_receipt_id = Digest(102U);
    candidate.realization_recipe_id = Digest(103U);
    candidate.obligation_fingerprint = Digest(104U);
    candidate.preference_rank = 1U;
    candidate.reused_subtree_count = 1U;
    candidate.structural_tier = 1U;
    candidate.match_class =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
    candidate.flags =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    *candidate_count = 1U;
    usage->rows_examined = 1U;
    usage->exact_whole_examined = 1U;
    usage->disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    (void)semantic_act;
    return 0;
}

laplace_cognition_realization_provider_v1 RealizationProvider(
    RealizationBackend* const backend) {
    laplace_cognition_realization_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(105U);
    provider.maximum_candidate_records = 2U;
    provider.enumerate = EnumerateRealization;
    provider.abi_major = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

struct MaterializationBackend final {
    laplace_cognition_materialization_node node{};
    std::size_t resolve_calls{};
    std::size_t trajectory_calls{};
};

MaterializationBackend AtomBackend(
    const std::uint32_t atom,
    const std::uint8_t seed) {
    MaterializationBackend backend{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(
            atom, &backend.node.entity_id, &backend.node.identity_witness),
        LAPLACE_IDENTITY_OK);
    backend.node.node_receipt_id = Digest(seed);
    backend.node.logical_count = 1U;
    backend.node.atom = atom;
    backend.node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
    return backend;
}

int ResolveNode(
    void* const opaque,
    const laplace_id128* const entity_id,
    laplace_cognition_materialization_node* const node) {
    if (opaque == nullptr || entity_id == nullptr || node == nullptr) return 1;
    auto& backend = *static_cast<MaterializationBackend*>(opaque);
    ++backend.resolve_calls;
    if (!SameId(*entity_id, backend.node.entity_id)) return 2;
    *node = backend.node;
    return 0;
}

int ReadTrajectory(
    void* const opaque,
    const laplace_cognition_materialization_node* const node,
    laplace_trajectory_carrier* const carriers,
    const std::size_t carrier_count,
    laplace_digest256* const receipt) {
    if (opaque == nullptr) return 1;
    auto& backend = *static_cast<MaterializationBackend*>(opaque);
    ++backend.trajectory_calls;
    (void)node;
    (void)carriers;
    (void)carrier_count;
    (void)receipt;
    return 2;
}

laplace_cognition_materialization_provider_v1 MaterializationProvider(
    MaterializationBackend* const backend) {
    laplace_cognition_materialization_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(120U);
    provider.resolve_node = ResolveNode;
    provider.read_trajectory = ReadTrajectory;
    provider.abi_major = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

TEST(CognitionConversation, ComposesAdmittedTurnThroughExactUnicodeAndDurableNextState) {
    CognitionBackend cognition{};
    cognition.source = Codepoint(0x42U);
    cognition.target = Codepoint(0x41U);
    RealizationBackend realization{cognition.target, false, 0U};
    auto materialization = AtomBackend(0x41U, 121U);
    auto cognition_provider = CognitionProvider(&cognition);
    auto realization_provider = RealizationProvider(&realization);
    auto materialization_provider = MaterializationProvider(&materialization);
    auto request = ConversationRequest(cognition.source, 0U, 0U);

    std::array<std::uint8_t, 16> output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    std::size_t output_bytes = 0U;
    std::size_t frame_bytes = 0U;
    laplace_cognition_conversation_result result{};

    ASSERT_EQ(
        laplace_cognition_conversation_execute(
            &request, nullptr, 0U, &cognition_provider, &realization_provider,
            &materialization_provider, output.data(), output.size(), &output_bytes,
            frame.data(), frame.size(), &frame_bytes, &result),
        LAPLACE_COGNITION_CONVERSATION_OK);
    ASSERT_EQ(output_bytes, 1U);
    EXPECT_EQ(output[0], static_cast<std::uint8_t>('A'));
    EXPECT_EQ(frame_bytes, frame.size());
    EXPECT_EQ(cognition.calls, 1U);
    EXPECT_EQ(realization.calls, 1U);
    EXPECT_EQ(materialization.resolve_calls, 1U);
    EXPECT_EQ(materialization.trajectory_calls, 0U);
    EXPECT_TRUE(SameId(result.semantic_act.primary_answer.entity_id, cognition.target));
    EXPECT_TRUE(SameId(result.realization.content_id, cognition.target));
    EXPECT_TRUE(SameDigest(
        result.realization_request.evidence_epoch,
        request.cognition_policy.evidence_epoch));
    EXPECT_TRUE(SameDigest(
        result.realization_request.context_fingerprint,
        result.cognition_request.context_fingerprint));
    EXPECT_TRUE(SameDigest(
        result.discourse_state.context_fingerprint,
        result.cognition_request.context_fingerprint));
    EXPECT_FALSE(ZeroDigest(result.conversation_id));

    laplace_cognition_discourse_state decoded{};
    laplace_cognition_discourse_frame_receipt decoded_receipt{};
    ASSERT_EQ(
        laplace_cognition_discourse_frame_decode(
            frame.data(), frame_bytes, &decoded, &decoded_receipt),
        LAPLACE_COGNITION_DISCOURSE_FRAME_OK);
    EXPECT_TRUE(SameDigest(decoded.state_id, result.discourse_state.state_id));
    EXPECT_TRUE(SameDigest(decoded.semantic_act_id, result.semantic_act.act_id));
    EXPECT_EQ(decoded.turn_ordinal, 0U);
}

TEST(CognitionConversation, SuccessorConsumesExactPriorFrameWithoutTransportState) {
    CognitionBackend cognition{};
    cognition.source = Codepoint(0x42U);
    cognition.target = Codepoint(0x41U);
    RealizationBackend realization{cognition.target, false, 0U};
    auto materialization = AtomBackend(0x41U, 121U);
    auto cognition_provider = CognitionProvider(&cognition);
    auto realization_provider = RealizationProvider(&realization);
    auto materialization_provider = MaterializationProvider(&materialization);

    auto first_request = ConversationRequest(cognition.source, 0U, 0U);
    std::array<std::uint8_t, 16> first_output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> first_frame{};
    std::size_t first_output_bytes = 0U;
    std::size_t first_frame_bytes = 0U;
    laplace_cognition_conversation_result first{};
    ASSERT_EQ(
        laplace_cognition_conversation_execute(
            &first_request, nullptr, 0U, &cognition_provider,
            &realization_provider, &materialization_provider,
            first_output.data(), first_output.size(), &first_output_bytes,
            first_frame.data(), first_frame.size(), &first_frame_bytes, &first),
        LAPLACE_COGNITION_CONVERSATION_OK);

    cognition.source = Codepoint(0x43U);
    cognition.target = Codepoint(0x44U);
    realization.content = cognition.target;
    materialization = AtomBackend(0x44U, 122U);
    auto second_request = ConversationRequest(
        cognition.source, 1U, LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE);
    std::array<std::uint8_t, 16> second_output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> second_frame{};
    std::size_t second_output_bytes = 0U;
    std::size_t second_frame_bytes = 0U;
    laplace_cognition_conversation_result second{};

    ASSERT_EQ(
        laplace_cognition_conversation_execute(
            &second_request, first_frame.data(), first_frame_bytes,
            &cognition_provider, &realization_provider, &materialization_provider,
            second_output.data(), second_output.size(), &second_output_bytes,
            second_frame.data(), second_frame.size(), &second_frame_bytes, &second),
        LAPLACE_COGNITION_CONVERSATION_OK);
    ASSERT_EQ(second_output_bytes, 1U);
    EXPECT_EQ(second_output[0], static_cast<std::uint8_t>('D'));
    EXPECT_TRUE(SameDigest(
        second.turn_receipt.previous_state_id, first.discourse_state.state_id));
    EXPECT_TRUE(SameDigest(
        second.discourse_state.previous_state_id, first.discourse_state.state_id));
    EXPECT_EQ(second.discourse_state.turn_ordinal, 1U);
    EXPECT_NE(
        std::memcmp(first_frame.data(), second_frame.data(), first_frame.size()), 0);
}

TEST(CognitionConversation, FailedRealizationPublishesNeitherBytesNorNextState) {
    CognitionBackend cognition{};
    cognition.source = Codepoint(0x42U);
    cognition.target = Codepoint(0x41U);
    RealizationBackend realization{cognition.target, true, 0U};
    auto materialization = AtomBackend(0x41U, 121U);
    auto cognition_provider = CognitionProvider(&cognition);
    auto realization_provider = RealizationProvider(&realization);
    auto materialization_provider = MaterializationProvider(&materialization);
    auto request = ConversationRequest(cognition.source, 0U, 0U);

    std::array<std::uint8_t, 16> output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    output.fill(0xA5U);
    frame.fill(0x5AU);
    const auto original_output = output;
    const auto original_frame = frame;
    std::size_t output_bytes = 99U;
    std::size_t frame_bytes = 99U;
    laplace_cognition_conversation_result result{};

    EXPECT_EQ(
        laplace_cognition_conversation_execute(
            &request, nullptr, 0U, &cognition_provider, &realization_provider,
            &materialization_provider, output.data(), output.size(), &output_bytes,
            frame.data(), frame.size(), &frame_bytes, &result),
        LAPLACE_COGNITION_CONVERSATION_REALIZATION_FAILURE);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_EQ(frame_bytes, 0U);
    EXPECT_EQ(output, original_output);
    EXPECT_EQ(frame, original_frame);
    EXPECT_EQ(materialization.resolve_calls, 0U);
    EXPECT_TRUE(ZeroDigest(result.conversation_id));

    laplace_cognition_conversation_diagnostics diagnostics{};
    EXPECT_EQ(laplace_cognition_conversation_execute_with_diagnostics(
        &request, nullptr, 0U, &cognition_provider, &realization_provider,
        &materialization_provider, output.data(), output.size(), &output_bytes,
        frame.data(), frame.size(), &frame_bytes, &result, &diagnostics),
        LAPLACE_COGNITION_CONVERSATION_REALIZATION_FAILURE);
    EXPECT_EQ(diagnostics.version, LAPLACE_COGNITION_CONVERSATION_VERSION);
    EXPECT_EQ(diagnostics.conversation_status, LAPLACE_COGNITION_CONVERSATION_REALIZATION_FAILURE);
    EXPECT_EQ(diagnostics.native_status, LAPLACE_COGNITION_REALIZATION_UNSUPPORTED);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_EQ(frame_bytes, 0U);
    EXPECT_EQ(output, original_output);
    EXPECT_EQ(frame, original_frame);
}

}  // namespace
