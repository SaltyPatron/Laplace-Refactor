#ifndef LAPLACE_COGNITION_SOLVER_H
#define LAPLACE_COGNITION_SOLVER_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_operator.h"
#include "laplace/contract/cognition_solver.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_cognition_solver_program {
    laplace_digest256 program_id;
    laplace_digest256 operator_id;
    laplace_digest256 result_contract_fingerprint;
    uint64_t max_iterations;
    double absolute_residual_tolerance;
    double relative_residual_tolerance;
    double regularization;
    uint32_t method;
    uint32_t flags;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_solver_program;

typedef struct laplace_cognition_solver_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 program_fingerprint;
    laplace_digest256 operator_id;
    laplace_digest256 operator_receipt_id;
    laplace_digest256 evidence_precision_fingerprint;
    laplace_digest256 input_fingerprint;
    laplace_digest256 iteration_trace_fingerprint;
    laplace_digest256 output_fingerprint;
    uint64_t field_count;
    uint64_t iteration_count;
    double initial_residual_l2;
    double final_residual_l2;
    double final_energy;
    double regularization;
    uint32_t method;
    uint32_t disposition;
    uint32_t status;
    uint32_t version;
    uint32_t flags;
} laplace_cognition_solver_receipt;

typedef enum laplace_cognition_solver_status {
    LAPLACE_COGNITION_SOLVER_OK = 0,
    LAPLACE_COGNITION_SOLVER_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_SOLVER_PROGRAM_INVALID = 2,
    LAPLACE_COGNITION_SOLVER_OPERATOR_MISMATCH = 3,
    LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE = 4,
    LAPLACE_COGNITION_SOLVER_MEMORY_FAILURE = 5,
    LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS = 6,
    LAPLACE_COGNITION_SOLVER_RANGE = 7
} laplace_cognition_solver_status;

LAPLACE_API laplace_cognition_solver_status
laplace_cognition_solver_execute(
    const laplace_cognition_operator* operator_value,
    const laplace_cognition_solver_program* program,
    const double* initial_state,
    size_t initial_state_count,
    double* solution,
    size_t solution_capacity,
    laplace_cognition_solver_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
