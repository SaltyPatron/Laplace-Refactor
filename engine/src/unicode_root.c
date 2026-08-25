#include "laplace/unicode_root.h"

#include <math.h>
#include <string.h>

static const uint8_t expected_payload_kind[LAPLACE_UNICODE_ATOM_FIELD_COUNT] = {
    1u, 2u, 1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 1u, 1u,
    1u, 11u, 11u, 11u, 12u, 13u, 1u, 1u, 1u, 1u, 1u, 13u, 1u};

static uint16_t read_u16le(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static uint32_t read_u32le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static uint64_t read_u64le(const uint8_t* bytes) {
    uint64_t value = 0u;
    size_t index;
    for (index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void write_u16le(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static uint16_t read_u16be(const uint8_t* bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | (uint16_t)bytes[1]);
}

static void write_u16be(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8u);
    bytes[1] = (uint8_t)value;
}

static void write_u32le(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static void write_u64le(uint8_t* bytes, uint64_t value) {
    size_t index;
    for (index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static int root_frame_kind_valid(uint16_t kind) {
    return kind >= LAPLACE_UNICODE_ROOT_FRAME_ATOM &&
        kind <= LAPLACE_UNICODE_ROOT_FRAME_MANIFEST;
}

static int root_frame_payload_valid(
    uint16_t kind,
    uint64_t section_ordinal,
    const uint8_t* payload,
    uint32_t payload_bytes) {
    if (!root_frame_kind_valid(kind) || payload == NULL || payload_bytes == 0u) {
        return 0;
    }
    if (kind == LAPLACE_UNICODE_ROOT_FRAME_ATOM) {
        laplace_unicode_atom_record_view atom;
        size_t consumed = 0u;
#if defined(LAPLACE_TEST_UNICODE_ROOT_FRAME_SKIP_NESTED_VALIDATION)
        (void)section_ordinal;
        (void)atom;
        (void)consumed;
        return 1;
#else
        return section_ordinal < LAPLACE_UNICODE_ROOT_POPULATION &&
            laplace_unicode_atom_record_open(
                payload, payload_bytes, &atom, &consumed) == LAPLACE_UNICODE_OK &&
            consumed == payload_bytes &&
            atom.value.codepoint_position == section_ordinal;
#endif
    } else if (kind == LAPLACE_UNICODE_ROOT_FRAME_DUCET_POSITION) {
        laplace_unicode_ducet_position_view position;
        size_t consumed = 0u;
        return section_ordinal < LAPLACE_UNICODE_ROOT_POPULATION &&
            laplace_unicode_ducet_position_open(
                payload, payload_bytes, &position, &consumed) ==
                LAPLACE_UNICODE_OK &&
            consumed == payload_bytes &&
            position.codepoint_position == section_ordinal;
    } else if (kind == LAPLACE_UNICODE_ROOT_FRAME_DUCET_CONTRACTION) {
        laplace_unicode_ducet_contraction_view contraction;
        size_t consumed = 0u;
        return laplace_unicode_ducet_contraction_open(
                   payload, payload_bytes, &contraction, &consumed) ==
                   LAPLACE_UNICODE_OK &&
            consumed == payload_bytes;
    } else if (kind == LAPLACE_UNICODE_ROOT_FRAME_NORMALIZATION_COMPOSITION) {
        laplace_unicode_normalization_composition composition;
        size_t consumed = 0u;
        return laplace_unicode_normalization_composition_open(
                   payload, payload_bytes, &composition, &consumed) ==
                   LAPLACE_UNICODE_OK &&
            consumed == payload_bytes;
    } else if (kind == LAPLACE_UNICODE_ROOT_FRAME_MANIFEST) {
        laplace_unicode_root_manifest manifest;
        size_t consumed = 0u;
        return section_ordinal == 0u &&
            laplace_unicode_root_manifest_open(
                payload, payload_bytes, &manifest, &consumed) ==
                LAPLACE_UNICODE_OK &&
            consumed == payload_bytes;
    }
    return 1;
}

static int field_shape_valid(const laplace_unicode_atom_field* field, size_t index) {
    if (field->field_id != index + 1u ||
        field->payload_kind != expected_payload_kind[index] ||
        field->flags != 0u ||
        (field->payload == NULL && field->payload_bytes != 0u)) {
        return 0;
    }
    if (field->payload_kind == LAPLACE_UNICODE_PAYLOAD_U8 ||
        field->payload_kind == LAPLACE_UNICODE_PAYLOAD_BOOLEAN) {
        return field->payload_bytes == 1u &&
            (field->payload_kind != LAPLACE_UNICODE_PAYLOAD_BOOLEAN ||
             field->payload[0] <= 1u);
    }
    if (field->payload_kind == LAPLACE_UNICODE_PAYLOAD_OPTIONAL_POSITION) {
        return field->payload_bytes == 0u || field->payload_bytes == 4u;
    }
    if (field->payload_kind == LAPLACE_UNICODE_PAYLOAD_OPTIONAL_POSITION_AND_ASCII_TYPE) {
        return field->payload_bytes == 0u || field->payload_bytes == 5u;
    }
    if (field->payload_kind == LAPLACE_UNICODE_PAYLOAD_POSITION_SEQUENCE) {
        return (field->payload_bytes % 4u) == 0u;
    }
    return 1;
}

static int identity_fields_match(
    const laplace_unicode_atom_record* record,
    const laplace_id128* expected_id,
    const laplace_digest256* expected_witness) {
#if defined(LAPLACE_TEST_SKIP_UNICODE_ATOM_IDENTITY_VALIDATION)
    (void)record;
    (void)expected_id;
    (void)expected_witness;
    return 1;
#else
    return memcmp(expected_id, &record->content_id, sizeof(*expected_id)) == 0 &&
        memcmp(expected_witness, &record->identity_preimage_fingerprint,
               sizeof(*expected_witness)) == 0;
#endif
}

static int fixed_record_valid(const laplace_unicode_atom_record* record) {
    uint8_t expected_lup[4] = {0u, 0u, 0u, 0u};
    size_t expected_length = 0u;
    laplace_id128 expected_id;
    laplace_digest256 expected_witness;
    size_t axis;
    size_t field;
    const int surrogate = record->codepoint_position >= UINT32_C(0xd800) &&
        record->codepoint_position <= UINT32_C(0xdfff);

    if (record->codepoint_position >= LAPLACE_UNICODE_ROOT_POPULATION ||
        record->placement_rank >= LAPLACE_UNICODE_ROOT_POPULATION ||
        record->position_class > LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS ||
        ((record->position_class == LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS) != surrogate) ||
        laplace_unicode_position_encode(record->codepoint_position, expected_lup,
                                        &expected_length) != LAPLACE_IDENTITY_OK ||
        expected_length != record->lup_v1_length ||
        memcmp(expected_lup, record->lup_v1_bytes, sizeof(expected_lup)) != 0 ||
        laplace_identity_codepoint_witness(record->codepoint_position, &expected_id,
                                           &expected_witness) != LAPLACE_IDENTITY_OK ||
        !identity_fields_match(record, &expected_id, &expected_witness)) {
        return 0;
    }
    for (axis = 0u; axis < 4u; ++axis) {
        if (!isfinite(record->coordinate.component[axis]) ||
            record->coordinate.component[axis] < -1.0 ||
            record->coordinate.component[axis] > 1.0) {
            return 0;
        }
    }
    for (field = 0u; field < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field) {
        if (!field_shape_valid(&record->fields[field], field)) {
            return 0;
        }
    }
    return 1;
}

laplace_unicode_status laplace_unicode_atom_record_measure(
    const laplace_unicode_atom_record* record,
    size_t* encoded_bytes) {
    size_t total = LAPLACE_UNICODE_ATOM_HEADER_BYTES;
    size_t field;
    if (record == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (!fixed_record_valid(record)) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    for (field = 0u; field < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field) {
        const size_t addition = LAPLACE_UNICODE_ATOM_FIELD_HEADER_BYTES +
            (size_t)record->fields[field].payload_bytes;
        if (SIZE_MAX - total < addition) {
            return LAPLACE_UNICODE_SIZE_OVERFLOW;
        }
        total += addition;
    }
    if (total > UINT32_MAX) {
        return LAPLACE_UNICODE_SIZE_OVERFLOW;
    }
    *encoded_bytes = total;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_atom_record_encode(
    const laplace_unicode_atom_record* record,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes) {
    size_t required = 0u;
    size_t offset = LAPLACE_UNICODE_ATOM_HEADER_BYTES;
    size_t field;
    laplace_unicode_status status;
    if (record == NULL || output == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    status = laplace_unicode_atom_record_measure(record, &required);
    if (status != LAPLACE_UNICODE_OK) {
        return status;
    }
    if (output_capacity < required) {
        *encoded_bytes = required;
        return LAPLACE_UNICODE_BUFFER_TOO_SMALL;
    }
    memset(output, 0, required);
    memcpy(output, "LUAR", 4u);
    write_u16le(output + 4u, LAPLACE_UNICODE_ATOM_RECORD_VERSION);
    write_u16le(output + 6u, LAPLACE_UNICODE_ATOM_HEADER_BYTES);
    write_u32le(output + 8u, (uint32_t)required);
    write_u32le(output + 12u, record->codepoint_position);
    write_u32le(output + 16u, record->placement_rank);
    output[20] = record->position_class;
    output[21] = record->lup_v1_length;
    memcpy(output + 24u, record->lup_v1_bytes, 4u);
    memcpy(output + 28u, record->content_id.bytes, 16u);
    memcpy(output + 44u, record->identity_preimage_fingerprint.bytes, 32u);
    for (field = 0u; field < 4u; ++field) {
        uint64_t bits = 0u;
        memcpy(&bits, &record->coordinate.component[field], sizeof(bits));
        write_u64le(output + 76u + field * 8u, bits);
    }
    memcpy(output + 108u, record->hilbert_key, LAPLACE_UNICODE_HILBERT_KEY_BYTES);
    write_u16le(output + 124u, LAPLACE_UNICODE_ATOM_FIELD_COUNT);
    write_u32le(output + 128u,
                (uint32_t)(required - LAPLACE_UNICODE_ATOM_HEADER_BYTES));
    for (field = 0u; field < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field) {
        const laplace_unicode_atom_field* value = &record->fields[field];
        write_u16le(output + offset, value->field_id);
        output[offset + 2u] = value->payload_kind;
        output[offset + 3u] = value->flags;
        write_u32le(output + offset + 4u, value->payload_bytes);
        if (value->payload_bytes != 0u) {
            memcpy(output + offset + 8u, value->payload, value->payload_bytes);
        }
        offset += 8u + value->payload_bytes;
    }
    *encoded_bytes = required;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_atom_record_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_atom_record_view* view,
    size_t* consumed_bytes) {
    uint32_t record_bytes;
    uint32_t variable_bytes;
    size_t offset = LAPLACE_UNICODE_ATOM_HEADER_BYTES;
    size_t axis;
    size_t field;
    if (encoded == NULL || view == NULL || consumed_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (available_bytes < LAPLACE_UNICODE_ATOM_HEADER_BYTES ||
        memcmp(encoded, "LUAR", 4u) != 0 ||
        read_u16le(encoded + 4u) != LAPLACE_UNICODE_ATOM_RECORD_VERSION ||
        read_u16le(encoded + 6u) != LAPLACE_UNICODE_ATOM_HEADER_BYTES ||
        read_u16le(encoded + 22u) != 0u ||
        read_u16le(encoded + 124u) != LAPLACE_UNICODE_ATOM_FIELD_COUNT ||
        read_u16le(encoded + 126u) != 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    record_bytes = read_u32le(encoded + 8u);
    variable_bytes = read_u32le(encoded + 128u);
    if (record_bytes < LAPLACE_UNICODE_ATOM_HEADER_BYTES ||
        record_bytes > available_bytes ||
        variable_bytes != record_bytes - LAPLACE_UNICODE_ATOM_HEADER_BYTES) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    memset(view, 0, sizeof(*view));
    view->value.codepoint_position = read_u32le(encoded + 12u);
    view->value.placement_rank = read_u32le(encoded + 16u);
    view->value.position_class = encoded[20];
    view->value.lup_v1_length = encoded[21];
    memcpy(view->value.lup_v1_bytes, encoded + 24u, 4u);
    memcpy(view->value.content_id.bytes, encoded + 28u, 16u);
    memcpy(view->value.identity_preimage_fingerprint.bytes, encoded + 44u, 32u);
    for (axis = 0u; axis < 4u; ++axis) {
        const uint64_t bits = read_u64le(encoded + 76u + axis * 8u);
        memcpy(&view->value.coordinate.component[axis], &bits, sizeof(bits));
    }
    memcpy(view->value.hilbert_key, encoded + 108u,
           LAPLACE_UNICODE_HILBERT_KEY_BYTES);
    for (field = 0u; field < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field) {
        laplace_unicode_atom_field* value = &view->value.fields[field];
        uint32_t payload_bytes;
        if (offset > record_bytes || record_bytes - offset < 8u) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
        value->field_id = read_u16le(encoded + offset);
        value->payload_kind = encoded[offset + 2u];
        value->flags = encoded[offset + 3u];
        payload_bytes = read_u32le(encoded + offset + 4u);
        if ((size_t)payload_bytes > (size_t)record_bytes - offset - 8u) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
        value->payload_bytes = payload_bytes;
        value->payload = payload_bytes == 0u ? NULL : encoded + offset + 8u;
        offset += 8u + payload_bytes;
    }
    if (offset != record_bytes || !fixed_record_valid(&view->value)) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    view->encoded_record = encoded;
    view->encoded_bytes = record_bytes;
    *consumed_bytes = record_bytes;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_root_frame_measure(
    const laplace_unicode_root_frame* frame,
    size_t* encoded_bytes) {
    size_t required;
    if (frame == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (frame->flags != 0u ||
        !root_frame_payload_valid(
            frame->kind, frame->section_ordinal,
            frame->payload, frame->payload_bytes)) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    required = LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES +
        (size_t)frame->payload_bytes;
    if (required > UINT32_MAX) {
        return LAPLACE_UNICODE_SIZE_OVERFLOW;
    }
    *encoded_bytes = required;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_root_frame_encode(
    const laplace_unicode_root_frame* frame,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes) {
    size_t required = 0u;
    laplace_unicode_status status;
    if (frame == NULL || output == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    status = laplace_unicode_root_frame_measure(frame, &required);
    if (status != LAPLACE_UNICODE_OK) {
        return status;
    }
    if (output_capacity < required) {
        *encoded_bytes = required;
        return LAPLACE_UNICODE_BUFFER_TOO_SMALL;
    }
    memset(output, 0, required);
    memcpy(output, "LURF", 4u);
    write_u16le(output + 4u, LAPLACE_UNICODE_ROOT_FRAME_VERSION);
    write_u16le(output + 6u, LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES);
    write_u32le(output + 8u, (uint32_t)required);
    write_u16le(output + 12u, frame->kind);
    write_u16le(output + 14u, frame->flags);
    write_u64le(output + 16u, frame->section_ordinal);
    write_u32le(output + 24u, frame->payload_bytes);
    memcpy(output + LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES,
           frame->payload, frame->payload_bytes);
    *encoded_bytes = required;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_root_frame_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_root_frame_view* view,
    size_t* consumed_bytes) {
    uint32_t frame_bytes;
    uint32_t payload_bytes;
    uint16_t kind;
    uint16_t flags;
    uint64_t section_ordinal;
    if (encoded == NULL || view == NULL || consumed_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (available_bytes < LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES ||
        memcmp(encoded, "LURF", 4u) != 0 ||
        read_u16le(encoded + 4u) != LAPLACE_UNICODE_ROOT_FRAME_VERSION ||
        read_u16le(encoded + 6u) != LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES ||
        read_u32le(encoded + 28u) != 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    frame_bytes = read_u32le(encoded + 8u);
    kind = read_u16le(encoded + 12u);
    flags = read_u16le(encoded + 14u);
    section_ordinal = read_u64le(encoded + 16u);
    payload_bytes = read_u32le(encoded + 24u);
    if (frame_bytes < LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES ||
        frame_bytes > available_bytes ||
        payload_bytes != frame_bytes - LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES ||
        flags != 0u ||
        !root_frame_payload_valid(
            kind, section_ordinal,
            encoded + LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES,
            payload_bytes)) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    memset(view, 0, sizeof(*view));
    view->value.payload = encoded + LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES;
    view->value.section_ordinal = section_ordinal;
    view->value.payload_bytes = payload_bytes;
    view->value.kind = kind;
    view->value.flags = flags;
    view->encoded_frame = encoded;
    view->encoded_bytes = frame_bytes;
    *consumed_bytes = frame_bytes;
    return LAPLACE_UNICODE_OK;
}

static int ducet_provenance_valid(uint8_t provenance) {
    return provenance >= LAPLACE_UNICODE_DUCET_EXPLICIT &&
        provenance <= LAPLACE_UNICODE_DUCET_LUP_SURROGATE_EXTENSION;
}

static int collation_element_valid(
    const laplace_unicode_collation_element* element) {
    return element != NULL && element->variable <= 1u &&
        element->reserved == 0u;
}

static void collation_element_encode(
    const laplace_unicode_collation_element* element,
    uint8_t output[LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES]) {
    output[0] = element->variable;
    output[1] = 0u;
    write_u16be(output + 2u, element->primary);
    write_u16be(output + 4u, element->secondary);
    write_u16be(output + 6u, element->tertiary);
}

static laplace_unicode_status collation_element_open(
    const uint8_t encoded[LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES],
    laplace_unicode_collation_element* element) {
    if (encoded == NULL || element == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (encoded[0] > 1u || encoded[1] != 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    element->variable = encoded[0];
    element->reserved = encoded[1];
    element->primary = read_u16be(encoded + 2u);
    element->secondary = read_u16be(encoded + 4u);
    element->tertiary = read_u16be(encoded + 6u);
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_position_measure(
    const laplace_unicode_ducet_position_record* record,
    size_t* encoded_bytes) {
    uint64_t required;
    uint32_t index;
    if (record == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (record->codepoint_position >= LAPLACE_UNICODE_ROOT_POPULATION ||
        !ducet_provenance_valid(record->provenance) ||
        record->reserved[0] != 0u || record->reserved[1] != 0u ||
        record->reserved[2] != 0u || record->elements == NULL ||
        record->element_count == 0u || record->equivalence_key == NULL ||
        record->equivalence_key_bytes == 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    for (index = 0u; index < record->element_count; ++index) {
        if (!collation_element_valid(&record->elements[index])) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
    }
    required = LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES +
        (uint64_t)record->element_count *
            LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES +
        record->equivalence_key_bytes;
    if (required > UINT32_MAX) {
        return LAPLACE_UNICODE_SIZE_OVERFLOW;
    }
    *encoded_bytes = (size_t)required;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_position_encode(
    const laplace_unicode_ducet_position_record* record,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes) {
    size_t required = 0u;
    size_t offset = LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES;
    uint32_t index;
    laplace_unicode_status status;
    if (record == NULL || output == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    status = laplace_unicode_ducet_position_measure(record, &required);
    if (status != LAPLACE_UNICODE_OK) {
        return status;
    }
    if (output_capacity < required) {
        *encoded_bytes = required;
        return LAPLACE_UNICODE_BUFFER_TOO_SMALL;
    }
    memset(output, 0, required);
    memcpy(output, "LUDP", 4u);
    write_u16le(output + 4u, LAPLACE_UNICODE_DUCET_POSITION_VERSION);
    write_u16le(output + 6u, LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES);
    write_u32le(output + 8u, (uint32_t)required);
    write_u32le(output + 12u, record->codepoint_position);
    output[16] = record->provenance;
    write_u32le(output + 20u, record->element_count);
    write_u32le(output + 24u, record->equivalence_key_bytes);
    for (index = 0u; index < record->element_count; ++index) {
        const laplace_unicode_collation_element* element =
            &record->elements[index];
        collation_element_encode(element, output + offset);
        offset += LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES;
    }
    memcpy(output + offset, record->equivalence_key,
           record->equivalence_key_bytes);
    *encoded_bytes = required;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_position_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_ducet_position_view* view,
    size_t* consumed_bytes) {
    uint32_t record_bytes;
    uint32_t element_count;
    uint32_t key_bytes;
    uint64_t element_bytes;
    uint64_t required;
    uint32_t index;
    if (encoded == NULL || view == NULL || consumed_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (available_bytes < LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES ||
        memcmp(encoded, "LUDP", 4u) != 0 ||
        read_u16le(encoded + 4u) != LAPLACE_UNICODE_DUCET_POSITION_VERSION ||
        read_u16le(encoded + 6u) != LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES ||
        encoded[17] != 0u || encoded[18] != 0u || encoded[19] != 0u ||
        read_u32le(encoded + 28u) != 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    record_bytes = read_u32le(encoded + 8u);
    element_count = read_u32le(encoded + 20u);
    key_bytes = read_u32le(encoded + 24u);
    if (element_count == 0u || key_bytes == 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    element_bytes = (uint64_t)element_count *
        LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES;
    required = LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES + element_bytes +
        key_bytes;
    if (required > UINT32_MAX || record_bytes != required ||
        record_bytes > available_bytes ||
        read_u32le(encoded + 12u) >= LAPLACE_UNICODE_ROOT_POPULATION ||
        !ducet_provenance_valid(encoded[16])) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    memset(view, 0, sizeof(*view));
    view->encoded_elements = encoded + LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES;
    view->equivalence_key = view->encoded_elements + (size_t)element_bytes;
    view->encoded_record = encoded;
    view->codepoint_position = read_u32le(encoded + 12u);
    view->element_count = element_count;
    view->equivalence_key_bytes = key_bytes;
    view->encoded_bytes = record_bytes;
    view->provenance = encoded[16];
    for (index = 0u; index < element_count; ++index) {
        laplace_unicode_collation_element element;
        if (laplace_unicode_ducet_position_element(view, index, &element) !=
            LAPLACE_UNICODE_OK) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
    }
    *consumed_bytes = record_bytes;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_position_element(
    const laplace_unicode_ducet_position_view* view,
    uint32_t element_index,
    laplace_unicode_collation_element* element) {
    const uint8_t* encoded;
    if (view == NULL || element == NULL || view->encoded_elements == NULL ||
        element_index >= view->element_count) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    encoded = view->encoded_elements +
        (size_t)element_index * LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES;
    return collation_element_open(encoded, element);
}

laplace_unicode_status laplace_unicode_normalization_composition_encode(
    const laplace_unicode_normalization_composition* record,
    uint8_t output[LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES]) {
    if (record == NULL || output == NULL ||
        record->starter_position >= LAPLACE_UNICODE_ROOT_POPULATION ||
        record->combining_position >= LAPLACE_UNICODE_ROOT_POPULATION ||
        record->composite_position >= LAPLACE_UNICODE_ROOT_POPULATION) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    memset(output, 0, LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES);
    memcpy(output, "LUNC", 4u);
    write_u16le(output + 4u, LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_VERSION);
    write_u16le(output + 6u, LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES);
    write_u32le(output + 8u, LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES);
    write_u32le(output + 12u, record->starter_position);
    write_u32le(output + 16u, record->combining_position);
    write_u32le(output + 20u, record->composite_position);
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_normalization_composition_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_normalization_composition* record,
    size_t* consumed_bytes) {
    laplace_unicode_normalization_composition decoded;
    if (encoded == NULL || record == NULL || consumed_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (available_bytes < LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES ||
        memcmp(encoded, "LUNC", 4u) != 0 ||
        read_u16le(encoded + 4u) !=
            LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_VERSION ||
        read_u16le(encoded + 6u) !=
            LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES ||
        read_u32le(encoded + 8u) !=
            LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES ||
        read_u32le(encoded + 24u) != 0u ||
        read_u32le(encoded + 28u) != 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    decoded.starter_position = read_u32le(encoded + 12u);
    decoded.combining_position = read_u32le(encoded + 16u);
    decoded.composite_position = read_u32le(encoded + 20u);
    if (decoded.starter_position >= LAPLACE_UNICODE_ROOT_POPULATION ||
        decoded.combining_position >= LAPLACE_UNICODE_ROOT_POPULATION ||
        decoded.composite_position >= LAPLACE_UNICODE_ROOT_POPULATION) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    *record = decoded;
    *consumed_bytes = LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_contraction_measure(
    const laplace_unicode_ducet_contraction_record* record,
    size_t* encoded_bytes) {
    uint64_t sequence_bytes;
    uint64_t element_bytes;
    uint64_t required;
    uint32_t index;
    if (record == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (record->source_line_ordinal == 0u || record->sequence == NULL ||
        record->sequence_count < 2u || record->elements == NULL ||
        record->element_count == 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    for (index = 0u; index < record->sequence_count; ++index) {
        if (record->sequence[index] >= LAPLACE_UNICODE_ROOT_POPULATION) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
    }
    for (index = 0u; index < record->element_count; ++index) {
        if (!collation_element_valid(&record->elements[index])) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
    }
    sequence_bytes = (uint64_t)record->sequence_count * 4u;
    element_bytes = (uint64_t)record->element_count *
        LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES;
    required = LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES +
        sequence_bytes + element_bytes;
    if (required > UINT32_MAX) {
        return LAPLACE_UNICODE_SIZE_OVERFLOW;
    }
    *encoded_bytes = (size_t)required;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_contraction_encode(
    const laplace_unicode_ducet_contraction_record* record,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes) {
    size_t required = 0u;
    size_t offset = LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES;
    uint32_t index;
    laplace_unicode_status status;
    if (record == NULL || output == NULL || encoded_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    status = laplace_unicode_ducet_contraction_measure(record, &required);
    if (status != LAPLACE_UNICODE_OK) {
        return status;
    }
    if (output_capacity < required) {
        *encoded_bytes = required;
        return LAPLACE_UNICODE_BUFFER_TOO_SMALL;
    }
    memset(output, 0, required);
    memcpy(output, "LUCR", 4u);
    write_u16le(output + 4u, LAPLACE_UNICODE_DUCET_CONTRACTION_VERSION);
    write_u16le(output + 6u, LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES);
    write_u32le(output + 8u, (uint32_t)required);
    write_u32le(output + 12u, record->source_line_ordinal);
    write_u32le(output + 16u, record->sequence_count);
    write_u32le(output + 20u, record->element_count);
    for (index = 0u; index < record->sequence_count; ++index) {
        write_u32le(output + offset, record->sequence[index]);
        offset += 4u;
    }
    for (index = 0u; index < record->element_count; ++index) {
        collation_element_encode(&record->elements[index], output + offset);
        offset += LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES;
    }
    *encoded_bytes = required;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_contraction_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_ducet_contraction_view* view,
    size_t* consumed_bytes) {
    uint32_t record_bytes;
    uint32_t sequence_count;
    uint32_t element_count;
    uint64_t sequence_bytes;
    uint64_t element_bytes;
    uint64_t required;
    uint32_t index;
    if (encoded == NULL || view == NULL || consumed_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (available_bytes < LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES ||
        memcmp(encoded, "LUCR", 4u) != 0 ||
        read_u16le(encoded + 4u) != LAPLACE_UNICODE_DUCET_CONTRACTION_VERSION ||
        read_u16le(encoded + 6u) != LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES ||
        read_u32le(encoded + 12u) == 0u || read_u32le(encoded + 24u) != 0u ||
        read_u32le(encoded + 28u) != 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    record_bytes = read_u32le(encoded + 8u);
    sequence_count = read_u32le(encoded + 16u);
    element_count = read_u32le(encoded + 20u);
    if (sequence_count < 2u || element_count == 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    sequence_bytes = (uint64_t)sequence_count * 4u;
    element_bytes = (uint64_t)element_count *
        LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES;
    required = LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES +
        sequence_bytes + element_bytes;
    if (required > UINT32_MAX || record_bytes != required ||
        record_bytes > available_bytes) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    memset(view, 0, sizeof(*view));
    view->encoded_sequence = encoded +
        LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES;
    view->encoded_elements = view->encoded_sequence + (size_t)sequence_bytes;
    view->encoded_record = encoded;
    view->source_line_ordinal = read_u32le(encoded + 12u);
    view->sequence_count = sequence_count;
    view->element_count = element_count;
    view->encoded_bytes = record_bytes;
    for (index = 0u; index < sequence_count; ++index) {
        uint32_t position = 0u;
        if (laplace_unicode_ducet_contraction_position(
                view, index, &position) != LAPLACE_UNICODE_OK) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
    }
    for (index = 0u; index < element_count; ++index) {
        laplace_unicode_collation_element element;
        if (laplace_unicode_ducet_contraction_element(
                view, index, &element) != LAPLACE_UNICODE_OK) {
            return LAPLACE_UNICODE_RECORD_INVALID;
        }
    }
    *consumed_bytes = record_bytes;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_contraction_position(
    const laplace_unicode_ducet_contraction_view* view,
    uint32_t position_index,
    uint32_t* codepoint_position) {
    uint32_t decoded;
    if (view == NULL || codepoint_position == NULL ||
        view->encoded_sequence == NULL || position_index >= view->sequence_count) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    decoded = read_u32le(view->encoded_sequence + (size_t)position_index * 4u);
    if (decoded >= LAPLACE_UNICODE_ROOT_POPULATION) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    *codepoint_position = decoded;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_ducet_contraction_element(
    const laplace_unicode_ducet_contraction_view* view,
    uint32_t element_index,
    laplace_unicode_collation_element* element) {
    if (view == NULL || element == NULL || view->encoded_elements == NULL ||
        element_index >= view->element_count) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    return collation_element_open(
        view->encoded_elements +
            (size_t)element_index * LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES,
        element);
}

static int root_manifest_counts_valid(
    const laplace_unicode_root_manifest* manifest) {
    uint64_t total;
    if (manifest->atom_count != LAPLACE_UNICODE_ROOT_POPULATION ||
        manifest->ducet_position_count != LAPLACE_UNICODE_ROOT_POPULATION) {
        return 0;
    }
    if (UINT64_MAX - manifest->atom_count < manifest->ducet_position_count) {
        return 0;
    }
    total = manifest->atom_count + manifest->ducet_position_count;
    if (UINT64_MAX - total < manifest->ducet_contraction_count) {
        return 0;
    }
    total += manifest->ducet_contraction_count;
    if (UINT64_MAX - total < manifest->normalization_composition_count ||
        UINT64_MAX - total - manifest->normalization_composition_count < 1u) {
        return 0;
    }
    total += manifest->normalization_composition_count + 1u;
    return manifest->total_frame_count == total;
}

laplace_unicode_status laplace_unicode_root_manifest_encode(
    const laplace_unicode_root_manifest* manifest,
    uint8_t output[LAPLACE_UNICODE_ROOT_MANIFEST_BYTES]) {
    size_t offset = 56u;
    const laplace_digest256* fingerprints[9];
    size_t index;
    if (manifest == NULL || output == NULL ||
        !root_manifest_counts_valid(manifest)) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    fingerprints[0] = &manifest->source_fingerprint;
    fingerprints[1] = &manifest->recipe_fingerprint;
    fingerprints[2] = &manifest->numeric_provider_receipt;
    fingerprints[3] = &manifest->stream_contract_fingerprint;
    fingerprints[4] = &manifest->atom_section_fingerprint;
    fingerprints[5] = &manifest->ducet_position_section_fingerprint;
    fingerprints[6] = &manifest->ducet_contraction_section_fingerprint;
    fingerprints[7] = &manifest->normalization_composition_section_fingerprint;
    fingerprints[8] = &manifest->algorithmic_hangul_rule_fingerprint;
    memset(output, 0, LAPLACE_UNICODE_ROOT_MANIFEST_BYTES);
    memcpy(output, "LURM", 4u);
    write_u16le(output + 4u, LAPLACE_UNICODE_ROOT_MANIFEST_VERSION);
    write_u16le(output + 6u, LAPLACE_UNICODE_ROOT_MANIFEST_BYTES);
    write_u32le(output + 8u, LAPLACE_UNICODE_ROOT_MANIFEST_BYTES);
    write_u64le(output + 16u, manifest->atom_count);
    write_u64le(output + 24u, manifest->ducet_position_count);
    write_u64le(output + 32u, manifest->ducet_contraction_count);
    write_u64le(output + 40u, manifest->normalization_composition_count);
    write_u64le(output + 48u, manifest->total_frame_count);
    for (index = 0u; index < 9u; ++index) {
        memcpy(output + offset, fingerprints[index]->bytes, 32u);
        offset += 32u;
    }
    return offset == 344u ? LAPLACE_UNICODE_OK : LAPLACE_UNICODE_PROVIDER_FAILURE;
}

laplace_unicode_status laplace_unicode_root_manifest_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_root_manifest* manifest,
    size_t* consumed_bytes) {
    laplace_unicode_root_manifest decoded;
    laplace_digest256* fingerprints[9];
    size_t offset = 56u;
    size_t index;
    if (encoded == NULL || manifest == NULL || consumed_bytes == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (available_bytes < LAPLACE_UNICODE_ROOT_MANIFEST_BYTES ||
        memcmp(encoded, "LURM", 4u) != 0 ||
        read_u16le(encoded + 4u) != LAPLACE_UNICODE_ROOT_MANIFEST_VERSION ||
        read_u16le(encoded + 6u) != LAPLACE_UNICODE_ROOT_MANIFEST_BYTES ||
        read_u32le(encoded + 8u) != LAPLACE_UNICODE_ROOT_MANIFEST_BYTES ||
        read_u32le(encoded + 12u) != 0u || read_u64le(encoded + 344u) != 0u) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.atom_count = read_u64le(encoded + 16u);
    decoded.ducet_position_count = read_u64le(encoded + 24u);
    decoded.ducet_contraction_count = read_u64le(encoded + 32u);
    decoded.normalization_composition_count = read_u64le(encoded + 40u);
    decoded.total_frame_count = read_u64le(encoded + 48u);
    fingerprints[0] = &decoded.source_fingerprint;
    fingerprints[1] = &decoded.recipe_fingerprint;
    fingerprints[2] = &decoded.numeric_provider_receipt;
    fingerprints[3] = &decoded.stream_contract_fingerprint;
    fingerprints[4] = &decoded.atom_section_fingerprint;
    fingerprints[5] = &decoded.ducet_position_section_fingerprint;
    fingerprints[6] = &decoded.ducet_contraction_section_fingerprint;
    fingerprints[7] = &decoded.normalization_composition_section_fingerprint;
    fingerprints[8] = &decoded.algorithmic_hangul_rule_fingerprint;
    for (index = 0u; index < 9u; ++index) {
        memcpy(fingerprints[index]->bytes, encoded + offset, 32u);
        offset += 32u;
    }
    if (offset != 344u || !root_manifest_counts_valid(&decoded)) {
        return LAPLACE_UNICODE_RECORD_INVALID;
    }
    *manifest = decoded;
    *consumed_bytes = LAPLACE_UNICODE_ROOT_MANIFEST_BYTES;
    return LAPLACE_UNICODE_OK;
}

typedef struct laplace_u96 {
    uint64_t low;
    uint32_t high;
} laplace_u96;

static laplace_u96 multiply_u53_u32(uint64_t left, uint32_t right) {
    const uint64_t low_product = (left & UINT64_C(0xffffffff)) * right;
    const uint64_t high_product = (left >> 32u) * right;
    const uint64_t shifted = high_product << 32u;
    laplace_u96 result;
    result.low = low_product + shifted;
    result.high = (uint32_t)(high_product >> 32u) +
        (result.low < low_product ? 1u : 0u);
    return result;
}

static uint32_t u96_shift_right(const laplace_u96* value, uint32_t shift) {
    if (shift < 64u) {
        return (uint32_t)((value->low >> shift) |
            ((uint64_t)value->high << (64u - shift)));
    }
    return value->high >> (shift - 64u);
}

static int u96_remainder_zero(const laplace_u96* value, uint32_t shift) {
    if (shift < 64u) {
        return (value->low & ((UINT64_C(1) << shift) - 1u)) == 0u;
    }
    if (value->low != 0u) {
        return 0;
    }
    return (value->high & ((UINT32_C(1) << (shift - 64u)) - 1u)) == 0u;
}

laplace_unicode_status laplace_unicode_quantize_component_u32(
    double component,
    uint32_t* quantized) {
    uint64_t bits;
    uint64_t fraction;
    uint64_t significand;
    uint32_t exponent;
    uint32_t denominator_shift;
    laplace_u96 product;
    uint32_t quotient;
    uint32_t base;
    int remainder_zero;
    const int negative = signbit(component) != 0;
    if (quantized == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    memcpy(&bits, &component, sizeof(bits));
    fraction = bits & UINT64_C(0x000fffffffffffff);
    exponent = (uint32_t)((bits >> 52u) & UINT64_C(0x7ff));
    if (exponent == 0x7ffu || component < -1.0 || component > 1.0) {
        return LAPLACE_UNICODE_NUMERIC_OUT_OF_RANGE;
    }
    if ((bits & UINT64_C(0x7fffffffffffffff)) == 0u) {
        *quantized = UINT32_C(2147483648);
        return LAPLACE_UNICODE_OK;
    }
    if (exponent == 0u) {
        significand = fraction;
        denominator_shift = 1075u;
    } else {
        significand = UINT64_C(0x0010000000000000) | fraction;
        denominator_shift = 1076u - exponent;
    }
    if (denominator_shift >= 86u) {
        *quantized = negative ? UINT32_C(2147483647) : UINT32_C(2147483648);
        return LAPLACE_UNICODE_OK;
    }
    product = multiply_u53_u32(significand, UINT32_C(4294967295));
    quotient = u96_shift_right(&product, denominator_shift);
    remainder_zero = u96_remainder_zero(&product, denominator_shift);
    if (negative) {
        base = UINT32_C(2147483647) - quotient;
        if (remainder_zero && (base & 1u) != 0u) {
            base += 1u;
        }
    } else {
        base = UINT32_C(2147483647) + quotient;
        if (!remainder_zero || (base & 1u) != 0u) {
            base += 1u;
        }
    }
    *quantized = base;
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_hilbert4_encode(
    const uint32_t axes[4],
    uint8_t key[LAPLACE_UNICODE_HILBERT_KEY_BYTES]) {
    uint32_t transformed[4];
    uint32_t q;
    uint32_t t;
    size_t axis;
    size_t byte_index = 0u;
    uint8_t accumulator = 0u;
    uint32_t accumulated_bits = 0u;
    int bit;
    if (axes == NULL || key == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    memcpy(transformed, axes, sizeof(transformed));
    for (q = UINT32_C(0x80000000); q > 1u; q >>= 1u) {
        const uint32_t p = q - 1u;
        for (axis = 0u; axis < 4u; ++axis) {
            if ((transformed[axis] & q) != 0u) {
                transformed[0] ^= p;
            } else {
                t = (transformed[0] ^ transformed[axis]) & p;
                transformed[0] ^= t;
                transformed[axis] ^= t;
            }
        }
    }
    for (axis = 1u; axis < 4u; ++axis) {
        transformed[axis] ^= transformed[axis - 1u];
    }
    t = 0u;
    for (q = UINT32_C(0x80000000); q > 1u; q >>= 1u) {
        if ((transformed[3] & q) != 0u) {
            t ^= q - 1u;
        }
    }
    for (axis = 0u; axis < 4u; ++axis) {
        transformed[axis] ^= t;
    }
    for (bit = 31; bit >= 0; --bit) {
        for (axis = 0u; axis < 4u; ++axis) {
#if defined(LAPLACE_TEST_UNICODE_HILBERT_REVERSE_AXIS_ORDER)
            const size_t emitted_axis = 3u - axis;
#else
            const size_t emitted_axis = axis;
#endif
            const uint32_t emitted_bit =
                (transformed[emitted_axis] >> (uint32_t)bit) & UINT32_C(1);
            accumulator = (uint8_t)(((uint32_t)accumulator << 1u) | emitted_bit);
            accumulated_bits += 1u;
            if (accumulated_bits == 8u) {
                key[byte_index++] = accumulator;
                accumulator = 0u;
                accumulated_bits = 0u;
            }
        }
    }
    return byte_index == LAPLACE_UNICODE_HILBERT_KEY_BYTES
        ? LAPLACE_UNICODE_OK
        : LAPLACE_UNICODE_PROVIDER_FAILURE;
}
