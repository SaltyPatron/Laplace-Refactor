#include "laplace/ucdxml_evidence.h"

#include "laplace/identity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SourceTypeValid(const std::uint32_t source_type) {
    return source_type >= LAPLACE_EVIDENCE_SOURCE_STANDARD &&
        source_type <= LAPLACE_EVIDENCE_SOURCE_EXTERNAL_PROVIDER;
}

laplace_ucdxml_evidence_status TextIdentity(
    const std::uint8_t* const bytes,
    const std::size_t count,
    laplace_text_identity* const identity) {
    const auto status = laplace_text_identity_utf8(bytes, count, identity);
    if (status == LAPLACE_TEXT_IDENTITY_EMPTY) {
        return LAPLACE_UCDXML_EVIDENCE_EMPTY_VALUE_UNREPRESENTABLE;
    }
    return status == LAPLACE_TEXT_IDENTITY_OK
        ? LAPLACE_UCDXML_EVIDENCE_OK
        : LAPLACE_UCDXML_EVIDENCE_IDENTITY_FAILURE;
}

struct PreparedProperty final {
    laplace_id128 codepoint_entity_id{};
    laplace_digest256 codepoint_identity_witness{};
    laplace_text_identity property_name_identity{};
    laplace_text_identity property_value_identity{};
    laplace_id128 proposition_entity_id{};
    laplace_digest256 proposition_identity_witness{};
    laplace_persistence_attestation_record occurrence{};
    laplace_source_evidence_claim claim{};
};

