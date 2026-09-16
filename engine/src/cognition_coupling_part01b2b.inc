    blake3_hasher hasher{};
    Start(&hasher, RootSetDomain);
    for (const auto seed_index : aggregate->seeds) {
        HashDigest(
            &hasher,
            request.seeds[static_cast<std::size_t>(seed_index)].occurrence_id);
    }
    Finish(&hasher, &aggregate->response.seed_root_set_fingerprint);

    Start(&hasher, EvidenceSetDomain);
    for (const auto& root : aggregate->evidence_roots) {
        blake3_hasher_update(&hasher, root.data(), root.size());
    }
    Finish(&hasher, &aggregate->response.evidence_root_set_fingerprint);
    std::sort(aggregate->witness_ids.begin(), aggregate->witness_ids.end());

    Start(&hasher, ResponseDomain);
    HashId(&hasher, aggregate->response.entity_id);
    HashDigest(&hasher, aggregate->response.seed_root_set_fingerprint);
    HashDigest(&hasher, aggregate->response.evidence_root_set_fingerprint);
