#include "laplace/unicode_root.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "blake3.h"

static const uint8_t unicode_root_section_domain[] =
    "laplace-unicode-root-section-v1";
static const uint8_t unicode_root_validation_domain[] =
    "laplace-unicode-root-validation-v1";

struct laplace_unicode_root_stream_validator {
    laplace_unicode_root_stream_expectation expectation;
    blake3_hasher section_hashers[4];
    uint64_t section_counts[4];
    uint64_t total_frame_count;
    uint64_t total_encoded_bytes;
    uint32_t* previous_contraction;
    uint32_t previous_contraction_count;
    uint32_t previous_contraction_capacity;
    uint32_t previous_normalization[3];
    uint16_t current_kind;
    uint8_t has_previous_contraction;
    uint8_t has_previous_normalization;
    uint8_t manifest_seen;
    uint8_t finished;
    laplace_unicode_status status;
    laplace_unicode_root_manifest manifest;
    laplace_unicode_root_stream_summary summary;
};

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

static int ascii_token_valid(
    const uint8_t* bytes,
    size_t byte_count,
    int allow_hyphen) {
    size_t index;
    if (bytes == NULL || byte_count == 0u) {
        return 0;
    }
    for (index = 0u; index < byte_count; ++index) {
        const uint8_t value = bytes[index];
        if (!((value >= (uint8_t)'A' && value <= (uint8_t)'Z') ||
              (value >= (uint8_t)'a' && value <= (uint8_t)'z') ||
              (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
              value == (uint8_t)'_' ||
              (allow_hyphen && value == (uint8_t)'-'))) {
            return 0;
        }
    }
    return 1;
}

static int bytes_compare(
    const uint8_t* left,
    size_t left_bytes,
    const uint8_t* right,
    size_t right_bytes) {
    const size_t common = left_bytes < right_bytes ? left_bytes : right_bytes;
    const int comparison = memcmp(left, right, common);
    if (comparison != 0) {
        return comparison;
    }
    if (left_bytes < right_bytes) {
        return -1;
    }
    return left_bytes > right_bytes ? 1 : 0;
}

static int ascii_rational_valid(const uint8_t* bytes, size_t byte_count) {
    size_t offset = 0u;
    size_t numerator_start;
    size_t denominator_start;
    if (byte_count == 0u) {
        return 1;
    }
    if (bytes == NULL) {
        return 0;
    }
    if (bytes[offset] == (uint8_t)'-') {
        ++offset;
    }
    numerator_start = offset;
    while (offset < byte_count && bytes[offset] >= (uint8_t)'0' &&
           bytes[offset] <= (uint8_t)'9') {
        ++offset;
    }
    if (offset == numerator_start ||
        (offset - numerator_start > 1u &&
         bytes[numerator_start] == (uint8_t)'0')) {
        return 0;
    }
    if (offset == byte_count) {
        return 1;
    }
    if (bytes[offset] != (uint8_t)'/') {
        return 0;
    }
    ++offset;
    denominator_start = offset;
    while (offset < byte_count && bytes[offset] >= (uint8_t)'0' &&
           bytes[offset] <= (uint8_t)'9') {
        ++offset;
    }
    return offset == byte_count && offset != denominator_start &&
        !(offset - denominator_start > 1u &&
          bytes[denominator_start] == (uint8_t)'0') &&
        !(offset - denominator_start == 1u &&
          bytes[denominator_start] == (uint8_t)'0');
}

static int position_sequence_valid(
    const uint8_t* bytes,
    size_t byte_count) {
    size_t offset;
    if ((byte_count % 4u) != 0u || (bytes == NULL && byte_count != 0u)) {
        return 0;
    }
    for (offset = 0u; offset < byte_count; offset += 4u) {
        if (read_u32le(bytes + offset) >= LAPLACE_UNICODE_ROOT_POPULATION) {
            return 0;
        }
    }
    return 1;
}

static int tagged_position_sequence_valid(
    const uint8_t* bytes,
    size_t byte_count) {
    uint16_t tag_bytes;
    uint32_t position_count;
    uint64_t required;
    if (byte_count == 0u) {
        return 1;
    }
    if (bytes == NULL || byte_count < 8u) {
        return 0;
    }
    tag_bytes = read_u16le(bytes);
    position_count = read_u32le(bytes + 4u);
    required = 8u + (uint64_t)tag_bytes + (uint64_t)position_count * 4u;
    return read_u16le(bytes + 2u) == 0u && tag_bytes != 0u &&
        position_count != 0u && required == byte_count &&
        ascii_token_valid(bytes + 8u, tag_bytes, 1) &&
        position_sequence_valid(
            bytes + 8u + tag_bytes, (size_t)position_count * 4u);
}

static int tagged_positions_valid(const uint8_t* bytes, size_t byte_count) {
    uint32_t count;
    uint32_t index;
    uint8_t prior_tag = 0u;
    if (byte_count == 0u) {
        return 1;
    }
    if (bytes == NULL || byte_count < 4u ||
        ((byte_count - 4u) % 8u) != 0u) {
        return 0;
    }
    count = read_u32le(bytes);
    if (count == 0u || (uint64_t)count * 8u + 4u != byte_count) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        const uint8_t* entry = bytes + 4u + (size_t)index * 8u;
        const uint8_t tag = entry[0];
        if (tag < 1u || tag > 3u || tag <= prior_tag || entry[1] != 0u ||
            entry[2] != 0u || entry[3] != 0u ||
            read_u32le(entry + 4u) >= LAPLACE_UNICODE_ROOT_POPULATION) {
            return 0;
        }
        prior_tag = tag;
    }
    return 1;
}

