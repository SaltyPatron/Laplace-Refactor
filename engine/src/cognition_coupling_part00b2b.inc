bool MakeState(
    const laplace_cognition_coupling_request& request,
    const laplace_digest256& binding_fingerprint,
    const laplace_cognition_coupling_seed& seed,
    std::uint64_t seed_index,
    const laplace_id128& entity_id,
    std::uint32_t depth,
    const laplace_digest256& context_fingerprint,
    laplace_query_search_state* state) {
    if (state == nullptr ||
        laplace_observation_query_entity_coordinate(&entity_id, &state->anchor_id) !=
            LAPLACE_OBSERVATION_QUERY_OK) {
        return false;
    }
    state->binding_fingerprint = binding_fingerprint;
    state->obligation_fingerprint = Obligation(seed, seed_index);
    state->context_fingerprint = context_fingerprint;
    state->evidence_epoch = request.evidence_epoch;
    state->boundary_id = request.evidence_boundary;
    state->depth = depth;

    blake3_hasher hasher{};
    Start(&hasher, DominanceDomain);
    HashU64(&hasher, seed_index);
    HashDigest(&hasher, state->anchor_id);
    HashDigest(&hasher, state->context_fingerprint);
    HashU32(&hasher, depth);
    Finish(&hasher, &state->dominance_key);

    Start(&hasher, StateDomain);
    HashU64(&hasher, seed_index);
    HashDigest(&hasher, seed.occurrence_id);
    HashDigest(&hasher, state->anchor_id);
    HashDigest(&hasher, state->dominance_key);
    HashDigest(&hasher, state->binding_fingerprint);
    HashDigest(&hasher, state->obligation_fingerprint);
    HashDigest(&hasher, state->context_fingerprint);
    HashDigest(&hasher, state->evidence_epoch);
    HashDigest(&hasher, state->boundary_id);
    HashU32(&hasher, depth);
    Finish(&hasher, &state->state_id);
    return true;
}

laplace_digest256 NextContext(
    const FrontierNode& source,
    const laplace_cognition_observation_candidate& candidate) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    Start(&hasher, PathContextDomain);
    HashDigest(&hasher, source.state.context_fingerprint);
    HashDigest(&hasher, candidate.observation_fingerprint);
    HashDigest(&hasher, candidate.evidence_root_fingerprint);
    HashId(&hasher, candidate.relation_id);
    HashU64(&hasher, candidate.source_logical_ordinal);
    HashU64(&hasher, candidate.target_logical_ordinal);
    HashU64(&hasher, candidate.gap);
    HashU32(&hasher, candidate.relation_family);
    HashU32(&hasher, candidate.source_layer);
    HashU32(&hasher, candidate.direction);
    Finish(&hasher, &output);
    return output;
}
