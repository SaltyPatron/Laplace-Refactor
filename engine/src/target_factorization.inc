#include "laplace/target_factorization.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <numeric>
#include <vector>

namespace {

constexpr char TargetFactorizationDomain[] = "laplace-target-factorization-v1";
constexpr char TargetLeftFactorDomain[] = "laplace-target-left-factor-v1";
constexpr char TargetRightFactorDomain[] = "laplace-target-right-factor-v1";
constexpr char TargetSingularValueDomain[] = "laplace-target-singular-values-v1";

bool TargetFactorizationRequestValid(
    const laplace_target_factorization_request& request) {
    return request.requested_rank > 0U &&
        std::isfinite(request.relative_tolerance) &&
        request.relative_tolerance > 0.0 &&
        (request.flags & ~LAPLACE_TARGET_FACTORIZATION_KNOWN_FLAGS) == 0U &&
        request.version == LAPLACE_TARGET_FACTORIZATION_VERSION;
}

void TargetFactorValuesFingerprint(
    const char* const domain,
    const std::size_t domain_size,
    const double* const values,
    const std::size_t count,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, domain_size);
    HashU64(&hasher, static_cast<std::uint64_t>(count));
    for (std::size_t index = 0U; index < count; ++index) {
        HashF64(&hasher, values[index]);
    }
    Finish(&hasher, output);
}

