namespace laplace_cognition_prompt_structural_detail {

constexpr char ProviderDomain[] = "laplace-cognition-prompt-structural-provider-v1";
constexpr char CompositionEdgeDomain[] = "laplace-cognition-prompt-composition-edge-v1";
constexpr char SpanEdgeDomain[] = "laplace-cognition-prompt-span-edge-v1";

struct StructuralEdge final {
    laplace_id128 source{};
    laplace_id128 target{};
    laplace_digest256 observation_fingerprint{};
    std::uint64_t multiplicity{1U};
    std::uint32_t relation_family{};
    std::uint32_t direction{};
};

bool IdLess(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

bool DigestLessLocal(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

bool EdgeLess(const StructuralEdge& left, const StructuralEdge& right) {
    if (IdLess(left.source, right.source)) return true;
    if (IdLess(right.source, left.source)) return false;
    if (left.relation_family != right.relation_family) {
        return left.relation_family < right.relation_family;
    }
    if (IdLess(left.target, right.target)) return true;
    if (IdLess(right.target, left.target)) return false;
    if (left.multiplicity != right.multiplicity) {
        return left.multiplicity < right.multiplicity;
    }
    return DigestLessLocal(
        left.observation_fingerprint, right.observation_fingerprint);
}

bool SameEdgeKey(const StructuralEdge& left, const StructuralEdge& right) {
    return SameId(left.source, right.source) &&
        SameId(left.target, right.target) &&
        left.relation_family == right.relation_family;
}

bool ResolveReference(
    const laplace_cognition_prompt_admission& admission,
    const laplace_composition_operand& reference,
    laplace_id128* const entity_id) {
    if (entity_id == nullptr || reference.multiplicity == 0U ||
        reference.flags != 0U) {
        return false;
    }
    if (reference.reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
        if (reference.reference_index >= admission.known_entities.size()) {
            return false;
        }
        *entity_id = admission.known_entities[
            static_cast<std::size_t>(reference.reference_index)].entity_id;
        return true;
    }
    if (reference.reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
        if (admission.working_set == nullptr) return false;
        std::size_t result_count = 0U;
        const auto* results = laplace_composition_working_set_results(
            admission.working_set, &result_count);
        if (results == nullptr || reference.reference_index >= result_count) {
            return false;
        }
        *entity_id = results[static_cast<std::size_t>(
            reference.reference_index)].entity_id;
        return true;
    }
    return false;
}

laplace_digest256 CompositionObservation(
    const laplace_cognition_prompt_admission& admission,
    const std::uint64_t request_index,
    const std::uint64_t operand_index,
    const laplace_id128& source,
    const laplace_id128& target,
    const laplace_composition_operand& operand,
    const std::uint32_t relation_family) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, CompositionEdgeDomain, sizeof(CompositionEdgeDomain) - 1U);
    HashId(&hasher, admission.view.trunk_entity_id);
    HashDigest(&hasher, admission.view.trunk_identity_witness);
    HashId(&hasher, source);
    HashId(&hasher, target);
    HashU64(&hasher, request_index);
    HashU64(&hasher, operand_index);
    HashU64(&hasher, operand.multiplicity);
    HashU64(&hasher, operand.relationship_metadata);
    HashU32(&hasher, relation_family);
    Finish(&hasher, &output);
    return output;
}

laplace_digest256 SpanObservation(
    const laplace_cognition_prompt_admission& admission,
    const laplace_decomposition_span& parent,
    const laplace_decomposition_span& child,
    const laplace_id128& source,
    const laplace_id128& target,
    const std::uint32_t relation_family) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, SpanEdgeDomain, sizeof(SpanEdgeDomain) - 1U);
    HashId(&hasher, admission.view.trunk_entity_id);
    HashId(&hasher, source);
    HashId(&hasher, target);
    HashDigest(&hasher, parent.provider_fingerprint);
    HashDigest(&hasher, child.provider_fingerprint);
    HashU64(&hasher, parent.byte_start);
    HashU64(&hasher, parent.byte_end);
    HashU64(&hasher, child.byte_start);
    HashU64(&hasher, child.byte_end);
    HashU64(&hasher, child.kind);
    HashU64(&hasher, child.grammar_kind);
    HashU64(&hasher, child.field_kind);
    HashU64(&hasher, child.sibling_ordinal);
    HashU32(&hasher, child.depth);
    HashU32(&hasher, child.flags);
    HashU32(&hasher, child.syntax_flags);
    HashU32(&hasher, relation_family);
    Finish(&hasher, &output);
    return output;
}