static int ascii_set_valid(
    const uint8_t* bytes,
    size_t byte_count,
    int key_value) {
    uint32_t count;
    uint32_t index;
    size_t offset = 4u;
    const uint8_t* prior = NULL;
    size_t prior_bytes = 0u;
    if (byte_count == 0u) {
        return 1;
    }
    if (bytes == NULL || byte_count < 4u) {
        return 0;
    }
    count = read_u32le(bytes);
    if (count == 0u) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        uint16_t key_bytes;
        uint16_t value_bytes = 0u;
        const uint8_t* key;
        if (offset > byte_count || byte_count - offset < 2u) {
            return 0;
        }
        key_bytes = read_u16le(bytes + offset);
        offset += 2u;
        if (key_value) {
            if (byte_count - offset < 2u) {
                return 0;
            }
            value_bytes = read_u16le(bytes + offset);
            offset += 2u;
        }
        if (key_bytes == 0u || byte_count - offset <
                (size_t)key_bytes + (size_t)value_bytes) {
            return 0;
        }
        key = bytes + offset;
        if (!ascii_token_valid(key, key_bytes, 1) ||
            (prior != NULL && bytes_compare(
                prior, prior_bytes, key, key_bytes) >= 0)) {
            return 0;
        }
        offset += key_bytes;
        if (key_value &&
            (value_bytes == 0u ||
             !ascii_token_valid(bytes + offset, value_bytes, 1))) {
            return 0;
        }
        offset += value_bytes;
        prior = key;
        prior_bytes = key_bytes;
    }
    return offset == byte_count;
}

