namespace laplace_cognition_observation_request_provider_detail {

using laplace_observation_query_detail::Add;
using laplace_observation_query_detail::ExecutionReceiptDomain;
using laplace_observation_query_detail::FindProjection;
using laplace_observation_query_detail::Finish;
using laplace_observation_query_detail::HashDigest;
using laplace_observation_query_detail::HashU32;
using laplace_observation_query_detail::HashU64;
using laplace_observation_query_detail::MakeOperation;
using laplace_observation_query_detail::ResolutionDisposition;
using laplace_observation_query_detail::ResolutionReceiptDomain;
using laplace_observation_query_detail::SameDigest;
using laplace_observation_query_detail::SearchResultDomain;
using laplace_observation_query_detail::StartDomain;

}  // namespace laplace_cognition_observation_request_provider_detail

struct laplace_cognition_observation_request_provider {
    laplace_observation_query_index* index{};
    laplace_digest256 request_fingerprint{};
    laplace_observation_query_binding binding{};
    laplace_query_search_state initial_search_state{};
    laplace_query_search_program search_program{};
};

namespace laplace_cognition_observation_request_provider_detail {

bool CompiledStateValid(
    laplace_observation_query_index& index,
    const laplace_cognition_observation_compiled_request& compiled) {
    if (compiled.status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK ||
        compiled.reserved != 0U || compiled.guidance_state == nullptr ||
        laplace_observation_query_detail::DigestZero(compiled.request_fingerprint)) {
        return false;
    }

    laplace_digest256 expected_binding{};
    if (laplace_observation_query_binding_identify(
            &compiled.binding, &expected_binding) != LAPLACE_OBSERVATION_QUERY_OK ||
        !SameDigest(expected_binding, compiled.binding.binding_fingerprint) ||
        static_cast<std::uint64_t>(compiled.binding.maximum_results) >
            index.summary.maximum_candidate_records_per_expansion ||
        !SameDigest(
            compiled.search_program.boundary_id,
            index.summary.boundary_id) ||
        !SameDigest(
            compiled.search_program.evidence_epoch,
            index.summary.evidence_epoch) ||
        !SameDigest(
            compiled.initial_search_state.boundary_id,
            index.summary.boundary_id) ||
        !SameDigest(
            compiled.initial_search_state.evidence_epoch,
            index.summary.evidence_epoch) ||
        !SameDigest(
            compiled.initial_search_state.binding_fingerprint,
            compiled.binding.binding_fingerprint) ||
        !SameDigest(
            compiled.forward_program.result_contract_fingerprint,
            compiled.search_program.result_contract_fingerprint) ||
        compiled.search_program.budget.requested_path_count !=
            compiled.binding.maximum_results) {
        return false;
    }

    laplace_cognition_guidance_header header{};
    if (laplace_cognition_guidance_state_header(
            compiled.guidance_state, &header) != LAPLACE_COGNITION_GUIDANCE_OK ||
        !SameDigest(
            header.bindings_fingerprint,
            compiled.binding.binding_fingerprint) ||
        !SameDigest(header.evidence_epoch, index.summary.evidence_epoch) ||
        !SameDigest(
            header.result_contract_fingerprint,
            compiled.search_program.result_contract_fingerprint) ||
        laplace_cognition_guidance_state_obligation_count(
            compiled.guidance_state) != 1U) {
        return false;
    }
    laplace_cognition_obligation obligation{};
    if (laplace_cognition_guidance_state_obligation(
            compiled.guidance_state, 0U, &obligation) !=
            LAPLACE_COGNITION_GUIDANCE_OK ||
        !SameDigest(
            obligation.obligation_id,
            compiled.initial_search_state.obligation_fingerprint) ||
        !SameDigest(
            obligation.binding_fingerprint,
            compiled.binding.binding_fingerprint)) {
        return false;
    }
    return true;
}

bool ProjectionMatchesProvider(
    const laplace_cognition_observation_request_provider& provider,
    const laplace_cognition_query_projection& projection) {
    return provider.index != nullptr &&
        SameDigest(
            projection.binding_fingerprint,
            provider.binding.binding_fingerprint) &&
        SameDigest(
            projection.obligation_id,
            provider.initial_search_state.obligation_fingerprint) &&
        SameDigest(
            projection.context_fingerprint,
            provider.initial_search_state.context_fingerprint) &&
        SameDigest(
            projection.evidence_boundary,
            provider.search_program.boundary_id) &&
        SameDigest(
            projection.result_contract_fingerprint,
            provider.search_program.result_contract_fingerprint);
}

void FinalizeExpansionReceipt(
    const laplace_cognition_observation_request_provider& provider,
    const laplace_query_search_state* const states,
    const std::size_t state_count,
    const laplace_query_search_transition* const transitions,
    const std::size_t transition_count,
    laplace_query_search_expansion_receipt* const receipt) {
    blake3_hasher receipt_hasher{};
    StartDomain(
        &receipt_hasher,
        laplace_observation_query_detail::TransitionDomain);
    HashDigest(&receipt_hasher, provider.index->summary.index_fingerprint);
    HashDigest(&receipt_hasher, provider.request_fingerprint);
    HashDigest(&receipt_hasher, provider.binding.binding_fingerprint);
    HashU64(&receipt_hasher, receipt->frontier_state_count);
    HashU64(&receipt_hasher, receipt->rows_examined);
    HashU64(&receipt_hasher, receipt->index_plan_count);
    HashU64(&receipt_hasher, receipt->emitted_transition_count);
    HashU32(&receipt_hasher, receipt->limiting_disposition);
    for (std::size_t state_index = 0U; state_index < state_count; ++state_index) {
        HashDigest(&receipt_hasher, states[state_index].state_id);
    }
    for (std::size_t transition_index = 0U;
         transition_index < transition_count; ++transition_index) {
        HashDigest(&receipt_hasher, transitions[transition_index].transition_id);
    }
    Finish(&receipt_hasher, &receipt->receipt_id);
}

int ExpandCompiledObservationBatch(
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
        receipt == nullptr) {
        return 1;
    }
    auto& provider =
        *static_cast<laplace_cognition_observation_request_provider*>(opaque);
    if (provider.index == nullptr) return 2;
    auto& index = *provider.index;
    *transition_count = 0U;
    *receipt = laplace_query_search_expansion_receipt{};
    receipt->frontier_state_count = static_cast<std::uint64_t>(state_count);

