bool StandingZero(const laplace_standing_state& state) noexcept {
    return ZeroDigest(state.state_id) && ZeroDigest(state.coordinate_id) &&
        ZeroDigest(state.arena_scope_id) && ZeroDigest(state.prior_state_id) &&
        ZeroDigest(state.epoch_id) && ZeroDigest(state.rating_recipe_id) &&
        state.rating == 0.0 && state.rating_deviation == 0.0 &&
        state.volatility == 0.0 && state.eligible_match_count == 0U &&
        state.period_ordinal == 0U && state.rating_recipe_version == 0U &&
        state.flags == 0U;
}

bool CandidateValid(
    const laplace_cognition_coupling_request& request,
    const laplace_cognition_observation_candidate& candidate,
    std::size_t source_count) {
    const bool relation_id_present =
        (candidate.flags & LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT) != 0U;
    const bool uncertainty_present =
        (candidate.flags & LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT) != 0U;
    const bool standing_present =
        (candidate.flags & LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U;
    if (candidate.source_state_index >= static_cast<std::uint64_t>(source_count) ||
        ZeroDigest(candidate.observation_fingerprint) || candidate.multiplicity == 0U ||
        candidate.relation_family == 0U ||
        (candidate.relation_family & (candidate.relation_family - 1U)) != 0U ||
        (candidate.relation_family & request.relation_mask) == 0U ||
        candidate.source_layer == 0U ||
        (candidate.source_layer & (candidate.source_layer - 1U)) != 0U ||
        (candidate.source_layer & LAPLACE_OBSERVATION_QUERY_SOURCE_LAYER_MASK) == 0U ||
        candidate.direction < LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD ||
        candidate.direction > LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC ||
        (candidate.flags & ~LAPLACE_COGNITION_OBSERVATION_CANDIDATE_KNOWN_FLAGS) != 0U ||
        candidate.reserved != 0U || (!relation_id_present && !ZeroId(candidate.relation_id)) ||
        (candidate.relation_family == LAPLACE_OBSERVATION_QUERY_SEMANTIC && !relation_id_present) ||
        (uncertainty_present &&
         (candidate.evidence_uncertainty_denominator == 0U ||
          candidate.evidence_uncertainty_numerator >= candidate.evidence_uncertainty_denominator)) ||
        (!uncertainty_present &&
         (candidate.evidence_uncertainty_numerator != 0U ||
          candidate.evidence_uncertainty_denominator != 0U))) {
        return false;
    }
    if (standing_present) {
        laplace_digest256 identified{};
        if (laplace_standing_state_identify(&candidate.standing, &identified) != LAPLACE_STANDING_OK ||
            !SameDigest(identified, candidate.standing.state_id)) {
            return false;
        }
    } else if (!StandingZero(candidate.standing)) {
        return false;
    }
    return true;
}
