#include "laplace/cognition_response_field.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace laplace_cognition_response_field_detail {

constexpr char InputDomain[] = "laplace-cognition-response-field-input-v1";
constexpr char EntryDomain[] = "laplace-cognition-response-field-entry-v1";
constexpr char OutputDomain[] = "laplace-cognition-response-field-output-v1";
constexpr char ReceiptDomain[] = "laplace-cognition-response-field-receipt-v1";
constexpr char ScanBindingDomain[] = "laplace-cognition-response-scan-binding-v1";
constexpr char ScanStateDomain[] = "laplace-cognition-response-scan-state-v1";
constexpr char ScanContextDomain[] = "laplace-cognition-response-scan-context-v1";
constexpr char ScanDominanceDomain[] = "laplace-cognition-response-scan-dominance-v1";
constexpr char ScanReceiptDomain[] = "laplace-cognition-response-scan-receipt-v1";

using IdKey = std::array<std::uint8_t, 16>;
using DigestKey = std::array<std::uint8_t, 32>;

IdKey Key(const laplace_id128& value) {
    IdKey key{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), key.begin());
    return key;
}

DigestKey Key(const laplace_digest256& value) {
    DigestKey key{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), key.begin());
    return key;
}

bool Zero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashDouble(blake3_hasher* const hasher, const double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    HashU64(hasher, bits);
}

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool Add(std::uint64_t* const value, const std::uint64_t increment) {
    if (*value > std::numeric_limits<std::uint64_t>::max() - increment) return false;
    *value += increment;
    return true;
}

bool Multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const output) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    *output = left * right;
    return true;
}

int RelationIndex(const std::uint32_t family) {
    switch (family) {
        case LAPLACE_OBSERVATION_QUERY_CONTAINER: return 0;
        case LAPLACE_OBSERVATION_QUERY_CONSTITUENT: return 1;
        case LAPLACE_OBSERVATION_QUERY_PREDECESSOR: return 2;
        case LAPLACE_OBSERVATION_QUERY_SUCCESSOR: return 3;
        case LAPLACE_OBSERVATION_QUERY_COOCCUR: return 4;
        case LAPLACE_OBSERVATION_QUERY_SEMANTIC: return 5;
        default: return -1;
    }
}

int SourceIndex(const std::uint32_t source) {
    switch (source) {
        case LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY: return 0;
        case LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY: return 1;
        case LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION: return 2;
        case LAPLACE_OBSERVATION_QUERY_SOURCE_GEOMETRY: return 3;
        case LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING: return 4;
        default: return -1;
    }
}

bool StandingZero(const laplace_standing_state& state) {
    return Zero(state.state_id) && Zero(state.coordinate_id) &&
        Zero(state.arena_scope_id) && Zero(state.prior_state_id) &&
        Zero(state.epoch_id) && Zero(state.rating_recipe_id) &&
        state.rating == 0.0 && state.rating_deviation == 0.0 &&
        state.volatility == 0.0 && state.eligible_match_count == 0U &&
        state.period_ordinal == 0U && state.rating_recipe_version == 0U &&
        state.flags == 0U;
}

bool StandingValid(const laplace_standing_state& state) {
    if (StandingZero(state)) return false;
    laplace_digest256 identified{};
    return laplace_standing_state_identify(&state, &identified) == LAPLACE_STANDING_OK &&
        SameDigest(identified, state.state_id);
}

bool CandidateValid(const laplace_cognition_observation_candidate& candidate) {
    const auto relation_index = RelationIndex(candidate.relation_family);
    const auto source_index = SourceIndex(candidate.source_layer);
    const bool relation_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT) != 0U;
    const bool uncertainty_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT) != 0U;
    const bool standing_present =
        (candidate.flags &
         LAPLACE_COGNITION_OBSERVATION_CANDIDATE_STANDING_PRESENT) != 0U;
    laplace_id128 zero_relation{};
    if (Zero(candidate.observation_fingerprint) || candidate.multiplicity == 0U ||
        relation_index < 0 || source_index < 0 ||
        candidate.direction < LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD ||
        candidate.direction > LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC ||
        (candidate.flags & ~LAPLACE_COGNITION_OBSERVATION_CANDIDATE_KNOWN_FLAGS) != 0U ||
        candidate.reserved != 0U ||
        (!relation_present &&
         std::memcmp(candidate.relation_id.bytes, zero_relation.bytes,
                     sizeof(zero_relation.bytes)) != 0) ||
        (uncertainty_present &&
         (candidate.evidence_uncertainty_denominator == 0U ||
          candidate.evidence_uncertainty_numerator >=
              candidate.evidence_uncertainty_denominator)) ||
        (!uncertainty_present &&
         (candidate.evidence_uncertainty_numerator != 0U ||
          candidate.evidence_uncertainty_denominator != 0U))) {
        return false;
    }
    if (candidate.relation_family == LAPLACE_OBSERVATION_QUERY_SEMANTIC &&
        !relation_present) {
        return false;
    }
    if (candidate.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_STANDING) {
        return standing_present && !uncertainty_present &&
            Zero(candidate.evidence_root_fingerprint) &&
            StandingValid(candidate.standing);
    }
    return !standing_present && StandingZero(candidate.standing);
}

