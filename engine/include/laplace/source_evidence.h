#ifndef LAPLACE_SOURCE_EVIDENCE_H
#define LAPLACE_SOURCE_EVIDENCE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_source_evidence_claim {
    laplace_id128 proposition_id;
    laplace_digest256 occurrence_id;
    laplace_digest256 outcome_detail_id;
    uint64_t source_ordinal;
    uint64_t uncertainty_numerator;
    uint64_t uncertainty_denominator;
    uint64_t sample_count;
    uint32_t outcome_type;
    uint32_t disposition;
    uint32_t flags;
    uint32_t reserved;
} laplace_source_evidence_claim;

typedef struct laplace_source_evidence_batch_input {
    /* One exact observed/source-release proposition from which every claim in
     * this batch depends.  The root and claim occurrence identities are supplied
     * by the caller because occurrence witnessing is a separate plane from
     * evidence lineage and testimony. */
    laplace_id128 dependence_root_proposition_id;
    laplace_digest256 dependence_root_occurrence_id;
    laplace_digest256 source_fingerprint;
    laplace_digest256 source_profile_id;
    laplace_digest256 recipe_receipt_id;
    laplace_digest256 trust_input_id;
    laplace_digest256 context_fingerprint;
    const laplace_source_evidence_claim* claims;
    size_t claim_count;
    uint64_t dependence_root_source_ordinal;
    uint64_t lineage_memory_limit_bytes;
    uint32_t dependence_root_epistemic_kind;
    uint32_t source_type;
    uint32_t flags;
    uint32_t reserved;
} laplace_source_evidence_batch_input;

typedef struct laplace_source_evidence_batch_receipt {
    laplace_digest256 dependence_root_node_id;
    laplace_evidence_lineage_receipt lineage_receipt;
    laplace_evidence_testimony_receipt testimony_receipt;
    uint64_t claim_count;
    uint64_t lineage_record_count;
    uint64_t root_relation_count;
    uint64_t testimony_count;
    uint32_t status;
    uint32_t reserved;
} laplace_source_evidence_batch_receipt;

typedef enum laplace_source_evidence_status {
    LAPLACE_SOURCE_EVIDENCE_OK = 0,
    LAPLACE_SOURCE_EVIDENCE_INVALID_ARGUMENT = 1,
    LAPLACE_SOURCE_EVIDENCE_OVERFLOW = 2,
    LAPLACE_SOURCE_EVIDENCE_CAPACITY_INSUFFICIENT = 3,
    LAPLACE_SOURCE_EVIDENCE_LINEAGE_FAILURE = 4,
    LAPLACE_SOURCE_EVIDENCE_TESTIMONY_FAILURE = 5,
    LAPLACE_SOURCE_EVIDENCE_DEPENDENCE_FAILURE = 6,
    LAPLACE_SOURCE_EVIDENCE_MEMORY_FAILURE = 7
} laplace_source_evidence_status;

/*
 * Close one source-scoped evidence batch through the existing canonical lineage
 * and testimony engines.  The complete emitted lineage is:
 *
 *   one dependence-root node
 *   + N testimony nodes
 *   + N dependence edges (claim -> source/release root)
 *
 * Nodes are canonically sorted by node identity and edges by child then parent;
 * testimony records are canonically sorted by testimony identity.  The function
 * stages all output internally and publishes nothing unless the entire batch,
 * including the common-root proof, succeeds.
 *
 * Required caller capacities are exactly 2*N+1 lineage records, N+1 root
 * relations, and N testimony records.
 */
LAPLACE_API laplace_source_evidence_status laplace_source_evidence_close_batch(
    const laplace_source_evidence_batch_input* input,
    laplace_evidence_lineage_record* lineage_records,
    size_t lineage_capacity,
    size_t* lineage_count,
    laplace_evidence_root_record* root_relations,
    size_t root_capacity,
    size_t* root_count,
    laplace_evidence_testimony_record* testimonies,
    size_t testimony_capacity,
    size_t* testimony_count,
    laplace_source_evidence_batch_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
