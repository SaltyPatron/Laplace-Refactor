#include "laplace/cognition_runtime.h"

#include <cmath>
#include <set>

struct laplace_cognition_observation_operator_estate_storage {
    laplace_digest256 path_id{};
    laplace_cognition_operator_program program{};
    std::vector<std::uint32_t> families;
    std::vector<laplace_cognition_operator_field> fields;
    std::vector<laplace_cognition_operator_constraint> constraints;
};

struct laplace_cognition_observation_result {
    std::vector<laplace_cognition_observation_answer> answers;
    std::vector<laplace_cognition_observation_operator_estate_storage> operator_estates;
};

namespace laplace_cognition_observation_candidate_provider_detail {

using laplace_observation_query_detail::Add;
using laplace_observation_query_detail::CalculationDomain;
using laplace_observation_query_detail::ContextDomain;
using laplace_observation_query_detail::DigestKey;
using laplace_observation_query_detail::DigestZero;
using laplace_observation_query_detail::DirectionForRelation;
using laplace_observation_query_detail::DominanceDomain;
using laplace_observation_query_detail::EntityCoordinate;
using laplace_observation_query_detail::ExecutionReceiptDomain;
using laplace_observation_query_detail::FindProjection;
using laplace_observation_query_detail::Finish;
using laplace_observation_query_detail::HashDigest;
using laplace_observation_query_detail::HashId;
using laplace_observation_query_detail::HashU32;
using laplace_observation_query_detail::HashU64;
using laplace_observation_query_detail::IdFromKey;
using laplace_observation_query_detail::IdKey;
using laplace_observation_query_detail::Key;
using laplace_observation_query_detail::OperationDomain;
using laplace_observation_query_detail::OperationEffectDomain;
using laplace_observation_query_detail::OperationOperandsDomain;
using laplace_observation_query_detail::OperationPreconditionsDomain;
using laplace_observation_query_detail::OperationReceiptContractDomain;
using laplace_observation_query_detail::ProjectionFingerprint;
using laplace_observation_query_detail::ResolutionDisposition;
using laplace_observation_query_detail::ResolutionReceiptDomain;
using laplace_observation_query_detail::SameDigest;
using laplace_observation_query_detail::SameId;
using laplace_observation_query_detail::SearchResultDomain;
using laplace_observation_query_detail::StartDomain;
using laplace_observation_query_detail::StateDomain;
using laplace_observation_query_detail::LawDomain;
using laplace_observation_query_detail::TransitionDomain;
using laplace_observation_query_detail::EnumerationReceiptDomain;

constexpr char GeneratedOperatorProgramDomain[] =
    "laplace-observation-generated-operator-program-v1";
constexpr char GeneratedOperatorFieldDomain[] =
    "laplace-observation-generated-operator-field-v1";
constexpr char GeneratedOperatorPlaneDomain[] =
    "laplace-observation-generated-operator-plane-v1";
constexpr char GeneratedOperatorUnitsDomain[] =
    "laplace-observation-generated-operator-units-v1";
constexpr char GeneratedOperatorConstraintDomain[] =
    "laplace-observation-generated-operator-constraint-v1";
constexpr char GeneratedSolverProgramDomain[] =
    "laplace-observation-generated-solver-program-v1";
constexpr char GeneratedAnchorBoundaryDomain[] =
    "laplace-observation-anchor-prior-boundary-v1";
constexpr char GeneratedTerminalBoundaryDomain[] =
    "laplace-observation-terminal-goal-boundary-v1";

struct ProviderState final {
    const laplace_cognition_observation_candidate_provider_v1* external{};
    laplace_digest256 request_fingerprint{};
    laplace_digest256 result_contract_fingerprint{};
    laplace_observation_query_binding binding{};
    laplace_query_search_state initial_search_state{};
    laplace_query_search_program search_program{};
    std::map<DigestKey, IdKey> coordinate_to_entity;
    std::vector<laplace_cognition_observation_answer> answers;
    std::vector<laplace_cognition_observation_operator_estate_storage> operator_estates;
};

bool ProviderValid(
    const laplace_cognition_observation_request&,
    const laplace_cognition_observation_candidate_provider_v1& provider) {
    return provider.enumerate_candidates != nullptr &&
        !DigestZero(provider.provider_fingerprint) &&
        provider.maximum_candidate_records_per_expansion != 0U &&
        provider.abi_major ==
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <=
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR &&
        provider.flags == 0U && provider.reserved == 0U;
}

bool RegisterEntity(ProviderState* const state, const laplace_id128& entity_id) {
    laplace_digest256 coordinate{};
    if (EntityCoordinate(entity_id, &coordinate) != LAPLACE_OBSERVATION_QUERY_OK) {
        return false;
    }
    const auto [found, inserted] = state->coordinate_to_entity.emplace(
        Key(coordinate), Key(entity_id));
    return inserted || found->second == Key(entity_id);
}

bool ResolveEntity(
    const ProviderState& state,
    const laplace_digest256& coordinate,
    laplace_id128* const entity_id) {
    const auto found = state.coordinate_to_entity.find(Key(coordinate));
    if (found == state.coordinate_to_entity.end() || entity_id == nullptr) {
        return false;
    }
    *entity_id = IdFromKey(found->second);
    return true;
}

bool IdZero(const laplace_id128& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool DirectionValid(const std::uint32_t direction) {
    return direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD ||
        direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE ||
        direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC;
}

bool CandidateValid(
    const ProviderState& state,
    const laplace_cognition_observation_candidate& candidate,
    const std::size_t source_count) {
    const bool relation_id_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT) != 0U;
    const bool uncertainty_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT) != 0U;
    if (candidate.source_state_index >= static_cast<std::uint64_t>(source_count) ||
        DigestZero(candidate.observation_fingerprint) ||
        candidate.multiplicity == 0U || candidate.relation_family == 0U ||
        (candidate.relation_family & (candidate.relation_family - 1U)) != 0U ||
        (candidate.relation_family & state.binding.relation_mask) == 0U ||
        candidate.source_layer == 0U ||
        (candidate.source_layer & (candidate.source_layer - 1U)) != 0U ||
        (candidate.source_layer & LAPLACE_OBSERVATION_QUERY_SOURCE_LAYER_MASK) == 0U ||
        !DirectionValid(candidate.direction) ||
        (candidate.flags &
         ~LAPLACE_COGNITION_OBSERVATION_CANDIDATE_KNOWN_FLAGS) != 0U ||
        candidate.reserved != 0U ||
        (!relation_id_present && !IdZero(candidate.relation_id)) ||
        (uncertainty_present &&
         (candidate.evidence_uncertainty_denominator == 0U ||
          candidate.evidence_uncertainty_numerator >=
              candidate.evidence_uncertainty_denominator)) ||
        (!uncertainty_present &&
         (candidate.evidence_uncertainty_numerator != 0U ||
          candidate.evidence_uncertainty_denominator != 0U))) {
        return false;
    }

