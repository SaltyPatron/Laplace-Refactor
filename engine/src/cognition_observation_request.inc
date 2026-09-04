#include "laplace/cognition_observation_request.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace laplace_cognition_observation_request_detail {

constexpr char RequestDomain[] =
    "laplace-cognition-observation-request-v1";
constexpr char GoalDomain[] =
    "laplace-cognition-observation-request-goal-v1";
constexpr char ScopeDomain[] =
    "laplace-cognition-observation-request-scope-v1";
constexpr char GuidanceProgramDomain[] =
    "laplace-cognition-observation-request-guidance-program-v1";
constexpr char ObligationDomain[] =
    "laplace-cognition-observation-request-obligation-v1";
constexpr char SearchProgramDomain[] =
    "laplace-cognition-observation-request-search-program-v1";
constexpr char SearchStateDomain[] =
    "laplace-cognition-observation-request-search-state-v1";
constexpr char SearchDominanceDomain[] =
    "laplace-cognition-observation-request-search-dominance-v1";
constexpr char ForwardProgramDomain[] =
    "laplace-cognition-observation-request-forward-program-v1";

bool BytesZero(const std::uint8_t* const bytes, const std::size_t count) {
    std::uint8_t aggregate = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        aggregate = static_cast<std::uint8_t>(aggregate | bytes[index]);
    }
    return aggregate == 0U;
}

bool DigestZero(const laplace_digest256& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

void StartDomain(
    blake3_hasher* const hasher,
    const char* const domain,
    const std::size_t byte_count) {
    blake3_hasher_init(hasher);
    blake3_hasher_update(hasher, domain, byte_count);
}

template <std::size_t Count>
void StartDomain(blake3_hasher* const hasher, const char (&domain)[Count]) {
    StartDomain(hasher, domain, Count - 1U);
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

void HashSearchBudget(
    blake3_hasher* const hasher,
    const laplace_query_search_budget& budget) {
    HashU64(hasher, budget.max_expanded_states);
    HashU64(hasher, budget.max_transition_records);
    HashU64(hasher, budget.max_emitted_states);
    HashU64(hasher, budget.max_frontier_states);
    HashU64(hasher, budget.max_memory_bytes);
    HashU64(hasher, budget.max_io_operations);
    HashU64(hasher, budget.max_database_operations);
    HashU64(hasher, budget.max_provider_calls);
    HashU32(hasher, budget.max_depth);
    HashU32(hasher, budget.requested_path_count);
    HashU32(hasher, budget.frontier_batch_width);
    HashU32(hasher, budget.transition_batch_capacity);
}

void HashForwardLimits(
    blake3_hasher* const hasher,
    const laplace_cognition_observation_forward_limits& limits) {
    HashU64(hasher, limits.max_layers);
    HashU64(hasher, limits.max_provider_calls);
    HashU64(hasher, limits.max_projected_queries);
    HashU64(hasher, limits.max_candidate_operations);
    HashU64(hasher, limits.max_resolutions);
    HashU64(hasher, limits.max_resource_cost);
    HashU64(hasher, limits.max_io_operations);
    HashU64(hasher, limits.max_database_operations);
    HashU32(hasher, limits.candidate_operation_capacity);
    HashU32(hasher, limits.resolution_capacity);
}

laplace_cognition_observation_request_status Validate(
    const laplace_cognition_observation_request& request) {
    if (request.version != LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    if ((request.flags & ~LAPLACE_COGNITION_OBSERVATION_REQUEST_KNOWN_FLAGS) != 0U) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_FLAGS;
    }
    if ((request.flags &
            (LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT |
             LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS)) == 0U) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_FLAGS;
    }
    if ((request.flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT) == 0U &&
        !BytesZero(request.goal_entity_id.bytes, sizeof(request.goal_entity_id.bytes))) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_FLAGS;
    }
    if (request.relation_mask == 0U ||
        (request.relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) != 0U ||
        request.maximum_results == 0U) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_RELATION;
    }
    if (DigestZero(request.world_id) ||
        DigestZero(request.time_fingerprint) ||
        DigestZero(request.context_fingerprint) ||
        DigestZero(request.evidence_boundary) ||
        DigestZero(request.evidence_epoch) ||
        DigestZero(request.authority_id) ||
        DigestZero(request.result_contract_fingerprint)) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }

    const auto& search = request.search_budget;
    if (search.max_expanded_states == 0U ||
        search.max_transition_records == 0U ||
        search.max_emitted_states == 0U ||
        search.max_frontier_states == 0U ||
        search.max_memory_bytes == 0U ||
        search.max_provider_calls == 0U ||
        search.max_depth == 0U ||
        search.requested_path_count == 0U ||
        search.frontier_batch_width < 2U ||
        search.transition_batch_capacity == 0U ||
        search.requested_path_count != request.maximum_results) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_LIMITS;
    }

    const auto& forward = request.forward_limits;
    if (forward.max_layers == 0U ||
        forward.max_provider_calls == 0U ||
        forward.max_projected_queries == 0U ||
        forward.max_candidate_operations == 0U ||
        forward.max_resolutions == 0U ||
        forward.max_resource_cost == 0U ||
        forward.max_io_operations == 0U ||
        forward.max_database_operations == 0U ||
        forward.candidate_operation_capacity == 0U ||
        forward.resolution_capacity == 0U ||
        static_cast<std::uint64_t>(forward.candidate_operation_capacity) >
            forward.max_candidate_operations ||
        static_cast<std::uint64_t>(forward.resolution_capacity) >
            forward.max_resolutions) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_LIMITS;
    }
    return LAPLACE_COGNITION_OBSERVATION_REQUEST_OK;
}

