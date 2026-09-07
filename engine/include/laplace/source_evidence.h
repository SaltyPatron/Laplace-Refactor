#ifndef LAPLACE_SOURCE_EVIDENCE_H
#define LAPLACE_SOURCE_EVIDENCE_H

#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/tabular_source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_source_claim {
    laplace_evidence_lineage_record lineage;
    uint32_t outcome_type;
} laplace_source_claim;

/* Native source-admission preparation over caller-owned bounded buffers.
 * Successful output is ordered by native identity for bulk deposition.
 * On any non-OK result the caller must discard the output buffer. */
LAPLACE_API laplace_tabular_source_status laplace_source_claims_build_batch(
    const laplace_tabular_source_plan_view* plan,
    const laplace_composition_result* results, size_t result_count,
    laplace_source_claim* claims, size_t claim_capacity);

LAPLACE_API laplace_tabular_source_status laplace_source_testimony_build_batch(
    const laplace_source_claim* claims, size_t claim_count,
    const laplace_source_profile_manifest* profile,
    const laplace_digest256* source_fingerprint,
    laplace_evidence_testimony_record* records, size_t record_capacity);

#ifdef __cplusplus
}
#endif
#endif