    if (candidate.relation_family == LAPLACE_OBSERVATION_QUERY_SEMANTIC) {
        return relation_id_present;
    }
    const std::uint32_t structural_direction =
        DirectionForRelation(candidate.relation_family);
    return structural_direction != 0U && structural_direction == candidate.direction;
}

bool BuildTransition(
    const ProviderState& state,
    const laplace_query_search_state& source,
    const laplace_id128& source_entity_id,
    const laplace_cognition_observation_candidate& candidate,
    laplace_query_search_transition* const transition) {
    if (transition == nullptr || source.depth == UINT32_MAX) return false;

    laplace_digest256 target_coordinate{};
    if (EntityCoordinate(candidate.target_entity_id, &target_coordinate) !=
        LAPLACE_OBSERVATION_QUERY_OK) {
        return false;
    }

    laplace_query_search_state target{};
    target.anchor_id = target_coordinate;
    target.binding_fingerprint = source.binding_fingerprint;
    target.obligation_fingerprint = source.obligation_fingerprint;
    target.evidence_epoch = source.evidence_epoch;
    target.boundary_id = source.boundary_id;
    target.depth = source.depth + 1U;
    const bool terminal_all =
        (state.binding.flags & LAPLACE_OBSERVATION_QUERY_BINDING_TERMINAL_RESULTS) != 0U;
    const bool terminal_goal =
        (state.binding.flags & LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT) != 0U &&
        SameId(state.binding.goal_entity_id, candidate.target_entity_id);
    if (terminal_all || terminal_goal) {
        target.flags = LAPLACE_QUERY_SEARCH_STATE_TERMINAL;
    }

    blake3_hasher context_hasher{};
    StartDomain(&context_hasher, ContextDomain);
    HashDigest(&context_hasher, source.context_fingerprint);
    HashDigest(&context_hasher, candidate.observation_fingerprint);
    HashId(&context_hasher, candidate.relation_id);
    HashU64(&context_hasher, candidate.source_logical_ordinal);
    HashU64(&context_hasher, candidate.target_logical_ordinal);
    HashU64(&context_hasher, candidate.evidence_uncertainty_numerator);
    HashU64(&context_hasher, candidate.evidence_uncertainty_denominator);
    HashU32(&context_hasher, candidate.relation_family);
    HashU32(&context_hasher, candidate.source_layer);
    HashU32(&context_hasher, candidate.direction);
    HashU32(&context_hasher, candidate.flags);
    Finish(&context_hasher, &target.context_fingerprint);

    blake3_hasher dominance_hasher{};
    StartDomain(&dominance_hasher, DominanceDomain);
    HashDigest(&dominance_hasher, target_coordinate);
    HashDigest(&dominance_hasher, candidate.observation_fingerprint);
    HashId(&dominance_hasher, candidate.relation_id);
    HashU64(&dominance_hasher, candidate.source_logical_ordinal);
    HashU64(&dominance_hasher, candidate.target_logical_ordinal);
    HashU64(&dominance_hasher, candidate.evidence_uncertainty_numerator);
    HashU64(&dominance_hasher, candidate.evidence_uncertainty_denominator);
    HashU32(&dominance_hasher, candidate.relation_family);
    HashU32(&dominance_hasher, candidate.source_layer);
    HashU32(&dominance_hasher, candidate.direction);
    HashU32(&dominance_hasher, candidate.flags);
    Finish(&dominance_hasher, &target.dominance_key);

    blake3_hasher state_hasher{};
    StartDomain(&state_hasher, StateDomain);
    HashDigest(&state_hasher, source.state_id);
    HashDigest(&state_hasher, target.anchor_id);
    HashDigest(&state_hasher, target.dominance_key);
    HashDigest(&state_hasher, target.binding_fingerprint);
    HashDigest(&state_hasher, target.obligation_fingerprint);
    HashDigest(&state_hasher, target.context_fingerprint);
    HashDigest(&state_hasher, target.evidence_epoch);
    HashDigest(&state_hasher, target.boundary_id);
    HashU64(&state_hasher, target.heuristic_cost);
    HashU32(&state_hasher, target.depth);
    HashU32(&state_hasher, target.flags);
    Finish(&state_hasher, &target.state_id);

    *transition = laplace_query_search_transition{};
    transition->source_state_id = source.state_id;
    transition->target = target;
    transition->evidence_root_fingerprint = candidate.evidence_root_fingerprint;
    transition->relation_id = candidate.relation_id;
    transition->relation_family = candidate.relation_family;
    transition->source_layer = candidate.source_layer;
    transition->direction = candidate.direction;
    if ((candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT) != 0U) {
        transition->flags |= LAPLACE_QUERY_SEARCH_TRANSITION_RELATION_ID_PRESENT;
    }
    transition->cost_components[0] = 1U;
    transition->cost_components[1] = candidate.gap;
    transition->cost_components[2] = candidate.evidence_uncertainty_numerator;
    transition->cost_components[3] = candidate.evidence_uncertainty_denominator;

    blake3_hasher law_hasher{};
    StartDomain(&law_hasher, LawDomain);
    HashId(&law_hasher, candidate.relation_id);
    HashU32(&law_hasher, candidate.relation_family);
    HashU32(&law_hasher, candidate.source_layer);
    HashU32(&law_hasher, candidate.direction);
    HashU32(&law_hasher, transition->flags);
    Finish(&law_hasher, &transition->law_fingerprint);

    blake3_hasher calculation_hasher{};
    StartDomain(&calculation_hasher, CalculationDomain);
    HashDigest(&calculation_hasher, state.external->provider_fingerprint);
    HashDigest(&calculation_hasher, state.request_fingerprint);
    HashId(&calculation_hasher, source_entity_id);
    HashId(&calculation_hasher, candidate.target_entity_id);
    HashId(&calculation_hasher, candidate.relation_id);
    HashDigest(&calculation_hasher, candidate.observation_fingerprint);
    HashDigest(&calculation_hasher, candidate.evidence_root_fingerprint);
    HashU64(&calculation_hasher, candidate.source_logical_ordinal);
    HashU64(&calculation_hasher, candidate.target_logical_ordinal);
    HashU64(&calculation_hasher, candidate.multiplicity);
    HashU64(&calculation_hasher, candidate.gap);
    HashU64(&calculation_hasher, candidate.evidence_uncertainty_numerator);
    HashU64(&calculation_hasher, candidate.evidence_uncertainty_denominator);
    HashU32(&calculation_hasher, candidate.relation_family);
    HashU32(&calculation_hasher, candidate.source_layer);
    HashU32(&calculation_hasher, candidate.direction);
    HashU32(&calculation_hasher, candidate.flags);
    Finish(&calculation_hasher, &transition->calculation_receipt);

    blake3_hasher transition_hasher{};
    StartDomain(&transition_hasher, TransitionDomain);
    HashDigest(&transition_hasher, transition->source_state_id);
    HashDigest(&transition_hasher, transition->target.state_id);
    HashDigest(&transition_hasher, transition->law_fingerprint);
    HashDigest(&transition_hasher, transition->evidence_root_fingerprint);
    HashDigest(&transition_hasher, transition->calculation_receipt);
    HashId(&transition_hasher, transition->relation_id);
    for (std::size_t component = 0U;
         component < LAPLACE_QUERY_SEARCH_COST_COMPONENT_COUNT; ++component) {
        HashU64(&transition_hasher, transition->cost_components[component]);
    }
    HashU32(&transition_hasher, transition->relation_family);
    HashU32(&transition_hasher, transition->source_layer);
    HashU32(&transition_hasher, transition->direction);
    HashU32(&transition_hasher, transition->flags);
    Finish(&transition_hasher, &transition->transition_id);
    return true;
}