void RequestFingerprint(
    const laplace_cognition_observation_request& request,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    StartDomain(&hasher, RequestDomain);
    HashId(&hasher, request.anchor_entity_id);
    if ((request.flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT) != 0U) {
        HashId(&hasher, request.goal_entity_id);
    } else {
        const laplace_id128 no_goal{};
        HashId(&hasher, no_goal);
    }
    HashDigest(&hasher, request.world_id);
    HashDigest(&hasher, request.time_fingerprint);
    HashDigest(&hasher, request.context_fingerprint);
    HashDigest(&hasher, request.evidence_boundary);
    HashDigest(&hasher, request.evidence_epoch);
    HashDigest(&hasher, request.authority_id);
    HashDigest(&hasher, request.result_contract_fingerprint);
    HashSearchBudget(&hasher, request.search_budget);
    HashForwardLimits(&hasher, request.forward_limits);
    HashU32(&hasher, request.relation_mask);
    HashU32(&hasher, request.maximum_results);
    HashU32(&hasher, request.flags);
    HashU32(&hasher, request.version);
    Finish(&hasher, output);
}

void DerivedDigest(
    const char* const domain,
    const std::size_t domain_bytes,
    const laplace_digest256& request_fingerprint,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    StartDomain(&hasher, domain, domain_bytes);
    HashDigest(&hasher, request_fingerprint);
    Finish(&hasher, output);
}

template <std::size_t Count>
void DerivedDigest(
    const char (&domain)[Count],
    const laplace_digest256& request_fingerprint,
    laplace_digest256* const output) {
    DerivedDigest(domain, Count - 1U, request_fingerprint, output);
}

void GoalDigest(
    const laplace_cognition_observation_request& request,
    const laplace_digest256& request_fingerprint,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    StartDomain(&hasher, GoalDomain);
    HashDigest(&hasher, request_fingerprint);
    HashId(&hasher, request.anchor_entity_id);
    if ((request.flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT) != 0U) {
        HashId(&hasher, request.goal_entity_id);
    }
    HashU32(&hasher, request.relation_mask);
    HashU32(&hasher, request.maximum_results);
    Finish(&hasher, output);
}

