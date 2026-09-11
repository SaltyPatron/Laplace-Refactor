#include "laplace/cognition_observation_request.h"

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

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool Zero(const laplace_digest256& value) {
    return std::all_of(
        std::begin(value.bytes), std::end(value.bytes),
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

laplace_cognition_observation_request Request(
    const laplace_id128& anchor,
    const laplace_id128* const goal,
    const bool terminal_results,
    const std::uint32_t max_depth) {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = anchor;
    request.world_id = Digest(60U);
    request.time_fingerprint = Digest(61U);
    request.context_fingerprint = Digest(62U);
    request.evidence_boundary = Digest(63U);
    request.evidence_epoch = Digest(64U);
    request.authority_id = Digest(65U);
    request.result_contract_fingerprint = Digest(66U);
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    request.maximum_results = 1U;
    request.flags = LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    if (terminal_results) {
        request.flags |= LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    }
    if (goal != nullptr) {
        request.goal_entity_id = *goal;
        request.flags |= LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT;
    }
    request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    request.search_budget.max_expanded_states = 8U;
    request.search_budget.max_transition_records = 16U;
    request.search_budget.max_emitted_states = 24U;
    request.search_budget.max_frontier_states = 16U;
    request.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.search_budget.max_io_operations = 16U;
    request.search_budget.max_database_operations = 16U;
    request.search_budget.max_provider_calls = 8U;
    request.search_budget.max_depth = max_depth;
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

struct CandidateBackend final {
    laplace_id128 a{};
    laplace_id128 b{};
    laplace_id128 c{};
    bool two_hop{};
    std::size_t calls{};
    bool saw_nonzero_native_state{};
    bool saw_expected_binding{};
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
    auto& backend = *static_cast<CandidateBackend*>(opaque);
    ++backend.calls;
    backend.saw_expected_binding = backend.saw_expected_binding ||
        binding->relation_mask == LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    backend.saw_nonzero_native_state = backend.saw_nonzero_native_state ||
        !Zero(frontier_states[0].state_id);
    (void)accumulated_costs;

    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;

    for (std::size_t source_index = 0U;
         source_index < frontier_state_count; ++source_index) {
        laplace_id128 target{};
        bool emit = false;
        if (backend.two_hop && SameId(source_entity_ids[source_index], backend.c)) {
            target = backend.b;
            emit = true;
        } else if (SameId(source_entity_ids[source_index], backend.b)) {
            target = backend.a;
            emit = true;
        }
        if (!emit) continue;
        if (*candidate_count >= candidate_capacity) return 2;

        auto& candidate = candidates[*candidate_count];
        candidate = laplace_cognition_observation_candidate{};
        candidate.target_entity_id = target;
        candidate.observation_fingerprint = Digest(static_cast<std::uint8_t>(
            120U + static_cast<unsigned int>(source_index) +
            static_cast<unsigned int>(frontier_states[source_index].depth)));
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

laplace_cognition_observation_candidate_provider_v1 Provider(
    CandidateBackend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(220U);
    provider.maximum_candidate_records_per_expansion = 8U;
    provider.enumerate_candidates = EnumerateCandidates;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}

struct PlaneBackend final {
    laplace_id128 source{};
    laplace_id128 target{};
    std::uint32_t source_layer{};
    std::uint8_t provider_seed{};
    std::uint8_t observation_seed{};
    std::uint8_t evidence_seed{};
    std::size_t calls{};
};

int EnumeratePlaneCandidates(
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
    auto& backend = *static_cast<PlaneBackend*>(opaque);
    ++backend.calls;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;
    (void)binding;
    (void)accumulated_costs;

    for (std::size_t source_index = 0U;
         source_index < frontier_state_count; ++source_index) {
        if (!SameId(source_entity_ids[source_index], backend.source)) continue;
        if (*candidate_count >= candidate_capacity) return 2;
        auto& candidate = candidates[*candidate_count];
        candidate = laplace_cognition_observation_candidate{};
        candidate.target_entity_id = backend.target;
        candidate.observation_fingerprint = Digest(backend.observation_seed);
        if (backend.evidence_seed != 0U) {
            candidate.evidence_root_fingerprint = Digest(backend.evidence_seed);
        }
        candidate.source_state_index = static_cast<std::uint64_t>(source_index);
        candidate.source_logical_ordinal = frontier_states[source_index].depth;
        candidate.target_logical_ordinal =
            static_cast<std::uint64_t>(frontier_states[source_index].depth) + 1U;
        candidate.multiplicity = 1U;
        candidate.gap = 1U;
        candidate.relation_family = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
        candidate.source_layer = backend.source_layer;
        candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE;
        ++*candidate_count;
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

laplace_cognition_observation_candidate_provider_v1 PlaneProvider(
    PlaneBackend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(backend->provider_seed);
    provider.maximum_candidate_records_per_expansion = 8U;
    provider.enumerate_candidates = EnumeratePlaneCandidates;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}

struct SemanticBackend final {
    laplace_id128 source{};
    laplace_id128 target{};
    laplace_id128 relation_id{};
    std::size_t calls{};
};

int EnumerateSemanticCandidate(
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
    auto& backend = *static_cast<SemanticBackend*>(opaque);
    ++backend.calls;
    if (binding->relation_mask != LAPLACE_OBSERVATION_QUERY_SEMANTIC) return 2;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;
    (void)accumulated_costs;
    for (std::size_t source_index = 0U;
         source_index < frontier_state_count; ++source_index) {
        if (!SameId(source_entity_ids[source_index], backend.source)) continue;
        if (*candidate_count >= candidate_capacity) return 3;
        auto& candidate = candidates[*candidate_count];
        candidate = laplace_cognition_observation_candidate{};
        candidate.target_entity_id = backend.target;
        candidate.relation_id = backend.relation_id;
        candidate.observation_fingerprint = Digest(170U);
        candidate.evidence_root_fingerprint = Digest(171U);
        candidate.source_state_index = static_cast<std::uint64_t>(source_index);
        candidate.source_logical_ordinal = frontier_states[source_index].depth;
        candidate.target_logical_ordinal =
            static_cast<std::uint64_t>(frontier_states[source_index].depth) + 1U;
        candidate.multiplicity = 1U;
        candidate.gap = 1U;
        candidate.relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
        candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
        candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
        candidate.flags =
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;
        ++*candidate_count;
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

laplace_cognition_observation_candidate_provider_v1 SemanticProvider(
    SemanticBackend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(223U);
    provider.maximum_candidate_records_per_expansion = 8U;
    provider.enumerate_candidates = EnumerateSemanticCandidate;
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

class ProviderSetHandle final {
public:
    ~ProviderSetHandle() {
        laplace_cognition_observation_candidate_provider_set_destroy(&value);
    }
    laplace_cognition_observation_candidate_provider_set* value{};
};

TEST(CognitionObservationExternalProvider, CandidateBackendCannotOwnSearchOrForwardLifecycle) {
    CandidateBackend backend{};
    backend.a = Codepoint(0x41U);
    backend.b = Codepoint(0x42U);
    const auto request = Request(backend.b, nullptr, true, 1U);
    const auto provider = Provider(&backend);
    ObservationResultHandle observation;
    ForwardResultHandle forward;
    laplace_cognition_forward_receipt receipt{};

    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &observation.value, &forward.value, &receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(observation.value, nullptr);
    ASSERT_NE(forward.value, nullptr);
    EXPECT_EQ(backend.calls, 1U);
    EXPECT_TRUE(backend.saw_expected_binding);
    EXPECT_TRUE(backend.saw_nonzero_native_state);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_EQ(receipt.layer_count, 1U);
    EXPECT_FALSE(Zero(receipt.output_fingerprint));

    ASSERT_EQ(laplace_cognition_observation_result_answer_count(observation.value), 1U);
    laplace_cognition_observation_answer answer{};
    ASSERT_EQ(
        laplace_cognition_observation_result_answer(observation.value, 0U, &answer),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameId(answer.entity_id, backend.a));
    EXPECT_EQ(answer.transition_count, 1U);
    EXPECT_EQ(answer.relation_family, LAPLACE_OBSERVATION_QUERY_PREDECESSOR);
    EXPECT_EQ(answer.source_layer, LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY);
    EXPECT_EQ(answer.direction, LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE);
    EXPECT_EQ(
        answer.flags,
        LAPLACE_COGNITION_OBSERVATION_ANSWER_OPERATOR_EXECUTED);
}

TEST(CognitionObservationExternalProvider, NativeSearchCarriesProviderTargetsAcrossTwoHops) {
    CandidateBackend backend{};
    backend.a = Codepoint(0x41U);
    backend.b = Codepoint(0x42U);
    backend.c = Codepoint(0x43U);
    backend.two_hop = true;
    const auto request = Request(backend.c, &backend.a, false, 2U);
    const auto provider = Provider(&backend);
    ObservationResultHandle observation;
    ForwardResultHandle forward;
    laplace_cognition_forward_receipt receipt{};

    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &observation.value, &forward.value, &receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(observation.value, nullptr);
    ASSERT_NE(forward.value, nullptr);
    EXPECT_EQ(backend.calls, 2U);
    EXPECT_TRUE(backend.saw_nonzero_native_state);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);

    ASSERT_EQ(laplace_cognition_observation_result_answer_count(observation.value), 1U);
    laplace_cognition_observation_answer answer{};
    ASSERT_EQ(
        laplace_cognition_observation_result_answer(observation.value, 0U, &answer),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameId(answer.entity_id, backend.a));
    EXPECT_EQ(answer.transition_count, 2U);
    EXPECT_EQ(answer.relation_family, LAPLACE_OBSERVATION_QUERY_PREDECESSOR);
    EXPECT_EQ(answer.source_layer, LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY);
}

TEST(CognitionObservationExternalProvider, NativeProviderSetCombinesIndependentEvidencePlanes) {
    const auto a = Codepoint(0x41U);
    const auto b = Codepoint(0x42U);
    const auto c = Codepoint(0x43U);
    PlaneBackend physicality{};
    physicality.source = c;
    physicality.target = b;
    physicality.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
    physicality.provider_seed = 221U;
    physicality.observation_seed = 151U;
    PlaneBackend testimony{};
    testimony.source = b;
    testimony.target = a;
    testimony.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
    testimony.provider_seed = 222U;
    testimony.observation_seed = 152U;
    testimony.evidence_seed = 181U;

    std::array<laplace_cognition_observation_candidate_provider_v1, 2> providers{{
        PlaneProvider(&testimony), PlaneProvider(&physicality)}};
    ProviderSetHandle provider_set;
    laplace_cognition_observation_candidate_provider_v1 composite{};
    ASSERT_EQ(
        laplace_cognition_observation_candidate_provider_set_create(
            providers.data(), providers.size(), &provider_set.value, &composite),
        LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK);
    ASSERT_NE(provider_set.value, nullptr);
    EXPECT_FALSE(Zero(composite.provider_fingerprint));
    EXPECT_EQ(composite.maximum_candidate_records_per_expansion, 16U);

    const auto request = Request(c, &a, false, 2U);
    ObservationResultHandle observation;
    ForwardResultHandle forward;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &composite, &observation.value, &forward.value, &receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(observation.value, nullptr);
    ASSERT_NE(forward.value, nullptr);
    EXPECT_EQ(physicality.calls, 2U);
    EXPECT_EQ(testimony.calls, 2U);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);

    ASSERT_EQ(laplace_cognition_observation_result_answer_count(observation.value), 1U);
    laplace_cognition_observation_answer answer{};
    ASSERT_EQ(
        laplace_cognition_observation_result_answer(observation.value, 0U, &answer),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameId(answer.entity_id, a));
    EXPECT_EQ(answer.transition_count, 2U);
    EXPECT_EQ(answer.independent_evidence_root_count, 1U);
    EXPECT_EQ(answer.relation_family, LAPLACE_OBSERVATION_QUERY_PREDECESSOR);
    EXPECT_EQ(answer.source_layer, LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY);
    EXPECT_EQ(answer.direction, LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE);
}

TEST(CognitionObservationExternalProvider, PreservesArbitraryTypedSemanticRelationIdentity) {
    SemanticBackend backend{};
    backend.source = Codepoint(0x41U);
    backend.target = Codepoint(0x42U);
    backend.relation_id = Codepoint(0x52U);
    auto request = Request(backend.source, nullptr, true, 1U);
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    const auto provider = SemanticProvider(&backend);

    ObservationResultHandle first_observation;
    ForwardResultHandle first_forward;
    laplace_cognition_forward_receipt first_receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &first_observation.value, &first_forward.value,
            &first_receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_EQ(
        laplace_cognition_observation_result_answer_count(first_observation.value),
        1U);
    laplace_cognition_observation_answer first{};
    ASSERT_EQ(
        laplace_cognition_observation_result_answer(
            first_observation.value, 0U, &first),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameId(first.entity_id, backend.target));
    EXPECT_TRUE(SameId(first.relation_id, backend.relation_id));
    EXPECT_EQ(first.relation_family, LAPLACE_OBSERVATION_QUERY_SEMANTIC);
    EXPECT_EQ(first.source_layer, LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY);
    EXPECT_EQ(first.direction, LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD);
    EXPECT_EQ(
        first.flags,
        LAPLACE_COGNITION_OBSERVATION_ANSWER_RELATION_ID_PRESENT |
            LAPLACE_COGNITION_OBSERVATION_ANSWER_OPERATOR_EXECUTED);

    backend.relation_id = Codepoint(0x53U);
    ObservationResultHandle second_observation;
    ForwardResultHandle second_forward;
    laplace_cognition_forward_receipt second_receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &second_observation.value, &second_forward.value,
            &second_receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    laplace_cognition_observation_answer second{};
    ASSERT_EQ(
        laplace_cognition_observation_result_answer(
            second_observation.value, 0U, &second),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameId(second.relation_id, backend.relation_id));
    EXPECT_FALSE(SameDigest(first.path_id, second.path_id));
    EXPECT_FALSE(SameDigest(first.terminal_state_id, second.terminal_state_id));
    EXPECT_FALSE(SameDigest(first_receipt.output_fingerprint,
                            second_receipt.output_fingerprint));
}

TEST(CognitionObservationExternalProvider, ProviderSetIdentityIsOrderIndependentAndRejectsDuplicates) {
    PlaneBackend physicality{};
    physicality.source = Codepoint(0x43U);
    physicality.target = Codepoint(0x42U);
    physicality.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
    physicality.provider_seed = 221U;
    physicality.observation_seed = 151U;
    PlaneBackend testimony{};
    testimony.source = Codepoint(0x42U);
    testimony.target = Codepoint(0x41U);
    testimony.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
    testimony.provider_seed = 222U;
    testimony.observation_seed = 152U;
    testimony.evidence_seed = 181U;
    const auto physical_provider = PlaneProvider(&physicality);
    const auto testimony_provider = PlaneProvider(&testimony);

    std::array<laplace_cognition_observation_candidate_provider_v1, 2> forward{{
        physical_provider, testimony_provider}};
    std::array<laplace_cognition_observation_candidate_provider_v1, 2> reverse{{
        testimony_provider, physical_provider}};
    ProviderSetHandle forward_set;
    ProviderSetHandle reverse_set;
    laplace_cognition_observation_candidate_provider_v1 forward_composite{};
    laplace_cognition_observation_candidate_provider_v1 reverse_composite{};
    ASSERT_EQ(
        laplace_cognition_observation_candidate_provider_set_create(
            forward.data(), forward.size(), &forward_set.value, &forward_composite),
        LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK);
    ASSERT_EQ(
        laplace_cognition_observation_candidate_provider_set_create(
            reverse.data(), reverse.size(), &reverse_set.value, &reverse_composite),
        LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK);
    EXPECT_EQ(
        std::memcmp(
            forward_composite.provider_fingerprint.bytes,
            reverse_composite.provider_fingerprint.bytes,
            sizeof(forward_composite.provider_fingerprint.bytes)),
        0);

    std::array<laplace_cognition_observation_candidate_provider_v1, 2> duplicate{{
        physical_provider, physical_provider}};
    ProviderSetHandle duplicate_set;
    laplace_cognition_observation_candidate_provider_v1 duplicate_composite{};
    EXPECT_EQ(
        laplace_cognition_observation_candidate_provider_set_create(
            duplicate.data(), duplicate.size(), &duplicate_set.value,
            &duplicate_composite),
        LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_DUPLICATE_PROVIDER);
    EXPECT_EQ(duplicate_set.value, nullptr);
    EXPECT_TRUE(Zero(duplicate_composite.provider_fingerprint));
}

TEST(CognitionObservationExternalProvider, RejectsCandidateProviderAbiDriftBeforeQueryingBackend) {
    CandidateBackend backend{};
    backend.a = Codepoint(0x41U);
    backend.b = Codepoint(0x42U);
    const auto request = Request(backend.b, nullptr, true, 1U);
    auto provider = Provider(&backend);
    provider.abi_major = static_cast<std::uint16_t>(
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR + 1U);
    ObservationResultHandle observation;
    ForwardResultHandle forward;
    laplace_cognition_forward_receipt receipt{};

    EXPECT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &observation.value, &forward.value, &receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_PROVIDER_FAILURE);
    EXPECT_EQ(observation.value, nullptr);
    EXPECT_EQ(forward.value, nullptr);
    EXPECT_EQ(backend.calls, 0U);
}

}  // namespace