int ExpandCandidateBatch(
    void* const opaque,
    const laplace_query_search_state* const states,
    const std::uint64_t* const accumulated_costs,
    const std::size_t state_count,
    laplace_query_search_transition* const transitions,
    const std::size_t transition_capacity,
    std::size_t* const transition_count,
    laplace_query_search_expansion_receipt* const receipt) {
    if (opaque == nullptr || states == nullptr || accumulated_costs == nullptr ||
        state_count == 0U || transitions == nullptr || transition_count == nullptr ||
        receipt == nullptr || transition_capacity == 0U) {
        return 1;
    }
    auto& state = *static_cast<ProviderState*>(opaque);
    *transition_count = 0U;
    *receipt = laplace_query_search_expansion_receipt{};
    receipt->frontier_state_count = static_cast<std::uint64_t>(state_count);

    try {
        std::vector<laplace_id128> source_entity_ids(state_count);
        for (std::size_t index = 0U; index < state_count; ++index) {
            if (!SameDigest(states[index].binding_fingerprint,
                            state.binding.binding_fingerprint) ||
                !SameDigest(states[index].boundary_id, state.search_program.boundary_id) ||
                !SameDigest(states[index].evidence_epoch,
                            state.search_program.evidence_epoch) ||
                !ResolveEntity(state, states[index].anchor_id,
                               &source_entity_ids[index])) {
                receipt->limiting_disposition =
                    LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
                break;
            }
        }
        if (receipt->limiting_disposition != 0U) {
            blake3_hasher failed_hasher{};
            StartDomain(&failed_hasher, TransitionDomain);
            HashDigest(&failed_hasher, state.external->provider_fingerprint);
            HashDigest(&failed_hasher, state.request_fingerprint);
            HashU64(&failed_hasher, receipt->frontier_state_count);
            HashU32(&failed_hasher, receipt->limiting_disposition);
            Finish(&failed_hasher, &receipt->receipt_id);
            return 0;
        }

        std::vector<laplace_cognition_observation_candidate> candidates(
            transition_capacity);
        std::size_t candidate_count = 0U;
        laplace_cognition_observation_candidate_usage usage{};
        const int external_status = state.external->enumerate_candidates(
            state.external->state, &state.binding, source_entity_ids.data(), states,
            accumulated_costs, state_count, candidates.data(), candidates.size(),
            &candidate_count, &usage);
        if (external_status != 0) return 2;
        if (candidate_count > candidates.size() || usage.flags != 0U ||
            (usage.limiting_disposition != 0U &&
             usage.limiting_disposition != LAPLACE_QUERY_SEARCH_DISPOSITION_UNSUPPORTED &&
             usage.limiting_disposition != LAPLACE_QUERY_SEARCH_DISPOSITION_DENIED &&
             usage.limiting_disposition != LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED &&
             usage.limiting_disposition != LAPLACE_QUERY_SEARCH_DISPOSITION_CONTRADICTED &&
             usage.limiting_disposition != LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN) ||
            (usage.limiting_disposition != 0U && candidate_count != 0U)) {
            return 3;
        }
        std::uint64_t maximum_rows = 0U;
        if (state.external->maximum_candidate_records_per_expansion >
            std::numeric_limits<std::uint64_t>::max() /
                static_cast<std::uint64_t>(state_count)) {
            return 4;
        }
        maximum_rows = state.external->maximum_candidate_records_per_expansion *
            static_cast<std::uint64_t>(state_count);
        if (usage.rows_examined > maximum_rows ||
            static_cast<std::uint64_t>(candidate_count) > maximum_rows) {
            return 5;
        }

        candidates.resize(candidate_count);
        for (std::size_t index = 0U; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            if (!CandidateValid(state, candidate, state_count)) return 6;
            const auto source_index =
                static_cast<std::size_t>(candidate.source_state_index);
            if (!BuildTransition(
                    state, states[source_index], source_entity_ids[source_index],
                    candidate, &transitions[index]) ||
                !RegisterEntity(&state, candidate.target_entity_id)) {
                return 7;
            }
        }
        *transition_count = candidates.size();
        receipt->rows_examined = usage.rows_examined;
        receipt->index_plan_count = usage.index_plan_count;
        receipt->crossing_count = usage.crossing_count;
        receipt->emitted_transition_count =
            static_cast<std::uint64_t>(candidates.size());
        receipt->io_operations = usage.io_operations;
        receipt->database_operations = usage.database_operations;
        receipt->limiting_disposition = usage.limiting_disposition;

        blake3_hasher receipt_hasher{};
        StartDomain(&receipt_hasher, TransitionDomain);
        HashDigest(&receipt_hasher, state.external->provider_fingerprint);
        HashDigest(&receipt_hasher, state.request_fingerprint);
        HashU64(&receipt_hasher, receipt->frontier_state_count);
        HashU64(&receipt_hasher, receipt->rows_examined);
        HashU64(&receipt_hasher, receipt->index_plan_count);
        HashU64(&receipt_hasher, receipt->crossing_count);
        HashU64(&receipt_hasher, receipt->emitted_transition_count);
        HashU64(&receipt_hasher, receipt->io_operations);
        HashU64(&receipt_hasher, receipt->database_operations);
        HashU32(&receipt_hasher, receipt->limiting_disposition);
        for (std::size_t index = 0U; index < state_count; ++index) {
            HashDigest(&receipt_hasher, states[index].state_id);
        }
        for (std::size_t index = 0U; index < candidates.size(); ++index) {
            HashDigest(&receipt_hasher, transitions[index].transition_id);
        }
        Finish(&receipt_hasher, &receipt->receipt_id);
        return 0;
    } catch (const std::bad_alloc&) {
        return 8;
    }
}