    std::vector<
        std::pair<std::size_t, laplace_observation_query_detail::Candidate>> pending;
    try {
        for (std::size_t state_index = 0U;
             state_index < state_count; ++state_index) {
            (void)accumulated_costs[state_index];
            const auto& state = states[state_index];
            laplace_id128 source_entity_id{};
            if (!SameDigest(
                    state.binding_fingerprint,
                    provider.binding.binding_fingerprint) ||
                !SameDigest(state.boundary_id, index.summary.boundary_id) ||
                !SameDigest(state.evidence_epoch, index.summary.evidence_epoch) ||
                !laplace_observation_query_detail::ResolveCoordinate(
                    index, state.anchor_id, &source_entity_id)) {
                receipt->limiting_disposition =
                    LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
                pending.clear();
                break;
            }

            std::vector<laplace_observation_query_detail::Candidate> candidates;
            std::uint64_t rows_examined = 0U;
            std::uint64_t index_plans = 0U;
            bool limited = false;
            if (!laplace_observation_query_detail::GenerateCandidates(
                    index, provider.binding, source_entity_id, &candidates,
                    &rows_examined, &index_plans, &limited)) {
                return 3;
            }
            if (!Add(
                    receipt->rows_examined, rows_examined,
                    &receipt->rows_examined) ||
                !Add(
                    receipt->index_plan_count, index_plans,
                    &receipt->index_plan_count)) {
                return 4;
            }
            if (limited) {
                receipt->limiting_disposition =
                    LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED;
                pending.clear();
                break;
            }
            for (const auto& candidate : candidates) {
                pending.emplace_back(state_index, candidate);
            }
        }
    } catch (const std::bad_alloc&) {
        return 5;
    }