void HashStanding(blake3_hasher* const hasher, const laplace_standing_state& standing) {
    HashDigest(hasher, standing.state_id);
    HashDigest(hasher, standing.coordinate_id);
    HashDigest(hasher, standing.arena_scope_id);
    HashDigest(hasher, standing.prior_state_id);
    HashDigest(hasher, standing.epoch_id);
    HashDigest(hasher, standing.rating_recipe_id);
    HashDouble(hasher, standing.rating);
    HashDouble(hasher, standing.rating_deviation);
    HashDouble(hasher, standing.volatility);
    HashU64(hasher, standing.eligible_match_count);
    HashU64(hasher, standing.period_ordinal);
    HashU32(hasher, standing.rating_recipe_version);
    HashU32(hasher, standing.flags);
}

void HashCandidate(
    blake3_hasher* const hasher,
    const laplace_cognition_observation_candidate& candidate) {
    HashId(hasher, candidate.target_entity_id);
    HashId(hasher, candidate.relation_id);
    HashDigest(hasher, candidate.observation_fingerprint);
    HashDigest(hasher, candidate.evidence_root_fingerprint);
    HashU64(hasher, candidate.source_state_index);
    HashU64(hasher, candidate.source_logical_ordinal);
    HashU64(hasher, candidate.target_logical_ordinal);
    HashU64(hasher, candidate.multiplicity);
    HashU64(hasher, candidate.gap);
    HashU64(hasher, candidate.evidence_uncertainty_numerator);
    HashU64(hasher, candidate.evidence_uncertainty_denominator);
    HashU32(hasher, candidate.relation_family);
    HashU32(hasher, candidate.source_layer);
    HashU32(hasher, candidate.direction);
    HashU32(hasher, candidate.flags);
    HashStanding(hasher, candidate.standing);
}

struct Accumulator {
    laplace_cognition_response_entry entry{};
    std::set<std::uint64_t> source_states;
    std::set<DigestKey> evidence_roots;
    std::vector<laplace_cognition_observation_candidate> candidates;
};

int CompareId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes));
}

int CompareDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes));
}

bool CandidateLess(
    const laplace_cognition_observation_candidate& left,
    const laplace_cognition_observation_candidate& right) {
    if (left.source_state_index != right.source_state_index) {
        return left.source_state_index < right.source_state_index;
    }
    if (left.relation_family != right.relation_family) {
        return left.relation_family < right.relation_family;
    }
    if (left.source_layer != right.source_layer) return left.source_layer < right.source_layer;
    if (left.direction != right.direction) return left.direction < right.direction;
    if (left.source_logical_ordinal != right.source_logical_ordinal) {
        return left.source_logical_ordinal < right.source_logical_ordinal;
    }
    if (left.target_logical_ordinal != right.target_logical_ordinal) {
        return left.target_logical_ordinal < right.target_logical_ordinal;
    }
    const int relation = CompareId(left.relation_id, right.relation_id);
    if (relation != 0) return relation < 0;
    const int observation = CompareDigest(
        left.observation_fingerprint, right.observation_fingerprint);
    if (observation != 0) return observation < 0;
    const int evidence = CompareDigest(
        left.evidence_root_fingerprint, right.evidence_root_fingerprint);
    if (evidence != 0) return evidence < 0;
    if (left.multiplicity != right.multiplicity) return left.multiplicity < right.multiplicity;
    if (left.gap != right.gap) return left.gap < right.gap;
    if (left.evidence_uncertainty_numerator != right.evidence_uncertainty_numerator) {
        return left.evidence_uncertainty_numerator < right.evidence_uncertainty_numerator;
    }
    if (left.evidence_uncertainty_denominator != right.evidence_uncertainty_denominator) {
        return left.evidence_uncertainty_denominator < right.evidence_uncertainty_denominator;
    }
    if (left.flags != right.flags) return left.flags < right.flags;
    return CompareDigest(left.standing.state_id, right.standing.state_id) < 0;
}

