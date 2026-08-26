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
    LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE = 65536,
    LAPLACE_UNICODE_ATOM_RECORD_TYPE = 65537,
    LAPLACE_UNICODE_ROOT_FRAME_VERSION = 1,
    LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES = 32,
    LAPLACE_UNICODE_DUCET_POSITION_VERSION = 1,
    LAPLACE_UNICODE_DUCET_POSITION_HEADER_BYTES = 32,
    LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES = 8,
    LAPLACE_UNICODE_DUCET_CONTRACTION_VERSION = 1,
    LAPLACE_UNICODE_DUCET_CONTRACTION_HEADER_BYTES = 32,
    LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_VERSION = 1,
    LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES = 32,
    LAPLACE_UNICODE_ROOT_MANIFEST_VERSION = 1,
    LAPLACE_UNICODE_ROOT_MANIFEST_BYTES = 352,
    LAPLACE_UNICODE_ATOM_RECORD_VERSION = 1,
    LAPLACE_UNICODE_ATOM_HEADER_BYTES = 132,
    LAPLACE_UNICODE_ATOM_FIELD_COUNT = 26,
    LAPLACE_UNICODE_CORE_FIELD_COUNT = LAPLACE_UNICODE_ATOM_FIELD_COUNT,
    LAPLACE_UNICODE_ATOM_FIELD_HEADER_BYTES = 8,
    LAPLACE_UNICODE_ROOT_POPULATION = 1114112,
    LAPLACE_UNICODE_HILBERT_KEY_BYTES = 16
};

typedef enum laplace_unicode_root_frame_kind {
    LAPLACE_UNICODE_ROOT_FRAME_ATOM = 1,
    LAPLACE_UNICODE_ROOT_FRAME_DUCET_POSITION = 2,
    LAPLACE_UNICODE_ROOT_FRAME_DUCET_CONTRACTION = 3,
    LAPLACE_UNICODE_ROOT_FRAME_NORMALIZATION_COMPOSITION = 4,
    LAPLACE_UNICODE_ROOT_FRAME_MANIFEST = 5
} laplace_unicode_root_frame_kind;

typedef enum laplace_unicode_ducet_provenance {
    LAPLACE_UNICODE_DUCET_EXPLICIT = 1,
    LAPLACE_UNICODE_DUCET_IMPLICIT = 2,
    LAPLACE_UNICODE_DUCET_HANGUL = 3,
    LAPLACE_UNICODE_DUCET_LUP_SURROGATE_EXTENSION = 4
} laplace_unicode_ducet_provenance;

typedef enum laplace_unicode_uca_alternate_handling {
    LAPLACE_UNICODE_UCA_NON_IGNORABLE = 0,
    LAPLACE_UNICODE_UCA_SHIFTED = 1
} laplace_unicode_uca_alternate_handling;

typedef struct laplace_unicode_collation_element {
    uint16_t primary;
    uint16_t secondary;
    uint16_t tertiary;
    uint8_t variable;
    uint8_t reserved;
} laplace_unicode_collation_element;

typedef struct laplace_unicode_ducet_position_record {
    const laplace_unicode_collation_element* elements;
    const uint8_t* equivalence_key;
    uint32_t codepoint_position;
    uint32_t element_count;
    uint32_t equivalence_key_bytes;
    uint8_t provenance;
    uint8_t reserved[3];
} laplace_unicode_ducet_position_record;

typedef struct laplace_unicode_ducet_position_view {
    const uint8_t* encoded_elements;
    const uint8_t* equivalence_key;
    const uint8_t* encoded_record;
    uint32_t codepoint_position;
    uint32_t element_count;
    uint32_t equivalence_key_bytes;
    uint32_t encoded_bytes;
    uint8_t provenance;
    uint8_t reserved[3];
} laplace_unicode_ducet_position_view;

typedef struct laplace_unicode_normalization_composition {
    uint32_t starter_position;
    uint32_t combining_position;
    uint32_t composite_position;
} laplace_unicode_normalization_composition;

typedef struct laplace_unicode_ducet_contraction_record {
    const uint32_t* sequence;
    const laplace_unicode_collation_element* elements;
    uint32_t source_line_ordinal;
    uint32_t sequence_count;
    uint32_t element_count;
} laplace_unicode_ducet_contraction_record;

typedef struct laplace_unicode_ducet_contraction_view {
    const uint8_t* encoded_sequence;
    const uint8_t* encoded_elements;
    const uint8_t* encoded_record;
    uint32_t source_line_ordinal;
    uint32_t sequence_count;
    uint32_t element_count;
    uint32_t encoded_bytes;
} laplace_unicode_ducet_contraction_view;