static int case_folding_valid(const uint8_t* bytes, size_t byte_count) {
    uint32_t count;
    uint32_t index;
    size_t offset = 4u;
    uint8_t prior_status = 0u;
    if (byte_count == 0u) {
        return 1;
    }
    if (bytes == NULL || byte_count < 4u) {
        return 0;
    }
    count = read_u32le(bytes);
    if (count == 0u) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        uint32_t position_count;
        uint64_t positions_bytes;
        uint8_t status;
        if (offset > byte_count || byte_count - offset < 8u) {
            return 0;
        }
        status = bytes[offset];
        position_count = read_u32le(bytes + offset + 4u);
        positions_bytes = (uint64_t)position_count * 4u;
        if ((status != (uint8_t)'C' && status != (uint8_t)'F' &&
             status != (uint8_t)'S' && status != (uint8_t)'T') ||
            status <= prior_status || bytes[offset + 1u] != 0u ||
            bytes[offset + 2u] != 0u || bytes[offset + 3u] != 0u ||
            position_count == 0u || positions_bytes > byte_count - offset - 8u ||
            !position_sequence_valid(
                bytes + offset + 8u, (size_t)positions_bytes)) {
            return 0;
        }
        prior_status = status;
        offset += 8u + (size_t)positions_bytes;
    }
    return offset == byte_count;
}

static int full_case_mappings_valid(
    const uint8_t* bytes,
    size_t byte_count) {
    uint32_t count;
    uint32_t index;
    size_t offset = 4u;
    const uint8_t* prior = NULL;
    size_t prior_bytes = 0u;
    if (byte_count == 0u) {
        return 1;
    }
    if (bytes == NULL || byte_count < 4u) {
        return 0;
    }
    count = read_u32le(bytes);
    if (count == 0u) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        const size_t entry_start = offset;
        uint16_t condition_count;
        uint32_t lower_count;
        uint32_t title_count;
        uint32_t upper_count;
        uint64_t mapping_bytes;
        uint16_t condition;
        const uint8_t* prior_condition = NULL;
        size_t prior_condition_bytes = 0u;
        if (offset > byte_count || byte_count - offset < 16u) {
            return 0;
        }
        condition_count = read_u16le(bytes + offset);
        lower_count = read_u32le(bytes + offset + 4u);
        title_count = read_u32le(bytes + offset + 8u);
        upper_count = read_u32le(bytes + offset + 12u);
        mapping_bytes = ((uint64_t)lower_count + title_count + upper_count) * 4u;
        if (read_u16le(bytes + offset + 2u) != 0u ||
            mapping_bytes > byte_count - offset - 16u) {
            return 0;
        }
        offset += 16u;
        if (!position_sequence_valid(bytes + offset, (size_t)mapping_bytes)) {
            return 0;
        }
        offset += (size_t)mapping_bytes;
        for (condition = 0u; condition < condition_count; ++condition) {
            uint16_t condition_bytes;
            const uint8_t* value;
            if (offset > byte_count || byte_count - offset < 2u) {
                return 0;
            }
            condition_bytes = read_u16le(bytes + offset);
            offset += 2u;
            if (condition_bytes == 0u || byte_count - offset < condition_bytes) {
                return 0;
            }
            value = bytes + offset;
            if (!ascii_token_valid(value, condition_bytes, 1) ||
                (prior_condition != NULL && bytes_compare(
                    prior_condition, prior_condition_bytes,
                    value, condition_bytes) >= 0)) {
                return 0;
            }
            prior_condition = value;
            prior_condition_bytes = condition_bytes;
            offset += condition_bytes;
        }
        if (prior != NULL) {
            const size_t entry_bytes = offset - entry_start;
            if (bytes_compare(
                    prior, prior_bytes,
                    bytes + entry_start, entry_bytes) >= 0) {
                return 0;
            }
        }
        prior = bytes + entry_start;
        prior_bytes = offset - entry_start;
    }
    return offset == byte_count;
}

