#ifndef LAPLACE_WORLD_ADMISSION_H
#define LAPLACE_WORLD_ADMISSION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/world_admission.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_world_admission_record {
    laplace_digest256 admission_id;
    laplace_digest256 source_profile_id;
    laplace_digest256 selected_boundary_fingerprint;
    laplace_digest256 source_profile_receipt_id;
    laplace_digest256 recipe_receipt_id;
    laplace_digest256 composition_working_set_receipt_id;
    laplace_digest256 composition_presence_receipt_id;
    laplace_digest256 composition_producer_receipt_id;
    laplace_digest256 composition_stream_receipt_id;
    laplace_digest256 evidence_lineage_receipt_id;
    laplace_digest256 evidence_testimony_receipt_id;
    laplace_digest256 readback_fingerprint;
    uint64_t profile_occurrence_count;
    uint64_t composition_occurrence_count;
    uint64_t profile_claim_count;
    uint64_t evidence_node_count;
    uint64_t testimony_count;
    uint64_t profile_bound_testimony_count;
    uint64_t recipe_bound_testimony_count;
    uint64_t lineage_bound_testimony_count;
    uint64_t closure_subject_count;
    uint64_t closed_subject_count;
    uint32_t reconstruction_class;
    uint32_t flags;
} laplace_world_admission_record;

typedef struct laplace_world_admission_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 selected_boundary_fingerprint;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t admission_count;
    uint64_t occurrence_count;
    uint64_t claim_count;
    uint64_t evidence_node_count;
    uint64_t testimony_count;
    uint64_t closure_subject_count;
    uint32_t version;
    uint32_t status;
} laplace_world_admission_receipt;

typedef struct laplace_world_admission_error {
    uint64_t admission_index;
    uint32_t field;
    uint32_t reserved;
} laplace_world_admission_error;

typedef enum laplace_world_admission_status {
    LAPLACE_WORLD_ADMISSION_OK = 0,
    LAPLACE_WORLD_ADMISSION_INVALID_ARGUMENT = 1,
    LAPLACE_WORLD_ADMISSION_COMPONENT_MISSING = 2,
    LAPLACE_WORLD_ADMISSION_OCCURRENCE_MISMATCH = 3,
    LAPLACE_WORLD_ADMISSION_CLAIM_MISMATCH = 4,
    LAPLACE_WORLD_ADMISSION_PROFILE_BINDING_MISMATCH = 5,
    LAPLACE_WORLD_ADMISSION_RECIPE_BINDING_MISMATCH = 6,
    LAPLACE_WORLD_ADMISSION_LINEAGE_BINDING_MISMATCH = 7,
    LAPLACE_WORLD_ADMISSION_CLOSURE_MISMATCH = 8,
    LAPLACE_WORLD_ADMISSION_RECONSTRUCTION_INVALID = 9,
    LAPLACE_WORLD_ADMISSION_IDENTITY_MISMATCH = 10,
    LAPLACE_WORLD_ADMISSION_ORDER_INVALID = 11,
    LAPLACE_WORLD_ADMISSION_BOUNDARY_MISMATCH = 12,
    LAPLACE_WORLD_ADMISSION_OVERFLOW = 13
} laplace_world_admission_status;

LAPLACE_API laplace_world_admission_status laplace_world_admission_identify(
    const laplace_world_admission_record* admission,
    laplace_digest256* admission_id);

LAPLACE_API laplace_world_admission_status laplace_world_admission_close_batch(
    const laplace_world_admission_record* admissions,
    size_t admission_count,
    laplace_world_admission_receipt* receipt,
    laplace_world_admission_error* error);

#ifdef __cplusplus
}
#endif

#endif
