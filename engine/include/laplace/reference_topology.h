#ifndef LAPLACE_REFERENCE_TOPOLOGY_H
#define LAPLACE_REFERENCE_TOPOLOGY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/reference_topology.h"
#include "laplace/export.h"
#include "laplace/highway.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_reference_candidate {
    laplace_digest256 source_profile_id;
    laplace_highway_key key;
    laplace_id128 row_entity_id;
    laplace_id128 field_entity_id;
    laplace_id128 value_entity_id;
    uint64_t source_ordinal;
    uint64_t artifact_ordinal;
    uint64_t row_ordinal;
    uint64_t column_ordinal;
    uint32_t rule_flags;
    uint32_t reserved;
} laplace_reference_candidate;

typedef struct laplace_reference_record {
    laplace_reference_candidate candidate;
    laplace_highway_coordinate coordinate;
    laplace_digest256 occurrence_id;
    laplace_digest256 reference_id;
    uint32_t disposition;
    uint32_t reserved;
} laplace_reference_record;

typedef struct laplace_reference_topology_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 source_profile_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t occurrence_count;
    uint64_t coordinate_count;
    uint64_t present_count;
    uint64_t retired_count;
    uint64_t unresolved_count;
    uint32_t version;
    uint32_t status;
} laplace_reference_topology_receipt;

typedef struct laplace_reference_topology_error {
    uint64_t record_index;
    uint32_t field;
    uint32_t reserved;
} laplace_reference_topology_error;

typedef enum laplace_reference_topology_status {
    LAPLACE_REFERENCE_TOPOLOGY_OK = 0,
    LAPLACE_REFERENCE_TOPOLOGY_INVALID_ARGUMENT = 1,
    LAPLACE_REFERENCE_TOPOLOGY_RECORD_INVALID = 2,
    LAPLACE_REFERENCE_TOPOLOGY_PROFILE_MISMATCH = 3,
    LAPLACE_REFERENCE_TOPOLOGY_DECLARATION_CONFLICT = 4,
    LAPLACE_REFERENCE_TOPOLOGY_COORDINATE_COLLISION = 5,
    LAPLACE_REFERENCE_TOPOLOGY_DUPLICATE_OCCURRENCE = 6,
    LAPLACE_REFERENCE_TOPOLOGY_MEMORY_FAILURE = 7,
    LAPLACE_REFERENCE_TOPOLOGY_OVERFLOW = 8,
    LAPLACE_REFERENCE_TOPOLOGY_HIGHWAY_FAILURE = 9
} laplace_reference_topology_status;

LAPLACE_API laplace_reference_topology_status
laplace_reference_topology_resolve_batch(
    const laplace_reference_candidate* candidates,
    size_t candidate_count,
    laplace_reference_record* records,
    laplace_reference_topology_receipt* receipt,
    laplace_reference_topology_error* error);

#ifdef __cplusplus
}
#endif

#endif