static int field_payload_valid(const laplace_unicode_atom_field* field) {
#if defined(LAPLACE_TEST_SKIP_UNICODE_FIELD_PAYLOAD_VALIDATION)
    if (field != NULL) {
        return 1;
    }
#endif
    switch (field->payload_kind) {
        case LAPLACE_UNICODE_PAYLOAD_ASCII_PROPERTY:
            return ascii_token_valid(
                field->payload, field->payload_bytes, 1);
        case LAPLACE_UNICODE_PAYLOAD_U8:
            return field->payload_bytes == 1u;
        case LAPLACE_UNICODE_PAYLOAD_OPTIONAL_POSITION_AND_ASCII_TYPE:
            return field->payload_bytes == 0u ||
                (field->payload_bytes == 5u &&
                 read_u32le(field->payload) < LAPLACE_UNICODE_ROOT_POPULATION &&
                 ascii_token_valid(field->payload + 4u, 1u, 0));
        case LAPLACE_UNICODE_PAYLOAD_OPTIONAL_POSITION:
            return field->payload_bytes == 0u ||
                (field->payload_bytes == 4u &&
                 read_u32le(field->payload) < LAPLACE_UNICODE_ROOT_POPULATION);
        case LAPLACE_UNICODE_PAYLOAD_POSITION_SEQUENCE:
            return position_sequence_valid(
                field->payload, field->payload_bytes);
        case LAPLACE_UNICODE_PAYLOAD_TAGGED_POSITION_SEQUENCE:
            return tagged_position_sequence_valid(
                field->payload, field->payload_bytes);
        case LAPLACE_UNICODE_PAYLOAD_ASCII_RATIONAL:
            return ascii_rational_valid(
                field->payload, field->payload_bytes);
        case LAPLACE_UNICODE_PAYLOAD_SORTED_TAGGED_POSITIONS:
            return tagged_positions_valid(
                field->payload, field->payload_bytes);
        case LAPLACE_UNICODE_PAYLOAD_FULL_CASE_MAPPINGS:
            return full_case_mappings_valid(
                field->payload, field->payload_bytes);
        case LAPLACE_UNICODE_PAYLOAD_CASE_FOLDING:
            return case_folding_valid(
                field->payload, field->payload_bytes);
        case LAPLACE_UNICODE_PAYLOAD_SORTED_ASCII_SET:
            return ascii_set_valid(
                field->payload, field->payload_bytes, 0);
        case LAPLACE_UNICODE_PAYLOAD_SORTED_ASCII_KEY_VALUE_SET:
            return ascii_set_valid(
                field->payload, field->payload_bytes, 1);
        case LAPLACE_UNICODE_PAYLOAD_BOOLEAN:
            return field->payload_bytes == 1u && field->payload[0] <= 1u;
        default:
            return 0;
    }
}

