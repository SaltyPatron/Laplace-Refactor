#ifndef LAPLACE_TABULAR_SOURCE_H
#define LAPLACE_TABULAR_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "laplace/contract/tabular_source.h"
#include "laplace/export.h"
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
    uint64_t byte_count;
    uint64_t name_byte_count;
    uint64_t expected_record_count;
    uint64_t expected_field_count;
    uint64_t reference_column_mask;
    uint32_t mode;
    uint32_t delimiter;
    uint32_t line_terminator;
    uint32_t expected_column_count;
    uint32_t outcome_type;
    uint32_t flags;
    uint32_t reserved;
} laplace_tabular_artifact;

enum {
    LAPLACE_TABULAR_ARTIFACT_CONTAINER = 1u,
    LAPLACE_TABULAR_ARTIFACT_MEMBER = 2u,
    LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION = 4u
};

typedef struct laplace_tabular_source_input {
    laplace_source_profile_manifest profile_declaration;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    const laplace_tabular_artifact* artifacts;
    uint64_t artifact_count;
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
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t claim_count;
    uint64_t artifact_count;
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
