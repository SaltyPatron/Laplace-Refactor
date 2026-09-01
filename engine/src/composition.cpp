#include "laplace/composition.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "blake3.h"

namespace {

constexpr std::string_view InputDomain{"laplace-composition-working-set-input-v1"};
constexpr std::string_view ReceiptDomain{"laplace-composition-working-set-receipt-v1"};
constexpr std::string_view ProducerDomain{"laplace-composition-working-set-producer-v1"};
constexpr std::string_view CursorDomain{"laplace-composition-working-set-cursor-v1"};
constexpr std::string_view PresenceCandidateDomain{
    "laplace-composition-presence-candidates-v1"};
constexpr std::string_view PresenceDispositionDomain{
    "laplace-composition-presence-dispositions-v1"};
constexpr std::uint64_t DefaultBatchBytes = UINT64_C(4) * 1024U * 1024U;
constexpr std::uint64_t MetadataMask = UINT64_C(0x000fffffffffffff);
constexpr std::uint64_t HasAtomMask =
    UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT;
constexpr std::uint64_t TierMask =
    static_cast<std::uint64_t>(LAPLACE_TRAJECTORY_TIER_MASK)
    << LAPLACE_TRAJECTORY_TIER_SHIFT;
constexpr std::uint64_t AtomMask =
    static_cast<std::uint64_t>(LAPLACE_TRAJECTORY_ATOM_MASK)
    << LAPLACE_TRAJECTORY_ATOM_SHIFT;
constexpr std::uint64_t StructuralMetadataMask = HasAtomMask | TierMask | AtomMask;

template <std::size_t Count>
struct ByteKey final {
    std::array<std::uint8_t, Count> bytes{};
    bool operator==(const ByteKey&) const = default;
};

template <std::size_t Count>
struct ByteKeyHash final {
    std::size_t operator()(const ByteKey<Count>& value) const noexcept {
        std::uint64_t hash = UINT64_C(1469598103934665603);
        for (const std::uint8_t byte : value.bytes) {
            hash ^= byte;
            hash *= UINT64_C(1099511628211);
        }
        return static_cast<std::size_t>(hash);
    }
};

using DigestKey = ByteKey<32>;
using IdKey = ByteKey<16>;

DigestKey Key(const laplace_digest256& value) {
    DigestKey result{};
    std::memcpy(result.bytes.data(), value.bytes, result.bytes.size());
    return result;
}

IdKey Key(const laplace_id128& value) {
    IdKey result{};
    std::memcpy(result.bytes.data(), value.bytes, result.bytes.size());
    return result;
}

bool DigestEqual(
    const laplace_digest256& left,
    const laplace_digest256& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool IdEqual(const laplace_id128& left, const laplace_id128& right) noexcept {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

void HashU8(blake3_hasher& hasher, std::uint8_t value) {
    blake3_hasher_update(&hasher, &value, sizeof(value));
}

void HashU32(blake3_hasher& hasher, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashDouble(blake3_hasher& hasher, double value) {
    std::uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    HashU64(hasher, bits);
}

void HashString(blake3_hasher& hasher, std::string_view value) {
    HashU64(hasher, static_cast<std::uint64_t>(value.size()));
    blake3_hasher_update(&hasher, value.data(), value.size());
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

void HashPhysicality(
    blake3_hasher& hasher,
    const laplace_persistence_physicality_record& value) {
    blake3_hasher_update(
        &hasher, value.physicality_id.bytes, sizeof(value.physicality_id.bytes));
    blake3_hasher_update(
        &hasher, value.entity_id.bytes, sizeof(value.entity_id.bytes));
    HashU32(hasher, value.physicality_type);
    HashU32(hasher, value.vertex_class);
    HashU32(hasher, value.recipe_version);
    HashU32(hasher, value.structural_form);
    HashU32(hasher, value.dimension_count);
    HashU32(hasher, value.flags);
    blake3_hasher_update(
        &hasher, value.recipe_fingerprint.bytes,
        sizeof(value.recipe_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, value.geometry_epoch.bytes, sizeof(value.geometry_epoch.bytes));
    blake3_hasher_update(
        &hasher, value.trajectory_fingerprint.bytes,
        sizeof(value.trajectory_fingerprint.bytes));
    for (const double component : value.centroid.component) {
        HashDouble(hasher, component);
    }
    HashDouble(hasher, value.radius);
    HashU64(hasher, value.logical_count);
    HashU64(hasher, value.vertex_count);
}

bool AddOverflow(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return true;
    }
    result = left + right;
    return false;
}

bool MultiplyOverflow(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return true;
    }
    result = left * right;
    return false;
}

bool AddMemory(
    std::uint64_t count,
    std::uint64_t width,
    std::uint64_t& total) noexcept {
    std::uint64_t bytes{};
    std::uint64_t next{};
    if (MultiplyOverflow(count, width, bytes) || AddOverflow(total, bytes, next)) {
        return false;
    }
    total = next;
    return true;
}

bool KnownValid(const laplace_composition_known_entity& known) {
    if (known.reserved != 0U || known.tier_floor > LAPLACE_COMPOSITION_TIER_MAXIMUM ||
        std::memcmp(
            known.entity_id.bytes, known.identity_witness.bytes,
            sizeof(known.entity_id.bytes)) != 0) {
        return false;
    }
    for (const double component : known.centroid.component) {
        if (!std::isfinite(component) || component < -1.0 || component > 1.0) {
            return false;
        }
    }
    if (known.has_atom > 1U ||
        (known.has_atom == 0U && known.atom != 0U) ||
        (known.has_atom != 0U &&
         (known.tier_floor != 0U || known.atom > LAPLACE_UNICODE_POSITION_MAXIMUM))) {
        return false;
    }
    if (known.has_atom != 0U) {
        laplace_id128 expected_id{};
        laplace_digest256 expected_witness{};
        if (laplace_identity_codepoint_witness(
                known.atom, &expected_id, &expected_witness) != LAPLACE_IDENTITY_OK ||
            !IdEqual(known.entity_id, expected_id) ||
            !DigestEqual(known.identity_witness, expected_witness)) {
            return false;
        }
    }
    return true;
}

struct ResolvedValue final {
    laplace_id128 entity_id{};
    laplace_digest256 identity_witness{};
    laplace_digest256 physicality_id{};
    laplace_point4d centroid{};
    double radius{};
    std::uint64_t logical_count{};
    std::uint64_t trajectory_vertex_count{};
    std::uint32_t atom{};
    std::uint8_t tier_floor{};
    bool has_atom{};
};

struct PhysicalityBundle final {
    laplace_persistence_physicality_record physicality{};
    std::vector<laplace_trajectory_carrier> carriers;
};

struct BatchSlice final {
    std::uint64_t offset{};
    std::uint64_t bytes{};
    std::uint64_t records{};
    std::uint64_t first_ordinal{};
};

struct ResourceCounts final {
    std::uint64_t unique_entity_count{};
    std::uint64_t unique_physicality_count{};
    std::uint64_t unique_trajectory_carrier_count{};
    std::uint64_t unique_occurrence_count{};
    std::uint64_t expanded_trajectory_carrier_count{};
    std::uint64_t maximum_request_operand_count{};
    std::uint64_t maximum_request_carrier_count{};
};

struct RequestCalculation final {
    ResolvedValue result{};
    std::vector<laplace_trajectory_carrier> carriers;
    laplace_persistence_physicality_record physicality{};
    bool has_physicality{};
};

}  // namespace

struct laplace_composition_working_set {
    laplace_composition_working_set_summary summary{};
    std::vector<laplace_composition_result> results;
    std::vector<laplace_composition_entity_candidate> entities;
    std::vector<PhysicalityBundle> physicalities;
    std::vector<laplace_persistence_attestation_record> occurrences;
    std::vector<std::uint8_t> entity_dispositions;
    std::vector<std::uint8_t> physicality_dispositions;
    std::vector<std::uint8_t> stream;
    std::vector<BatchSlice> slices;
    std::vector<laplace_framework_canonical_batch> batches;
    laplace_digest256 producer_fingerprint{};
    std::uint64_t preferred_batch_bytes{};
    std::uint32_t effect_disposition{LAPLACE_FRAMEWORK_EFFECT_NONE};
};

namespace {

ResolvedValue KnownResolved(const laplace_composition_known_entity& known);
laplace_composition_status ResolveOperand(
    const laplace_composition_working_set_input& input,
    const std::vector<ResolvedValue>& calculated,
    const laplace_composition_operand& operand,
    std::uint64_t request_index,
    ResolvedValue& resolved);
laplace_composition_status CalculateRequest(
    const laplace_composition_working_set_input& input,
    const std::vector<ResolvedValue>& calculated,
    std::uint64_t request_index,
    ResolvedValue& result,
    std::vector<laplace_trajectory_carrier>& carriers,
    laplace_persistence_physicality_record& physicality,
    bool& has_physicality);
laplace_composition_status MakeOccurrence(
    const laplace_composition_working_set_input& input,
    const laplace_composition_request& request,
    const ResolvedValue& result,
    laplace_persistence_attestation_record& occurrence);

struct RequestLevelTask final {
    const laplace_composition_working_set_input* input{};
    const std::vector<ResolvedValue>* calculated{};
    const std::vector<std::uint64_t>* request_indexes{};
    std::vector<RequestCalculation>* outputs{};
    std::atomic<std::uint32_t> first_failure{LAPLACE_COMPOSITION_OK};
};

laplace_execution_status CalculateRequestChunk(
    void* opaque,
    const laplace_execution_chunk* chunk,
    laplace_digest256* fingerprint) {
    if (opaque == nullptr || chunk == nullptr || fingerprint == nullptr) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    auto& task = *static_cast<RequestLevelTask*>(opaque);
    if (task.input == nullptr || task.calculated == nullptr ||
        task.request_indexes == nullptr || task.outputs == nullptr ||
        chunk->first_item > task.request_indexes->size() ||
        chunk->item_count > task.request_indexes->size() - chunk->first_item) {
        return LAPLACE_EXECUTION_INVALID_ARGUMENT;
    }
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, "laplace-composition-request-chunk-v1");
    HashU64(hasher, chunk->chunk_index);
    HashU64(hasher, chunk->first_item);
    HashU64(hasher, chunk->item_count);
    for (std::uint64_t offset = 0U; offset < chunk->item_count; ++offset) {
        const std::size_t output_index = static_cast<std::size_t>(
            chunk->first_item + offset);
        const std::uint64_t request_index =
            (*task.request_indexes)[output_index];
        RequestCalculation& output = (*task.outputs)[output_index];
        const auto status = CalculateRequest(
            *task.input, *task.calculated, request_index,
            output.result, output.carriers, output.physicality,
            output.has_physicality);
        if (status != LAPLACE_COMPOSITION_OK) {
            std::uint32_t expected = LAPLACE_COMPOSITION_OK;
            task.first_failure.compare_exchange_strong(
                expected, static_cast<std::uint32_t>(status),
                std::memory_order_acq_rel);
            return LAPLACE_EXECUTION_PROVIDER_RUN_FAILED;
        }
        HashU64(hasher, request_index);
        blake3_hasher_update(
            &hasher, output.result.identity_witness.bytes,
            sizeof(output.result.identity_witness.bytes));
        blake3_hasher_update(
            &hasher, output.result.physicality_id.bytes,
            sizeof(output.result.physicality_id.bytes));
        HashU64(hasher, output.carriers.size());
    }
    *fingerprint = Finish(hasher);
    return LAPLACE_EXECUTION_OK;
}

template <typename Consumer>
laplace_composition_status CalculateRequestLevels(
    const laplace_composition_working_set_input& input,
    Consumer&& consume) {
    try {
        const std::size_t request_count =
            static_cast<std::size_t>(input.request_count);
        std::vector<std::uint32_t> depths(request_count, 0U);
        std::uint32_t maximum_depth = 0U;
        for (std::size_t request_index = 0U;
             request_index < request_count; ++request_index) {
            const auto& request = input.requests[request_index];
            std::uint32_t depth = 0U;
            const std::uint64_t end =
                request.first_operand + request.operand_count;
            for (std::uint64_t operand_index = request.first_operand;
                 operand_index < end; ++operand_index) {
                const auto& operand = input.operands[operand_index];
                if (operand.reference_kind ==
                    LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
                    const std::uint32_t dependency_depth =
                        depths[static_cast<std::size_t>(
                            operand.reference_index)];
                    if (dependency_depth == UINT32_MAX) {
                        return LAPLACE_COMPOSITION_TIER_OVERFLOW;
                    }
                    depth = std::max(depth, dependency_depth + 1U);
                }
            }
            depths[request_index] = depth;
            maximum_depth = std::max(maximum_depth, depth);
        }

        std::vector<std::vector<std::uint64_t>> levels(
            static_cast<std::size_t>(maximum_depth) + 1U);
        for (std::size_t request_index = 0U;
             request_index < request_count; ++request_index) {
            levels[depths[request_index]].push_back(
                static_cast<std::uint64_t>(request_index));
        }

        std::vector<ResolvedValue> calculated(request_count);
        for (const auto& request_indexes : levels) {
            if (request_indexes.empty()) {
                continue;
            }
            std::vector<RequestCalculation> outputs(request_indexes.size());
            RequestLevelTask task{};
            task.input = &input;
            task.calculated = &calculated;
            task.request_indexes = &request_indexes;
            task.outputs = &outputs;

            laplace_execution_runtime_provider_v1 provider{};
            laplace_execution_oneapi_provider_state provider_state{};
            if (laplace_execution_oneapi_provider(
                    &provider_state, &provider) != LAPLACE_EXECUTION_OK &&
                laplace_execution_serial_provider(&provider) !=
                    LAPLACE_EXECUTION_OK) {
                return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
            }
            laplace_execution_work_request work{};
            work.item_count = request_indexes.size();
            work.minimum_chunk_items = 1U;
            work.outer_worker_limit = input.context->resource_grant.cpu_slots;
            work.inner_threads_per_worker = 1U;
            laplace_execution_work_receipt receipt{};
            const auto execution_status = laplace_execution_run_work(
                &input.context->resource_grant, &work, &provider,
                &task, CalculateRequestChunk, &receipt);
            if (execution_status != LAPLACE_EXECUTION_OK) {
                const auto calculation_status =
                    static_cast<laplace_composition_status>(
                        task.first_failure.load(std::memory_order_acquire));
                return calculation_status == LAPLACE_COMPOSITION_OK
                    ? LAPLACE_COMPOSITION_PERSISTENCE_INVALID
                    : calculation_status;
            }
            for (std::size_t index = 0U;
                 index < request_indexes.size(); ++index) {
                const std::uint64_t request_index = request_indexes[index];
                calculated[static_cast<std::size_t>(request_index)] =
                    outputs[index].result;
                const auto consume_status = consume(
                    request_index, outputs[index]);
                if (consume_status != LAPLACE_COMPOSITION_OK) {
                    return consume_status;
                }
            }
        }
        return LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
}

laplace_composition_status EstimatePlanningMemory(
    const laplace_composition_working_set_input& input,
    std::uint64_t& estimated) {
    estimated = 0U;
    std::uint64_t maximum_entities{};
    std::uint64_t maximum_request_operands{};
    std::uint64_t maximum_request_carriers{};
    if (AddOverflow(
            input.known_entity_count, input.request_count, maximum_entities)) {
        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
    }
    for (std::uint64_t request_index = 0U;
         request_index < input.request_count; ++request_index) {
        const auto& request = input.requests[request_index];
        maximum_request_operands =
            std::max(maximum_request_operands, request.operand_count);
        std::uint64_t request_carriers{};
        const std::uint64_t end = request.first_operand + request.operand_count;
        for (std::uint64_t operand_index = request.first_operand;
             operand_index < end; ++operand_index) {
            const std::uint64_t multiplicity = input.operands[operand_index].multiplicity;
            const std::uint64_t carriers =
                multiplicity / LAPLACE_COMPOSITION_MAXIMUM_RUN_PER_CARRIER +
                ((multiplicity % LAPLACE_COMPOSITION_MAXIMUM_RUN_PER_CARRIER) != 0U
                     ? 1U
                     : 0U);
            if (AddOverflow(request_carriers, carriers, request_carriers)) {
                return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
            }
        }
        maximum_request_carriers =
            std::max(maximum_request_carriers, request_carriers);
    }
    const std::uint64_t map_pair_bytes =
        static_cast<std::uint64_t>(sizeof(IdKey) + sizeof(DigestKey));
    if (!AddMemory(input.request_count, sizeof(ResolvedValue), estimated) ||
        !AddMemory(maximum_entities, map_pair_bytes * 4U, estimated) ||
        !AddMemory(input.request_count, sizeof(DigestKey) * 8U, estimated) ||
        !AddMemory(maximum_request_operands,
                   sizeof(laplace_id_run) +
                       sizeof(std::pair<ResolvedValue, laplace_composition_operand>),
                   estimated) ||
        !AddMemory(maximum_request_carriers,
                   sizeof(laplace_trajectory_carrier), estimated)) {
        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
    }
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_status PlanResourceCounts(
    const laplace_composition_working_set_input& input,
    ResourceCounts& resources) {
    resources = ResourceCounts{};
    std::uint64_t maximum_entities{};
    if (AddOverflow(
            input.known_entity_count, input.request_count, maximum_entities)) {
        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
    }
    try {
        std::unordered_map<IdKey, DigestKey, ByteKeyHash<16>> witnesses;
        std::unordered_set<DigestKey, ByteKeyHash<32>> entities;
        std::unordered_set<DigestKey, ByteKeyHash<32>> physicalities;
        std::unordered_set<DigestKey, ByteKeyHash<32>> occurrences;
        witnesses.reserve(static_cast<std::size_t>(maximum_entities));
        entities.reserve(static_cast<std::size_t>(maximum_entities));
        physicalities.reserve(static_cast<std::size_t>(input.request_count));
        occurrences.reserve(static_cast<std::size_t>(input.request_count));

        for (std::uint64_t index = 0U; index < input.known_entity_count; ++index) {
            const auto& known = input.known_entities[index];
            const IdKey id = Key(known.entity_id);
            const DigestKey witness = Key(known.identity_witness);
            const auto prior = witnesses.find(id);
            if (prior != witnesses.end() && !(prior->second == witness)) {
                return LAPLACE_COMPOSITION_IDENTITY_COLLISION;
            }
            witnesses.emplace(id, witness);
            entities.insert(witness);
        }

        const auto calculation_status = CalculateRequestLevels(
            input,
            [&](const std::uint64_t request_index,
                const RequestCalculation& calculation) {
            const auto& result = calculation.result;
            const auto& carriers = calculation.carriers;
            const auto& physicality = calculation.physicality;
            resources.maximum_request_operand_count = std::max(
                resources.maximum_request_operand_count,
                input.requests[request_index].operand_count);
            resources.maximum_request_carrier_count = std::max(
                resources.maximum_request_carrier_count,
                static_cast<std::uint64_t>(carriers.size()));
            if (AddOverflow(
                    resources.expanded_trajectory_carrier_count,
                    static_cast<std::uint64_t>(carriers.size()),
                    resources.expanded_trajectory_carrier_count)) {
                return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
            }

            const IdKey id = Key(result.entity_id);
            const DigestKey witness = Key(result.identity_witness);
            const auto prior = witnesses.find(id);
            if (prior != witnesses.end() && !(prior->second == witness)) {
                return LAPLACE_COMPOSITION_IDENTITY_COLLISION;
            }
            witnesses.emplace(id, witness);
            entities.insert(witness);

            if (calculation.has_physicality &&
                physicalities.insert(Key(physicality.physicality_id)).second) {
                if (AddOverflow(
                        resources.unique_trajectory_carrier_count,
                        static_cast<std::uint64_t>(carriers.size()),
                        resources.unique_trajectory_carrier_count)) {
                    return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
                }
            }

#if defined(LAPLACE_TEST_COMPOSITION_IMPLICIT_OCCURRENCE)
            const bool emit_occurrence = true;
#else
            const bool emit_occurrence =
                (input.requests[request_index].flags &
                 LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE) != 0U;
#endif
            if (emit_occurrence) {
                laplace_persistence_attestation_record occurrence{};
                const auto occurrence_status = MakeOccurrence(
                    input, input.requests[request_index], result, occurrence);
                if (occurrence_status != LAPLACE_COMPOSITION_OK) {
                    return occurrence_status;
                }
                occurrences.insert(Key(occurrence.attestation_id));
            }
            return LAPLACE_COMPOSITION_OK;
        });
        if (calculation_status != LAPLACE_COMPOSITION_OK) {
            return calculation_status;
        }
        resources.unique_entity_count =
            static_cast<std::uint64_t>(entities.size());
        resources.unique_physicality_count =
            static_cast<std::uint64_t>(physicalities.size());
        resources.unique_occurrence_count =
            static_cast<std::uint64_t>(occurrences.size());
        return LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
}

laplace_composition_status EstimateWorkingMemory(
    const laplace_composition_working_set_input& input,
    const ResourceCounts& resources,
    std::uint64_t& estimated) {
    estimated = 0U;
    std::uint64_t entity_count = resources.unique_entity_count;
    std::uint64_t physicality_count = resources.unique_physicality_count;
    std::uint64_t carrier_count = resources.unique_trajectory_carrier_count;
    std::uint64_t occurrence_count = resources.unique_occurrence_count;
#if defined(LAPLACE_TEST_COMPOSITION_REQUEST_COUNT_MEMORY)
    if (AddOverflow(input.known_entity_count, input.request_count, entity_count)) {
        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
    }
    physicality_count = input.request_count;
    carrier_count = resources.expanded_trajectory_carrier_count;
    occurrence_count = input.request_count;
#endif
    if (!AddMemory(input.request_count,
                   sizeof(laplace_composition_result), estimated) ||
        !AddMemory(input.request_count, sizeof(ResolvedValue), estimated) ||
        !AddMemory(resources.maximum_request_operand_count,
                   sizeof(laplace_id_run) +
                       sizeof(std::pair<ResolvedValue, laplace_composition_operand>),
                   estimated) ||
        !AddMemory(resources.maximum_request_carrier_count,
                   sizeof(laplace_trajectory_carrier), estimated) ||
        !AddMemory(entity_count,
                   sizeof(laplace_composition_entity_candidate) * 4U, estimated) ||
        !AddMemory(physicality_count,
                   sizeof(PhysicalityBundle) * 4U, estimated) ||
        !AddMemory(occurrence_count,
                   sizeof(laplace_persistence_attestation_record) * 2U, estimated) ||
        !AddMemory(carrier_count,
                   sizeof(laplace_trajectory_carrier) * 3U, estimated) ||
        !AddMemory(entity_count,
                   laplace_persistence_frame_bytes(
                       LAPLACE_PERSISTENCE_RECORD_ENTITY), estimated) ||
        !AddMemory(physicality_count,
                   laplace_persistence_frame_bytes(
                       LAPLACE_PERSISTENCE_RECORD_PHYSICALITY), estimated) ||
        !AddMemory(carrier_count,
                   laplace_persistence_frame_bytes(
                       LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT), estimated) ||
        !AddMemory(occurrence_count,
                   laplace_persistence_frame_bytes(
                       LAPLACE_PERSISTENCE_RECORD_ATTESTATION), estimated)) {
        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
    }
    return LAPLACE_COMPOSITION_OK;
}

laplace_digest256 InputFingerprint(
    const laplace_composition_working_set_input& input,
    const laplace_digest256& context_fingerprint) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, InputDomain);
    blake3_hasher_update(
        &hasher, context_fingerprint.bytes, sizeof(context_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, input.source_fingerprint->bytes,
        sizeof(input.source_fingerprint->bytes));
    blake3_hasher_update(
        &hasher, input.calculation_recipe_fingerprint->bytes,
        sizeof(input.calculation_recipe_fingerprint->bytes));
    HashU64(hasher, input.known_entity_count);
    for (std::uint64_t index = 0U; index < input.known_entity_count; ++index) {
        const auto& value = input.known_entities[index];
        blake3_hasher_update(&hasher, value.entity_id.bytes, sizeof(value.entity_id.bytes));
        blake3_hasher_update(
            &hasher, value.identity_witness.bytes,
            sizeof(value.identity_witness.bytes));
        blake3_hasher_update(
            &hasher, value.physicality_id.bytes,
            sizeof(value.physicality_id.bytes));
        for (const double component : value.centroid.component) {
            HashDouble(hasher, component);
        }
        HashU32(hasher, value.atom);
        HashU8(hasher, value.tier_floor);
        HashU8(hasher, value.has_atom);
    }
    HashU64(hasher, input.operand_count);
    for (std::uint64_t index = 0U; index < input.operand_count; ++index) {
        const auto& value = input.operands[index];
        HashU64(hasher, value.reference_index);
        HashU64(hasher, value.multiplicity);
        HashU64(hasher, value.relationship_metadata);
        HashU32(hasher, value.reference_kind);
        HashU32(hasher, value.flags);
    }
    HashU64(hasher, input.request_count);
    for (std::uint64_t index = 0U; index < input.request_count; ++index) {
        const auto& value = input.requests[index];
        HashU64(hasher, value.first_operand);
        HashU64(hasher, value.operand_count);
        HashU64(hasher, value.source_ordinal);
        HashU32(hasher, value.recipe_version);
        HashU32(hasher, value.flags);
        blake3_hasher_update(
            &hasher, value.recipe_fingerprint.bytes,
            sizeof(value.recipe_fingerprint.bytes));
        blake3_hasher_update(
            &hasher, value.geometry_epoch.bytes,
            sizeof(value.geometry_epoch.bytes));
        blake3_hasher_update(
            &hasher, value.occurrence_context_fingerprint.bytes,
            sizeof(value.occurrence_context_fingerprint.bytes));
    }
    return Finish(hasher);
}

laplace_composition_status ValidateInput(
    const laplace_composition_working_set_input& input,
    laplace_digest256& context_fingerprint,
    ResourceCounts& resources,
    std::uint64_t& estimated_bytes) {
    if (input.context == nullptr || input.source_fingerprint == nullptr ||
        input.calculation_recipe_fingerprint == nullptr ||
        input.known_entities == nullptr || input.known_entity_count == 0U ||
        input.operands == nullptr || input.operand_count == 0U ||
        input.requests == nullptr || input.request_count == 0U ||
        input.reserved != 0U ||
        input.known_entity_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        input.operand_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        input.request_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    if (laplace_framework_context_fingerprint(
            input.context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        return LAPLACE_COMPOSITION_CONTEXT_INVALID;
    }
    for (std::uint64_t index = 0U; index < input.known_entity_count; ++index) {
        if (!KnownValid(input.known_entities[index])) {
            return LAPLACE_COMPOSITION_IDENTITY_INVALID;
        }
    }
    for (std::uint64_t index = 0U; index < input.operand_count; ++index) {
        const auto& operand = input.operands[index];
        if (operand.multiplicity == 0U || operand.flags != 0U ||
            (operand.relationship_metadata & ~MetadataMask) != 0U ||
            (operand.relationship_metadata & StructuralMetadataMask) != 0U ||
            (operand.reference_kind != LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY &&
             operand.reference_kind != LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT)) {
            return LAPLACE_COMPOSITION_METADATA_INVALID;
        }
    }
    for (std::uint64_t index = 0U; index < input.request_count; ++index) {
        const auto& request = input.requests[index];
        std::uint64_t end{};
        const bool emit_occurrence =
            (request.flags & LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE) != 0U;
        if (request.operand_count == 0U ||
            request.recipe_version == 0U ||
            (request.flags & ~LAPLACE_COMPOSITION_REQUEST_KNOWN_FLAGS) != 0U ||
            (emit_occurrence && request.source_ordinal == 0U) ||
            AddOverflow(request.first_operand, request.operand_count, end) ||
            end > input.operand_count) {
            return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
        }
        for (std::uint64_t offset = request.first_operand; offset < end; ++offset) {
            const auto& operand = input.operands[offset];
            if ((operand.reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY &&
                 operand.reference_index >= input.known_entity_count) ||
                (operand.reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT &&
                 operand.reference_index >= index)) {
                return LAPLACE_COMPOSITION_REFERENCE_INVALID;
            }
        }
    }
    std::uint64_t planning_bytes{};
    const auto planning_status = EstimatePlanningMemory(input, planning_bytes);
    if (planning_status != LAPLACE_COMPOSITION_OK) return planning_status;
    if (planning_bytes > input.context->resource_grant.memory_bytes) {
        return LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT;
    }
    const auto plan_status = PlanResourceCounts(input, resources);
    if (plan_status != LAPLACE_COMPOSITION_OK) return plan_status;
    const auto estimate_status = EstimateWorkingMemory(input, resources, estimated_bytes);
    if (estimate_status != LAPLACE_COMPOSITION_OK) return estimate_status;
    if (estimated_bytes > input.context->resource_grant.memory_bytes) {
        return LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT;
    }
    return LAPLACE_COMPOSITION_OK;
}

ResolvedValue KnownResolved(const laplace_composition_known_entity& known) {
    ResolvedValue result{};
    result.entity_id = known.entity_id;
    result.identity_witness = known.identity_witness;
    result.physicality_id = known.physicality_id;
    result.centroid = known.centroid;
    result.radius = std::sqrt(
        known.centroid.component[0] * known.centroid.component[0] +
        known.centroid.component[1] * known.centroid.component[1] +
        known.centroid.component[2] * known.centroid.component[2] +
        known.centroid.component[3] * known.centroid.component[3]);
    result.logical_count = 1U;
    result.atom = known.atom;
    result.tier_floor = known.tier_floor;
    result.has_atom = known.has_atom != 0U;
    return result;
}

laplace_composition_status ResolveOperand(
    const laplace_composition_working_set_input& input,
    const std::vector<ResolvedValue>& calculated,
    const laplace_composition_operand& operand,
    std::uint64_t request_index,
    ResolvedValue& resolved) {
    if (operand.reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY &&
        operand.reference_index < input.known_entity_count) {
        resolved = KnownResolved(input.known_entities[operand.reference_index]);
        return LAPLACE_COMPOSITION_OK;
    }
    if (operand.reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT &&
        operand.reference_index < request_index) {
        resolved = calculated[operand.reference_index];
        return LAPLACE_COMPOSITION_OK;
    }
    return LAPLACE_COMPOSITION_REFERENCE_INVALID;
}

laplace_composition_status CalculateRequest(
    const laplace_composition_working_set_input& input,
    const std::vector<ResolvedValue>& calculated,
    const std::uint64_t request_index,
    ResolvedValue& result,
    std::vector<laplace_trajectory_carrier>& carriers,
    laplace_persistence_physicality_record& physicality,
    bool& has_physicality) {
    result = ResolvedValue{};
    carriers.clear();
    physicality = laplace_persistence_physicality_record{};
    has_physicality = false;
    const auto& request = input.requests[request_index];
    std::vector<laplace_id_run> identity_runs;
    std::vector<std::pair<ResolvedValue, laplace_composition_operand>> resolved;
    identity_runs.reserve(static_cast<std::size_t>(request.operand_count));
    resolved.reserve(static_cast<std::size_t>(request.operand_count));
    laplace_geometry_accumulator geometry{};
    laplace_geometry_accumulator_init(&geometry);
    std::uint8_t maximum_child_tier = 0U;
    std::uint64_t logical_count = 0U;
    for (std::uint64_t operand_offset = 0U;
         operand_offset < request.operand_count; ++operand_offset) {
        const auto& operand = input.operands[
            request.first_operand + operand_offset];
        ResolvedValue child{};
        const auto resolve_status = ResolveOperand(
            input, calculated, operand, request_index, child);
        if (resolve_status != LAPLACE_COMPOSITION_OK) return resolve_status;
        identity_runs.push_back(laplace_id_run{
            child.entity_id, operand.multiplicity});
        resolved.emplace_back(child, operand);
        maximum_child_tier = std::max(maximum_child_tier, child.tier_floor);
        if (AddOverflow(logical_count, operand.multiplicity, logical_count)) {
            return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
        }
        if (laplace_geometry_accumulator_add(
                &geometry, &child.centroid, operand.multiplicity) !=
                LAPLACE_GEOMETRY_OK) {
            return LAPLACE_COMPOSITION_GEOMETRY_INVALID;
        }
    }

    if (logical_count == 1U) {
        if (resolved.size() != 1U ||
            resolved[0].second.relationship_metadata != 0U) {
            return LAPLACE_COMPOSITION_METADATA_INVALID;
        }
        result = resolved[0].first;
        return LAPLACE_COMPOSITION_OK;
    }
    if (maximum_child_tier >= LAPLACE_COMPOSITION_TIER_MAXIMUM) {
        return LAPLACE_COMPOSITION_TIER_OVERFLOW;
    }
    result.tier_floor = static_cast<std::uint8_t>(maximum_child_tier + 1U);
    if (laplace_identity_composite_runs_witness(
            identity_runs.data(), identity_runs.size(), nullptr,
            &result.logical_count, &result.entity_id,
            &result.identity_witness) != LAPLACE_IDENTITY_OK ||
        result.logical_count != logical_count) {
        return LAPLACE_COMPOSITION_IDENTITY_INVALID;
    }
    laplace_geometry_summary geometry_summary{};
    if (laplace_geometry_accumulator_finish(
            &geometry, &geometry_summary) != LAPLACE_GEOMETRY_OK ||
        geometry_summary.logical_count != logical_count) {
        return LAPLACE_COMPOSITION_GEOMETRY_INVALID;
    }
    result.centroid = geometry_summary.centroid;
    result.radius = geometry_summary.radius;
    std::uint64_t logical_ordinal = 1U;
    for (const auto& [child, operand] : resolved) {
        std::uint64_t remaining = operand.multiplicity;
        const std::uint64_t structural_metadata =
            (static_cast<std::uint64_t>(child.tier_floor)
             << LAPLACE_TRAJECTORY_TIER_SHIFT) |
            (child.has_atom ? HasAtomMask : 0U) |
            (child.has_atom
                 ? static_cast<std::uint64_t>(child.atom)
                       << LAPLACE_TRAJECTORY_ATOM_SHIFT
                 : 0U);
        const std::uint64_t metadata =
            operand.relationship_metadata | structural_metadata;
        while (remaining != 0U) {
            const auto run = static_cast<std::uint16_t>(
                std::min<std::uint64_t>(
                    remaining,
                    LAPLACE_COMPOSITION_MAXIMUM_RUN_PER_CARRIER));
            laplace_trajectory_carrier carrier{};
            if (laplace_trajectory_composition_encode(
                    &child.entity_id, logical_ordinal, run,
                    metadata, &carrier) != LAPLACE_TRAJECTORY_OK) {
                return LAPLACE_COMPOSITION_TRAJECTORY_INVALID;
            }
            carriers.push_back(carrier);
            logical_ordinal += run;
            remaining -= run;
        }
    }
    result.trajectory_vertex_count =
        static_cast<std::uint64_t>(carriers.size());
    physicality.entity_id = result.entity_id;
    physicality.physicality_type =
        LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    physicality.vertex_class =
        LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    physicality.recipe_version = request.recipe_version;
    physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    physicality.flags = LAPLACE_PERSISTENCE_PHYSICALITY_FLAGS_NONE;
    physicality.recipe_fingerprint = request.recipe_fingerprint;
    physicality.geometry_epoch = request.geometry_epoch;
    physicality.centroid = result.centroid;
    physicality.radius = result.radius;
    physicality.logical_count = result.logical_count;
    physicality.vertex_count = result.trajectory_vertex_count;
    if (laplace_persistence_trajectory_fingerprint(
            carriers.data(), carriers.size(),
            &physicality.trajectory_fingerprint) !=
            LAPLACE_PERSISTENCE_OK ||
        laplace_persistence_physicality_identify(
            &physicality, &physicality.physicality_id) !=
            LAPLACE_PERSISTENCE_OK) {
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }
    result.physicality_id = physicality.physicality_id;
    has_physicality = true;
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_status MakeOccurrence(
    const laplace_composition_working_set_input& input,
    const laplace_composition_request& request,
    const ResolvedValue& result,
    laplace_persistence_attestation_record& occurrence) {
    occurrence = laplace_persistence_attestation_record{};
    occurrence.entity_id = result.entity_id;
    occurrence.physicality_id = result.physicality_id;
    occurrence.source_fingerprint = *input.source_fingerprint;
    occurrence.context_fingerprint = request.occurrence_context_fingerprint;
    occurrence.source_ordinal = request.source_ordinal;
    occurrence.flags = LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY;
    occurrence.attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
    if (laplace_persistence_attestation_identify(
            &occurrence, &occurrence.attestation_id) != LAPLACE_PERSISTENCE_OK) {
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_status AddEntity(
    std::unordered_map<IdKey, DigestKey, ByteKeyHash<16>>& witnesses,
    std::unordered_map<DigestKey, std::size_t, ByteKeyHash<32>>& entity_index,
    std::vector<laplace_composition_entity_candidate>& entities,
    const ResolvedValue& value,
    std::uint64_t& duplicate_count) {
    const IdKey id_key = Key(value.entity_id);
    const DigestKey witness_key = Key(value.identity_witness);
    const auto prior_witness = witnesses.find(id_key);
    if (prior_witness != witnesses.end() && !(prior_witness->second == witness_key)) {
        return LAPLACE_COMPOSITION_IDENTITY_COLLISION;
    }
    witnesses.emplace(id_key, witness_key);
    const auto prior = entity_index.find(witness_key);
#if !defined(LAPLACE_TEST_COMPOSITION_DISABLE_WORKING_SET_DEDUP)
    if (prior != entity_index.end()) {
        auto& existing = entities[prior->second];
        existing.tier_floor = std::min(existing.tier_floor, value.tier_floor);
        ++duplicate_count;
        return LAPLACE_COMPOSITION_OK;
    }
#else
    (void)prior;
    (void)duplicate_count;
#endif
    laplace_composition_entity_candidate candidate{};
    candidate.entity.entity_id = value.entity_id;
    candidate.entity.identity_witness = value.identity_witness;
    candidate.tier_floor = value.tier_floor;
    entity_index.emplace(witness_key, entities.size());
    entities.push_back(candidate);
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_status AppendFrame(
    std::vector<std::uint8_t>& bytes,
    std::vector<std::uint64_t>& frame_offsets,
    std::uint16_t kind,
    const void* value,
    const laplace_digest256* physicality_id = nullptr,
    std::uint64_t vertex_index = 0U) {
    const std::size_t frame_bytes = laplace_persistence_frame_bytes(kind);
    const std::size_t offset = bytes.size();
    if (frame_bytes == 0U ||
        offset > std::numeric_limits<std::size_t>::max() - frame_bytes) {
        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
    }
    bytes.resize(offset + frame_bytes);
    std::size_t written{};
    laplace_persistence_status status = LAPLACE_PERSISTENCE_INVALID_ARGUMENT;
    if (kind == LAPLACE_PERSISTENCE_RECORD_ENTITY) {
        const auto* entity = static_cast<const laplace_persistence_entity_record*>(value);
        status = laplace_persistence_frame_encode_entity(
            &entity->entity_id, &entity->identity_witness,
            bytes.data() + offset, frame_bytes, &written);
    } else if (kind == LAPLACE_PERSISTENCE_RECORD_PHYSICALITY) {
        status = laplace_persistence_frame_encode_physicality(
            static_cast<const laplace_persistence_physicality_record*>(value),
            bytes.data() + offset, frame_bytes, &written);
    } else if (kind == LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT) {
        status = laplace_persistence_frame_encode_trajectory_segment(
            physicality_id, vertex_index,
            static_cast<const laplace_trajectory_carrier*>(value),
            bytes.data() + offset, frame_bytes, &written);
    } else if (kind == LAPLACE_PERSISTENCE_RECORD_ATTESTATION) {
        status = laplace_persistence_frame_encode_attestation(
            static_cast<const laplace_persistence_attestation_record*>(value),
            bytes.data() + offset, frame_bytes, &written);
    }
    if (status != LAPLACE_PERSISTENCE_OK || written != frame_bytes) {
        bytes.resize(offset);
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }
    frame_offsets.push_back(static_cast<std::uint64_t>(offset));
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_status BuildStream(
    laplace_composition_working_set& state,
    std::uint64_t preferred_batch_bytes) {
    std::vector<std::uint64_t> offsets;
    std::vector<std::size_t> group_ends;
    state.stream.clear();
    state.slices.clear();
    state.batches.clear();
    for (std::size_t index = 0U; index < state.entities.size(); ++index) {
#if !defined(LAPLACE_TEST_COMPOSITION_IGNORE_PRESENCE)
        if (state.entity_dispositions[index] ==
            LAPLACE_COMPOSITION_EXACT_PRESENT) {
            continue;
        }
#endif
        const auto& entity = state.entities[index];
        const auto status = AppendFrame(
            state.stream, offsets, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            &entity.entity);
        if (status != LAPLACE_COMPOSITION_OK) {
            return status;
        }
        group_ends.push_back(offsets.size());
    }
    for (std::size_t bundle_index = 0U;
         bundle_index < state.physicalities.size(); ++bundle_index) {
#if !defined(LAPLACE_TEST_COMPOSITION_IGNORE_PRESENCE)
        if (state.physicality_dispositions[bundle_index] ==
            LAPLACE_COMPOSITION_EXACT_PRESENT) {
            continue;
        }
#endif
        const auto& bundle = state.physicalities[bundle_index];
        auto status = AppendFrame(
            state.stream, offsets, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
            &bundle.physicality);
        if (status != LAPLACE_COMPOSITION_OK) {
            return status;
        }
        for (std::size_t index = 0U; index < bundle.carriers.size(); ++index) {
            status = AppendFrame(
                state.stream, offsets,
                LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT,
                &bundle.carriers[index], &bundle.physicality.physicality_id,
                static_cast<std::uint64_t>(index));
            if (status != LAPLACE_COMPOSITION_OK) {
                return status;
            }
        }
        group_ends.push_back(offsets.size());
    }
    for (const auto& occurrence : state.occurrences) {
        const auto status = AppendFrame(
            state.stream, offsets,
            LAPLACE_PERSISTENCE_RECORD_ATTESTATION, &occurrence);
        if (status != LAPLACE_COMPOSITION_OK) {
            return status;
        }
        group_ends.push_back(offsets.size());
    }
    if (offsets.empty()) {
#if defined(LAPLACE_TEST_COMPOSITION_REJECT_EMPTY_EFFECT)
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
#else
        if (laplace_framework_canonical_empty_stream_fingerprint(
                LAPLACE_COMPOSITION_STREAM_RECORD_TYPE,
                &state.summary.stream_fingerprint) != LAPLACE_FRAMEWORK_OK) {
            return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
        }
        state.summary.batch_count = 0U;
        state.summary.stream_record_count = 0U;
        state.summary.stream_byte_count = 0U;
        state.effect_disposition = LAPLACE_FRAMEWORK_EFFECT_NONE;
        return LAPLACE_COMPOSITION_OK;
#endif
    }
    offsets.push_back(static_cast<std::uint64_t>(state.stream.size()));
    const std::uint64_t target = preferred_batch_bytes == 0U
        ? DefaultBatchBytes
        : preferred_batch_bytes;
    std::size_t first = 0U;
    std::size_t first_group = 0U;
    std::uint64_t ordinal = 0U;
    while (first_group < group_ends.size()) {
        std::size_t end_group = first_group;
        std::size_t end = group_ends[end_group];
        while (end_group + 1U < group_ends.size() &&
               offsets[group_ends[end_group + 1U]] - offsets[first] <= target) {
            ++end_group;
            end = group_ends[end_group];
        }
        const std::uint64_t record_count = static_cast<std::uint64_t>(end - first);
        state.slices.push_back(BatchSlice{
            offsets[first], offsets[end] - offsets[first], record_count, ordinal});
        ordinal += record_count;
        first = end;
        first_group = end_group + 1U;
    }
    state.batches.reserve(state.slices.size());
    for (const BatchSlice& slice : state.slices) {
        state.batches.push_back(laplace_framework_canonical_batch{
            state.stream.data() + slice.offset,
            slice.bytes,
            slice.records,
            slice.first_ordinal,
            LAPLACE_COMPOSITION_STREAM_RECORD_TYPE,
            LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS});
    }
    laplace_persistence_summary persisted{};
    if (laplace_persistence_validate_stream(
            state.batches.data(), state.batches.size(), &persisted) !=
            LAPLACE_PERSISTENCE_OK ||
        persisted.entity_count != state.summary.novel_entity_count ||
        persisted.physicality_count != state.summary.novel_physicality_count ||
        persisted.trajectory_segment_count !=
            state.summary.novel_trajectory_vertex_count ||
        persisted.attestation_count != state.occurrences.size() ||
        persisted.consensus_count != 0u) {
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }
    std::uint32_t record_type{};
    std::uint64_t records{};
    std::uint64_t bytes{};
    if (laplace_framework_canonical_stream_fingerprint(
            state.batches.data(), state.batches.size(),
            &state.summary.stream_fingerprint, &record_type, &records, &bytes) !=
            LAPLACE_FRAMEWORK_OK ||
        record_type != LAPLACE_COMPOSITION_STREAM_RECORD_TYPE) {
        return LAPLACE_COMPOSITION_PERSISTENCE_INVALID;
    }
    state.summary.batch_count = static_cast<std::uint64_t>(state.batches.size());
    state.summary.stream_record_count = records;
    state.summary.stream_byte_count = bytes;
    state.effect_disposition = LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT;
    return LAPLACE_COMPOSITION_OK;
}

void RefreshPublicationFingerprints(laplace_composition_working_set& state) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, ReceiptDomain);
    blake3_hasher_update(
        &hasher, state.summary.context_fingerprint.bytes,
        sizeof(state.summary.context_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, state.summary.input_fingerprint.bytes,
        sizeof(state.summary.input_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, state.summary.stream_fingerprint.bytes,
        sizeof(state.summary.stream_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, state.summary.presence_receipt_id.bytes,
        sizeof(state.summary.presence_receipt_id.bytes));
    HashU64(hasher, state.summary.novel_entity_count);
    HashU64(hasher, state.summary.novel_physicality_count);
    HashU64(hasher, state.summary.novel_trajectory_vertex_count);
    HashU64(hasher, state.summary.occurrence_count);
    HashU32(hasher, state.effect_disposition);
    state.summary.receipt_id = Finish(hasher);
    blake3_hasher_init(&hasher);
    HashString(hasher, ProducerDomain);
    blake3_hasher_update(
        &hasher, state.summary.receipt_id.bytes,
        sizeof(state.summary.receipt_id.bytes));
    state.producer_fingerprint = Finish(hasher);
}

laplace_digest256 CursorFingerprint(
    const laplace_composition_working_set& state,
    std::uint64_t next_batch) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, CursorDomain);
    blake3_hasher_update(
        &hasher, state.producer_fingerprint.bytes,
        sizeof(state.producer_fingerprint.bytes));
    HashU64(hasher, next_batch);
    return Finish(hasher);
}

laplace_framework_status ProducerPrepare(
    void* opaque,
    const laplace_framework_context* context,
    const laplace_digest256* source,
    const laplace_digest256* recipe,
    laplace_framework_producer_plan* plan) {
    if (opaque == nullptr || context == nullptr || source == nullptr ||
        recipe == nullptr || plan == nullptr) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    *plan = laplace_framework_producer_plan{};
    const auto& state = *static_cast<const laplace_composition_working_set*>(opaque);
    laplace_digest256 context_fingerprint{};
    if (laplace_framework_context_fingerprint(context, &context_fingerprint) !=
            LAPLACE_FRAMEWORK_OK ||
        !DigestEqual(context_fingerprint, state.summary.context_fingerprint) ||
        !DigestEqual(*source, state.summary.source_fingerprint) ||
        !DigestEqual(*recipe, state.summary.calculation_recipe_fingerprint)) {
        return LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED;
    }
    plan->producer_fingerprint = state.producer_fingerprint;
    plan->initial_cursor_fingerprint = CursorFingerprint(state, 0U);
    plan->batch_count = state.summary.batch_count;
    plan->total_records = state.summary.stream_record_count;
    plan->total_bytes = state.summary.stream_byte_count;
    plan->record_type = LAPLACE_COMPOSITION_STREAM_RECORD_TYPE;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status ProducerNext(
    void* opaque,
    std::uint64_t batch_index,
    laplace_framework_canonical_batch* batch,
    laplace_digest256* cursor_fingerprint) {
    if (opaque == nullptr || batch == nullptr || cursor_fingerprint == nullptr) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    const auto& state = *static_cast<const laplace_composition_working_set*>(opaque);
    if (batch_index >= state.batches.size()) {
        return LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
    }
    *batch = state.batches[static_cast<std::size_t>(batch_index)];
    *cursor_fingerprint = CursorFingerprint(state, batch_index + 1U);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status ProducerFinish(
    void* opaque,
    laplace_digest256* completion_fingerprint) {
    if (opaque == nullptr || completion_fingerprint == nullptr) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    const auto& state = *static_cast<const laplace_composition_working_set*>(opaque);
    *completion_fingerprint = state.summary.receipt_id;
    return LAPLACE_FRAMEWORK_OK;
}

void ProducerAbort(void*) {}

}  // namespace

extern "C" laplace_composition_status laplace_composition_working_set_create(
    const laplace_composition_working_set_input* input,
    laplace_composition_working_set** working_set) {
    if (input == nullptr || working_set == nullptr) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    *working_set = nullptr;
    laplace_digest256 context_fingerprint{};
    ResourceCounts resources{};
    std::uint64_t estimated_bytes{};
    const auto validation = ValidateInput(
        *input, context_fingerprint, resources, estimated_bytes);
    if (validation != LAPLACE_COMPOSITION_OK) {
        return validation;
    }
    auto* state = new (std::nothrow) laplace_composition_working_set{};
    if (state == nullptr) {
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
    try {
        state->summary.context_fingerprint = context_fingerprint;
        state->summary.source_fingerprint = *input->source_fingerprint;
        state->summary.calculation_recipe_fingerprint =
            *input->calculation_recipe_fingerprint;
        state->summary.input_fingerprint = InputFingerprint(*input, context_fingerprint);
        state->summary.known_entity_count = input->known_entity_count;
        state->summary.request_count = input->request_count;
        state->summary.operand_count = input->operand_count;
        state->summary.estimated_peak_working_bytes = estimated_bytes;
        state->results.resize(static_cast<std::size_t>(input->request_count));
        state->entities.reserve(
            static_cast<std::size_t>(resources.unique_entity_count));
        state->physicalities.reserve(
            static_cast<std::size_t>(resources.unique_physicality_count));
        state->occurrences.reserve(
            static_cast<std::size_t>(resources.unique_occurrence_count));

        std::unordered_map<IdKey, DigestKey, ByteKeyHash<16>> witnesses;
        std::unordered_map<DigestKey, std::size_t, ByteKeyHash<32>> entity_index;
        std::unordered_map<DigestKey, std::size_t, ByteKeyHash<32>> physicality_index;
        std::unordered_map<DigestKey, std::size_t, ByteKeyHash<32>> occurrence_index;
        witnesses.reserve(static_cast<std::size_t>(resources.unique_entity_count));
        entity_index.reserve(static_cast<std::size_t>(resources.unique_entity_count));
        physicality_index.reserve(
            static_cast<std::size_t>(resources.unique_physicality_count));
        occurrence_index.reserve(
            static_cast<std::size_t>(resources.unique_occurrence_count));

        for (std::uint64_t index = 0U; index < input->known_entity_count; ++index) {
            const ResolvedValue known = KnownResolved(input->known_entities[index]);
            const auto status = AddEntity(
                witnesses, entity_index, state->entities, known,
                state->summary.deduplicated_entity_count);
            if (status != LAPLACE_COMPOSITION_OK) {
                delete state;
                return status;
            }
        }

        const auto calculation_status = CalculateRequestLevels(
            *input,
            [&](const std::uint64_t request_index,
                RequestCalculation& calculation) {
            const auto& request = input->requests[request_index];
            const auto& result = calculation.result;
            const auto& physicality = calculation.physicality;
            if (!calculation.has_physicality) {
                ++state->summary.collapsed_request_count;
            } else {
                const DigestKey physicality_key = Key(physicality.physicality_id);
                const auto prior = physicality_index.find(physicality_key);
#if !defined(LAPLACE_TEST_COMPOSITION_DISABLE_WORKING_SET_DEDUP)
                if (prior != physicality_index.end()) {
                    const auto& existing = state->physicalities[prior->second];
                    if (std::memcmp(
                            &existing.physicality, &physicality,
                            sizeof(physicality)) != 0 ||
                        existing.carriers.size() !=
                            calculation.carriers.size() ||
                        std::memcmp(
                            existing.carriers.data(),
                            calculation.carriers.data(),
                            calculation.carriers.size() *
                                sizeof(calculation.carriers[0])) != 0) {
                        return LAPLACE_COMPOSITION_IDENTITY_COLLISION;
                    }
                } else {
#else
                (void)prior;
#endif
                    physicality_index.emplace(
                        physicality_key, state->physicalities.size());
                    state->physicalities.push_back(PhysicalityBundle{
                        physicality, std::move(calculation.carriers)});
#if !defined(LAPLACE_TEST_COMPOSITION_DISABLE_WORKING_SET_DEDUP)
                }
#endif
            }
            const auto entity_status = AddEntity(
                witnesses, entity_index, state->entities, result,
                state->summary.deduplicated_entity_count);
            if (entity_status != LAPLACE_COMPOSITION_OK) {
                return entity_status;
            }
#if defined(LAPLACE_TEST_COMPOSITION_IMPLICIT_OCCURRENCE)
            const bool emit_occurrence = true;
#else
            const bool emit_occurrence =
                (request.flags & LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE) != 0U;
#endif
            if (emit_occurrence) {
                laplace_persistence_attestation_record occurrence{};
                const auto occurrence_status = MakeOccurrence(
                    *input, request, result, occurrence);
                if (occurrence_status != LAPLACE_COMPOSITION_OK) {
                    return occurrence_status;
                }
                if (occurrence_index.emplace(
                        Key(occurrence.attestation_id),
                        state->occurrences.size()).second) {
                    state->occurrences.push_back(occurrence);
                }
            }
            state->results[static_cast<std::size_t>(request_index)] =
                laplace_composition_result{
                result.entity_id,
                result.identity_witness,
                result.physicality_id,
                result.centroid,
                result.radius,
                result.logical_count,
                result.trajectory_vertex_count,
                result.tier_floor,
                static_cast<std::uint8_t>(!calculation.has_physicality),
                0U,
                0U};
            state->summary.maximum_tier_floor = std::max(
                state->summary.maximum_tier_floor,
                static_cast<std::uint32_t>(result.tier_floor));
            return LAPLACE_COMPOSITION_OK;
        });
        if (calculation_status != LAPLACE_COMPOSITION_OK) {
            delete state;
            return calculation_status;
        }

        std::sort(state->entities.begin(), state->entities.end(),
            [](const auto& left, const auto& right) {
                return laplace_identity_compare(
                    &left.entity.entity_id, &right.entity.entity_id) < 0;
            });
        std::sort(state->physicalities.begin(), state->physicalities.end(),
            [](const auto& left, const auto& right) {
                return std::memcmp(
                    left.physicality.physicality_id.bytes,
                    right.physicality.physicality_id.bytes,
                    sizeof(left.physicality.physicality_id.bytes)) < 0;
            });
        std::sort(state->occurrences.begin(), state->occurrences.end(),
            [](const auto& left, const auto& right) {
                return std::memcmp(
                    left.attestation_id.bytes, right.attestation_id.bytes,
                    sizeof(left.attestation_id.bytes)) < 0;
            });
        state->summary.unique_entity_count =
            static_cast<std::uint64_t>(state->entities.size());
        state->summary.unique_physicality_count =
            static_cast<std::uint64_t>(state->physicalities.size());
        state->summary.occurrence_count =
            static_cast<std::uint64_t>(state->occurrences.size());
        for (const auto& bundle : state->physicalities) {
            state->summary.trajectory_vertex_count +=
                static_cast<std::uint64_t>(bundle.carriers.size());
            state->summary.logical_occurrence_count +=
                bundle.physicality.logical_count;
        }
        state->preferred_batch_bytes = input->preferred_batch_bytes;
        state->summary.status = LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        delete state;
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
    *working_set = state;
    return LAPLACE_COMPOSITION_OK;
}

extern "C" laplace_composition_status laplace_composition_working_set_summary_get(
    const laplace_composition_working_set* working_set,
    laplace_composition_working_set_summary* summary) {
    if (working_set == nullptr || summary == nullptr) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    *summary = working_set->summary;
    return LAPLACE_COMPOSITION_OK;
}

extern "C" laplace_composition_status
laplace_composition_working_set_effect_disposition_get(
    const laplace_composition_working_set* working_set,
    std::uint32_t* effect_disposition) {
    if (working_set == nullptr || effect_disposition == nullptr) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    if (working_set->summary.presence_applied == 0U) {
        *effect_disposition = LAPLACE_FRAMEWORK_EFFECT_NONE;
        return LAPLACE_COMPOSITION_PRESENCE_REQUIRED;
    }
    *effect_disposition = working_set->effect_disposition;
    return LAPLACE_COMPOSITION_OK;
}

extern "C" const laplace_composition_result*
laplace_composition_working_set_results(
    const laplace_composition_working_set* working_set,
    size_t* result_count) {
    if (working_set == nullptr || result_count == nullptr) {
        return nullptr;
    }
    *result_count = working_set->results.size();
    return working_set->results.data();
}

extern "C" const laplace_composition_entity_candidate*
laplace_composition_working_set_entity_candidates(
    const laplace_composition_working_set* working_set,
    size_t* candidate_count) {
    if (working_set == nullptr || candidate_count == nullptr) {
        return nullptr;
    }
    *candidate_count = working_set->entities.size();
    return working_set->entities.data();
}

extern "C" const uint8_t*
laplace_composition_working_set_entity_dispositions(
    const laplace_composition_working_set* working_set,
    size_t* disposition_count) {
    if (working_set == nullptr || disposition_count == nullptr) {
        return nullptr;
    }
    if (working_set->summary.presence_applied == 0U) {
        *disposition_count = 0U;
        return nullptr;
    }
    *disposition_count = working_set->entity_dispositions.size();
    return working_set->entity_dispositions.data();
}

extern "C" const uint8_t*
laplace_composition_working_set_physicality_dispositions(
    const laplace_composition_working_set* working_set,
    size_t* disposition_count) {
    if (working_set == nullptr || disposition_count == nullptr) {
        return nullptr;
    }
    if (working_set->summary.presence_applied == 0U) {
        *disposition_count = 0U;
        return nullptr;
    }
    *disposition_count = working_set->physicality_dispositions.size();
    return working_set->physicality_dispositions.data();
}

extern "C" laplace_composition_status
laplace_composition_working_set_physicality_candidate_get(
    const laplace_composition_working_set* working_set,
    size_t candidate_index,
    laplace_persistence_physicality_record* candidate) {
    if (working_set == nullptr || candidate == nullptr ||
        candidate_index >= working_set->physicalities.size()) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    *candidate = working_set->physicalities[candidate_index].physicality;
    return LAPLACE_COMPOSITION_OK;
}

static laplace_composition_status ApplyPresence(
    laplace_composition_working_set* working_set,
    const uint8_t* entity_dispositions,
    size_t entity_disposition_count,
    const uint8_t* physicality_dispositions,
    size_t physicality_disposition_count,
    const laplace_digest256* presence_receipt_id) {
    if (working_set == nullptr || entity_dispositions == nullptr ||
        presence_receipt_id == nullptr ||
        (physicality_disposition_count != 0U &&
         physicality_dispositions == nullptr) ||
        entity_disposition_count != working_set->entities.size() ||
        physicality_disposition_count != working_set->physicalities.size()) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    if (working_set->summary.presence_applied != 0U) {
        return LAPLACE_COMPOSITION_PRESENCE_ALREADY_APPLIED;
    }
    std::uint64_t novel_entities{};
    std::uint64_t novel_physicalities{};
    std::uint64_t novel_trajectory_vertices{};
    for (std::size_t index = 0U; index < entity_disposition_count; ++index) {
        if (entity_dispositions[index] > LAPLACE_COMPOSITION_EXACT_PRESENT) {
            return LAPLACE_COMPOSITION_PRESENCE_INVALID;
        }
        if (entity_dispositions[index] == LAPLACE_COMPOSITION_NOVEL) {
            ++novel_entities;
        }
    }
    for (std::size_t index = 0U; index < physicality_disposition_count; ++index) {
        if (physicality_dispositions[index] >
            LAPLACE_COMPOSITION_EXACT_PRESENT) {
            return LAPLACE_COMPOSITION_PRESENCE_INVALID;
        }
        if (physicality_dispositions[index] == LAPLACE_COMPOSITION_NOVEL) {
            ++novel_physicalities;
            novel_trajectory_vertices += static_cast<std::uint64_t>(
                working_set->physicalities[index].carriers.size());
        }
    }
    try {
        std::vector<std::uint8_t> new_entity_dispositions(
            entity_dispositions, entity_dispositions + entity_disposition_count);
        std::vector<std::uint8_t> new_physicality_dispositions;
        if (physicality_disposition_count != 0U) {
            new_physicality_dispositions.assign(
                physicality_dispositions,
                physicality_dispositions + physicality_disposition_count);
        }
        const auto prior_summary = working_set->summary;
        working_set->entity_dispositions =
            std::move(new_entity_dispositions);
        working_set->physicality_dispositions =
            std::move(new_physicality_dispositions);
        working_set->summary.novel_entity_count = novel_entities;
        working_set->summary.novel_physicality_count = novel_physicalities;
        working_set->summary.novel_trajectory_vertex_count =
            novel_trajectory_vertices;
        working_set->summary.presence_receipt_id = *presence_receipt_id;
        const auto status = BuildStream(
            *working_set, working_set->preferred_batch_bytes);
        if (status != LAPLACE_COMPOSITION_OK) {
            working_set->stream.clear();
            working_set->slices.clear();
            working_set->batches.clear();
            working_set->entity_dispositions.clear();
            working_set->physicality_dispositions.clear();
            working_set->summary = prior_summary;
            return status;
        }
        working_set->summary.presence_applied = 1U;
        RefreshPublicationFingerprints(*working_set);
        return LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        working_set->stream.clear();
        working_set->slices.clear();
        working_set->batches.clear();
        working_set->entity_dispositions.clear();
        working_set->physicality_dispositions.clear();
        working_set->summary.stream_fingerprint = laplace_digest256{};
        working_set->summary.receipt_id = laplace_digest256{};
        working_set->summary.presence_receipt_id = laplace_digest256{};
        working_set->summary.novel_entity_count = 0U;
        working_set->summary.novel_physicality_count = 0U;
        working_set->summary.novel_trajectory_vertex_count = 0U;
        working_set->summary.batch_count = 0U;
        working_set->summary.stream_record_count = 0U;
        working_set->summary.stream_byte_count = 0U;
        working_set->effect_disposition = LAPLACE_FRAMEWORK_EFFECT_NONE;
        working_set->summary.presence_applied = 0U;
        working_set->producer_fingerprint = laplace_digest256{};
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
}

extern "C" laplace_composition_status
laplace_composition_working_set_resolve_presence(
    laplace_composition_working_set* working_set,
    const laplace_composition_presence_provider_v1* provider,
    laplace_composition_presence_receipt* receipt) {
    if (receipt != nullptr) {
        *receipt = laplace_composition_presence_receipt{};
    }
    if (working_set == nullptr || provider == nullptr || receipt == nullptr ||
        provider->resolve == nullptr ||
        provider->abi_major != LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI ||
        provider->abi_minor > LAPLACE_COMPOSITION_ABI_MINOR ||
        provider->flags != 0U || provider->reserved != 0U) {
        if (receipt != nullptr) {
            receipt->status = LAPLACE_COMPOSITION_PRESENCE_INVALID;
        }
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    if (working_set->summary.presence_applied != 0U) {
        receipt->status = LAPLACE_COMPOSITION_PRESENCE_ALREADY_APPLIED;
        return LAPLACE_COMPOSITION_PRESENCE_ALREADY_APPLIED;
    }
    try {
        std::vector<laplace_persistence_physicality_record> physicalities;
        physicalities.reserve(working_set->physicalities.size());
        for (const auto& bundle : working_set->physicalities) {
            physicalities.push_back(bundle.physicality);
        }
        std::vector<std::uint8_t> entity_dispositions(
            working_set->entities.size(), UINT8_MAX);
        std::vector<std::uint8_t> physicality_dispositions(
            physicalities.size(), UINT8_MAX);
        std::array<bool, LAPLACE_COMPOSITION_TIER_MAXIMUM + 1U> tiers{};
        std::uint64_t participating_tiers = 0U;
        for (const auto& candidate : working_set->entities) {
            if (!tiers[candidate.tier_floor]) {
                tiers[candidate.tier_floor] = true;
                ++participating_tiers;
            }
        }

        blake3_hasher hasher{};
        blake3_hasher_init(&hasher);
        HashString(hasher, PresenceCandidateDomain);
        blake3_hasher_update(
            &hasher, working_set->summary.input_fingerprint.bytes,
            sizeof(working_set->summary.input_fingerprint.bytes));
        HashU64(hasher, working_set->entities.size());
        for (const auto& candidate : working_set->entities) {
            blake3_hasher_update(
                &hasher, candidate.entity.entity_id.bytes,
                sizeof(candidate.entity.entity_id.bytes));
            blake3_hasher_update(
                &hasher, candidate.entity.identity_witness.bytes,
                sizeof(candidate.entity.identity_witness.bytes));
            HashU8(hasher, candidate.tier_floor);
        }
        HashU64(hasher, physicalities.size());
        for (const auto& physicality : physicalities) {
            HashPhysicality(hasher, physicality);
        }
        receipt->candidate_fingerprint = Finish(hasher);
        receipt->working_set_input_fingerprint =
            working_set->summary.input_fingerprint;
        receipt->entity_candidate_count = working_set->entities.size();
        receipt->physicality_candidate_count = physicalities.size();
        receipt->participating_tier_count = participating_tiers;

        laplace_composition_presence_provider_result provider_result{};
        const auto provider_status = provider->resolve(
            provider->state,
            working_set->entities.data(), working_set->entities.size(),
            physicalities.data(), physicalities.size(),
            entity_dispositions.data(), physicality_dispositions.data(),
            &provider_result);
        receipt->provider_fingerprint = provider_result.provider_fingerprint;
        receipt->provider_receipt_id = provider_result.provider_receipt_id;
        receipt->entity_round_count = provider_result.entity_round_count;
        receipt->physicality_round_count =
            provider_result.physicality_round_count;
        if (provider_status != LAPLACE_COMPOSITION_OK) {
            receipt->status = provider_status;
            return provider_status;
        }
        if (provider_result.returned_entity_count !=
                working_set->entities.size() ||
            provider_result.returned_physicality_count != physicalities.size() ||
            provider_result.entity_round_count > participating_tiers ||
            provider_result.physicality_round_count > 1U ||
            provider_result.flags != 0U || provider_result.reserved != 0U) {
            receipt->status = LAPLACE_COMPOSITION_PRESENCE_INVALID;
            return LAPLACE_COMPOSITION_PRESENCE_INVALID;
        }
        for (const std::uint8_t value : entity_dispositions) {
            if (value > LAPLACE_COMPOSITION_EXACT_PRESENT) {
                receipt->status = LAPLACE_COMPOSITION_PRESENCE_INVALID;
                return LAPLACE_COMPOSITION_PRESENCE_INVALID;
            }
        }
        for (const std::uint8_t value : physicality_dispositions) {
            if (value > LAPLACE_COMPOSITION_EXACT_PRESENT) {
                receipt->status = LAPLACE_COMPOSITION_PRESENCE_INVALID;
                return LAPLACE_COMPOSITION_PRESENCE_INVALID;
            }
        }

        blake3_hasher_init(&hasher);
        HashString(hasher, PresenceDispositionDomain);
        blake3_hasher_update(
            &hasher, receipt->candidate_fingerprint.bytes,
            sizeof(receipt->candidate_fingerprint.bytes));
        HashU64(hasher, entity_dispositions.size());
        blake3_hasher_update(
            &hasher, entity_dispositions.data(), entity_dispositions.size());
        HashU64(hasher, physicality_dispositions.size());
        if (!physicality_dispositions.empty()) {
            blake3_hasher_update(
                &hasher, physicality_dispositions.data(),
                physicality_dispositions.size());
        }
        receipt->disposition_fingerprint = Finish(hasher);

        blake3_hasher_init(&hasher);
        HashString(
            hasher, LAPLACE_COMPOSITION_PRESENCE_SEMANTIC_RECEIPT_DOMAIN);
        blake3_hasher_update(
            &hasher, receipt->working_set_input_fingerprint.bytes,
            sizeof(receipt->working_set_input_fingerprint.bytes));
        blake3_hasher_update(
            &hasher, receipt->candidate_fingerprint.bytes,
            sizeof(receipt->candidate_fingerprint.bytes));
        blake3_hasher_update(
            &hasher, receipt->disposition_fingerprint.bytes,
            sizeof(receipt->disposition_fingerprint.bytes));
        HashU64(hasher, receipt->entity_candidate_count);
        HashU64(hasher, receipt->physicality_candidate_count);
        HashU64(hasher, receipt->participating_tier_count);
        HashU32(hasher, LAPLACE_COMPOSITION_OK);
        receipt->semantic_receipt_id = Finish(hasher);

        blake3_hasher_init(&hasher);
        HashString(
            hasher, LAPLACE_COMPOSITION_PRESENCE_EXECUTION_RECEIPT_DOMAIN);
        blake3_hasher_update(
            &hasher, receipt->semantic_receipt_id.bytes,
            sizeof(receipt->semantic_receipt_id.bytes));
        blake3_hasher_update(
            &hasher, receipt->provider_fingerprint.bytes,
            sizeof(receipt->provider_fingerprint.bytes));
        blake3_hasher_update(
            &hasher, receipt->provider_receipt_id.bytes,
            sizeof(receipt->provider_receipt_id.bytes));
        HashU64(hasher, receipt->entity_round_count);
        HashU64(hasher, receipt->physicality_round_count);
        HashU32(hasher, LAPLACE_COMPOSITION_OK);
        receipt->execution_receipt_id = Finish(hasher);
        receipt->status = LAPLACE_COMPOSITION_OK;

        const auto apply_status = ApplyPresence(
            working_set,
            entity_dispositions.data(), entity_dispositions.size(),
            physicality_dispositions.data(), physicality_dispositions.size(),
            &receipt->semantic_receipt_id);
        if (apply_status != LAPLACE_COMPOSITION_OK) {
            receipt->status = apply_status;
            return apply_status;
        }
        return LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        receipt->status = LAPLACE_COMPOSITION_MEMORY_FAILURE;
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
}

extern "C" laplace_composition_status laplace_composition_working_set_producer(
    laplace_composition_working_set* working_set,
    laplace_framework_producer_v1* producer) {
    if (working_set == nullptr || producer == nullptr) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    *producer = laplace_framework_producer_v1{};
    if (working_set->summary.presence_applied == 0U) {
        return LAPLACE_COMPOSITION_PRESENCE_REQUIRED;
    }
    producer->state = working_set;
    producer->prepare = ProducerPrepare;
    producer->next = ProducerNext;
    producer->finish = ProducerFinish;
    producer->abort = ProducerAbort;
    producer->abi_major = LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR;
    producer->abi_minor = LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR;
    producer->flags = LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS;
    return LAPLACE_COMPOSITION_OK;
}

extern "C" laplace_composition_status
laplace_composition_working_set_compact_publication_input(
    laplace_composition_working_set* working_set) {
    if (working_set == nullptr) {
        return LAPLACE_COMPOSITION_INVALID_ARGUMENT;
    }
    if (working_set->summary.presence_applied == 0U) {
        return LAPLACE_COMPOSITION_PRESENCE_REQUIRED;
    }
    std::vector<laplace_composition_entity_candidate>{}.swap(
        working_set->entities);
    std::vector<PhysicalityBundle>{}.swap(working_set->physicalities);
    std::vector<laplace_persistence_attestation_record>{}.swap(
        working_set->occurrences);
    return LAPLACE_COMPOSITION_OK;
}

extern "C" void laplace_composition_working_set_destroy(
    laplace_composition_working_set** working_set) {
    if (working_set == nullptr || *working_set == nullptr) {
        return;
    }
    delete *working_set;
    *working_set = nullptr;
}
