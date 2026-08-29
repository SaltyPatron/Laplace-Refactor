extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_state_create(
    const laplace_cognition_guidance_header* const header,
    const laplace_cognition_obligation* const obligations,
    const size_t obligation_count,
    laplace_cognition_guidance_state** const state) {
    if (state != nullptr) *state = nullptr;
    if (header == nullptr || obligations == nullptr || obligation_count == 0U ||
        state == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    return BuildState(*header, obligations, obligation_count, state);
}

extern "C" void laplace_cognition_guidance_state_destroy(
    laplace_cognition_guidance_state** const state) {
    if (state != nullptr) {
        delete *state;
        *state = nullptr;
    }
}

extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_state_identify(
    const laplace_cognition_guidance_state* const state,
    laplace_digest256* const state_id) {
    if (state == nullptr || state_id == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    *state_id = state->state_id;
    return LAPLACE_COGNITION_GUIDANCE_OK;
}

extern "C" size_t laplace_cognition_guidance_state_obligation_count(
    const laplace_cognition_guidance_state* const state) {
    return state == nullptr ? 0U : state->obligations.size();
}

extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_state_obligation(
    const laplace_cognition_guidance_state* const state,
    const size_t index,
    laplace_cognition_obligation* const obligation) {
    if (state == nullptr || obligation == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    if (index >= state->obligations.size()) {
        return LAPLACE_COGNITION_GUIDANCE_RANGE;
    }
    *obligation = state->obligations[index];
    return LAPLACE_COGNITION_GUIDANCE_OK;
}

extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_completion(
    const laplace_cognition_guidance_state* const state,
    uint32_t* const completion,
    uint64_t* const remaining_required_count) {
    if (completion != nullptr) *completion = LAPLACE_COGNITION_COMPLETION_INCOMPLETE;
    if (remaining_required_count != nullptr) *remaining_required_count = 0U;
    if (state == nullptr || completion == nullptr || remaining_required_count == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    std::uint64_t open = 0U;
    std::uint64_t blocked = 0U;
    for (const auto& obligation : state->obligations) {
        if ((obligation.flags & LAPLACE_COGNITION_OBLIGATION_REQUIRED) == 0U) {
            continue;
        }
        if (obligation.disposition == LAPLACE_COGNITION_OBLIGATION_SATISFIED) {
            continue;
        }
        if (obligation.disposition == LAPLACE_COGNITION_OBLIGATION_OPEN) {
            ++open;
            continue;
        }
        if (TypedUnresolved(obligation.disposition) &&
            (obligation.flags &
             LAPLACE_COGNITION_OBLIGATION_ALLOW_TYPED_UNRESOLVED) != 0U) {
            continue;
        }
        ++blocked;
    }
    *remaining_required_count = open + blocked;
#if defined(LAPLACE_TEST_COGNITION_GUIDANCE_NONEMPTY_COMPLETE)
    *completion = state->obligations.empty()
        ? LAPLACE_COGNITION_COMPLETION_INCOMPLETE
        : LAPLACE_COGNITION_COMPLETION_COMPLETE;
#else
    if (blocked != 0U) {
        *completion = LAPLACE_COGNITION_COMPLETION_BLOCKED;
    } else if (open != 0U) {
        *completion = LAPLACE_COGNITION_COMPLETION_INCOMPLETE;
    } else {
        *completion = LAPLACE_COGNITION_COMPLETION_COMPLETE;
    }
#endif
    return LAPLACE_COGNITION_GUIDANCE_OK;
}

extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_project_queries(
    const laplace_cognition_guidance_state* const state,
    laplace_cognition_query_projection* const projections,
    const size_t projection_capacity,
    size_t* const projection_count) {
    if (projection_count != nullptr) *projection_count = 0U;
    if (state == nullptr || projection_count == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    std::size_t required = 0U;
    for (const auto& obligation : state->obligations) {
        if (obligation.disposition == LAPLACE_COGNITION_OBLIGATION_OPEN) ++required;
    }
    *projection_count = required;
    if (required == 0U) return LAPLACE_COGNITION_GUIDANCE_OK;
    if (projections == nullptr || projection_capacity < required) {
        return LAPLACE_COGNITION_GUIDANCE_RANGE;
    }
    std::size_t output_index = 0U;
    for (const auto& obligation : state->obligations) {
        if (obligation.disposition != LAPLACE_COGNITION_OBLIGATION_OPEN) continue;
        auto& projection = projections[output_index++];
        projection = laplace_cognition_query_projection{};
        projection.obligation_id = obligation.obligation_id;
        projection.binding_fingerprint = obligation.binding_fingerprint;
        projection.world_id = obligation.world_id;
        projection.time_fingerprint = obligation.time_fingerprint;
        projection.context_fingerprint = obligation.context_fingerprint;
        projection.evidence_boundary = obligation.evidence_boundary;
        projection.authority_id = obligation.authority_id;
        projection.result_contract_fingerprint = obligation.result_contract_fingerprint;
        projection.query_kind = obligation.kind;
        projection.obligation_flags = obligation.flags;
        blake3_hasher hasher{};
        blake3_hasher_init(&hasher);
        blake3_hasher_update(
            &hasher, LAPLACE_COGNITION_GUIDANCE_PROJECTION_DOMAIN,
            sizeof(LAPLACE_COGNITION_GUIDANCE_PROJECTION_DOMAIN) - 1U);
        HashDigest(&hasher, state->state_id);
        HashObligation(&hasher, obligation);
        Finish(&hasher, &projection.projection_id);
    }
    return LAPLACE_COGNITION_GUIDANCE_OK;
}

extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_apply_resolutions(
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_resolution* const resolutions,
    const size_t resolution_count,
    laplace_cognition_guidance_state** const next_state,
    laplace_cognition_guidance_transition_receipt* const receipt) {
    if (next_state != nullptr) *next_state = nullptr;
    if (receipt != nullptr) *receipt = laplace_cognition_guidance_transition_receipt{};
    if (state == nullptr || resolutions == nullptr || resolution_count == 0U ||
        next_state == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    try {
        std::vector<laplace_cognition_resolution> ordered(
            resolutions, resolutions + resolution_count);
        std::sort(ordered.begin(), ordered.end(), ResolutionLess);
        for (std::size_t index = 0U; index < ordered.size(); ++index) {
            const auto& resolution = ordered[index];
            if (Zero(resolution.obligation_id) || Zero(resolution.receipt_id) ||
                resolution.flags != 0U ||
                resolution.disposition == LAPLACE_COGNITION_OBLIGATION_OPEN ||
                !DispositionValid(resolution.disposition) ||
                (resolution.disposition == LAPLACE_COGNITION_OBLIGATION_SATISFIED &&
                 Zero(resolution.value_id))) {
                return LAPLACE_COGNITION_GUIDANCE_STATE_INVALID;
            }
            if (index != 0U && Same(
                    ordered[index - 1U].obligation_id,
                    resolution.obligation_id)) {
                return LAPLACE_COGNITION_GUIDANCE_DUPLICATE;
            }
        }
        std::vector<laplace_cognition_obligation> updated = state->obligations;
        for (const auto& resolution : ordered) {
            auto found = std::lower_bound(
                updated.begin(), updated.end(), resolution.obligation_id,
                [](const auto& obligation, const auto& id) {
                    return DigestLess(obligation.obligation_id, id);
                });
            if (found == updated.end() ||
                !Same(found->obligation_id, resolution.obligation_id)) {
                return LAPLACE_COGNITION_GUIDANCE_RANGE;
            }
            if (found->disposition != LAPLACE_COGNITION_OBLIGATION_OPEN) {
                return LAPLACE_COGNITION_GUIDANCE_CONFLICT;
            }
            found->value_id = resolution.value_id;
            found->resolution_receipt_id = resolution.receipt_id;
            found->disposition = resolution.disposition;
        }
        const auto status = BuildState(
            state->header, updated.data(), updated.size(), next_state);
        if (status != LAPLACE_COGNITION_GUIDANCE_OK) return status;

        blake3_hasher resolution_hasher{};
        blake3_hasher_init(&resolution_hasher);
        blake3_hasher_update(
            &resolution_hasher, LAPLACE_COGNITION_GUIDANCE_TRANSITION_DOMAIN,
            sizeof(LAPLACE_COGNITION_GUIDANCE_TRANSITION_DOMAIN) - 1U);
        HashDigest(&resolution_hasher, state->state_id);
        for (const auto& resolution : ordered) {
            HashDigest(&resolution_hasher, resolution.obligation_id);
            HashDigest(&resolution_hasher, resolution.value_id);
            HashDigest(&resolution_hasher, resolution.receipt_id);
            HashU32(&resolution_hasher, resolution.disposition);
        }
        Finish(&resolution_hasher, &receipt->resolution_set_fingerprint);
        receipt->prior_state_id = state->state_id;
        receipt->next_state_id = (*next_state)->state_id;
        receipt->applied_resolution_count = resolution_count;
        for (const auto& obligation : (*next_state)->obligations) {
            if (obligation.disposition == LAPLACE_COGNITION_OBLIGATION_OPEN) {
                ++receipt->remaining_open_count;
            }
        }
        std::uint64_t remaining_required = 0U;
        const auto completion_status = laplace_cognition_guidance_completion(
            *next_state, &receipt->completion, &remaining_required);
        if (completion_status != LAPLACE_COGNITION_GUIDANCE_OK) {
            laplace_cognition_guidance_state_destroy(next_state);
            return completion_status;
        }
        (void)remaining_required;
        blake3_hasher receipt_hasher{};
        blake3_hasher_init(&receipt_hasher);
        blake3_hasher_update(
            &receipt_hasher, LAPLACE_COGNITION_GUIDANCE_TRANSITION_DOMAIN,
            sizeof(LAPLACE_COGNITION_GUIDANCE_TRANSITION_DOMAIN) - 1U);
        HashDigest(&receipt_hasher, receipt->prior_state_id);
        HashDigest(&receipt_hasher, receipt->next_state_id);
        HashDigest(&receipt_hasher, receipt->resolution_set_fingerprint);
        HashU64(&receipt_hasher, receipt->applied_resolution_count);
        HashU64(&receipt_hasher, receipt->remaining_open_count);
        HashU32(&receipt_hasher, receipt->completion);
        Finish(&receipt_hasher, &receipt->transition_receipt_id);
        receipt->status = LAPLACE_COGNITION_GUIDANCE_OK;
        return LAPLACE_COGNITION_GUIDANCE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_GUIDANCE_MEMORY_FAILURE;
    }
}