    if (pending.size() > transition_capacity) {
        pending.clear();
        receipt->limiting_disposition =
            LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED;
        FinalizeExpansionReceipt(
            provider, states, state_count, transitions, 0U, receipt);
        return 0;
    }

    for (std::size_t index_in_pending = 0U;
         index_in_pending < pending.size(); ++index_in_pending) {
        const auto state_index = pending[index_in_pending].first;
        laplace_id128 source_entity_id{};
        if (!laplace_observation_query_detail::ResolveCoordinate(
                index, states[state_index].anchor_id, &source_entity_id) ||
            !laplace_observation_query_detail::BuildTransition(
                index, provider.binding, states[state_index], source_entity_id,
                pending[index_in_pending].second,
                &transitions[index_in_pending])) {
            return 6;
        }
    }
    *transition_count = pending.size();
    receipt->emitted_transition_count =
        static_cast<std::uint64_t>(pending.size());
    FinalizeExpansionReceipt(
        provider, states, state_count, transitions, pending.size(), receipt);
    return 0;
}

int EnumerateCompiledObservationOperations(
    void* const opaque,
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_guidance_operation* const operations,
    const std::size_t operation_capacity,
    std::size_t* const operation_count,
    laplace_cognition_forward_enumeration_receipt* const receipt) {
    if (opaque == nullptr || state == nullptr || projections == nullptr ||
        projection_count == 0U || grant == nullptr || operations == nullptr ||
        operation_count == nullptr || receipt == nullptr) {
        return 1;
    }
    auto& provider =
        *static_cast<laplace_cognition_observation_request_provider*>(opaque);
    if (provider.index == nullptr) return 2;
    auto& index = *provider.index;

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 3;
    }
    std::size_t emitted = 0U;
    for (std::size_t projection_index = 0U;
         projection_index < projection_count; ++projection_index) {
        const auto& projection = projections[projection_index];
        if (!ProjectionMatchesProvider(provider, projection)) continue;
        if (emitted >= operation_capacity ||
            !MakeOperation(
                index, projection, provider.binding, &operations[emitted])) {
            return 4;
        }
        ++emitted;
    }
    *operation_count = emitted;
    *receipt = laplace_cognition_forward_enumeration_receipt{};
    receipt->state_id = state_id;
    laplace_observation_query_detail::ProjectionFingerprint(
        state_id, projections, projection_count,
        &receipt->projection_fingerprint);
    receipt->projected_query_count = static_cast<std::uint64_t>(projection_count);
    receipt->candidate_operation_count = static_cast<std::uint64_t>(emitted);

    blake3_hasher receipt_hasher{};
    StartDomain(
        &receipt_hasher,
        laplace_observation_query_detail::EnumerationReceiptDomain);
    HashDigest(&receipt_hasher, index.summary.index_fingerprint);
    HashDigest(&receipt_hasher, provider.request_fingerprint);
    HashDigest(&receipt_hasher, state_id);
    HashDigest(&receipt_hasher, receipt->projection_fingerprint);
    HashU64(&receipt_hasher, receipt->projected_query_count);
    HashU64(&receipt_hasher, receipt->candidate_operation_count);
    for (std::size_t operation_index = 0U;
         operation_index < emitted; ++operation_index) {
        HashDigest(&receipt_hasher, operations[operation_index].operation_id);
    }
    Finish(&receipt_hasher, &receipt->receipt_id);
    (void)grant;
    return 0;
}

