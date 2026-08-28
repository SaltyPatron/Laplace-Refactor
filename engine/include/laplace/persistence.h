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

typedef struct laplace_persistence_trajectory_segment_record {
    laplace_digest256 physicality_id;
    uint64_t vertex_index;
    laplace_trajectory_carrier carrier;
    laplace_composition_occurrence occurrence;
} laplace_persistence_trajectory_segment_record;

typedef struct laplace_persistence_attestation_record {
    laplace_digest256 attestation_id;
    laplace_id128 entity_id;
    laplace_digest256 physicality_id;
    laplace_digest256 source_fingerprint;
    laplace_digest256 context_fingerprint;
    uint64_t source_ordinal;
    uint32_t flags;
    uint32_t attestation_kind;
} laplace_persistence_attestation_record;

typedef struct laplace_persistence_consensus_record {
    laplace_digest256 consensus_id;
    laplace_id128 proposition_entity_id;
    laplace_digest256 epoch_id;
    laplace_digest256 evidence_boundary;
    laplace_digest256 recipe_fingerprint;
    uint64_t observation_count;
    uint64_t independent_root_count;
    uint32_t disposition;
    uint32_t flags;
    double standing;
} laplace_persistence_consensus_record;

typedef struct laplace_persistence_record {
    uint16_t kind;
    uint16_t version;
    uint32_t frame_bytes;
    union {
        laplace_persistence_entity_record entity;
        laplace_persistence_physicality_record physicality;
        laplace_persistence_trajectory_segment_record trajectory_segment;
        laplace_persistence_attestation_record attestation;
        laplace_persistence_consensus_record consensus;
    } value;
} laplace_persistence_record;

typedef struct laplace_persistence_summary {
    uint64_t entity_count;
    uint64_t physicality_count;
    uint64_t trajectory_segment_count;
    uint64_t attestation_count;
    uint64_t consensus_count;
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

LAPLACE_API laplace_persistence_status
laplace_persistence_atomic_point_physicality(
    const laplace_id128* entity_id,
    uint32_t recipe_version,
    const laplace_digest256* recipe_fingerprint,
    const laplace_digest256* geometry_epoch,
    const laplace_point4d* point,
    laplace_persistence_physicality_record* physicality);

LAPLACE_API laplace_persistence_status laplace_persistence_attestation_identify(
    const laplace_persistence_attestation_record* attestation,
    laplace_digest256* attestation_id);

LAPLACE_API laplace_persistence_status laplace_persistence_consensus_identify(
    const laplace_persistence_consensus_record* consensus,
    laplace_digest256* consensus_id);

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

LAPLACE_API laplace_persistence_status
laplace_persistence_frame_encode_trajectory_segment(
    const laplace_digest256* physicality_id,
    uint64_t vertex_index,
    const laplace_trajectory_carrier* carrier,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes);

LAPLACE_API laplace_persistence_status laplace_persistence_frame_encode_attestation(
    const laplace_persistence_attestation_record* attestation,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes);

LAPLACE_API laplace_persistence_status laplace_persistence_frame_encode_consensus(
    const laplace_persistence_consensus_record* consensus,
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