typedef struct laplace_unicode_root_manifest {
    uint64_t atom_count;
    uint64_t ducet_position_count;
    uint64_t ducet_contraction_count;
    uint64_t normalization_composition_count;
    uint64_t total_frame_count;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 numeric_provider_receipt;
    laplace_digest256 stream_contract_fingerprint;
    laplace_digest256 atom_section_fingerprint;
    laplace_digest256 ducet_position_section_fingerprint;
    laplace_digest256 ducet_contraction_section_fingerprint;
    laplace_digest256 normalization_composition_section_fingerprint;
    laplace_digest256 algorithmic_hangul_rule_fingerprint;
} laplace_unicode_root_manifest;

typedef struct laplace_unicode_root_frame {
    const uint8_t* payload;
    uint64_t section_ordinal;
    uint32_t payload_bytes;
    uint16_t kind;
    uint16_t flags;
} laplace_unicode_root_frame;

typedef struct laplace_unicode_root_frame_view {
    laplace_unicode_root_frame value;
    const uint8_t* encoded_frame;
    uint32_t encoded_bytes;
} laplace_unicode_root_frame_view;

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
    LAPLACE_UNICODE_PAYLOAD_BOOLEAN = 13,
    LAPLACE_UNICODE_PAYLOAD_NORMALIZATION_PROPERTIES = 14
} laplace_unicode_payload_kind;

typedef enum laplace_unicode_normalization_value_kind {
    LAPLACE_UNICODE_NORMALIZATION_BINARY_TRUE = 1,
    LAPLACE_UNICODE_NORMALIZATION_ASCII_PROPERTY_VALUE = 2,
    LAPLACE_UNICODE_NORMALIZATION_POSITION_SEQUENCE = 3,
    LAPLACE_UNICODE_NORMALIZATION_EMPTY_POSITION_SEQUENCE = 4
} laplace_unicode_normalization_value_kind;

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

typedef struct laplace_unicode_source_bundle laplace_unicode_source_bundle;
typedef struct laplace_unicode_core_table laplace_unicode_core_table;
typedef struct laplace_unicode_ducet_table laplace_unicode_ducet_table;

typedef struct laplace_unicode_source_file_view {
    const uint8_t* bytes;
    uint64_t byte_count;
} laplace_unicode_source_file_view;

typedef struct laplace_unicode_core_record_view {
    laplace_unicode_atom_field fields[LAPLACE_UNICODE_CORE_FIELD_COUNT];
    uint32_t codepoint_position;
    uint8_t position_class;
    uint8_t reserved[3];
} laplace_unicode_core_record_view;

typedef struct laplace_unicode_core_summary {
    laplace_digest256 receipt_id;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 normalized_fingerprint;
    uint64_t position_class_counts[5];
    uint64_t explicit_position_count;
    uint64_t canonical_decomposition_count;
    uint64_t compatibility_decomposition_count;
    uint64_t simple_case_mapping_position_count;
    uint64_t field_source_row_counts[LAPLACE_UNICODE_ATOM_FIELD_COUNT];
    uint64_t field_explicit_position_counts[LAPLACE_UNICODE_ATOM_FIELD_COUNT];
    uint64_t field_membership_counts[LAPLACE_UNICODE_ATOM_FIELD_COUNT];
    laplace_digest256 complete_property_fingerprint;
    uint32_t unicode_data_row_count;
    uint32_t unicode_data_range_count;
    uint32_t bidi_range_count;
    uint32_t bidi_missing_rule_count;
    uint32_t status;
    uint32_t reserved;
} laplace_unicode_core_summary;

typedef struct laplace_unicode_ducet_mapping_view {
    const uint32_t* sequence;
    const laplace_unicode_collation_element* elements;
    uint32_t source_line_ordinal;
    uint32_t sequence_count;
    uint32_t element_count;
} laplace_unicode_ducet_mapping_view;

typedef struct laplace_unicode_ducet_implicit_range_view {
    uint32_t first_position;
    uint32_t last_position;
    uint16_t lead_primary;
    uint16_t reserved;
    uint32_t source_line_ordinal;
} laplace_unicode_ducet_implicit_range_view;

typedef struct laplace_unicode_ducet_summary {
    laplace_digest256 receipt_id;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 retained_table_fingerprint;
    uint64_t explicit_mapping_count;
    uint64_t explicit_single_position_count;
    uint64_t contraction_count;
    uint64_t expansion_mapping_count;
    uint64_t collation_element_count;
    uint64_t variable_collation_element_count;
    uint32_t implicit_range_count;
    uint32_t maximum_sequence_count;
    uint32_t maximum_element_count;
    uint32_t status;
} laplace_unicode_ducet_summary;

typedef struct laplace_unicode_root_stream_validator
    laplace_unicode_root_stream_validator;

