namespace laplace_cognition_observation_request_provider_detail {

using laplace_observation_query_detail::Add;
using laplace_observation_query_detail::ExecutionReceiptDomain;
using laplace_observation_query_detail::FindBinding;
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
    const auto* const indexed_binding =
        FindBinding(index, compiled.binding.binding_fingerprint);
    if (indexed_binding == nullptr ||
        !SameDigest(
            indexed_binding->binding_fingerprint,
            compiled.binding.binding_fingerprint) ||
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
    if (opaque == nullptr) return 1;
    auto& provider =
        *static_cast<laplace_cognition_observation_request_provider*>(opaque);
    return laplace_observation_query_detail::EnumerateObservationOperations(
        provider.index, state, projections, projection_count, grant,
        operations, operation_capacity, operation_count, receipt);
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
            &projection)) {
        return 3;
    }
    const auto* const binding = FindBinding(
        index, projection->binding_fingerprint);
    if (binding == nullptr ||
        !SameDigest(
            binding->binding_fingerprint,
            provider.binding.binding_fingerprint) ||
        !SameDigest(
            projection->obligation_id,
            provider.initial_search_state.obligation_fingerprint) ||
        !SameDigest(
            projection->context_fingerprint,
            provider.initial_search_state.context_fingerprint) ||
        !SameDigest(
            projection->evidence_boundary,
            provider.search_program.boundary_id) ||
        !SameDigest(
            projection->result_contract_fingerprint,
            provider.search_program.result_contract_fingerprint) ||
        !SameDigest(
            provider.search_program.boundary_id,
            index.summary.boundary_id) ||
        !SameDigest(
            provider.search_program.evidence_epoch,
            index.summary.evidence_epoch)) {
        return 4;
    }

    laplace_cognition_guidance_operation expected_operation{};
    if (!MakeOperation(index, *projection, *binding, &expected_operation) ||
        !SameDigest(expected_operation.operation_id, operation->operation_id)) {
        return 5;
    }

    laplace_query_search_provider_v1 search_provider{};
    search_provider.state = &index;
    search_provider.expand_batch =
        laplace_observation_query_detail::ExpandObservationBatch;
    search_provider.abi_major = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR;
    search_provider.abi_minor = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR;

    laplace_query_search_result* search_result = nullptr;
    laplace_query_search_receipt search_receipt{};
    const auto search_status = laplace_query_search_execute(
        &provider.search_program, &provider.initial_search_state, 1U,
        &search_provider, &search_result, &search_receipt);
    if (search_status != LAPLACE_QUERY_SEARCH_OK || search_result == nullptr) {
        laplace_query_search_result_destroy(&search_result);
        return 6;
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
        return 7;
    }

    blake3_hasher result_hasher{};
    StartDomain(&result_hasher, SearchResultDomain);
    HashDigest(&result_hasher, index.summary.index_fingerprint);
    HashDigest(&result_hasher, provider.request_fingerprint);
    HashDigest(&result_hasher, provider.search_program.program_id);
    HashDigest(&result_hasher, search_receipt.receipt_id);
    HashDigest(&result_hasher, projection->obligation_id);
    HashDigest(&result_hasher, binding->binding_fingerprint);
    HashU32(&result_hasher, search_receipt.disposition);
    const std::size_t path_count =
        laplace_query_search_result_path_count(search_result);
    HashU64(&result_hasher, static_cast<std::uint64_t>(path_count));
    for (std::size_t path_index = 0U; path_index < path_count; ++path_index) {
        laplace_query_search_path_summary path{};
        if (laplace_query_search_result_path(
                search_result, path_index, &path) != LAPLACE_QUERY_SEARCH_OK) {
            laplace_query_search_result_destroy(&search_result);
            return 8;
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
        return 9;
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
