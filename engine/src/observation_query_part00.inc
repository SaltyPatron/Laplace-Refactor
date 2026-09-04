#include "laplace/observation_query.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <utility>
#include <vector>

namespace laplace_observation_query_detail {

constexpr char EntityCoordinateDomain[] = "laplace-observation-query-entity-coordinate-v1";
constexpr char BindingDomain[] = "laplace-observation-query-binding-v1";
constexpr char IndexDomain[] = "laplace-observation-query-index-v1";
constexpr char StateDomain[] = "laplace-observation-query-state-v1";
constexpr char DominanceDomain[] = "laplace-observation-query-dominance-v1";
constexpr char ContextDomain[] = "laplace-observation-query-context-v1";
constexpr char LawDomain[] = "laplace-observation-query-law-v1";
constexpr char CalculationDomain[] = "laplace-observation-query-calculation-v1";
constexpr char TransitionDomain[] = "laplace-observation-query-transition-v1";
constexpr char EnumerationReceiptDomain[] = "laplace-observation-query-enumeration-receipt-v1";
constexpr char OperationDomain[] = "laplace-observation-query-operation-v1";
constexpr char OperationOperandsDomain[] = "laplace-observation-query-operation-operands-v1";
constexpr char OperationPreconditionsDomain[] = "laplace-observation-query-operation-preconditions-v1";
constexpr char OperationEffectDomain[] = "laplace-observation-query-operation-effect-v1";
constexpr char OperationReceiptContractDomain[] = "laplace-observation-query-operation-receipt-contract-v1";
constexpr char SearchProgramDomain[] = "laplace-observation-query-search-program-v1";
constexpr char SearchGoalDomain[] = "laplace-observation-query-search-goal-v1";
constexpr char SearchResultDomain[] = "laplace-observation-query-search-result-v1";
constexpr char ResolutionReceiptDomain[] = "laplace-observation-query-resolution-receipt-v1";
constexpr char ExecutionReceiptDomain[] = "laplace-observation-query-execution-receipt-v1";

using IdKey = std::array<std::uint8_t, 16>;
using DigestKey = std::array<std::uint8_t, 32>;

struct Location final {
    std::size_t container_index{};
    std::size_t occurrence_index{};
};

struct Container final {
    laplace_persistence_physicality_record physicality{};
    std::vector<laplace_persistence_trajectory_segment_record> segments;
    std::vector<laplace_trajectory_carrier> carriers;
    std::vector<laplace_composition_occurrence> occurrences;
};

struct Candidate final {
    laplace_id128 target_entity_id{};
    laplace_digest256 physicality_id{};
    std::uint64_t source_logical_ordinal{};
    std::uint64_t target_logical_ordinal{};
    std::uint64_t multiplicity{1U};
    std::uint64_t gap{};
    std::uint32_t relation{};
};

bool DigestZero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool BytesZero(const std::uint8_t* const bytes, const std::size_t count) {
    std::uint8_t aggregate = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        aggregate = static_cast<std::uint8_t>(aggregate | bytes[index]);
    }
    return aggregate == 0U;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

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

laplace_id128 IdFromKey(const IdKey& key) {
    laplace_id128 value{};
    std::copy(key.begin(), key.end(), std::begin(value.bytes));
    return value;
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

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

void StartDomain(blake3_hasher* const hasher, const char* const domain, const std::size_t bytes) {
    blake3_hasher_init(hasher);
    blake3_hasher_update(hasher, domain, bytes);
}

template <std::size_t Count>
void StartDomain(blake3_hasher* const hasher, const char (&domain)[Count]) {
    StartDomain(hasher, domain, Count - 1U);
}

bool Add(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const output) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return false;
    }
    *output = left + right;
    return true;
}

std::uint64_t AbsoluteDistance(
    const std::uint64_t left,
    const std::uint64_t right) {
    return left >= right ? left - right : right - left;
}

