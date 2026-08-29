#ifndef LAPLACE_REFERENCE_MAPPING_H
#define LAPLACE_REFERENCE_MAPPING_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/reference_mapping.h"
#include "laplace/export.h"
#include "laplace/highway.h"
#include "laplace/reference_topology.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_reference_mapping_candidate {
    laplace_digest256 boundary_id;
    laplace_digest256 source_profile_id;
    laplace_digest256 left_reference_id;
    laplace_digest256 right_reference_id;
    laplace_highway_coordinate left_coordinate;
    laplace_highway_coordinate right_coordinate;
    laplace_id128 relation_id;
    laplace_id128 row_entity_id;
    laplace_id128 left_field_entity_id;
    laplace_id128 left_value_entity_id;
    laplace_id128 right_field_entity_id;
    laplace_id128 right_value_entity_id;
    uint64_t source_ordinal;
    uint64_t artifact_ordinal;
    uint64_t row_ordinal;
    uint64_t relation_version;
    uint32_t relation_kind;
    uint32_t flags;
    uint32_t left_disposition;
    uint32_t right_disposition;
} laplace_reference_mapping_candidate;

typedef struct laplace_reference_mapping_record {
    laplace_reference_mapping_candidate candidate;
    laplace_digest256 proposition_id;
    laplace_digest256 occurrence_id;
    laplace_digest256 mapping_id;
    uint32_t disposition;
    uint32_t reserved;
} laplace_reference_mapping_record;

typedef struct laplace_reference_mapping_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 boundary_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t occurrence_count;
    uint64_t proposition_count;
    uint64_t resolved_count;
    uint64_t unresolved_count;
    uint64_t retired_count;
    uint32_t version;
    uint32_t status;
} laplace_reference_mapping_receipt;

typedef struct laplace_reference_mapping_error {
    uint64_t record_index;
    uint32_t field;
    uint32_t reserved;
} laplace_reference_mapping_error;

typedef enum laplace_reference_mapping_status {
    LAPLACE_REFERENCE_MAPPING_OK = 0,
    LAPLACE_REFERENCE_MAPPING_INVALID_ARGUMENT = 1,
    LAPLACE_REFERENCE_MAPPING_RECORD_INVALID = 2,
    LAPLACE_REFERENCE_MAPPING_BOUNDARY_MISMATCH = 3,
    LAPLACE_REFERENCE_MAPPING_DUPLICATE_OCCURRENCE = 4,
    LAPLACE_REFERENCE_MAPPING_COORDINATE_COLLISION = 5,
    LAPLACE_REFERENCE_MAPPING_MEMORY_FAILURE = 6,
    LAPLACE_REFERENCE_MAPPING_OVERFLOW = 7
} laplace_reference_mapping_status;

LAPLACE_API laplace_reference_mapping_status
laplace_reference_mapping_resolve_batch(
    const laplace_reference_mapping_candidate* candidates,
    size_t candidate_count,
    laplace_reference_mapping_record* records,
    laplace_reference_mapping_receipt* receipt,
    laplace_reference_mapping_error* error);

#ifdef __cplusplus
}
#endif

#endif
