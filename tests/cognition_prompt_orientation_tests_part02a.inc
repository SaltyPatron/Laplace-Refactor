laplace_cognition_prompt_conversation_request UnboundConversationPolicy() {
    laplace_cognition_prompt_conversation_request request{};
    request.cognition_policy.evidence_boundary = Digest(0xE0U);
    request.cognition_policy.evidence_epoch = Digest(0xE1U);
    request.cognition_policy.authority_id = Digest(0xE2U);
    request.cognition_policy.result_contract_fingerprint = Digest(0xE3U);
    /* Deliberately wrong/narrow for interpretation. The unbound prompt path must
     * orient over the full provider estate and derive the relation families that
     * actually tug back instead of trusting this caller mask. */
    request.cognition_policy.relation_mask = LAPLACE_OBSERVATION_QUERY_CONTAINER;
    request.cognition_policy.maximum_results = 1U;
    request.cognition_policy.request_flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    request.cognition_policy.version = LAPLACE_COGNITION_TURN_POLICY_VERSION;
    request.cognition_policy.search_budget.max_expanded_states = 32U;
    request.cognition_policy.search_budget.max_transition_records = 128U;
    request.cognition_policy.search_budget.max_emitted_states = 128U;
    request.cognition_policy.search_budget.max_frontier_states = 64U;
    request.cognition_policy.search_budget.max_memory_bytes = UINT64_C(2097152);
    request.cognition_policy.search_budget.max_io_operations = 32U;
    request.cognition_policy.search_budget.max_database_operations = 32U;
    request.cognition_policy.search_budget.max_provider_calls = 32U;
    request.cognition_policy.search_budget.max_depth = 3U;
    request.cognition_policy.search_budget.requested_path_count = 1U;
    request.cognition_policy.search_budget.frontier_batch_width = 16U;
    request.cognition_policy.search_budget.transition_batch_capacity = 128U;
    request.cognition_policy.forward_limits.max_layers = 2U;
    request.cognition_policy.forward_limits.max_provider_calls = 4U;
    request.cognition_policy.forward_limits.max_projected_queries = 4U;
    request.cognition_policy.forward_limits.max_candidate_operations = 8U;
    request.cognition_policy.forward_limits.max_resolutions = 2U;
    request.cognition_policy.forward_limits.max_resource_cost = 512U;
    request.cognition_policy.forward_limits.max_io_operations = 32U;
    request.cognition_policy.forward_limits.max_database_operations = 32U;
    request.cognition_policy.forward_limits.candidate_operation_capacity = 4U;
    request.cognition_policy.forward_limits.resolution_capacity = 2U;

    request.realization_policy.modality_id =
        Codepoint(static_cast<std::uint32_t>('T'));
    request.realization_policy.language_id =
        Codepoint(static_cast<std::uint32_t>('E'));
    request.realization_policy.realization_recipe_epoch = Digest(0xE4U);
    request.realization_policy.maximum_candidates = 1U;
    request.realization_policy.flags =
        LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT;
    request.realization_policy.version =
        LAPLACE_COGNITION_CONVERSATION_REALIZATION_POLICY_VERSION;

    request.materialization.maximum_nodes = 8U;
    request.materialization.maximum_trajectory_carriers = 8U;
    request.materialization.maximum_output_bytes = 64U;
    request.materialization.maximum_depth = 4U;
    request.materialization.version = LAPLACE_COGNITION_MATERIALIZATION_VERSION;

    request.discourse_roots.active_entity_set_fingerprint = Digest(0xE5U);
    request.discourse_roots.flags =
        LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES;
    request.discourse_roots.version =
        LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_VERSION;
    request.version = LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION;
    return request;
}