laplace_observation_query_status EntityCoordinate(
    const laplace_id128& entity_id,
    laplace_digest256* const coordinate) {
    if (coordinate == nullptr) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    blake3_hasher hasher{};
    StartDomain(&hasher, EntityCoordinateDomain);
    HashId(&hasher, entity_id);
    Finish(&hasher, coordinate);
    return LAPLACE_OBSERVATION_QUERY_OK;
}

bool BindingFieldsValid(const laplace_observation_query_binding& binding) {
    if (binding.relation_mask == 0U ||
        (binding.relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) != 0U ||
        binding.maximum_results == 0U ||
        (binding.flags & ~LAPLACE_OBSERVATION_QUERY_BINDING_KNOWN_FLAGS) != 0U ||
        binding.reserved != 0U) {
        return false;
    }
    if ((binding.flags & LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT) == 0U &&
        !BytesZero(binding.goal_entity_id.bytes, sizeof(binding.goal_entity_id.bytes))) {
        return false;
    }
    return true;
}

laplace_observation_query_status BindingFingerprint(
    const laplace_observation_query_binding& binding,
    laplace_digest256* const fingerprint) {
    if (fingerprint == nullptr || !BindingFieldsValid(binding)) {
        return LAPLACE_OBSERVATION_QUERY_BINDING_INVALID;
    }
    blake3_hasher hasher{};
    StartDomain(&hasher, BindingDomain);
    HashId(&hasher, binding.anchor_entity_id);
    HashId(&hasher, binding.goal_entity_id);
    HashU32(&hasher, binding.relation_mask);
    HashU32(&hasher, binding.maximum_results);
    HashU32(&hasher, binding.flags);
    Finish(&hasher, fingerprint);
    return LAPLACE_OBSERVATION_QUERY_OK;
}

bool PhysicalityLess(
    const laplace_persistence_physicality_record& left,
    const laplace_persistence_physicality_record& right) {
    return std::memcmp(
               left.physicality_id.bytes, right.physicality_id.bytes,
               sizeof(left.physicality_id.bytes)) < 0;
}

bool BindingLess(
    const laplace_observation_query_binding& left,
    const laplace_observation_query_binding& right) {
    return std::memcmp(
               left.binding_fingerprint.bytes, right.binding_fingerprint.bytes,
               sizeof(left.binding_fingerprint.bytes)) < 0;
}

bool SegmentLess(
    const laplace_persistence_trajectory_segment_record& left,
    const laplace_persistence_trajectory_segment_record& right) {
    return left.vertex_index < right.vertex_index;
}

bool CandidateLess(const Candidate& left, const Candidate& right) {
    if (left.relation != right.relation) return left.relation < right.relation;
    const int physicality_order = std::memcmp(
        left.physicality_id.bytes, right.physicality_id.bytes,
        sizeof(left.physicality_id.bytes));
    if (physicality_order != 0) return physicality_order < 0;
    if (left.source_logical_ordinal != right.source_logical_ordinal) {
        return left.source_logical_ordinal < right.source_logical_ordinal;
    }
    if (left.target_logical_ordinal != right.target_logical_ordinal) {
        return left.target_logical_ordinal < right.target_logical_ordinal;
    }
    const int entity_order = std::memcmp(
        left.target_entity_id.bytes, right.target_entity_id.bytes,
        sizeof(left.target_entity_id.bytes));
    if (entity_order != 0) return entity_order < 0;
    if (left.multiplicity != right.multiplicity) {
        return left.multiplicity < right.multiplicity;
    }
    return left.gap < right.gap;
}

std::uint32_t DirectionForRelation(const std::uint32_t relation) {
    switch (relation) {
        case LAPLACE_OBSERVATION_QUERY_CONTAINER:
        case LAPLACE_OBSERVATION_QUERY_PREDECESSOR:
            return LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE;
        case LAPLACE_OBSERVATION_QUERY_CONSTITUENT:
        case LAPLACE_OBSERVATION_QUERY_SUCCESSOR:
            return LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
        case LAPLACE_OBSERVATION_QUERY_COOCCUR:
            return LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC;
        default:
            return 0U;
    }
}

}  // namespace laplace_observation_query_detail

