#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))

provider = "engine/src/cognition_observation_candidate_provider.inc"

replace_once(provider,
'''bool DirectionValid(const std::uint32_t direction) {
    return direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD ||
        direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE ||
        direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC;
}

bool CandidateValid(''',
'''bool DirectionValid(const std::uint32_t direction) {
    return direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD ||
        direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE ||
        direction == LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC;
}

bool StandingZero(const laplace_standing_state& state) {
    return DigestZero(state.state_id) && DigestZero(state.coordinate_id) &&
        DigestZero(state.arena_scope_id) && DigestZero(state.prior_state_id) &&
        DigestZero(state.epoch_id) && DigestZero(state.rating_recipe_id) &&
        state.rating == 0.0 && state.rating_deviation == 0.0 &&
        state.volatility == 0.0 && state.eligible_match_count == 0U &&
        state.period_ordinal == 0U && state.rating_recipe_version == 0U &&
        state.flags == 0U;
}

bool StandingValid(const laplace_standing_state& state) {
    if (StandingZero(state)) return false;
    laplace_digest256 identified{};
    return laplace_standing_state_identify(&state, &identified) == LAPLACE_STANDING_OK &&
        SameDigest(identified, state.state_id);
}

void HashStandingIdentity(
    blake3_hasher* const hasher,
    const laplace_standing_state& standing) {
    HashDigest(hasher, standing.state_id);
    HashDigest(hasher, standing.coordinate_id);
    HashDigest(hasher, standing.arena_scope_id);
    HashDigest(hasher, standing.epoch_id);
    HashDigest(hasher, standing.rating_recipe_id);
}

bool CandidateValid(''')

replace_once(provider,
'''    const bool uncertainty_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT) != 0U;
    if (candidate.source_state_index''',
'''    const bool uncertainty_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT) != 0U;
    const bool standing_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U;
    if (candidate.source_state_index''')

replace_once(provider,
'''        (!uncertainty_present &&
         (candidate.evidence_uncertainty_numerator != 0U ||
          candidate.evidence_uncertainty_denominator != 0U))) {
        return false;
    }

    if (candidate.relation_family''',
'''        (!uncertainty_present &&
         (candidate.evidence_uncertainty_numerator != 0U ||
          candidate.evidence_uncertainty_denominator != 0U)) ||
        (candidate.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING &&
         (!standing_present || uncertainty_present ||
          !DigestZero(candidate.evidence_root_fingerprint) ||
          !StandingValid(candidate.standing))) ||
        (candidate.source_layer != LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING &&
         (standing_present || !StandingZero(candidate.standing)))) {
        return false;
    }

    if (candidate.relation_family''')

# Standing identity participates in state/dominance/context without becoming cost.
for anchor in [
'''    HashU32(&context_hasher, candidate.flags);
    Finish(&context_hasher, &target.context_fingerprint);''',
'''    HashU32(&dominance_hasher, candidate.flags);
    Finish(&dominance_hasher, &target.dominance_key);''']:
    prefix = anchor.split("    Finish", 1)[0]
    finish = "    Finish" + anchor.split("    Finish", 1)[1]
    replace_once(provider, anchor,
        prefix + '''    if ((candidate.flags & LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U) {
        HashStandingIdentity(&''' + ("context_hasher" if "context" in anchor else "dominance_hasher") + ''', candidate.standing);
    }
''' + finish)

replace_once(provider,
'''    if ((candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT) != 0U) {
        transition->flags |= LAPLACE_QUERY_SEARCH_TRANSITION_RELATION_ID_PRESENT;
    }
    transition->cost_components[0] = 1U;''',
'''    if ((candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT) != 0U) {
        transition->flags |= LAPLACE_QUERY_SEARCH_TRANSITION_RELATION_ID_PRESENT;
    }
    if ((candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U) {
        transition->flags |= LAPLACE_QUERY_SEARCH_TRANSITION_STANDING_PRESENT;
        transition->standing = candidate.standing;
    }
    transition->cost_components[0] = 1U;''')

