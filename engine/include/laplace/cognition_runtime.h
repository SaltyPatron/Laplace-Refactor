#ifndef LAPLACE_COGNITION_RUNTIME_H
#define LAPLACE_COGNITION_RUNTIME_H

#include <stdint.h>

#include "laplace/cognition_operator.h"
#include "laplace/cognition_solver.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_cognition_runtime_request {
    laplace_cognition_operator_program operator_program;
    const laplace_cognition_operator_field* fields;
    const laplace_cognition_operator_constraint* constraints;
    const double* initial_state;
    uint64_t field_count;
    uint64_t constraint_count;
    uint64_t initial_state_count;
    laplace_cognition_solver_program solver_program;
} laplace_cognition_runtime_request;

typedef struct laplace_cognition_runtime_result {
    double* solution;
    uint64_t solution_capacity;
    uint64_t solution_count;
    laplace_cognition_operator_receipt operator_receipt;
    laplace_cognition_solver_receipt solver_receipt;
    uint32_t status;
    uint32_t reserved;
} laplace_cognition_runtime_result;

typedef enum laplace_cognition_runtime_status {
    LAPLACE_COGNITION_RUNTIME_OK = 0,
    LAPLACE_COGNITION_RUNTIME_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_RUNTIME_RANGE = 2,
    LAPLACE_COGNITION_RUNTIME_OPERATOR_FAILURE = 3,
    LAPLACE_COGNITION_RUNTIME_SOLVER_FAILURE = 4
} laplace_cognition_runtime_status;

LAPLACE_API laplace_cognition_runtime_status
laplace_cognition_runtime_execute(
    const laplace_cognition_runtime_request* request,
    laplace_cognition_runtime_result* result);

#ifdef __cplusplus
}
#endif

#endif
