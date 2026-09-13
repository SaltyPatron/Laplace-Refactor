#include "laplace/cognition_interpretation_query.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace laplace_cognition_interpretation_query_detail {

using CandidateState =
    laplace_cognition_observation_candidate_provider_detail::ProviderState;

constexpr char QueryProgramDomain[] =
    "laplace-cognition-interpreted-query-program-v1";
constexpr char QueryContextDomain[] =
    "laplace-cognition-interpreted-obligation-query-context-v1";
constexpr char EnumerationReceiptDomain[] =
    "laplace-cognition-interpreted-query-enumeration-v1";

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool QueryRuleValid(const laplace_cognition_interpretation_query_rule& query) {
    constexpr std::uint32_t AllowedRequestFlags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_ALLOW_TYPED_UNRESOLVED |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    return query.anchor_operand_index !=
               LAPLACE_COGNITION_INTERPRETATION_QUERY_NO_OPERAND &&
        query.relation_mask != 0U &&
        (query.relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) == 0U &&
        query.maximum_results != 0U &&
        (query.request_flags & ~AllowedRequestFlags) == 0U &&
        query.reserved0 == 0U && query.reserved1 == 0U;
}

laplace_cognition_interpretation_status Identify(
    const laplace_cognition_interpretation_query_program& program,
    laplace_digest256* const identity) {
    if (identity != nullptr) *identity = laplace_digest256{};
    if (identity == nullptr ||
        program.version != LAPLACE_COGNITION_INTERPRETATION_QUERY_PROGRAM_VERSION ||
        program.reserved != 0U || program.queries == nullptr ||
        program.query_count == 0U ||
        program.query_count >
            static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, QueryProgramDomain, sizeof(QueryProgramDomain) - 1U);
    HashU32(&hasher, program.version);
    HashU64(&hasher, static_cast<std::uint64_t>(program.query_count));
    std::uint32_t previous = 0U;
    for (std::size_t index = 0U; index < program.query_count; ++index) {
        const auto& query = program.queries[index];
        if (!QueryRuleValid(query) ||
            (index != 0U && query.obligation_index <= previous)) {
            return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
        }
        previous = query.obligation_index;
        HashU32(&hasher, query.obligation_index);
        HashU32(&hasher, query.anchor_operand_index);
        HashU32(&hasher, query.goal_operand_index);
        HashU32(&hasher, query.relation_mask);
        HashU32(&hasher, query.maximum_results);
        HashU32(&hasher, query.request_flags);
    }
    Finish(&hasher, identity);
    return LAPLACE_COGNITION_INTERPRETATION_OK;
}

laplace_digest256 QueryContext(
    const laplace_digest256& interpreted_context,
    const laplace_digest256& recipe_id,
    const laplace_digest256& obligation_id,
    const std::uint32_t obligation_index) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, QueryContextDomain, sizeof(QueryContextDomain) - 1U);
    HashDigest(&hasher, interpreted_context);
    HashDigest(&hasher, recipe_id);
    HashDigest(&hasher, obligation_id);
    HashU32(&hasher, obligation_index);
    laplace_digest256 output{};
    Finish(&hasher, &output);
    return output;
}

struct QueryExecution final {
    laplace_digest256 obligation_id{};
    laplace_digest256 outer_binding_fingerprint{};
    CandidateState candidate{};
};

struct Runtime final {
    const laplace_cognition_observation_candidate_provider_v1* external{};
    laplace_digest256 recipe_id{};
    std::vector<QueryExecution> queries;
};

QueryExecution* FindQuery(Runtime& runtime, const laplace_digest256& obligation_id) {
    for (auto& query : runtime.queries) {
        if (SameDigest(query.obligation_id, obligation_id)) return &query;
    }
    return nullptr;
}

const QueryExecution* FindQuery(
    const Runtime& runtime, const laplace_digest256& obligation_id) {
    for (const auto& query : runtime.queries) {
        if (SameDigest(query.obligation_id, obligation_id)) return &query;
    }
    return nullptr;
}

