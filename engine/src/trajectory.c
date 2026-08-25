#include "laplace/trajectory.h"

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(double) == 8, "trajectory carrier requires binary64 width");
_Static_assert(FLT_RADIX == 2, "trajectory carrier requires radix two");
_Static_assert(DBL_MANT_DIG == 53, "trajectory carrier requires 53-bit precision");
_Static_assert(DBL_MAX_EXP == 1024, "trajectory carrier requires binary64 exponent range");
_Static_assert(sizeof(laplace_trajectory_carrier) == 32,
               "trajectory carrier ABI must be 32 bytes");

#define MANTISSA_MASK UINT64_C(0x000fffffffffffff)
#define SLOT_PAYLOAD_MASK UINT64_C(0x001fffffffffffff)
#define EXPONENT_MASK UINT64_C(0x7ff)
#define METADATA_MASK UINT64_C(0x000fffffffffffff)
#define LOW_11_MASK UINT64_C(0x7ff)
#define LOW_21_MASK UINT64_C(0x1fffff)
#define LOW_22_MASK UINT64_C(0x3fffff)
#define LOW_31_MASK UINT64_C(0x7fffffff)
#define LOW_42_MASK UINT64_C(0x3ffffffffff)

static uint64_t load_u64_le(const uint8_t bytes[8]) {
    uint64_t value = 0;
    size_t index;
    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void store_u64_le(uint64_t value, uint8_t bytes[8]) {
    size_t index;
    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static double payload_slot_to_double(uint64_t payload) {
    const uint64_t sign = (payload >> 52) & UINT64_C(1);
    const uint64_t mantissa = payload & MANTISSA_MASK;
    const uint64_t bits =
        (sign << 63) |
        (LAPLACE_TRAJECTORY_BINARY64_EXPONENT << 52) |
        mantissa;
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int double_to_payload_slot(double value, uint64_t* payload) {
    uint64_t bits;
    uint64_t exponent;
    memcpy(&bits, &value, sizeof(bits));
    exponent = (bits >> 52) & EXPONENT_MASK;
    if (exponent != LAPLACE_TRAJECTORY_BINARY64_EXPONENT || payload == NULL) {
        return 0;
    }
    *payload = (((bits >> 63) & UINT64_C(1)) << 52) |
               (bits & MANTISSA_MASK);
    return 1;
}

laplace_trajectory_status laplace_trajectory_carrier_encode(
    const laplace_trajectory_payload* payload,
    laplace_trajectory_carrier* carrier) {
    uint64_t high_lane;
    uint64_t low_lane;
    uint64_t slots[4];
    laplace_trajectory_carrier encoded;
    size_t index;

    if (payload == NULL || carrier == NULL) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    if ((payload->metadata & ~METADATA_MASK) != 0) {
        return LAPLACE_TRAJECTORY_METADATA_OUT_OF_RANGE;
    }

    high_lane = load_u64_le(payload->lane128);
    low_lane = load_u64_le(payload->lane128 + 8);
    slots[0] = low_lane & SLOT_PAYLOAD_MASK;
    slots[1] = ((low_lane >> 53) & LOW_11_MASK) |
               ((high_lane & LOW_42_MASK) << 11);
    slots[2] = ((high_lane >> 42) & LOW_22_MASK) |
               ((payload->metadata & LOW_31_MASK) << 22);
    slots[3] = (uint64_t)payload->ordinal |
               ((uint64_t)payload->run_length << 16) |
               (((payload->metadata >> 31) & LOW_21_MASK) << 32);

    for (index = 0; index < LAPLACE_TRAJECTORY_SLOT_COUNT; ++index) {
        encoded.slots[index] = payload_slot_to_double(slots[index]);
    }
    *carrier = encoded;
    return LAPLACE_TRAJECTORY_OK;
}

laplace_trajectory_status laplace_trajectory_carrier_decode(
    const laplace_trajectory_carrier* carrier,
    laplace_trajectory_payload* payload) {
    uint64_t slots[4];
    uint64_t high_lane;
    uint64_t low_lane;
    laplace_trajectory_payload decoded;
    size_t index;

    if (carrier == NULL || payload == NULL) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    for (index = 0; index < LAPLACE_TRAJECTORY_SLOT_COUNT; ++index) {
        if (!double_to_payload_slot(carrier->slots[index], &slots[index])) {
            return LAPLACE_TRAJECTORY_INVALID_CARRIER;
        }
    }

    low_lane = (slots[0] & SLOT_PAYLOAD_MASK) |
               ((slots[1] & LOW_11_MASK) << 53);
    high_lane = ((slots[1] >> 11) & LOW_42_MASK) |
                ((slots[2] & LOW_22_MASK) << 42);
    memset(&decoded, 0, sizeof(decoded));
    store_u64_le(high_lane, decoded.lane128);
    store_u64_le(low_lane, decoded.lane128 + 8);
    decoded.ordinal = (uint16_t)(slots[3] & UINT64_C(0xffff));
    decoded.run_length = (uint16_t)((slots[3] >> 16) & UINT64_C(0xffff));
    decoded.metadata = ((slots[2] >> 22) & LOW_31_MASK) |
                       (((slots[3] >> 32) & LOW_21_MASK) << 31);
    *payload = decoded;
    return LAPLACE_TRAJECTORY_OK;
}

laplace_trajectory_status laplace_trajectory_composition_encode(
    const laplace_id128* entity_id,
    uint64_t logical_ordinal,
    uint16_t run_length,
    uint64_t metadata,
    laplace_trajectory_carrier* carrier) {
    laplace_trajectory_payload payload;

    if (entity_id == NULL || carrier == NULL || logical_ordinal == 0) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    if (run_length == 0) {
        return LAPLACE_TRAJECTORY_ZERO_RUN;
    }
    if (UINT64_MAX - logical_ordinal < (uint64_t)run_length - 1u) {
        return LAPLACE_TRAJECTORY_COUNT_OVERFLOW;
    }
    memset(&payload, 0, sizeof(payload));
    memcpy(payload.lane128, entity_id->bytes, sizeof(payload.lane128));
    payload.ordinal = logical_ordinal <= UINT16_MAX
        ? (uint16_t)logical_ordinal
        : 0u;
    payload.run_length = run_length;
    payload.metadata = metadata;
    return laplace_trajectory_carrier_encode(&payload, carrier);
}

uint8_t laplace_trajectory_metadata_tier(uint64_t metadata) {
    return (uint8_t)((metadata >> LAPLACE_TRAJECTORY_TIER_SHIFT) &
                     LAPLACE_TRAJECTORY_TIER_MASK);
}

int laplace_trajectory_metadata_has_atom(uint64_t metadata) {
    return (metadata & (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT)) != 0;
}

uint32_t laplace_trajectory_metadata_atom(uint64_t metadata) {
    return (uint32_t)((metadata >> LAPLACE_TRAJECTORY_ATOM_SHIFT) &
                      LAPLACE_TRAJECTORY_ATOM_MASK);
}

static laplace_trajectory_status validate_composition(
    const laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    uint64_t* total) {
    uint64_t logical_ordinal = 1;
    size_t index;

    for (index = 0; index < carrier_count; ++index) {
        laplace_composition_occurrence occurrence;
        const laplace_trajectory_status status =
            laplace_trajectory_composition_decode_one(
                &carriers[index], logical_ordinal, &occurrence);
        if (status != LAPLACE_TRAJECTORY_OK) {
            return status;
        }
        if (UINT64_MAX - logical_ordinal < (uint64_t)occurrence.run_length) {
            return LAPLACE_TRAJECTORY_COUNT_OVERFLOW;
        }
        logical_ordinal += occurrence.run_length;
    }
    *total = logical_ordinal - 1u;
    return LAPLACE_TRAJECTORY_OK;
}

laplace_trajectory_status laplace_trajectory_composition_decode_one(
    const laplace_trajectory_carrier* carrier,
    uint64_t logical_ordinal,
    laplace_composition_occurrence* occurrence) {
    laplace_trajectory_payload payload;
    laplace_composition_occurrence decoded;
    uint16_t expected_packed;
    laplace_trajectory_status status;

    if (carrier == NULL || occurrence == NULL || logical_ordinal == 0) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    status = laplace_trajectory_carrier_decode(carrier, &payload);
    if (status != LAPLACE_TRAJECTORY_OK) {
        return status;
    }
    if (payload.run_length == 0) {
        return LAPLACE_TRAJECTORY_ZERO_RUN;
    }
    expected_packed = logical_ordinal <= UINT16_MAX
        ? (uint16_t)logical_ordinal
        : 0u;
    if (payload.ordinal != expected_packed) {
        return LAPLACE_TRAJECTORY_ORDINAL_MISMATCH;
    }
    if (UINT64_MAX - logical_ordinal < (uint64_t)payload.run_length - 1u) {
        return LAPLACE_TRAJECTORY_COUNT_OVERFLOW;
    }

    memset(&decoded, 0, sizeof(decoded));
    memcpy(decoded.entity_id.bytes, payload.lane128,
           sizeof(decoded.entity_id.bytes));
    decoded.logical_ordinal = logical_ordinal;
    decoded.metadata = payload.metadata;
    decoded.atom = laplace_trajectory_metadata_atom(payload.metadata);
    decoded.packed_ordinal = payload.ordinal;
    decoded.run_length = payload.run_length;
    decoded.tier = laplace_trajectory_metadata_tier(payload.metadata);
    decoded.has_atom = (uint8_t)laplace_trajectory_metadata_has_atom(
        payload.metadata);
    *occurrence = decoded;
    return LAPLACE_TRAJECTORY_OK;
}

laplace_trajectory_status laplace_trajectory_composition_decode(
    const laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_composition_occurrence* occurrences,
    size_t occurrence_capacity,
    uint64_t* logical_count) {
    uint64_t total;
    uint64_t logical_ordinal = 1;
    size_t index;
    laplace_trajectory_status status;

    if ((carriers == NULL && carrier_count != 0) ||
        (occurrences == NULL && carrier_count != 0) || logical_count == NULL) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    if (occurrence_capacity < carrier_count) {
        return LAPLACE_TRAJECTORY_CAPACITY_INSUFFICIENT;
    }
    status = validate_composition(carriers, carrier_count, &total);
    if (status != LAPLACE_TRAJECTORY_OK) {
        return status;
    }

    for (index = 0; index < carrier_count; ++index) {
        laplace_composition_occurrence occurrence;
        status = laplace_trajectory_composition_decode_one(
            &carriers[index], logical_ordinal, &occurrence);
        if (status != LAPLACE_TRAJECTORY_OK) {
            return status;
        }
        occurrences[index] = occurrence;
        logical_ordinal += occurrence.run_length;
    }
    *logical_count = total;
    return LAPLACE_TRAJECTORY_OK;
}

laplace_trajectory_status laplace_trajectory_entity_count(
    const laplace_composition_occurrence* occurrences,
    size_t occurrence_count,
    const laplace_id128* entity_id,
    uint64_t* logical_count) {
    uint64_t count = 0;
    size_t index;

    if ((occurrences == NULL && occurrence_count != 0) ||
        entity_id == NULL || logical_count == NULL) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    for (index = 0; index < occurrence_count; ++index) {
        if (laplace_identity_equal(&occurrences[index].entity_id, entity_id)) {
            if (UINT64_MAX - count < occurrences[index].run_length) {
                return LAPLACE_TRAJECTORY_COUNT_OVERFLOW;
            }
            count += occurrences[index].run_length;
        }
    }
    *logical_count = count;
    return LAPLACE_TRAJECTORY_OK;
}

laplace_trajectory_status laplace_trajectory_occurrence_precedes(
    const laplace_composition_occurrence* occurrences,
    size_t occurrence_count,
    size_t left_index,
    size_t right_index,
    int* result) {
    const laplace_composition_occurrence* left;
    const laplace_composition_occurrence* right;
    uint64_t left_end;
    if ((occurrences == NULL && occurrence_count != 0) || result == NULL ||
        left_index >= occurrence_count || right_index >= occurrence_count) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    left = &occurrences[left_index];
    right = &occurrences[right_index];
    if (left->run_length == 0 || right->run_length == 0) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    if (UINT64_MAX - left->logical_ordinal < (uint64_t)left->run_length - 1u) {
        return LAPLACE_TRAJECTORY_COUNT_OVERFLOW;
    }
    left_end = left->logical_ordinal + left->run_length - 1u;
    *result = left_end < right->logical_ordinal;
    return LAPLACE_TRAJECTORY_OK;
}

laplace_trajectory_status laplace_trajectory_entities_cooccur(
    const laplace_composition_occurrence* occurrences,
    size_t occurrence_count,
    const laplace_id128* left,
    const laplace_id128* right,
    int* result) {
    int found_left = 0;
    int found_right = 0;
    size_t index;
    if ((occurrences == NULL && occurrence_count != 0) || left == NULL ||
        right == NULL || result == NULL) {
        return LAPLACE_TRAJECTORY_INVALID_ARGUMENT;
    }
    for (index = 0; index < occurrence_count; ++index) {
        found_left = found_left ||
            laplace_identity_equal(&occurrences[index].entity_id, left);
        found_right = found_right ||
            laplace_identity_equal(&occurrences[index].entity_id, right);
    }
    *result = found_left && found_right;
    return LAPLACE_TRAJECTORY_OK;
}
