#include "laplace/cognition_observation_request.h"
#include "laplace/target_observation_compile.h"

#include "laplace/identity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 OperatorViewDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

bool OperatorViewSameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool OperatorViewSameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_id128 OperatorViewCodepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

laplace_cognition_observation_request OperatorViewRequest(const laplace_id128& anchor) {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = anchor;
    request.world_id = OperatorViewDigest(60U);
    request.time_fingerprint = OperatorViewDigest(61U);
    request.context_fingerprint = OperatorViewDigest(62U);
    request.evidence_boundary = OperatorViewDigest(63U);
    request.evidence_epoch = OperatorViewDigest(64U);
    request.authority_id = OperatorViewDigest(65U);
    request.result_contract_fingerprint = OperatorViewDigest(66U);
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
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

struct OperatorViewBackend final {
    laplace_id128 source{};
    laplace_id128 target{};
};

int OperatorViewEnumerate(
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
    auto& backend = *static_cast<OperatorViewBackend*>(opaque);
    if (binding->relation_mask != LAPLACE_OBSERVATION_QUERY_PREDECESSOR) return 2;
    (void)accumulated_costs;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;
    for (std::size_t index = 0U; index < frontier_state_count; ++index) {
        if (!OperatorViewSameId(source_entity_ids[index], backend.source)) continue;
        if (*candidate_count >= candidate_capacity) return 3;
        auto& candidate = candidates[*candidate_count];
        candidate = laplace_cognition_observation_candidate{};
        candidate.target_entity_id = backend.target;
        candidate.observation_fingerprint = OperatorViewDigest(120U);
        candidate.source_state_index = static_cast<std::uint64_t>(index);
        candidate.source_logical_ordinal = frontier_states[index].depth;
        candidate.target_logical_ordinal =
            static_cast<std::uint64_t>(frontier_states[index].depth) + 1U;
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

laplace_cognition_observation_candidate_provider_v1 OperatorViewProvider(
    OperatorViewBackend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = OperatorViewDigest(220U);
    provider.maximum_candidate_records_per_expansion = 8U;
    provider.enumerate_candidates = OperatorViewEnumerate;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}

struct OperatorViewObservationHandle {
    laplace_cognition_observation_result* value{};
    ~OperatorViewObservationHandle() {
        laplace_cognition_observation_result_destroy(&value);
    }
};

struct OperatorViewForwardHandle {
    laplace_cognition_forward_result* value{};
    ~OperatorViewForwardHandle() {
        laplace_cognition_forward_result_destroy(&value);
    }
};

struct OperatorHandle {
    laplace_cognition_operator* value{};
    ~OperatorHandle() { laplace_cognition_operator_destroy(&value); }
};

struct ObservationTargetHandle {
    laplace_target_compile_result* value{};
    ~ObservationTargetHandle() { laplace_target_compile_result_destroy(&value); }
};

TEST(CognitionObservationOperatorView, ReusesExactLiveOperatorEstate) {
    OperatorViewBackend backend{};
    backend.source = OperatorViewCodepoint(0x42U);
    backend.target = OperatorViewCodepoint(0x41U);
    const auto request = OperatorViewRequest(backend.source);
    const auto provider = OperatorViewProvider(&backend);
    OperatorViewObservationHandle observation;
    OperatorViewForwardHandle forward;
    laplace_cognition_forward_receipt forward_receipt{};

    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &observation.value, &forward.value, &forward_receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(observation.value, nullptr);
    ASSERT_EQ(laplace_cognition_observation_result_answer_count(observation.value), 1U);

    laplace_cognition_observation_answer answer{};
    ASSERT_EQ(
        laplace_cognition_observation_result_answer(
            observation.value, 0U, &answer),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(
        answer.flags & LAPLACE_COGNITION_OBSERVATION_ANSWER_OPERATOR_EXECUTED,
        0U);

    laplace_cognition_observation_operator_view view{};
    ASSERT_EQ(
        laplace_cognition_observation_result_operator_view(
            observation.value, 0U, &view),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(OperatorViewSameDigest(view.path_id, answer.path_id));
    ASSERT_NE(view.fields, nullptr);
    ASSERT_NE(view.constraints, nullptr);
    ASSERT_NE(view.program.eligible_relation_families, nullptr);
    ASSERT_EQ(view.field_count, 2U);
    ASSERT_EQ(view.constraint_count, 1U);
    ASSERT_EQ(view.program.eligible_relation_family_count, 1U);
    EXPECT_EQ(
        view.program.eligible_relation_families[0],
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR);
    EXPECT_EQ(
        view.constraints[0].relation_family,
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR);
    EXPECT_EQ(
        view.constraints[0].source_class,
        LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY);
    EXPECT_EQ(
        view.constraints[0].direction,
        LAPLACE_COGNITION_OPERATOR_DIRECTION_TARGET_TO_SOURCE);

    OperatorHandle rebuilt;
    laplace_cognition_operator_receipt rebuilt_receipt{};
    ASSERT_EQ(
        laplace_cognition_operator_create(
            &view.program, view.fields, view.field_count,
            view.constraints, view.constraint_count,
            &rebuilt.value, &rebuilt_receipt),
        LAPLACE_COGNITION_OPERATOR_OK);
    EXPECT_TRUE(
        OperatorViewSameDigest(rebuilt_receipt.receipt_id, answer.operator_receipt_id));

    std::vector<laplace_cognition_operator_constraint> mutated(
        view.constraints, view.constraints + view.constraint_count);
    mutated[0].precision *= 0.5;
    OperatorHandle altered;
    laplace_cognition_operator_receipt altered_receipt{};
    ASSERT_EQ(
        laplace_cognition_operator_create(
            &view.program, view.fields, view.field_count,
            mutated.data(), mutated.size(), &altered.value, &altered_receipt),
        LAPLACE_COGNITION_OPERATOR_OK);
    EXPECT_FALSE(
        OperatorViewSameDigest(altered_receipt.receipt_id, answer.operator_receipt_id));
}

TEST(CognitionObservationOperatorView, TargetCompilationUsesSameLiveMaterializedOperator) {
    OperatorViewBackend backend{};
    backend.source = OperatorViewCodepoint(0x42U);
    backend.target = OperatorViewCodepoint(0x41U);
    const auto request = OperatorViewRequest(backend.source);
    const auto provider = OperatorViewProvider(&backend);
    OperatorViewObservationHandle observation;
    OperatorViewForwardHandle forward;
    laplace_cognition_forward_receipt forward_receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &observation.value, &forward.value, &forward_receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);

    laplace_cognition_observation_operator_view view{};
    ASSERT_EQ(
        laplace_cognition_observation_result_operator_view(
            observation.value, 0U, &view),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);

    const std::uint32_t family = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    laplace_target_scope_slot_spec slot{};
    slot.target_role = LAPLACE_TARGET_ROLE_POSITION;
    slot.layer_index = 0U;
    slot.head_index = 0U;
    slot.expert_index = 0U;
    slot.eligible_relation_families = &family;
    slot.eligible_relation_family_count = 1U;
    slot.eligible_source_mask = 1U;
    slot.head_rank = 0U;

    laplace_target_observation_compile_request target_request{};
    target_request.observation_result = observation.value;
    target_request.answer_index = 0U;
    target_request.recipe_fingerprint = OperatorViewDigest(230U);
    target_request.target_contract_fingerprint = OperatorViewDigest(231U);
    target_request.slots = &slot;
    target_request.slot_count = 1U;
    target_request.target_compile_flags = 0U;
    target_request.version = LAPLACE_TARGET_OBSERVATION_COMPILE_VERSION;

    ObservationTargetHandle target;
    laplace_target_compile_receipt compile_receipt{};
    laplace_target_scope_plan_receipt scope_receipt{};
    ASSERT_EQ(
        laplace_target_observation_compile(
            &target_request, &target.value, &compile_receipt, &scope_receipt),
        LAPLACE_TARGET_SCOPE_PLAN_OK);
    ASSERT_NE(target.value, nullptr);
    EXPECT_EQ(scope_receipt.field_count, view.field_count);
    EXPECT_EQ(scope_receipt.selected_constraint_count, view.constraint_count);
    EXPECT_EQ(compile_receipt.slot_count, 1U);

    laplace_target_compile_slot_receipt target_slot{};
    ASSERT_EQ(
        laplace_target_compile_result_slot_receipt(
            target.value, 0U, &target_slot),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(target_slot.target_role, LAPLACE_TARGET_ROLE_POSITION);
    EXPECT_EQ(target_slot.selected_constraint_count, view.constraint_count);

    OperatorHandle live_operator;
    laplace_cognition_operator_receipt live_receipt{};
    ASSERT_EQ(
        laplace_cognition_operator_create(
            &view.program, view.fields, view.field_count,
            view.constraints, view.constraint_count,
            &live_operator.value, &live_receipt),
        LAPLACE_COGNITION_OPERATOR_OK);

    std::vector<double> live_matrix(view.field_count * view.field_count, 0.0);
    std::vector<double> live_rhs(view.field_count, 0.0);
    laplace_digest256 materialization_receipt{};
    ASSERT_EQ(
        laplace_cognition_operator_materialize_dense(
            live_operator.value,
            live_matrix.data(), live_matrix.size(),
            live_rhs.data(), live_rhs.size(),
            &materialization_receipt),
        LAPLACE_COGNITION_OPERATOR_OK);

    std::vector<double> target_matrix(
        static_cast<std::size_t>(target_slot.matrix_value_count), 0.0);
    std::vector<double> target_rhs(
        static_cast<std::size_t>(target_slot.rhs_value_count), 0.0);
    std::size_t required = 0U;
    ASSERT_EQ(
        laplace_target_compile_result_matrix(
            target.value, 0U, target_matrix.data(), target_matrix.size(), &required),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(required, target_matrix.size());
    ASSERT_EQ(
        laplace_target_compile_result_rhs(
            target.value, 0U, target_rhs.data(), target_rhs.size(), &required),
        LAPLACE_TARGET_COMPILE_OK);
    EXPECT_EQ(required, target_rhs.size());
    EXPECT_EQ(target_matrix, live_matrix);
    EXPECT_EQ(target_rhs, live_rhs);
}

TEST(CognitionObservationOperatorView, RejectsOutOfRangeAnswer) {
    laplace_cognition_observation_operator_view view{};
    EXPECT_EQ(
        laplace_cognition_observation_result_operator_view(nullptr, 0U, &view),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT);
}

}  // namespace