bool OperationCost(const ProviderState& state, std::uint64_t* const cost) {
    std::uint64_t doubled = 0U;
    if (!Add(
            state.external->maximum_candidate_records_per_expansion,
            state.external->maximum_candidate_records_per_expansion,
            &doubled) ||
        !Add(doubled, 1U, cost)) {
        return false;
    }
    return true;
}

void HashOperationField(
    const char* const domain,
    const std::size_t domain_bytes,
    const ProviderState& state,
    const laplace_cognition_query_projection& projection,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    StartDomain(&hasher, domain, domain_bytes);
    HashDigest(&hasher, state.external->provider_fingerprint);
    HashDigest(&hasher, state.request_fingerprint);
    HashDigest(&hasher, projection.projection_id);
    HashDigest(&hasher, projection.obligation_id);
    HashDigest(&hasher, state.binding.binding_fingerprint);
    HashId(&hasher, state.binding.anchor_entity_id);
    HashU32(&hasher, state.binding.relation_mask);
    HashU32(&hasher, state.binding.maximum_results);
    HashU32(&hasher, state.binding.flags);
    Finish(&hasher, output);
}

template <std::size_t Count>
void HashOperationField(
    const char (&domain)[Count],
    const ProviderState& state,
    const laplace_cognition_query_projection& projection,
    laplace_digest256* const output) {
    HashOperationField(domain, Count - 1U, state, projection, output);
}

bool MakeOperation(
    const ProviderState& state,
    const laplace_cognition_query_projection& projection,
    laplace_cognition_guidance_operation* const operation) {
    if (operation == nullptr ||
        (projection.query_kind != LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH &&
         projection.query_kind != LAPLACE_COGNITION_OPERATION_STRUCTURAL_CALCULATION) ||
        !SameDigest(projection.evidence_boundary, state.search_program.boundary_id) ||
        !SameDigest(projection.binding_fingerprint,
                    state.binding.binding_fingerprint)) {
        return false;
    }
    std::uint64_t operation_cost = 0U;
    if (!OperationCost(state, &operation_cost)) return false;
    *operation = laplace_cognition_guidance_operation{};
    operation->target_obligation_id = projection.obligation_id;
    operation->authority_id = projection.authority_id;
    operation->expected_obligation_reduction = 1U;
    operation->information_value = 1U;
    operation->resource_cost = operation_cost;
    operation->kind = projection.query_kind;
    operation->flags = LAPLACE_COGNITION_OPERATION_INFORMATION_SEEKING;
    HashOperationField(OperationDomain, state, projection, &operation->operation_id);
    HashOperationField(
        OperationOperandsDomain, state, projection, &operation->operands_fingerprint);
    HashOperationField(
        OperationPreconditionsDomain, state, projection,
        &operation->preconditions_fingerprint);
    HashOperationField(
        OperationEffectDomain, state, projection,
        &operation->predicted_effect_fingerprint);
    HashOperationField(
        OperationReceiptContractDomain, state, projection,
        &operation->receipt_contract_fingerprint);
    return true;
}

int EnumerateOperations(
    void* const opaque,
    const laplace_cognition_guidance_state* const guidance,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_guidance_operation* const operations,
    const std::size_t operation_capacity,
    std::size_t* const operation_count,
    laplace_cognition_forward_enumeration_receipt* const receipt) {
    if (opaque == nullptr || guidance == nullptr || projections == nullptr ||
        projection_count == 0U || grant == nullptr || operations == nullptr ||
        operation_count == nullptr || receipt == nullptr) {
        return 1;
    }
    auto& state = *static_cast<ProviderState*>(opaque);
    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(guidance, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 2;
    }
    std::size_t emitted = 0U;
    for (std::size_t index = 0U; index < projection_count; ++index) {
        if (emitted >= operation_capacity ||
            !MakeOperation(state, projections[index], &operations[emitted])) {
            return 3;
        }
        ++emitted;
    }
    *operation_count = emitted;
    *receipt = laplace_cognition_forward_enumeration_receipt{};
    receipt->state_id = state_id;
    ProjectionFingerprint(
        state_id, projections, projection_count, &receipt->projection_fingerprint);
    receipt->projected_query_count = static_cast<std::uint64_t>(projection_count);
    receipt->candidate_operation_count = static_cast<std::uint64_t>(emitted);

    blake3_hasher receipt_hasher{};
    StartDomain(&receipt_hasher, EnumerationReceiptDomain);
    HashDigest(&receipt_hasher, state.external->provider_fingerprint);
    HashDigest(&receipt_hasher, state.request_fingerprint);
    HashDigest(&receipt_hasher, state_id);
    HashDigest(&receipt_hasher, receipt->projection_fingerprint);
    HashU64(&receipt_hasher, receipt->projected_query_count);
    HashU64(&receipt_hasher, receipt->candidate_operation_count);
    for (std::size_t index = 0U; index < emitted; ++index) {
        HashDigest(&receipt_hasher, operations[index].operation_id);
    }
    Finish(&receipt_hasher, &receipt->receipt_id);
    (void)grant;
    return 0;
}