bool CandidateGlobalLess(
    const laplace_cognition_observation_candidate& left,
    const laplace_cognition_observation_candidate& right) {
    const int target = CompareId(left.target_entity_id, right.target_entity_id);
    if (target != 0) return target < 0;
    return CandidateLess(left, right);
}

bool ProviderDispositionValid(const std::uint32_t disposition) {
    return disposition == 0U ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_UNSUPPORTED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_DENIED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_CONTRADICTED ||
        disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
}

bool ProviderValid(const laplace_cognition_observation_candidate_provider_v1& provider) {
    return provider.enumerate_candidates != nullptr &&
        !Zero(provider.provider_fingerprint) &&
        provider.maximum_candidate_records_per_expansion != 0U &&
        provider.abi_major ==
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <=
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR &&
        provider.flags == 0U && provider.reserved == 0U;
}

bool ProgramValid(const laplace_cognition_response_scan_program& program) {
    return program.version == LAPLACE_COGNITION_RESPONSE_SCAN_VERSION &&
        program.reserved == 0U && !Zero(program.program_id) &&
        !Zero(program.context_fingerprint) && !Zero(program.evidence_boundary) &&
        !Zero(program.evidence_epoch) && program.max_frontier_states != 0U &&
        program.max_candidate_records != 0U && program.max_provider_calls != 0U &&
        program.max_depth != 0U && program.relation_mask != 0U &&
        (program.relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) == 0U &&
        program.max_frontier_states <=
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) &&
        program.max_candidate_records <=
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}

void HashStateFields(
    blake3_hasher* const hasher,
    const laplace_query_search_state& state) {
    HashDigest(hasher, state.anchor_id);
    HashDigest(hasher, state.dominance_key);
    HashDigest(hasher, state.binding_fingerprint);
    HashDigest(hasher, state.obligation_fingerprint);
    HashDigest(hasher, state.context_fingerprint);
    HashDigest(hasher, state.evidence_epoch);
    HashDigest(hasher, state.boundary_id);
    HashU64(hasher, state.heuristic_cost);
    HashU32(hasher, state.depth);
    HashU32(hasher, state.flags);
}

struct ScanState final {
    laplace_id128 entity{};
    laplace_query_search_state state{};
    std::uint64_t ordinal{};
};

bool BuildSeedState(
    const laplace_cognition_response_scan_program& program,
    const laplace_observation_query_binding& binding,
    const laplace_id128& entity,
    const std::uint64_t ordinal,
    ScanState* const output) {
    if (output == nullptr ||
        laplace_observation_query_entity_coordinate(&entity, &output->state.anchor_id) !=
            LAPLACE_OBSERVATION_QUERY_OK) {
        return false;
    }
    output->entity = entity;
    output->ordinal = ordinal;
    output->state.binding_fingerprint = binding.binding_fingerprint;
    output->state.obligation_fingerprint = program.program_id;
    output->state.context_fingerprint = program.context_fingerprint;
    output->state.evidence_epoch = program.evidence_epoch;
    output->state.boundary_id = program.evidence_boundary;
    output->state.depth = 0U;

    blake3_hasher dominance{};
    blake3_hasher_init(&dominance);
    blake3_hasher_update(
        &dominance, ScanDominanceDomain, sizeof(ScanDominanceDomain) - 1U);
    HashDigest(&dominance, program.program_id);
    HashId(&dominance, entity);
    HashU64(&dominance, ordinal);
    Finish(&dominance, &output->state.dominance_key);

    blake3_hasher state{};
    blake3_hasher_init(&state);
    blake3_hasher_update(&state, ScanStateDomain, sizeof(ScanStateDomain) - 1U);
    HashDigest(&state, program.program_id);
    HashId(&state, entity);
    HashU64(&state, ordinal);
    HashStateFields(&state, output->state);
    Finish(&state, &output->state.state_id);
    return true;
}

