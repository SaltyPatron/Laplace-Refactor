#ifndef LAPLACE_UCDXML_EVIDENCE_H
#define LAPLACE_UCDXML_EVIDENCE_H

#include <stdint.h>

#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/persistence.h"
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
    LAPLACE_UCDXML_EVIDENCE_EMPTY_VALUE_UNREPRESENTABLE = 9
} laplace_ucdxml_evidence_status;

/*
 * Convert one effective UAX #42 property into ordinary Laplace evidence:
 *
 *   canonical [codepoint, property-name, effective-value] proposition
 *     -> source-testimony occurrence
 *     -> evidence node/root
 *     -> typed testimony record/receipt.
 *
 * Exact XML syntax provenance remains in property.observation_fingerprint while
 * the proposition's value constituent is the decoded effective value.  The
 * source type is explicit so standards, curated corpora, direct observations,
 * user self-assertions and model outputs enter the same evidence machinery
 * without being assigned equal epistemic meaning.
 */
LAPLACE_API laplace_ucdxml_evidence_status laplace_ucdxml_evidence_emit(
    const laplace_ucdxml_evidence_input* input,
    laplace_ucdxml_evidence_result* result);

#ifdef __cplusplus
}
#endif

#endif
