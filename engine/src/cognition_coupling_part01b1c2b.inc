    if (!Add(
            aggregate.response.total_multiplicity,
            candidate.multiplicity,
            &aggregate.response.total_multiplicity) ||
        !Add(
            aggregate.response.total_gap,
            candidate.gap,
            &aggregate.response.total_gap)) {
        return false;
    }
    aggregate.response.relation_family_mask |=
        source.path_relation_mask | candidate.relation_family;
    aggregate.response.source_layer_mask |=
        source.path_source_layer_mask | candidate.source_layer;
    aggregate.response.direction_mask |=
        UINT32_C(1) << (candidate.direction - 1U);