laplace_ucdxml_evidence_status PrepareProperty(
    const laplace_ucdxml_projection* const projection,
    const laplace_decomposition_content* const source_content,
    const laplace_ucdxml_property_view& property,
    const laplace_digest256& source_fingerprint,
    const laplace_digest256& context_fingerprint,
    const std::uint64_t source_ordinal,
    PreparedProperty* const prepared) {
    if (projection == nullptr || source_content == nullptr ||
        source_content->bytes == nullptr || prepared == nullptr ||
        source_ordinal == 0u ||
        source_content->byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        property.codepoint_position >= UINT32_C(0x110000) ||
        property.range_first > property.range_last ||
        property.codepoint_position < property.range_first ||
        property.codepoint_position > property.range_last ||
        property.property_name_byte_start >= property.property_name_byte_end ||
        property.property_name_byte_end > source_content->byte_count ||
        property.property_value_byte_start > property.property_value_byte_end ||
        property.property_value_byte_end > source_content->byte_count ||
        (property.flags & ~LAPLACE_UCDXML_PROPERTY_KNOWN_FLAGS) != 0u ||
        DigestZero(property.observation_fingerprint)) {
        return LAPLACE_UCDXML_EVIDENCE_PROPERTY_INVALID;
    }

    *prepared = PreparedProperty{};
    if (laplace_identity_codepoint_witness(
            property.codepoint_position,
            &prepared->codepoint_entity_id,
            &prepared->codepoint_identity_witness) != LAPLACE_IDENTITY_OK) {
        return LAPLACE_UCDXML_EVIDENCE_IDENTITY_FAILURE;
    }

    const auto* const property_name = source_content->bytes +
        static_cast<std::size_t>(property.property_name_byte_start);
    const std::size_t property_name_bytes = static_cast<std::size_t>(
        property.property_name_byte_end - property.property_name_byte_start);
    auto status = TextIdentity(
        property_name, property_name_bytes, &prepared->property_name_identity);
    if (status != LAPLACE_UCDXML_EVIDENCE_OK) return status;

    std::vector<std::uint8_t> effective_value;
    try {
        std::size_t required_bytes = 0u;
        if (laplace_ucdxml_property_value(
                projection, &property, nullptr, 0u, &required_bytes) !=
            LAPLACE_UCDXML_OK) {
            return LAPLACE_UCDXML_EVIDENCE_VALUE_INVALID;
        }
        if (required_bytes == 0u) {
            return LAPLACE_UCDXML_EVIDENCE_EMPTY_VALUE_UNREPRESENTABLE;
        }
        effective_value.resize(required_bytes);
        std::size_t decoded_bytes = 0u;
        if (laplace_ucdxml_property_value(
                projection, &property, effective_value.data(),
                effective_value.size(), &decoded_bytes) != LAPLACE_UCDXML_OK ||
            decoded_bytes != effective_value.size()) {
            return LAPLACE_UCDXML_EVIDENCE_VALUE_INVALID;
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_UCDXML_EVIDENCE_MEMORY_FAILURE;
    }
    status = TextIdentity(
        effective_value.data(), effective_value.size(),
        &prepared->property_value_identity);
    if (status != LAPLACE_UCDXML_EVIDENCE_OK) return status;

    const laplace_id128 proposition_children[3] = {
        prepared->codepoint_entity_id,
        prepared->property_name_identity.entity_id,
        prepared->property_value_identity.entity_id};
    if (laplace_identity_composite_witness(
            proposition_children, 3u, nullptr,
            &prepared->proposition_entity_id,
            &prepared->proposition_identity_witness) != LAPLACE_IDENTITY_OK) {
        return LAPLACE_UCDXML_EVIDENCE_IDENTITY_FAILURE;
    }

    prepared->occurrence.entity_id = prepared->proposition_entity_id;
    prepared->occurrence.source_fingerprint = source_fingerprint;
    prepared->occurrence.context_fingerprint = context_fingerprint;
    prepared->occurrence.source_ordinal = source_ordinal;
    prepared->occurrence.attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_SOURCE_TESTIMONY;
    if (laplace_persistence_attestation_identify(
            &prepared->occurrence,
            &prepared->occurrence.attestation_id) != LAPLACE_PERSISTENCE_OK) {
        return LAPLACE_UCDXML_EVIDENCE_OCCURRENCE_FAILURE;
    }

    prepared->claim.proposition_id = prepared->proposition_entity_id;
    prepared->claim.occurrence_id = prepared->occurrence.attestation_id;
    prepared->claim.outcome_detail_id = property.observation_fingerprint;
    prepared->claim.source_ordinal = source_ordinal;
    prepared->claim.uncertainty_numerator = 0u;
    prepared->claim.uncertainty_denominator = 1u;
    prepared->claim.sample_count = 1u;
    prepared->claim.outcome_type = LAPLACE_EVIDENCE_OUTCOME_ASSERTION;
    prepared->claim.disposition = LAPLACE_EVIDENCE_DISPOSITION_PERSISTED;
    return LAPLACE_UCDXML_EVIDENCE_OK;
}

laplace_ucdxml_evidence_status MakeSourceRootOccurrence(
    const laplace_text_identity& source_root_identity,
    const laplace_digest256& source_fingerprint,
    const laplace_digest256& context_fingerprint,
    const std::uint64_t source_root_ordinal,
    laplace_persistence_attestation_record* const occurrence) {
    if (occurrence == nullptr || source_root_identity.codepoint_count == 0u ||
        source_root_ordinal == 0u) {
        return LAPLACE_UCDXML_EVIDENCE_SOURCE_ROOT_INVALID;
    }
    *occurrence = laplace_persistence_attestation_record{};
    occurrence->entity_id = source_root_identity.entity_id;
    occurrence->source_fingerprint = source_fingerprint;
    occurrence->context_fingerprint = context_fingerprint;
    occurrence->source_ordinal = source_root_ordinal;
    occurrence->attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
    return laplace_persistence_attestation_identify(
               occurrence, &occurrence->attestation_id) == LAPLACE_PERSISTENCE_OK
        ? LAPLACE_UCDXML_EVIDENCE_OK
        : LAPLACE_UCDXML_EVIDENCE_SOURCE_ROOT_INVALID;
}

laplace_ucdxml_evidence_status ClosePreparedBatch(
    const laplace_text_identity& source_root_identity,
    const laplace_digest256& source_fingerprint,
    const laplace_digest256& source_profile_id,
    const laplace_digest256& recipe_receipt_id,
    const laplace_digest256& trust_input_id,
    const laplace_digest256& context_fingerprint,
    const std::uint64_t source_root_ordinal,
    const std::uint64_t lineage_memory_limit_bytes,
    const std::uint32_t source_type,
    const PreparedProperty* const prepared,
    const std::size_t prepared_count,
    laplace_evidence_lineage_record* const lineage_records,
    const std::size_t lineage_capacity,
    std::size_t* const lineage_count,
    laplace_evidence_root_record* const root_relations,
    const std::size_t root_capacity,
    std::size_t* const root_count,
    laplace_evidence_testimony_record* const testimonies,
    const std::size_t testimony_capacity,
    std::size_t* const testimony_count,
    laplace_ucdxml_evidence_batch_receipt* const receipt) {
    if (receipt != nullptr) *receipt = laplace_ucdxml_evidence_batch_receipt{};
    if (prepared == nullptr || prepared_count == 0u || receipt == nullptr ||
        lineage_memory_limit_bytes == 0u || !SourceTypeValid(source_type) ||
        DigestZero(source_fingerprint) || DigestZero(source_profile_id) ||
        DigestZero(recipe_receipt_id) || DigestZero(trust_input_id) ||
        DigestZero(context_fingerprint)) {
        return LAPLACE_UCDXML_EVIDENCE_INVALID_ARGUMENT;
    }

    std::vector<laplace_source_evidence_claim> claims;
    try {
        claims.reserve(prepared_count);
        for (std::size_t index = 0u; index < prepared_count; ++index) {
            claims.push_back(prepared[index].claim);
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_UCDXML_EVIDENCE_MEMORY_FAILURE;
    }

    auto status = MakeSourceRootOccurrence(
        source_root_identity, source_fingerprint, context_fingerprint,
        source_root_ordinal, &receipt->source_root_occurrence);
    if (status != LAPLACE_UCDXML_EVIDENCE_OK) return status;

    laplace_source_evidence_batch_input evidence{};
    evidence.dependence_root_proposition_id = source_root_identity.entity_id;
    evidence.dependence_root_occurrence_id =
        receipt->source_root_occurrence.attestation_id;
    evidence.source_fingerprint = source_fingerprint;
    evidence.source_profile_id = source_profile_id;
    evidence.recipe_receipt_id = recipe_receipt_id;
    evidence.trust_input_id = trust_input_id;
    evidence.context_fingerprint = context_fingerprint;
    evidence.claims = claims.data();
    evidence.claim_count = claims.size();
    evidence.dependence_root_source_ordinal = source_root_ordinal;
    evidence.lineage_memory_limit_bytes = lineage_memory_limit_bytes;
    evidence.dependence_root_epistemic_kind = LAPLACE_EVIDENCE_KIND_OBSERVED;
    evidence.source_type = source_type;

    const auto evidence_status = laplace_source_evidence_close_batch(
        &evidence,
        lineage_records, lineage_capacity, lineage_count,
        root_relations, root_capacity, root_count,
        testimonies, testimony_capacity, testimony_count,
        &receipt->evidence);
    if (evidence_status == LAPLACE_SOURCE_EVIDENCE_CAPACITY_INSUFFICIENT) {
        return LAPLACE_UCDXML_EVIDENCE_CAPACITY_INSUFFICIENT;
    }
    if (evidence_status == LAPLACE_SOURCE_EVIDENCE_MEMORY_FAILURE) {
        return LAPLACE_UCDXML_EVIDENCE_MEMORY_FAILURE;
    }
    if (evidence_status != LAPLACE_SOURCE_EVIDENCE_OK) {
        return LAPLACE_UCDXML_EVIDENCE_SOURCE_EVIDENCE_FAILURE;
    }
    receipt->property_count = static_cast<std::uint64_t>(prepared_count);
    receipt->occurrence_count = static_cast<std::uint64_t>(prepared_count);
    receipt->status = LAPLACE_UCDXML_EVIDENCE_OK;
    return LAPLACE_UCDXML_EVIDENCE_OK;
}

}  // namespace

extern "C" laplace_ucdxml_evidence_status laplace_ucdxml_evidence_emit_batch(
    const laplace_ucdxml_evidence_batch_input* const input,
    laplace_persistence_attestation_record* const occurrences,
    const std::size_t occurrence_capacity,
    std::size_t* const occurrence_count,
    laplace_evidence_lineage_record* const lineage_records,
    const std::size_t lineage_capacity,
    std::size_t* const lineage_count,
    laplace_evidence_root_record* const root_relations,
    const std::size_t root_capacity,
    std::size_t* const root_count,
    laplace_evidence_testimony_record* const testimonies,
    const std::size_t testimony_capacity,
    std::size_t* const testimony_count,
    laplace_ucdxml_evidence_batch_receipt* const receipt) {
    if (occurrence_count != nullptr) *occurrence_count = 0u;
    if (lineage_count != nullptr) *lineage_count = 0u;
    if (root_count != nullptr) *root_count = 0u;
    if (testimony_count != nullptr) *testimony_count = 0u;
    if (receipt != nullptr) *receipt = laplace_ucdxml_evidence_batch_receipt{};
    if (input == nullptr || input->projection == nullptr ||
        input->source_content == nullptr || input->source_content->bytes == nullptr ||
        input->properties == nullptr || input->property_count == 0u ||
        input->source_root_ordinal == 0u || input->lineage_memory_limit_bytes == 0u ||
        input->flags != 0u || !SourceTypeValid(input->source_type) ||
        occurrences == nullptr || occurrence_count == nullptr ||
        lineage_records == nullptr || lineage_count == nullptr ||
        root_relations == nullptr || root_count == nullptr || testimonies == nullptr ||
        testimony_count == nullptr || receipt == nullptr) {
        return LAPLACE_UCDXML_EVIDENCE_INVALID_ARGUMENT;
    }
    if (input->property_count >
        (std::numeric_limits<std::size_t>::max() - 1u) / 2u) {
        return LAPLACE_UCDXML_EVIDENCE_OVERFLOW;
    }
    if (occurrence_capacity < input->property_count ||
        lineage_capacity < input->property_count * 2u + 1u ||
        root_capacity < input->property_count + 1u ||
        testimony_capacity < input->property_count) {
        return LAPLACE_UCDXML_EVIDENCE_CAPACITY_INSUFFICIENT;
    }

    try {
        std::vector<PreparedProperty> prepared(input->property_count);
        for (std::size_t index = 0u; index < input->property_count; ++index) {
            const auto& property = input->properties[index];
            if (property.declaration_element_span_index == UINT64_MAX) {
                return LAPLACE_UCDXML_EVIDENCE_PROPERTY_INVALID;
            }
            const auto status = PrepareProperty(
                input->projection, input->source_content, property,
                input->source_fingerprint, input->context_fingerprint,
                property.declaration_element_span_index + 1u, &prepared[index]);
            if (status != LAPLACE_UCDXML_EVIDENCE_OK) return status;
        }

        const auto status = ClosePreparedBatch(
            input->source_root_identity,
            input->source_fingerprint,
            input->source_profile_id,
            input->evidence_recipe_receipt_id,
            input->trust_input_id,
            input->context_fingerprint,
            input->source_root_ordinal,
            input->lineage_memory_limit_bytes,
            input->source_type,
            prepared.data(), prepared.size(),
            lineage_records, lineage_capacity, lineage_count,
            root_relations, root_capacity, root_count,
            testimonies, testimony_capacity, testimony_count, receipt);
        if (status != LAPLACE_UCDXML_EVIDENCE_OK) return status;

        for (std::size_t index = 0u; index < prepared.size(); ++index) {
            occurrences[index] = prepared[index].occurrence;
        }
        *occurrence_count = prepared.size();
        return LAPLACE_UCDXML_EVIDENCE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_UCDXML_EVIDENCE_MEMORY_FAILURE;
    }
}

extern "C" laplace_ucdxml_evidence_status laplace_ucdxml_evidence_emit(
    const laplace_ucdxml_evidence_input* const input,
    laplace_ucdxml_evidence_result* const result) {
    if (result != nullptr) *result = laplace_ucdxml_evidence_result{};
    if (input == nullptr || result == nullptr || input->projection == nullptr ||
        input->source_content == nullptr || input->source_content->bytes == nullptr ||
        input->property == nullptr || input->source_ordinal == 0u ||
        input->lineage_memory_limit_bytes == 0u ||
        !SourceTypeValid(input->source_type) || input->flags != 0u ||
        DigestZero(input->source_fingerprint) ||
        DigestZero(input->source_profile_id) ||
        DigestZero(input->evidence_recipe_receipt_id) ||
        DigestZero(input->trust_input_id) ||
        DigestZero(input->context_fingerprint) ||
        input->source_content->byte_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_UCDXML_EVIDENCE_INVALID_ARGUMENT;
    }

    PreparedProperty prepared{};
    auto status = PrepareProperty(
        input->projection, input->source_content, *input->property,
        input->source_fingerprint, input->context_fingerprint,
        input->source_ordinal, &prepared);
    if (status != LAPLACE_UCDXML_EVIDENCE_OK) return status;

    laplace_text_identity source_root_identity{};
    status = TextIdentity(
        input->source_content->bytes,
        static_cast<std::size_t>(input->source_content->byte_count),
        &source_root_identity);
    if (status != LAPLACE_UCDXML_EVIDENCE_OK) {
        return LAPLACE_UCDXML_EVIDENCE_SOURCE_ROOT_INVALID;
    }

    laplace_evidence_lineage_record lineage_records[3]{};
    laplace_evidence_root_record root_relations[2]{};
    laplace_evidence_testimony_record testimonies[1]{};
    std::size_t lineage_count = 0u;
    std::size_t root_count = 0u;
    std::size_t testimony_count = 0u;
    laplace_ucdxml_evidence_batch_receipt batch_receipt{};
    status = ClosePreparedBatch(
        source_root_identity,
        input->source_fingerprint,
        input->source_profile_id,
        input->evidence_recipe_receipt_id,
        input->trust_input_id,
        input->context_fingerprint,
        1u,
        input->lineage_memory_limit_bytes,
        input->source_type,
        &prepared, 1u,
        lineage_records, 3u, &lineage_count,
        root_relations, 2u, &root_count,
        testimonies, 1u, &testimony_count,
        &batch_receipt);
    if (status != LAPLACE_UCDXML_EVIDENCE_OK || lineage_count != 3u ||
        root_count != 2u || testimony_count != 1u) {
        return status == LAPLACE_UCDXML_EVIDENCE_OK
            ? LAPLACE_UCDXML_EVIDENCE_SOURCE_EVIDENCE_FAILURE
            : status;
    }

    result->codepoint_entity_id = prepared.codepoint_entity_id;
    result->codepoint_identity_witness = prepared.codepoint_identity_witness;
    result->property_name_identity = prepared.property_name_identity;
    result->property_value_identity = prepared.property_value_identity;
    result->proposition_entity_id = prepared.proposition_entity_id;
    result->proposition_identity_witness = prepared.proposition_identity_witness;
    result->occurrence = prepared.occurrence;
    result->testimony = testimonies[0];
    result->lineage_receipt = batch_receipt.evidence.lineage_receipt;
    result->testimony_receipt = batch_receipt.evidence.testimony_receipt;

    for (const auto& lineage : lineage_records) {
        if (lineage.record_kind == LAPLACE_EVIDENCE_RECORD_NODE &&
            SameDigest(lineage.node_id, result->testimony.evidence_node_id)) {
            result->lineage = lineage;
            break;
        }
    }
    for (const auto& root : root_relations) {
        if (SameDigest(root.node_id, result->testimony.evidence_node_id)) {
            result->root = root;
            break;
        }
    }
    if (!SameDigest(result->lineage.node_id, result->testimony.evidence_node_id) ||
        !SameDigest(result->root.node_id, result->testimony.evidence_node_id) ||
        !SameDigest(
            result->root.root_node_id,
            batch_receipt.evidence.dependence_root_node_id) ||
        result->root.path_depth != 1u ||
        result->root.root_epistemic_kind != LAPLACE_EVIDENCE_KIND_OBSERVED) {
        return LAPLACE_UCDXML_EVIDENCE_SOURCE_EVIDENCE_FAILURE;
    }

    result->status = LAPLACE_UCDXML_EVIDENCE_OK;
    return LAPLACE_UCDXML_EVIDENCE_OK;
}
