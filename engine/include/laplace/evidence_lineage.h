#ifndef LAPLACE_EVIDENCE_LINEAGE_H
#define LAPLACE_EVIDENCE_LINEAGE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/evidence_lineage.h"
#include "laplace/export.h"
#include "laplace/identity.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_evidence_lineage_record {
    laplace_digest256 node_id;
    laplace_id128 proposition_id;
    laplace_digest256 occurrence_id;
    laplace_digest256 source_id;
    laplace_digest256 context_id;
    laplace_digest256 parent_node_id;
    uint64_t source_ordinal;
    uint32_t record_kind;
    uint32_t epistemic_kind;
    uint32_t flags;
    uint32_t reserved;
} laplace_evidence_lineage_record;

typedef struct laplace_evidence_root_record {
    laplace_digest256 node_id;
    laplace_digest256 root_node_id;
    laplace_id128 proposition_id;
    uint64_t path_depth;
    uint32_t root_epistemic_kind;
    uint32_t flags;
} laplace_evidence_root_record;

typedef struct laplace_evidence_lineage_error {
    laplace_digest256* cycle_path;
    uint64_t cycle_path_capacity;
    uint64_t cycle_path_count;
    uint64_t record_index;
} laplace_evidence_lineage_error;

typedef struct laplace_evidence_lineage_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t input_record_count;
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t root_relation_count;
    uint32_t version;
    uint32_t status;
} laplace_evidence_lineage_receipt;

typedef enum laplace_evidence_lineage_status {
    LAPLACE_EVIDENCE_LINEAGE_OK = 0,
    LAPLACE_EVIDENCE_LINEAGE_INVALID_ARGUMENT = 1,
    LAPLACE_EVIDENCE_LINEAGE_RECORD_INVALID = 2,
    LAPLACE_EVIDENCE_LINEAGE_ORDER_INVALID = 3,
    LAPLACE_EVIDENCE_LINEAGE_IDENTITY_MISMATCH = 4,
    LAPLACE_EVIDENCE_LINEAGE_REFERENCE_MISSING = 5,
    LAPLACE_EVIDENCE_LINEAGE_CYCLE = 6,
    LAPLACE_EVIDENCE_LINEAGE_CAPACITY_INSUFFICIENT = 7,
    LAPLACE_EVIDENCE_LINEAGE_RESOURCE_INSUFFICIENT = 8,
    LAPLACE_EVIDENCE_LINEAGE_OVERFLOW = 9
} laplace_evidence_lineage_status;

LAPLACE_API laplace_evidence_lineage_status laplace_evidence_node_identify(
    const laplace_evidence_lineage_record* node,
    laplace_digest256* node_id);

LAPLACE_API laplace_evidence_lineage_status laplace_evidence_record_lineage_batch(
    const laplace_evidence_lineage_record* records,
    size_t record_count,
    uint64_t memory_limit_bytes,
    laplace_evidence_root_record* roots,
    size_t root_capacity,
    size_t* root_count,
    laplace_evidence_lineage_receipt* receipt,
    laplace_evidence_lineage_error* error);

#ifdef __cplusplus
}
#endif

#endif
