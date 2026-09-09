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

}  // namespace

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
        DigestZero(input->context_fingerprint)) {
        return LAPLACE_UCDXML_EVIDENCE_INVALID_ARGUMENT;
    }
    const auto& property = *input->property;
    if (property.codepoint_position >= UINT32_C(0x110000) ||
        property.range_first > property.range_last ||
        property.codepoint_position < property.range_first ||
        property.codepoint_position > property.range_last ||
        property.property_name_byte_start >= property.property_name_byte_end ||
        property.property_name_byte_end > input->source_content->byte_count ||
        property.property_value_byte_start > property.property_value_byte_end ||
        property.property_value_byte_end > input->source_content->byte_count ||
        (property.flags & ~LAPLACE_UCDXML_PROPERTY_KNOWN_FLAGS) != 0u ||
        DigestZero(property.observation_fingerprint) ||
        input->source_content->byte_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_UCDXML_EVIDENCE_PROPERTY_INVALID;
    }

    if (laplace_identity_codepoint_witness(
            property.codepoint_position,
            &result->codepoint_entity_id,
            &result->codepoint_identity_witness) != LAPLACE_IDENTITY_OK) {
        return LAPLACE_UCDXML_EVIDENCE_IDENTITY_FAILURE;
    }

    const auto* const property_name = input->source_content->bytes +
        static_cast<std::size_t>(property.property_name_byte_start);
    const std::size_t property_name_bytes = static_cast<std::size_t>(
        property.property_name_byte_end - property.property_name_byte_start);
    auto identity_status = TextIdentity(
        property_name, property_name_bytes, &result->property_name_identity);
    if (identity_status != LAPLACE_UCDXML_EVIDENCE_OK) return identity_status;

    std::vector<std::uint8_t> effective_value;
    try {
        std::size_t required_bytes = 0u;
        const auto measure_status = laplace_ucdxml_property_value(
            input->projection, &property, nullptr, 0u, &required_bytes);
        if (measure_status != LAPLACE_UCDXML_OK) {
            return LAPLACE_UCDXML_EVIDENCE_VALUE_INVALID;
        }
        if (required_bytes == 0u) {
            return LAPLACE_UCDXML_EVIDENCE_EMPTY_VALUE_UNREPRESENTABLE;
        }
        effective_value.resize(required_bytes);
        std::size_t decoded_bytes = 0u;
        const auto value_status = laplace_ucdxml_property_value(
            input->projection, &property, effective_value.data(),
            effective_value.size(), &decoded_bytes);
        if (value_status != LAPLACE_UCDXML_OK ||
            decoded_bytes != effective_value.size()) {
            return LAPLACE_UCDXML_EVIDENCE_VALUE_INVALID;
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_UCDXML_EVIDENCE_MEMORY_FAILURE;
    }
    identity_status = TextIdentity(
        effective_value.data(), effective_value.size(),
        &result->property_value_identity);
    if (identity_status != LAPLACE_UCDXML_EVIDENCE_OK) return identity_status;

    const laplace_id128 proposition_children[3] = {
        result->codepoint_entity_id,
        result->property_name_identity.entity_id,
        result->property_value_identity.entity_id};
    if (laplace_identity_composite_witness(
            proposition_children, 3u, nullptr,
            &result->proposition_entity_id,
            &result->proposition_identity_witness) != LAPLACE_IDENTITY_OK) {
        return LAPLACE_UCDXML_EVIDENCE_IDENTITY_FAILURE;
    }

    result->occurrence = laplace_persistence_attestation_record{};
    result->occurrence.entity_id = result->proposition_entity_id;
    result->occurrence.source_fingerprint = input->source_fingerprint;
    result->occurrence.context_fingerprint = input->context_fingerprint;
    result->occurrence.source_ordinal = input->source_ordinal;
    result->occurrence.attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_SOURCE_TESTIMONY;
    if (laplace_persistence_attestation_identify(
            &result->occurrence,
            &result->occurrence.attestation_id) != LAPLACE_PERSISTENCE_OK) {
        return LAPLACE_UCDXML_EVIDENCE_OCCURRENCE_FAILURE;
    }

    result->lineage = laplace_evidence_lineage_record{};
    result->lineage.proposition_id = result->proposition_entity_id;
    result->lineage.occurrence_id = result->occurrence.attestation_id;
    result->lineage.source_id = input->source_fingerprint;
    result->lineage.context_id = input->context_fingerprint;
    result->lineage.source_ordinal = input->source_ordinal;
    result->lineage.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
    result->lineage.epistemic_kind = LAPLACE_EVIDENCE_KIND_TESTIMONY;
    if (laplace_evidence_node_identify(
            &result->lineage, &result->lineage.node_id) !=
        LAPLACE_EVIDENCE_LINEAGE_OK) {
        return LAPLACE_UCDXML_EVIDENCE_LINEAGE_FAILURE;
    }

    std::size_t root_count = 0u;
    laplace_evidence_lineage_error lineage_error{};
    if (laplace_evidence_record_lineage_batch(
            &result->lineage, 1u, input->lineage_memory_limit_bytes,
            &result->root, 1u, &root_count,
            &result->lineage_receipt, &lineage_error) !=
            LAPLACE_EVIDENCE_LINEAGE_OK ||
        root_count != 1u ||
        std::memcmp(
            result->root.node_id.bytes, result->lineage.node_id.bytes,
            sizeof(result->root.node_id.bytes)) != 0 ||
        std::memcmp(
            result->root.root_node_id.bytes, result->lineage.node_id.bytes,
            sizeof(result->root.root_node_id.bytes)) != 0 ||
        result->root.path_depth != 0u ||
        result->root.root_epistemic_kind != LAPLACE_EVIDENCE_KIND_TESTIMONY) {
        return LAPLACE_UCDXML_EVIDENCE_LINEAGE_FAILURE;
    }

    result->testimony = laplace_evidence_testimony_record{};
    result->testimony.evidence_node_id = result->lineage.node_id;
    result->testimony.source_profile_id = input->source_profile_id;
    result->testimony.recipe_receipt_id = input->evidence_recipe_receipt_id;
    result->testimony.trust_input_id = input->trust_input_id;
    result->testimony.outcome_detail_id = property.observation_fingerprint;
    result->testimony.uncertainty_denominator = 1u;
    result->testimony.sample_count = 1u;
    result->testimony.source_type = input->source_type;
    result->testimony.outcome_type = LAPLACE_EVIDENCE_OUTCOME_ASSERTION;
    result->testimony.disposition = LAPLACE_EVIDENCE_DISPOSITION_PERSISTED;
    if (laplace_evidence_testimony_identify(
            &result->testimony, &result->testimony.testimony_id) !=
        LAPLACE_EVIDENCE_TESTIMONY_OK) {
        return LAPLACE_UCDXML_EVIDENCE_TESTIMONY_FAILURE;
    }
    laplace_evidence_testimony_error testimony_error{};
    if (laplace_evidence_record_testimony_batch(
            &result->testimony, 1u,
            &result->testimony_receipt, &testimony_error) !=
        LAPLACE_EVIDENCE_TESTIMONY_OK) {
        return LAPLACE_UCDXML_EVIDENCE_TESTIMONY_FAILURE;
    }

    result->status = LAPLACE_UCDXML_EVIDENCE_OK;
    return LAPLACE_UCDXML_EVIDENCE_OK;
}