typedef struct laplace_unicode_root_stream_expectation {
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 numeric_provider_receipt;
    laplace_digest256 stream_contract_fingerprint;
    laplace_digest256 algorithmic_hangul_rule_fingerprint;
    uint32_t flags;
    uint32_t reserved;
} laplace_unicode_root_stream_expectation;

typedef struct laplace_unicode_root_stream_summary {
    laplace_digest256 receipt_id;
    laplace_unicode_root_manifest manifest;
    laplace_digest256 section_fingerprints[4];
    uint64_t section_counts[4];
    uint64_t total_frame_count;
    uint64_t total_encoded_bytes;
    uint32_t status;
    uint32_t reserved;
} laplace_unicode_root_stream_summary;

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
    LAPLACE_UNICODE_SOURCE_VERSION_MISMATCH = 14,
    LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE = 15,
    LAPLACE_UNICODE_STREAM_STATE_INVALID = 16,
    LAPLACE_UNICODE_STREAM_ORDER_INVALID = 17,
    LAPLACE_UNICODE_STREAM_MANIFEST_MISMATCH = 18,
    LAPLACE_UNICODE_STREAM_INCOMPLETE = 19,
    LAPLACE_UNICODE_SOURCE_SYNTAX_INVALID = 20,
    LAPLACE_UNICODE_SOURCE_CONFLICT = 21,
    LAPLACE_UNICODE_SOURCE_INCOMPLETE = 22
} laplace_unicode_status;

enum {
    LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MINOR = 0
};

typedef struct laplace_unicode_numeric_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 environment_fingerprint;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t first_rank;
    uint64_t rank_count;
    uint32_t threading_layer;
    uint32_t instruction_branch;
    uint32_t vml_status;
    uint32_t floating_exceptions;
    uint32_t system_error;
    uint32_t status;
    uint32_t reserved;
} laplace_unicode_numeric_receipt;

typedef laplace_unicode_status (*laplace_unicode_numeric_workspace_fn)(
    void* state,
    size_t rank_count,
    size_t* workspace_bytes);

typedef laplace_unicode_status (*laplace_unicode_numeric_calculate_fn)(
    void* state,
    uint32_t first_rank,
    size_t rank_count,
    void* workspace,
    size_t workspace_bytes,
    laplace_point4d* coordinates,
    laplace_unicode_hopf_point* hopf_points,
    laplace_unicode_numeric_receipt* receipt);

