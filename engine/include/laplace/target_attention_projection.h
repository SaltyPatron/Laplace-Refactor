#ifndef LAPLACE_TARGET_ATTENTION_PROJECTION_H
#define LAPLACE_TARGET_ATTENTION_PROJECTION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/target_compile.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION = 1,
    LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT = UINT32_C(1),
    LAPLACE_TARGET_ATTENTION_PROJECTION_KNOWN_FLAGS =
        LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT
};

typedef struct laplace_target_attention_head_spec {
    size_t qk_slot_index;
    size_t vo_slot_index;
    uint64_t head_rank;
    uint32_t flags;
    uint32_t reserved;
} laplace_target_attention_head_spec;

typedef struct laplace_target_attention_projection_request {
    const laplace_target_attention_head_spec* heads;
    size_t head_count;
    uint64_t hidden_width;
    double relative_tolerance;
    uint32_t flags;
    uint32_t version;
} laplace_target_attention_projection_request;

typedef struct laplace_target_attention_head_receipt {
    laplace_digest256 head_projection_id;
    laplace_digest256 qk_slot_id;
    laplace_digest256 vo_slot_id;
    laplace_digest256 qk_operator_id;
    laplace_digest256 vo_operator_id;
    laplace_digest256 q_tensor_fingerprint;
    laplace_digest256 k_tensor_fingerprint;
    laplace_digest256 v_tensor_fingerprint;
    laplace_digest256 o_tensor_fingerprint;
    uint64_t field_count;
    uint64_t hidden_width;
    uint64_t head_rank;
    double qk_relative_residual;
    double vo_relative_residual;
    uint32_t layer_index;
    uint32_t head_index;
    uint32_t expert_index;
    uint32_t status;
} laplace_target_attention_head_receipt;

typedef struct laplace_target_attention_projection_receipt {
    laplace_digest256 projection_id;
    laplace_digest256 embedding_fingerprint;
    laplace_digest256 head_set_fingerprint;
    uint64_t field_count;
    uint64_t hidden_width;
    uint64_t semantic_basis_rank;
    uint64_t null_basis_rank;
    uint64_t head_count;
    uint64_t tensor_value_count;
    double max_relative_residual;
    uint32_t flags;
    uint32_t status;
    uint32_t version;
    uint32_t reserved;
} laplace_target_attention_projection_receipt;

typedef struct laplace_target_attention_projection_result
    laplace_target_attention_projection_result;

typedef enum laplace_target_attention_projection_status {
    LAPLACE_TARGET_ATTENTION_PROJECTION_OK = 0,
    LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT = 1,
    LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_REQUEST = 2,
    LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_HEAD = 3,
    LAPLACE_TARGET_ATTENTION_PROJECTION_DOMAIN_MISMATCH = 4,
    LAPLACE_TARGET_ATTENTION_PROJECTION_HIDDEN_WIDTH_INSUFFICIENT = 5,
    LAPLACE_TARGET_ATTENTION_PROJECTION_NUMERIC_FAILURE = 6,
    LAPLACE_TARGET_ATTENTION_PROJECTION_MEMORY_FAILURE = 7,
    LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT = 8
} laplace_target_attention_projection_status;

/*
 * Project already-generated QK compatibility and VO contribution operators into
 * one deterministic residual/embedding basis and conventional Q/K/V/O weights.
 *
 * For each head:
 *   M_qk ~= (E Wq^T) (E Wk^T)^T
 *   M_vo ~= (E Wv^T) (E Wo)^T
 *
 * E is generated from the factor spaces themselves using deterministic
 * re-orthogonalized Gram-Schmidt. Unused target width is filled only with a
 * canonical orthogonal null basis; dependent weights in those dimensions are
 * zero up to the declared numeric contract. If target width cannot span the
 * selected operator factors, REQUIRE_EXACT fails instead of cycling or filling
 * semantic capacity.
 */
LAPLACE_API laplace_target_attention_projection_status
laplace_target_attention_project(
    const laplace_target_compile_result* compiled,
    const laplace_target_attention_projection_request* request,
    laplace_target_attention_projection_result** result,
    laplace_target_attention_projection_receipt* receipt);

LAPLACE_API void laplace_target_attention_projection_result_destroy(
    laplace_target_attention_projection_result** result);

LAPLACE_API size_t laplace_target_attention_projection_head_count(
    const laplace_target_attention_projection_result* result);

LAPLACE_API laplace_target_attention_projection_status
laplace_target_attention_projection_head_receipt(
    const laplace_target_attention_projection_result* result,
    size_t head_index,
    laplace_target_attention_head_receipt* receipt);

LAPLACE_API laplace_target_attention_projection_status
laplace_target_attention_projection_embedding(
    const laplace_target_attention_projection_result* result,
    double* output,
    size_t output_capacity,
    size_t* required_count);

/* Q/K/V are row-major [head_rank, hidden_width]. */
LAPLACE_API laplace_target_attention_projection_status
laplace_target_attention_projection_q(
    const laplace_target_attention_projection_result* result,
    size_t head_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

LAPLACE_API laplace_target_attention_projection_status
laplace_target_attention_projection_k(
    const laplace_target_attention_projection_result* result,
    size_t head_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

LAPLACE_API laplace_target_attention_projection_status
laplace_target_attention_projection_v(
    const laplace_target_attention_projection_result* result,
    size_t head_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

/* O is row-major [hidden_width, head_rank]. */
LAPLACE_API laplace_target_attention_projection_status
laplace_target_attention_projection_o(
    const laplace_target_attention_projection_result* result,
    size_t head_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

#ifdef __cplusplus
}
#endif

#endif