bool BuildChildState(
    const laplace_cognition_response_scan_program& program,
    const laplace_observation_query_binding& binding,
    const ScanState& parent,
    const laplace_cognition_observation_candidate& candidate,
    const std::uint64_t ordinal,
    ScanState* const output) {
    if (output == nullptr || parent.state.depth == UINT32_MAX ||
        laplace_observation_query_entity_coordinate(
            &candidate.target_entity_id, &output->state.anchor_id) !=
            LAPLACE_OBSERVATION_QUERY_OK) {
        return false;
    }
    output->entity = candidate.target_entity_id;
    output->ordinal = ordinal;
    output->state.binding_fingerprint = binding.binding_fingerprint;
    output->state.obligation_fingerprint = program.program_id;
    output->state.evidence_epoch = program.evidence_epoch;
    output->state.boundary_id = program.evidence_boundary;
    output->state.depth = parent.state.depth + 1U;

    blake3_hasher context{};
    blake3_hasher_init(&context);
    blake3_hasher_update(&context, ScanContextDomain, sizeof(ScanContextDomain) - 1U);
    HashDigest(&context, parent.state.context_fingerprint);
    HashDigest(&context, candidate.observation_fingerprint);
    HashId(&context, candidate.relation_id);
    HashU64(&context, candidate.source_logical_ordinal);
    HashU64(&context, candidate.target_logical_ordinal);
    HashU32(&context, candidate.relation_family);
    HashU32(&context, candidate.source_layer);
    HashU32(&context, candidate.direction);
    Finish(&context, &output->state.context_fingerprint);

    blake3_hasher dominance{};
    blake3_hasher_init(&dominance);
    blake3_hasher_update(
        &dominance, ScanDominanceDomain, sizeof(ScanDominanceDomain) - 1U);
    HashDigest(&dominance, output->state.anchor_id);
    HashDigest(&dominance, output->state.context_fingerprint);
    HashU32(&dominance, output->state.depth);
    Finish(&dominance, &output->state.dominance_key);

    blake3_hasher state{};
    blake3_hasher_init(&state);
    blake3_hasher_update(&state, ScanStateDomain, sizeof(ScanStateDomain) - 1U);
    HashDigest(&state, parent.state.state_id);
    HashCandidate(&state, candidate);
    HashU64(&state, ordinal);
    HashStateFields(&state, output->state);
    Finish(&state, &output->state.state_id);
    return true;
}

void SealScanReceipt(laplace_cognition_response_scan_receipt* const receipt) {
    if (receipt == nullptr) return;
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, ScanReceiptDomain, sizeof(ScanReceiptDomain) - 1U);
    HashDigest(&hasher, receipt->provider_fingerprint);
    HashDigest(&hasher, receipt->field_receipt_id);
    HashU64(&hasher, receipt->expanded_state_count);
    HashU64(&hasher, receipt->emitted_state_count);
    HashU64(&hasher, receipt->candidate_count);
    HashU64(&hasher, receipt->response_count);
    HashU64(&hasher, receipt->provider_call_count);
    HashU64(&hasher, receipt->rows_examined);
    HashU64(&hasher, receipt->index_plan_count);
    HashU64(&hasher, receipt->crossing_count);
    HashU32(&hasher, receipt->relation_family_mask);
    HashU32(&hasher, receipt->source_layer_mask);
    HashU32(&hasher, receipt->limiting_disposition);
    HashU32(&hasher, receipt->version);
    HashU32(&hasher, receipt->status);
    Finish(&hasher, &receipt->receipt_id);
}

laplace_cognition_response_status FinishScan(
    laplace_cognition_response_scan_receipt* const receipt,
    const laplace_cognition_response_status status,
    const std::uint32_t limiting_disposition = 0U) {
    receipt->limiting_disposition = limiting_disposition;
    receipt->version = LAPLACE_COGNITION_RESPONSE_SCAN_VERSION;
    receipt->status = static_cast<std::uint32_t>(status);
    SealScanReceipt(receipt);
    return status;
}

}  // namespace laplace_cognition_response_field_detail

