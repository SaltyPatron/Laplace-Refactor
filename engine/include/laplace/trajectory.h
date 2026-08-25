#ifndef LAPLACE_TRAJECTORY_H
#define LAPLACE_TRAJECTORY_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/trajectory.h"
#include "laplace/export.h"
#include "laplace/identity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_trajectory_carrier {
    double slots[LAPLACE_TRAJECTORY_SLOT_COUNT];
} laplace_trajectory_carrier;

typedef struct laplace_trajectory_payload {
    uint8_t lane128[LAPLACE_IDENTITY_BYTES];
    uint16_t ordinal;
    uint16_t run_length;
    uint64_t metadata;
} laplace_trajectory_payload;

typedef struct laplace_composition_occurrence {
    laplace_id128 entity_id;
    uint64_t logical_ordinal;
    uint64_t metadata;
    uint32_t atom;
    uint16_t packed_ordinal;
    uint16_t run_length;
    uint8_t tier;
    uint8_t has_atom;
    uint16_t reserved;
} laplace_composition_occurrence;

typedef enum laplace_trajectory_status {
    LAPLACE_TRAJECTORY_OK = 0,
    LAPLACE_TRAJECTORY_INVALID_ARGUMENT = 1,
    LAPLACE_TRAJECTORY_METADATA_OUT_OF_RANGE = 2,
    LAPLACE_TRAJECTORY_INVALID_CARRIER = 3,
    LAPLACE_TRAJECTORY_ZERO_RUN = 4,
    LAPLACE_TRAJECTORY_ORDINAL_MISMATCH = 5,
    LAPLACE_TRAJECTORY_CAPACITY_INSUFFICIENT = 6,
    LAPLACE_TRAJECTORY_COUNT_OVERFLOW = 7
} laplace_trajectory_status;

LAPLACE_API laplace_trajectory_status laplace_trajectory_carrier_encode(
    const laplace_trajectory_payload* payload,
    laplace_trajectory_carrier* carrier);

LAPLACE_API laplace_trajectory_status laplace_trajectory_carrier_decode(
    const laplace_trajectory_carrier* carrier,
    laplace_trajectory_payload* payload);

LAPLACE_API laplace_trajectory_status laplace_trajectory_composition_encode(
    const laplace_id128* entity_id,
    uint64_t logical_ordinal,
    uint16_t run_length,
    uint64_t metadata,
    laplace_trajectory_carrier* carrier);

LAPLACE_API laplace_trajectory_status laplace_trajectory_composition_decode(
    const laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_composition_occurrence* occurrences,
    size_t occurrence_capacity,
    uint64_t* logical_count);

LAPLACE_API laplace_trajectory_status laplace_trajectory_composition_decode_one(
    const laplace_trajectory_carrier* carrier,
    uint64_t logical_ordinal,
    laplace_composition_occurrence* occurrence);

LAPLACE_API uint8_t laplace_trajectory_metadata_tier(uint64_t metadata);
LAPLACE_API int laplace_trajectory_metadata_has_atom(uint64_t metadata);
LAPLACE_API uint32_t laplace_trajectory_metadata_atom(uint64_t metadata);

LAPLACE_API laplace_trajectory_status laplace_trajectory_entity_count(
    const laplace_composition_occurrence* occurrences,
    size_t occurrence_count,
    const laplace_id128* entity_id,
    uint64_t* logical_count);

LAPLACE_API laplace_trajectory_status laplace_trajectory_occurrence_precedes(
    const laplace_composition_occurrence* occurrences,
    size_t occurrence_count,
    size_t left_index,
    size_t right_index,
    int* result);

LAPLACE_API laplace_trajectory_status laplace_trajectory_entities_cooccur(
    const laplace_composition_occurrence* occurrences,
    size_t occurrence_count,
    const laplace_id128* left,
    const laplace_id128* right,
    int* result);

#ifdef __cplusplus
}
#endif

#endif
