    if (aggregate.response.witness_count == 0U) {
        aggregate.response.entity_id = candidate.target_entity_id;
        aggregate.response.minimum_depth = depth;
        aggregate.response.minimum_gap = candidate.gap;
    } else {
        aggregate.response.minimum_depth =
            std::min(aggregate.response.minimum_depth, depth);
        aggregate.response.minimum_gap =
            std::min(aggregate.response.minimum_gap, candidate.gap);
    }
    ++aggregate.response.witness_count;
    if (candidate.relation_family == LAPLACE_OBSERVATION_QUERY_SEMANTIC) {
        ++aggregate.response.semantic_witness_count;
    }
    CountLayer(&aggregate, candidate.source_layer);
