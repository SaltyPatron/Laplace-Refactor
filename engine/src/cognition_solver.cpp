#include "laplace/cognition_solver.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace {

bool Zero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::equal(std::begin(left.bytes), std::end(left.bytes), std::begin(right.bytes));
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashF64(blake3_hasher* const hasher, const double value) {
    HashU64(hasher, std::bit_cast<std::uint64_t>(value));
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool ProgramValid(const laplace_cognition_solver_program& program) {
    if (Zero(program.program_id) || Zero(program.operator_id) ||
        Zero(program.result_contract_fingerprint) || program.max_iterations == 0U ||
        !std::isfinite(program.absolute_residual_tolerance) ||
        !std::isfinite(program.relative_residual_tolerance) ||
        !std::isfinite(program.regularization) ||
        program.absolute_residual_tolerance < 0.0 ||
        program.relative_residual_tolerance < 0.0 || program.regularization < 0.0 ||
        (program.absolute_residual_tolerance == 0.0 &&
         program.relative_residual_tolerance == 0.0) ||
        program.method != LAPLACE_COGNITION_SOLVER_METHOD_CONJUGATE_GRADIENT ||
        (program.flags & ~LAPLACE_COGNITION_SOLVER_KNOWN_FLAGS) != 0U ||
        program.version != LAPLACE_COGNITION_SOLVER_VERSION ||
        program.reserved != 0U) {
        return false;
    }
    return true;
}

void HashProgram(
    const laplace_cognition_solver_program& program,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_SOLVER_PROGRAM_DOMAIN,
        sizeof(LAPLACE_COGNITION_SOLVER_PROGRAM_DOMAIN) - 1U);
    HashDigest(&hasher, program.program_id);
    HashDigest(&hasher, program.operator_id);
    HashDigest(&hasher, program.result_contract_fingerprint);
    HashU64(&hasher, program.max_iterations);
    HashF64(&hasher, program.absolute_residual_tolerance);
    HashF64(&hasher, program.relative_residual_tolerance);
    HashF64(&hasher, program.regularization);
    HashU32(&hasher, program.method);
    HashU32(&hasher, program.flags);
    HashU32(&hasher, program.version);
    Finish(&hasher, output);
}

double Dot(const std::vector<double>& left, const std::vector<double>& right) {
    double total = 0.0;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        total += left[index] * right[index];
    }
    return total;
}

bool Finite(const std::vector<double>& values) {
    return std::all_of(values.begin(), values.end(), [](double value) {
        return std::isfinite(value);
    });
}

laplace_cognition_solver_status ApplyLinearRegularized(
    const laplace_cognition_operator* const operator_value,
    const laplace_cognition_solver_program& program,
    const std::vector<double>& input,
    std::vector<double>* const output) {
    const auto status = laplace_cognition_operator_apply_linear(
        operator_value, input.data(), input.size(), output->data(), output->size());
    if (status != LAPLACE_COGNITION_OPERATOR_OK) {
        return LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE;
    }
#if defined(LAPLACE_TEST_COGNITION_SOLVER_IGNORE_REGULARIZATION)
    (void)program;
#else
    for (std::size_t index = 0U; index < output->size(); ++index) {
        (*output)[index] += program.regularization * input[index];
    }
#endif
    return Finite(*output)
        ? LAPLACE_COGNITION_SOLVER_OK
        : LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
}

void FinalizeReceipt(laplace_cognition_solver_receipt* const receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_SOLVER_RECEIPT_DOMAIN,
        sizeof(LAPLACE_COGNITION_SOLVER_RECEIPT_DOMAIN) - 1U);
    HashDigest(&hasher, receipt->program_fingerprint);
    HashDigest(&hasher, receipt->operator_id);
    HashDigest(&hasher, receipt->operator_receipt_id);
    HashDigest(&hasher, receipt->evidence_precision_fingerprint);
    HashDigest(&hasher, receipt->input_fingerprint);
    HashDigest(&hasher, receipt->iteration_trace_fingerprint);
    HashDigest(&hasher, receipt->output_fingerprint);
    HashU64(&hasher, receipt->field_count);
    HashU64(&hasher, receipt->iteration_count);
    HashF64(&hasher, receipt->initial_residual_l2);
    HashF64(&hasher, receipt->final_residual_l2);
    HashF64(&hasher, receipt->final_energy);
    HashF64(&hasher, receipt->regularization);
    HashU32(&hasher, receipt->method);
    HashU32(&hasher, receipt->disposition);
    HashU32(&hasher, receipt->status);
    HashU32(&hasher, receipt->version);
    HashU32(&hasher, receipt->flags);
    Finish(&hasher, &receipt->receipt_id);
}

}  // namespace

