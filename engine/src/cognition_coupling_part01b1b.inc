    laplace_cognition_coupling_witness witness{};
    witness.witness_id = witness_id;
    witness.source_entity_id = source.entity_id;
    witness.target_entity_id = candidate.target_entity_id;
    witness.relation_id = candidate.relation_id;
    witness.seed_occurrence_id =
        request.seeds[static_cast<std::size_t>(source.seed_index)].occurrence_id;
    witness.observation_fingerprint = candidate.observation_fingerprint;
    witness.evidence_root_fingerprint = candidate.evidence_root_fingerprint;
    if ((candidate.flags & LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U) {
        witness.standing = candidate.standing;
    }
    witness.seed_index = source.seed_index;
    witness.source_logical_ordinal = candidate.source_logical_ordinal;
    witness.target_logical_ordinal = candidate.target_logical_ordinal;
    witness.multiplicity = candidate.multiplicity;
    witness.gap = candidate.gap;
    witness.evidence_uncertainty_numerator = candidate.evidence_uncertainty_numerator;
    witness.evidence_uncertainty_denominator = candidate.evidence_uncertainty_denominator;
    witness.depth = depth;
    witness.relation_family = candidate.relation_family;
    witness.source_layer = candidate.source_layer;
    witness.direction = candidate.direction;
    witness.flags = candidate.flags;
    witnesses->push_back(witness);
