namespace {

laplace_cognition_forward_status ForwardPass::Finalize(
    const std::uint32_t disposition) {
    if (result_->final_state == nullptr) {
        return LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE;
    }
    if (laplace_cognition_guidance_state_identify(
            result_->final_state, &receipt_->final_state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE;
    }
    if (laplace_cognition_guidance_completion(
            result_->final_state, &receipt_->final_completion,
            &receipt_->final_remaining_required_count) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE;
    }
    receipt_->disposition = disposition;
    receipt_->flags = program_.flags;
    receipt_->version = LAPLACE_COGNITION_FORWARD_VERSION;
    receipt_->status = LAPLACE_COGNITION_FORWARD_OK;
    Finish(&trace_hasher_, &receipt_->layer_trace_fingerprint);

    blake3_hasher output_hasher{};
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, LAPLACE_COGNITION_FORWARD_OUTPUT_DOMAIN,
        sizeof(LAPLACE_COGNITION_FORWARD_OUTPUT_DOMAIN) - 1U);
    HashDigest(&output_hasher, receipt_->program_fingerprint);
    HashDigest(&output_hasher, receipt_->initial_state_id);
    HashDigest(&output_hasher, receipt_->final_state_id);
    HashDigest(&output_hasher, receipt_->layer_trace_fingerprint);
    HashU64(&output_hasher, receipt_->layer_count);
    HashU64(&output_hasher, receipt_->provider_call_count);
    HashU64(&output_hasher, receipt_->projected_query_count);
    HashU64(&output_hasher, receipt_->candidate_operation_count);
    HashU64(&output_hasher, receipt_->resolution_count);
    HashU64(&output_hasher, receipt_->resource_cost);
    HashU64(&output_hasher, receipt_->io_operations);
    HashU64(&output_hasher, receipt_->database_operations);
    HashU64(&output_hasher, receipt_->final_remaining_required_count);
    HashU32(&output_hasher, receipt_->final_completion);
    HashU32(&output_hasher, receipt_->disposition);
    Finish(&output_hasher, &receipt_->output_fingerprint);

    blake3_hasher receipt_hasher{};
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_COGNITION_FORWARD_RECEIPT_DOMAIN,
        sizeof(LAPLACE_COGNITION_FORWARD_RECEIPT_DOMAIN) - 1U);
    HashDigest(&receipt_hasher, receipt_->program_fingerprint);
    HashDigest(&receipt_hasher, receipt_->initial_state_id);
    HashDigest(&receipt_hasher, receipt_->final_state_id);
    HashDigest(&receipt_hasher, receipt_->layer_trace_fingerprint);
    HashDigest(&receipt_hasher, receipt_->output_fingerprint);
    HashU64(&receipt_hasher, receipt_->layer_count);
    HashU64(&receipt_hasher, receipt_->provider_call_count);
    HashU64(&receipt_hasher, receipt_->projected_query_count);
    HashU64(&receipt_hasher, receipt_->candidate_operation_count);
    HashU64(&receipt_hasher, receipt_->resolution_count);
    HashU64(&receipt_hasher, receipt_->resource_cost);
    HashU64(&receipt_hasher, receipt_->io_operations);
    HashU64(&receipt_hasher, receipt_->database_operations);
    HashU64(&receipt_hasher, receipt_->final_remaining_required_count);
    HashU32(&receipt_hasher, receipt_->final_completion);
    HashU32(&receipt_hasher, receipt_->disposition);
    HashU32(&receipt_hasher, receipt_->status);
    HashU32(&receipt_hasher, receipt_->version);
    HashU32(&receipt_hasher, receipt_->flags);
    Finish(&receipt_hasher, &receipt_->receipt_id);
    return LAPLACE_COGNITION_FORWARD_OK;
}

}  // namespace

