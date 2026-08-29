#ifndef LAPLACE_COGNITION_OPERATOR_H
#define LAPLACE_COGNITION_OPERATOR_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/cognition_operator.h"
#include "laplace/export.h"
#include "laplace/identity.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_cognition_operator_field {
    laplace_digest256 field_id;
    laplace_id128 entity_id;
    laplace_digest256 physicality_id;
    laplace_digest256 role_id;
    laplace_digest256 recipe_fingerprint;
    uint64_t ordinal;
    uint32_t value_dimension;
    uint32_t flags;
} laplace_cognition_operator_field;

typedef struct laplace_cognition_operator_program {
    laplace_digest256 program_id;
    laplace_digest256 boundary_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 evidence_epoch;
    laplace_digest256 result_contract_fingerprint;
    const uint32_t* eligible_relation_families;
    size_t eligible_relation_family_count;
    uint32_t eligible_source_mask;
    uint32_t flags;
    double numeric_tolerance;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_operator_program;

typedef struct laplace_cognition_operator_constraint {
    laplace_digest256 constraint_id;
    laplace_digest256 plane_id;
    laplace_digest256 law_fingerprint;
    laplace_digest256 units_fingerprint;
    laplace_digest256 evidence_root_id;
    laplace_digest256 calculation_receipt_id;
    uint64_t source_field_index;
    uint64_t target_field_index;
    double transport_scale;
    double transport_offset;
    double target_value;
    double precision;
    uint32_t relation_family;
    uint32_t source_class;
    uint32_t direction;
    uint32_t transport_kind;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_operator_constraint;

typedef struct laplace_cognition_operator_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 operator_id;
    laplace_digest256 program_fingerprint;
    laplace_digest256 field_set_fingerprint;
    laplace_digest256 constraint_set_fingerprint;
    uint64_t field_count;
    uint64_t input_constraint_count;
    uint64_t selected_constraint_count;
    uint64_t deduplicated_dependent_count;
    uint64_t physicality_constraint_count;
    uint64_t testimony_constraint_count;
    uint64_t derived_constraint_count;
    uint32_t relation_plane_count;
    uint32_t status;
    uint32_t version;
    uint32_t flags;
} laplace_cognition_operator_receipt;

typedef struct laplace_cognition_operator_application_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 operator_id;
    laplace_digest256 input_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t field_count;
    uint64_t constraint_count;
    double energy;
    uint32_t status;
    uint32_t flags;
} laplace_cognition_operator_application_receipt;

typedef struct laplace_cognition_operator laplace_cognition_operator;

typedef enum laplace_cognition_operator_status {
    LAPLACE_COGNITION_OPERATOR_OK = 0,
    LAPLACE_COGNITION_OPERATOR_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_OPERATOR_PROGRAM_INVALID = 2,
    LAPLACE_COGNITION_OPERATOR_FIELD_INVALID = 3,
    LAPLACE_COGNITION_OPERATOR_CONSTRAINT_INVALID = 4,
    LAPLACE_COGNITION_OPERATOR_EVIDENCE_CONFLICT = 5,
    LAPLACE_COGNITION_OPERATOR_EMPTY = 6,
    LAPLACE_COGNITION_OPERATOR_RANGE = 7,
    LAPLACE_COGNITION_OPERATOR_MEMORY_FAILURE = 8,
    LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE = 9
} laplace_cognition_operator_status;

LAPLACE_API laplace_cognition_operator_status
laplace_cognition_operator_create(
    const laplace_cognition_operator_program* program,
    const laplace_cognition_operator_field* fields,
    size_t field_count,
    const laplace_cognition_operator_constraint* constraints,
    size_t constraint_count,
    laplace_cognition_operator** operator_value,
    laplace_cognition_operator_receipt* receipt);

LAPLACE_API void
laplace_cognition_operator_destroy(laplace_cognition_operator** operator_value);

LAPLACE_API size_t
laplace_cognition_operator_field_count(const laplace_cognition_operator* operator_value);

LAPLACE_API size_t
laplace_cognition_operator_constraint_count(const laplace_cognition_operator* operator_value);

LAPLACE_API laplace_cognition_operator_status
laplace_cognition_operator_receipt_get(
    const laplace_cognition_operator* operator_value,
    laplace_cognition_operator_receipt* receipt);

LAPLACE_API laplace_cognition_operator_status
laplace_cognition_operator_apply_linear(
    const laplace_cognition_operator* operator_value,
    const double* input,
    size_t input_count,
    double* output,
    size_t output_count);

LAPLACE_API laplace_cognition_operator_status
laplace_cognition_operator_apply(
    const laplace_cognition_operator* operator_value,
    const double* input,
    size_t input_count,
    double* output,
    size_t output_count,
    laplace_cognition_operator_application_receipt* receipt);

LAPLACE_API laplace_cognition_operator_status
laplace_cognition_operator_energy(
    const laplace_cognition_operator* operator_value,
    const double* input,
    size_t input_count,
    double* energy);

LAPLACE_API laplace_cognition_operator_status
laplace_cognition_operator_materialize_dense(
    const laplace_cognition_operator* operator_value,
    double* matrix_row_major,
    size_t matrix_capacity,
    double* right_hand_side,
    size_t rhs_capacity,
    laplace_digest256* materialization_receipt_id);

#ifdef __cplusplus
}
#endif

#endif
