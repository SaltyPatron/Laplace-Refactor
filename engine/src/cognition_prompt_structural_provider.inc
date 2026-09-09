#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace laplace_cognition_prompt_structural_detail {

constexpr char ProviderDomain[] = "laplace-cognition-prompt-structural-provider-v2";
constexpr char SpanEdgeDomain[] = "laplace-cognition-prompt-span-edge-v1";

struct SpanEdge final {
    laplace_id128 source{};
    laplace_cognition_observation_candidate candidate{};
};

struct RetainedIndex final {
    laplace_observation_query_index* canonical{};
    std::vector<SpanEdge> spans;
    laplace_digest256 fingerprint{};
    std::uint64_t maximum_candidates{1U};
    ~RetainedIndex() { laplace_observation_query_index_destroy(&canonical); }
};

void DestroyIndex(RetainedIndex* const index) { delete index; }

bool SpanLess(const SpanEdge& left, const SpanEdge& right) {
    const int source = std::memcmp(left.source.bytes, right.source.bytes, sizeof(left.source.bytes));
    if (source != 0) return source < 0;
    if (left.candidate.relation_family != right.candidate.relation_family) {
        return left.candidate.relation_family < right.candidate.relation_family;
    }
    return std::memcmp(left.candidate.observation_fingerprint.bytes,
                       right.candidate.observation_fingerprint.bytes,
                       sizeof(left.candidate.observation_fingerprint.bytes)) < 0;
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


void AddSpan(
    RetainedIndex& index, const laplace_id128& source, const laplace_id128& target,
    const laplace_digest256& witness, const std::uint64_t ordinal,
    const std::uint32_t family) {
    SpanEdge edge{};
    edge.source = source;
    edge.candidate.target_entity_id = target;
    edge.candidate.observation_fingerprint = witness;
    edge.candidate.multiplicity = 1U;
    edge.candidate.relation_family = family;
    edge.candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
    if (family == LAPLACE_OBSERVATION_QUERY_CONSTITUENT) {
        edge.candidate.target_logical_ordinal = ordinal;
        edge.candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
    } else {
        edge.candidate.source_logical_ordinal = ordinal;
        edge.candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE;
    }
    index.spans.push_back(edge);
}

bool BuildSpans(const laplace_cognition_prompt_admission& admission, RetainedIndex& index) {
    laplace_decomposition_composition_plan_view plan{};
    if (laplace_decomposition_composition_plan_view_get(admission.composition_plan, &plan) !=
        LAPLACE_DECOMPOSITION_COMPOSITION_OK) return false;
    std::size_t count = 0U;
    const auto* spans = laplace_decomposition_spans(admission.decomposition, &count);
    if (spans == nullptr || count != plan.span_count || plan.span_references == nullptr ||
        plan.span_has_content == nullptr || count > index.spans.max_size() / 2U) return false;
    index.spans.reserve(count * 2U);
    for (std::size_t child_index = 1U; child_index < count; ++child_index) {
        if (plan.span_has_content[child_index] == 0U) continue;
        const auto& child_span = spans[child_index];
        if (child_span.parent_span_index >= count ||
            child_span.sibling_ordinal == UINT64_MAX) return false;
        const auto parent_index = static_cast<std::size_t>(child_span.parent_span_index);
        if (plan.span_has_content[parent_index] == 0U) continue;
        laplace_id128 parent{}, child{};
        if (!ResolveReference(admission, plan.span_references[parent_index], &parent) ||
            !ResolveReference(admission, plan.span_references[child_index], &child)) return false;
        if (SameId(parent, child)) continue;
        const auto ordinal = child_span.sibling_ordinal + UINT64_C(1);
        // Grammar occurrence ordinals belong to the exact parent/child span
        // witness; they are not search depth or canonical-trajectory offsets.
        AddSpan(index, parent, child,
                SpanObservation(admission, spans[parent_index], child_span, parent, child,
                                LAPLACE_OBSERVATION_QUERY_CONSTITUENT),
                ordinal, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
        AddSpan(index, child, parent,
                SpanObservation(admission, spans[parent_index], child_span, child, parent,
                                LAPLACE_OBSERVATION_QUERY_CONTAINER),
                ordinal, LAPLACE_OBSERVATION_QUERY_CONTAINER);
    }
    std::sort(index.spans.begin(), index.spans.end(), SpanLess);
    // Only an identical occurrence witness is a duplicate. Equal endpoint IDs
    // at different positions must remain independently addressable.
    index.spans.erase(std::unique(index.spans.begin(), index.spans.end(),
        [](const auto& left, const auto& right) {
            return !SpanLess(left, right) && !SpanLess(right, left);
        }), index.spans.end());
    return true;
}

bool BuildCanonical(const laplace_cognition_prompt_admission& admission, RetainedIndex& index) {
    if (admission.working_set == nullptr) return true;  // An admitted Tier-0 atom.
    laplace_composition_working_set_summary summary{};
    if (laplace_composition_working_set_summary_get(admission.working_set, &summary) !=
        LAPLACE_COMPOSITION_OK) return false;
    std::vector<laplace_persistence_physicality_record> physicalities;
    std::vector<laplace_persistence_trajectory_segment_record> segments;
    if (summary.unique_physicality_count > physicalities.max_size() ||
        summary.trajectory_vertex_count > segments.max_size()) return false;
    physicalities.resize(static_cast<std::size_t>(summary.unique_physicality_count));
    segments.reserve(static_cast<std::size_t>(summary.trajectory_vertex_count));
    for (std::size_t i = 0U; i < physicalities.size(); ++i) {
        const laplace_trajectory_carrier* carriers = nullptr;
        std::size_t count = 0U;
        if (laplace_composition_working_set_physicality_candidate_get(
                admission.working_set, i, &physicalities[i]) != LAPLACE_COMPOSITION_OK ||
            laplace_composition_working_set_trajectory_candidate_view_get(
                admission.working_set, i, &carriers, &count) != LAPLACE_COMPOSITION_OK ||
            count != physicalities[i].vertex_count ||
            count > static_cast<std::size_t>(summary.trajectory_vertex_count) - segments.size()) return false;
        std::uint64_t ordinal = 1U;
        for (std::size_t vertex = 0U; vertex < count; ++vertex) {
            laplace_persistence_trajectory_segment_record segment{};
            segment.physicality_id = physicalities[i].physicality_id;
            segment.vertex_index = static_cast<std::uint64_t>(vertex);
            segment.carrier = carriers[vertex];
            if (laplace_trajectory_composition_decode_one(
                    &segment.carrier, ordinal, &segment.occurrence) != LAPLACE_TRAJECTORY_OK ||
                !laplace_observation_query_detail::Add(
                    ordinal, segment.occurrence.run_length, &ordinal)) return false;
            segments.push_back(segment);
        }
    }
    if (segments.size() != summary.trajectory_vertex_count) return false;
    if (physicalities.empty()) return true;
    // A linear count of compressed runs bounds all enabled structural families.
    // Co-occurrence candidates are generated only on demand, never V-squared
    // adjacency materialized while admitting a prompt.
    std::map<laplace_observation_query_detail::IdKey, std::uint64_t> bounds;
    std::size_t offset = 0U;
    for (const auto& physicality : physicalities) {
        auto& parent = bounds[laplace_observation_query_detail::Key(physicality.entity_id)];
        if (!laplace_observation_query_detail::Add(parent, physicality.vertex_count, &parent)) return false;
        for (std::uint64_t i = 0U; i < physicality.vertex_count; ++i) {
            auto& bound = bounds[laplace_observation_query_detail::Key(segments[offset++].occurrence.entity_id)];
            std::uint64_t contribution = 0U;
            if (!laplace_observation_query_detail::Add(physicality.vertex_count, 5U, &contribution) ||
                !laplace_observation_query_detail::Add(bound, contribution, &bound)) return false;
        }
    }
    for (const auto& entry : bounds) index.maximum_candidates = std::max(index.maximum_candidates, entry.second);
    laplace_observation_query_index_base_input input{};
    input.physicalities = physicalities.data();
    input.physicality_count = physicalities.size();
    input.trajectory_segments = segments.data();
    input.trajectory_segment_count = segments.size();
    input.boundary_id = admission.view.trunk_identity_witness;
    input.evidence_epoch = admission.view.decomposition_trace_fingerprint;
    input.maximum_candidate_records_per_expansion = index.maximum_candidates;
    return laplace_observation_query_index_create_base(&input, &index.canonical) == LAPLACE_OBSERVATION_QUERY_OK;
}

RetainedIndex* BuildIndex(const laplace_cognition_prompt_admission& admission) {
    std::unique_ptr<RetainedIndex> index(new RetainedIndex{});
    if (!BuildCanonical(admission, *index) || !BuildSpans(admission, *index)) return nullptr;
    std::uint64_t maximum_spans = 0U, current = 0U;
    laplace_id128 previous{};
    bool present = false;
    for (const auto& span : index->spans) {
        if (!present || !SameId(previous, span.source)) {
            maximum_spans = std::max(maximum_spans, current);
            previous = span.source;
            present = true;
            current = 0U;
        }
        ++current;
    }
    maximum_spans = std::max(maximum_spans, current);
    if (!laplace_observation_query_detail::Add(index->maximum_candidates, maximum_spans,
                                             &index->maximum_candidates)) return nullptr;
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ProviderDomain, sizeof(ProviderDomain) - 1U);
    HashId(&hasher, admission.view.trunk_entity_id);
    HashDigest(&hasher, admission.view.trunk_identity_witness);
    HashDigest(&hasher, admission.view.decomposition_trace_fingerprint);
    if (index->canonical != nullptr) {
        laplace_observation_query_index_summary summary{};
        if (laplace_observation_query_index_summary_get(index->canonical, &summary) != LAPLACE_OBSERVATION_QUERY_OK) return nullptr;
        HashDigest(&hasher, summary.index_fingerprint);
    }
    HashU64(&hasher, index->maximum_candidates);
    HashU64(&hasher, static_cast<std::uint64_t>(index->spans.size()));
    for (const auto& span : index->spans) HashDigest(&hasher, span.candidate.observation_fingerprint);
    Finish(&hasher, &index->fingerprint);
    return index.release();
}

int Enumerate(
    void* const opaque, const laplace_observation_query_binding* const binding,
    const laplace_id128* const source_entity_ids,
    const laplace_query_search_state* const frontier_states,
    const std::uint64_t* const accumulated_costs, const std::size_t frontier_state_count,
    laplace_cognition_observation_candidate* const candidates, const std::size_t candidate_capacity,
    std::size_t* const candidate_count, laplace_cognition_observation_candidate_usage* const usage) {
    if (candidate_count != nullptr) *candidate_count = 0U;
    if (usage != nullptr) *usage = laplace_cognition_observation_candidate_usage{};
    if (opaque == nullptr || binding == nullptr || candidate_count == nullptr || usage == nullptr ||
        (frontier_state_count != 0U && (source_entity_ids == nullptr || frontier_states == nullptr || accumulated_costs == nullptr)) ||
        (candidate_capacity != 0U && candidates == nullptr)) return 1;
    laplace_digest256 identified{};
    if (laplace_observation_query_binding_identify(binding, &identified) != LAPLACE_OBSERVATION_QUERY_OK ||
        !SameDigest(identified, binding->binding_fingerprint)) return 1;
    if (frontier_state_count == 0U) return 0;
    const auto& index = *static_cast<const RetainedIndex*>(opaque);
    std::vector<laplace_cognition_observation_candidate> staged;
    if (candidate_capacity > staged.max_size() ||
        candidate_capacity > std::numeric_limits<std::size_t>::max() / 3U /
            sizeof(laplace_cognition_observation_candidate) ||
        frontier_state_count > std::numeric_limits<std::size_t>::max() /
            sizeof(laplace_query_search_state)) return 1;
    try {
        staged.resize(candidate_capacity);
        std::size_t count = 0U;
        laplace_cognition_observation_candidate_usage measured{};
        if (index.canonical != nullptr) {
            const auto status = laplace_observation_query_index_candidates_batch(
                index.canonical, binding, source_entity_ids, frontier_state_count,
                staged.data(), staged.size(), &count, &measured);
            if (status == LAPLACE_OBSERVATION_QUERY_OVERFLOW) {
                *usage = measured;
                usage->crossing_count = 0U;
                usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
                return 0;
            }
            if (status != LAPLACE_OBSERVATION_QUERY_OK) return 2;
        }
        for (std::size_t source = 0U; source < frontier_state_count; ++source) {
            for (const std::uint32_t family : {LAPLACE_OBSERVATION_QUERY_CONTAINER,
                                               LAPLACE_OBSERVATION_QUERY_CONSTITUENT}) {
                if ((binding->relation_mask & family) == 0U) continue;
                SpanEdge key{};
                key.source = source_entity_ids[source];
                key.candidate.relation_family = family;
                const auto first = std::lower_bound(index.spans.begin(), index.spans.end(), key, SpanLess);
                ++measured.index_plan_count;
                for (auto it = first; it != index.spans.end() && SameId(it->source, key.source) &&
                     it->candidate.relation_family == family; ++it) {
                    ++measured.rows_examined;
                    if (count == candidate_capacity) {
                        *usage = measured;
                        usage->crossing_count = 0U;
                        usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
                        return 0;
                    }
                    staged[count] = it->candidate;
                    staged[count++].source_state_index = static_cast<std::uint64_t>(source);
                }
            }
        }
#if defined(LAPLACE_TEST_PROMPT_DEPTH_ORDINAL)
        for (std::size_t i = 0U; i < count; ++i) {
            staged[i].source_logical_ordinal = frontier_states[staged[i].source_state_index].depth;
            staged[i].target_logical_ordinal = staged[i].source_logical_ordinal + 1U;
        }
#endif
        if (count != 0U) std::copy_n(staged.data(), count, candidates);
        measured.crossing_count = static_cast<std::uint64_t>(count);
        *candidate_count = count;
        *usage = measured;
        return 0;
    } catch (const std::bad_alloc&) {
        return 2;
    } catch (const std::length_error&) {
        return 2;
    }
}

}  // namespace laplace_cognition_prompt_structural_detail

extern "C" laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_structural_provider(
    laplace_cognition_prompt_admission* const admission,
    laplace_cognition_observation_candidate_provider_v1* const provider) {
    using namespace laplace_cognition_prompt_structural_detail;
    if (provider != nullptr) *provider = laplace_cognition_observation_candidate_provider_v1{};
    if (admission == nullptr || provider == nullptr) return LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT;
    try {
        const std::lock_guard<std::mutex> lock(admission->structural_index_mutex);
        if (admission->structural_index == nullptr) admission->structural_index = BuildIndex(*admission);
        if (admission->structural_index == nullptr) return LAPLACE_COGNITION_PROMPT_ADMISSION_STRUCTURAL_PROVIDER_FAILURE;
        const auto& index = *admission->structural_index;
        provider->state = admission->structural_index;
        provider->provider_fingerprint = index.fingerprint;
        provider->maximum_candidate_records_per_expansion = index.maximum_candidates;
        provider->enumerate_candidates = Enumerate;
        provider->abi_major = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
        provider->abi_minor = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_COGNITION_PROMPT_ADMISSION_MEMORY_FAILURE;
    }
    return LAPLACE_COGNITION_PROMPT_ADMISSION_OK;
}