replace_once(provider,
'''    HashU32(&calculation_hasher, candidate.flags);
    Finish(&calculation_hasher, &transition->calculation_receipt);''',
'''    HashU32(&calculation_hasher, candidate.flags);
    if ((candidate.flags & LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U) {
        HashStandingIdentity(&calculation_hasher, candidate.standing);
    }
    Finish(&calculation_hasher, &transition->calculation_receipt);''')

replace_once(provider,
'''    HashU32(&transition_hasher, transition->flags);
    Finish(&transition_hasher, &transition->transition_id);''',
'''    HashU32(&transition_hasher, transition->flags);
    if ((transition->flags & LAPLACE_QUERY_SEARCH_TRANSITION_STANDING_PRESENT) != 0U) {
        HashStandingIdentity(&transition_hasher, transition->standing);
    }
    Finish(&transition_hasher, &transition->transition_id);''')

replace_once(provider,
'''    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY ||
        transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING) {
        return !DigestZero(transition.evidence_root_fingerprint)
            ? LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY : 0U;
    }
    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION) {''',
'''    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY) {
        return !DigestZero(transition.evidence_root_fingerprint)
            ? LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY : 0U;
    }
    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING) {
        return DigestZero(transition.evidence_root_fingerprint) &&
               (transition.flags & LAPLACE_QUERY_SEARCH_TRANSITION_STANDING_PRESENT) != 0U &&
               StandingValid(transition.standing)
            ? LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING : 0U;
    }
    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION) {''')

replace_once(provider,
'''        constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
        family_set.insert(step.relation_family);''',
'''        constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
        if (source_class == LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING) {
            constraint.standing = step.standing;
        }
        family_set.insert(step.relation_family);''')

replace_once(provider,
'''    operator_program.eligible_source_mask = 7U;''',
'''    operator_program.eligible_source_mask = 15U;''')

query = "engine/src/query_search_part01.inc"
replace_once(query,
'''bool TransitionValid(
    const laplace_query_search_program& program,
    const laplace_query_search_transition& transition) {
    const bool relation_id_present =
        (transition.flags & LAPLACE_QUERY_SEARCH_TRANSITION_RELATION_ID_PRESENT) != 0U;
    return !Zero(transition.transition_id) &&''',
'''bool SearchStandingZero(const laplace_standing_state& state) {
    return Zero(state.state_id) && Zero(state.coordinate_id) &&
        Zero(state.arena_scope_id) && Zero(state.prior_state_id) &&
        Zero(state.epoch_id) && Zero(state.rating_recipe_id) &&
        state.rating == 0.0 && state.rating_deviation == 0.0 &&
        state.volatility == 0.0 && state.eligible_match_count == 0U &&
        state.period_ordinal == 0U && state.rating_recipe_version == 0U &&
        state.flags == 0U;
}

bool SearchStandingValid(const laplace_standing_state& state) {
    if (SearchStandingZero(state)) return false;
    laplace_digest256 identified{};
    return laplace_standing_state_identify(&state, &identified) == LAPLACE_STANDING_OK &&
        Same(identified, state.state_id);
}

bool TransitionValid(
    const laplace_query_search_program& program,
    const laplace_query_search_transition& transition) {
    const bool relation_id_present =
        (transition.flags & LAPLACE_QUERY_SEARCH_TRANSITION_RELATION_ID_PRESENT) != 0U;
    const bool standing_present =
        (transition.flags & LAPLACE_QUERY_SEARCH_TRANSITION_STANDING_PRESENT) != 0U;
    return !Zero(transition.transition_id) &&''')

replace_once(query,
'''        (transition.flags & ~LAPLACE_QUERY_SEARCH_TRANSITION_KNOWN_FLAGS) == 0U &&
        (relation_id_present || RelationIdZero(transition.relation_id));''',
'''        (transition.flags & ~LAPLACE_QUERY_SEARCH_TRANSITION_KNOWN_FLAGS) == 0U &&
        (relation_id_present || RelationIdZero(transition.relation_id)) &&
        (standing_present ? SearchStandingValid(transition.standing)
                          : SearchStandingZero(transition.standing));''')

# Remove the transient patch machinery from the resulting commit.
Path("tools/apply_typed_standing_query.py").unlink(missing_ok=True)
Path(".github/workflows/apply-typed-standing-query.yml").unlink(missing_ok=True)
