#ifndef LAPLACE_EVIDENCE_TESTIMONY_H
#define LAPLACE_EVIDENCE_TESTIMONY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/evidence_testimony.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_evidence_testimony_record {
    laplace_digest256 testimony_id;
    laplace_digest256 evidence_node_id;
    laplace_digest256 source_profile_id;
    laplace_digest256 recipe_receipt_id;
    laplace_digest256 trust_input_id;
    laplace_digest256 outcome_detail_id;
    uint64_t uncertainty_numerator;
    uint64_t uncertainty_denominator;
    uint64_t sample_count;
    uint32_t source_type;
    uint32_t outcome_type;
    uint32_t disposition;
    uint32_t flags;
} laplace_evidence_testimony_record;

typedef struct laplace_evidence_testimony_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 source_profile_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t testimony_count;
    uint64_t sample_count;
    uint64_t uncertain_count;
    uint64_t negative_disposition_count;
    uint32_t version;
    uint32_t status;
} laplace_evidence_testimony_receipt;

typedef struct laplace_evidence_testimony_error {
    uint64_t record_index;
} laplace_evidence_testimony_error;

typedef enum laplace_evidence_testimony_status {
    LAPLACE_EVIDENCE_TESTIMONY_OK = 0,
    LAPLACE_EVIDENCE_TESTIMONY_INVALID_ARGUMENT = 1,
    LAPLACE_EVIDENCE_TESTIMONY_RECORD_INVALID = 2,
    LAPLACE_EVIDENCE_TESTIMONY_IDENTITY_MISMATCH = 3,
    LAPLACE_EVIDENCE_TESTIMONY_ORDER_INVALID = 4,
    LAPLACE_EVIDENCE_TESTIMONY_PROFILE_MISMATCH = 5,
    LAPLACE_EVIDENCE_TESTIMONY_UNCERTAINTY_INVALID = 6,
    LAPLACE_EVIDENCE_TESTIMONY_OVERFLOW = 7
} laplace_evidence_testimony_status;

LAPLACE_API laplace_evidence_testimony_status laplace_evidence_testimony_identify(
    const laplace_evidence_testimony_record* testimony,
    laplace_digest256* testimony_id);

LAPLACE_API laplace_evidence_testimony_status
laplace_evidence_record_testimony_batch(
    const laplace_evidence_testimony_record* records,
    size_t record_count,
    laplace_evidence_testimony_receipt* receipt,
    laplace_evidence_testimony_error* error);

#ifdef __cplusplus
}
#endif

#endif