int ExecuteCompiledObservationOperation(
    void* const opaque,
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_guidance_operation* const operation,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_resolution* const resolutions,
    const std::size_t resolution_capacity,
    std::size_t* const resolution_count,
    laplace_cognition_forward_execution_receipt* const receipt) {
    if (opaque == nullptr || state == nullptr || operation == nullptr ||
        projections == nullptr || projection_count == 0U || grant == nullptr ||
        resolutions == nullptr || resolution_capacity == 0U ||
        resolution_count == nullptr || receipt == nullptr) {
        return 1;
    }
    auto& provider =
        *static_cast<laplace_cognition_observation_request_provider*>(opaque);
    if (provider.index == nullptr) return 2;
    auto& index = *provider.index;

    const laplace_cognition_query_projection* projection = nullptr;
    if (!FindProjection(
            projections, projection_count, operation->target_obligation_id,
            &projection) ||
        projection == nullptr || !ProjectionMatchesProvider(provider, *projection)) {
        return 3;
    }

    laplace_cognition_guidance_operation expected_operation{};
    if (!MakeOperation(
            index, *projection, provider.binding, &expected_operation) ||
        !SameDigest(expected_operation.operation_id, operation->operation_id)) {
        return 4;
    }

    laplace_query_search_provider_v1 search_provider{};
    search_provider.state = &provider;
    search_provider.expand_batch = ExpandCompiledObservationBatch;
    search_provider.abi_major = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR;
    search_provider.abi_minor = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR;

    laplace_query_search_result* search_result = nullptr;
    laplace_query_search_receipt search_receipt{};
    const auto search_status = laplace_query_search_execute(
        &provider.search_program, &provider.initial_search_state, 1U,
        &search_provider, &search_result, &search_receipt);
    if (search_status != LAPLACE_QUERY_SEARCH_OK || search_result == nullptr) {
        laplace_query_search_result_destroy(&search_result);
        return 5;
    }

    std::uint64_t execution_cost = 1U;
    if (!Add(execution_cost, search_receipt.rows_examined, &execution_cost) ||
        !Add(
            execution_cost, search_receipt.transition_record_count,
            &execution_cost) ||
        execution_cost > grant->remaining_resource_cost ||
        search_receipt.io_operations > grant->remaining_io_operations ||
        search_receipt.database_operations >
            grant->remaining_database_operations) {
        laplace_query_search_result_destroy(&search_result);
        return 6;
    }

    blake3_hasher result_hasher{};
    StartDomain(&result_hasher, SearchResultDomain);
    HashDigest(&result_hasher, index.summary.index_fingerprint);
    HashDigest(&result_hasher, provider.request_fingerprint);
    HashDigest(&result_hasher, provider.search_program.program_id);
    HashDigest(&result_hasher, search_receipt.receipt_id);
    HashDigest(&result_hasher, projection->obligation_id);
    HashDigest(&result_hasher, provider.binding.binding_fingerprint);
    HashU32(&result_hasher, search_receipt.disposition);
    const std::size_t path_count =
        laplace_query_search_result_path_count(search_result);
    HashU64(&result_hasher, static_cast<std::uint64_t>(path_count));
    for (std::size_t path_index = 0U; path_index < path_count; ++path_index) {
        laplace_query_search_path_summary path{};
        if (laplace_query_search_result_path(
                search_result, path_index, &path) != LAPLACE_QUERY_SEARCH_OK) {
            laplace_query_search_result_destroy(&search_result);
            return 7;
        }
        HashDigest(&result_hasher, path.path_id);
        HashDigest(&result_hasher, path.terminal_state_id);
        HashU64(&result_hasher, path.total_cost);
        HashU64(&result_hasher, path.transition_count);
    }
    laplace_digest256 result_fingerprint{};
    Finish(&result_hasher, &result_fingerprint);

    blake3_hasher resolution_receipt_hasher{};
    StartDomain(&resolution_receipt_hasher, ResolutionReceiptDomain);
    HashDigest(&resolution_receipt_hasher, provider.request_fingerprint);
    HashDigest(&resolution_receipt_hasher, result_fingerprint);
    HashDigest(&resolution_receipt_hasher, search_receipt.receipt_id);
    HashDigest(&resolution_receipt_hasher, operation->operation_id);
    laplace_digest256 resolution_receipt{};
    Finish(&resolution_receipt_hasher, &resolution_receipt);

    resolutions[0] = laplace_cognition_resolution{};
    resolutions[0].obligation_id = projection->obligation_id;
    resolutions[0].receipt_id = resolution_receipt;
    if (path_count != 0U &&
        search_receipt.disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE) {
        resolutions[0].value_id = result_fingerprint;
        resolutions[0].disposition = LAPLACE_COGNITION_OBLIGATION_SATISFIED;
    } else {
        resolutions[0].disposition =
            ResolutionDisposition(search_receipt.disposition);
    }
    *resolution_count = 1U;

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
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

    blake3_hasher receipt_hasher{};
    StartDomain(&receipt_hasher, ExecutionReceiptDomain);
    HashDigest(&receipt_hasher, index.summary.index_fingerprint);
    HashDigest(&receipt_hasher, provider.request_fingerprint);
    HashDigest(&receipt_hasher, provider.search_program.program_id);
    HashDigest(&receipt_hasher, receipt->state_id);
    HashDigest(&receipt_hasher, receipt->operation_id);
    HashDigest(&receipt_hasher, receipt->result_fingerprint);
    HashDigest(&receipt_hasher, search_receipt.receipt_id);
    HashDigest(&receipt_hasher, resolution_receipt);
    HashU64(&receipt_hasher, receipt->resource_cost);
    Finish(&receipt_hasher, &receipt->receipt_id);

    laplace_query_search_result_destroy(&search_result);
    return 0;
}

}  // namespace laplace_cognition_observation_request_provider_detail

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_request_cognition_provider(
    laplace_observation_query_index* const index,
    const laplace_cognition_observation_compiled_request* const compiled,
    laplace_cognition_observation_request_provider** const provider_state,
    laplace_cognition_forward_provider_v1* const provider) {
    using namespace laplace_cognition_observation_request_provider_detail;
    if (index == nullptr || compiled == nullptr || provider_state == nullptr ||
        provider == nullptr || *provider_state != nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    if (!CompiledStateValid(*index, *compiled)) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_PROVIDER_FAILURE;
    }
    auto* const created =
        new (std::nothrow) laplace_cognition_observation_request_provider{};
    if (created == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_MEMORY_FAILURE;
    }
    created->index = index;
    created->request_fingerprint = compiled->request_fingerprint;
    created->binding = compiled->binding;
    created->initial_search_state = compiled->initial_search_state;
    created->search_program = compiled->search_program;

    *provider = laplace_cognition_forward_provider_v1{};
    provider->state = created;
    provider->enumerate = EnumerateCompiledObservationOperations;
    provider->execute = ExecuteCompiledObservationOperation;
    provider->abi_major = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR;
    *provider_state = created;
    return LAPLACE_COGNITION_OBSERVATION_REQUEST_OK;
}