laplace_digest256 GeneratedDigest(
    const char* const domain,
    const std::size_t domain_bytes,
    const ProviderState& state,
    const laplace_digest256& path_id,
    const std::uint64_t ordinal,
    const laplace_id128* const entity_id,
    const laplace_query_search_transition* const transition) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    StartDomain(&hasher, domain, domain_bytes);
    HashDigest(&hasher, state.request_fingerprint);
    HashDigest(&hasher, path_id);
    HashU64(&hasher, ordinal);
    if (entity_id != nullptr) HashId(&hasher, *entity_id);
    if (transition != nullptr) {
        HashDigest(&hasher, transition->transition_id);
        HashDigest(&hasher, transition->law_fingerprint);
        HashDigest(&hasher, transition->evidence_root_fingerprint);
        HashDigest(&hasher, transition->calculation_receipt);
        HashId(&hasher, transition->relation_id);
        HashU32(&hasher, transition->relation_family);
        HashU32(&hasher, transition->source_layer);
        HashU32(&hasher, transition->direction);
    }
    Finish(&hasher, &output);
    return output;
}

template <std::size_t Count>
laplace_digest256 GeneratedDigest(
    const char (&domain)[Count],
    const ProviderState& state,
    const laplace_digest256& path_id,
    const std::uint64_t ordinal,
    const laplace_id128* const entity_id = nullptr,
    const laplace_query_search_transition* const transition = nullptr) {
    return GeneratedDigest(
        domain, Count - 1U, state, path_id, ordinal, entity_id, transition);
}

std::uint32_t OperatorDirection(const std::uint32_t direction) {
    switch (direction) {
        case LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD:
            return LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET;
        case LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE:
            return LAPLACE_COGNITION_OPERATOR_DIRECTION_TARGET_TO_SOURCE;
        case LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC:
            return LAPLACE_COGNITION_OPERATOR_DIRECTION_SYMMETRIC;
        default:
            return 0U;
    }
}

std::uint32_t OperatorSourceClass(
    const laplace_query_search_transition& transition) {
    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY ||
        transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_GEOMETRY) {
        return DigestZero(transition.evidence_root_fingerprint)
            ? LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY : 0U;
    }
    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY ||
        transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING) {
        return !DigestZero(transition.evidence_root_fingerprint)
            ? LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY : 0U;
    }
    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION) {
        return LAPLACE_COGNITION_OPERATOR_SOURCE_DERIVED;
    }
    return 0U;
}

double ConstraintPrecision(const laplace_query_search_transition& transition) {
    const std::uint64_t numerator = transition.cost_components[2];
    const std::uint64_t denominator = transition.cost_components[3];
    if (denominator == 0U) return numerator == 0U ? 1.0 : 0.0;
    if (numerator >= denominator) return 0.0;
    return static_cast<double>(denominator - numerator) /
        static_cast<double>(denominator);
}

