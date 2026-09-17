namespace {

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_id128 Id(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

struct CouplingBackend final {
    laplace_id128 seed_entity{};
    laplace_id128 shared_target{};
    laplace_id128 propagated_target{};
    std::size_t calls{};
};

void FillCommon(
    laplace_cognition_observation_candidate* candidate,
    const std::size_t source_index,
    const laplace_id128& target,
    const std::uint8_t observation_seed) {
    *candidate = laplace_cognition_observation_candidate{};
    candidate->target_entity_id = target;
    candidate->observation_fingerprint = Digest(observation_seed);
    candidate->source_state_index = static_cast<std::uint64_t>(source_index);
    candidate->source_logical_ordinal = static_cast<std::uint64_t>(source_index + 1U);
    candidate->target_logical_ordinal = static_cast<std::uint64_t>(source_index + 2U);
    candidate->multiplicity = 1U;
    candidate->gap = 1U;
}

int EnumerateCoupling(
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
    auto& backend = *static_cast<CouplingBackend*>(opaque);
    ++backend.calls;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;

    for (std::size_t source = 0U; source < frontier_state_count; ++source) {
        (void)frontier_states[source];
        (void)accumulated_costs[source];
        if (SameId(source_entity_ids[source], backend.seed_entity)) {
            if (candidate_capacity - *candidate_count < 2U) return 2;
            auto& structural = candidates[(*candidate_count)++];
            FillCommon(
                &structural,
                source,
                backend.shared_target,
                static_cast<std::uint8_t>(0x40U + source));
            structural.relation_family = LAPLACE_OBSERVATION_QUERY_COOCCUR;
            structural.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
            structural.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC;

            auto& semantic = candidates[(*candidate_count)++];
            FillCommon(
                &semantic,
                source,
                backend.shared_target,
                static_cast<std::uint8_t>(0x50U + source));
            semantic.relation_id = Id(0x61U);
            semantic.evidence_root_fingerprint = Digest(0x70U);
            semantic.evidence_uncertainty_numerator = 2U;
            semantic.evidence_uncertainty_denominator = 10U;
            semantic.relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
            semantic.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
            semantic.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
            semantic.flags =
                LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT |
                LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT;
        } else if (SameId(source_entity_ids[source], backend.shared_target)) {
            if (*candidate_count >= candidate_capacity) return 3;
            auto& calculated = candidates[(*candidate_count)++];
            FillCommon(
                &calculated,
                source,
                backend.propagated_target,
                static_cast<std::uint8_t>(0x80U + source));
            calculated.relation_id = Id(0x62U);
            calculated.relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
            calculated.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION;
            calculated.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
            calculated.flags =
                LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;
        }
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

laplace_cognition_observation_candidate_provider_v1
CouplingProvider(CouplingBackend* const backend) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = backend;
    provider.provider_fingerprint = Digest(0x31U);
    provider.maximum_candidate_records_per_expansion = 2U;
    provider.enumerate_candidates = EnumerateCoupling;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}
