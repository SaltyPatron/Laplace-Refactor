#include "laplace/ucdxml_estate.h"

#include "blake3.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (std::size_t index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

void Start(blake3_hasher* const hasher, const char* const domain) {
    blake3_hasher_init(hasher);
    blake3_hasher_update(hasher, domain, std::strlen(domain));
}

laplace_digest256 Finish(blake3_hasher* const hasher) {
    laplace_digest256 digest{};
    blake3_hasher_finalize(hasher, digest.bytes, sizeof(digest.bytes));
    return digest;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool Add(std::uint64_t* const total, const std::size_t value) {
    if (total == nullptr ||
        value > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()) ||
        *total > std::numeric_limits<std::uint64_t>::max() -
            static_cast<std::uint64_t>(value)) {
        return false;
    }
    *total += static_cast<std::uint64_t>(value);
    return true;
}

struct EstateState final {
    const laplace_ucdxml_estate_input& input;
    laplace_ucdxml_estate_receipt& receipt;
    blake3_hasher lineage_hasher{};
    blake3_hasher testimony_hasher{};
    std::vector<laplace_ucdxml_property_view> properties;
    bool root_seen{};

    explicit EstateState(
        const laplace_ucdxml_estate_input& input_value,
        laplace_ucdxml_estate_receipt& receipt_value)
        : input(input_value), receipt(receipt_value) {
        Start(&lineage_hasher, LAPLACE_UCDXML_ESTATE_LINEAGE_BATCH_DOMAIN);
        Start(&testimony_hasher, LAPLACE_UCDXML_ESTATE_TESTIMONY_BATCH_DOMAIN);
        properties.reserve(input.property_batch_capacity);
    }
};

laplace_ucdxml_estate_status Flush(EstateState* const state) {
    if (state == nullptr || state->properties.empty()) {
        return LAPLACE_UCDXML_ESTATE_OK;
    }
    const std::size_t count = state->properties.size();
    if (count > (std::numeric_limits<std::size_t>::max() - 1u) / 2u) {
        return LAPLACE_UCDXML_ESTATE_OVERFLOW;
    }
    std::vector<laplace_persistence_attestation_record> occurrences(count);
    std::vector<laplace_evidence_lineage_record> lineage(count * 2u + 1u);
    std::vector<laplace_evidence_root_record> roots(count + 1u);
    std::vector<laplace_evidence_testimony_record> testimonies(count);

    laplace_ucdxml_evidence_batch_input batch{};
    batch.projection = state->input.projection;
    batch.source_content = state->input.source_content;
    batch.properties = state->properties.data();
    batch.property_count = count;
    batch.source_root_identity = state->input.source_root_identity;
    batch.source_fingerprint = state->input.source_fingerprint;
    batch.source_profile_id = state->input.source_profile_id;
    batch.evidence_recipe_receipt_id = state->input.evidence_recipe_receipt_id;
    batch.trust_input_id = state->input.trust_input_id;
    batch.context_fingerprint = state->input.context_fingerprint;
    batch.source_root_ordinal = state->input.source_root_ordinal;
    batch.lineage_memory_limit_bytes = state->input.lineage_memory_limit_bytes;
    batch.source_type = state->input.source_type;

    std::size_t occurrence_count = 0u;
    std::size_t lineage_count = 0u;
    std::size_t root_count = 0u;
    std::size_t testimony_count = 0u;
    laplace_ucdxml_evidence_batch_receipt batch_receipt{};
    if (laplace_ucdxml_evidence_emit_batch(
            &batch,
            occurrences.data(), occurrences.size(), &occurrence_count,
            lineage.data(), lineage.size(), &lineage_count,
            roots.data(), roots.size(), &root_count,
            testimonies.data(), testimonies.size(), &testimony_count,
            &batch_receipt) != LAPLACE_UCDXML_EVIDENCE_OK ||
        occurrence_count != count || lineage_count != count * 2u + 1u ||
        root_count != count + 1u || testimony_count != count) {
        return LAPLACE_UCDXML_ESTATE_EVIDENCE_FAILURE;
    }

    if (!state->root_seen) {
        state->receipt.source_root_node_id =
            batch_receipt.evidence.dependence_root_node_id;
        state->root_seen = true;
    } else if (!SameDigest(
                   state->receipt.source_root_node_id,
                   batch_receipt.evidence.dependence_root_node_id)) {
        return LAPLACE_UCDXML_ESTATE_ROOT_DRIFT;
    }

    if (state->input.sink != nullptr && state->input.sink(
            state->input.sink_state,
            occurrences.data(), occurrence_count,
            lineage.data(), lineage_count,
            roots.data(), root_count,
            testimonies.data(), testimony_count,
            &batch_receipt) != 0) {
        return LAPLACE_UCDXML_ESTATE_SINK_FAILURE;
    }

    blake3_hasher_update(
        &state->lineage_hasher,
        batch_receipt.evidence.lineage_receipt.receipt_id.bytes,
        sizeof(batch_receipt.evidence.lineage_receipt.receipt_id.bytes));
    blake3_hasher_update(
        &state->testimony_hasher,
        batch_receipt.evidence.testimony_receipt.receipt_id.bytes,
        sizeof(batch_receipt.evidence.testimony_receipt.receipt_id.bytes));
    HashU64(&state->lineage_hasher, static_cast<std::uint64_t>(lineage_count));
    HashU64(&state->testimony_hasher, static_cast<std::uint64_t>(testimony_count));

    if (!Add(&state->receipt.property_count, count) ||
        !Add(&state->receipt.occurrence_count, occurrence_count) ||
        !Add(&state->receipt.lineage_record_count, lineage_count) ||
        !Add(&state->receipt.root_relation_count, root_count) ||
        !Add(&state->receipt.testimony_count, testimony_count) ||
        state->receipt.batch_count == std::numeric_limits<std::uint64_t>::max()) {
        return LAPLACE_UCDXML_ESTATE_OVERFLOW;
    }
    ++state->receipt.batch_count;
    state->properties.clear();
    return LAPLACE_UCDXML_ESTATE_OK;
}

}  // namespace

