#ifndef LAPLACE_TARGET_FACTORIZATION_H
#define LAPLACE_TARGET_FACTORIZATION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/target_compile.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_FACTORIZATION_VERSION = 1,
    LAPLACE_TARGET_FACTORIZATION_REQUIRE_EXACT = UINT32_C(1),
    LAPLACE_TARGET_FACTORIZATION_KNOWN_FLAGS =
        LAPLACE_TARGET_FACTORIZATION_REQUIRE_EXACT
};

typedef struct laplace_target_factorization_request {
    size_t slot_index;
    uint64_t requested_rank;
    double relative_tolerance;
    uint32_t flags;
    uint32_t version;
} laplace_target_factorization_request;

typedef struct laplace_target_factorization_receipt {
    laplace_digest256 factorization_id;
    laplace_digest256 slot_id;
    laplace_digest256 operator_id;
    laplace_digest256 matrix_fingerprint;
    laplace_digest256 left_factor_fingerprint;
    laplace_digest256 right_factor_fingerprint;
    laplace_digest256 singular_value_fingerprint;
    uint64_t row_count;
    uint64_t column_count;
    uint64_t requested_rank;
    uint64_t accepted_rank;
    uint64_t factor_value_count;
    double source_frobenius;
    double residual_frobenius;
    double relative_residual;
    uint32_t target_role;
    uint32_t layer_index;
    uint32_t head_index;
    uint32_t expert_index;
    uint32_t flags;
    uint32_t status;
    uint32_t version;
    uint32_t reserved;
} laplace_target_factorization_receipt;

typedef enum laplace_target_factorization_status {
    LAPLACE_TARGET_FACTORIZATION_OK = 0,
    LAPLACE_TARGET_FACTORIZATION_INVALID_ARGUMENT = 1,
    LAPLACE_TARGET_FACTORIZATION_INVALID_REQUEST = 2,
    LAPLACE_TARGET_FACTORIZATION_INVALID_SLOT = 3,
    LAPLACE_TARGET_FACTORIZATION_CAPACITY_INSUFFICIENT = 4,
    LAPLACE_TARGET_FACTORIZATION_NUMERIC_FAILURE = 5,
    LAPLACE_TARGET_FACTORIZATION_RANK_INSUFFICIENT = 6,
    LAPLACE_TARGET_FACTORIZATION_MEMORY_FAILURE = 7
} laplace_target_factorization_status;

/*
 * Deterministically factor one already-generated target-neutral pair operator M
 * into left/right target factors such that M ~= left * right^T.
 *
 * The factorization uses a canonical one-sided Jacobi SVD, descending singular
 * value order, stable tie ordering, and a fixed sign gauge. Both factors split
 * sqrt(sigma), so Q/K, K/V, or V/O consumer weights are derived from the exact
 * operator rather than invented independently. Truncation is explicit through
 * requested_rank and the residual recorded in the receipt.
 *
 * left_factor and right_factor are row-major [field_count, requested_rank].
 * singular_values contains requested_rank values in descending order.
 */
LAPLACE_API laplace_target_factorization_status laplace_target_factorize_slot(
    const laplace_target_compile_result* result,
    const laplace_target_factorization_request* request,
    double* left_factor,
    size_t left_factor_capacity,
    double* right_factor,
    size_t right_factor_capacity,
    double* singular_values,
    size_t singular_value_capacity,
    size_t* required_factor_values,
    size_t* required_singular_values,
    laplace_target_factorization_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