extern "C" laplace_cognition_forward_status
laplace_cognition_forward_pass_execute(
    const laplace_cognition_forward_program* const program,
    const laplace_cognition_guidance_state* const initial_state,
    const laplace_cognition_forward_provider_v1* const provider,
    laplace_cognition_forward_result** const result,
    laplace_cognition_forward_receipt* const receipt) {
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) {
        *receipt = laplace_cognition_forward_receipt{};
        receipt->version = LAPLACE_COGNITION_FORWARD_VERSION;
    }
    if (program == nullptr || initial_state == nullptr || provider == nullptr ||
        provider->enumerate == nullptr || provider->execute == nullptr ||
        result == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_FORWARD_INVALID_ARGUMENT;
    }
    if (!ProgramValid(*program) ||
        provider->abi_major != LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR ||
        provider->abi_minor > LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR ||
        provider->flags != 0U || provider->reserved != 0U) {
        receipt->status = LAPLACE_COGNITION_FORWARD_PROGRAM_INVALID;
        return LAPLACE_COGNITION_FORWARD_PROGRAM_INVALID;
    }
    laplace_cognition_guidance_header guidance_header{};
    if (laplace_cognition_guidance_state_header(
            initial_state, &guidance_header) != LAPLACE_COGNITION_GUIDANCE_OK ||
        !Same(
            guidance_header.result_contract_fingerprint,
            program->result_contract_fingerprint)) {
        receipt->status = LAPLACE_COGNITION_FORWARD_PROGRAM_INVALID;
        return LAPLACE_COGNITION_FORWARD_PROGRAM_INVALID;
    }
    HashProgram(*program, &receipt->program_fingerprint);
    try {
        std::unique_ptr<laplace_cognition_forward_result> owned(
            new laplace_cognition_forward_result{});
        ForwardPass pass(*program, *provider, owned.get(), receipt);
        const auto status = pass.Run(initial_state);
        if (status != LAPLACE_COGNITION_FORWARD_OK) {
            receipt->status = static_cast<std::uint32_t>(status);
            return status;
        }
        *result = owned.release();
        return LAPLACE_COGNITION_FORWARD_OK;
    } catch (const std::bad_alloc&) {
        receipt->status = LAPLACE_COGNITION_FORWARD_MEMORY_FAILURE;
        return LAPLACE_COGNITION_FORWARD_MEMORY_FAILURE;
    }
}

extern "C" void laplace_cognition_forward_result_destroy(
    laplace_cognition_forward_result** const result) {
    if (result != nullptr) {
        delete *result;
        *result = nullptr;
    }
}

extern "C" size_t laplace_cognition_forward_result_layer_count(
    const laplace_cognition_forward_result* const result) {
    return result == nullptr ? 0U : result->layers.size();
}

extern "C" laplace_cognition_forward_status
laplace_cognition_forward_result_layer(
    const laplace_cognition_forward_result* const result,
    const size_t layer_index,
    laplace_cognition_forward_layer_receipt* const layer) {
    if (result == nullptr || layer == nullptr) {
        return LAPLACE_COGNITION_FORWARD_INVALID_ARGUMENT;
    }
    if (layer_index >= result->layers.size()) {
        return LAPLACE_COGNITION_FORWARD_RANGE;
    }
    *layer = result->layers[layer_index];
    return LAPLACE_COGNITION_FORWARD_OK;
}

extern "C" laplace_cognition_forward_status
laplace_cognition_forward_result_final_state_clone(
    const laplace_cognition_forward_result* const result,
    laplace_cognition_guidance_state** const state) {
    if (state != nullptr) *state = nullptr;
    if (result == nullptr || result->final_state == nullptr || state == nullptr) {
        return LAPLACE_COGNITION_FORWARD_INVALID_ARGUMENT;
    }
    return laplace_cognition_guidance_state_clone(result->final_state, state) ==
            LAPLACE_COGNITION_GUIDANCE_OK
        ? LAPLACE_COGNITION_FORWARD_OK
        : LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE;
}
