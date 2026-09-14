laplace_digest256 WitnessId(
    const FrontierNode& source,
    const laplace_cognition_coupling_seed& seed,
    const laplace_cognition_observation_candidate& candidate,
    std::uint32_t depth) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    Start(&hasher, WitnessDomain);
    HashU64(&hasher, source.seed_index);
    HashDigest(&hasher, seed.occurrence_id);
    HashId(&hasher, source.entity_id);
    HashId(&hasher, candidate.target_entity_id);
    HashId(&hasher, candidate.relation_id);
    HashDigest(&hasher, candidate.observation_fingerprint);
    HashDigest(&hasher, candidate.evidence_root_fingerprint);
    HashU64(&hasher, candidate.source_logical_ordinal);
    HashU64(&hasher, candidate.target_logical_ordinal);
    HashU64(&hasher, candidate.multiplicity);
    HashU64(&hasher, candidate.gap);
    HashU64(&hasher, candidate.evidence_uncertainty_numerator);
    HashU64(&hasher, candidate.evidence_uncertainty_denominator);
    HashU32(&hasher, depth);
    HashU32(&hasher, candidate.relation_family);
    HashU32(&hasher, candidate.source_layer);
    HashU32(&hasher, candidate.direction);
    HashU32(&hasher, candidate.flags);
    if ((candidate.flags & LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U) {
        HashDigest(&hasher, candidate.standing.state_id);
    }
    Finish(&hasher, &output);
    return output;
}
