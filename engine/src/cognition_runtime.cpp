#include "laplace/cognition_runtime.h"

#include <cstddef>
#include <cstdint>

namespace {

bool FitsSize(const std::uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= static_cast<std::uint64_t>(SIZE_MAX);
#else
    (void)value;
    return true;
#endif
}

}  // namespace

extern "C" laplace_cognition_runtime_status
laplace_cognition_runtime_execute(
    const laplace_cognition_runtime_request* const request,
    laplace_cognition_runtime_result* const result) {
    if (result != nullptr) {
        result->solution_count = 0U;
        result->operator_receipt = laplace_cognition_operator_receipt{};
        result->solver_receipt = laplace_cognition_solver_receipt{};
        result->status = LAPLACE_COGNITION_RUNTIME_INVALID_ARGUMENT;
        result->reserved = 0U;
    }
    if (request == nullptr || result == nullptr ||
        request->fields == nullptr || request->constraints == nullptr ||
        request->initial_state == nullptr || request->field_count == 0U ||
        request->constraint_count == 0U ||
        request->initial_state_count != request->field_count ||
        !FitsSize(request->field_count) ||
        !FitsSize(request->constraint_count) ||
        !FitsSize(request->initial_state_count) ||
        !FitsSize(result->solution_capacity) ||
        result->solution == nullptr ||
        result->solution_capacity < request->field_count) {
        return LAPLACE_COGNITION_RUNTIME_INVALID_ARGUMENT;
    }

    laplace_cognition_operator* operator_value = nullptr;
    const auto operator_status = laplace_cognition_operator_create(
        &request->operator_program,
        request->fields,
        static_cast<std::size_t>(request->field_count),
        request->constraints,
        static_cast<std::size_t>(request->constraint_count),
        &operator_value,
        &result->operator_receipt);
    if (operator_status != LAPLACE_COGNITION_OPERATOR_OK) {
        if (operator_value != nullptr) {
            laplace_cognition_operator_destroy(&operator_value);
        }
        result->status = LAPLACE_COGNITION_RUNTIME_OPERATOR_FAILURE;
        return LAPLACE_COGNITION_RUNTIME_OPERATOR_FAILURE;
    }

    laplace_cognition_solver_program solver_program = request->solver_program;
    solver_program.operator_id = result->operator_receipt.operator_id;
    const auto solver_status = laplace_cognition_solver_execute(
        operator_value,
        &solver_program,
        request->initial_state,
        static_cast<std::size_t>(request->initial_state_count),
        result->solution,
        static_cast<std::size_t>(result->solution_capacity),
        &result->solver_receipt);
    laplace_cognition_operator_destroy(&operator_value);
    if (solver_status != LAPLACE_COGNITION_SOLVER_OK) {
        result->status = LAPLACE_COGNITION_RUNTIME_SOLVER_FAILURE;
        return LAPLACE_COGNITION_RUNTIME_SOLVER_FAILURE;
    }

    result->solution_count = request->field_count;
    result->status = LAPLACE_COGNITION_RUNTIME_OK;
    return LAPLACE_COGNITION_RUNTIME_OK;
}
