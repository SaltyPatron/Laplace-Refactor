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
#include <vector>

namespace laplace_cognition_response_field_detail {

constexpr char InputDomain[] = "laplace-cognition-response-field-input-v1";
constexpr char EntryDomain[] = "laplace-cognition-response-field-entry-v1";
constexpr char OutputDomain[] = "laplace-cognition-response-field-output-v1";
constexpr char ReceiptDomain[] = "laplace-cognition-response-field-receipt-v1";

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
    for (const auto byte : value.bytes) aggregate = static_cast<std::uint8_t>(aggregate | byte);
    return aggregate == 0U;
}

bool IdZero(const laplace_id128& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) aggregate = static_cast<std::uint8_t>(aggregate | byte);
    return aggregate == 0U;
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

bool CandidateValid(const laplace_cognition_observation_candidate& candidate) {
    const auto relation_index = RelationIndex(candidate.relation_family);
    const auto source_index = SourceIndex(candidate.source_layer);
    if (IdZero(candidate.target_entity_id) || Zero(candidate.observation_fingerprint) ||
        candidate.multiplicity == 0U || relation_index < 0 || source_index < 0 ||
        candidate.direction < LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD ||
        candidate.direction > LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC ||
        (candidate.flags & ~LAPLACE_COGNITION_OBSERVATION_CANDIDATE_KNOWN_FLAGS) != 0U ||
        candidate.reserved != 0U) {
        return false;
    }
    return true;
}

void HashCandidate(blake3_hasher* const hasher,
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
}

struct Accumulator {
    laplace_cognition_response_entry entry{};
    std::set<std::uint64_t> source_states;
    std::set<DigestKey> evidence_roots;
    std::vector<laplace_cognition_observation_candidate> candidates;
};

bool CandidateLess(const laplace_cognition_observation_candidate& left,
                   const laplace_cognition_observation_candidate& right) {
    if (left.source_state_index != right.source_state_index) {
        return left.source_state_index < right.source_state_index;
    }
    const int observation = std::memcmp(left.observation_fingerprint.bytes,
                                        right.observation_fingerprint.bytes,
                                        sizeof(left.observation_fingerprint.bytes));
    if (observation != 0) return observation < 0;
    if (left.relation_family != right.relation_family) {
        return left.relation_family < right.relation_family;
    }
    if (left.source_layer != right.source_layer) return left.source_layer < right.source_layer;
    if (left.direction != right.direction) return left.direction < right.direction;
    if (left.source_logical_ordinal != right.source_logical_ordinal) {
        return left.source_logical_ordinal < right.source_logical_ordinal;
    }
    return left.target_logical_ordinal < right.target_logical_ordinal;
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
    if (candidates == nullptr || candidate_count == 0U || response_count == nullptr ||
        receipt == nullptr || (response_capacity != 0U && responses == nullptr)) {
        return LAPLACE_COGNITION_RESPONSE_INVALID_ARGUMENT;
    }

    try {
        std::map<IdKey, Accumulator> grouped;
        std::set<std::uint64_t> global_sources;
        std::set<DigestKey> global_roots;
        blake3_hasher input_hasher{};
        blake3_hasher_init(&input_hasher);
        blake3_hasher_update(&input_hasher, InputDomain, sizeof(InputDomain) - 1U);
        HashU64(&input_hasher, static_cast<std::uint64_t>(candidate_count));

        for (std::size_t index = 0U; index < candidate_count; ++index) {
            const auto& candidate = candidates[index];
            if (!CandidateValid(candidate)) {
                return LAPLACE_COGNITION_RESPONSE_INVALID_CANDIDATE;
            }
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

        if (grouped.size() > response_capacity) {
            *response_count = grouped.size();
            receipt->candidate_count = static_cast<std::uint64_t>(candidate_count);
            receipt->response_count = static_cast<std::uint64_t>(grouped.size());
            receipt->distinct_source_state_count = static_cast<std::uint64_t>(global_sources.size());
            receipt->independent_evidence_root_count = static_cast<std::uint64_t>(global_roots.size());
            receipt->version = LAPLACE_COGNITION_RESPONSE_FIELD_VERSION;
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
            std::sort(accumulator.candidates.begin(), accumulator.candidates.end(), CandidateLess);

            blake3_hasher entry_hasher{};
            blake3_hasher_init(&entry_hasher);
            blake3_hasher_update(&entry_hasher, EntryDomain, sizeof(EntryDomain) - 1U);
            HashId(&entry_hasher, accumulator.entry.entity_id);
            HashU64(&entry_hasher, accumulator.entry.source_state_count);
            HashU64(&entry_hasher, accumulator.entry.candidate_count);
            HashU64(&entry_hasher, accumulator.entry.multiplicity_mass);
            HashU64(&entry_hasher, accumulator.entry.minimum_gap);
            HashU64(&entry_hasher, accumulator.entry.independent_evidence_root_count);
            for (const auto mass : accumulator.entry.relation_channel_mass) HashU64(&entry_hasher, mass);
            for (const auto mass : accumulator.entry.source_channel_mass) HashU64(&entry_hasher, mass);
            HashU32(&entry_hasher, accumulator.entry.relation_family_mask);
            HashU32(&entry_hasher, accumulator.entry.source_layer_mask);
            HashU32(&entry_hasher, accumulator.entry.direction_mask);
            for (const auto& candidate : accumulator.candidates) HashCandidate(&entry_hasher, candidate);
            Finish(&entry_hasher, &accumulator.entry.response_fingerprint);

            responses[output_index] = accumulator.entry;
            HashDigest(&output_hasher, responses[output_index].response_fingerprint);
            ++output_index;
        }
        Finish(&output_hasher, &receipt->output_fingerprint);
        *response_count = output_index;
        receipt->candidate_count = static_cast<std::uint64_t>(candidate_count);
        receipt->response_count = static_cast<std::uint64_t>(output_index);
        receipt->distinct_source_state_count = static_cast<std::uint64_t>(global_sources.size());
        receipt->independent_evidence_root_count = static_cast<std::uint64_t>(global_roots.size());
        receipt->version = LAPLACE_COGNITION_RESPONSE_FIELD_VERSION;
        receipt->status = LAPLACE_COGNITION_RESPONSE_OK;

        blake3_hasher receipt_hasher{};
        blake3_hasher_init(&receipt_hasher);
        blake3_hasher_update(&receipt_hasher, ReceiptDomain, sizeof(ReceiptDomain) - 1U);
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
    }
}
