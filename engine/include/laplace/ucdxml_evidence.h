#ifndef LAPLACE_UCDXML_EVIDENCE_H
#define LAPLACE_UCDXML_EVIDENCE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/persistence.h"
#include "laplace/source_evidence.h"
#include "laplace/text_identity.h"
#include "laplace/ucdxml_projection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_ucdxml_evidence_input {
    const laplace_ucdxml_projection* projection;
    const laplace_decomposition_content* source_content;
    const laplace_ucdxml_property_view* property;
    laplace_digest256 source_fingerprint;
    laplace_digest256 source_profile_id;
    laplace_digest256 evidence_recipe_receipt_id;
    laplace_digest256 trust_input_id;
    laplace_digest256 context_fingerprint;
    uint64_t source_ordinal;
    uint64_t lineage_memory_limit_bytes;
    uint32_t source_type;
    uint32_t flags;
} laplace_ucdxml_evidence_input;

typedef struct laplace_ucdxml_evidence_result {
    laplace_id128 codepoint_entity_id;
    laplace_digest256 codepoint_identity_witness;
    laplace_text_identity property_name_identity;
    laplace_text_identity property_value_identity;
    laplace_id128 proposition_entity_id;
    laplace_digest256 proposition_identity_witness;
    laplace_persistence_attestation_record occurrence;
    laplace_evidence_lineage_record lineage;
    laplace_evidence_root_record root;
    laplace_evidence_testimony_record testimony;
    laplace_evidence_lineage_receipt lineage_receipt;
    laplace_evidence_testimony_receipt testimony_receipt;
    uint32_t status;
    uint32_t flags;
} laplace_ucdxml_evidence_result;

typedef struct laplace_ucdxml_evidence_batch_input {
    const laplace_ucdxml_projection* projection;
    const laplace_decomposition_content* source_content;
    const laplace_ucdxml_property_view* properties;
    size_t property_count;
    /* Canonical identity of the complete selected UCDXML content. Compute once
     * from the common content-admission/identity path and reuse across bounded
     * batches; it is the common dependence-root proposition. */
    laplace_text_identity source_root_identity;
    laplace_digest256 source_fingerprint;
    laplace_digest256 source_profile_id;
    laplace_digest256 evidence_recipe_receipt_id;
    laplace_digest256 trust_input_id;
    laplace_digest256 context_fingerprint;
    uint64_t source_root_ordinal;
    uint64_t lineage_memory_limit_bytes;
    uint32_t source_type;
    uint32_t flags;
} laplace_ucdxml_evidence_batch_input;

typedef struct laplace_ucdxml_evidence_batch_receipt {
    laplace_persistence_attestation_record source_root_occurrence;
    laplace_source_evidence_batch_receipt evidence;
    uint64_t property_count;
    uint64_t occurrence_count;
    uint32_t status;
    uint32_t flags;
} laplace_ucdxml_evidence_batch_receipt;

typedef enum laplace_ucdxml_evidence_status {
    LAPLACE_UCDXML_EVIDENCE_OK = 0,
    LAPLACE_UCDXML_EVIDENCE_INVALID_ARGUMENT = 1,
    LAPLACE_UCDXML_EVIDENCE_PROPERTY_INVALID = 2,
    LAPLACE_UCDXML_EVIDENCE_VALUE_INVALID = 3,
    LAPLACE_UCDXML_EVIDENCE_IDENTITY_FAILURE = 4,
    LAPLACE_UCDXML_EVIDENCE_OCCURRENCE_FAILURE = 5,
    LAPLACE_UCDXML_EVIDENCE_LINEAGE_FAILURE = 6,
    LAPLACE_UCDXML_EVIDENCE_TESTIMONY_FAILURE = 7,
    LAPLACE_UCDXML_EVIDENCE_MEMORY_FAILURE = 8,
    LAPLACE_UCDXML_EVIDENCE_EMPTY_VALUE_UNREPRESENTABLE = 9,
    LAPLACE_UCDXML_EVIDENCE_SOURCE_ROOT_INVALID = 10,
    LAPLACE_UCDXML_EVIDENCE_CAPACITY_INSUFFICIENT = 11,
    LAPLACE_UCDXML_EVIDENCE_SOURCE_EVIDENCE_FAILURE = 12,
    LAPLACE_UCDXML_EVIDENCE_OVERFLOW = 13
} laplace_ucdxml_evidence_status;

/*
 * Convert one effective UAX #42 property into ordinary Laplace evidence.  This
 * compatibility scalar path still binds the property to the canonical exact
 * source-content root; it no longer manufactures the property itself as an
 * independent evidence root.
 */
LAPLACE_API laplace_ucdxml_evidence_status laplace_ucdxml_evidence_emit(
    const laplace_ucdxml_evidence_input* input,
    laplace_ucdxml_evidence_result* result);

/*
 * Set-oriented evidence adapter for UAX #42 projection. Every property becomes
 * a canonical [codepoint, property-name, effective-value] proposition and exact
 * source-testimony occurrence. The generic source-evidence owner then closes one
 * common source/release dependence root + N dependent testimony nodes + N edges
 * in a single lineage batch and a single testimony batch.
 *
 * Caller capacities are N occurrences, 2*N+1 lineage records, N+1 root
 * relations, and N testimony records. Output is atomic.
 */
LAPLACE_API laplace_ucdxml_evidence_status laplace_ucdxml_evidence_emit_batch(
    const laplace_ucdxml_evidence_batch_input* input,
    laplace_persistence_attestation_record* occurrences,
    size_t occurrence_capacity,
    size_t* occurrence_count,
    laplace_evidence_lineage_record* lineage_records,
    size_t lineage_capacity,
    size_t* lineage_count,
    laplace_evidence_root_record* root_relations,
    size_t root_capacity,
    size_t* root_count,
    laplace_evidence_testimony_record* testimonies,
    size_t testimony_capacity,
    size_t* testimony_count,
    laplace_ucdxml_evidence_batch_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
