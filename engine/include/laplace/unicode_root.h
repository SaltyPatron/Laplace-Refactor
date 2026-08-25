#ifndef LAPLACE_UNICODE_ROOT_H
#define LAPLACE_UNICODE_ROOT_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/geometry.h"
#include "laplace/identity.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_UNICODE_ATOM_RECORD_TYPE = 65537,
    LAPLACE_UNICODE_ATOM_RECORD_VERSION = 1,
    LAPLACE_UNICODE_ATOM_HEADER_BYTES = 132,
    LAPLACE_UNICODE_ATOM_FIELD_COUNT = 26,
    LAPLACE_UNICODE_ATOM_FIELD_HEADER_BYTES = 8,
    LAPLACE_UNICODE_ROOT_POPULATION = 1114112,
    LAPLACE_UNICODE_HILBERT_KEY_BYTES = 16
};

typedef enum laplace_unicode_position_class {
    LAPLACE_UNICODE_ASSIGNED_SCALAR = 0,
    LAPLACE_UNICODE_UNASSIGNED_OR_RESERVED_SCALAR = 1,
    LAPLACE_UNICODE_PRIVATE_USE_SCALAR = 2,
    LAPLACE_UNICODE_NONCHARACTER_SCALAR = 3,
    LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS = 4
} laplace_unicode_position_class;

typedef enum laplace_unicode_payload_kind {
    LAPLACE_UNICODE_PAYLOAD_ASCII_PROPERTY = 1,
    LAPLACE_UNICODE_PAYLOAD_U8 = 2,
    LAPLACE_UNICODE_PAYLOAD_OPTIONAL_POSITION_AND_ASCII_TYPE = 3,
    LAPLACE_UNICODE_PAYLOAD_OPTIONAL_POSITION = 4,
    LAPLACE_UNICODE_PAYLOAD_POSITION_SEQUENCE = 5,
    LAPLACE_UNICODE_PAYLOAD_TAGGED_POSITION_SEQUENCE = 6,
    LAPLACE_UNICODE_PAYLOAD_ASCII_RATIONAL = 7,
    LAPLACE_UNICODE_PAYLOAD_SORTED_TAGGED_POSITIONS = 8,
    LAPLACE_UNICODE_PAYLOAD_FULL_CASE_MAPPINGS = 9,
    LAPLACE_UNICODE_PAYLOAD_CASE_FOLDING = 10,
    LAPLACE_UNICODE_PAYLOAD_SORTED_ASCII_SET = 11,
    LAPLACE_UNICODE_PAYLOAD_SORTED_ASCII_KEY_VALUE_SET = 12,
    LAPLACE_UNICODE_PAYLOAD_BOOLEAN = 13
} laplace_unicode_payload_kind;

typedef struct laplace_unicode_atom_field {
    const uint8_t* payload;
    uint32_t payload_bytes;
    uint16_t field_id;
    uint8_t payload_kind;
    uint8_t flags;
} laplace_unicode_atom_field;

typedef struct laplace_unicode_atom_record {
    uint32_t codepoint_position;
    uint32_t placement_rank;
    uint8_t position_class;
    uint8_t lup_v1_length;
    uint8_t lup_v1_bytes[4];
    laplace_id128 content_id;
    laplace_digest256 identity_preimage_fingerprint;
    laplace_point4d coordinate;
    uint8_t hilbert_key[LAPLACE_UNICODE_HILBERT_KEY_BYTES];
    laplace_unicode_atom_field fields[LAPLACE_UNICODE_ATOM_FIELD_COUNT];
} laplace_unicode_atom_record;

typedef struct laplace_unicode_atom_record_view {
    laplace_unicode_atom_record value;
    const uint8_t* encoded_record;
    uint32_t encoded_bytes;
} laplace_unicode_atom_record_view;

typedef struct laplace_unicode_hopf_point {
    double component[3];
} laplace_unicode_hopf_point;

typedef struct laplace_unicode_source_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 verified_file_set_fingerprint;
    uint64_t total_source_bytes;
    uint32_t verified_file_count;
    uint32_t status;
} laplace_unicode_source_receipt;

typedef enum laplace_unicode_status {
    LAPLACE_UNICODE_OK = 0,
    LAPLACE_UNICODE_INVALID_ARGUMENT = 1,
    LAPLACE_UNICODE_POSITION_OUT_OF_RANGE = 2,
    LAPLACE_UNICODE_RECORD_INVALID = 3,
    LAPLACE_UNICODE_IDENTITY_MISMATCH = 4,
    LAPLACE_UNICODE_FIELD_INVALID = 5,
    LAPLACE_UNICODE_BUFFER_TOO_SMALL = 6,
    LAPLACE_UNICODE_SIZE_OVERFLOW = 7,
    LAPLACE_UNICODE_NUMERIC_OUT_OF_RANGE = 8,
    LAPLACE_UNICODE_PROVIDER_UNAVAILABLE = 9,
    LAPLACE_UNICODE_PROVIDER_FAILURE = 10,
    LAPLACE_UNICODE_SOURCE_ROOT_INVALID = 11,
    LAPLACE_UNICODE_SOURCE_FILE_INVALID = 12,
    LAPLACE_UNICODE_SOURCE_DIGEST_MISMATCH = 13,
    LAPLACE_UNICODE_SOURCE_VERSION_MISMATCH = 14
} laplace_unicode_status;

LAPLACE_API laplace_unicode_status laplace_unicode_atom_record_measure(
    const laplace_unicode_atom_record* record,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_atom_record_encode(
    const laplace_unicode_atom_record* record,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_atom_record_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_atom_record_view* view,
    size_t* consumed_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_quantize_component_u32(
    double component,
    uint32_t* quantized);

LAPLACE_API laplace_unicode_status laplace_unicode_hilbert4_encode(
    const uint32_t axes[4],
    uint8_t key[LAPLACE_UNICODE_HILBERT_KEY_BYTES]);

LAPLACE_API laplace_unicode_status laplace_unicode_source_verify(
    const char* source_root,
    laplace_unicode_source_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
