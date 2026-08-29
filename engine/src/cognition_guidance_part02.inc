namespace {

struct RootEvidence {
    bool positive{};
    bool negative{};
};

bool EvidenceObservationValid(
    const laplace_cognition_evidence_observation& observation) {
    return !Zero(observation.observation_id) && !Zero(observation.proposition_id) &&
        !Zero(observation.dependence_root_id) && !Zero(observation.source_id) &&
        !Zero(observation.context_fingerprint) && !Zero(observation.evidence_epoch) &&
        !Zero(observation.uncertainty_fingerprint) &&
        !Zero(observation.standing_fingerprint) &&
        (observation.polarity == 1 || observation.polarity == -1) &&
        observation.flags == 0U;
}

bool ObservationLess(
    const laplace_cognition_evidence_observation& left,
    const laplace_cognition_evidence_observation& right) {
    return DigestLess(left.observation_id, right.observation_id);
}

bool OperationBasicValid(const laplace_cognition_guidance_operation& operation) {
    if (Zero(operation.operation_id) || Zero(operation.operands_fingerprint) ||
        Zero(operation.preconditions_fingerprint) ||
        Zero(operation.predicted_effect_fingerprint) || Zero(operation.authority_id) ||
        Zero(operation.receipt_contract_fingerprint) ||
        operation.kind < LAPLACE_COGNITION_OPERATION_STRUCTURAL_CALCULATION ||
        operation.kind > LAPLACE_COGNITION_OPERATION_EXECUTE ||
        (operation.flags & ~LAPLACE_COGNITION_OPERATION_KNOWN_FLAGS) != 0U) {
        return false;
    }
    const bool information =
        operation.kind >= LAPLACE_COGNITION_OPERATION_STRUCTURAL_CALCULATION &&
        operation.kind <= LAPLACE_COGNITION_OPERATION_CLARIFICATION;
    const bool terminal = operation.kind >= LAPLACE_COGNITION_OPERATION_ANSWER;
    if (information &&
        (operation.flags & LAPLACE_COGNITION_OPERATION_INFORMATION_SEEKING) == 0U) {
        return false;
    }
    if (terminal &&
        (operation.flags & LAPLACE_COGNITION_OPERATION_TERMINAL_ACT) == 0U) {
        return false;
    }
    return true;
}

bool OperationEligible(
    const laplace_cognition_guidance_state& state,
    const laplace_cognition_guidance_operation& operation,
    const std::uint32_t completion) {
    if (!OperationBasicValid(operation)) return false;
    if ((operation.flags & LAPLACE_COGNITION_OPERATION_REQUIRES_EFFECT_AUTHORITY) != 0U &&
        !Same(operation.authority_id, state.header.authority_id)) {
        return false;
    }
    const bool terminal = operation.kind >= LAPLACE_COGNITION_OPERATION_ANSWER;
    if (terminal) {
        return completion == LAPLACE_COGNITION_COMPLETION_COMPLETE;
    }
    if (Zero(operation.target_obligation_id)) return false;
    const auto* const obligation = FindObligation(state, operation.target_obligation_id);
    if (obligation == nullptr ||
        obligation->disposition != LAPLACE_COGNITION_OBLIGATION_OPEN ||
        !Same(operation.authority_id, obligation->authority_id)) {
        return false;
    }
    if (operation.kind == LAPLACE_COGNITION_OPERATION_EXPLICIT_UNRESOLVED &&
        (obligation->flags & LAPLACE_COGNITION_OBLIGATION_ALLOW_TYPED_UNRESOLVED) == 0U) {
        return false;
    }
    return true;
}

bool OperationBetter(
    const laplace_cognition_guidance_operation& left,
    const laplace_cognition_guidance_operation& right) {
#if defined(LAPLACE_TEST_COGNITION_GUIDANCE_RAW_NOVELTY)
    if (left.novelty != right.novelty) return left.novelty > right.novelty;
#endif
    if (left.expected_obligation_reduction != right.expected_obligation_reduction) {
        return left.expected_obligation_reduction > right.expected_obligation_reduction;
    }
    if (left.information_value != right.information_value) {
        return left.information_value > right.information_value;
    }
    if (left.resource_cost != right.resource_cost) {
        return left.resource_cost < right.resource_cost;
    }
    if (left.kind != right.kind) return left.kind < right.kind;
    return DigestLess(left.operation_id, right.operation_id);
}

}  // namespace

