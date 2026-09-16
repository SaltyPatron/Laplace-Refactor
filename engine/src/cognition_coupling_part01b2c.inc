    HashU64(&hasher, aggregate->response.seed_support_count);
    HashU64(&hasher, aggregate->response.witness_count);
    HashU64(&hasher, aggregate->response.independent_evidence_root_count);
    HashU64(&hasher, aggregate->response.semantic_witness_count);
    HashU64(&hasher, aggregate->response.physicality_witness_count);
    HashU64(&hasher, aggregate->response.testimony_witness_count);
    HashU64(&hasher, aggregate->response.calculation_witness_count);
    HashU64(&hasher, aggregate->response.geometry_witness_count);
    HashU64(&hasher, aggregate->response.standing_witness_count);
    HashU64(&hasher, aggregate->response.total_multiplicity);
    HashU64(&hasher, aggregate->response.total_gap);
    HashU64(&hasher, aggregate->response.minimum_gap);
    HashU32(&hasher, aggregate->response.minimum_depth);
    HashU32(&hasher, aggregate->response.relation_family_mask);
    HashU32(&hasher, aggregate->response.source_layer_mask);
    HashU32(&hasher, aggregate->response.direction_mask);
    HashU32(&hasher, aggregate->response.flags);
    for (const auto& witness : aggregate->witness_ids) {
        blake3_hasher_update(&hasher, witness.data(), witness.size());
    }
    Finish(&hasher, &aggregate->response.response_fingerprint);
}
