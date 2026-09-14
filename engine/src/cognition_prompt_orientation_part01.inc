OrientationStatus BuildSeeds(
    const laplace_cognition_prompt_admission& admission,
    const laplace_cognition_turn_policy& policy,
    std::vector<laplace_cognition_coupling_seed>* const seeds) {
    if (seeds == nullptr || admission.decomposition == nullptr ||
        admission.composition_plan == nullptr) {
        return OrientationStatus::Invalid;
    }
    seeds->clear();
    laplace_decomposition_composition_plan_view plan{};
    if (laplace_decomposition_composition_plan_view_get(
            admission.composition_plan,
            &plan) != LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
        return OrientationStatus::Invalid;
    }
    std::size_t span_count = 0U;
    const auto* const spans =
        laplace_decomposition_spans(admission.decomposition, &span_count);
    if (spans == nullptr || plan.span_has_content == nullptr ||
        plan.span_references == nullptr ||
        plan.span_count != static_cast<std::uint64_t>(span_count) ||
        span_count == 0U) {
        return OrientationStatus::Invalid;
    }

    std::set<OrientationDigestKey> occurrences;
    laplace_cognition_coupling_seed root{};
    root.entity_id = admission.view.trunk_entity_id;
    root.occurrence_id = admission.view.occurrence_id;
    root.flags = LAPLACE_COGNITION_COUPLING_SEED_PRIMARY;
    seeds->push_back(root);
    occurrences.insert(OrientationKey(root.occurrence_id));

    for (std::size_t index = 1U; index < span_count; ++index) {
        if (plan.span_has_content[index] == 0U) continue;
        laplace_id128 entity_id{};
        if (!laplace_cognition_prompt_structural_detail::ResolveReference(
                admission,
                plan.span_references[index],
                &entity_id)) {
            return OrientationStatus::Invalid;
        }
        laplace_cognition_coupling_seed seed{};
        seed.entity_id = entity_id;
        seed.occurrence_id = SpanOccurrence(admission, spans[index], entity_id);
        seed.logical_ordinal = spans[index].sibling_ordinal;
        if (!occurrences.insert(OrientationKey(seed.occurrence_id)).second) {
            continue;
        }
        if (seeds->size() >= policy.search_budget.max_frontier_states) {
            return OrientationStatus::Exhausted;
        }
        seeds->push_back(seed);
    }
    return OrientationStatus::Ok;
}

std::uint64_t NonphysicalSemanticSupport(
    const laplace_cognition_coupling_response& response) {
    return response.testimony_witness_count +
        response.calculation_witness_count +
        response.standing_witness_count;
}

bool EligibleResponse(const laplace_cognition_coupling_response& response) {
    return response.semantic_witness_count != 0U &&
        NonphysicalSemanticSupport(response) != 0U;
}

int CompareResponse(
    const laplace_cognition_coupling_response& left,
    const laplace_cognition_coupling_response& right) {
    if (left.seed_support_count != right.seed_support_count) {
        return left.seed_support_count > right.seed_support_count ? 1 : -1;
    }
    if (left.independent_evidence_root_count !=
        right.independent_evidence_root_count) {
        return left.independent_evidence_root_count >
                right.independent_evidence_root_count ? 1 : -1;
    }
    const auto left_typed = NonphysicalSemanticSupport(left);
    const auto right_typed = NonphysicalSemanticSupport(right);
    if (left_typed != right_typed) return left_typed > right_typed ? 1 : -1;
    if (left.semantic_witness_count != right.semantic_witness_count) {
        return left.semantic_witness_count > right.semantic_witness_count ? 1 : -1;
    }
    if (left.minimum_depth != right.minimum_depth) {
        return left.minimum_depth < right.minimum_depth ? 1 : -1;
    }
    if (left.minimum_gap != right.minimum_gap) {
        return left.minimum_gap < right.minimum_gap ? 1 : -1;
    }
    return 0;
}