bool TargetFiniteMatrix(const std::vector<double>& matrix) {
    for (const double value : matrix) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

void TargetRotateColumns(
    std::vector<double>* const matrix,
    const std::size_t rows,
    const std::size_t columns,
    const std::size_t p,
    const std::size_t q,
    const double cosine,
    const double sine) {
    for (std::size_t row = 0U; row < rows; ++row) {
        const std::size_t p_index = row * columns + p;
        const std::size_t q_index = row * columns + q;
        const double p_value = (*matrix)[p_index];
        const double q_value = (*matrix)[q_index];
        (*matrix)[p_index] = cosine * p_value - sine * q_value;
        (*matrix)[q_index] = sine * p_value + cosine * q_value;
    }
}

bool TargetJacobiFactorize(
    const std::vector<double>& source,
    const std::size_t dimension,
    const std::size_t requested_rank,
    const double relative_tolerance,
    std::vector<double>* const left,
    std::vector<double>* const right,
    std::vector<double>* const singular_values,
    std::uint64_t* const accepted_rank,
    double* const source_frobenius,
    double* const residual_frobenius) {
    if (dimension == 0U || requested_rank == 0U || requested_rank > dimension ||
        source.size() != dimension * dimension) {
        return false;
    }

    std::vector<double> rotated(source);
    std::vector<double> right_basis(dimension * dimension, 0.0);
    for (std::size_t index = 0U; index < dimension; ++index) {
        right_basis[index * dimension + index] = 1.0;
    }

    const double rotation_tolerance = std::max(
        relative_tolerance * 0.125,
        std::numeric_limits<double>::epsilon() * 64.0);
    const std::size_t max_sweeps = std::max<std::size_t>(24U, 8U * dimension);

    for (std::size_t sweep = 0U; sweep < max_sweeps; ++sweep) {
        bool rotated_any = false;
        for (std::size_t p = 0U; p < dimension; ++p) {
            for (std::size_t q = p + 1U; q < dimension; ++q) {
                double alpha = 0.0;
                double beta = 0.0;
                double gamma = 0.0;
                for (std::size_t row = 0U; row < dimension; ++row) {
                    const double p_value = rotated[row * dimension + p];
                    const double q_value = rotated[row * dimension + q];
                    alpha += p_value * p_value;
                    beta += q_value * q_value;
                    gamma += p_value * q_value;
                }
                if (!std::isfinite(alpha) || !std::isfinite(beta) ||
                    !std::isfinite(gamma)) {
                    return false;
                }
                if (alpha == 0.0 || beta == 0.0) continue;
                const double scale = std::sqrt(alpha * beta);
                if (scale == 0.0 || std::abs(gamma) <= rotation_tolerance * scale) {
                    continue;
                }

                const double tau = (beta - alpha) / (2.0 * gamma);
                const double sign = tau < 0.0 ? -1.0 : 1.0;
                const double tangent = sign /
                    (std::abs(tau) + std::sqrt(1.0 + tau * tau));
                const double cosine = 1.0 / std::sqrt(1.0 + tangent * tangent);
                const double sine = cosine * tangent;
                if (!std::isfinite(cosine) || !std::isfinite(sine)) return false;

                TargetRotateColumns(
                    &rotated, dimension, dimension, p, q, cosine, sine);
                TargetRotateColumns(
                    &right_basis, dimension, dimension, p, q, cosine, sine);
                rotated_any = true;
            }
        }
        if (!rotated_any) break;
    }

    std::vector<double> sigma_all(dimension, 0.0);
    for (std::size_t column = 0U; column < dimension; ++column) {
        double squared = 0.0;
        for (std::size_t row = 0U; row < dimension; ++row) {
            const double value = rotated[row * dimension + column];
            squared += value * value;
        }
        if (!std::isfinite(squared) || squared < 0.0) return false;
        sigma_all[column] = std::sqrt(squared);
    }

    std::vector<std::size_t> order(dimension, 0U);
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(
        order.begin(), order.end(),
        [&](const std::size_t left_index, const std::size_t right_index) {
            const double left_sigma = sigma_all[left_index];
            const double right_sigma = sigma_all[right_index];
            if (left_sigma != right_sigma) return left_sigma > right_sigma;
            return left_index < right_index;
        });

    left->assign(dimension * requested_rank, 0.0);
    right->assign(dimension * requested_rank, 0.0);
    singular_values->assign(requested_rank, 0.0);

    const double leading_sigma = sigma_all[order.front()];
    const double sigma_floor = leading_sigma * relative_tolerance;
    *accepted_rank = 0U;
    for (std::size_t rank = 0U; rank < requested_rank; ++rank) {
        const std::size_t source_column = order[rank];
        const double sigma = sigma_all[source_column];
        (*singular_values)[rank] = sigma;
        if (sigma > sigma_floor || (rank == 0U && sigma > 0.0)) {
            ++(*accepted_rank);
        }
        if (sigma == 0.0) continue;

        const double root_sigma = std::sqrt(sigma);
        double pivot_abs = 0.0;
        double pivot_value = 0.0;
        for (std::size_t row = 0U; row < dimension; ++row) {
            const double u_value = rotated[row * dimension + source_column] / sigma;
            const double magnitude = std::abs(u_value);
            if (magnitude > pivot_abs) {
                pivot_abs = magnitude;
                pivot_value = u_value;
            }
        }
        const double gauge = pivot_abs > 0.0 && pivot_value < 0.0 ? -1.0 : 1.0;
        for (std::size_t row = 0U; row < dimension; ++row) {
            const double u_value = rotated[row * dimension + source_column] / sigma;
            const double v_value = right_basis[row * dimension + source_column];
            (*left)[row * requested_rank + rank] = gauge * u_value * root_sigma;
            (*right)[row * requested_rank + rank] = gauge * v_value * root_sigma;
        }
    }

    double source_squared = 0.0;
    double residual_squared = 0.0;
    for (std::size_t row = 0U; row < dimension; ++row) {
        for (std::size_t column = 0U; column < dimension; ++column) {
            const double expected = source[row * dimension + column];
            double reconstructed = 0.0;
            for (std::size_t rank = 0U; rank < requested_rank; ++rank) {
                reconstructed +=
                    (*left)[row * requested_rank + rank] *
                    (*right)[column * requested_rank + rank];
            }
            const double delta = expected - reconstructed;
            source_squared += expected * expected;
            residual_squared += delta * delta;
        }
    }
    if (!std::isfinite(source_squared) || !std::isfinite(residual_squared) ||
        source_squared < 0.0 || residual_squared < 0.0) {
        return false;
    }
    *source_frobenius = std::sqrt(source_squared);
    *residual_frobenius = std::sqrt(residual_squared);
    return std::isfinite(*source_frobenius) && std::isfinite(*residual_frobenius);
}

void TargetIdentifyFactorization(
    const laplace_target_factorization_receipt& receipt,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetFactorizationDomain, sizeof(TargetFactorizationDomain) - 1U);
    HashDigest(&hasher, receipt.slot_id);
    HashDigest(&hasher, receipt.operator_id);
    HashDigest(&hasher, receipt.matrix_fingerprint);
    HashDigest(&hasher, receipt.left_factor_fingerprint);
    HashDigest(&hasher, receipt.right_factor_fingerprint);
    HashDigest(&hasher, receipt.singular_value_fingerprint);
    HashU64(&hasher, receipt.row_count);
    HashU64(&hasher, receipt.column_count);
    HashU64(&hasher, receipt.requested_rank);
    HashU64(&hasher, receipt.accepted_rank);
    HashU64(&hasher, receipt.factor_value_count);
    HashF64(&hasher, receipt.source_frobenius);
    HashF64(&hasher, receipt.residual_frobenius);
    HashF64(&hasher, receipt.relative_residual);
    HashU32(&hasher, receipt.target_role);
    HashU32(&hasher, receipt.layer_index);
    HashU32(&hasher, receipt.head_index);
    HashU32(&hasher, receipt.expert_index);
    HashU32(&hasher, receipt.flags);
    HashU32(&hasher, receipt.status);
    HashU32(&hasher, receipt.version);
    Finish(&hasher, output);
}

}  // namespace

