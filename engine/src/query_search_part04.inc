        if (boundary_complete) {
            receipt_->flags |= LAPLACE_QUERY_SEARCH_RECEIPT_BOUNDARY_COMPLETE;
        }
        const bool enough_paths =
            result->paths.size() >= program_.budget.requested_path_count;
        if (enough_paths && boundary_complete && !resource_exhausted_) {
            receipt_->disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE;
            if ((program_.flags & LAPLACE_QUERY_SEARCH_FLAG_REQUEST_OPTIMAL) != 0U) {
#if defined(LAPLACE_TEST_QUERY_SEARCH_PROMOTE_UPPER_BOUND)
                receipt_->flags |= LAPLACE_QUERY_SEARCH_RECEIPT_OPTIMAL_CERTIFIED;
#else
                receipt_->flags |= LAPLACE_QUERY_SEARCH_RECEIPT_OPTIMAL_CERTIFIED;
#endif
            }
        } else if (!result->paths.empty()) {
#if defined(LAPLACE_TEST_QUERY_SEARCH_PROMOTE_UPPER_BOUND)
            receipt_->disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE;
            receipt_->flags |= LAPLACE_QUERY_SEARCH_RECEIPT_OPTIMAL_CERTIFIED;
#else
            receipt_->disposition = enough_paths
                ? LAPLACE_QUERY_SEARCH_DISPOSITION_UPPER_BOUND
                : LAPLACE_QUERY_SEARCH_DISPOSITION_PARTIAL;
#endif
        } else if (resource_exhausted_) {
            receipt_->disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED;
        } else if (!boundary_complete) {
            receipt_->disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_INCOMPLETE_BOUNDARY;
        } else if (provider_disposition_ != 0U) {
            receipt_->disposition = provider_disposition_;
        } else {
            receipt_->disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_NO_ADMISSIBLE_PATH;
        }
        receipt_->path_count = static_cast<std::uint32_t>(result->paths.size());
        HashU32(&execution_hasher_, receipt_->disposition);
        HashU32(&execution_hasher_, receipt_->path_count);
        Finish(&execution_hasher_, &receipt_->output_fingerprint);
    }
};

void FinalizeReceipt(laplace_query_search_receipt* const receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_QUERY_SEARCH_RECEIPT_DOMAIN,
        sizeof(LAPLACE_QUERY_SEARCH_RECEIPT_DOMAIN) - 1U);
    HashDigest(&hasher, receipt->program_fingerprint);
    HashDigest(&hasher, receipt->input_fingerprint);
    HashDigest(&hasher, receipt->output_fingerprint);
    HashU64(&hasher, receipt->expanded_state_count);
    HashU64(&hasher, receipt->transition_record_count);
    HashU64(&hasher, receipt->emitted_state_count);
    HashU64(&hasher, receipt->reopened_state_count);
    HashU64(&hasher, receipt->pruned_state_count);
    HashU64(&hasher, receipt->rows_examined);
    HashU64(&hasher, receipt->index_plan_count);
    HashU64(&hasher, receipt->crossing_count);
    HashU64(&hasher, receipt->io_operations);
    HashU64(&hasher, receipt->database_operations);
    HashU64(&hasher, receipt->provider_call_count);
    HashU64(&hasher, receipt->estimated_peak_memory_bytes);
    HashU32(&hasher, receipt->path_count);
    HashU32(&hasher, receipt->disposition);
    HashU32(&hasher, receipt->flags);
    HashU32(&hasher, receipt->version);
    HashU32(&hasher, receipt->status);
    Finish(&hasher, &receipt->receipt_id);
}

}  // namespace