extern "C" laplace_cognition_response_status
laplace_cognition_response_field_measure(
    const laplace_cognition_observation_candidate* const candidates,
    const size_t candidate_count,
    laplace_cognition_response_entry* const responses,
    const size_t response_capacity,
    size_t* const response_count,
    laplace_cognition_response_receipt* const receipt) {
    using namespace laplace_cognition_response_field_detail;
    if (response_count != nullptr) *response_count = 0U;
    if (receipt != nullptr) *receipt = laplace_cognition_response_receipt{};
    if ((candidate_count != 0U && candidates == nullptr) || response_count == nullptr ||
        receipt == nullptr || (response_capacity != 0U && responses == nullptr)) {
        return LAPLACE_COGNITION_RESPONSE_INVALID_ARGUMENT;
    }

    try {
        std::vector<laplace_cognition_observation_candidate> ordered;
        if (candidate_count != 0U) {
            ordered.assign(candidates, candidates + candidate_count);
            for (const auto& candidate : ordered) {
                if (!CandidateValid(candidate)) {
                    return LAPLACE_COGNITION_RESPONSE_INVALID_CANDIDATE;
                }
            }
            std::sort(ordered.begin(), ordered.end(), CandidateGlobalLess);
        }

        std::map<IdKey, Accumulator> grouped;
        std::set<std::uint64_t> global_sources;
        std::set<DigestKey> global_roots;
        blake3_hasher input_hasher{};
        blake3_hasher_init(&input_hasher);
        blake3_hasher_update(&input_hasher, InputDomain, sizeof(InputDomain) - 1U);
        HashU64(&input_hasher, static_cast<std::uint64_t>(candidate_count));

        for (const auto& candidate : ordered) {
            HashCandidate(&input_hasher, candidate);
            auto& accumulator = grouped[Key(candidate.target_entity_id)];
            accumulator.entry.entity_id = candidate.target_entity_id;
            if (!Add(&accumulator.entry.candidate_count, 1U) ||
                !Add(&accumulator.entry.multiplicity_mass, candidate.multiplicity)) {
                return LAPLACE_COGNITION_RESPONSE_OVERFLOW;
            }
            if (accumulator.entry.candidate_count == 1U ||
                candidate.gap < accumulator.entry.minimum_gap) {
                accumulator.entry.minimum_gap = candidate.gap;
            }
            accumulator.entry.relation_family_mask |= candidate.relation_family;
            accumulator.entry.source_layer_mask |= candidate.source_layer;
            accumulator.entry.direction_mask |= UINT32_C(1) << (candidate.direction - 1U);

            const auto relation_index = RelationIndex(candidate.relation_family);
            const auto source_index = SourceIndex(candidate.source_layer);
            if (relation_index < 0 || source_index < 0 ||
                !Add(&accumulator.entry.relation_channel_mass[
                         static_cast<std::size_t>(relation_index)], candidate.multiplicity) ||
                !Add(&accumulator.entry.source_channel_mass[
                         static_cast<std::size_t>(source_index)], candidate.multiplicity)) {
                return LAPLACE_COGNITION_RESPONSE_OVERFLOW;
            }
            accumulator.source_states.insert(candidate.source_state_index);
            global_sources.insert(candidate.source_state_index);
            if (!Zero(candidate.evidence_root_fingerprint)) {
                accumulator.evidence_roots.insert(Key(candidate.evidence_root_fingerprint));
                global_roots.insert(Key(candidate.evidence_root_fingerprint));
            }
            accumulator.candidates.push_back(candidate);
        }
        Finish(&input_hasher, &receipt->input_fingerprint);

        receipt->candidate_count = static_cast<std::uint64_t>(candidate_count);
        receipt->response_count = static_cast<std::uint64_t>(grouped.size());
        receipt->distinct_source_state_count =
            static_cast<std::uint64_t>(global_sources.size());
        receipt->independent_evidence_root_count =
            static_cast<std::uint64_t>(global_roots.size());
        receipt->version = LAPLACE_COGNITION_RESPONSE_FIELD_VERSION;

        if (grouped.size() > response_capacity) {
            *response_count = grouped.size();
            receipt->status = LAPLACE_COGNITION_RESPONSE_RANGE;
            return LAPLACE_COGNITION_RESPONSE_RANGE;
        }

        std::size_t output_index = 0U;
        blake3_hasher output_hasher{};
        blake3_hasher_init(&output_hasher);
        blake3_hasher_update(&output_hasher, OutputDomain, sizeof(OutputDomain) - 1U);
        for (auto& pair : grouped) {
            auto& accumulator = pair.second;
            accumulator.entry.source_state_count =
                static_cast<std::uint64_t>(accumulator.source_states.size());
            accumulator.entry.independent_evidence_root_count =
                static_cast<std::uint64_t>(accumulator.evidence_roots.size());
            std::sort(
                accumulator.candidates.begin(), accumulator.candidates.end(), CandidateLess);

            blake3_hasher entry_hasher{};
            blake3_hasher_init(&entry_hasher);
            blake3_hasher_update(&entry_hasher, EntryDomain, sizeof(EntryDomain) - 1U);
            HashId(&entry_hasher, accumulator.entry.entity_id);
            HashU64(&entry_hasher, accumulator.entry.source_state_count);
            HashU64(&entry_hasher, accumulator.entry.candidate_count);
            HashU64(&entry_hasher, accumulator.entry.multiplicity_mass);
            HashU64(&entry_hasher, accumulator.entry.minimum_gap);
            HashU64(&entry_hasher, accumulator.entry.independent_evidence_root_count);
            for (const auto mass : accumulator.entry.relation_channel_mass) {
                HashU64(&entry_hasher, mass);
            }
            for (const auto mass : accumulator.entry.source_channel_mass) {
                HashU64(&entry_hasher, mass);
            }
            HashU32(&entry_hasher, accumulator.entry.relation_family_mask);
            HashU32(&entry_hasher, accumulator.entry.source_layer_mask);
            HashU32(&entry_hasher, accumulator.entry.direction_mask);
            for (const auto& candidate : accumulator.candidates) {
                HashCandidate(&entry_hasher, candidate);
            }
            Finish(&entry_hasher, &accumulator.entry.response_fingerprint);

            responses[output_index] = accumulator.entry;
            HashDigest(&output_hasher, responses[output_index].response_fingerprint);
            ++output_index;
        }
        Finish(&output_hasher, &receipt->output_fingerprint);
        *response_count = output_index;
        receipt->status = LAPLACE_COGNITION_RESPONSE_OK;

        blake3_hasher receipt_hasher{};
        blake3_hasher_init(&receipt_hasher);
        blake3_hasher_update(
            &receipt_hasher, ReceiptDomain, sizeof(ReceiptDomain) - 1U);
        HashDigest(&receipt_hasher, receipt->input_fingerprint);
        HashDigest(&receipt_hasher, receipt->output_fingerprint);
        HashU64(&receipt_hasher, receipt->candidate_count);
        HashU64(&receipt_hasher, receipt->response_count);
        HashU64(&receipt_hasher, receipt->distinct_source_state_count);
        HashU64(&receipt_hasher, receipt->independent_evidence_root_count);
        HashU32(&receipt_hasher, receipt->version);
        HashU32(&receipt_hasher, receipt->status);
        Finish(&receipt_hasher, &receipt->receipt_id);
        return LAPLACE_COGNITION_RESPONSE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_RESPONSE_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_COGNITION_RESPONSE_RANGE;
    }
}

