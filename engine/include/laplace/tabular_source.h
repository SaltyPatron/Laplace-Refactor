#ifndef LAPLACE_TABULAR_SOURCE_H
#define LAPLACE_TABULAR_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "laplace/contract/tabular_source.h"
#include "laplace/export.h"
#include "laplace/reference_mapping.h"
#include "laplace/reference_topology.h"
#include "laplace/source_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_tabular_artifact {
    laplace_digest256 artifact_id;
    laplace_digest256 parent_artifact_id;
    uint8_t expected_sha256[32];
    const uint8_t* bytes;
    const char* name;
    const char* media_type;
    const struct laplace_tabular_column* columns;
    uint64_t byte_count;
    uint64_t name_byte_count;
    uint64_t media_type_byte_count;
    uint64_t expected_record_count;
    uint64_t expected_field_count;
    uint64_t reference_column_mask;
    uint32_t mode;
    uint32_t delimiter;
    uint32_t line_terminator;
    uint32_t expected_column_count;
    uint32_t outcome_type;
    uint32_t header_record_count;
    uint32_t flags;
    uint32_t reserved;
    const struct laplace_tabular_fixed_width_field* fixed_width_fields;
    uint32_t padding_byte;
    uint32_t overflow_field_index;
    uint32_t maximum_overflow_bytes;
    uint32_t expected_overflow_record_count;
} laplace_tabular_artifact;

typedef struct laplace_tabular_column {
    const uint8_t* bytes;
    uint64_t byte_count;
} laplace_tabular_column;

typedef struct laplace_tabular_fixed_width_field {
    uint32_t width;
    uint32_t flags;
} laplace_tabular_fixed_width_field;

enum {
    LAPLACE_TABULAR_FIXED_WIDTH_TRIM_LEFT = 1u,
    LAPLACE_TABULAR_FIXED_WIDTH_TRIM_RIGHT = 2u,
    LAPLACE_TABULAR_FIXED_WIDTH_KNOWN_FIELD_FLAGS = 3u
};

#define LAPLACE_TABULAR_FIXED_WIDTH_NO_OVERFLOW_FIELD UINT32_MAX

enum {
    LAPLACE_TABULAR_ARTIFACT_CONTAINER = 1u,
    LAPLACE_TABULAR_ARTIFACT_MEMBER = 2u,
    LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION = 4u
};

typedef struct laplace_tabular_reference_rule {
    laplace_id128 name_space;
    uint64_t artifact_index;
    uint64_t column_index;
    uint32_t kind;
    uint32_t flags;
} laplace_tabular_reference_rule;

typedef struct laplace_tabular_reference_occurrence {
    laplace_id128 name_space;
    uint64_t value_result_index;
    uint64_t field_result_index;
    uint64_t row_result_index;
    uint64_t source_ordinal;
    uint64_t artifact_ordinal;
    uint64_t row_ordinal;
    uint64_t column_ordinal;
    uint32_t kind;
    uint32_t rule_flags;
} laplace_tabular_reference_occurrence;

typedef struct laplace_tabular_mapping_rule {
    const uint8_t* relation_content;
    uint64_t relation_content_byte_count;
    uint64_t artifact_index;
    uint64_t left_column_index;
    uint64_t right_column_index;
    uint64_t relation_version;
    uint32_t relation_kind;
    uint32_t flags;
} laplace_tabular_mapping_rule;

typedef struct laplace_tabular_mapping_occurrence {
    uint64_t relation_result_index;
    uint64_t left_reference_occurrence_index;
    uint64_t right_reference_occurrence_index;
    uint64_t row_result_index;
    uint64_t source_ordinal;
    uint64_t artifact_ordinal;
    uint64_t row_ordinal;
    uint64_t relation_version;
    uint32_t relation_kind;
    uint32_t flags;
} laplace_tabular_mapping_occurrence;

/*
 * Structural grammar testimony retained by source admission.  canonical_content
 * names only the exact span bytes in the enclosing canonical working set.  The
 * remaining fields describe the parser/provider observation and therefore never
 * participate in entity or physicality identity.  media_type bytes live in the
 * plan view's decomposition_witness_media_types buffer at the declared offset.
 */