extern "C" laplace_cognition_guidance_status laplace_cognition_evidence_fold(
    const laplace_cognition_evidence_observation* const observations,
    const size_t observation_count,
    laplace_cognition_evidence_fold_summary* const summary) {
    if (summary != nullptr) *summary = laplace_cognition_evidence_fold_summary{};
    if (observations == nullptr || observation_count == 0U || summary == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    try {
        std::vector<laplace_cognition_evidence_observation> ordered(
            observations, observations + observation_count);
        std::sort(ordered.begin(), ordered.end(), ObservationLess);
        const auto proposition = ordered.front().proposition_id;
        const auto context = ordered.front().context_fingerprint;
        const auto epoch = ordered.front().evidence_epoch;
        std::map<DigestKey, RootEvidence> roots;
        blake3_hasher uncertainty_hasher{};
        blake3_hasher standing_hasher{};
        blake3_hasher_init(&uncertainty_hasher);
        blake3_hasher_init(&standing_hasher);
        blake3_hasher_update(
            &uncertainty_hasher, LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN,
            sizeof(LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN) - 1U);
        blake3_hasher_update(
            &standing_hasher, LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN,
            sizeof(LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN) - 1U);
        laplace_digest256 previous_observation{};
        bool have_previous = false;
        for (const auto& observation : ordered) {
            if (!EvidenceObservationValid(observation) ||
                !Same(observation.proposition_id, proposition) ||
                !Same(observation.context_fingerprint, context) ||
                !Same(observation.evidence_epoch, epoch)) {
                return LAPLACE_COGNITION_GUIDANCE_STATE_INVALID;
            }
            if (have_previous && Same(previous_observation, observation.observation_id)) {
                return LAPLACE_COGNITION_GUIDANCE_DUPLICATE;
            }
            previous_observation = observation.observation_id;
            have_previous = true;
            auto& root = roots[Key(observation.dependence_root_id)];
            root.positive = root.positive || observation.polarity > 0;
            root.negative = root.negative || observation.polarity < 0;
            HashDigest(&uncertainty_hasher, observation.observation_id);
            HashDigest(&uncertainty_hasher, observation.dependence_root_id);
            HashDigest(&uncertainty_hasher, observation.uncertainty_fingerprint);
            HashDigest(&standing_hasher, observation.observation_id);
            HashDigest(&standing_hasher, observation.dependence_root_id);
            HashDigest(&standing_hasher, observation.standing_fingerprint);
            HashI32(&standing_hasher, observation.polarity);
        }
        summary->proposition_id = proposition;
        summary->context_fingerprint = context;
        summary->evidence_epoch = epoch;
        summary->raw_observation_count = static_cast<std::uint64_t>(ordered.size());
        summary->independent_root_count = static_cast<std::uint64_t>(roots.size());
        blake3_hasher root_hasher{};
        blake3_hasher_init(&root_hasher);
        blake3_hasher_update(
            &root_hasher, LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN,
            sizeof(LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN) - 1U);
        HashDigest(&root_hasher, proposition);
        for (const auto& [root_id, evidence] : roots) {
            blake3_hasher_update(&root_hasher, root_id.data(), root_id.size());
            HashU32(&root_hasher, evidence.positive ? 1U : 0U);
            HashU32(&root_hasher, evidence.negative ? 1U : 0U);
            if (evidence.positive) ++summary->positive_root_count;
            if (evidence.negative) ++summary->negative_root_count;
            if (evidence.positive && evidence.negative) {
                ++summary->contradictory_root_count;
            }
        }
        Finish(&root_hasher, &summary->root_set_fingerprint);
        Finish(&uncertainty_hasher, &summary->uncertainty_fingerprint);
        Finish(&standing_hasher, &summary->standing_fingerprint);
        blake3_hasher receipt_hasher{};
        blake3_hasher_init(&receipt_hasher);
        blake3_hasher_update(
            &receipt_hasher, LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN,
            sizeof(LAPLACE_COGNITION_EVIDENCE_FOLD_DOMAIN) - 1U);
        HashDigest(&receipt_hasher, summary->proposition_id);
        HashDigest(&receipt_hasher, summary->context_fingerprint);
        HashDigest(&receipt_hasher, summary->evidence_epoch);
        HashDigest(&receipt_hasher, summary->root_set_fingerprint);
        HashDigest(&receipt_hasher, summary->uncertainty_fingerprint);
        HashDigest(&receipt_hasher, summary->standing_fingerprint);
        HashU64(&receipt_hasher, summary->raw_observation_count);
        HashU64(&receipt_hasher, summary->independent_root_count);
        HashU64(&receipt_hasher, summary->positive_root_count);
        HashU64(&receipt_hasher, summary->negative_root_count);
        HashU64(&receipt_hasher, summary->contradictory_root_count);
        Finish(&receipt_hasher, &summary->fold_receipt_id);
        summary->status = LAPLACE_COGNITION_GUIDANCE_OK;
        return LAPLACE_COGNITION_GUIDANCE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_GUIDANCE_MEMORY_FAILURE;
    }
}

extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_select_operation(
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_guidance_operation* const operations,
    const size_t operation_count,
    laplace_cognition_guidance_decision* const decision) {
    if (decision != nullptr) *decision = laplace_cognition_guidance_decision{};
    if (state == nullptr || operations == nullptr || operation_count == 0U ||
        decision == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    std::uint32_t completion = LAPLACE_COGNITION_COMPLETION_INCOMPLETE;
    std::uint64_t remaining = 0U;
    const auto completion_status = laplace_cognition_guidance_completion(
        state, &completion, &remaining);
    if (completion_status != LAPLACE_COGNITION_GUIDANCE_OK) {
        return completion_status;
    }
    (void)remaining;
    const laplace_cognition_guidance_operation* selected = nullptr;
    blake3_hasher decision_hasher{};
    blake3_hasher_init(&decision_hasher);
    blake3_hasher_update(
        &decision_hasher, LAPLACE_COGNITION_GUIDANCE_DECISION_DOMAIN,
        sizeof(LAPLACE_COGNITION_GUIDANCE_DECISION_DOMAIN) - 1U);
    HashDigest(&decision_hasher, state->state_id);
    for (std::size_t index = 0U; index < operation_count; ++index) {
        const auto& operation = operations[index];
        const bool eligible = OperationEligible(*state, operation, completion);
        HashDigest(&decision_hasher, operation.operation_id);
        HashDigest(&decision_hasher, operation.target_obligation_id);
        HashDigest(&decision_hasher, operation.operands_fingerprint);
        HashDigest(&decision_hasher, operation.preconditions_fingerprint);
        HashDigest(&decision_hasher, operation.predicted_effect_fingerprint);
        HashDigest(&decision_hasher, operation.authority_id);
        HashDigest(&decision_hasher, operation.receipt_contract_fingerprint);
        HashU64(&decision_hasher, operation.expected_obligation_reduction);
        HashU64(&decision_hasher, operation.information_value);
        HashU64(&decision_hasher, operation.resource_cost);
        HashU64(&decision_hasher, operation.novelty);
        HashU32(&decision_hasher, operation.kind);
        HashU32(&decision_hasher, operation.flags);
        HashU32(&decision_hasher, eligible ? 1U : 0U);
        if (!eligible) {
            ++decision->rejected_operation_count;
            continue;
        }
        ++decision->eligible_operation_count;
        if (selected == nullptr || OperationBetter(operation, *selected)) {
            selected = &operation;
        }
    }
    if (selected == nullptr) {
        decision->state_id = state->state_id;
        decision->status = LAPLACE_COGNITION_GUIDANCE_NO_OPERATION;
        Finish(&decision_hasher, &decision->decision_receipt_id);
        return LAPLACE_COGNITION_GUIDANCE_NO_OPERATION;
    }
    decision->state_id = state->state_id;
    decision->selected_operation_id = selected->operation_id;
    decision->selected_expected_obligation_reduction =
        selected->expected_obligation_reduction;
    decision->selected_information_value = selected->information_value;
    decision->selected_resource_cost = selected->resource_cost;
    decision->selected_kind = selected->kind;
    decision->status = LAPLACE_COGNITION_GUIDANCE_OK;
    HashDigest(&decision_hasher, selected->operation_id);
    Finish(&decision_hasher, &decision->decision_receipt_id);
    return LAPLACE_COGNITION_GUIDANCE_OK;
}