bool ExecutePathMathematics(
    const ProviderState& state,
    const laplace_query_search_path_summary& path,
    const std::vector<laplace_query_search_transition>& steps,
    laplace_cognition_observation_answer* const answer,
    laplace_cognition_observation_operator_estate_storage* const estate) {
    if (answer == nullptr || estate == nullptr || steps.empty() ||
        steps.size() + 1U >
            static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }

    std::vector<laplace_id128> entities(steps.size() + 1U);
    if (!ResolveEntity(state, state.initial_search_state.anchor_id, &entities[0])) {
        return false;
    }
    laplace_digest256 expected_source_state = state.initial_search_state.state_id;
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        if (!SameDigest(steps[index].source_state_id, expected_source_state) ||
            !ResolveEntity(state, steps[index].target.anchor_id, &entities[index + 1U])) {
            return false;
        }
        expected_source_state = steps[index].target.state_id;
    }
    if (!SameDigest(expected_source_state, path.terminal_state_id)) return false;

    std::vector<laplace_cognition_operator_field> fields(entities.size());
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        fields[index] = laplace_cognition_operator_field{};
        fields[index].field_id = GeneratedDigest(
            GeneratedOperatorFieldDomain, state, path.path_id,
            static_cast<std::uint64_t>(index), &entities[index]);
        fields[index].entity_id = entities[index];
        fields[index].recipe_fingerprint = state.request_fingerprint;
        fields[index].ordinal = static_cast<std::uint64_t>(index);
        fields[index].value_dimension = 1U;
    }

    std::vector<laplace_cognition_operator_constraint> constraints(steps.size());
    std::set<std::uint32_t> family_set;
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        const auto& step = steps[index];
        const std::uint32_t source_class = OperatorSourceClass(step);
        const std::uint32_t direction = OperatorDirection(step.direction);
        const double precision = ConstraintPrecision(step);
        if (source_class == 0U || direction == 0U ||
            !std::isfinite(precision) || precision <= 0.0) {
            return false;
        }
        auto& constraint = constraints[index];
        constraint = laplace_cognition_operator_constraint{};
        constraint.constraint_id = GeneratedDigest(
            GeneratedOperatorConstraintDomain, state, path.path_id,
            static_cast<std::uint64_t>(index), nullptr, &step);
        constraint.plane_id = GeneratedDigest(
            GeneratedOperatorPlaneDomain, state, path.path_id,
            (static_cast<std::uint64_t>(step.relation_family) << 32U) |
                static_cast<std::uint64_t>(step.source_layer));
        constraint.law_fingerprint = step.law_fingerprint;
        constraint.units_fingerprint = GeneratedDigest(
            GeneratedOperatorUnitsDomain, state, path.path_id,
            static_cast<std::uint64_t>(step.relation_family));
        constraint.evidence_root_id = step.evidence_root_fingerprint;
        constraint.calculation_receipt_id = step.calculation_receipt;
        constraint.source_field_index = static_cast<std::uint64_t>(index);
        constraint.target_field_index = static_cast<std::uint64_t>(index + 1U);
        constraint.transport_scale = 1.0;
        constraint.transport_offset = 0.0;
        constraint.target_value = 0.0;
        constraint.precision = precision;
        constraint.relation_family = step.relation_family;
        constraint.source_class = source_class;
        constraint.direction = direction;
        constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
        family_set.insert(step.relation_family);
    }
    std::vector<std::uint32_t> families(family_set.begin(), family_set.end());
    if (families.empty()) return false;

    laplace_cognition_operator_program operator_program{};
    operator_program.program_id = GeneratedDigest(
        GeneratedOperatorProgramDomain, state, path.path_id, 0U);
    operator_program.boundary_id = state.search_program.boundary_id;
    operator_program.context_fingerprint = state.initial_search_state.context_fingerprint;
    operator_program.evidence_epoch = state.search_program.evidence_epoch;
    operator_program.result_contract_fingerprint = state.result_contract_fingerprint;
    operator_program.eligible_relation_families = families.data();
    operator_program.eligible_relation_family_count = families.size();
    operator_program.eligible_source_mask = 7U;
    operator_program.flags =
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_POSITIVE_SEMIDEFINITE_PRECISION |
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_RELATION_PLANE_SEPARATION;
    operator_program.numeric_tolerance = 1e-12;
    operator_program.version = LAPLACE_COGNITION_OPERATOR_VERSION;

    std::array<laplace_cognition_solver_boundary, 2> boundaries{};
    boundaries[0].receipt_id = GeneratedDigest(
        GeneratedAnchorBoundaryDomain, state, path.path_id, 0U, &entities.front());
    boundaries[0].field_index = 0U;
    boundaries[0].target_value = 0.0;
    boundaries[0].precision = 1.0;
    boundaries[0].kind = LAPLACE_COGNITION_SOLVER_BOUNDARY_PRIOR;
    boundaries[1].receipt_id = GeneratedDigest(
        GeneratedTerminalBoundaryDomain, state, path.path_id,
        static_cast<std::uint64_t>(steps.size()), &entities.back());
    boundaries[1].field_index = static_cast<std::uint64_t>(steps.size());
    boundaries[1].target_value = 1.0;
    boundaries[1].precision = 1.0;
    boundaries[1].kind = LAPLACE_COGNITION_SOLVER_BOUNDARY_GOAL;

    laplace_cognition_solver_program solver_program{};
    solver_program.program_id = GeneratedDigest(
        GeneratedSolverProgramDomain, state, path.path_id, 0U);
    solver_program.result_contract_fingerprint = state.result_contract_fingerprint;
    if (fields.size() >
        (std::numeric_limits<std::uint64_t>::max() - UINT64_C(32)) / UINT64_C(8)) {
        return false;
    }
    solver_program.max_iterations =
        static_cast<std::uint64_t>(fields.size()) * UINT64_C(8) + UINT64_C(32);
    solver_program.absolute_residual_tolerance = 1e-10;
    solver_program.relative_residual_tolerance = 1e-10;
    solver_program.regularization = 0.0;
    solver_program.method = LAPLACE_COGNITION_SOLVER_METHOD_CONJUGATE_GRADIENT;
    solver_program.version = LAPLACE_COGNITION_SOLVER_VERSION;

    std::vector<double> initial(fields.size(), 0.0);
    std::vector<double> solution(fields.size(), 0.0);
    laplace_cognition_runtime_request runtime_request{};
    runtime_request.operator_program = operator_program;
    runtime_request.fields = fields.data();
    runtime_request.constraints = constraints.data();
    runtime_request.boundaries = boundaries.data();
    runtime_request.initial_state = initial.data();
    runtime_request.field_count = fields.size();
    runtime_request.constraint_count = constraints.size();
    runtime_request.boundary_count = boundaries.size();
    runtime_request.initial_state_count = initial.size();
    runtime_request.solver_program = solver_program;

    laplace_cognition_runtime_result runtime_result{};
    runtime_result.solution = solution.data();
    runtime_result.solution_capacity = solution.size();
    if (laplace_cognition_runtime_execute(&runtime_request, &runtime_result) !=
            LAPLACE_COGNITION_RUNTIME_OK ||
        runtime_result.solution_count != solution.size() ||
        runtime_result.operator_receipt.status != LAPLACE_COGNITION_OPERATOR_OK ||
        runtime_result.solver_receipt.status != LAPLACE_COGNITION_SOLVER_OK ||
        !std::isfinite(runtime_result.solver_receipt.final_residual_l2) ||
        !std::isfinite(runtime_result.solver_receipt.final_energy) ||
        !(solution.back() > solution.front())) {
        return false;
    }

    answer->operator_receipt_id = runtime_result.operator_receipt.receipt_id;
    answer->solver_receipt_id = runtime_result.solver_receipt.receipt_id;
    answer->solver_output_fingerprint = runtime_result.solver_receipt.output_fingerprint;
    answer->solver_final_residual_l2 = runtime_result.solver_receipt.final_residual_l2;
    answer->solver_final_energy = runtime_result.solver_receipt.final_energy;
    answer->flags |= LAPLACE_COGNITION_OBSERVATION_ANSWER_OPERATOR_EXECUTED;

    estate->path_id = path.path_id;
    estate->program = operator_program;
    estate->families = std::move(families);
    estate->fields = std::move(fields);
    estate->constraints = std::move(constraints);
    estate->program.eligible_relation_families = nullptr;
    estate->program.eligible_relation_family_count = estate->families.size();
    return true;
}

