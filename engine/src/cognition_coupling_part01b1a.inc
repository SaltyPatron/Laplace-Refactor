void CountLayer(Aggregate* aggregate, std::uint32_t source_layer) {
    if (source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY) {
        ++aggregate->response.physicality_witness_count;
    } else if (source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY) {
        ++aggregate->response.testimony_witness_count;
    } else if (source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION) {
        ++aggregate->response.calculation_witness_count;
    } else if (source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_GEOMETRY) {
        ++aggregate->response.geometry_witness_count;
    } else if (source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING) {
        ++aggregate->response.standing_witness_count;
    }
}

bool RecordCandidate(
    const laplace_cognition_coupling_request& request,
    const FrontierNode& source,
    const laplace_cognition_observation_candidate& candidate,
    std::uint32_t depth,
    std::map<IdKey, Aggregate>* aggregates,
    std::set<DigestKey>* witness_ids,
    std::vector<laplace_cognition_coupling_witness>* witnesses) {
    const auto witness_id = WitnessId(
        source, request.seeds[static_cast<std::size_t>(source.seed_index)], candidate, depth);
    if (!witness_ids->insert(Key(witness_id)).second) return true;
