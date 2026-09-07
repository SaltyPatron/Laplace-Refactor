#include "laplace/source_evidence.h"
#include "laplace/persistence.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "blake3.h"

static int claim_compare(const void* left, const void* right) {
    const laplace_source_claim* a = (const laplace_source_claim*)left;
    const laplace_source_claim* b = (const laplace_source_claim*)right;
    return memcmp(a->lineage.node_id.bytes, b->lineage.node_id.bytes, 32u);
}

static int testimony_compare(const void* left, const void* right) {
    const laplace_evidence_testimony_record* a =
        (const laplace_evidence_testimony_record*)left;
    const laplace_evidence_testimony_record* b =
        (const laplace_evidence_testimony_record*)right;
    return memcmp(a->testimony_id.bytes, b->testimony_id.bytes, 32u);
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 24u)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static laplace_digest256 finish_hash(blake3_hasher* hasher) {
    laplace_digest256 result;
    blake3_hasher_finalize(hasher, result.bytes, sizeof(result.bytes));
    return result;
}

static laplace_digest256 trust_input_id(
    const laplace_source_profile_manifest* profile) {
    static const char domain[] = "laplace-source-trust-input-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(
        &hasher, profile->authority_release_fingerprint.bytes, 32u);
    blake3_hasher_update(&hasher, profile->license_fingerprint.bytes, 32u);
    blake3_hasher_update(
        &hasher, profile->epistemic_witnessing_fingerprint.bytes, 32u);
    return finish_hash(&hasher);
}

static laplace_digest256 outcome_detail_id(
    const laplace_digest256* source,
    const laplace_digest256* node,
    uint32_t outcome_type) {
    static const char domain[] = "laplace-source-outcome-detail-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(&hasher, source->bytes, 32u);
    blake3_hasher_update(&hasher, node->bytes, 32u);
    hash_u32(&hasher, outcome_type);
    return finish_hash(&hasher);
}

laplace_tabular_source_status laplace_source_claims_build_batch(
    const laplace_tabular_source_plan_view* plan,
    const laplace_composition_result* results, size_t result_count,
    laplace_source_claim* claims, size_t claim_capacity) {
    size_t index;
    if (plan == NULL || results == NULL || claims == NULL ||
        plan->claim_count == 0u || plan->claim_count > SIZE_MAX / sizeof(*claims) ||
        plan->claim_count > claim_capacity ||
        plan->claim_result_indexes == NULL || plan->claim_source_ordinals == NULL ||
        plan->claim_outcome_types == NULL || plan->requests == NULL ||
        plan->request_count > SIZE_MAX / sizeof(*plan->requests) ||
        result_count > SIZE_MAX / sizeof(*results)) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    memset(claims, 0, (size_t)plan->claim_count * sizeof(*claims));
    for (index = 0u; index < (size_t)plan->claim_count; ++index) {
        const uint64_t result_index = plan->claim_result_indexes[index];
        laplace_persistence_attestation_record occurrence;
        if (result_index >= result_count ||
            result_index >= plan->request_count ||
            plan->claim_source_ordinals[index] == 0u) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
        memset(&occurrence, 0, sizeof(occurrence));
        occurrence.entity_id = results[result_index].entity_id;
        occurrence.physicality_id = results[result_index].physicality_id;
        occurrence.source_fingerprint = plan->source_fingerprint;
        occurrence.context_fingerprint =
            plan->requests[result_index].occurrence_context_fingerprint;
        occurrence.source_ordinal = plan->claim_source_ordinals[index];
        occurrence.flags = LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY;
        occurrence.attestation_kind =
            LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
        if (laplace_persistence_attestation_identify(
                &occurrence, &claims[index].lineage.occurrence_id) !=
            LAPLACE_PERSISTENCE_OK) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
        claims[index].lineage.proposition_id =
            results[result_index].entity_id;
        claims[index].lineage.source_id = plan->source_fingerprint;
        claims[index].lineage.context_id =
            plan->requests[result_index].occurrence_context_fingerprint;
        claims[index].lineage.source_ordinal =
            plan->claim_source_ordinals[index];
        claims[index].lineage.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
        claims[index].lineage.epistemic_kind =
            LAPLACE_EVIDENCE_KIND_TESTIMONY;
        claims[index].outcome_type = plan->claim_outcome_types[index];
        if (laplace_evidence_node_identify(
                &claims[index].lineage,
                &claims[index].lineage.node_id) !=
            LAPLACE_EVIDENCE_LINEAGE_OK) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
    }
    qsort(claims, (size_t)plan->claim_count, sizeof(*claims), claim_compare);
    return LAPLACE_TABULAR_SOURCE_OK;
}

laplace_tabular_source_status laplace_source_testimony_build_batch(
    const laplace_source_claim* claims, size_t claim_count,
    const laplace_source_profile_manifest* profile,
    const laplace_digest256* source_fingerprint,
    laplace_evidence_testimony_record* records, size_t record_capacity) {
    if (claims == NULL || profile == NULL || source_fingerprint == NULL ||
        records == NULL || claim_count == 0u || claim_count > record_capacity ||
        claim_count > SIZE_MAX / sizeof(*records) ||
        claim_count > SIZE_MAX / sizeof(*claims)) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    memset(records, 0, claim_count * sizeof(*records));
    const laplace_digest256 trust = trust_input_id(profile);
    const uint32_t source_type =
        laplace_source_profile_evidence_source_type(profile);
    size_t index;
    if (source_type == LAPLACE_SOURCE_PROFILE_EVIDENCE_UNSPECIFIED) {
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
    for (index = 0u; index < claim_count; ++index) {
        laplace_digest256 expected_node;
        if (memcmp(claims[index].lineage.source_id.bytes,
                   source_fingerprint->bytes, sizeof(source_fingerprint->bytes)) != 0 ||
            claims[index].lineage.record_kind != LAPLACE_EVIDENCE_RECORD_NODE ||
            claims[index].lineage.epistemic_kind != LAPLACE_EVIDENCE_KIND_TESTIMONY ||
            laplace_evidence_node_identify(&claims[index].lineage, &expected_node) !=
                LAPLACE_EVIDENCE_LINEAGE_OK ||
            memcmp(expected_node.bytes, claims[index].lineage.node_id.bytes,
                   sizeof(expected_node.bytes)) != 0) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
        records[index].evidence_node_id = claims[index].lineage.node_id;
        records[index].source_profile_id = profile->profile_id;
        records[index].recipe_receipt_id = profile->recipe_program_fingerprint;
        records[index].trust_input_id = trust;
        records[index].outcome_detail_id = outcome_detail_id(
            source_fingerprint, &claims[index].lineage.node_id,
            claims[index].outcome_type);
        records[index].uncertainty_denominator = 1u;
        records[index].sample_count = 1u;
        records[index].source_type = source_type;
        records[index].outcome_type = claims[index].outcome_type;
        records[index].disposition = LAPLACE_EVIDENCE_DISPOSITION_PERSISTED;
        if (laplace_evidence_testimony_identify(
                &records[index], &records[index].testimony_id) !=
            LAPLACE_EVIDENCE_TESTIMONY_OK) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
    }
    qsort(records, claim_count, sizeof(*records), testimony_compare);
    return LAPLACE_TABULAR_SOURCE_OK;
}