bool CaptureAnswers(
    ProviderState* const state,
    const laplace_query_search_result* const search_result) {
    state->answers.clear();
    state->operator_estates.clear();
    const std::size_t path_count = laplace_query_search_result_path_count(search_result);
    state->answers.reserve(path_count);
    state->operator_estates.reserve(path_count);
    for (std::size_t index = 0U; index < path_count; ++index) {
        laplace_query_search_path_summary path{};
        if (laplace_query_search_result_path(
                search_result, index, &path) != LAPLACE_QUERY_SEARCH_OK ||
            path.transition_count == 0U ||
            path.transition_count >
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return false;
        }

        laplace_cognition_observation_answer answer{};
        answer.path_id = path.path_id;
        answer.terminal_state_id = path.terminal_state_id;
        answer.total_cost = path.total_cost;
        answer.transition_count = path.transition_count;
        answer.independent_evidence_root_count =
            path.independent_evidence_root_count;
        answer.rank = path.rank;

        std::vector<laplace_query_search_transition> steps(
            static_cast<std::size_t>(path.transition_count));
        std::size_t step_count = 0U;
        if (laplace_query_search_result_path_steps(
                search_result, index, steps.data(), steps.size(), &step_count) !=
                LAPLACE_QUERY_SEARCH_OK ||
            step_count != steps.size()) {
            return false;
        }
        const auto& last = steps.back();
        const laplace_digest256 terminal_coordinate = last.target.anchor_id;
        answer.relation_id = last.relation_id;
        answer.relation_family = last.relation_family;
        answer.source_layer = last.source_layer;
        answer.direction = last.direction;
        if ((last.flags & LAPLACE_QUERY_SEARCH_TRANSITION_RELATION_ID_PRESENT) != 0U) {
            answer.flags |= LAPLACE_COGNITION_OBSERVATION_ANSWER_RELATION_ID_PRESENT;
        }
        laplace_cognition_observation_operator_estate_storage estate{};
        if (!ResolveEntity(*state, terminal_coordinate, &answer.entity_id) ||
            !ExecutePathMathematics(*state, path, steps, &answer, &estate)) {
            return false;
        }
        state->answers.push_back(answer);
        state->operator_estates.push_back(std::move(estate));
    }
    return true;
}

int ExecuteOperation(
    void* const opaque,
    const laplace_cognition_guidance_state* const guidance,
    const laplace_cognition_guidance_operation* const operation,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_resolution* const resolutions,
    const std::size_t resolution_capacity,
    std::size_t* const resolution_count,
    laplace_cognition_forward_execution_receipt* const receipt) {
    if (opaque == nullptr || guidance == nullptr || operation == nullptr ||
        projections == nullptr || projection_count == 0U || grant == nullptr ||
        resolutions == nullptr || resolution_capacity == 0U ||
        resolution_count == nullptr || receipt == nullptr) {
        return 1;
    }
    auto& state = *static_cast<ProviderState*>(opaque);
    const laplace_cognition_query_projection* projection = nullptr;
    if (!FindProjection(
            projections, projection_count, operation->target_obligation_id,
            &projection) || projection == nullptr) {
        return 2;
    }
    laplace_cognition_guidance_operation expected{};
    if (!MakeOperation(state, *projection, &expected) ||
        !SameDigest(expected.operation_id, operation->operation_id)) {
        return 3;
    }

    laplace_query_search_provider_v1 search_provider{};
    search_provider.state = &state;
    search_provider.expand_batch = ExpandCandidateBatch;
    search_provider.abi_major = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR;
    search_provider.abi_minor = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR;

    laplace_query_search_result* search_result = nullptr;
    laplace_query_search_receipt search_receipt{};
    const auto search_status = laplace_query_search_execute(
        &state.search_program, &state.initial_search_state, 1U,
        &search_provider, &search_result, &search_receipt);
    if (search_status != LAPLACE_QUERY_SEARCH_OK || search_result == nullptr) {
        laplace_query_search_result_destroy(&search_result);
        return 4;
    }

    std::uint64_t execution_cost = 1U;
    if (!Add(execution_cost, search_receipt.rows_examined, &execution_cost) ||
        !Add(
            execution_cost, search_receipt.transition_record_count,
            &execution_cost) ||
        execution_cost > grant->remaining_resource_cost ||
        search_receipt.io_operations > grant->remaining_io_operations ||
        search_receipt.database_operations > grant->remaining_database_operations) {
        laplace_query_search_result_destroy(&search_result);
        return 5;
    }

    blake3_hasher result_hasher{};
    StartDomain(&result_hasher, SearchResultDomain);
    HashDigest(&result_hasher, state.external->provider_fingerprint);
    HashDigest(&result_hasher, state.request_fingerprint);
    HashDigest(&result_hasher, state.search_program.program_id);
    HashDigest(&result_hasher, search_receipt.receipt_id);
    HashDigest(&result_hasher, projection->obligation_id);
    HashDigest(&result_hasher, state.binding.binding_fingerprint);
    HashU32(&result_hasher, search_receipt.disposition);
    const std::size_t path_count =
        laplace_query_search_result_path_count(search_result);
    HashU64(&result_hasher, static_cast<std::uint64_t>(path_count));
    for (std::size_t index = 0U; index < path_count; ++index) {
        laplace_query_search_path_summary path{};
        if (laplace_query_search_result_path(
                search_result, index, &path) != LAPLACE_QUERY_SEARCH_OK) {
            laplace_query_search_result_destroy(&search_result);
            return 6;
        }
        HashDigest(&result_hasher, path.path_id);
        HashDigest(&result_hasher, path.terminal_state_id);
        HashU64(&result_hasher, path.total_cost);
        HashU64(&result_hasher, path.transition_count);
    }
    laplace_digest256 result_fingerprint{};
    Finish(&result_hasher, &result_fingerprint);

    if (!CaptureAnswers(&state, search_result)) {
        laplace_query_search_result_destroy(&search_result);
        return 7;
    }

    blake3_hasher resolution_hasher{};
    StartDomain(&resolution_hasher, ResolutionReceiptDomain);
    HashDigest(&resolution_hasher, result_fingerprint);
    HashDigest(&resolution_hasher, search_receipt.receipt_id);
    HashDigest(&resolution_hasher, operation->operation_id);
    laplace_digest256 resolution_receipt{};
    Finish(&resolution_hasher, &resolution_receipt);

    resolutions[0] = laplace_cognition_resolution{};
    resolutions[0].obligation_id = projection->obligation_id;
    resolutions[0].receipt_id = resolution_receipt;
    if (path_count != 0U &&
        search_receipt.disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE) {
        resolutions[0].value_id = result_fingerprint;
        resolutions[0].disposition = LAPLACE_COGNITION_OBLIGATION_SATISFIED;
    } else {
        resolutions[0].disposition = ResolutionDisposition(search_receipt.disposition);
    }
    *resolution_count = 1U;

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(guidance, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        laplace_query_search_result_destroy(&search_result);
        return 8;
    }

    *receipt = laplace_cognition_forward_execution_receipt{};
    receipt->state_id = state_id;
    receipt->operation_id = operation->operation_id;
    receipt->result_fingerprint = result_fingerprint;
    receipt->resolution_count = 1U;
    receipt->resource_cost = execution_cost;
    receipt->io_operations = search_receipt.io_operations;
    receipt->database_operations = search_receipt.database_operations;

    blake3_hasher execution_hasher{};
    StartDomain(&execution_hasher, ExecutionReceiptDomain);
    HashDigest(&execution_hasher, state.external->provider_fingerprint);
    HashDigest(&execution_hasher, state.request_fingerprint);
    HashDigest(&execution_hasher, state.search_program.program_id);
    HashDigest(&execution_hasher, search_receipt.receipt_id);
    HashDigest(&execution_hasher, state_id);
    HashDigest(&execution_hasher, operation->operation_id);
    HashDigest(&execution_hasher, result_fingerprint);
    HashDigest(&execution_hasher, resolution_receipt);
    HashU64(&execution_hasher, receipt->resource_cost);
    HashU64(&execution_hasher, receipt->io_operations);
    HashU64(&execution_hasher, receipt->database_operations);
    Finish(&execution_hasher, &receipt->receipt_id);

    laplace_query_search_result_destroy(&search_result);
    return 0;
}

}  // namespace laplace_cognition_observation_candidate_provider_detail

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_request_execute_with_candidate_provider(
    const laplace_cognition_observation_request* const request,
    const laplace_cognition_observation_candidate_provider_v1* const provider,
    laplace_cognition_observation_result** const observation_result,
    laplace_cognition_forward_result** const forward_result,
    laplace_cognition_forward_receipt* const receipt) {
    if (observation_result != nullptr) *observation_result = nullptr;
    if (forward_result != nullptr) *forward_result = nullptr;
    if (receipt != nullptr) *receipt = laplace_cognition_forward_receipt{};
    if (request == nullptr || provider == nullptr || observation_result == nullptr ||
        forward_result == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    if (!laplace_cognition_observation_candidate_provider_detail::ProviderValid(
            *request, *provider)) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_PROVIDER_FAILURE;
    }

    laplace_cognition_observation_compiled_request compiled{};
    auto status = laplace_cognition_observation_request_compile(request, &compiled);
    if (status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) return status;

    try {
        laplace_cognition_observation_candidate_provider_detail::ProviderState state{};
        state.external = provider;
        state.request_fingerprint = compiled.request_fingerprint;
        state.result_contract_fingerprint = request->result_contract_fingerprint;
        state.binding = compiled.binding;
        state.initial_search_state = compiled.initial_search_state;
        state.search_program = compiled.search_program;
        if (!laplace_cognition_observation_candidate_provider_detail::RegisterEntity(
                &state, request->anchor_entity_id)) {
            laplace_cognition_observation_compiled_request_destroy(&compiled);
            return LAPLACE_COGNITION_OBSERVATION_REQUEST_COORDINATE_FAILURE;
        }

        laplace_cognition_forward_provider_v1 forward_provider{};
        forward_provider.state = &state;
        forward_provider.enumerate =
            laplace_cognition_observation_candidate_provider_detail::EnumerateOperations;
        forward_provider.execute =
            laplace_cognition_observation_candidate_provider_detail::ExecuteOperation;
        forward_provider.abi_major = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR;
        forward_provider.abi_minor = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR;

        const auto forward_status = laplace_cognition_forward_pass_execute(
            &compiled.forward_program, compiled.guidance_state, &forward_provider,
            forward_result, receipt);
        if (forward_status != LAPLACE_COGNITION_FORWARD_OK) {
            laplace_cognition_forward_result_destroy(forward_result);
            *receipt = laplace_cognition_forward_receipt{};
            status = LAPLACE_COGNITION_OBSERVATION_REQUEST_EXECUTION_FAILURE;
        } else {
            auto* retained = new laplace_cognition_observation_result{};
            retained->answers = std::move(state.answers);
            retained->operator_estates = std::move(state.operator_estates);
            *observation_result = retained;
        }
    } catch (const std::bad_alloc&) {
        laplace_cognition_forward_result_destroy(forward_result);
        delete *observation_result;
        *observation_result = nullptr;
        *receipt = laplace_cognition_forward_receipt{};
        status = LAPLACE_COGNITION_OBSERVATION_REQUEST_MEMORY_FAILURE;
    }

    laplace_cognition_observation_compiled_request_destroy(&compiled);
    return status;
}

