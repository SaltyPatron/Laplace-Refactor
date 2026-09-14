struct CouplingOwner final {
    ~CouplingOwner() {
        laplace_cognition_coupling_result_destroy(&value);
    }
    laplace_cognition_coupling_result* value{};
};

struct InterpretationOwner final {
    ~InterpretationOwner() {
        laplace_cognition_interpretation_destroy(&value);
    }
    laplace_cognition_interpretation_result* value{};
};

bool FindResponse(
    const laplace_cognition_coupling_result* const field,
    const laplace_id128& entity_id,
    laplace_cognition_coupling_response* const response) {
    if (field == nullptr || response == nullptr) return false;
    const std::size_t response_count =
        laplace_cognition_coupling_response_count(field);
    for (std::size_t index = 0U; index < response_count; ++index) {
        laplace_cognition_coupling_response current{};
        if (laplace_cognition_coupling_response_read(
                field,
                index,
                &current) != LAPLACE_COGNITION_COUPLING_OK) {
            return false;
        }
        if (OrientationSameId(current.entity_id, entity_id)) {
            *response = current;
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

    std::vector<OrientationFactorInputs> factor_inputs;
    const auto factor_status = BuildJointFactors(
        seeds,
        field.value,
        &factor_inputs);
    if (factor_status != OrientationStatus::Ok) return factor_status;

    std::vector<laplace_cognition_interpretation_factor> factors;
    factors.reserve(factor_inputs.size());
    for (auto& input : factor_inputs) {
        input.storage.SealRows(input.witnesses);
        laplace_cognition_interpretation_factor factor{};
        factor.factor_id = input.storage.factor_id;
        factor.law_id = input.storage.law_id;
        factor.slots = &input.storage.slot_index;
        factor.slot_count = 1U;
        factor.rows = input.storage.rows.data();
        factor.row_count = input.storage.rows.size();
        factors.push_back(factor);
    }

    const auto goal_slot = OrientationDerivedDigest(
        GoalSlotDomain,
        admission.view.occurrence_id,
        0U);
    laplace_cognition_interpretation_program program{};
    program.observation_root = admission.view.trunk_entity_id;
    program.occurrence_id = admission.view.occurrence_id;
    program.world_id = admission.view.turn.world_id;
    program.time_fingerprint = admission.view.turn.time_fingerprint;
    program.context_id = admission.view.turn.context_fingerprint;
    program.evidence_epoch = policy.evidence_epoch;
    program.boundary_id = policy.evidence_boundary;
    program.authority_id = policy.authority_id;
    program.slot_ids = &goal_slot;
    program.slot_count = 1U;
    program.maximum_comparisons = policy.search_budget.max_transition_records;
    program.maximum_states = policy.search_budget.max_frontier_states;
    program.maximum_memory_bytes = policy.search_budget.max_memory_bytes;
    program.version = LAPLACE_COGNITION_INTERPRETATION_VERSION;

    InterpretationOwner interpretation;
    const auto interpretation_status = laplace_cognition_interpretation_execute(
        &program,
        factors.data(),
        factors.size(),
        &interpretation.value,
        &output->interpretation_receipt);
    if (interpretation_status == LAPLACE_COGNITION_INTERPRETATION_EXHAUSTED) {
        return OrientationStatus::Exhausted;
    }
    if (interpretation_status != LAPLACE_COGNITION_INTERPRETATION_OK ||
        interpretation.value == nullptr) {
        return OrientationStatus::Invalid;
    }
    if (output->interpretation_receipt.disposition ==
        LAPLACE_COGNITION_INTERPRETATION_NO_COMPATIBLE_BINDING) {
        return OrientationStatus::NoResponse;
    }
    if (output->interpretation_receipt.disposition ==
        LAPLACE_COGNITION_INTERPRETATION_AMBIGUOUS) {
        return OrientationStatus::Ambiguous;
    }
    if (output->interpretation_receipt.disposition !=
        LAPLACE_COGNITION_INTERPRETATION_UNIQUE) {
        return OrientationStatus::Incomplete;
    }

    laplace_id128 goal_entity_id{};
    if (laplace_cognition_interpretation_read_binding(
            interpretation.value,
            &goal_slot,
            &goal_entity_id) != LAPLACE_COGNITION_INTERPRETATION_OK ||
        !FindResponse(field.value, goal_entity_id, &output->selected)) {
        return OrientationStatus::Invalid;
    }

    output->turn = admission.view.turn;
    output->policy = policy;
    output->policy.goal_entity_id = goal_entity_id;
    output->policy.relation_mask =
        output->selected.relation_family_mask |
        LAPLACE_OBSERVATION_QUERY_CONSTITUENT;
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
    OrientationHashDigest(
        &hasher,
        output->interpretation_receipt.receipt_id);
    OrientationHashDigest(
        &hasher,
        output->selected.response_fingerprint);
    OrientationHashId(&hasher, goal_entity_id);
    OrientationHashU32(&hasher, output->policy.relation_mask);
    OrientationFinish(&hasher, &output->turn.context_fingerprint);
    return OrientationStatus::Ok;
}

}  // namespace laplace_cognition_prompt_orientation_detail