extern "C" laplace_query_search_status laplace_query_search_execute(
    const laplace_query_search_program* const program,
    const laplace_query_search_state* const initial_states,
    const size_t initial_state_count,
    const laplace_query_search_provider_v1* const provider,
    laplace_query_search_result** const output_result,
    laplace_query_search_receipt* const receipt) {
    if (output_result != nullptr) *output_result = nullptr;
    if (receipt != nullptr) {
        *receipt = laplace_query_search_receipt{};
        receipt->version = LAPLACE_QUERY_SEARCH_VERSION;
    }
    if (program == nullptr || initial_states == nullptr ||
        initial_state_count == 0U || provider == nullptr ||
        provider->expand_batch == nullptr || output_result == nullptr ||
        receipt == nullptr) {
        return LAPLACE_QUERY_SEARCH_INVALID_ARGUMENT;
    }
    if (!ProgramValid(*program) ||
        provider->abi_major != LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR ||
        provider->abi_minor > LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR ||
        provider->flags != 0U || provider->reserved != 0U) {
        return LAPLACE_QUERY_SEARCH_PROGRAM_INVALID;
    }
    HashProgram(*program, &receipt->program_fingerprint);
    blake3_hasher input_hasher{};
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(
        &input_hasher, LAPLACE_QUERY_SEARCH_INPUT_DOMAIN,
        sizeof(LAPLACE_QUERY_SEARCH_INPUT_DOMAIN) - 1U);
    HashDigest(&input_hasher, receipt->program_fingerprint);
    HashU64(&input_hasher, static_cast<std::uint64_t>(initial_state_count));
    for (std::size_t index = 0U; index < initial_state_count; ++index) {
        HashState(&input_hasher, initial_states[index]);
    }
    Finish(&input_hasher, &receipt->input_fingerprint);
    try {
        std::unique_ptr<laplace_query_search_result> result(
            new laplace_query_search_result{});
        Search search(*program, *provider, receipt);
        const auto status = search.Run(initial_states, initial_state_count, result.get());
        receipt->status = static_cast<std::uint32_t>(status);
        if (status != LAPLACE_QUERY_SEARCH_OK) {
            return status;
        }
        FinalizeReceipt(receipt);
        *output_result = result.release();
        return LAPLACE_QUERY_SEARCH_OK;
    } catch (const std::bad_alloc&) {
        receipt->status = LAPLACE_QUERY_SEARCH_MEMORY_FAILURE;
        return LAPLACE_QUERY_SEARCH_MEMORY_FAILURE;
    }
}

extern "C" size_t laplace_query_search_result_path_count(
    const laplace_query_search_result* const result) {
    return result == nullptr ? 0U : result->paths.size();
}

extern "C" laplace_query_search_status laplace_query_search_result_path(
    const laplace_query_search_result* const result,
    const size_t path_index,
    laplace_query_search_path_summary* const summary) {
    if (result == nullptr || summary == nullptr) {
        return LAPLACE_QUERY_SEARCH_INVALID_ARGUMENT;
    }
    if (path_index >= result->paths.size()) {
        return LAPLACE_QUERY_SEARCH_RESULT_RANGE;
    }
    *summary = result->paths[path_index].summary;
    return LAPLACE_QUERY_SEARCH_OK;
}

extern "C" laplace_query_search_status laplace_query_search_result_path_steps(
    const laplace_query_search_result* const result,
    const size_t path_index,
    laplace_query_search_transition* const transitions,
    const size_t transition_capacity,
    size_t* const transition_count) {
    if (transition_count != nullptr) *transition_count = 0U;
    if (result == nullptr || transition_count == nullptr) {
        return LAPLACE_QUERY_SEARCH_INVALID_ARGUMENT;
    }
    if (path_index >= result->paths.size()) {
        return LAPLACE_QUERY_SEARCH_RESULT_RANGE;
    }
    const auto& path = result->paths[path_index].transitions;
    *transition_count = path.size();
    if (path.empty()) return LAPLACE_QUERY_SEARCH_OK;
    if (transitions == nullptr || transition_capacity < path.size()) {
        return LAPLACE_QUERY_SEARCH_RESULT_RANGE;
    }
    std::copy(path.begin(), path.end(), transitions);
    return LAPLACE_QUERY_SEARCH_OK;
}

extern "C" void laplace_query_search_result_destroy(
    laplace_query_search_result** const result) {
    if (result != nullptr) {
        delete *result;
        *result = nullptr;
    }
}