bool ProjectionForQuery(
    const QueryExecution& query,
    const laplace_cognition_query_projection& outer,
    laplace_cognition_query_projection* const local) {
    if (local == nullptr || !SameDigest(query.obligation_id, outer.obligation_id) ||
        !SameDigest(query.outer_binding_fingerprint, outer.binding_fingerprint) ||
        !SameDigest(outer.evidence_boundary, query.candidate.search_program.boundary_id)) {
        return false;
    }
    *local = outer;
    local->binding_fingerprint = query.candidate.binding.binding_fingerprint;
    return true;
}

int Enumerate(
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
        operation_count == nullptr || receipt == nullptr ||
        operation_capacity < projection_count) {
        return 1;
    }
    auto& runtime = *static_cast<Runtime*>(opaque);
    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(guidance, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 2;
    }
    for (std::size_t index = 0U; index < projection_count; ++index) {
        const auto* query = FindQuery(runtime, projections[index].obligation_id);
        laplace_cognition_query_projection local{};
        if (query == nullptr || !ProjectionForQuery(*query, projections[index], &local) ||
            !laplace_cognition_observation_candidate_provider_detail::MakeOperation(
                query->candidate, local, &operations[index])) {
            return 3;
        }
    }
    *operation_count = projection_count;
    *receipt = laplace_cognition_forward_enumeration_receipt{};
    receipt->state_id = state_id;
    laplace_cognition_observation_candidate_provider_detail::ProjectionFingerprint(
        state_id, projections, projection_count, &receipt->projection_fingerprint);
    receipt->projected_query_count = static_cast<std::uint64_t>(projection_count);
    receipt->candidate_operation_count = static_cast<std::uint64_t>(projection_count);

    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, EnumerationReceiptDomain, sizeof(EnumerationReceiptDomain) - 1U);
    HashDigest(&hasher, runtime.external->provider_fingerprint);
    HashDigest(&hasher, runtime.recipe_id);
    HashDigest(&hasher, state_id);
    HashDigest(&hasher, receipt->projection_fingerprint);
    HashU64(&hasher, receipt->projected_query_count);
    HashU64(&hasher, receipt->candidate_operation_count);
    for (std::size_t index = 0U; index < projection_count; ++index) {
        HashDigest(&hasher, operations[index].operation_id);
    }
    Finish(&hasher, &receipt->receipt_id);
    (void)grant;
    return 0;
}

int Execute(
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
    auto& runtime = *static_cast<Runtime*>(opaque);
    auto* query = FindQuery(runtime, operation->target_obligation_id);
    if (query == nullptr) return 2;
    const laplace_cognition_query_projection* outer = nullptr;
    for (std::size_t index = 0U; index < projection_count; ++index) {
        if (SameDigest(projections[index].obligation_id, query->obligation_id)) {
            outer = &projections[index];
            break;
        }
    }
    laplace_cognition_query_projection local{};
    if (outer == nullptr || !ProjectionForQuery(*query, *outer, &local)) return 3;
    return laplace_cognition_observation_candidate_provider_detail::ExecuteOperation(
        &query->candidate, guidance, operation, &local, 1U, grant,
        resolutions, resolution_capacity, resolution_count, receipt);
}

struct GuidanceOwner final {
    laplace_cognition_guidance_state* state{};
    ~GuidanceOwner() { laplace_cognition_guidance_state_destroy(&state); }
};