void ScopeDigest(
    const laplace_cognition_observation_request& request,
    const laplace_digest256& request_fingerprint,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    StartDomain(&hasher, ScopeDomain);
    HashDigest(&hasher, request_fingerprint);
    HashDigest(&hasher, request.world_id);
    HashDigest(&hasher, request.evidence_boundary);
    HashU32(&hasher, request.relation_mask);
    Finish(&hasher, output);
}

}  // namespace laplace_cognition_observation_request_detail

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_request_identify(
    const laplace_cognition_observation_request* const request,
    laplace_digest256* const request_fingerprint) {
    using namespace laplace_cognition_observation_request_detail;
    if (request == nullptr || request_fingerprint == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    const auto status = Validate(*request);
    if (status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        return status;
    }
    RequestFingerprint(*request, request_fingerprint);
    return LAPLACE_COGNITION_OBSERVATION_REQUEST_OK;
}

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_request_compile(
    const laplace_cognition_observation_request* const request,
    laplace_cognition_observation_compiled_request* const compiled) {
    using namespace laplace_cognition_observation_request_detail;
    if (request == nullptr || compiled == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    *compiled = laplace_cognition_observation_compiled_request{};

    const auto validation = Validate(*request);
    if (validation != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        compiled->status = static_cast<std::uint32_t>(validation);
        return validation;
    }
    RequestFingerprint(*request, &compiled->request_fingerprint);

    compiled->binding.anchor_entity_id = request->anchor_entity_id;
    compiled->binding.relation_mask = request->relation_mask;
    compiled->binding.maximum_results = request->maximum_results;
    if ((request->flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT) != 0U) {
        compiled->binding.goal_entity_id = request->goal_entity_id;
        compiled->binding.flags |= LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT;
    }
    if ((request->flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS) != 0U) {
        compiled->binding.flags |= LAPLACE_OBSERVATION_QUERY_BINDING_TERMINAL_RESULTS;
    }
    if (laplace_observation_query_binding_identify(
            &compiled->binding, &compiled->binding.binding_fingerprint) !=
        LAPLACE_OBSERVATION_QUERY_OK) {
        compiled->status = LAPLACE_COGNITION_OBSERVATION_REQUEST_BINDING_FAILURE;
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_BINDING_FAILURE;
    }

    laplace_digest256 goal_id{};
    GoalDigest(*request, compiled->request_fingerprint, &goal_id);
    laplace_digest256 scope_fingerprint{};
    ScopeDigest(*request, compiled->request_fingerprint, &scope_fingerprint);
    laplace_digest256 obligation_id{};
    DerivedDigest(ObligationDomain, compiled->request_fingerprint, &obligation_id);

    laplace_cognition_guidance_header header{};
    DerivedDigest(
        GuidanceProgramDomain, compiled->request_fingerprint, &header.program_id);
    header.goal_id = goal_id;
    header.bindings_fingerprint = compiled->binding.binding_fingerprint;
    header.scope_fingerprint = scope_fingerprint;
    header.world_id = request->world_id;
    header.time_fingerprint = request->time_fingerprint;
    header.context_fingerprint = request->context_fingerprint;
    header.evidence_epoch = request->evidence_epoch;
    header.authority_id = request->authority_id;
    header.result_contract_fingerprint = request->result_contract_fingerprint;
    header.version = LAPLACE_COGNITION_GUIDANCE_VERSION;

    laplace_cognition_obligation obligation{};
    obligation.obligation_id = obligation_id;
    obligation.binding_fingerprint = compiled->binding.binding_fingerprint;
    obligation.world_id = request->world_id;
    obligation.time_fingerprint = request->time_fingerprint;
    obligation.context_fingerprint = request->context_fingerprint;
    obligation.evidence_boundary = request->evidence_boundary;
    obligation.authority_id = request->authority_id;
    obligation.result_contract_fingerprint = request->result_contract_fingerprint;
    obligation.kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    obligation.disposition = LAPLACE_COGNITION_OBLIGATION_OPEN;
    obligation.flags = LAPLACE_COGNITION_OBLIGATION_REQUIRED;
    if ((request->flags &
            LAPLACE_COGNITION_OBSERVATION_REQUEST_ALLOW_TYPED_UNRESOLVED) != 0U) {
        obligation.flags |= LAPLACE_COGNITION_OBLIGATION_ALLOW_TYPED_UNRESOLVED;
    }
    if (laplace_cognition_guidance_state_create(
            &header, &obligation, 1U, &compiled->guidance_state) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        compiled->status = LAPLACE_COGNITION_OBSERVATION_REQUEST_GUIDANCE_FAILURE;
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_GUIDANCE_FAILURE;
    }

    if (laplace_observation_query_entity_coordinate(
            &request->anchor_entity_id, &compiled->initial_search_state.anchor_id) !=
        LAPLACE_OBSERVATION_QUERY_OK) {
        laplace_cognition_guidance_state_destroy(&compiled->guidance_state);
        compiled->status = LAPLACE_COGNITION_OBSERVATION_REQUEST_COORDINATE_FAILURE;
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_COORDINATE_FAILURE;
    }
    compiled->initial_search_state.binding_fingerprint =
        compiled->binding.binding_fingerprint;
    compiled->initial_search_state.obligation_fingerprint = obligation_id;
    compiled->initial_search_state.context_fingerprint = request->context_fingerprint;
    compiled->initial_search_state.evidence_epoch = request->evidence_epoch;
    compiled->initial_search_state.boundary_id = request->evidence_boundary;

    {
        blake3_hasher hasher{};
        StartDomain(&hasher, SearchDominanceDomain);
        HashDigest(&hasher, compiled->initial_search_state.anchor_id);
        HashDigest(&hasher, compiled->binding.binding_fingerprint);
        HashDigest(&hasher, request->context_fingerprint);
        HashDigest(&hasher, request->evidence_epoch);
        HashDigest(&hasher, request->evidence_boundary);
        Finish(&hasher, &compiled->initial_search_state.dominance_key);
    }
    {
        blake3_hasher hasher{};
        StartDomain(&hasher, SearchStateDomain);
        HashDigest(&hasher, compiled->request_fingerprint);
        HashDigest(&hasher, compiled->initial_search_state.anchor_id);
        HashDigest(&hasher, compiled->initial_search_state.dominance_key);
        HashDigest(&hasher, compiled->initial_search_state.binding_fingerprint);
        HashDigest(&hasher, compiled->initial_search_state.obligation_fingerprint);
        HashDigest(&hasher, compiled->initial_search_state.context_fingerprint);
        HashDigest(&hasher, compiled->initial_search_state.evidence_epoch);
        HashDigest(&hasher, compiled->initial_search_state.boundary_id);
        Finish(&hasher, &compiled->initial_search_state.state_id);
    }

    DerivedDigest(
        SearchProgramDomain, compiled->request_fingerprint,
        &compiled->search_program.program_id);
    compiled->search_program.goal_id = goal_id;
    compiled->search_program.boundary_id = request->evidence_boundary;
    compiled->search_program.evidence_epoch = request->evidence_epoch;
    compiled->search_program.result_contract_fingerprint =
        request->result_contract_fingerprint;
    compiled->search_program.cost_weights[0] = 1U;
    compiled->search_program.budget = request->search_budget;
    compiled->search_program.flags = LAPLACE_QUERY_SEARCH_FLAG_REQUIRE_SET_ORIENTED;
    if ((request->flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE) != 0U) {
        compiled->search_program.flags |= LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE;
    }

    DerivedDigest(
        ForwardProgramDomain, compiled->request_fingerprint,
        &compiled->forward_program.program_id);
    compiled->forward_program.result_contract_fingerprint =
        request->result_contract_fingerprint;
    compiled->forward_program.max_layers = request->forward_limits.max_layers;
    compiled->forward_program.max_provider_calls =
        request->forward_limits.max_provider_calls;
    compiled->forward_program.max_projected_queries =
        request->forward_limits.max_projected_queries;
    compiled->forward_program.max_candidate_operations =
        request->forward_limits.max_candidate_operations;
    compiled->forward_program.max_resolutions =
        request->forward_limits.max_resolutions;
    compiled->forward_program.max_resource_cost =
        request->forward_limits.max_resource_cost;
    compiled->forward_program.max_io_operations =
        request->forward_limits.max_io_operations;
    compiled->forward_program.max_database_operations =
        request->forward_limits.max_database_operations;
    compiled->forward_program.candidate_operation_capacity =
        request->forward_limits.candidate_operation_capacity;
    compiled->forward_program.resolution_capacity =
        request->forward_limits.resolution_capacity;
    compiled->forward_program.flags =
        LAPLACE_COGNITION_FORWARD_REQUIRE_STATE_PROGRESS |
        LAPLACE_COGNITION_FORWARD_REQUIRE_PROJECTED_QUERY_CONSUMPTION |
        LAPLACE_COGNITION_FORWARD_REQUIRE_RECEIPTED_EXECUTION;
    compiled->forward_program.version = LAPLACE_COGNITION_FORWARD_VERSION;

    compiled->status = LAPLACE_COGNITION_OBSERVATION_REQUEST_OK;
    return LAPLACE_COGNITION_OBSERVATION_REQUEST_OK;
}

extern "C" void
laplace_cognition_observation_compiled_request_destroy(
    laplace_cognition_observation_compiled_request* const compiled) {
    if (compiled == nullptr) return;
    laplace_cognition_guidance_state_destroy(&compiled->guidance_state);
    *compiled = laplace_cognition_observation_compiled_request{};
}