extern "C" laplace_ucdxml_estate_status laplace_ucdxml_estate_execute(
    const laplace_ucdxml_estate_input* const input,
    laplace_ucdxml_estate_receipt* const receipt) {
    if (receipt != nullptr) *receipt = laplace_ucdxml_estate_receipt{};
    if (input == nullptr || receipt == nullptr || input->projection == nullptr ||
        input->source_content == nullptr || input->source_content->bytes == nullptr ||
        input->source_content->byte_count == 0u ||
        input->source_root_identity.codepoint_count == 0u ||
        input->source_root_ordinal == 0u || input->lineage_memory_limit_bytes == 0u ||
        input->property_batch_capacity == 0u || input->flags != 0u ||
        input->property_batch_capacity >
            (std::numeric_limits<std::size_t>::max() - 1u) / 2u) {
        return LAPLACE_UCDXML_ESTATE_INVALID_ARGUMENT;
    }

    receipt->source_profile_id = input->source_profile_id;
    receipt->version = LAPLACE_UCDXML_ESTATE_VERSION;
    try {
        EstateState state(*input, *receipt);
        for (std::uint32_t position = 0u; position < UINT32_C(0x110000); ++position) {
            std::size_t property_count = 0u;
            const auto count_status = laplace_ucdxml_property_count(
                input->projection, position, &property_count);
            if (count_status == LAPLACE_UCDXML_PROPERTY_NOT_FOUND) continue;
            if (count_status != LAPLACE_UCDXML_OK) {
                return LAPLACE_UCDXML_ESTATE_PROJECTION_FAILURE;
            }
            if (receipt->covered_position_count ==
                std::numeric_limits<std::uint64_t>::max()) {
                return LAPLACE_UCDXML_ESTATE_OVERFLOW;
            }
            ++receipt->covered_position_count;
            for (std::size_t property_index = 0u;
                 property_index < property_count; ++property_index) {
                laplace_ucdxml_property_view property{};
                if (laplace_ucdxml_property(
                        input->projection, position, property_index, &property) !=
                    LAPLACE_UCDXML_OK) {
                    return LAPLACE_UCDXML_ESTATE_PROJECTION_FAILURE;
                }
                state.properties.push_back(property);
                if (state.properties.size() == input->property_batch_capacity) {
                    const auto flush_status = Flush(&state);
                    if (flush_status != LAPLACE_UCDXML_ESTATE_OK) {
                        return flush_status;
                    }
                }
            }
        }
        const auto flush_status = Flush(&state);
        if (flush_status != LAPLACE_UCDXML_ESTATE_OK) return flush_status;
        if (!state.root_seen || receipt->property_count == 0u ||
            receipt->batch_count == 0u) {
            return LAPLACE_UCDXML_ESTATE_EMPTY;
        }
        receipt->lineage_batches_fingerprint = Finish(&state.lineage_hasher);
        receipt->testimony_batches_fingerprint = Finish(&state.testimony_hasher);
        receipt->status = LAPLACE_UCDXML_ESTATE_OK;

        blake3_hasher hasher{};
        Start(&hasher, LAPLACE_UCDXML_ESTATE_RECEIPT_DOMAIN);
        blake3_hasher_update(
            &hasher, receipt->source_profile_id.bytes,
            sizeof(receipt->source_profile_id.bytes));
        blake3_hasher_update(
            &hasher, receipt->source_root_node_id.bytes,
            sizeof(receipt->source_root_node_id.bytes));
        blake3_hasher_update(
            &hasher, receipt->lineage_batches_fingerprint.bytes,
            sizeof(receipt->lineage_batches_fingerprint.bytes));
        blake3_hasher_update(
            &hasher, receipt->testimony_batches_fingerprint.bytes,
            sizeof(receipt->testimony_batches_fingerprint.bytes));
        HashU64(&hasher, receipt->covered_position_count);
        HashU64(&hasher, receipt->property_count);
        HashU64(&hasher, receipt->batch_count);
        HashU64(&hasher, receipt->occurrence_count);
        HashU64(&hasher, receipt->lineage_record_count);
        HashU64(&hasher, receipt->root_relation_count);
        HashU64(&hasher, receipt->testimony_count);
        HashU32(&hasher, receipt->version);
        HashU32(&hasher, receipt->status);
        receipt->receipt_id = Finish(&hasher);
        return LAPLACE_UCDXML_ESTATE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_UCDXML_ESTATE_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_UCDXML_ESTATE_OVERFLOW;
    }
}
