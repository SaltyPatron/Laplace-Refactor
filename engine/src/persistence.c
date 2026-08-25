#include "laplace/persistence.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake3.h"

static const uint8_t PHYSICALITY_DOMAIN[] = LAPLACE_PERSISTENCE_PHYSICALITY_DOMAIN;
static const uint8_t TRAJECTORY_DOMAIN[] = LAPLACE_PERSISTENCE_TRAJECTORY_DOMAIN;
static const uint8_t OCCURRENCE_DOMAIN[] = LAPLACE_PERSISTENCE_OCCURRENCE_DOMAIN;
static const uint8_t PLAN_SEQUENCE_DOMAIN[] =
    LAPLACE_PERSISTENCE_PG_PLAN_FINGERPRINT_DOMAIN;
static const uint8_t PLAN_MANIFEST[] = LAPLACE_PERSISTENCE_PG_PLAN_MANIFEST;

enum {
    FRAME_HEADER_BYTES = 8,
    PHYSICALITY_IDENTITY_INPUT_OFFSET = 32,
    OCCURRENCE_IDENTITY_INPUT_OFFSET = 32
};

static int bytes_all_zero(const uint8_t* bytes, size_t length) {
    uint8_t aggregate = 0;
    size_t index;
    for (index = 0; index < length; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0;
}

static int reject_zero_identity_field(const uint8_t* bytes, size_t length) {
#if defined(LAPLACE_TEST_REJECT_ZERO_IDENTITY_FIELDS)
    return bytes_all_zero(bytes, length);
#else
    (void)bytes;
    (void)length;
    return 0;
#endif
}

static int reject_zero_digest_field(const uint8_t* bytes, size_t length) {
#if defined(LAPLACE_TEST_REJECT_ZERO_DIGEST_FIELDS)
    return bytes_all_zero(bytes, length);
#else
    (void)bytes;
    (void)length;
    return 0;
#endif
}

static int digest_equal(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static uint16_t load_u16(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t load_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t load_u64(const uint8_t* bytes) {
    uint64_t value = 0;
    size_t index;
    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void store_u16(uint16_t value, uint8_t* bytes) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void store_u32(uint32_t value, uint8_t* bytes) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void store_u64(uint64_t value, uint8_t* bytes) {
    size_t index;
    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static void store_double(double value, uint8_t* bytes) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    store_u64(bits, bytes);
}

static double load_double(const uint8_t* bytes) {
    const uint64_t bits = load_u64(bytes);
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void finish_digest(blake3_hasher* hasher, laplace_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

size_t laplace_persistence_frame_bytes(uint16_t kind) {
    switch (kind) {
        case LAPLACE_PERSISTENCE_RECORD_ENTITY:
            return FRAME_HEADER_BYTES + LAPLACE_PERSISTENCE_ENTITY_PAYLOAD_BYTES;
        case LAPLACE_PERSISTENCE_RECORD_PHYSICALITY:
            return FRAME_HEADER_BYTES + LAPLACE_PERSISTENCE_PHYSICALITY_PAYLOAD_BYTES;
        case LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX:
            return FRAME_HEADER_BYTES + LAPLACE_PERSISTENCE_TRAJECTORY_VERTEX_PAYLOAD_BYTES;
        case LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE:
            return FRAME_HEADER_BYTES + LAPLACE_PERSISTENCE_OBSERVED_OCCURRENCE_PAYLOAD_BYTES;
        default:
            return 0;
    }
}

static laplace_persistence_status frame_header(
    uint16_t kind,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes) {
    const size_t frame_bytes = laplace_persistence_frame_bytes(kind);
    if (output == NULL || output_bytes == NULL || frame_bytes == 0) {
        return LAPLACE_PERSISTENCE_INVALID_ARGUMENT;
    }
    if (output_capacity < frame_bytes) {
        return LAPLACE_PERSISTENCE_CAPACITY_INSUFFICIENT;
    }
    memset(output, 0, frame_bytes);
    store_u32((uint32_t)frame_bytes, output);
    store_u16(kind, output + 4);
    store_u16(LAPLACE_PERSISTENCE_FRAME_VERSION, output + 6);
    *output_bytes = frame_bytes;
    return LAPLACE_PERSISTENCE_OK;
}

static void carrier_store(
    const laplace_trajectory_carrier* carrier,
    uint8_t bytes[32]) {
    size_t slot;
    for (slot = 0; slot < LAPLACE_TRAJECTORY_SLOT_COUNT; ++slot) {
        store_double(carrier->slots[slot], bytes + slot * 8u);
    }
}

static void carrier_load(
    const uint8_t bytes[32],
    laplace_trajectory_carrier* carrier) {
    size_t slot;
    for (slot = 0; slot < LAPLACE_TRAJECTORY_SLOT_COUNT; ++slot) {
        carrier->slots[slot] = load_double(bytes + slot * 8u);
    }
}

laplace_persistence_status laplace_persistence_trajectory_fingerprint(
    const laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_digest256* fingerprint) {
    blake3_hasher hasher;
    uint8_t bytes[32];
    size_t index;
    if (carriers == NULL || carrier_count == 0 || fingerprint == NULL) {
        return LAPLACE_PERSISTENCE_INVALID_ARGUMENT;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, TRAJECTORY_DOMAIN, sizeof(TRAJECTORY_DOMAIN) - 1u);
    for (index = 0; index < carrier_count; ++index) {
        carrier_store(&carriers[index], bytes);
        blake3_hasher_update(&hasher, bytes, sizeof(bytes));
    }
    finish_digest(&hasher, fingerprint);
    return LAPLACE_PERSISTENCE_OK;
}

laplace_persistence_status laplace_persistence_plan_sequence_fingerprint(
    const uint32_t* plan_ids,
    size_t plan_count,
    laplace_digest256* fingerprint) {
    blake3_hasher hasher;
    size_t index;
    uint8_t encoded[4];
    static const uint32_t expected_plan_ids[LAPLACE_PERSISTENCE_PG_PLAN_COUNT] = {
        LAPLACE_PERSISTENCE_PG_PLAN_REFERENCE_PREFLIGHT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_VERIFY};
    if (plan_ids == NULL || plan_count != LAPLACE_PERSISTENCE_PG_PLAN_COUNT ||
        fingerprint == NULL) {
        return LAPLACE_PERSISTENCE_INVALID_ARGUMENT;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, PLAN_SEQUENCE_DOMAIN, sizeof(PLAN_SEQUENCE_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, PLAN_MANIFEST, sizeof(PLAN_MANIFEST) - 1u);
    for (index = 0; index < plan_count; ++index) {
        if (plan_ids[index] != expected_plan_ids[index]) {
            return LAPLACE_PERSISTENCE_RECORD_INVALID;
        }
        store_u32(plan_ids[index], encoded);
        blake3_hasher_update(&hasher, encoded, sizeof(encoded));
    }
    finish_digest(&hasher, fingerprint);
    return LAPLACE_PERSISTENCE_OK;
}

static int physicality_scalars_valid(
    const laplace_persistence_physicality_record* physicality) {
    size_t index;
    if (physicality->physicality_type != LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION ||
        physicality->vertex_class != LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER ||
        physicality->recipe_version == 0 ||
        physicality->dimension_count != LAPLACE_GEOMETRY_COMPONENTS ||
        physicality->structural_form != LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION ||
        physicality->flags != LAPLACE_PERSISTENCE_PHYSICALITY_FLAGS_NONE ||
        physicality->logical_count == 0 ||
        physicality->vertex_count == 0 ||
        reject_zero_digest_field(
            physicality->entity_id.bytes, sizeof(physicality->entity_id.bytes)) ||
        reject_zero_digest_field(
            physicality->recipe_fingerprint.bytes,
            sizeof(physicality->recipe_fingerprint.bytes)) ||
        reject_zero_digest_field(
            physicality->geometry_epoch.bytes,
            sizeof(physicality->geometry_epoch.bytes)) ||
        reject_zero_digest_field(
            physicality->trajectory_fingerprint.bytes,
            sizeof(physicality->trajectory_fingerprint.bytes)) ||
        !isfinite(physicality->radius) || physicality->radius < 0.0 ||
        physicality->radius > 1.0) {
        return 0;
    }
    for (index = 0; index < LAPLACE_GEOMETRY_COMPONENTS; ++index) {
        const double value = physicality->centroid.component[index];
        if (!isfinite(value) || value < -1.0 || value > 1.0) {
            return 0;
        }
    }
    return 1;
}

static void physicality_payload(
    const laplace_persistence_physicality_record* physicality,
    uint8_t payload[LAPLACE_PERSISTENCE_PHYSICALITY_PAYLOAD_BYTES]) {
    size_t offset = 0;
    size_t index;
    memset(payload, 0, LAPLACE_PERSISTENCE_PHYSICALITY_PAYLOAD_BYTES);
    memcpy(payload + offset, physicality->physicality_id.bytes, 32); offset += 32;
    memcpy(payload + offset, physicality->entity_id.bytes, 16); offset += 16;
    store_u32(physicality->physicality_type, payload + offset); offset += 4;
    store_u32(physicality->vertex_class, payload + offset); offset += 4;
    store_u32(physicality->recipe_version, payload + offset); offset += 4;
    store_u32(physicality->structural_form, payload + offset); offset += 4;
    store_u32(physicality->dimension_count, payload + offset); offset += 4;
    store_u32(physicality->flags, payload + offset); offset += 4;
    memcpy(payload + offset, physicality->recipe_fingerprint.bytes, 32); offset += 32;
    memcpy(payload + offset, physicality->geometry_epoch.bytes, 32); offset += 32;
    memcpy(payload + offset, physicality->trajectory_fingerprint.bytes, 32); offset += 32;
    for (index = 0; index < LAPLACE_GEOMETRY_COMPONENTS; ++index) {
        store_double(physicality->centroid.component[index], payload + offset);
        offset += 8;
    }
    store_double(physicality->radius, payload + offset); offset += 8;
    store_u64(physicality->logical_count, payload + offset); offset += 8;
    store_u64(physicality->vertex_count, payload + offset); offset += 8;
    (void)offset;
}

laplace_persistence_status laplace_persistence_physicality_identify(
    const laplace_persistence_physicality_record* physicality,
    laplace_digest256* physicality_id) {
    uint8_t payload[LAPLACE_PERSISTENCE_PHYSICALITY_PAYLOAD_BYTES];
    blake3_hasher hasher;
    if (physicality == NULL || physicality_id == NULL ||
        !physicality_scalars_valid(physicality)) {
        return LAPLACE_PERSISTENCE_RECORD_INVALID;
    }
    physicality_payload(physicality, payload);
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, PHYSICALITY_DOMAIN, sizeof(PHYSICALITY_DOMAIN) - 1u);
    blake3_hasher_update(
        &hasher, payload + PHYSICALITY_IDENTITY_INPUT_OFFSET,
        sizeof(payload) - PHYSICALITY_IDENTITY_INPUT_OFFSET);
    finish_digest(&hasher, physicality_id);
    return LAPLACE_PERSISTENCE_OK;
}

static int occurrence_scalars_valid(
    const laplace_persistence_occurrence_record* occurrence) {
    const uint32_t known_flags = LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY;
    const int has_physicality =
        (occurrence->flags & LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY) != 0;
    if ((occurrence->flags & ~known_flags) != 0 || occurrence->reserved != 0 ||
        occurrence->source_ordinal == 0 ||
        reject_zero_digest_field(
            occurrence->entity_id.bytes, sizeof(occurrence->entity_id.bytes)) ||
        reject_zero_digest_field(
            occurrence->source_fingerprint.bytes,
            sizeof(occurrence->source_fingerprint.bytes)) ||
        reject_zero_digest_field(
            occurrence->context_fingerprint.bytes,
            sizeof(occurrence->context_fingerprint.bytes)) ||
        (has_physicality && reject_zero_digest_field(
            occurrence->physicality_id.bytes,
            sizeof(occurrence->physicality_id.bytes))) ||
        (!has_physicality && !bytes_all_zero(
            occurrence->physicality_id.bytes,
            sizeof(occurrence->physicality_id.bytes)))) {
        return 0;
    }
    return 1;
}

static void occurrence_payload(
    const laplace_persistence_occurrence_record* occurrence,
    uint8_t payload[LAPLACE_PERSISTENCE_OBSERVED_OCCURRENCE_PAYLOAD_BYTES]) {
    size_t offset = 0;
    memset(payload, 0, LAPLACE_PERSISTENCE_OBSERVED_OCCURRENCE_PAYLOAD_BYTES);
    memcpy(payload + offset, occurrence->occurrence_id.bytes, 32); offset += 32;
    memcpy(payload + offset, occurrence->entity_id.bytes, 16); offset += 16;
    memcpy(payload + offset, occurrence->physicality_id.bytes, 32); offset += 32;
    memcpy(payload + offset, occurrence->source_fingerprint.bytes, 32); offset += 32;
    memcpy(payload + offset, occurrence->context_fingerprint.bytes, 32); offset += 32;
    store_u64(occurrence->source_ordinal, payload + offset); offset += 8;
    store_u32(occurrence->flags, payload + offset); offset += 4;
    store_u32(occurrence->reserved, payload + offset); offset += 4;
    (void)offset;
}

laplace_persistence_status laplace_persistence_occurrence_identify(
    const laplace_persistence_occurrence_record* occurrence,
    laplace_digest256* occurrence_id) {
    uint8_t payload[LAPLACE_PERSISTENCE_OBSERVED_OCCURRENCE_PAYLOAD_BYTES];
    blake3_hasher hasher;
    if (occurrence == NULL || occurrence_id == NULL ||
        !occurrence_scalars_valid(occurrence)) {
        return LAPLACE_PERSISTENCE_RECORD_INVALID;
    }
    occurrence_payload(occurrence, payload);
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, OCCURRENCE_DOMAIN, sizeof(OCCURRENCE_DOMAIN) - 1u);
    blake3_hasher_update(
        &hasher, payload + OCCURRENCE_IDENTITY_INPUT_OFFSET,
        sizeof(payload) - OCCURRENCE_IDENTITY_INPUT_OFFSET);
    finish_digest(&hasher, occurrence_id);
    return LAPLACE_PERSISTENCE_OK;
}

laplace_persistence_status laplace_persistence_frame_encode_entity(
    const laplace_id128* entity,
    const laplace_digest256* identity_witness,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes) {
    laplace_persistence_status status;
    if (entity == NULL || identity_witness == NULL ||
        reject_zero_identity_field(entity->bytes, sizeof(entity->bytes)) ||
        reject_zero_identity_field(
            identity_witness->bytes, sizeof(identity_witness->bytes)) ||
        memcmp(entity->bytes, identity_witness->bytes, sizeof(entity->bytes)) != 0) {
        return LAPLACE_PERSISTENCE_RECORD_INVALID;
    }
    status = frame_header(
        LAPLACE_PERSISTENCE_RECORD_ENTITY, output, output_capacity, output_bytes);
    if (status == LAPLACE_PERSISTENCE_OK) {
        memcpy(output + FRAME_HEADER_BYTES, entity->bytes, sizeof(entity->bytes));
        memcpy(output + FRAME_HEADER_BYTES + sizeof(entity->bytes),
               identity_witness->bytes, sizeof(identity_witness->bytes));
    }
    return status;
}

laplace_persistence_status laplace_persistence_frame_encode_physicality(
    const laplace_persistence_physicality_record* physicality,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes) {
    laplace_digest256 expected;
    laplace_persistence_status status;
    if (physicality == NULL ||
        laplace_persistence_physicality_identify(physicality, &expected) !=
            LAPLACE_PERSISTENCE_OK ||
        !digest_equal(&expected, &physicality->physicality_id)) {
        return LAPLACE_PERSISTENCE_IDENTITY_MISMATCH;
    }
    status = frame_header(
        LAPLACE_PERSISTENCE_RECORD_PHYSICALITY, output, output_capacity, output_bytes);
    if (status == LAPLACE_PERSISTENCE_OK) {
        physicality_payload(physicality, output + FRAME_HEADER_BYTES);
    }
    return status;
}

laplace_persistence_status laplace_persistence_frame_encode_trajectory(
    const laplace_digest256* physicality_id,
    uint64_t vertex_index,
    const laplace_trajectory_carrier* carrier,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes) {
    laplace_trajectory_payload payload;
    laplace_persistence_status status;
    uint8_t* record_payload;
    if (physicality_id == NULL || carrier == NULL ||
        reject_zero_digest_field(
            physicality_id->bytes, sizeof(physicality_id->bytes)) ||
        laplace_trajectory_carrier_decode(carrier, &payload) != LAPLACE_TRAJECTORY_OK) {
        return LAPLACE_PERSISTENCE_TRAJECTORY_INVALID;
    }
    status = frame_header(
        LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX,
        output, output_capacity, output_bytes);
    if (status != LAPLACE_PERSISTENCE_OK) {
        return status;
    }
    record_payload = output + FRAME_HEADER_BYTES;
    memcpy(record_payload, physicality_id->bytes, 32);
    store_u64(vertex_index, record_payload + 32);
    carrier_store(carrier, record_payload + 40);
    return LAPLACE_PERSISTENCE_OK;
}

laplace_persistence_status laplace_persistence_frame_encode_occurrence(
    const laplace_persistence_occurrence_record* occurrence,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes) {
    laplace_digest256 expected;
    laplace_persistence_status status;
    if (occurrence == NULL ||
        laplace_persistence_occurrence_identify(occurrence, &expected) !=
            LAPLACE_PERSISTENCE_OK ||
        !digest_equal(&expected, &occurrence->occurrence_id)) {
        return LAPLACE_PERSISTENCE_IDENTITY_MISMATCH;
    }
    status = frame_header(
        LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE,
        output, output_capacity, output_bytes);
    if (status == LAPLACE_PERSISTENCE_OK) {
        occurrence_payload(occurrence, output + FRAME_HEADER_BYTES);
    }
    return status;
}

static laplace_persistence_status decode_physicality(
    const uint8_t* payload,
    laplace_persistence_physicality_record* physicality) {
    laplace_persistence_physicality_record decoded;
    laplace_digest256 expected;
    size_t offset = 0;
    size_t index;
    memset(&decoded, 0, sizeof(decoded));
    memcpy(decoded.physicality_id.bytes, payload + offset, 32); offset += 32;
    memcpy(decoded.entity_id.bytes, payload + offset, 16); offset += 16;
    decoded.physicality_type = load_u32(payload + offset); offset += 4;
    decoded.vertex_class = load_u32(payload + offset); offset += 4;
    decoded.recipe_version = load_u32(payload + offset); offset += 4;
    decoded.structural_form = load_u32(payload + offset); offset += 4;
    decoded.dimension_count = load_u32(payload + offset); offset += 4;
    decoded.flags = load_u32(payload + offset); offset += 4;
    memcpy(decoded.recipe_fingerprint.bytes, payload + offset, 32); offset += 32;
    memcpy(decoded.geometry_epoch.bytes, payload + offset, 32); offset += 32;
    memcpy(decoded.trajectory_fingerprint.bytes, payload + offset, 32); offset += 32;
    for (index = 0; index < LAPLACE_GEOMETRY_COMPONENTS; ++index) {
        decoded.centroid.component[index] = load_double(payload + offset);
        offset += 8;
    }
    decoded.radius = load_double(payload + offset); offset += 8;
    decoded.logical_count = load_u64(payload + offset); offset += 8;
    decoded.vertex_count = load_u64(payload + offset); offset += 8;
    if (offset != LAPLACE_PERSISTENCE_PHYSICALITY_PAYLOAD_BYTES ||
        laplace_persistence_physicality_identify(&decoded, &expected) !=
            LAPLACE_PERSISTENCE_OK ||
        !digest_equal(&decoded.physicality_id, &expected)) {
        return LAPLACE_PERSISTENCE_IDENTITY_MISMATCH;
    }
    *physicality = decoded;
    return LAPLACE_PERSISTENCE_OK;
}

static laplace_persistence_status decode_occurrence(
    const uint8_t* payload,
    laplace_persistence_occurrence_record* occurrence) {
    laplace_persistence_occurrence_record decoded;
    laplace_digest256 expected;
    size_t offset = 0;
    memset(&decoded, 0, sizeof(decoded));
    memcpy(decoded.occurrence_id.bytes, payload + offset, 32); offset += 32;
    memcpy(decoded.entity_id.bytes, payload + offset, 16); offset += 16;
    memcpy(decoded.physicality_id.bytes, payload + offset, 32); offset += 32;
    memcpy(decoded.source_fingerprint.bytes, payload + offset, 32); offset += 32;
    memcpy(decoded.context_fingerprint.bytes, payload + offset, 32); offset += 32;
    decoded.source_ordinal = load_u64(payload + offset); offset += 8;
    decoded.flags = load_u32(payload + offset); offset += 4;
    decoded.reserved = load_u32(payload + offset); offset += 4;
    if (offset != LAPLACE_PERSISTENCE_OBSERVED_OCCURRENCE_PAYLOAD_BYTES ||
        laplace_persistence_occurrence_identify(&decoded, &expected) !=
            LAPLACE_PERSISTENCE_OK ||
        !digest_equal(&decoded.occurrence_id, &expected)) {
        return LAPLACE_PERSISTENCE_IDENTITY_MISMATCH;
    }
    *occurrence = decoded;
    return LAPLACE_PERSISTENCE_OK;
}

laplace_persistence_status laplace_persistence_frame_decode(
    const uint8_t* frame,
    size_t available_bytes,
    laplace_persistence_record* record,
    size_t* consumed_bytes) {
    laplace_persistence_record decoded;
    const uint8_t* payload;
    uint32_t frame_bytes;
    uint16_t kind;
    uint16_t version;
    size_t expected_bytes;
    laplace_persistence_status status = LAPLACE_PERSISTENCE_OK;
    if (frame == NULL || record == NULL || consumed_bytes == NULL ||
        available_bytes < FRAME_HEADER_BYTES) {
        return LAPLACE_PERSISTENCE_INVALID_ARGUMENT;
    }
    frame_bytes = load_u32(frame);
    kind = load_u16(frame + 4);
    version = load_u16(frame + 6);
    expected_bytes = laplace_persistence_frame_bytes(kind);
    if (expected_bytes == 0 || frame_bytes != expected_bytes ||
        frame_bytes > available_bytes ||
        version != LAPLACE_PERSISTENCE_FRAME_VERSION) {
        return LAPLACE_PERSISTENCE_FRAME_INVALID;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.kind = kind;
    decoded.version = version;
    decoded.frame_bytes = frame_bytes;
    payload = frame + FRAME_HEADER_BYTES;
    switch (kind) {
        case LAPLACE_PERSISTENCE_RECORD_ENTITY:
            memcpy(decoded.value.entity.entity_id.bytes, payload, 16);
            memcpy(decoded.value.entity.identity_witness.bytes, payload + 16, 32);
            if (reject_zero_identity_field(
                    decoded.value.entity.entity_id.bytes, 16) ||
                reject_zero_identity_field(
                    decoded.value.entity.identity_witness.bytes, 32) ||
                memcmp(decoded.value.entity.entity_id.bytes,
                       decoded.value.entity.identity_witness.bytes, 16) != 0) {
                status = LAPLACE_PERSISTENCE_RECORD_INVALID;
            }
            break;
        case LAPLACE_PERSISTENCE_RECORD_PHYSICALITY:
            status = decode_physicality(payload, &decoded.value.physicality);
            break;
        case LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX:
            memcpy(decoded.value.trajectory.physicality_id.bytes, payload, 32);
            decoded.value.trajectory.vertex_index = load_u64(payload + 32);
            carrier_load(payload + 40, &decoded.value.trajectory.carrier);
            if (reject_zero_digest_field(
                    decoded.value.trajectory.physicality_id.bytes, 32)) {
                status = LAPLACE_PERSISTENCE_REFERENCE_INVALID;
            } else {
                laplace_trajectory_payload trajectory_payload;
                if (laplace_trajectory_carrier_decode(
                        &decoded.value.trajectory.carrier, &trajectory_payload) !=
                    LAPLACE_TRAJECTORY_OK) {
                    status = LAPLACE_PERSISTENCE_TRAJECTORY_INVALID;
                }
            }
            break;
        case LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE:
            status = decode_occurrence(payload, &decoded.value.occurrence);
            break;
        default:
            status = LAPLACE_PERSISTENCE_FRAME_INVALID;
            break;
    }
    if (status != LAPLACE_PERSISTENCE_OK) {
        return status;
    }
    *record = decoded;
    *consumed_bytes = frame_bytes;
    return LAPLACE_PERSISTENCE_OK;
}

typedef struct validation_state {
    laplace_persistence_summary summary;
    uint16_t phase;
    int have_entity;
    int have_physicality;
    int have_occurrence;
    laplace_id128 previous_entity;
    laplace_digest256 previous_physicality;
    laplace_digest256 previous_occurrence;
    laplace_persistence_physicality_record current_physicality;
    uint64_t current_vertices;
    uint64_t current_logical_count;
    blake3_hasher trajectory_hasher;
    int current_open;
} validation_state;

static laplace_persistence_status close_physicality(validation_state* state) {
    laplace_digest256 trajectory;
    if (!state->current_open) {
        return LAPLACE_PERSISTENCE_OK;
    }
    finish_digest(&state->trajectory_hasher, &trajectory);
    if (state->current_vertices != state->current_physicality.vertex_count ||
        state->current_logical_count != state->current_physicality.logical_count ||
        !digest_equal(&trajectory, &state->current_physicality.trajectory_fingerprint)) {
        return LAPLACE_PERSISTENCE_TRAJECTORY_INVALID;
    }
    state->current_open = 0;
    return LAPLACE_PERSISTENCE_OK;
}

static laplace_persistence_status validate_record(
    validation_state* state,
    laplace_persistence_record* record,
    const uint8_t* frame) {
    int comparison;
    switch (record->kind) {
        case LAPLACE_PERSISTENCE_RECORD_ENTITY:
            if (state->phase > LAPLACE_PERSISTENCE_RECORD_ENTITY) {
                return LAPLACE_PERSISTENCE_ORDER_INVALID;
            }
            comparison = state->have_entity
                ? memcmp(state->previous_entity.bytes,
                         record->value.entity.entity_id.bytes, 16)
                : -1;
            if (comparison >= 0) {
                return LAPLACE_PERSISTENCE_ORDER_INVALID;
            }
            state->previous_entity = record->value.entity.entity_id;
            state->have_entity = 1;
            state->phase = LAPLACE_PERSISTENCE_RECORD_ENTITY;
            state->summary.entity_count += 1u;
            break;
        case LAPLACE_PERSISTENCE_RECORD_PHYSICALITY:
            if (state->phase == LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE) {
                return LAPLACE_PERSISTENCE_ORDER_INVALID;
            }
            {
                const laplace_persistence_status close_status = close_physicality(state);
                if (close_status != LAPLACE_PERSISTENCE_OK) {
                    return close_status;
                }
            }
            comparison = state->have_physicality
                ? memcmp(state->previous_physicality.bytes,
                         record->value.physicality.physicality_id.bytes, 32)
                : -1;
            if (comparison >= 0) {
                return LAPLACE_PERSISTENCE_ORDER_INVALID;
            }
            state->previous_physicality = record->value.physicality.physicality_id;
            state->have_physicality = 1;
            state->phase = LAPLACE_PERSISTENCE_RECORD_PHYSICALITY;
            state->current_physicality = record->value.physicality;
            state->current_vertices = 0;
            state->current_logical_count = 0;
            state->current_open = 1;
            blake3_hasher_init(&state->trajectory_hasher);
            blake3_hasher_update(
                &state->trajectory_hasher, TRAJECTORY_DOMAIN,
                sizeof(TRAJECTORY_DOMAIN) - 1u);
            state->summary.physicality_count += 1u;
            break;
        case LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX: {
            laplace_composition_occurrence occurrence;
            const uint64_t logical_ordinal = state->current_logical_count + 1u;
            if (!state->current_open ||
                !digest_equal(&state->current_physicality.physicality_id,
                              &record->value.trajectory.physicality_id) ||
                record->value.trajectory.vertex_index != state->current_vertices ||
                laplace_trajectory_composition_decode_one(
                    &record->value.trajectory.carrier, logical_ordinal, &occurrence) !=
                    LAPLACE_TRAJECTORY_OK ||
                UINT64_MAX - state->current_logical_count < occurrence.run_length) {
                return LAPLACE_PERSISTENCE_TRAJECTORY_INVALID;
            }
            record->value.trajectory.occurrence = occurrence;
            blake3_hasher_update(
                &state->trajectory_hasher,
                frame + FRAME_HEADER_BYTES + 40,
                32);
            state->current_vertices += 1u;
            state->current_logical_count += occurrence.run_length;
            state->summary.trajectory_vertex_count += 1u;
            state->summary.logical_occurrence_count += occurrence.run_length;
            break;
        }
        case LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE:
            if (close_physicality(state) != LAPLACE_PERSISTENCE_OK) {
                return LAPLACE_PERSISTENCE_TRAJECTORY_INVALID;
            }
            comparison = state->have_occurrence
                ? memcmp(state->previous_occurrence.bytes,
                         record->value.occurrence.occurrence_id.bytes, 32)
                : -1;
            if (comparison >= 0) {
                return LAPLACE_PERSISTENCE_ORDER_INVALID;
            }
            state->previous_occurrence = record->value.occurrence.occurrence_id;
            state->have_occurrence = 1;
            state->phase = LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE;
            state->summary.occurrence_count += 1u;
            break;
        default:
            return LAPLACE_PERSISTENCE_FRAME_INVALID;
    }
    state->summary.frame_count += 1u;
    return LAPLACE_PERSISTENCE_OK;
}

laplace_persistence_status laplace_persistence_validate_stream(
    const laplace_framework_canonical_batch* batches,
    size_t batch_count,
    laplace_persistence_summary* summary) {
    validation_state state;
    size_t batch_index;
    if (batches == NULL || batch_count == 0 || summary == NULL) {
        return LAPLACE_PERSISTENCE_INVALID_ARGUMENT;
    }
    memset(&state, 0, sizeof(state));
    for (batch_index = 0; batch_index < batch_count; ++batch_index) {
        const laplace_framework_canonical_batch* batch = &batches[batch_index];
        size_t offset = 0;
        uint64_t records = 0;
        if (batch->canonical_bytes == NULL || batch->byte_count == 0 ||
            batch->byte_count > SIZE_MAX || batch->record_count == 0 ||
            batch->record_type != LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE ||
            batch->flags != 0) {
            return LAPLACE_PERSISTENCE_RECORD_INVALID;
        }
        while (offset < (size_t)batch->byte_count) {
            laplace_persistence_record record;
            size_t consumed = 0;
            laplace_persistence_status status = laplace_persistence_frame_decode(
                batch->canonical_bytes + offset,
                (size_t)batch->byte_count - offset,
                &record,
                &consumed);
            if (status != LAPLACE_PERSISTENCE_OK) {
                return status;
            }
            status = validate_record(
                &state, &record, batch->canonical_bytes + offset);
            if (status != LAPLACE_PERSISTENCE_OK) {
                return status;
            }
            offset += consumed;
            records += 1u;
        }
        if (records != batch->record_count ||
            UINT64_MAX - state.summary.byte_count < batch->byte_count) {
            return LAPLACE_PERSISTENCE_RECORD_INVALID;
        }
        state.summary.byte_count += batch->byte_count;
    }
    {
        const laplace_persistence_status close_status = close_physicality(&state);
        if (close_status != LAPLACE_PERSISTENCE_OK) {
            return close_status;
        }
    }
    if (state.summary.entity_count == 0 || state.summary.physicality_count == 0 ||
        state.summary.trajectory_vertex_count == 0 ||
        state.summary.occurrence_count == 0) {
        return LAPLACE_PERSISTENCE_RECORD_INVALID;
    }
    *summary = state.summary;
    return LAPLACE_PERSISTENCE_OK;
}