struct laplace_observation_query_index {
    laplace_observation_query_index_summary summary{};
    std::vector<laplace_observation_query_detail::Container> containers;
    std::map<
        laplace_observation_query_detail::IdKey,
        std::vector<laplace_observation_query_detail::Location>> occurrences_by_entity;
    std::map<
        laplace_observation_query_detail::IdKey,
        std::vector<std::size_t>> containers_by_entity;
    std::map<
        laplace_observation_query_detail::DigestKey,
        laplace_observation_query_detail::IdKey> coordinate_to_entity;
    std::map<
        laplace_observation_query_detail::DigestKey,
        laplace_observation_query_binding> bindings;
};

namespace laplace_observation_query_detail {

laplace_observation_query_status RegisterEntity(
    laplace_observation_query_index* const index,
    const laplace_id128& entity_id) {
    laplace_digest256 coordinate{};
    const auto coordinate_status = EntityCoordinate(entity_id, &coordinate);
    if (coordinate_status != LAPLACE_OBSERVATION_QUERY_OK) {
        return coordinate_status;
    }
    const auto key = Key(entity_id);
    const auto [found, inserted] = index->coordinate_to_entity.emplace(Key(coordinate), key);
    if (!inserted && found->second != key) {
        return LAPLACE_OBSERVATION_QUERY_COLLISION;
    }
    return LAPLACE_OBSERVATION_QUERY_OK;
}

laplace_observation_query_status ValidateAndDecodeContainer(
    laplace_observation_query_index* const index,
    Container* const container) {
    if (container->physicality.vertex_count == 0U) {
        if (!container->segments.empty()) {
            return LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID;
        }
        return LAPLACE_OBSERVATION_QUERY_OK;
    }
    if (container->physicality.vertex_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        container->segments.size() !=
            static_cast<std::size_t>(container->physicality.vertex_count)) {
        return LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID;
    }
    std::sort(container->segments.begin(), container->segments.end(), SegmentLess);
    container->carriers.reserve(container->segments.size());
    for (std::size_t segment_index = 0U;
         segment_index < container->segments.size(); ++segment_index) {
        const auto& segment = container->segments[segment_index];
        if (segment.vertex_index != static_cast<std::uint64_t>(segment_index) ||
            !SameDigest(segment.physicality_id, container->physicality.physicality_id)) {
            return LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID;
        }
        container->carriers.push_back(segment.carrier);
    }

    laplace_digest256 trajectory_fingerprint{};
    if (laplace_persistence_trajectory_fingerprint(
            container->carriers.data(), container->carriers.size(),
            &trajectory_fingerprint) != LAPLACE_PERSISTENCE_OK ||
        !SameDigest(
            trajectory_fingerprint,
            container->physicality.trajectory_fingerprint)) {
        return LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID;
    }

    container->occurrences.resize(container->carriers.size());
    std::uint64_t logical_count = 0U;
    if (laplace_trajectory_composition_decode(
            container->carriers.data(), container->carriers.size(),
            container->occurrences.data(), container->occurrences.size(),
            &logical_count) != LAPLACE_TRAJECTORY_OK ||
        logical_count != container->physicality.logical_count) {
        return LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID;
    }

    std::uint64_t next_logical_total = 0U;
    if (!Add(
            index->summary.logical_occurrence_count, logical_count,
            &next_logical_total)) {
        return LAPLACE_OBSERVATION_QUERY_OVERFLOW;
    }
    index->summary.logical_occurrence_count = next_logical_total;
    std::uint64_t next_run_total = 0U;
    if (!Add(
            index->summary.occurrence_run_count,
            static_cast<std::uint64_t>(container->occurrences.size()),
            &next_run_total)) {
        return LAPLACE_OBSERVATION_QUERY_OVERFLOW;
    }
    index->summary.occurrence_run_count = next_run_total;

    const std::size_t container_index =
        static_cast<std::size_t>(container - index->containers.data());
    for (std::size_t occurrence_index = 0U;
         occurrence_index < container->occurrences.size(); ++occurrence_index) {
        const auto& occurrence = container->occurrences[occurrence_index];
        auto status = RegisterEntity(index, occurrence.entity_id);
        if (status != LAPLACE_OBSERVATION_QUERY_OK) return status;
        index->occurrences_by_entity[Key(occurrence.entity_id)].push_back(
            Location{container_index, occurrence_index});
    }
    return LAPLACE_OBSERVATION_QUERY_OK;
}

void HashIndex(
    const laplace_observation_query_index& index,
    laplace_digest256* const fingerprint) {
    blake3_hasher hasher{};
    StartDomain(&hasher, IndexDomain);
    HashDigest(&hasher, index.summary.boundary_id);
    HashDigest(&hasher, index.summary.evidence_epoch);
    HashU64(&hasher, index.summary.physicality_count);
    HashU64(&hasher, index.summary.trajectory_segment_count);
    HashU64(&hasher, index.summary.occurrence_run_count);
    HashU64(&hasher, index.summary.logical_occurrence_count);
    HashU64(&hasher, index.summary.binding_count);
    HashU64(&hasher, index.summary.maximum_candidate_records_per_expansion);
    for (const auto& container : index.containers) {
        HashDigest(&hasher, container.physicality.physicality_id);
        HashId(&hasher, container.physicality.entity_id);
        HashDigest(&hasher, container.physicality.trajectory_fingerprint);
        HashU64(&hasher, container.physicality.logical_count);
        HashU64(&hasher, container.physicality.vertex_count);
    }
    for (const auto& [fingerprint_key, binding] : index.bindings) {
        (void)fingerprint_key;
        HashDigest(&hasher, binding.binding_fingerprint);
    }
    Finish(&hasher, fingerprint);
}

}  // namespace laplace_observation_query_detail