extern "C" laplace_cognition_solver_status laplace_cognition_solver_execute(
    const laplace_cognition_operator* const operator_value,
    const laplace_cognition_solver_program* const program,
    const double* const initial_state,
    const size_t initial_state_count,
    double* const solution,
    const size_t solution_capacity,
    laplace_cognition_solver_receipt* const receipt) {
    if (receipt != nullptr) {
        *receipt = laplace_cognition_solver_receipt{};
        receipt->version = LAPLACE_COGNITION_SOLVER_VERSION;
    }
    if (operator_value == nullptr || program == nullptr || initial_state == nullptr ||
        initial_state_count == 0U || solution == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_SOLVER_INVALID_ARGUMENT;
    }
    if (!ProgramValid(*program)) {
        receipt->status = LAPLACE_COGNITION_SOLVER_PROGRAM_INVALID;
        return LAPLACE_COGNITION_SOLVER_PROGRAM_INVALID;
    }
    const std::size_t field_count = laplace_cognition_operator_field_count(operator_value);
    if (field_count != initial_state_count || solution_capacity < field_count) {
        receipt->status = LAPLACE_COGNITION_SOLVER_RANGE;
        return LAPLACE_COGNITION_SOLVER_RANGE;
    }
    laplace_cognition_operator_receipt operator_receipt{};
    if (laplace_cognition_operator_receipt_get(operator_value, &operator_receipt) !=
        LAPLACE_COGNITION_OPERATOR_OK) {
        receipt->status = LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE;
        return LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE;
    }
    if (!Same(program->operator_id, operator_receipt.operator_id)) {
        receipt->status = LAPLACE_COGNITION_SOLVER_OPERATOR_MISMATCH;
        return LAPLACE_COGNITION_SOLVER_OPERATOR_MISMATCH;
    }
    try {
        std::vector<double> x(initial_state, initial_state + field_count);
        if (!Finite(x)) {
            receipt->status = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
            return LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
        }
        std::vector<double> gradient(field_count, 0.0);
        std::vector<double> residual(field_count, 0.0);
        std::vector<double> direction(field_count, 0.0);
        std::vector<double> product(field_count, 0.0);
        laplace_cognition_operator_application_receipt application{};
        if (laplace_cognition_operator_apply(
                operator_value, x.data(), x.size(), gradient.data(), gradient.size(),
                &application) != LAPLACE_COGNITION_OPERATOR_OK) {
            receipt->status = LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE;
            return LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE;
        }
        for (std::size_t index = 0U; index < field_count; ++index) {
            gradient[index] += program->regularization * x[index];
            residual[index] = -gradient[index];
            direction[index] = residual[index];
        }
        double residual_squared = Dot(residual, residual);
        if (!std::isfinite(residual_squared) || residual_squared < 0.0) {
            receipt->status = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
            return LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
        }
        const double initial_l2 = std::sqrt(residual_squared);
        const double threshold = std::max(
            program->absolute_residual_tolerance,
            program->relative_residual_tolerance * initial_l2);
        receipt->program_fingerprint = laplace_digest256{};
        HashProgram(*program, &receipt->program_fingerprint);
        receipt->operator_id = operator_receipt.operator_id;
        receipt->operator_receipt_id = operator_receipt.receipt_id;
#if defined(LAPLACE_TEST_COGNITION_SOLVER_RESIDUAL_AS_PRECISION)
        receipt->evidence_precision_fingerprint = application.output_fingerprint;
#else
        receipt->evidence_precision_fingerprint = operator_receipt.constraint_set_fingerprint;
#endif
        receipt->field_count = field_count;
        receipt->initial_residual_l2 = initial_l2;
        receipt->regularization = program->regularization;
        receipt->method = program->method;
        receipt->flags = program->flags;

        blake3_hasher input_hasher{};
        blake3_hasher_init(&input_hasher);
        blake3_hasher_update(
            &input_hasher, LAPLACE_COGNITION_SOLVER_INPUT_DOMAIN,
            sizeof(LAPLACE_COGNITION_SOLVER_INPUT_DOMAIN) - 1U);
        HashDigest(&input_hasher, receipt->program_fingerprint);
        HashDigest(&input_hasher, receipt->operator_id);
        for (const auto value : x) HashF64(&input_hasher, value);
        Finish(&input_hasher, &receipt->input_fingerprint);

        blake3_hasher trace_hasher{};
        blake3_hasher_init(&trace_hasher);
        blake3_hasher_update(
            &trace_hasher, LAPLACE_COGNITION_SOLVER_ITERATION_DOMAIN,
            sizeof(LAPLACE_COGNITION_SOLVER_ITERATION_DOMAIN) - 1U);
        HashDigest(&trace_hasher, receipt->input_fingerprint);

        std::uint32_t disposition = LAPLACE_COGNITION_SOLVER_INITIALLY_SATISFIED;
        if (initial_l2 > threshold) {
            disposition = LAPLACE_COGNITION_SOLVER_ITERATION_LIMIT;
            for (std::uint64_t iteration = 0U; iteration < program->max_iterations; ++iteration) {
                const auto apply_status = ApplyLinearRegularized(
                    operator_value, *program, direction, &product);
                if (apply_status != LAPLACE_COGNITION_SOLVER_OK) {
                    receipt->status = apply_status;
                    receipt->disposition = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE;
                    return apply_status;
                }
                const double denominator = Dot(direction, product);
                if (!std::isfinite(denominator) || denominator <= 0.0) {
                    disposition = LAPLACE_COGNITION_SOLVER_SINGULAR_DIRECTION;
                    break;
                }
                const double alpha = residual_squared / denominator;
                if (!std::isfinite(alpha)) {
                    receipt->status = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                    receipt->disposition = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE;
                    return LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                }
                for (std::size_t index = 0U; index < field_count; ++index) {
                    x[index] += alpha * direction[index];
                    residual[index] -= alpha * product[index];
                }
                if (!Finite(x) || !Finite(residual)) {
                    receipt->status = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                    receipt->disposition = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE;
                    return LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                }
                const double next_squared = Dot(residual, residual);
                if (!std::isfinite(next_squared) || next_squared < 0.0) {
                    receipt->status = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                    receipt->disposition = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE;
                    return LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                }
                ++receipt->iteration_count;
                HashU64(&trace_hasher, iteration + 1U);
                HashF64(&trace_hasher, residual_squared);
                HashF64(&trace_hasher, denominator);
                HashF64(&trace_hasher, alpha);
                HashF64(&trace_hasher, next_squared);
                const double next_l2 = std::sqrt(next_squared);
                if (next_l2 <= threshold) {
                    residual_squared = next_squared;
                    disposition = LAPLACE_COGNITION_SOLVER_CONVERGED;
                    break;
                }
                const double beta = next_squared / residual_squared;
                if (!std::isfinite(beta)) {
                    receipt->status = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                    receipt->disposition = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE;
                    return LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
                }
                for (std::size_t index = 0U; index < field_count; ++index) {
                    direction[index] = residual[index] + beta * direction[index];
                }
                residual_squared = next_squared;
            }
        }
        receipt->disposition = disposition;
        receipt->final_residual_l2 = std::sqrt(residual_squared);
        Finish(&trace_hasher, &receipt->iteration_trace_fingerprint);
        std::copy(x.begin(), x.end(), solution);
        double operator_energy = 0.0;
        if (laplace_cognition_operator_energy(
                operator_value, x.data(), x.size(), &operator_energy) !=
            LAPLACE_COGNITION_OPERATOR_OK) {
            receipt->status = LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE;
            return LAPLACE_COGNITION_SOLVER_OPERATOR_FAILURE;
        }
        double norm_squared = Dot(x, x);
        receipt->final_energy =
            operator_energy + 0.5 * program->regularization * norm_squared;
        if (!std::isfinite(receipt->final_energy)) {
            receipt->status = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
            receipt->disposition = LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE;
            return LAPLACE_COGNITION_SOLVER_NUMERIC_FAILURE_STATUS;
        }
        blake3_hasher output_hasher{};
        blake3_hasher_init(&output_hasher);
        blake3_hasher_update(
            &output_hasher, LAPLACE_COGNITION_SOLVER_OUTPUT_DOMAIN,
            sizeof(LAPLACE_COGNITION_SOLVER_OUTPUT_DOMAIN) - 1U);
        HashDigest(&output_hasher, receipt->operator_id);
        for (const auto value : x) HashF64(&output_hasher, value);
        HashF64(&output_hasher, receipt->final_residual_l2);
        HashF64(&output_hasher, receipt->final_energy);
        HashU32(&output_hasher, receipt->disposition);
        Finish(&output_hasher, &receipt->output_fingerprint);
        receipt->status = LAPLACE_COGNITION_SOLVER_OK;
        FinalizeReceipt(receipt);
        return LAPLACE_COGNITION_SOLVER_OK;
    } catch (const std::bad_alloc&) {
        receipt->status = LAPLACE_COGNITION_SOLVER_MEMORY_FAILURE;
        return LAPLACE_COGNITION_SOLVER_MEMORY_FAILURE;
    }
}
