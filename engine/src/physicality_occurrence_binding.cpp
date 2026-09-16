#include "laplace/physicality_occurrence_binding.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "blake3.h"

namespace {

void Hash32(blake3_hasher& hash, std::uint32_t value) {
    std::uint8_t bytes[4];
    for (unsigned index = 0U; index < 4U; ++index)
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    blake3_hasher_update(&hash, bytes, sizeof(bytes));
}

void Hash64(blake3_hasher& hash, std::uint64_t value) {
    std::uint8_t bytes[8];
    for (unsigned index = 0U; index < 8U; ++index)
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    blake3_hasher_update(&hash, bytes, sizeof(bytes));
}

bool Same(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

bool Zero(const laplace_digest256& value) {
    return std::all_of(value.bytes, value.bytes + sizeof(value.bytes),
        [](std::uint8_t byte) { return byte == 0U; });
}

laplace_physicality_occurrence_status MapStatus(laplace_physicality_entity_status status) {
    if (status == LAPLACE_PHYSICALITY_ENTITY_OK) return LAPLACE_PHYSICALITY_OCCURRENCE_OK;
    if (status == LAPLACE_PHYSICALITY_ENTITY_LIMIT) return LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT;
    if (status == LAPLACE_PHYSICALITY_ENTITY_MEMORY_FAILURE) return LAPLACE_PHYSICALITY_OCCURRENCE_MEMORY_FAILURE;
    return LAPLACE_PHYSICALITY_OCCURRENCE_PARENT_INVALID;
}

bool IntervalsMatch(const laplace_physicality_occurrence_parent& parent,
                    const laplace_physicality_occurrence_binding* bindings,
                    std::uint64_t count) {
    const auto& record = *parent.physicality;
    std::uint64_t covered = 0U, carrier_index = 0U, decoded = 0U, remaining = 0U;
    laplace_composition_occurrence occurrence{};
    for (std::uint64_t index = 0U; index < count; ++index) {
        const auto& binding = bindings[index];
        if (binding.version != LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION ||
            binding.reserved != 0U || Zero(binding.selected_physicality_id) ||
            !Same(binding.parent_physicality_id, record.physicality_id) ||
            covered == UINT64_MAX || binding.first_logical_ordinal != covered + 1U ||
            binding.logical_count == 0U || binding.logical_count > record.logical_count - covered)
            return false;
        auto unbound = binding.logical_count;
        while (unbound != 0U) {
            if (remaining == 0U) {
                if (carrier_index >= parent.carrier_count || decoded == UINT64_MAX ||
                    laplace_trajectory_composition_decode_one(&parent.carriers[carrier_index],
                        decoded + 1U, &occurrence) != LAPLACE_TRAJECTORY_OK ||
                    occurrence.run_length > record.logical_count - decoded)
                    return false;
                ++carrier_index;
                remaining = occurrence.run_length;
                decoded += occurrence.run_length;
            }
            if (!laplace_identity_equal(&binding.entity_id, &occurrence.entity_id) ||
                binding.metadata != occurrence.metadata)
                return false;
            const auto part = std::min(unbound, remaining);
            unbound -= part;
            remaining -= part;
            covered += part;
        }
    }
    return covered == record.logical_count && remaining == 0U &&
        carrier_index == parent.carrier_count;
}

laplace_digest256 ParentReceipt(const laplace_digest256& parent,
    const laplace_physicality_occurrence_binding* bindings, std::uint64_t count) {
    static constexpr char Domain[] = "laplace-physicality-occurrence-parent-v1";
    blake3_hasher hash;
    blake3_hasher_init(&hash);
    blake3_hasher_update(&hash, Domain, sizeof(Domain) - 1U);
    Hash32(hash, LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION);
    blake3_hasher_update(&hash, parent.bytes, sizeof(parent.bytes));
    Hash64(hash, count);
    for (std::uint64_t index = 0U; index < count; ++index) {
        const auto& binding = bindings[index];
        blake3_hasher_update(&hash, binding.parent_physicality_id.bytes, 32U);
        blake3_hasher_update(&hash, binding.entity_id.bytes, 16U);
#if defined(LAPLACE_TEST_OCCURRENCE_IGNORE_SELECTION)
        const laplace_digest256 omitted{};
        blake3_hasher_update(&hash, omitted.bytes, 32U);
#else
        blake3_hasher_update(&hash, binding.selected_physicality_id.bytes, 32U);
#endif
        Hash64(hash, binding.first_logical_ordinal);
        Hash64(hash, binding.logical_count);
        Hash64(hash, binding.metadata);
        Hash32(hash, binding.version);
        Hash32(hash, binding.reserved);
    }
    laplace_digest256 result{};
    blake3_hasher_finalize(&hash, result.bytes, sizeof(result.bytes));
    return result;
}

} // namespace

extern "C" laplace_physicality_occurrence_status
laplace_physicality_occurrence_bindings_memory_bound(
    std::uint64_t maximum_parent_carrier_count, std::uint64_t* bytes) {
    if (bytes == nullptr) return LAPLACE_PHYSICALITY_OCCURRENCE_INVALID_ARGUMENT;
    *bytes = 0U;
    return MapStatus(laplace_physicality_entity_validation_memory_bound(maximum_parent_carrier_count, bytes));
}

extern "C" laplace_physicality_occurrence_status
laplace_physicality_occurrence_bindings_validate_batch(
    const laplace_physicality_occurrence_parent* parents, std::uint64_t parent_count,
    const laplace_physicality_occurrence_binding* bindings, std::uint64_t binding_count,
    std::uint64_t maximum_parents, std::uint64_t maximum_carriers,
    std::uint64_t maximum_bindings, std::uint64_t maximum_logical_count,
    laplace_digest256* batch_receipt, laplace_digest256* parent_receipts,
    std::uint64_t parent_receipt_capacity) {
    if (batch_receipt != nullptr) *batch_receipt = {};
    const auto output_count = std::min(std::min(parent_count, parent_receipt_capacity), maximum_parents);
    if (parent_receipts != nullptr && output_count <= SIZE_MAX / sizeof(*parent_receipts))
        std::memset(parent_receipts, 0, static_cast<std::size_t>(output_count) * sizeof(*parent_receipts));
    if (batch_receipt == nullptr || (parents == nullptr && parent_count != 0U) ||
        (bindings == nullptr && binding_count != 0U) ||
        (parent_receipts == nullptr && parent_receipt_capacity != 0U))
        return LAPLACE_PHYSICALITY_OCCURRENCE_INVALID_ARGUMENT;
    if (parent_count > maximum_parents || binding_count > maximum_bindings ||
        parent_count > SIZE_MAX / sizeof(*parents) || binding_count > SIZE_MAX / sizeof(*bindings) ||
        parent_receipt_capacity > SIZE_MAX / sizeof(*parent_receipts) ||
        (parent_receipts != nullptr && parent_receipt_capacity < parent_count))
        return LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT;

    /* Admit aggregate expanded identity work before any shared validator hashes
     * even the first parent. The interval walk itself never expands RLE runs. */
    std::uint64_t total_carriers = 0U, total_logical = 0U;
    for (std::uint64_t index = 0U; index < parent_count; ++index) {
        const auto& parent = parents[index];
        if (parent.physicality == nullptr || (parent.carriers == nullptr && parent.carrier_count != 0U))
            return LAPLACE_PHYSICALITY_OCCURRENCE_INVALID_ARGUMENT;
        if (parent.carrier_count > maximum_carriers - total_carriers ||
            parent.physicality->logical_count > maximum_logical_count - total_logical)
            return LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT;
        total_carriers += parent.carrier_count;
        total_logical += parent.physicality->logical_count;
    }
    for (std::uint64_t index = 0U; index < parent_count; ++index) {
        const auto& parent = parents[index];
        if (parent.physicality->physicality_type != LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION ||
            parent.carrier_count == 0U ||
            (index != 0U && std::memcmp(parents[index - 1U].physicality->physicality_id.bytes,
                parent.physicality->physicality_id.bytes, 32U) >= 0))
            return LAPLACE_PHYSICALITY_OCCURRENCE_PARENT_INVALID;
    }
    std::uint64_t cursor = 0U;
    for (std::uint64_t index = 0U; index < parent_count; ++index) {
        const auto& parent = parents[index];
        laplace_physicality_entity_validation validation{};
        const auto status = MapStatus(laplace_physicality_entity_record_validate(
            parent.physicality, parent.carriers, parent.carrier_count,
            maximum_carriers, maximum_logical_count, &validation));
        if (status != LAPLACE_PHYSICALITY_OCCURRENCE_OK) return status;
        const auto first = cursor;
        while (cursor < binding_count && Same(bindings[cursor].parent_physicality_id, parent.physicality->physicality_id))
            ++cursor;
        if (first == cursor || !IntervalsMatch(parent, &bindings[first], cursor - first))
            return LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID;
    }
    if (cursor != binding_count) return LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID;

    static constexpr char Domain[] = "laplace-physicality-occurrence-batch-v1";
    blake3_hasher hash;
    blake3_hasher_init(&hash);
    blake3_hasher_update(&hash, Domain, sizeof(Domain) - 1U);
    Hash32(hash, LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION);
    Hash64(hash, parent_count);
    Hash64(hash, binding_count);
    cursor = 0U;
    for (std::uint64_t index = 0U; index < parent_count; ++index) {
        const auto first = cursor;
        while (cursor < binding_count && Same(bindings[cursor].parent_physicality_id, parents[index].physicality->physicality_id))
            ++cursor;
        const auto receipt = ParentReceipt(parents[index].physicality->physicality_id, &bindings[first], cursor - first);
        if (parent_receipts != nullptr) parent_receipts[index] = receipt;
        blake3_hasher_update(&hash, receipt.bytes, sizeof(receipt.bytes));
    }
    blake3_hasher_finalize(&hash, batch_receipt->bytes, sizeof(batch_receipt->bytes));
    return LAPLACE_PHYSICALITY_OCCURRENCE_OK;
}

extern "C" laplace_physicality_occurrence_status
laplace_physicality_occurrence_bindings_validate(
    const laplace_persistence_physicality_record* parent,
    const laplace_trajectory_carrier* carriers, std::uint64_t carrier_count,
    const laplace_physicality_occurrence_binding* bindings, std::uint64_t binding_count,
    std::uint64_t maximum_carriers, std::uint64_t maximum_bindings,
    std::uint64_t maximum_logical_count, laplace_digest256* receipt) {
    const laplace_physicality_occurrence_parent view{parent, carriers, carrier_count};
    return laplace_physicality_occurrence_bindings_validate_batch(&view, 1U, bindings,
        binding_count, 1U, maximum_carriers, maximum_bindings, maximum_logical_count,
        receipt, nullptr, 0U);
}
