namespace {

using namespace laplace_test::admission;

struct OrientationSemanticFixture final {
    laplace_id128 a{};
    laplace_id128 b{};
    laplace_id128 preferred{};
    laplace_id128 distractor{};
    std::size_t calls{};
};

void EmitSemanticCandidate(
    laplace_cognition_observation_candidate* const candidate,
    const std::size_t source_index,
    const laplace_id128& target,
    const std::uint8_t observation_seed,
    const std::uint8_t evidence_seed,
    const std::uint32_t depth) {
    *candidate = laplace_cognition_observation_candidate{};
    candidate->target_entity_id = target;
    candidate->relation_id = Codepoint(static_cast<std::uint32_t>('R'));
    candidate->observation_fingerprint = Digest(observation_seed);
    candidate->evidence_root_fingerprint = Digest(evidence_seed);
    candidate->source_state_index = static_cast<std::uint64_t>(source_index);
    candidate->source_logical_ordinal = depth;
    candidate->target_logical_ordinal = static_cast<std::uint64_t>(depth) + 1U;
    candidate->multiplicity = 1U;
    candidate->gap = 1U;
    candidate->relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    candidate->source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
    candidate->direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
    candidate->flags =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;
}

int EnumerateOrientationSemantic(
    void* const opaque,
    const laplace_observation_query_binding* const binding,
    const laplace_id128* const source_entity_ids,
    const laplace_query_search_state* const frontier_states,
    const std::uint64_t* const accumulated_costs,
    const std::size_t frontier_state_count,
    laplace_cognition_observation_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_observation_candidate_usage* const usage) {
    if (opaque == nullptr || binding == nullptr || source_entity_ids == nullptr ||
        frontier_states == nullptr || accumulated_costs == nullptr ||
        frontier_state_count == 0U || candidates == nullptr ||
        candidate_count == nullptr || usage == nullptr) {
        return 1;
    }
    auto& fixture = *static_cast<OrientationSemanticFixture*>(opaque);
    ++fixture.calls;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;
    if ((binding->relation_mask & LAPLACE_OBSERVATION_QUERY_SEMANTIC) == 0U) {
        return 0;
    }

    for (std::size_t index = 0U; index < frontier_state_count; ++index) {
        (void)accumulated_costs[index];
        const bool from_a = SameId(source_entity_ids[index], fixture.a);
        const bool from_b = SameId(source_entity_ids[index], fixture.b);
        if (!from_a && !from_b) continue;
        if (*candidate_count >= candidate_capacity) return 2;
        EmitSemanticCandidate(
            &candidates[(*candidate_count)++],
            index,
            fixture.preferred,
            static_cast<std::uint8_t>(0xA0U + index),
            from_a ? 0xB0U : 0xB1U,
            frontier_states[index].depth);
        if (from_a) {
            if (*candidate_count >= candidate_capacity) return 3;
            EmitSemanticCandidate(
                &candidates[(*candidate_count)++],
                index,
                fixture.distractor,
                static_cast<std::uint8_t>(0xC0U + index),
                0xC8U,
                frontier_states[index].depth);
        }
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

laplace_cognition_observation_candidate_provider_v1 OrientationSemanticProvider(
    OrientationSemanticFixture* const fixture) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0x99U);
    provider.maximum_candidate_records_per_expansion = 2U;
    provider.enumerate_candidates = EnumerateOrientationSemantic;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}