typedef struct laplace_tabular_decomposition_witness {
    laplace_digest256 trace_fingerprint;
    laplace_digest256 provider_fingerprint;
    laplace_composition_operand canonical_content;
    uint64_t artifact_index;
    uint64_t span_index;
    uint64_t parent_span_index;
    uint64_t byte_start;
    uint64_t byte_end;
    uint64_t kind;
    uint64_t media_type_byte_offset;
    uint64_t media_type_byte_count;
    uint32_t depth;
    uint32_t flags;
} laplace_tabular_decomposition_witness;

typedef struct laplace_tabular_source_input {
    laplace_source_profile_manifest profile_declaration;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    const laplace_tabular_artifact* artifacts;
    uint64_t artifact_count;
    const laplace_tabular_reference_rule* reference_rules;
    uint64_t reference_rule_count;
    const laplace_tabular_mapping_rule* mapping_rules;
    uint64_t mapping_rule_count;
    uint64_t preferred_batch_bytes;
    uint32_t flags;
    uint32_t reserved;
} laplace_tabular_source_input;

typedef struct laplace_tabular_source_plan laplace_tabular_source_plan;

typedef struct laplace_tabular_source_plan_view {
    laplace_source_profile_manifest profile;
    laplace_digest256 source_fingerprint;
    laplace_digest256 reconstruction_fingerprint;
    const uint32_t* atom_positions;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    const uint64_t* claim_result_indexes;
    const uint64_t* claim_source_ordinals;
    const uint32_t* claim_outcome_types;
    const uint64_t* artifact_root_result_indexes;
    const laplace_tabular_reference_occurrence* reference_occurrences;
    const laplace_tabular_mapping_occurrence* mapping_occurrences;
    const laplace_tabular_decomposition_witness* decomposition_witnesses;
    const uint8_t* decomposition_witness_media_types;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t claim_count;
    uint64_t artifact_count;
    uint64_t reference_occurrence_count;
    uint64_t mapping_occurrence_count;
    uint64_t decomposition_witness_count;
    uint64_t decomposition_witness_media_type_byte_count;
    uint64_t root_result_index;
    uint32_t recipe_version;
    uint32_t flags;
} laplace_tabular_source_plan_view;

typedef enum laplace_tabular_source_status {
    LAPLACE_TABULAR_SOURCE_OK = 0,
    LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT = 1,
    LAPLACE_TABULAR_SOURCE_PROFILE_INVALID = 2,
    LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID = 3,
    LAPLACE_TABULAR_SOURCE_DIGEST_MISMATCH = 4,
    LAPLACE_TABULAR_SOURCE_UTF8_INVALID = 5,
    LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID = 6,
    LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH = 7,
    LAPLACE_TABULAR_SOURCE_RECONSTRUCTION_MISMATCH = 8,
    LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE = 9,
    LAPLACE_TABULAR_SOURCE_OVERFLOW = 10,
    LAPLACE_TABULAR_SOURCE_RECONSTRUCTION_UNAVAILABLE = 11
} laplace_tabular_source_status;

LAPLACE_API laplace_tabular_source_status laplace_tabular_artifact_graph_identify(
    const laplace_tabular_artifact* artifacts,
    size_t artifact_count,
    laplace_digest256* artifact_graph_fingerprint);

LAPLACE_API laplace_tabular_source_status laplace_tabular_source_graph_identify(
    const laplace_tabular_artifact* artifacts,
    size_t artifact_count,
    const laplace_tabular_reference_rule* reference_rules,
    size_t reference_rule_count,
    const laplace_tabular_mapping_rule* mapping_rules,
    size_t mapping_rule_count,
    laplace_digest256* source_graph_fingerprint);

LAPLACE_API laplace_tabular_source_status laplace_tabular_source_plan_create(
    const laplace_tabular_source_input* input,
    laplace_tabular_source_plan** plan);

LAPLACE_API laplace_tabular_source_status laplace_tabular_source_plan_view_get(
    const laplace_tabular_source_plan* plan,
    laplace_tabular_source_plan_view* view);

LAPLACE_API laplace_tabular_source_status
laplace_tabular_source_profile_finalize(
    const laplace_tabular_source_plan* plan,
    const laplace_composition_working_set_summary* composition,
    laplace_source_profile_manifest* profile);

LAPLACE_API laplace_tabular_source_status laplace_tabular_source_recompose_artifact(
    const laplace_tabular_source_plan* plan,
    size_t artifact_index,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes);

LAPLACE_API void laplace_tabular_source_plan_destroy(
    laplace_tabular_source_plan** plan);

#ifdef __cplusplus
}
#endif

#endif