extern "C" laplace_cognition_response_status
laplace_cognition_response_field_scan(
    const laplace_cognition_response_scan_program* const program,
    const laplace_id128* const initial_entities,
    const size_t initial_entity_count,
    const laplace_cognition_observation_candidate_provider_v1* const provider,
    laplace_cognition_observation_candidate* const candidates,
    const size_t candidate_capacity,
    size_t* const candidate_count,
    laplace_cognition_response_entry* const responses,
    const size_t response_capacity,
    size_t* const response_count,
    laplace_cognition_response_scan_receipt* const receipt) {
    using namespace laplace_cognition_response_field_detail;
    if (candidate_count != nullptr) *candidate_count = 0U;
    if (response_count != nullptr) *response_count = 0U;
    if (receipt != nullptr) *receipt = laplace_cognition_response_scan_receipt{};
    if (program == nullptr || initial_entities == nullptr || initial_entity_count == 0U ||
        provider == nullptr || candidate_count == nullptr || response_count == nullptr ||
        receipt == nullptr || (candidate_capacity != 0U && candidates == nullptr) ||
        (response_capacity != 0U && responses == nullptr) || !ProgramValid(*program) ||
        !ProviderValid(*provider) ||
        initial_entity_count > static_cast<std::size_t>(program->max_frontier_states)) {
        return LAPLACE_COGNITION_RESPONSE_INVALID_ARGUMENT;
    }
    receipt->provider_fingerprint = provider->provider_fingerprint;
    receipt->version = LAPLACE_COGNITION_RESPONSE_SCAN_VERSION;

    try {
        laplace_observation_query_binding binding{};
        binding.anchor_entity_id = initial_entities[0];
        binding.relation_mask = program->relation_mask;
        binding.maximum_results = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                program->max_candidate_records,
                static_cast<std::uint64_t>(UINT32_MAX)));
        if (binding.maximum_results == 0U ||
            laplace_observation_query_binding_identify(
                &binding, &binding.binding_fingerprint) != LAPLACE_OBSERVATION_QUERY_OK) {
            return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_INVALID_ARGUMENT);
        }

        blake3_hasher binding_hasher{};
        blake3_hasher_init(&binding_hasher);
        blake3_hasher_update(
            &binding_hasher, ScanBindingDomain, sizeof(ScanBindingDomain) - 1U);
        HashDigest(&binding_hasher, program->program_id);
        HashDigest(&binding_hasher, binding.binding_fingerprint);
        HashDigest(&binding_hasher, program->context_fingerprint);
        HashDigest(&binding_hasher, program->evidence_boundary);
        HashDigest(&binding_hasher, program->evidence_epoch);
        HashU32(&binding_hasher, program->max_depth);
        laplace_digest256 scan_scope{};
        Finish(&binding_hasher, &scan_scope);

        std::vector<ScanState> frontier;
        frontier.reserve(initial_entity_count);
        std::set<DigestKey> published_states;
        std::uint64_t next_ordinal = 0U;
        for (std::size_t index = 0U; index < initial_entity_count; ++index) {
            ScanState state{};
            if (!BuildSeedState(
                    *program, binding, initial_entities[index], next_ordinal, &state)) {
                return FinishScan(
                    receipt, LAPLACE_COGNITION_RESPONSE_INVALID_ARGUMENT);
            }
            if (next_ordinal == std::numeric_limits<std::uint64_t>::max()) {
                return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_OVERFLOW);
            }
            ++next_ordinal;
            published_states.insert(Key(state.state.state_id));
            frontier.push_back(state);
        }
        receipt->emitted_state_count = static_cast<std::uint64_t>(frontier.size());

        std::vector<laplace_cognition_observation_candidate> all_candidates;
        all_candidates.reserve(static_cast<std::size_t>(program->max_candidate_records));

        while (!frontier.empty()) {
            if (frontier.front().state.depth >= program->max_depth) break;
            std::vector<ScanState> next_frontier;
            std::set<DigestKey> next_state_ids;
            std::size_t offset = 0U;

            while (offset < frontier.size()) {
                if (receipt->provider_call_count >= program->max_provider_calls) {
                    return FinishScan(
                        receipt, LAPLACE_COGNITION_RESPONSE_INCOMPLETE,
                        LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
                }
                const std::uint64_t remaining =
                    program->max_candidate_records -
                    static_cast<std::uint64_t>(all_candidates.size());
                const std::uint64_t per_state =
                    provider->maximum_candidate_records_per_expansion;
                if (remaining < per_state) {
                    return FinishScan(
                        receipt, LAPLACE_COGNITION_RESPONSE_INCOMPLETE,
                        LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
                }
                const std::uint64_t maximum_batch = remaining / per_state;
                const std::size_t batch_count = std::min<std::size_t>(
                    frontier.size() - offset,
                    static_cast<std::size_t>(std::min<std::uint64_t>(
                        maximum_batch,
                        static_cast<std::uint64_t>(
                            std::numeric_limits<std::size_t>::max()))));
                if (batch_count == 0U) {
                    return FinishScan(
                        receipt, LAPLACE_COGNITION_RESPONSE_INCOMPLETE,
                        LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
                }
                std::uint64_t batch_capacity_u64 = 0U;
                if (!Multiply(
                        per_state, static_cast<std::uint64_t>(batch_count),
                        &batch_capacity_u64) ||
                    batch_capacity_u64 > static_cast<std::uint64_t>(
                        std::numeric_limits<std::size_t>::max())) {
                    return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_OVERFLOW);
                }
                const auto batch_capacity = static_cast<std::size_t>(batch_capacity_u64);

                std::vector<laplace_id128> sources(batch_count);
                std::vector<laplace_query_search_state> states(batch_count);
                std::vector<std::uint64_t> costs(batch_count, 0U);
                for (std::size_t local = 0U; local < batch_count; ++local) {
                    sources[local] = frontier[offset + local].entity;
                    states[local] = frontier[offset + local].state;
                }
                std::vector<laplace_cognition_observation_candidate> staged(
                    batch_capacity);
                std::size_t staged_count = 0U;
                laplace_cognition_observation_candidate_usage usage{};
                const int provider_status = provider->enumerate_candidates(
                    provider->state, &binding, sources.data(), states.data(), costs.data(),
                    batch_count, staged.data(), staged.size(), &staged_count, &usage);
                ++receipt->provider_call_count;
                if (provider_status != 0) {
                    return FinishScan(
                        receipt, LAPLACE_COGNITION_RESPONSE_PROVIDER_FAILURE);
                }
                if (staged_count > staged.size() || usage.flags != 0U ||
                    !ProviderDispositionValid(usage.limiting_disposition) ||
                    (usage.limiting_disposition != 0U && staged_count != 0U)) {
                    return FinishScan(
                        receipt, LAPLACE_COGNITION_RESPONSE_PROVIDER_CONTRACT);
                }
                if (!Add(&receipt->rows_examined, usage.rows_examined) ||
                    !Add(&receipt->index_plan_count, usage.index_plan_count) ||
                    !Add(&receipt->crossing_count, usage.crossing_count) ||
                    !Add(
                        &receipt->expanded_state_count,
                        static_cast<std::uint64_t>(batch_count))) {
                    return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_OVERFLOW);
                }
                if (usage.limiting_disposition != 0U) {
                    return FinishScan(
                        receipt, LAPLACE_COGNITION_RESPONSE_INCOMPLETE,
                        usage.limiting_disposition);
                }

                for (std::size_t candidate_index = 0U;
                     candidate_index < staged_count; ++candidate_index) {
                    auto candidate = staged[candidate_index];
                    if (!CandidateValid(candidate) ||
                        candidate.source_state_index >=
                            static_cast<std::uint64_t>(batch_count)) {
                        return FinishScan(
                            receipt, LAPLACE_COGNITION_RESPONSE_PROVIDER_CONTRACT);
                    }
                    const std::size_t local_source =
                        static_cast<std::size_t>(candidate.source_state_index);
                    const auto& parent = frontier[offset + local_source];
                    candidate.source_state_index = parent.ordinal;
                    receipt->relation_family_mask |= candidate.relation_family;
                    receipt->source_layer_mask |= candidate.source_layer;
                    all_candidates.push_back(candidate);

                    ScanState child{};
                    if (!BuildChildState(
                            *program, binding, parent, candidate, next_ordinal, &child)) {
                        return FinishScan(
                            receipt, LAPLACE_COGNITION_RESPONSE_PROVIDER_CONTRACT);
                    }
                    if (next_ordinal == std::numeric_limits<std::uint64_t>::max()) {
                        return FinishScan(
                            receipt, LAPLACE_COGNITION_RESPONSE_OVERFLOW);
                    }
                    ++next_ordinal;
                    const auto state_key = Key(child.state.state_id);
                    const bool new_state = published_states.insert(state_key).second;
                    if (new_state) {
                        if (!Add(&receipt->emitted_state_count, 1U)) {
                            return FinishScan(
                                receipt, LAPLACE_COGNITION_RESPONSE_OVERFLOW);
                        }
                        if (child.state.depth < program->max_depth &&
                            next_state_ids.insert(state_key).second) {
                            if (next_frontier.size() >= static_cast<std::size_t>(
                                    program->max_frontier_states)) {
                                return FinishScan(
                                    receipt, LAPLACE_COGNITION_RESPONSE_INCOMPLETE,
                                    LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
                            }
                            next_frontier.push_back(child);
                        }
                    }
                }
                offset += batch_count;
            }
            frontier = std::move(next_frontier);
        }

        std::vector<laplace_cognition_response_entry> measured(
            all_candidates.size());
        std::size_t measured_count = 0U;
        laplace_cognition_response_receipt field_receipt{};
        const auto field_status = laplace_cognition_response_field_measure(
            all_candidates.empty() ? nullptr : all_candidates.data(),
            all_candidates.size(), measured.empty() ? nullptr : measured.data(),
            measured.size(), &measured_count, &field_receipt);
        if (field_status != LAPLACE_COGNITION_RESPONSE_OK) {
            return FinishScan(receipt, field_status);
        }
        measured.resize(measured_count);
        receipt->field_receipt_id = field_receipt.receipt_id;
        receipt->candidate_count = static_cast<std::uint64_t>(all_candidates.size());
        receipt->response_count = static_cast<std::uint64_t>(measured.size());

        *candidate_count = all_candidates.size();
        *response_count = measured.size();
        if (all_candidates.size() > candidate_capacity ||
            measured.size() > response_capacity) {
            return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_RANGE);
        }
        if (!all_candidates.empty()) {
            std::copy(all_candidates.begin(), all_candidates.end(), candidates);
        }
        if (!measured.empty()) {
            std::copy(measured.begin(), measured.end(), responses);
        }
        (void)scan_scope;
        return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_OK);
    } catch (const std::bad_alloc&) {
        return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_MEMORY_FAILURE);
    } catch (const std::length_error&) {
        return FinishScan(receipt, LAPLACE_COGNITION_RESPONSE_RANGE);
    }
}
