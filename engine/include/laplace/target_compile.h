#ifndef LAPLACE_TARGET_COMPILE_H
#define LAPLACE_TARGET_COMPILE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_operator.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_COMPILE_VERSION = 1,
    LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO = UINT32_C(1),
    LAPLACE_TARGET_COMPILE_KNOWN_FLAGS =
        LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO,

    LAPLACE_TARGET_ROLE_COMPATIBILITY_QK = 1,
    LAPLACE_TARGET_ROLE_CONTRIBUTION_VO = 2,
    LAPLACE_TARGET_ROLE_TRANSFORM_FFN = 3,
    LAPLACE_TARGET_ROLE_ROUTING = 4,
    LAPLACE_TARGET_ROLE_POSITION = 5,
    LAPLACE_TARGET_ROLE_EMBEDDING = 6,
    LAPLACE_TARGET_ROLE_OUTPUT = 7
};

typedef struct laplace_target_compile_job {
    uint32_t target_role;
    uint32_t layer_index;
    uint32_t head_index;
    uint32_t expert_index;
    laplace_digest256 role_fingerprint;
    laplace_cognition_operator_program operator_program;
    const laplace_cognition_operator_field* fields;
    size_t field_count;
    const laplace_cognition_operator_constraint* constraints;
    size_t constraint_count;
    uint32_t flags;
    uint32_t reserved;
} laplace_target_compile_job;

typedef struct laplace_target_compile_request {
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 target_contract_fingerprint;
    const laplace_target_compile_job* jobs;
    size_t job_count;
    uint32_t flags;
    uint32_t version;
    uint64_t reserved;
} laplace_target_compile_request;

typedef struct laplace_target_compile_slot_receipt {
    laplace_digest256 slot_id;
    laplace_digest256 role_fingerprint;
    laplace_digest256 operator_id;
    laplace_digest256 operator_receipt_id;
    laplace_digest256 program_fingerprint;
    laplace_digest256 field_set_fingerprint;
    laplace_digest256 constraint_set_fingerprint;
    laplace_digest256 materialization_receipt_id;
    laplace_digest256 matrix_fingerprint;
    uint64_t field_count;
    uint64_t selected_constraint_count;
    uint64_t matrix_value_count;
    uint64_t rhs_value_count;
    uint32_t target_role;
    uint32_t layer_index;
    uint32_t head_index;
    uint32_t expert_index;
    uint32_t relation_plane_count;
    uint32_t status;
} laplace_target_compile_slot_receipt;

typedef struct laplace_target_compile_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 target_contract_fingerprint;
    uint64_t slot_count;
    uint64_t distinct_operator_count;
    uint64_t matrix_value_count;
    uint64_t rhs_value_count;
    uint32_t flags;
    uint32_t status;
    uint32_t version;
    uint32_t reserved;
} laplace_target_compile_receipt;

typedef struct laplace_target_compile_result laplace_target_compile_result;

typedef enum laplace_target_compile_status {
    LAPLACE_TARGET_COMPILE_OK = 0,
    LAPLACE_TARGET_COMPILE_INVALID_ARGUMENT = 1,
    LAPLACE_TARGET_COMPILE_INVALID_REQUEST = 2,
    LAPLACE_TARGET_COMPILE_INVALID_JOB = 3,
    LAPLACE_TARGET_COMPILE_OPERATOR_FAILURE = 4,
    LAPLACE_TARGET_COMPILE_MATERIALIZATION_FAILURE = 5,
    LAPLACE_TARGET_COMPILE_FLATTENED_OPERATOR = 6,
    LAPLACE_TARGET_COMPILE_MEMORY_FAILURE = 7,
    LAPLACE_TARGET_COMPILE_CAPACITY_INSUFFICIENT = 8
} laplace_target_compile_status;

/*
 * Compile selected, typed substrate operator jobs into a deterministic
 * target-neutral materialization. This is the operator-first boundary: codecs and
 * consumer tensor layouts come later and may not invent semantics absent here.
 */
LAPLACE_API laplace_target_compile_status laplace_target_compile_execute(
    const laplace_target_compile_request* request,
    laplace_target_compile_result** result,
    laplace_target_compile_receipt* receipt);

LAPLACE_API void laplace_target_compile_result_destroy(
    laplace_target_compile_result** result);

LAPLACE_API size_t laplace_target_compile_result_slot_count(
    const laplace_target_compile_result* result);

LAPLACE_API laplace_target_compile_status laplace_target_compile_result_slot_receipt(
    const laplace_target_compile_result* result,
    size_t slot_index,
    laplace_target_compile_slot_receipt* receipt);

LAPLACE_API laplace_target_compile_status laplace_target_compile_result_matrix(
    const laplace_target_compile_result* result,
    size_t slot_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

LAPLACE_API laplace_target_compile_status laplace_target_compile_result_rhs(
    const laplace_target_compile_result* result,
    size_t slot_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

#ifdef __cplusplus
}
#endif

#endif
