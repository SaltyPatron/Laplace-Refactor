    aggregate.seeds.insert(source.seed_index);
    if (!ZeroDigest(candidate.evidence_root_fingerprint)) {
        aggregate.evidence_roots.insert(
            Key(candidate.evidence_root_fingerprint));
    }
    aggregate.witness_ids.push_back(Key(witness_id));
    return true;
}