static int field_shape_valid(const laplace_unicode_atom_field* field, size_t index) {
    if (field->field_id != index + 1u ||
        field->payload_kind != expected_payload_kind[index] ||
        field->flags != 0u ||
        (field->payload == NULL && field->payload_bytes != 0u)) {
        return 0;
    }
    return field_payload_valid(field);
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

static int unicode_digest_equal(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static void unicode_root_section_hasher_initialize(
    blake3_hasher* hasher,
    uint16_t kind) {
    uint8_t encoded_kind[2];
    write_u16le(encoded_kind, kind);
    blake3_hasher_init(hasher);
    blake3_hasher_update(
        hasher, unicode_root_section_domain,
        sizeof(unicode_root_section_domain) - 1u);
    blake3_hasher_update(hasher, encoded_kind, sizeof(encoded_kind));
}

static void unicode_root_section_hasher_update(
    blake3_hasher* hasher,
    uint64_t section_ordinal,
    const uint8_t* payload,
    uint32_t payload_bytes) {
    uint8_t header[12];
    write_u64le(header, section_ordinal);
    write_u32le(header + 8u, payload_bytes);
    blake3_hasher_update(hasher, header, sizeof(header));
    blake3_hasher_update(hasher, payload, payload_bytes);
}

static void unicode_root_section_hasher_finish(
    const blake3_hasher* hasher,
    uint64_t section_count,
    laplace_digest256* fingerprint) {
    blake3_hasher copy = *hasher;
    uint8_t encoded_count[8];
    write_u64le(encoded_count, section_count);
    blake3_hasher_update(&copy, encoded_count, sizeof(encoded_count));
    blake3_hasher_finalize(
        &copy, fingerprint->bytes, sizeof(fingerprint->bytes));
}

static laplace_unicode_status unicode_root_stream_poison(
    laplace_unicode_root_stream_validator* validator,
    laplace_unicode_status status) {
    validator->status = status;
    return status;
}

static int unicode_root_required_sections_complete(
    const laplace_unicode_root_stream_validator* validator,
    uint16_t next_kind) {
    if (next_kind > LAPLACE_UNICODE_ROOT_FRAME_ATOM &&
        validator->section_counts[0] != LAPLACE_UNICODE_ROOT_POPULATION) {
        return 0;
    }
    if (next_kind > LAPLACE_UNICODE_ROOT_FRAME_DUCET_POSITION &&
        validator->section_counts[1] != LAPLACE_UNICODE_ROOT_POPULATION) {
        return 0;
    }
    return 1;
}

static int unicode_root_contraction_order_valid(
    const laplace_unicode_root_stream_validator* validator,
    const laplace_unicode_ducet_contraction_view* current) {
    uint32_t index;
    uint32_t shared;
    if (validator->has_previous_contraction == 0u) {
        return 1;
    }
    shared = validator->previous_contraction_count < current->sequence_count
        ? validator->previous_contraction_count
        : current->sequence_count;
    for (index = 0u; index < shared; ++index) {
        const uint32_t prior = validator->previous_contraction[index];
        const uint32_t value = read_u32le(
            current->encoded_sequence + (size_t)index * 4u);
        if (prior < value) {
            return 1;
        }
        if (prior > value) {
            return 0;
        }
    }
    return validator->previous_contraction_count < current->sequence_count;
}

static laplace_unicode_status unicode_root_remember_contraction(
    laplace_unicode_root_stream_validator* validator,
    const laplace_unicode_ducet_contraction_view* current) {
    uint32_t index;
    if (current->sequence_count > validator->previous_contraction_capacity) {
        const size_t requested_bytes =
            (size_t)current->sequence_count *
            sizeof(*validator->previous_contraction);
        uint32_t* resized;
        if (current->sequence_count != 0u &&
            requested_bytes / sizeof(*validator->previous_contraction) !=
                (size_t)current->sequence_count) {
            return LAPLACE_UNICODE_SIZE_OVERFLOW;
        }
        resized = (uint32_t*)realloc(
            validator->previous_contraction, requested_bytes);
        if (resized == NULL) {
            return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
        }
        validator->previous_contraction = resized;
        validator->previous_contraction_capacity = current->sequence_count;
    }
    for (index = 0u; index < current->sequence_count; ++index) {
        validator->previous_contraction[index] = read_u32le(
            current->encoded_sequence + (size_t)index * 4u);
    }
    validator->previous_contraction_count = current->sequence_count;
    validator->has_previous_contraction = 1u;
    return LAPLACE_UNICODE_OK;
}

static int unicode_root_normalization_order_valid(
    const laplace_unicode_root_stream_validator* validator,
    const laplace_unicode_normalization_composition* current) {
    const uint32_t values[3] = {
        current->starter_position,
        current->combining_position,
        current->composite_position};
    size_t index;
    if (validator->has_previous_normalization == 0u) {
        return 1;
    }
    for (index = 0u; index < 3u; ++index) {
        if (validator->previous_normalization[index] < values[index]) {
            return 1;
        }
        if (validator->previous_normalization[index] > values[index]) {
            return 0;
        }
    }
    return 0;
}

static void unicode_root_remember_normalization(
    laplace_unicode_root_stream_validator* validator,
    const laplace_unicode_normalization_composition* current) {
    validator->previous_normalization[0] = current->starter_position;
    validator->previous_normalization[1] = current->combining_position;
    validator->previous_normalization[2] = current->composite_position;
    validator->has_previous_normalization = 1u;
}

static void unicode_root_calculate_section_fingerprints(
    const laplace_unicode_root_stream_validator* validator,
    laplace_digest256 fingerprints[4]) {
    size_t index;
    for (index = 0u; index < 4u; ++index) {
        unicode_root_section_hasher_finish(
            &validator->section_hashers[index],
            validator->section_counts[index], &fingerprints[index]);
    }
}

static int unicode_root_manifest_matches(
    const laplace_unicode_root_stream_validator* validator,
    const laplace_unicode_root_manifest* manifest,
    const laplace_digest256 section_fingerprints[4]) {
    return manifest->atom_count == validator->section_counts[0] &&
        manifest->ducet_position_count == validator->section_counts[1] &&
        manifest->ducet_contraction_count == validator->section_counts[2] &&
        manifest->normalization_composition_count ==
            validator->section_counts[3] &&
        validator->total_frame_count != UINT64_MAX &&
        manifest->total_frame_count == validator->total_frame_count + 1u &&
        unicode_digest_equal(
            &manifest->source_fingerprint,
            &validator->expectation.source_fingerprint) &&
        unicode_digest_equal(
            &manifest->recipe_fingerprint,
            &validator->expectation.recipe_fingerprint) &&
        unicode_digest_equal(
            &manifest->numeric_provider_receipt,
            &validator->expectation.numeric_provider_receipt) &&
        unicode_digest_equal(
            &manifest->stream_contract_fingerprint,
            &validator->expectation.stream_contract_fingerprint) &&
        unicode_digest_equal(
            &manifest->algorithmic_hangul_rule_fingerprint,
            &validator->expectation.algorithmic_hangul_rule_fingerprint) &&
        unicode_digest_equal(
            &manifest->atom_section_fingerprint, &section_fingerprints[0]) &&
        unicode_digest_equal(
            &manifest->ducet_position_section_fingerprint,
            &section_fingerprints[1]) &&
        unicode_digest_equal(
            &manifest->ducet_contraction_section_fingerprint,
            &section_fingerprints[2]) &&
        unicode_digest_equal(
            &manifest->normalization_composition_section_fingerprint,
            &section_fingerprints[3]);
}

laplace_unicode_status laplace_unicode_root_stream_validator_create(
    const laplace_unicode_root_stream_expectation* expectation,
    laplace_unicode_root_stream_validator** validator) {
    laplace_unicode_root_stream_validator* created;
    size_t index;
    if (expectation == NULL || validator == NULL || expectation->flags != 0u ||
        expectation->reserved != 0u) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    *validator = NULL;
    created = (laplace_unicode_root_stream_validator*)calloc(
        1u, sizeof(*created));
    if (created == NULL) {
        return LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE;
    }
    created->expectation = *expectation;
    created->current_kind = LAPLACE_UNICODE_ROOT_FRAME_ATOM;
    created->status = LAPLACE_UNICODE_OK;
    for (index = 0u; index < 4u; ++index) {
        unicode_root_section_hasher_initialize(
            &created->section_hashers[index], (uint16_t)(index + 1u));
    }
    *validator = created;
    return LAPLACE_UNICODE_OK;
}

static laplace_unicode_status unicode_root_stream_consume_frame(
    laplace_unicode_root_stream_validator* validator,
    const laplace_unicode_root_frame_view* frame) {
    laplace_unicode_status status;
    if (validator->manifest_seen != 0u ||
        frame->value.kind < validator->current_kind ||
        !unicode_root_required_sections_complete(
            validator, frame->value.kind)) {
        return unicode_root_stream_poison(
            validator, LAPLACE_UNICODE_STREAM_ORDER_INVALID);
    }
    if (frame->value.kind != LAPLACE_UNICODE_ROOT_FRAME_MANIFEST) {
        const size_t section_index = (size_t)frame->value.kind - 1u;
#if !defined(LAPLACE_TEST_SKIP_UNICODE_STREAM_SECTION_ORDINAL_VALIDATION)
        if (frame->value.section_ordinal !=
            validator->section_counts[section_index]) {
            return unicode_root_stream_poison(
                validator, LAPLACE_UNICODE_STREAM_ORDER_INVALID);
        }
#endif
        if (frame->value.kind == LAPLACE_UNICODE_ROOT_FRAME_DUCET_CONTRACTION) {
            laplace_unicode_ducet_contraction_view contraction;
            size_t consumed = 0u;
            status = laplace_unicode_ducet_contraction_open(
                frame->value.payload, frame->value.payload_bytes,
                &contraction, &consumed);
            if (status != LAPLACE_UNICODE_OK ||
                !unicode_root_contraction_order_valid(
                    validator, &contraction)) {
                return unicode_root_stream_poison(
                    validator, LAPLACE_UNICODE_STREAM_ORDER_INVALID);
            }
            status = unicode_root_remember_contraction(
                validator, &contraction);
            if (status != LAPLACE_UNICODE_OK) {
                return unicode_root_stream_poison(validator, status);
            }
        } else if (frame->value.kind ==
                   LAPLACE_UNICODE_ROOT_FRAME_NORMALIZATION_COMPOSITION) {
            laplace_unicode_normalization_composition composition;
            size_t consumed = 0u;
            status = laplace_unicode_normalization_composition_open(
                frame->value.payload, frame->value.payload_bytes,
                &composition, &consumed);
            if (status != LAPLACE_UNICODE_OK ||
                !unicode_root_normalization_order_valid(
                    validator, &composition)) {
                return unicode_root_stream_poison(
                    validator, LAPLACE_UNICODE_STREAM_ORDER_INVALID);
            }
            unicode_root_remember_normalization(validator, &composition);
        }
        unicode_root_section_hasher_update(
            &validator->section_hashers[section_index],
            frame->value.section_ordinal, frame->value.payload,
            frame->value.payload_bytes);
        ++validator->section_counts[section_index];
        validator->current_kind = frame->value.kind;
    } else {
        laplace_unicode_root_manifest manifest;
        laplace_digest256 section_fingerprints[4];
        size_t consumed = 0u;
        status = laplace_unicode_root_manifest_open(
            frame->value.payload, frame->value.payload_bytes,
            &manifest, &consumed);
        unicode_root_calculate_section_fingerprints(
            validator, section_fingerprints);
        if (status != LAPLACE_UNICODE_OK ||
            !unicode_root_manifest_matches(
                validator, &manifest, section_fingerprints)) {
            return unicode_root_stream_poison(
                validator, LAPLACE_UNICODE_STREAM_MANIFEST_MISMATCH);
        }
        validator->manifest = manifest;
        validator->manifest_seen = 1u;
        validator->current_kind = frame->value.kind;
    }
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_root_stream_validator_consume(
    laplace_unicode_root_stream_validator* validator,
    const uint8_t* canonical_bytes,
    size_t byte_count,
    uint64_t frame_count,
    uint64_t first_frame_ordinal) {
    uint64_t frame_index;
    size_t offset = 0u;
    if (validator == NULL || canonical_bytes == NULL || byte_count == 0u ||
        frame_count == 0u) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (validator->status != LAPLACE_UNICODE_OK || validator->finished != 0u ||
        first_frame_ordinal != validator->total_frame_count) {
        return unicode_root_stream_poison(
            validator, LAPLACE_UNICODE_STREAM_STATE_INVALID);
    }
    for (frame_index = 0u; frame_index < frame_count; ++frame_index) {
        laplace_unicode_root_frame_view frame;
        size_t consumed = 0u;
        laplace_unicode_status status;
        if (offset >= byte_count) {
            return unicode_root_stream_poison(
                validator, LAPLACE_UNICODE_STREAM_STATE_INVALID);
        }
        status = laplace_unicode_root_frame_open(
            canonical_bytes + offset, byte_count - offset, &frame, &consumed);
        if (status != LAPLACE_UNICODE_OK || consumed == 0u ||
            consumed > byte_count - offset) {
            return unicode_root_stream_poison(
                validator, LAPLACE_UNICODE_STREAM_STATE_INVALID);
        }
        status = unicode_root_stream_consume_frame(validator, &frame);
        if (status != LAPLACE_UNICODE_OK) {
            return status;
        }
        if (UINT64_MAX - validator->total_frame_count < 1u ||
            UINT64_MAX - validator->total_encoded_bytes < consumed) {
            return unicode_root_stream_poison(
                validator, LAPLACE_UNICODE_SIZE_OVERFLOW);
        }
        ++validator->total_frame_count;
        validator->total_encoded_bytes += (uint64_t)consumed;
        offset += consumed;
    }
    if (offset != byte_count) {
        return unicode_root_stream_poison(
            validator, LAPLACE_UNICODE_STREAM_STATE_INVALID);
    }
    return LAPLACE_UNICODE_OK;
}

laplace_unicode_status laplace_unicode_root_stream_validator_finish(
    laplace_unicode_root_stream_validator* validator,
    laplace_unicode_root_stream_summary* summary) {
    uint8_t manifest_bytes[LAPLACE_UNICODE_ROOT_MANIFEST_BYTES];
    uint8_t encoded_total_bytes[8];
    blake3_hasher hasher;
    size_t index;
    if (validator == NULL || summary == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (validator->finished != 0u) {
        *summary = validator->summary;
        return validator->status;
    }
    if (validator->status != LAPLACE_UNICODE_OK) {
        return validator->status;
    }
    if (validator->manifest_seen == 0u ||
        validator->total_frame_count != validator->manifest.total_frame_count) {
        return unicode_root_stream_poison(
            validator, LAPLACE_UNICODE_STREAM_INCOMPLETE);
    }
    memset(&validator->summary, 0, sizeof(validator->summary));
    validator->summary.manifest = validator->manifest;
    validator->summary.total_frame_count = validator->total_frame_count;
    validator->summary.total_encoded_bytes = validator->total_encoded_bytes;
    validator->summary.status = LAPLACE_UNICODE_OK;
    unicode_root_calculate_section_fingerprints(
        validator, validator->summary.section_fingerprints);
    for (index = 0u; index < 4u; ++index) {
        validator->summary.section_counts[index] =
            validator->section_counts[index];
    }
    if (laplace_unicode_root_manifest_encode(
            &validator->manifest, manifest_bytes) != LAPLACE_UNICODE_OK) {
        return unicode_root_stream_poison(
            validator, LAPLACE_UNICODE_STREAM_MANIFEST_MISMATCH);
    }
    write_u64le(encoded_total_bytes, validator->total_encoded_bytes);
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, unicode_root_validation_domain,
        sizeof(unicode_root_validation_domain) - 1u);
    blake3_hasher_update(&hasher, manifest_bytes, sizeof(manifest_bytes));
    blake3_hasher_update(
        &hasher, encoded_total_bytes, sizeof(encoded_total_bytes));
    for (index = 0u; index < 4u; ++index) {
        blake3_hasher_update(
            &hasher, validator->summary.section_fingerprints[index].bytes,
            sizeof(validator->summary.section_fingerprints[index].bytes));
    }
    blake3_hasher_finalize(
        &hasher, validator->summary.receipt_id.bytes,
        sizeof(validator->summary.receipt_id.bytes));
    validator->finished = 1u;
    *summary = validator->summary;
    return LAPLACE_UNICODE_OK;
}

void laplace_unicode_root_stream_validator_destroy(
    laplace_unicode_root_stream_validator* validator) {
    if (validator == NULL) {
        return;
    }
    free(validator->previous_contraction);
    memset(validator, 0, sizeof(*validator));
    free(validator);
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
