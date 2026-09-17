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

bool IsSeedEntity(
    const std::vector<laplace_cognition_coupling_seed>& seeds,
    const laplace_id128& entity_id) {
    for (const auto& seed : seeds) {
        if (OrientationSameId(seed.entity_id, entity_id)) return true;
    }
    return false;
}

bool JointSemanticWitness(
    const laplace_cognition_coupling_witness& witness) {
    return witness.relation_family == LAPLACE_OBSERVATION_QUERY_SEMANTIC &&
        (witness.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY ||
         witness.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION ||
         witness.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING);
}

struct OrientationFactorStorage final {
    std::uint64_t seed_index{};
    laplace_digest256 factor_id{};
    laplace_digest256 law_id{};
    std::uint32_t slot_index{};
    std::vector<std::array<laplace_id128, 1>> values;
    std::vector<laplace_cognition_interpretation_row> rows;

    void SealRows(
        const std::vector<laplace_cognition_coupling_witness>& witnesses) {
        rows.clear();
        rows.reserve(values.size());
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const auto& witness = witnesses[index];
            laplace_cognition_interpretation_row row{};
            row.values = values[index].data();
            row.observation_id = witness.observation_fingerprint;
            row.evidence_root_id = witness.evidence_root_fingerprint;
            row.calculation_receipt_id = witness.witness_id;
            rows.push_back(row);
        }
    }
};

struct OrientationFactorInputs final {
    OrientationFactorStorage storage;
    std::vector<laplace_cognition_coupling_witness> witnesses;
};

OrientationStatus BuildJointFactors(
    const std::vector<laplace_cognition_coupling_seed>& seeds,
    const laplace_cognition_coupling_result* const field,
    std::vector<OrientationFactorInputs>* const factor_inputs) {
    if (field == nullptr || factor_inputs == nullptr) {
        return OrientationStatus::Invalid;
    }
    factor_inputs->clear();
    std::vector<std::vector<laplace_cognition_coupling_witness>> by_seed(
        seeds.size());
    const std::size_t witness_count =
        laplace_cognition_coupling_witness_count(field);
    for (std::size_t index = 0U; index < witness_count; ++index) {
        laplace_cognition_coupling_witness witness{};
        if (laplace_cognition_coupling_witness_read(
                field,
                index,
                &witness) != LAPLACE_COGNITION_COUPLING_OK ||
            witness.seed_index >= static_cast<std::uint64_t>(seeds.size())) {
            return OrientationStatus::Invalid;
        }
        const auto& seed = seeds[static_cast<std::size_t>(witness.seed_index)];
        /*
         * Coupling intentionally preserves every structural path reachable from a
         * seed.  Joint prompt interpretation must not, however, let one prompt
         * occurrence walk through structure to another occurrence and then claim
         * that occurrence's semantic witness as independent support of its own.
         * The witness contract retains both seed_index and actual source_entity_id,
         * so keep the complete coupling field while admitting into this factor only
         * semantic back-reaction emitted by the seed entity itself.
         */
        if (!JointSemanticWitness(witness) ||
            !OrientationSameId(witness.source_entity_id, seed.entity_id) ||
            IsSeedEntity(seeds, witness.target_entity_id)) {
            continue;
        }
        by_seed[static_cast<std::size_t>(witness.seed_index)].push_back(witness);
    }

    for (std::size_t seed_index = 0U; seed_index < by_seed.size(); ++seed_index) {
        auto& witness_rows = by_seed[seed_index];
        if (witness_rows.empty()) continue;
        OrientationFactorInputs input{};
        input.storage.seed_index = static_cast<std::uint64_t>(seed_index);
        input.storage.factor_id = OrientationDerivedDigest(
            FactorDomain,
            seeds[seed_index].occurrence_id,
            input.storage.seed_index);
        input.storage.law_id = OrientationDerivedDigest(
            FactorLawDomain,
            seeds[seed_index].occurrence_id,
            input.storage.seed_index);
        input.storage.slot_index = 0U;
        input.storage.values.reserve(witness_rows.size());
        input.witnesses.reserve(witness_rows.size());
        for (const auto& witness : witness_rows) {
            input.storage.values.push_back({witness.target_entity_id});
            input.witnesses.push_back(witness);
        }
        factor_inputs->push_back(std::move(input));
    }
    if (factor_inputs->empty()) return OrientationStatus::NoResponse;
    std::sort(
        factor_inputs->begin(),
        factor_inputs->end(),
        [](const auto& left, const auto& right) {
            return std::memcmp(
                left.storage.factor_id.bytes,
                right.storage.factor_id.bytes,
                sizeof(left.storage.factor_id.bytes)) < 0;
        });
    return OrientationStatus::Ok;
}