extern "C" void
laplace_cognition_observation_request_provider_destroy(
    laplace_cognition_observation_request_provider** const provider_state) {
    if (provider_state == nullptr || *provider_state == nullptr) return;
    delete *provider_state;
    *provider_state = nullptr;
}

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_request_execute(
    laplace_observation_query_index* const index,
    const laplace_cognition_observation_request* const request,
    laplace_cognition_forward_result** const result,
    laplace_cognition_forward_receipt* const receipt) {
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) *receipt = laplace_cognition_forward_receipt{};
    if (index == nullptr || request == nullptr || result == nullptr ||
        receipt == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }

    laplace_cognition_observation_compiled_request compiled{};
    auto status = laplace_cognition_observation_request_compile(request, &compiled);
    if (status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) return status;

    laplace_cognition_observation_request_provider* provider_state = nullptr;
    laplace_cognition_forward_provider_v1 provider{};
    status = laplace_cognition_observation_request_cognition_provider(
        index, &compiled, &provider_state, &provider);
    if (status == LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        const auto forward_status = laplace_cognition_forward_pass_execute(
            &compiled.forward_program,
            compiled.guidance_state,
            &provider,
            result,
            receipt);
        if (forward_status != LAPLACE_COGNITION_FORWARD_OK) {
            laplace_cognition_forward_result_destroy(result);
            *receipt = laplace_cognition_forward_receipt{};
            status = LAPLACE_COGNITION_OBSERVATION_REQUEST_EXECUTION_FAILURE;
        }
    }

    laplace_cognition_observation_request_provider_destroy(&provider_state);
    laplace_cognition_observation_compiled_request_destroy(&compiled);
    return status;
}
