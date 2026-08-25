#ifndef LAPLACE_PERSISTENCE_H
#define LAPLACE_PERSISTENCE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/persistence.h"
#include "laplace/framework.h"
#include "laplace/geometry.h"
#include "laplace/identity.h"
#include "laplace/trajectory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_persistence_physicality_record {
    laplace_digest256 physicality_id;
    laplace_id128 entity_id;
    uint32_t physicality_type;
    uint32_t vertex_class;
    uint32_t recipe_version;
    uint32_t structural_form;
    uint32_t dimension_count;
    uint32_t flags;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 trajectory_fingerprint;
    laplace_point4d centroid;
    double radius;
    uint64_t logical_count;
    uint64_t vertex_count;
} laplace_persistence_physicality_record;

typedef struct laplace_persistence_entity_record {
    laplace_id128 entity_id;
    laplace_digest256 identity_witness;
} laplace_persistence_entity_record;

typedef struct laplace_persistence_trajectory_record {
    laplace_digest256 physicality_id;
    uint64_t vertex_index;
    laplace_trajectory_carrier carrier;
    laplace_composition_occurrence occurrence;
} laplace_persistence_trajectory_record;

typedef struct laplace_persistence_occurrence_record {
    laplace_digest256 occurrence_id;
    laplace_id128 entity_id;
    laplace_digest256 physicality_id;
    laplace_digest256 source_fingerprint;
    laplace_digest256 context_fingerprint;
    uint64_t source_ordinal;
    uint32_t flags;
    uint32_t reserved;
} laplace_persistence_occurrence_record;

typedef struct laplace_persistence_record {
    uint16_t kind;
    uint16_t version;
    uint32_t frame_bytes;
    union {
        laplace_persistence_entity_record entity;
        laplace_persistence_physicality_record physicality;
        laplace_persistence_trajectory_record trajectory;
        laplace_persistence_occurrence_record occurrence;
    } value;
} laplace_persistence_record;

typedef struct laplace_persistence_summary {
    uint64_t entity_count;
    uint64_t physicality_count;
    uint64_t trajectory_vertex_count;
    uint64_t occurrence_count;
    uint64_t logical_occurrence_count;
    uint64_t frame_count;
    uint64_t byte_count;
} laplace_persistence_summary;

typedef enum laplace_persistence_status {
    LAPLACE_PERSISTENCE_OK = 0,
    LAPLACE_PERSISTENCE_INVALID_ARGUMENT = 1,
    LAPLACE_PERSISTENCE_CAPACITY_INSUFFICIENT = 2,
    LAPLACE_PERSISTENCE_FRAME_INVALID = 3,
    LAPLACE_PERSISTENCE_RECORD_INVALID = 4,
    LAPLACE_PERSISTENCE_IDENTITY_MISMATCH = 5,
    LAPLACE_PERSISTENCE_ORDER_INVALID = 6,
    LAPLACE_PERSISTENCE_TRAJECTORY_INVALID = 7,
    LAPLACE_PERSISTENCE_REFERENCE_INVALID = 8,
    LAPLACE_PERSISTENCE_OVERFLOW = 9
} laplace_persistence_status;

LAPLACE_API size_t laplace_persistence_frame_bytes(uint16_t kind);

LAPLACE_API laplace_persistence_status laplace_persistence_trajectory_fingerprint(
    const laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_persistence_status laplace_persistence_plan_sequence_fingerprint(
    const uint32_t* plan_ids,
    size_t plan_count,
    laplace_digest256* fingerprint);

LAPLACE_API laplace_persistence_status laplace_persistence_physicality_identify(
    const laplace_persistence_physicality_record* physicality,
    laplace_digest256* physicality_id);

LAPLACE_API laplace_persistence_status laplace_persistence_occurrence_identify(
    const laplace_persistence_occurrence_record* occurrence,
    laplace_digest256* occurrence_id);

LAPLACE_API laplace_persistence_status laplace_persistence_frame_encode_entity(
    const laplace_id128* entity,
    const laplace_digest256* identity_witness,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes);

LAPLACE_API laplace_persistence_status laplace_persistence_frame_encode_physicality(
    const laplace_persistence_physicality_record* physicality,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes);

LAPLACE_API laplace_persistence_status laplace_persistence_frame_encode_trajectory(
    const laplace_digest256* physicality_id,
    uint64_t vertex_index,
    const laplace_trajectory_carrier* carrier,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes);

LAPLACE_API laplace_persistence_status laplace_persistence_frame_encode_occurrence(
    const laplace_persistence_occurrence_record* occurrence,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes);

LAPLACE_API laplace_persistence_status laplace_persistence_frame_decode(
    const uint8_t* frame,
    size_t available_bytes,
    laplace_persistence_record* record,
    size_t* consumed_bytes);

LAPLACE_API laplace_persistence_status laplace_persistence_validate_stream(
    const laplace_framework_canonical_batch* batches,
    size_t batch_count,
    laplace_persistence_summary* summary);

#ifdef __cplusplus
}
#endif

#endif
