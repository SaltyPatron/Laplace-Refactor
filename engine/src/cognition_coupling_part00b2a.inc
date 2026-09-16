laplace_digest256 SeedSetFingerprint(const laplace_cognition_coupling_request& request) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    Start(&hasher, SeedSetDomain);
    HashU64(&hasher, static_cast<std::uint64_t>(request.seed_count));
    for (std::size_t index = 0U; index < request.seed_count; ++index) {
        const auto& seed = request.seeds[index];
        HashU64(&hasher, static_cast<std::uint64_t>(index));
        HashId(&hasher, seed.entity_id);
        HashDigest(&hasher, seed.occurrence_id);
        HashU64(&hasher, seed.logical_ordinal);
        HashU32(&hasher, seed.flags);
    }
    Finish(&hasher, &output);
    return output;
}

laplace_digest256 SeedContext(
    const laplace_cognition_coupling_request& request,
    const laplace_cognition_coupling_seed& seed,
    std::uint64_t seed_index) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    Start(&hasher, SeedContextDomain);
    HashDigest(&hasher, request.context_fingerprint);
    HashDigest(&hasher, request.world_id);
    HashDigest(&hasher, request.time_fingerprint);
    HashDigest(&hasher, seed.occurrence_id);
    HashId(&hasher, seed.entity_id);
    HashU64(&hasher, seed_index);
    HashU64(&hasher, seed.logical_ordinal);
    Finish(&hasher, &output);
    return output;
}

laplace_digest256 Obligation(
    const laplace_cognition_coupling_seed& seed,
    std::uint64_t seed_index) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    Start(&hasher, ObligationDomain);
    HashDigest(&hasher, seed.occurrence_id);
    HashId(&hasher, seed.entity_id);
    HashU64(&hasher, seed_index);
    Finish(&hasher, &output);
    return output;
}

struct FrontierNode final {
    laplace_id128 entity_id{};
    laplace_query_search_state state{};
    std::uint64_t seed_index{};
    std::uint64_t accumulated_cost{};
    std::uint32_t path_relation_mask{};
    std::uint32_t path_source_layer_mask{};
};