extern "C" size_t laplace_cognition_observation_result_answer_count(
    const laplace_cognition_observation_result* const result) {
    return result == nullptr ? 0U : result->answers.size();
}

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_result_answer(
    const laplace_cognition_observation_result* const result,
    const size_t answer_index,
    laplace_cognition_observation_answer* const answer) {
    if (result == nullptr || answer == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    if (answer_index >= result->answers.size()) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_RESULT_RANGE;
    }
    *answer = result->answers[answer_index];
    return LAPLACE_COGNITION_OBSERVATION_REQUEST_OK;
}

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_result_operator_view(
    const laplace_cognition_observation_result* const result,
    const size_t answer_index,
    laplace_cognition_observation_operator_view* const view) {
    if (view != nullptr) *view = laplace_cognition_observation_operator_view{};
    if (result == nullptr || view == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    if (answer_index >= result->answers.size() ||
        answer_index >= result->operator_estates.size()) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_RESULT_RANGE;
    }
    const auto& estate = result->operator_estates[answer_index];
    view->path_id = estate.path_id;
    view->program = estate.program;
    view->program.eligible_relation_families = estate.families.data();
    view->program.eligible_relation_family_count = estate.families.size();
    view->fields = estate.fields.data();
    view->field_count = estate.fields.size();
    view->constraints = estate.constraints.data();
    view->constraint_count = estate.constraints.size();
    return LAPLACE_COGNITION_OBSERVATION_REQUEST_OK;
}

extern "C" void laplace_cognition_observation_result_destroy(
    laplace_cognition_observation_result** const result) {
    if (result != nullptr) {
        delete *result;
        *result = nullptr;
    }
}