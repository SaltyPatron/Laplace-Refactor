extern "C" laplace_cognition_coupling_status
laplace_cognition_coupling_execute(
    const laplace_cognition_coupling_request* request,
    const laplace_cognition_observation_candidate_provider_v1* provider,
    laplace_cognition_coupling_result** result,
    laplace_cognition_coupling_receipt* receipt) {
    using namespace laplace_cognition_coupling_detail;
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) *receipt = laplace_cognition_coupling_receipt{};
    if (request == nullptr || provider == nullptr || result == nullptr ||
        receipt == nullptr || !RequestValid(*request)) {
        return LAPLACE_COGNITION_COUPLING_INVALID_ARGUMENT;
    }
    if (!ProviderValid(*provider)) {
        return LAPLACE_COGNITION_COUPLING_INVALID_PROVIDER;
    }
    if (!MemoryBoundValid(*request) ||
        request->seed_count > request->maximum_frontier_states) {
        return LAPLACE_COGNITION_COUPLING_EXHAUSTED;
    }

    std::unique_ptr<laplace_cognition_coupling_result> owned;
    try {
        owned.reset(new laplace_cognition_coupling_result{});
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_COUPLING_MEMORY_FAILURE;
    }
    receipt->provider_fingerprint = provider->provider_fingerprint;
    receipt->seed_set_fingerprint = SeedSetFingerprint(*request);
    receipt->version = LAPLACE_COGNITION_COUPLING_VERSION;

    laplace_observation_query_binding binding{};
    binding.anchor_entity_id = request->seeds[0].entity_id;
    binding.relation_mask = request->relation_mask;
    binding.maximum_results = 1U;
    if (laplace_observation_query_binding_identify(
            &binding,
            &binding.binding_fingerprint) != LAPLACE_OBSERVATION_QUERY_OK) {
        return LAPLACE_COGNITION_COUPLING_INVALID_ARGUMENT;
    }