void AddBidirectionalEdge(
    std::vector<StructuralEdge>* const edges,
    const laplace_id128& parent,
    const laplace_id128& child,
    const std::uint64_t multiplicity,
    const laplace_digest256& constituent_observation,
    const laplace_digest256& container_observation) {
    if (edges == nullptr || SameId(parent, child)) return;
    edges->push_back(StructuralEdge{
        parent,
        child,
        constituent_observation,
        multiplicity,
        LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
        LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD});
    edges->push_back(StructuralEdge{
        child,
        parent,
        container_observation,
        multiplicity,
        LAPLACE_OBSERVATION_QUERY_CONTAINER,
        LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE});
}

bool BuildEdges(
    const laplace_cognition_prompt_admission& admission,
    std::vector<StructuralEdge>* const edges) {
    if (edges == nullptr || admission.composition_plan == nullptr ||
        admission.decomposition == nullptr) {
        return false;
    }
    edges->clear();

    laplace_decomposition_composition_plan_view plan{};
    if (laplace_decomposition_composition_plan_view_get(
            admission.composition_plan, &plan) !=
        LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
        return false;
    }

    std::size_t result_count = 0U;
    const laplace_composition_result* results = nullptr;
    if (plan.request_count != 0U) {
        if (admission.working_set == nullptr) return false;
        results = laplace_composition_working_set_results(
            admission.working_set, &result_count);
        if (results == nullptr || result_count != plan.request_count) return false;
    }

    try {
        edges->reserve(static_cast<std::size_t>(
            plan.operand_count * UINT64_C(2) + plan.span_count * UINT64_C(2)));

        for (std::uint64_t request_index = 0U;
             request_index < plan.request_count; ++request_index) {
            const auto& request = plan.requests[request_index];
            if (request.first_operand > plan.operand_count ||
                request.operand_count > plan.operand_count - request.first_operand) {
                return false;
            }
            const auto& parent = results[static_cast<std::size_t>(request_index)].entity_id;
            for (std::uint64_t offset = 0U; offset < request.operand_count; ++offset) {
                const std::uint64_t operand_index = request.first_operand + offset;
                const auto& operand = plan.operands[operand_index];
                laplace_id128 child{};
                if (!ResolveReference(admission, operand, &child)) return false;
                const auto constituent = CompositionObservation(
                    admission, request_index, operand_index, parent, child, operand,
                    LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
                const auto container = CompositionObservation(
                    admission, request_index, operand_index, child, parent, operand,
                    LAPLACE_OBSERVATION_QUERY_CONTAINER);
                AddBidirectionalEdge(
                    edges, parent, child, operand.multiplicity,
                    constituent, container);
            }
        }

        std::size_t span_count = 0U;
        const auto* spans = laplace_decomposition_spans(
            admission.decomposition, &span_count);
        if (spans == nullptr || span_count != plan.span_count ||
            plan.span_references == nullptr || plan.span_has_content == nullptr) {
            return false;
        }
        for (std::size_t child_index = 1U; child_index < span_count; ++child_index) {
            if (plan.span_has_content[child_index] == 0U) continue;
            const auto& child_span = spans[child_index];
            if (child_span.parent_span_index >= span_count) return false;
            const auto parent_index = static_cast<std::size_t>(
                child_span.parent_span_index);
            if (plan.span_has_content[parent_index] == 0U) continue;
            laplace_id128 parent{};
            laplace_id128 child{};
            if (!ResolveReference(
                    admission, plan.span_references[parent_index], &parent) ||
                !ResolveReference(
                    admission, plan.span_references[child_index], &child)) {
                return false;
            }
            if (SameId(parent, child)) continue;
            const auto constituent = SpanObservation(
                admission, spans[parent_index], child_span, parent, child,
                LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
            const auto container = SpanObservation(
                admission, child_span, spans[parent_index], child, parent,
                LAPLACE_OBSERVATION_QUERY_CONTAINER);
            AddBidirectionalEdge(
                edges, parent, child, UINT64_C(1), constituent, container);
        }

        std::sort(edges->begin(), edges->end(), EdgeLess);
        /* Content-composition adjacency is authoritative for identical endpoint
         * pairs. A provider syntax witness may expose the same pair again, but
         * the structural search plane needs one canonical crossing per
         * source/target/family. Keep the lexicographically first exact witness. */
        edges->erase(
            std::unique(edges->begin(), edges->end(), SameEdgeKey),
            edges->end());
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

std::uint64_t MaximumOutDegree(const std::vector<StructuralEdge>& edges) {
    std::uint64_t maximum = 0U;
    std::uint64_t current = 0U;
    laplace_id128 prior{};
    bool have_prior = false;
    for (const auto& edge : edges) {
        if (!have_prior || !SameId(prior, edge.source)) {
            maximum = std::max(maximum, current);
            prior = edge.source;
            current = 0U;
            have_prior = true;
        }
        ++current;
    }
    return std::max(maximum, current);
}

laplace_digest256 ProviderFingerprint(
    const laplace_cognition_prompt_admission& admission,
    const std::uint64_t maximum_out_degree,
    const std::uint64_t edge_count) {
    laplace_digest256 output{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ProviderDomain, sizeof(ProviderDomain) - 1U);
    HashId(&hasher, admission.view.trunk_entity_id);
    HashDigest(&hasher, admission.view.trunk_identity_witness);
    HashDigest(&hasher, admission.view.decomposition_trace_fingerprint);
    HashDigest(&hasher, admission.view.composition_summary.receipt_id);
    HashU64(&hasher, maximum_out_degree);
    HashU64(&hasher, edge_count);
    Finish(&hasher, &output);
    return output;
}

int Enumerate(
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
        candidate_capacity == 0U || candidate_count == nullptr || usage == nullptr) {
        return 1;
    }
    auto& admission = *static_cast<laplace_cognition_prompt_admission*>(opaque);
    std::vector<StructuralEdge> edges;
    if (!BuildEdges(admission, &edges)) return 2;

    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->index_plan_count = UINT64_C(1);
    (void)accumulated_costs;

    for (std::size_t source_index = 0U;
         source_index < frontier_state_count; ++source_index) {
        const auto& source_id = source_entity_ids[source_index];
        for (const auto& edge : edges) {
            if (!SameId(edge.source, source_id)) continue;
            ++usage->rows_examined;
            if ((binding->relation_mask & edge.relation_family) == 0U) continue;
            if (*candidate_count >= candidate_capacity) return 3;
            auto& candidate = candidates[*candidate_count];
            candidate = laplace_cognition_observation_candidate{};
            candidate.target_entity_id = edge.target;
            candidate.observation_fingerprint = edge.observation_fingerprint;
            candidate.source_state_index = static_cast<std::uint64_t>(source_index);
            candidate.source_logical_ordinal =
                static_cast<std::uint64_t>(frontier_states[source_index].depth);
            candidate.target_logical_ordinal =
                candidate.source_logical_ordinal + UINT64_C(1);
            candidate.multiplicity = edge.multiplicity;
            candidate.gap = UINT64_C(1);
            candidate.relation_family = edge.relation_family;
            candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
            candidate.direction = edge.direction;
            ++*candidate_count;
        }
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

}  // namespace laplace_cognition_prompt_structural_detail

extern "C" laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_structural_provider(
    laplace_cognition_prompt_admission* const admission,
    laplace_cognition_observation_candidate_provider_v1* const provider) {
    using namespace laplace_cognition_prompt_structural_detail;
    if (admission == nullptr || provider == nullptr) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    }
    *provider = laplace_cognition_observation_candidate_provider_v1{};
    std::vector<StructuralEdge> edges;
    if (!BuildEdges(*admission, &edges)) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_STRUCTURAL_PROVIDER_FAILURE;
    }
    const std::uint64_t maximum_out_degree = MaximumOutDegree(edges);
    provider->state = admission;
    provider->provider_fingerprint = ProviderFingerprint(
        *admission, maximum_out_degree,
        static_cast<std::uint64_t>(edges.size()));
    provider->maximum_candidate_records_per_expansion =
        std::max<std::uint64_t>(UINT64_C(1), maximum_out_degree);
    provider->enumerate_candidates = Enumerate;
    provider->abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider->abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return LAPLACE_COGNITION_PROMPT_ADMISSION_OK;
}