typedef struct laplace_unicode_numeric_provider_v1 {
    void* state;
    laplace_digest256 provider_fingerprint;
    laplace_unicode_numeric_workspace_fn workspace;
    laplace_unicode_numeric_calculate_fn calculate;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_unicode_numeric_provider_v1;

LAPLACE_API laplace_unicode_status laplace_unicode_atom_record_measure(
    const laplace_unicode_atom_record* record,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_atom_field_payload_kind(
    uint16_t field_id,
    uint8_t* payload_kind);

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

LAPLACE_API laplace_unicode_status laplace_unicode_root_frame_measure(
    const laplace_unicode_root_frame* frame,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_root_frame_encode(
    const laplace_unicode_root_frame* frame,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_root_frame_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_root_frame_view* view,
    size_t* consumed_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_position_measure(
    const laplace_unicode_ducet_position_record* record,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_position_encode(
    const laplace_unicode_ducet_position_record* record,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_position_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_ducet_position_view* view,
    size_t* consumed_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_position_element(
    const laplace_unicode_ducet_position_view* view,
    uint32_t element_index,
    laplace_unicode_collation_element* element);

LAPLACE_API laplace_unicode_status
laplace_unicode_normalization_composition_encode(
    const laplace_unicode_normalization_composition* record,
    uint8_t output[LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES]);

LAPLACE_API laplace_unicode_status
laplace_unicode_normalization_composition_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_normalization_composition* record,
    size_t* consumed_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_contraction_measure(
    const laplace_unicode_ducet_contraction_record* record,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_contraction_encode(
    const laplace_unicode_ducet_contraction_record* record,
    uint8_t* output,
    size_t output_capacity,
    size_t* encoded_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_contraction_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_ducet_contraction_view* view,
    size_t* consumed_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_contraction_position(
    const laplace_unicode_ducet_contraction_view* view,
    uint32_t position_index,
    uint32_t* codepoint_position);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_contraction_element(
    const laplace_unicode_ducet_contraction_view* view,
    uint32_t element_index,
    laplace_unicode_collation_element* element);

LAPLACE_API laplace_unicode_status laplace_unicode_root_manifest_encode(
    const laplace_unicode_root_manifest* manifest,
    uint8_t output[LAPLACE_UNICODE_ROOT_MANIFEST_BYTES]);

LAPLACE_API laplace_unicode_status laplace_unicode_root_manifest_open(
    const uint8_t* encoded,
    size_t available_bytes,
    laplace_unicode_root_manifest* manifest,
    size_t* consumed_bytes);

LAPLACE_API laplace_unicode_status
laplace_unicode_root_stream_validator_create(
    const laplace_unicode_root_stream_expectation* expectation,
    laplace_unicode_root_stream_validator** validator);

LAPLACE_API laplace_unicode_status
laplace_unicode_root_stream_validator_consume(
    laplace_unicode_root_stream_validator* validator,
    const uint8_t* canonical_bytes,
    size_t byte_count,
    uint64_t frame_count,
    uint64_t first_frame_ordinal);

LAPLACE_API laplace_unicode_status
laplace_unicode_root_stream_validator_finish(
    laplace_unicode_root_stream_validator* validator,
    laplace_unicode_root_stream_summary* summary);

LAPLACE_API void laplace_unicode_root_stream_validator_destroy(
    laplace_unicode_root_stream_validator* validator);

LAPLACE_API laplace_unicode_status laplace_unicode_quantize_component_u32(
    double component,
    uint32_t* quantized);

LAPLACE_API laplace_unicode_status laplace_unicode_hilbert4_encode(
    const uint32_t axes[4],
    uint8_t key[LAPLACE_UNICODE_HILBERT_KEY_BYTES]);

LAPLACE_API laplace_unicode_status laplace_unicode_source_verify(
    const char* source_root,
    laplace_unicode_source_receipt* receipt);

LAPLACE_API laplace_unicode_status laplace_unicode_source_bundle_open(
    const char* source_root,
    laplace_unicode_source_bundle** bundle,
    laplace_unicode_source_receipt* receipt);

LAPLACE_API laplace_unicode_status laplace_unicode_source_bundle_file(
    const laplace_unicode_source_bundle* bundle,
    const char* relative_path,
    laplace_unicode_source_file_view* view);

LAPLACE_API laplace_unicode_status laplace_unicode_source_bundle_receipt(
    const laplace_unicode_source_bundle* bundle,
    laplace_unicode_source_receipt* receipt);

LAPLACE_API void laplace_unicode_source_bundle_close(
    laplace_unicode_source_bundle** bundle);

LAPLACE_API laplace_unicode_status laplace_unicode_core_table_create(
    const laplace_unicode_source_bundle* bundle,
    laplace_unicode_core_table** table,
    laplace_unicode_core_summary* summary);

LAPLACE_API laplace_unicode_status laplace_unicode_core_table_record(
    const laplace_unicode_core_table* table,
    uint32_t codepoint_position,
    laplace_unicode_core_record_view* view);

LAPLACE_API void laplace_unicode_core_table_destroy(
    laplace_unicode_core_table** table);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_table_create(
    const laplace_unicode_source_bundle* bundle,
    laplace_unicode_ducet_table** table,
    laplace_unicode_ducet_summary* summary);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_table_mapping(
    const laplace_unicode_ducet_table* table,
    uint64_t mapping_ordinal,
    laplace_unicode_ducet_mapping_view* view);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_table_lookup(
    const laplace_unicode_ducet_table* table,
    const uint32_t* sequence,
    uint32_t sequence_count,
    laplace_unicode_ducet_mapping_view* view);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_table_implicit_range(
    const laplace_unicode_ducet_table* table,
    uint32_t range_ordinal,
    laplace_unicode_ducet_implicit_range_view* view);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_sort_key_measure(
    const laplace_unicode_ducet_table* table,
    const laplace_unicode_core_table* core,
    const uint32_t* sequence,
    uint32_t sequence_count,
    uint8_t alternate_handling,
    uint32_t* normalized_position_count,
    uint32_t* collation_element_count,
    size_t* key_bytes);

LAPLACE_API laplace_unicode_status laplace_unicode_ducet_sort_key_calculate(
    const laplace_unicode_ducet_table* table,
    const laplace_unicode_core_table* core,
    const uint32_t* sequence,
    uint32_t sequence_count,
    uint8_t alternate_handling,
    uint32_t* normalized_positions,
    uint32_t normalized_capacity,
    laplace_unicode_collation_element* elements,
    uint32_t element_capacity,
    uint8_t* key,
    size_t key_capacity,
    uint8_t* provenance,
    size_t* key_bytes);

LAPLACE_API void laplace_unicode_ducet_table_destroy(
    laplace_unicode_ducet_table** table);

LAPLACE_API laplace_unicode_status laplace_unicode_numeric_oneapi_provider(
    laplace_unicode_numeric_provider_v1* provider);

#ifdef __cplusplus
}
#endif

#endif
