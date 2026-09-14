struct CouplingOwner final {
    ~CouplingOwner() {
        laplace_cognition_coupling_result_destroy(&value);
    }
    laplace_cognition_coupling_result* value{};
};

bool IsSeedEntity(
    const std::vector<laplace_cognition_coupling_seed>& seeds,
    const laplace_id128& entity_id) {
    for (const auto& seed : seeds) {
        if (std::memcmp(
                seed.entity_id.bytes,
                entity_id.bytes,
                sizeof(entity_id.bytes)) == 0) {
            return true;
        }
    }
    return false;
}

OrientationStatus Orient(
    const laplace_cognition_prompt_admission& admission,
    const laplace_cognition_turn_policy& policy,
    const laplace_cognition_observation_candidate_provider_v1& provider,
    OrientationOutput* const output) {
    if (output == nullptr) return OrientationStatus::Invalid;
    *output = OrientationOutput{};

    std::vector<laplace_cognition_coupling_seed> seeds;
    const auto seed_status = BuildSeeds(admission, policy, &seeds);
    if (seed_status != OrientationStatus::Ok) return seed_status;

    laplace_cognition_coupling_request request{};
    request.seeds = seeds.data();
    request.seed_count = seeds.size();
    request.world_id = admission.view.turn.world_id;
    request.time_fingerprint = admission.view.turn.time_fingerprint;
    request.context_fingerprint = admission.view.turn.context_fingerprint;
    request.evidence_boundary = policy.evidence_boundary;
    request.evidence_epoch = policy.evidence_epoch;
    request.authority_id = policy.authority_id;
    request.maximum_witnesses = policy.search_budget.max_transition_records;
    request.maximum_frontier_states = policy.search_budget.max_frontier_states;
    request.maximum_provider_calls = policy.search_budget.max_provider_calls;
    request.maximum_memory_bytes = policy.search_budget.max_memory_bytes;
    request.maximum_depth = policy.search_budget.max_depth;
    request.frontier_batch_width = policy.search_budget.frontier_batch_width;
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_RELATION_MASK;
    request.version = LAPLACE_COGNITION_COUPLING_VERSION;

    CouplingOwner field;
    const auto coupling_status = laplace_cognition_coupling_execute(
        &request,
        &provider,
        &field.value,
        &output->receipt);
    if (coupling_status == LAPLACE_COGNITION_COUPLING_EXHAUSTED) {
        return OrientationStatus::Exhausted;
    }
    if (coupling_status != LAPLACE_COGNITION_COUPLING_OK ||
        field.value == nullptr) {
        return OrientationStatus::Invalid;
    }
    if (output->receipt.disposition == LAPLACE_COGNITION_COUPLING_NO_RESPONSE) {
        return OrientationStatus::NoResponse;
    }
    if (output->receipt.disposition != LAPLACE_COGNITION_COUPLING_COMPLETE) {
        return OrientationStatus::Incomplete;
    }

    bool have_best = false;
    bool ambiguous = false;
    laplace_cognition_coupling_response best{};
    const std::size_t response_count =
        laplace_cognition_coupling_response_count(field.value);
    for (std::size_t index = 0U; index < response_count; ++index) {
        laplace_cognition_coupling_response current{};
        if (laplace_cognition_coupling_response_read(
                field.value,
                index,
                &current) != LAPLACE_COGNITION_COUPLING_OK) {
            return OrientationStatus::Invalid;
        }
        if (!EligibleResponse(current) || IsSeedEntity(seeds, current.entity_id)) {
            continue;
        }
        if (!have_best) {
            best = current;
            have_best = true;
            ambiguous = false;
            continue;
        }
        const int comparison = CompareResponse(current, best);
        if (comparison > 0) {
            best = current;
            ambiguous = false;
        } else if (comparison == 0) {
            ambiguous = true;
        }
    }
    if (!have_best) return OrientationStatus::NoResponse;
    if (ambiguous) return OrientationStatus::Ambiguous;

    output->selected = best;
    output->turn = admission.view.turn;
    output->policy = policy;
    output->policy.goal_entity_id = best.entity_id;
    output->policy.relation_mask =
        best.relation_family_mask | LAPLACE_OBSERVATION_QUERY_CONSTITUENT;
    output->policy.request_flags &=
        ~LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    output->policy.request_flags |=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT;

    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher,
        ContextDomain,
        sizeof(ContextDomain) - 1U);
    OrientationHashDigest(&hasher, admission.view.turn.context_fingerprint);
    OrientationHashDigest(&hasher, output->receipt.receipt_id);
    OrientationHashDigest(&hasher, best.response_fingerprint);
    OrientationHashId(&hasher, best.entity_id);
    OrientationHashU32(&hasher, output->policy.relation_mask);
    OrientationFinish(&hasher, &output->turn.context_fingerprint);
    return OrientationStatus::Ok;
}

}  // namespace laplace_cognition_prompt_orientation_detail
