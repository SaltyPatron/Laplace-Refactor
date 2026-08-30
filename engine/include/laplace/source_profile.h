#ifndef LAPLACE_SOURCE_PROFILE_H
#define LAPLACE_SOURCE_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/source_profile.h"
#include "laplace/export.h"
#include "laplace/highway.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_source_profile_manifest {
    laplace_digest256 profile_id;
    laplace_highway_key coordinate;
    laplace_digest256 authority_release_fingerprint;
    laplace_digest256 license_fingerprint;
    laplace_digest256 artifact_graph_fingerprint;
    laplace_digest256 syntax_authority_fingerprint;
    laplace_digest256 recipe_program_fingerprint;
    laplace_digest256 universal_ast_mapping_fingerprint;
    laplace_digest256 highway_references_fingerprint;
    laplace_digest256 epistemic_witnessing_fingerprint;
    laplace_digest256 denominator_declaration_fingerprint;
    laplace_digest256 conformance_fingerprint;
    laplace_digest256 completion_law_fingerprint;
    laplace_digest256 selected_boundary_fingerprint;
    uint64_t byte_count;
    uint64_t container_count;
    uint64_t member_count;
    uint64_t file_count;
    uint64_t record_count;
    uint64_t field_count;
    uint64_t syntax_node_count;
    uint64_t span_count;
    uint64_t edge_count;
    uint64_t reference_count;
    uint64_t occurrence_count;
    uint64_t claim_count;
    uint64_t mapping_count;
    uint64_t error_count;
    uint64_t unknown_count;
    uint64_t transformation_count;
    uint64_t output_count;
    uint64_t closure_subject_count;
    uint64_t accepted_count;
    uint64_t rejected_count;
    uint64_t duplicate_count;
    uint64_t reused_count;
    uint64_t transformed_count;
    uint64_t lossy_count;
    uint64_t unsupported_count;
    uint64_t malformed_count;
    uint64_t unresolved_count;
    uint64_t persisted_count;
    uint64_t derived_count;
    uint64_t not_applicable_mask;
    uint32_t reconstruction_class;
    uint32_t flags;
} laplace_source_profile_manifest;

typedef struct laplace_source_profile_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 selected_boundary_fingerprint;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t profile_count;
    uint64_t closure_subject_count;
    uint64_t persisted_count;
    uint64_t negative_count;
    uint64_t exact_reconstruction_count;
    uint64_t semantic_reconstruction_count;
    uint64_t no_reconstruction_count;
    uint32_t version;
    uint32_t status;
} laplace_source_profile_receipt;

typedef struct laplace_source_profile_error {
    uint64_t profile_index;
    uint32_t field;
    uint32_t reserved;
} laplace_source_profile_error;

typedef enum laplace_source_profile_status {
    LAPLACE_SOURCE_PROFILE_OK = 0,
    LAPLACE_SOURCE_PROFILE_INVALID_ARGUMENT = 1,
    LAPLACE_SOURCE_PROFILE_COORDINATE_INVALID = 2,
    LAPLACE_SOURCE_PROFILE_FINGERPRINT_MISSING = 3,
    LAPLACE_SOURCE_PROFILE_DENOMINATOR_INVALID = 4,
    LAPLACE_SOURCE_PROFILE_DISPOSITION_INVALID = 5,
    LAPLACE_SOURCE_PROFILE_IDENTITY_MISMATCH = 6,
    LAPLACE_SOURCE_PROFILE_ORDER_INVALID = 7,
    LAPLACE_SOURCE_PROFILE_BOUNDARY_MISMATCH = 8,
    LAPLACE_SOURCE_PROFILE_OVERFLOW = 9,
    LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_INVALID = 10
} laplace_source_profile_status;

LAPLACE_API uint32_t laplace_source_profile_epistemic_class(
    const laplace_source_profile_manifest* profile);

LAPLACE_API uint32_t laplace_source_profile_evidence_source_type(
    const laplace_source_profile_manifest* profile);

LAPLACE_API laplace_source_profile_status laplace_source_profile_identify(
    const laplace_source_profile_manifest* profile,
    laplace_digest256* profile_id);

LAPLACE_API laplace_source_profile_status laplace_source_profile_validate_batch(
    const laplace_source_profile_manifest* profiles,
    size_t profile_count,
    laplace_source_profile_receipt* receipt,
    laplace_source_profile_error* error);

#ifdef __cplusplus
}
#endif

#endif