laplace_cognition_forward_program ForwardProgram(
    const laplace_cognition_guidance_header& header,
    const laplace_cognition_observation_forward_limits& limits) {
    laplace_cognition_forward_program program{};
    program.program_id = header.program_id;
    program.result_contract_fingerprint = header.result_contract_fingerprint;
    program.max_layers = limits.max_layers;
    program.max_provider_calls = limits.max_provider_calls;
    program.max_projected_queries = limits.max_projected_queries;
    program.max_candidate_operations = limits.max_candidate_operations;
    program.max_resolutions = limits.max_resolutions;
    program.max_resource_cost = limits.max_resource_cost;
    program.max_io_operations = limits.max_io_operations;
    program.max_database_operations = limits.max_database_operations;
    program.candidate_operation_capacity = limits.candidate_operation_capacity;
    program.resolution_capacity = limits.resolution_capacity;
    program.flags =
        LAPLACE_COGNITION_FORWARD_REQUIRE_STATE_PROGRESS |
        LAPLACE_COGNITION_FORWARD_REQUIRE_PROJECTED_QUERY_CONSUMPTION |
        LAPLACE_COGNITION_FORWARD_REQUIRE_RECEIPTED_EXECUTION;
    program.version = LAPLACE_COGNITION_FORWARD_VERSION;
    return program;
}

}  // namespace laplace_cognition_interpretation_query_detail

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_query_program_identify(
    const laplace_cognition_interpretation_query_program* const program,
    laplace_digest256* const identity) {
    if (identity != nullptr) *identity = laplace_digest256{};
    if (program == nullptr || identity == nullptr) {
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }
    return laplace_cognition_interpretation_query_detail::Identify(*program, identity);
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_forward_execute_with_candidate_provider(
    const laplace_cognition_interpretation_result* const interpretation,
    const laplace_cognition_interpretation_program_rule* const rule,
    const laplace_cognition_interpretation_query_program* const query_program,
    const laplace_cognition_turn_input* const turn,
    const laplace_cognition_turn_policy* const policy,
    const laplace_cognition_observation_candidate_provider_v1* const provider,
    laplace_cognition_forward_result** const result,
    laplace_cognition_forward_receipt* const receipt) {
    using namespace laplace_cognition_interpretation_query_detail;
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) *receipt = laplace_cognition_forward_receipt{};
    if (interpretation == nullptr || rule == nullptr || query_program == nullptr ||
        turn == nullptr || policy == nullptr || provider == nullptr ||
        result == nullptr || receipt == nullptr ||
        query_program->query_count != rule->obligation_count) {
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }

    laplace_digest256 query_program_id{};
    if (Identify(*query_program, &query_program_id) !=
            LAPLACE_COGNITION_INTERPRETATION_OK ||
        !SameDigest(query_program_id, rule->recipe_id)) {
        return LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH;
    }
    laplace_cognition_observation_request provider_probe{};
    if (!laplace_cognition_observation_candidate_provider_detail::ProviderValid(
            provider_probe, *provider)) {
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }

    laplace_cognition_turn_input inferred_turn{};
    laplace_cognition_turn_policy inferred_policy{};
    const auto prepared = laplace_cognition_interpretation_prepare_turn(
        interpretation, &rule->goal_slot_id, turn, policy,
        &inferred_turn, &inferred_policy);
    if (prepared != LAPLACE_COGNITION_INTERPRETATION_OK) return prepared;

    GuidanceOwner guidance;
    const auto compiled_guidance = laplace_cognition_interpretation_compile_guidance(
        interpretation, rule, &guidance.state);
    if (compiled_guidance != LAPLACE_COGNITION_INTERPRETATION_OK) {
        return compiled_guidance;
    }
    laplace_cognition_guidance_header header{};
    if (laplace_cognition_guidance_state_header(guidance.state, &header) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return LAPLACE_COGNITION_INTERPRETATION_GUIDANCE_FAILURE;
    }

    try {
        Runtime runtime{};
        runtime.external = provider;
        runtime.recipe_id = query_program_id;
        runtime.queries.reserve(rule->obligation_count);
        constexpr std::uint32_t AllowedRequestFlags =
            LAPLACE_COGNITION_OBSERVATION_REQUEST_ALLOW_TYPED_UNRESOLVED |
            LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;

        for (std::size_t index = 0U; index < rule->obligation_count; ++index) {
            const auto& declared = rule->obligations[index];
            const auto& query = query_program->queries[index];
            if (query.obligation_index != index || declared.operand_slots == nullptr ||
                declared.operand_count == 0U ||
                query.anchor_operand_index >= declared.operand_count ||
                (query.goal_operand_index !=
                     LAPLACE_COGNITION_INTERPRETATION_QUERY_NO_OPERAND &&
                 query.goal_operand_index >= declared.operand_count) ||
                (declared.kind != LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH &&
                 declared.kind != LAPLACE_COGNITION_OPERATION_STRUCTURAL_CALCULATION) ||
                (query.request_flags & ~policy->request_flags & AllowedRequestFlags) != 0U ||
                policy->relation_mask == 0U ||
                (query.relation_mask & ~policy->relation_mask) != 0U ||
                policy->maximum_results == 0U ||
                query.maximum_results > policy->maximum_results ||
                policy->search_budget.requested_path_count == 0U ||
                query.maximum_results > policy->search_budget.requested_path_count) {
                return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
            }

            laplace_cognition_obligation outer_obligation{};
            if (laplace_cognition_guidance_state_obligation(
                    guidance.state, index, &outer_obligation) !=
                LAPLACE_COGNITION_GUIDANCE_OK) {
                return LAPLACE_COGNITION_INTERPRETATION_GUIDANCE_FAILURE;
            }

            laplace_cognition_observation_request request{};
            if (laplace_cognition_interpretation_read_binding(
                    interpretation,
                    declared.operand_slots + query.anchor_operand_index,
                    &request.anchor_entity_id) !=
                LAPLACE_COGNITION_INTERPRETATION_OK) {
                return LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH;
            }
            if (query.goal_operand_index !=
                LAPLACE_COGNITION_INTERPRETATION_QUERY_NO_OPERAND) {
                if (laplace_cognition_interpretation_read_binding(
                        interpretation,
                        declared.operand_slots + query.goal_operand_index,
                        &request.goal_entity_id) !=
                    LAPLACE_COGNITION_INTERPRETATION_OK) {
                    return LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH;
                }
                request.flags = query.request_flags |
                    LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT;
            } else {
                request.flags = query.request_flags |
                    LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
            }
            request.world_id = inferred_turn.world_id;
            request.time_fingerprint = inferred_turn.time_fingerprint;
            request.context_fingerprint = QueryContext(
                inferred_turn.context_fingerprint, query_program_id,
                outer_obligation.obligation_id,
                static_cast<std::uint32_t>(index));
            request.evidence_boundary = inferred_policy.evidence_boundary;
            request.evidence_epoch = inferred_policy.evidence_epoch;
            request.authority_id = inferred_policy.authority_id;
            request.result_contract_fingerprint =
                declared.result_contract_fingerprint;
            request.search_budget = inferred_policy.search_budget;
            request.search_budget.requested_path_count = query.maximum_results;
            request.forward_limits = inferred_policy.forward_limits;
            request.relation_mask = query.relation_mask;
            request.maximum_results = query.maximum_results;
            request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

            laplace_cognition_observation_compiled_request compiled{};
            const auto request_status =
                laplace_cognition_observation_request_compile(&request, &compiled);
            if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
                laplace_cognition_observation_compiled_request_destroy(&compiled);
                return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
            }

            QueryExecution execution{};
            execution.obligation_id = outer_obligation.obligation_id;
            execution.outer_binding_fingerprint =
                outer_obligation.binding_fingerprint;
            execution.candidate.external = provider;
            execution.candidate.request_fingerprint = compiled.request_fingerprint;
            execution.candidate.result_contract_fingerprint =
                request.result_contract_fingerprint;
            execution.candidate.binding = compiled.binding;
            execution.candidate.initial_search_state = compiled.initial_search_state;
            execution.candidate.search_program = compiled.search_program;
            const bool registered =
                laplace_cognition_observation_candidate_provider_detail::RegisterEntity(
                    &execution.candidate, request.anchor_entity_id);
            laplace_cognition_observation_compiled_request_destroy(&compiled);
            if (!registered) {
                return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
            }
            runtime.queries.push_back(std::move(execution));
        }

        laplace_cognition_forward_provider_v1 forward_provider{};
        forward_provider.state = &runtime;
        forward_provider.enumerate = Enumerate;
        forward_provider.execute = Execute;
        forward_provider.abi_major = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR;
        forward_provider.abi_minor = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR;

        const auto program = ForwardProgram(header, inferred_policy.forward_limits);
        const auto status = laplace_cognition_forward_pass_execute(
            &program, guidance.state, &forward_provider, result, receipt);
        if (status == LAPLACE_COGNITION_FORWARD_MEMORY_FAILURE) {
            return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
        }
        if (status != LAPLACE_COGNITION_FORWARD_OK) {
            laplace_cognition_forward_result_destroy(result);
            *receipt = laplace_cognition_forward_receipt{};
            return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
        }
        return LAPLACE_COGNITION_INTERPRETATION_OK;
    } catch (const std::bad_alloc&) {
        laplace_cognition_forward_result_destroy(result);
        *receipt = laplace_cognition_forward_receipt{};
        return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        laplace_cognition_forward_result_destroy(result);
        *receipt = laplace_cognition_forward_receipt{};
        return LAPLACE_COGNITION_INTERPRETATION_RANGE;
    }
}