extern "C" laplace_target_factorization_status laplace_target_factorize_slot(
    const laplace_target_compile_result* const result,
    const laplace_target_factorization_request* const request,
    double* const left_factor,
    const size_t left_factor_capacity,
    double* const right_factor,
    const size_t right_factor_capacity,
    double* const singular_values,
    const size_t singular_value_capacity,
    size_t* const required_factor_values,
    size_t* const required_singular_values,
    laplace_target_factorization_receipt* const receipt) {
    if (result == nullptr || request == nullptr || required_factor_values == nullptr ||
        required_singular_values == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_FACTORIZATION_INVALID_ARGUMENT;
    }
    *required_factor_values = 0U;
    *required_singular_values = 0U;
    *receipt = laplace_target_factorization_receipt{};
    if (!TargetFactorizationRequestValid(*request)) {
        return LAPLACE_TARGET_FACTORIZATION_INVALID_REQUEST;
    }
    if (request->slot_index >= result->slots.size()) {
        return LAPLACE_TARGET_FACTORIZATION_INVALID_SLOT;
    }

    const auto& slot = result->slots[request->slot_index];
    const auto& matrix = result->matrices[request->slot_index];
    const std::size_t dimension = static_cast<std::size_t>(slot.field_count);
    if (dimension == 0U || request->requested_rank > slot.field_count ||
        dimension > std::numeric_limits<std::size_t>::max() / dimension ||
        matrix.size() != dimension * dimension || !TargetFiniteMatrix(matrix)) {
        return LAPLACE_TARGET_FACTORIZATION_INVALID_SLOT;
    }
    const std::size_t requested_rank = static_cast<std::size_t>(request->requested_rank);
    if (dimension > std::numeric_limits<std::size_t>::max() / requested_rank) {
        return LAPLACE_TARGET_FACTORIZATION_INVALID_REQUEST;
    }
    const std::size_t factor_values = dimension * requested_rank;
    *required_factor_values = factor_values;
    *required_singular_values = requested_rank;
    if (left_factor == nullptr || right_factor == nullptr || singular_values == nullptr ||
        left_factor_capacity < factor_values ||
        right_factor_capacity < factor_values ||
        singular_value_capacity < requested_rank) {
        return LAPLACE_TARGET_FACTORIZATION_CAPACITY_INSUFFICIENT;
    }

    std::vector<double> left;
    std::vector<double> right;
    std::vector<double> sigma;
    std::uint64_t accepted_rank = 0U;
    double source_frobenius = 0.0;
    double residual_frobenius = 0.0;
    try {
        if (!TargetJacobiFactorize(
                matrix, dimension, requested_rank, request->relative_tolerance,
                &left, &right, &sigma, &accepted_rank,
                &source_frobenius, &residual_frobenius)) {
            return LAPLACE_TARGET_FACTORIZATION_NUMERIC_FAILURE;
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_FACTORIZATION_MEMORY_FAILURE;
    }

    std::copy(left.begin(), left.end(), left_factor);
    std::copy(right.begin(), right.end(), right_factor);
    std::copy(sigma.begin(), sigma.end(), singular_values);

    receipt->slot_id = slot.slot_id;
    receipt->operator_id = slot.operator_id;
    receipt->matrix_fingerprint = slot.matrix_fingerprint;
    receipt->row_count = slot.field_count;
    receipt->column_count = slot.field_count;
    receipt->requested_rank = request->requested_rank;
    receipt->accepted_rank = accepted_rank;
    receipt->factor_value_count = static_cast<std::uint64_t>(factor_values);
    receipt->source_frobenius = source_frobenius;
    receipt->residual_frobenius = residual_frobenius;
    receipt->relative_residual = source_frobenius > 0.0
        ? residual_frobenius / source_frobenius
        : residual_frobenius;
    receipt->target_role = slot.target_role;
    receipt->layer_index = slot.layer_index;
    receipt->head_index = slot.head_index;
    receipt->expert_index = slot.expert_index;
    receipt->flags = request->flags;
    receipt->version = LAPLACE_TARGET_FACTORIZATION_VERSION;

    TargetFactorValuesFingerprint(
        TargetLeftFactorDomain, sizeof(TargetLeftFactorDomain) - 1U,
        left.data(), left.size(), &receipt->left_factor_fingerprint);
    TargetFactorValuesFingerprint(
        TargetRightFactorDomain, sizeof(TargetRightFactorDomain) - 1U,
        right.data(), right.size(), &receipt->right_factor_fingerprint);
    TargetFactorValuesFingerprint(
        TargetSingularValueDomain, sizeof(TargetSingularValueDomain) - 1U,
        sigma.data(), sigma.size(), &receipt->singular_value_fingerprint);

    const bool insufficient =
        (request->flags & LAPLACE_TARGET_FACTORIZATION_REQUIRE_EXACT) != 0U &&
        receipt->relative_residual > request->relative_tolerance;
    receipt->status = insufficient
        ? LAPLACE_TARGET_FACTORIZATION_RANK_INSUFFICIENT
        : LAPLACE_TARGET_FACTORIZATION_OK;
    TargetIdentifyFactorization(*receipt, &receipt->factorization_id);
    return insufficient
        ? LAPLACE_TARGET_FACTORIZATION_RANK_INSUFFICIENT
        : LAPLACE_TARGET_FACTORIZATION_OK;
}