extern "C" laplace_observation_query_status
laplace_observation_query_entity_coordinate(
    const laplace_id128* const entity_id,
    laplace_digest256* const coordinate) {
    if (entity_id == nullptr || coordinate == nullptr) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    return laplace_observation_query_detail::EntityCoordinate(*entity_id, coordinate);
}

extern "C" laplace_observation_query_status
laplace_observation_query_binding_identify(
    const laplace_observation_query_binding* const binding,
    laplace_digest256* const binding_fingerprint) {
    if (binding == nullptr || binding_fingerprint == nullptr) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    return laplace_observation_query_detail::BindingFingerprint(
        *binding, binding_fingerprint);
}

extern "C" laplace_observation_query_status
laplace_observation_query_index_create(
    const laplace_observation_query_index_input* const input,
    laplace_observation_query_index** const index) {
    using namespace laplace_observation_query_detail;
    if (index != nullptr) *index = nullptr;
    if (input == nullptr || index == nullptr ||
        input->physicalities == nullptr || input->physicality_count == 0U ||
        (input->trajectory_segments == nullptr && input->trajectory_segment_count != 0U) ||
        input->bindings == nullptr || input->binding_count == 0U ||
        input->maximum_candidate_records_per_expansion == 0U ||
        input->flags != 0U || input->reserved != 0U ||
        DigestZero(input->boundary_id) || DigestZero(input->evidence_epoch)) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    try {
        auto* created = new laplace_observation_query_index{};
        created->summary.boundary_id = input->boundary_id;
        created->summary.evidence_epoch = input->evidence_epoch;
        created->summary.physicality_count =
            static_cast<std::uint64_t>(input->physicality_count);
        created->summary.trajectory_segment_count =
            static_cast<std::uint64_t>(input->trajectory_segment_count);
        created->summary.binding_count =
            static_cast<std::uint64_t>(input->binding_count);
        created->summary.maximum_candidate_records_per_expansion =
            input->maximum_candidate_records_per_expansion;

        std::vector<laplace_persistence_physicality_record> ordered_physicalities(
            input->physicalities,
            input->physicalities + input->physicality_count);
        std::sort(
            ordered_physicalities.begin(), ordered_physicalities.end(),
            PhysicalityLess);
        created->containers.reserve(ordered_physicalities.size());
        std::map<DigestKey, std::size_t> physicality_lookup;
        for (const auto& physicality : ordered_physicalities) {
            laplace_digest256 expected{};
            if (laplace_persistence_physicality_identify(
                    &physicality, &expected) != LAPLACE_PERSISTENCE_OK ||
                !SameDigest(expected, physicality.physicality_id)) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_PHYSICALITY_INVALID;
            }
            const auto physicality_key = Key(physicality.physicality_id);
            if (physicality_lookup.find(physicality_key) != physicality_lookup.end()) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_DUPLICATE;
            }
            const std::size_t container_index = created->containers.size();
            physicality_lookup.emplace(physicality_key, container_index);
            created->containers.push_back(Container{});
            created->containers.back().physicality = physicality;
            auto status = RegisterEntity(created, physicality.entity_id);
            if (status != LAPLACE_OBSERVATION_QUERY_OK) {
                delete created;
                return status;
            }
            created->containers_by_entity[Key(physicality.entity_id)].push_back(
                container_index);
        }

        for (std::size_t segment_index = 0U;
             segment_index < input->trajectory_segment_count; ++segment_index) {
            const auto& segment = input->trajectory_segments[segment_index];
            const auto found = physicality_lookup.find(Key(segment.physicality_id));
            if (found == physicality_lookup.end()) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_REFERENCE_INVALID;
            }
            created->containers[found->second].segments.push_back(segment);
        }

        for (auto& container : created->containers) {
            const auto status = ValidateAndDecodeContainer(created, &container);
            if (status != LAPLACE_OBSERVATION_QUERY_OK) {
                delete created;
                return status;
            }
        }

        std::vector<laplace_observation_query_binding> ordered_bindings(
            input->bindings, input->bindings + input->binding_count);
        std::sort(ordered_bindings.begin(), ordered_bindings.end(), BindingLess);
        for (const auto& binding : ordered_bindings) {
            if (static_cast<std::uint64_t>(binding.maximum_results) >
                input->maximum_candidate_records_per_expansion) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_BINDING_INVALID;
            }
            laplace_digest256 expected{};
            if (BindingFingerprint(binding, &expected) != LAPLACE_OBSERVATION_QUERY_OK ||
                !SameDigest(expected, binding.binding_fingerprint)) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_BINDING_INVALID;
            }
            const auto binding_key = Key(binding.binding_fingerprint);
            if (!created->bindings.emplace(binding_key, binding).second) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_DUPLICATE;
            }
            auto status = RegisterEntity(created, binding.anchor_entity_id);
            if (status != LAPLACE_OBSERVATION_QUERY_OK) {
                delete created;
                return status;
            }
            if ((binding.flags & LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT) != 0U) {
                status = RegisterEntity(created, binding.goal_entity_id);
                if (status != LAPLACE_OBSERVATION_QUERY_OK) {
                    delete created;
                    return status;
                }
            }
        }

        created->summary.indexed_entity_count =
            static_cast<std::uint64_t>(created->coordinate_to_entity.size());
        HashIndex(*created, &created->summary.index_fingerprint);
        created->summary.status = LAPLACE_OBSERVATION_QUERY_OK;
        *index = created;
        return LAPLACE_OBSERVATION_QUERY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_OBSERVATION_QUERY_MEMORY_FAILURE;
    }
}

extern "C" void laplace_observation_query_index_destroy(
    laplace_observation_query_index** const index) {
    if (index != nullptr) {
        delete *index;
        *index = nullptr;
    }
}

extern "C" laplace_observation_query_status
laplace_observation_query_index_summary_get(
    const laplace_observation_query_index* const index,
    laplace_observation_query_index_summary* const summary) {
    if (index == nullptr || summary == nullptr) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    *summary = index->summary;
    return LAPLACE_OBSERVATION_QUERY_OK;
}
