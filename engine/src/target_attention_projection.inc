#include "laplace/target_attention_projection.h"
#include "laplace/target_factorization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr char AttentionProjectionDomain[] = "laplace-target-attention-projection-v1";
constexpr char AttentionEmbeddingDomain[] = "laplace-target-attention-embedding-v1";
constexpr char AttentionHeadDomain[] = "laplace-target-attention-head-v1";
constexpr char AttentionHeadSetDomain[] = "laplace-target-attention-head-set-v1";
constexpr char AttentionQDomain[] = "laplace-target-attention-q-v1";
constexpr char AttentionKDomain[] = "laplace-target-attention-k-v1";
constexpr char AttentionVDomain[] = "laplace-target-attention-v-v1";
constexpr char AttentionODomain[] = "laplace-target-attention-o-v1";

struct AttentionFactorizedHead {
    laplace_target_attention_head_spec spec{};
    laplace_target_compile_slot_receipt qk_slot{};
    laplace_target_compile_slot_receipt vo_slot{};
    laplace_target_factorization_receipt qk_factorization{};
    laplace_target_factorization_receipt vo_factorization{};
    std::vector<double> q_factor;
    std::vector<double> k_factor;
    std::vector<double> v_factor;
    std::vector<double> o_factor;
};

struct AttentionProjectedHead {
    laplace_target_attention_head_receipt receipt{};
    std::vector<double> q;
    std::vector<double> k;
    std::vector<double> v;
    std::vector<double> o;
};

bool AttentionRequestValid(
    const laplace_target_attention_projection_request& request) {
    return request.heads != nullptr && request.head_count > 0U &&
        request.hidden_width > 0U &&
        std::isfinite(request.relative_tolerance) &&
        request.relative_tolerance > 0.0 &&
        (request.flags & ~LAPLACE_TARGET_ATTENTION_PROJECTION_KNOWN_FLAGS) == 0U &&
        request.version == LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;
}

bool AttentionHeadSpecValid(const laplace_target_attention_head_spec& spec) {
    return spec.head_rank > 0U && spec.flags == 0U && spec.reserved == 0U;
}

void AttentionValuesFingerprint(
    const char* const domain,
    const std::size_t domain_size,
    const std::vector<double>& values,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, domain_size);
    HashU64(&hasher, static_cast<std::uint64_t>(values.size()));
    for (const double value : values) HashF64(&hasher, value);
    Finish(&hasher, output);
}

bool AttentionFinite(const std::vector<double>& values) {
    for (const double value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

bool AttentionSameDomain(
    const laplace_target_compile_slot_receipt& left,
    const laplace_target_compile_slot_receipt& right) {
    return left.field_count == right.field_count &&
        Same(left.field_set_fingerprint, right.field_set_fingerprint);
}

bool AttentionHeadLess(
    const AttentionFactorizedHead& left,
    const AttentionFactorizedHead& right) {
    const auto left_key = std::tie(
        left.qk_slot.layer_index, left.qk_slot.head_index, left.qk_slot.expert_index);
    const auto right_key = std::tie(
        right.qk_slot.layer_index, right.qk_slot.head_index, right.qk_slot.expert_index);
    if (left_key != right_key) return left_key < right_key;
    if (!Same(left.qk_slot.slot_id, right.qk_slot.slot_id)) {
        return DigestLess(left.qk_slot.slot_id, right.qk_slot.slot_id);
    }
    return DigestLess(left.vo_slot.slot_id, right.vo_slot.slot_id);
}

laplace_target_attention_projection_status AttentionFactorizeHead(
    const laplace_target_compile_result& compiled,
    const laplace_target_attention_head_spec& spec,
    const double relative_tolerance,
    AttentionFactorizedHead* const output) {
    if (!AttentionHeadSpecValid(spec) ||
        spec.qk_slot_index >= compiled.slots.size() ||
        spec.vo_slot_index >= compiled.slots.size()) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_HEAD;
    }
    const auto& qk = compiled.slots[spec.qk_slot_index];
    const auto& vo = compiled.slots[spec.vo_slot_index];
    if (qk.target_role != LAPLACE_TARGET_ROLE_COMPATIBILITY_QK ||
        vo.target_role != LAPLACE_TARGET_ROLE_CONTRIBUTION_VO ||
        qk.layer_index != vo.layer_index || qk.head_index != vo.head_index ||
        qk.expert_index != vo.expert_index || !AttentionSameDomain(qk, vo) ||
        spec.head_rank > qk.field_count) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_HEAD;
    }

    const std::size_t field_count = static_cast<std::size_t>(qk.field_count);
    const std::size_t head_rank = static_cast<std::size_t>(spec.head_rank);
    if (field_count == 0U || field_count > std::numeric_limits<std::size_t>::max() / head_rank) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_HEAD;
    }
    const std::size_t factor_values = field_count * head_rank;

    AttentionFactorizedHead result{};
    result.spec = spec;
    result.qk_slot = qk;
    result.vo_slot = vo;
    try {
        result.q_factor.resize(factor_values);
        result.k_factor.resize(factor_values);
        result.v_factor.resize(factor_values);
        result.o_factor.resize(factor_values);
        std::vector<double> qk_sigma(head_rank);
        std::vector<double> vo_sigma(head_rank);

        laplace_target_factorization_request factor_request{};
        factor_request.requested_rank = spec.head_rank;
        factor_request.relative_tolerance = relative_tolerance;
        factor_request.version = LAPLACE_TARGET_FACTORIZATION_VERSION;
        std::size_t required_factors = 0U;
        std::size_t required_sigma = 0U;

        factor_request.slot_index = spec.qk_slot_index;
        const auto qk_status = laplace_target_factorize_slot(
            &compiled, &factor_request,
            result.q_factor.data(), result.q_factor.size(),
            result.k_factor.data(), result.k_factor.size(),
            qk_sigma.data(), qk_sigma.size(),
            &required_factors, &required_sigma, &result.qk_factorization);
        if (qk_status != LAPLACE_TARGET_FACTORIZATION_OK ||
            required_factors != factor_values || required_sigma != head_rank) {
            return LAPLACE_TARGET_ATTENTION_PROJECTION_NUMERIC_FAILURE;
        }

        factor_request.slot_index = spec.vo_slot_index;
        const auto vo_status = laplace_target_factorize_slot(
            &compiled, &factor_request,
            result.v_factor.data(), result.v_factor.size(),
            result.o_factor.data(), result.o_factor.size(),
            vo_sigma.data(), vo_sigma.size(),
            &required_factors, &required_sigma, &result.vo_factorization);
        if (vo_status != LAPLACE_TARGET_FACTORIZATION_OK ||
            required_factors != factor_values || required_sigma != head_rank) {
            return LAPLACE_TARGET_ATTENTION_PROJECTION_NUMERIC_FAILURE;
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_MEMORY_FAILURE;
    }
    *output = std::move(result);
    return LAPLACE_TARGET_ATTENTION_PROJECTION_OK;
}

double AttentionDot(
    const std::vector<double>& basis,
    const std::size_t field_count,
    const std::size_t hidden_width,
    const std::size_t basis_column,
    const std::vector<double>& vector) {
    double sum = 0.0;
    double correction = 0.0;
    for (std::size_t row = 0U; row < field_count; ++row) {
        const double term = basis[row * hidden_width + basis_column] * vector[row];
        const double next = sum + term;
        if (std::abs(sum) >= std::abs(term)) {
            correction += (sum - next) + term;
        } else {
            correction += (term - next) + sum;
        }
        sum = next;
    }
    return sum + correction;
}

double AttentionNorm(const std::vector<double>& vector) {
    double sum = 0.0;
    double correction = 0.0;
    for (const double value : vector) {
        const double term = value * value;
        const double next = sum + term;
        if (std::abs(sum) >= std::abs(term)) {
            correction += (sum - next) + term;
        } else {
            correction += (term - next) + sum;
        }
        sum = next;
    }
    const double squared = sum + correction;
    return squared > 0.0 ? std::sqrt(squared) : 0.0;
}

bool AttentionCandidateColumn(
    const std::vector<double>& factor,
    const std::size_t field_count,
    const std::size_t rank,
    const std::size_t column,
    std::vector<double>* const output) {
    if (factor.size() != field_count * rank || column >= rank) return false;
    output->resize(field_count);
    for (std::size_t row = 0U; row < field_count; ++row) {
        (*output)[row] = factor[row * rank + column];
    }
    return AttentionFinite(*output);
}

bool AttentionTryAppendBasis(
    const std::vector<double>& candidate,
    const double relative_tolerance,
    const std::size_t hidden_width,
    std::vector<double>* const basis,
    std::size_t* const basis_count,
    double* const unresolved_ratio) {
    const std::size_t field_count = candidate.size();
    if (basis->size() != field_count * hidden_width || *basis_count > hidden_width) {
        return false;
    }
    std::vector<double> work(candidate);
    const double original_norm = AttentionNorm(work);
    if (!std::isfinite(original_norm)) return false;
    if (original_norm == 0.0) {
        *unresolved_ratio = 0.0;
        return true;
    }

    for (std::size_t pass = 0U; pass < 2U; ++pass) {
        for (std::size_t column = 0U; column < *basis_count; ++column) {
            const double coefficient =
                AttentionDot(*basis, field_count, hidden_width, column, work);
            for (std::size_t row = 0U; row < field_count; ++row) {
                work[row] -= coefficient * (*basis)[row * hidden_width + column];
            }
        }
    }
    const double residual_norm = AttentionNorm(work);
    if (!std::isfinite(residual_norm)) return false;
    *unresolved_ratio = residual_norm / original_norm;
    const double threshold = std::max(
        relative_tolerance,
        std::numeric_limits<double>::epsilon() * 128.0);
    if (*unresolved_ratio <= threshold) return true;
    if (*basis_count >= hidden_width) return true;

    for (double& value : work) value /= residual_norm;
    std::size_t pivot = 0U;
    double pivot_abs = 0.0;
    for (std::size_t row = 0U; row < field_count; ++row) {
        const double magnitude = std::abs(work[row]);
        if (magnitude > pivot_abs) {
            pivot_abs = magnitude;
            pivot = row;
        }
    }
    if (pivot_abs == 0.0 || !std::isfinite(pivot_abs)) return false;
    if (work[pivot] < 0.0) {
        for (double& value : work) value = -value;
    }
    for (std::size_t row = 0U; row < field_count; ++row) {
        (*basis)[row * hidden_width + *basis_count] = work[row];
    }
    ++(*basis_count);
    return true;
}

bool AttentionBuildBasis(
    const std::vector<AttentionFactorizedHead>& heads,
    const std::size_t field_count,
    const std::size_t hidden_width,
    const double relative_tolerance,
    std::vector<double>* const basis,
    std::size_t* const semantic_rank,
    double* const max_unresolved_ratio) {
    if (field_count == 0U || hidden_width == 0U || hidden_width > field_count ||
        field_count > std::numeric_limits<std::size_t>::max() / hidden_width) {
        return false;
    }
    basis->assign(field_count * hidden_width, 0.0);
    *semantic_rank = 0U;
    *max_unresolved_ratio = 0.0;
    std::vector<double> candidate;

    for (const auto& head : heads) {
        const std::size_t rank = static_cast<std::size_t>(head.spec.head_rank);
        const std::array<const std::vector<double>*, 4> factors{{
            &head.q_factor, &head.k_factor, &head.v_factor, &head.o_factor}};
        for (const auto* const factor : factors) {
            for (std::size_t column = 0U; column < rank; ++column) {
                if (!AttentionCandidateColumn(
                        *factor, field_count, rank, column, &candidate)) {
                    return false;
                }
                double unresolved = 0.0;
                if (!AttentionTryAppendBasis(
                        candidate, relative_tolerance, hidden_width, basis,
                        semantic_rank, &unresolved)) {
                    return false;
                }
                *max_unresolved_ratio = std::max(*max_unresolved_ratio, unresolved);
            }
        }
    }

    const std::size_t active_rank = *semantic_rank;
    for (std::size_t axis = 0U; axis < field_count && *semantic_rank < hidden_width; ++axis) {
        candidate.assign(field_count, 0.0);
        candidate[axis] = 1.0;
        double unresolved = 0.0;
        if (!AttentionTryAppendBasis(
                candidate, relative_tolerance, hidden_width, basis,
                semantic_rank, &unresolved)) {
            return false;
        }
    }
    if (*semantic_rank != hidden_width) return false;
    *semantic_rank = active_rank;
    return true;
}

void AttentionProjectInputFactor(
    const std::vector<double>& factor,
    const std::size_t field_count,
    const std::size_t rank,
    const std::vector<double>& basis,
    const std::size_t hidden_width,
    std::vector<double>* const output) {
    output->assign(rank * hidden_width, 0.0);
    for (std::size_t component = 0U; component < rank; ++component) {
        for (std::size_t hidden = 0U; hidden < hidden_width; ++hidden) {
            double sum = 0.0;
            double correction = 0.0;
            for (std::size_t row = 0U; row < field_count; ++row) {
                const double term =
                    factor[row * rank + component] *
                    basis[row * hidden_width + hidden];
                const double next = sum + term;
                if (std::abs(sum) >= std::abs(term)) {
                    correction += (sum - next) + term;
                } else {
                    correction += (term - next) + sum;
                }
                sum = next;
            }
            (*output)[component * hidden_width + hidden] = sum + correction;
        }
    }
}

void AttentionProjectOutputFactor(
    const std::vector<double>& factor,
    const std::size_t field_count,
    const std::size_t rank,
    const std::vector<double>& basis,
    const std::size_t hidden_width,
    std::vector<double>* const output) {
    output->assign(hidden_width * rank, 0.0);
    for (std::size_t hidden = 0U; hidden < hidden_width; ++hidden) {
        for (std::size_t component = 0U; component < rank; ++component) {
            double sum = 0.0;
            double correction = 0.0;
            for (std::size_t row = 0U; row < field_count; ++row) {
                const double term =
                    basis[row * hidden_width + hidden] *
                    factor[row * rank + component];
                const double next = sum + term;
                if (std::abs(sum) >= std::abs(term)) {
                    correction += (sum - next) + term;
                } else {
                    correction += (term - next) + sum;
                }
                sum = next;
            }
            (*output)[hidden * rank + component] = sum + correction;
        }
    }
}

void AttentionReconstructInputFactor(
    const std::vector<double>& basis,
    const std::size_t field_count,
    const std::size_t hidden_width,
    const std::vector<double>& weight,
    const std::size_t rank,
    std::vector<double>* const output) {
    output->assign(field_count * rank, 0.0);
    for (std::size_t row = 0U; row < field_count; ++row) {
        for (std::size_t component = 0U; component < rank; ++component) {
            double sum = 0.0;
            for (std::size_t hidden = 0U; hidden < hidden_width; ++hidden) {
                sum += basis[row * hidden_width + hidden] *
                    weight[component * hidden_width + hidden];
            }
            (*output)[row * rank + component] = sum;
        }
    }
}

void AttentionReconstructOutputFactor(
    const std::vector<double>& basis,
    const std::size_t field_count,
    const std::size_t hidden_width,
    const std::vector<double>& weight,
    const std::size_t rank,
    std::vector<double>* const output) {
    output->assign(field_count * rank, 0.0);
    for (std::size_t row = 0U; row < field_count; ++row) {
        for (std::size_t component = 0U; component < rank; ++component) {
            double sum = 0.0;
            for (std::size_t hidden = 0U; hidden < hidden_width; ++hidden) {
                sum += basis[row * hidden_width + hidden] *
                    weight[hidden * rank + component];
            }
            (*output)[row * rank + component] = sum;
        }
    }
}

double AttentionPairResidual(
    const std::vector<double>& source,
    const std::vector<double>& left,
    const std::vector<double>& right,
    const std::size_t field_count,
    const std::size_t rank) {
    if (source.size() != field_count * field_count ||
        left.size() != field_count * rank || right.size() != field_count * rank) {
        return std::numeric_limits<double>::infinity();
    }
    double source_squared = 0.0;
    double residual_squared = 0.0;
    for (std::size_t row = 0U; row < field_count; ++row) {
        for (std::size_t column = 0U; column < field_count; ++column) {
            double reconstructed = 0.0;
            for (std::size_t component = 0U; component < rank; ++component) {
                reconstructed += left[row * rank + component] *
                    right[column * rank + component];
            }
            const double expected = source[row * field_count + column];
            const double delta = expected - reconstructed;
            source_squared += expected * expected;
            residual_squared += delta * delta;
        }
    }
    if (!std::isfinite(source_squared) || !std::isfinite(residual_squared) ||
        source_squared < 0.0 || residual_squared < 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const double source_norm = std::sqrt(source_squared);
    const double residual_norm = std::sqrt(residual_squared);
    return source_norm > 0.0 ? residual_norm / source_norm : residual_norm;
}

void AttentionIdentifyHead(laplace_target_attention_head_receipt* const receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, AttentionHeadDomain, sizeof(AttentionHeadDomain) - 1U);
    HashDigest(&hasher, receipt->qk_slot_id);
    HashDigest(&hasher, receipt->vo_slot_id);
    HashDigest(&hasher, receipt->qk_operator_id);
    HashDigest(&hasher, receipt->vo_operator_id);
    HashDigest(&hasher, receipt->qk_factorization_id);
    HashDigest(&hasher, receipt->vo_factorization_id);
    HashDigest(&hasher, receipt->q_tensor_fingerprint);
    HashDigest(&hasher, receipt->k_tensor_fingerprint);
    HashDigest(&hasher, receipt->v_tensor_fingerprint);
    HashDigest(&hasher, receipt->o_tensor_fingerprint);
    HashU64(&hasher, receipt->field_count);
    HashU64(&hasher, receipt->hidden_width);
    HashU64(&hasher, receipt->head_rank);
    HashF64(&hasher, receipt->qk_relative_residual);
    HashF64(&hasher, receipt->vo_relative_residual);
    HashU32(&hasher, receipt->layer_index);
    HashU32(&hasher, receipt->head_index);
    HashU32(&hasher, receipt->expert_index);
    HashU32(&hasher, receipt->status);
    Finish(&hasher, &receipt->head_projection_id);
}

void AttentionIdentifyProjection(
    const laplace_target_attention_projection_result& result,
    laplace_target_attention_projection_receipt* const receipt) {
    blake3_hasher head_hasher{};
    blake3_hasher_init(&head_hasher);
    blake3_hasher_update(
        &head_hasher, AttentionHeadSetDomain, sizeof(AttentionHeadSetDomain) - 1U);
    for (const auto& head : result.heads) {
        HashDigest(&head_hasher, head.receipt.head_projection_id);
    }
    Finish(&head_hasher, &receipt->head_set_fingerprint);

    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, AttentionProjectionDomain, sizeof(AttentionProjectionDomain) - 1U);
    HashDigest(&hasher, receipt->embedding_fingerprint);
    HashDigest(&hasher, receipt->head_set_fingerprint);
    HashU64(&hasher, receipt->field_count);
    HashU64(&hasher, receipt->hidden_width);
    HashU64(&hasher, receipt->semantic_basis_rank);
    HashU64(&hasher, receipt->null_basis_rank);
    HashU64(&hasher, receipt->head_count);
    HashU64(&hasher, receipt->tensor_value_count);
    HashF64(&hasher, receipt->max_relative_residual);
    HashU32(&hasher, receipt->flags);
    HashU32(&hasher, receipt->status);
    HashU32(&hasher, receipt->version);
    Finish(&hasher, &receipt->projection_id);
}

laplace_target_attention_projection_status AttentionCopyVector(
    const std::vector<double>& values,
    double* const output,
    const std::size_t output_capacity,
    std::size_t* const required_count) {
    if (required_count == nullptr) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    }
    *required_count = values.size();
    if (output == nullptr || output_capacity < values.size()) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_CAPACITY_INSUFFICIENT;
    }
    std::copy(values.begin(), values.end(), output);
    return LAPLACE_TARGET_ATTENTION_PROJECTION_OK;
}

}  // namespace

struct laplace_target_attention_projection_result {
    std::vector<double> embedding;
    std::vector<AttentionProjectedHead> heads;
};

extern "C" laplace_target_attention_projection_status
laplace_target_attention_project(
    const laplace_target_compile_result* const compiled,
    const laplace_target_attention_projection_request* const request,
    laplace_target_attention_projection_result** const result,
    laplace_target_attention_projection_receipt* const receipt) {
    if (compiled == nullptr || request == nullptr || result == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    }
    *result = nullptr;
    *receipt = laplace_target_attention_projection_receipt{};
    if (!AttentionRequestValid(*request)) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_REQUEST;
    }

    std::vector<AttentionFactorizedHead> factors;
    try {
        factors.resize(request->head_count);
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_MEMORY_FAILURE;
    }
    for (std::size_t index = 0U; index < request->head_count; ++index) {
        const auto status = AttentionFactorizeHead(
            *compiled, request->heads[index], request->relative_tolerance, &factors[index]);
        if (status != LAPLACE_TARGET_ATTENTION_PROJECTION_OK) return status;
    }
    std::sort(factors.begin(), factors.end(), AttentionHeadLess);

    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> coordinates;
    const auto& first_slot = factors.front().qk_slot;
    for (const auto& head : factors) {
        if (!AttentionSameDomain(first_slot, head.qk_slot)) {
            return LAPLACE_TARGET_ATTENTION_PROJECTION_DOMAIN_MISMATCH;
        }
        const auto coordinate = std::make_tuple(
            head.qk_slot.layer_index, head.qk_slot.head_index, head.qk_slot.expert_index);
        if (!coordinates.insert(coordinate).second) {
            return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_HEAD;
        }
    }

    const std::size_t field_count = static_cast<std::size_t>(first_slot.field_count);
    const std::size_t hidden_width = static_cast<std::size_t>(request->hidden_width);
    if (request->hidden_width > first_slot.field_count ||
        field_count == 0U || hidden_width == 0U || hidden_width > field_count) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_REQUEST;
    }

    auto projected = std::unique_ptr<laplace_target_attention_projection_result>(
        new (std::nothrow) laplace_target_attention_projection_result());
    if (!projected) return LAPLACE_TARGET_ATTENTION_PROJECTION_MEMORY_FAILURE;

    std::size_t semantic_rank = 0U;
    double max_basis_unresolved = 0.0;
    try {
        if (!AttentionBuildBasis(
                factors, field_count, hidden_width, request->relative_tolerance,
                &projected->embedding, &semantic_rank, &max_basis_unresolved)) {
            return LAPLACE_TARGET_ATTENTION_PROJECTION_NUMERIC_FAILURE;
        }
        projected->heads.resize(factors.size());
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_MEMORY_FAILURE;
    }

    double max_relative_residual = 0.0;
    std::uint64_t tensor_value_count = static_cast<std::uint64_t>(projected->embedding.size());
    for (std::size_t index = 0U; index < factors.size(); ++index) {
        const auto& source = factors[index];
        auto& target = projected->heads[index];
        const std::size_t rank = static_cast<std::size_t>(source.spec.head_rank);
        try {
            AttentionProjectInputFactor(
                source.q_factor, field_count, rank, projected->embedding,
                hidden_width, &target.q);
            AttentionProjectInputFactor(
                source.k_factor, field_count, rank, projected->embedding,
                hidden_width, &target.k);
            AttentionProjectInputFactor(
                source.v_factor, field_count, rank, projected->embedding,
                hidden_width, &target.v);
            AttentionProjectOutputFactor(
                source.o_factor, field_count, rank, projected->embedding,
                hidden_width, &target.o);

            std::vector<double> q_reconstructed;
            std::vector<double> k_reconstructed;
            std::vector<double> v_reconstructed;
            std::vector<double> o_reconstructed;
            AttentionReconstructInputFactor(
                projected->embedding, field_count, hidden_width,
                target.q, rank, &q_reconstructed);
            AttentionReconstructInputFactor(
                projected->embedding, field_count, hidden_width,
                target.k, rank, &k_reconstructed);
            AttentionReconstructInputFactor(
                projected->embedding, field_count, hidden_width,
                target.v, rank, &v_reconstructed);
            AttentionReconstructOutputFactor(
                projected->embedding, field_count, hidden_width,
                target.o, rank, &o_reconstructed);

            const double qk_residual = AttentionPairResidual(
                compiled->matrices[source.spec.qk_slot_index],
                q_reconstructed, k_reconstructed, field_count, rank);
            const double vo_residual = AttentionPairResidual(
                compiled->matrices[source.spec.vo_slot_index],
                v_reconstructed, o_reconstructed, field_count, rank);
            if (!std::isfinite(qk_residual) || !std::isfinite(vo_residual)) {
                return LAPLACE_TARGET_ATTENTION_PROJECTION_NUMERIC_FAILURE;
            }

            target.receipt.qk_slot_id = source.qk_slot.slot_id;
            target.receipt.vo_slot_id = source.vo_slot.slot_id;
            target.receipt.qk_operator_id = source.qk_slot.operator_id;
            target.receipt.vo_operator_id = source.vo_slot.operator_id;
            target.receipt.qk_factorization_id = source.qk_factorization.factorization_id;
            target.receipt.vo_factorization_id = source.vo_factorization.factorization_id;
            target.receipt.field_count = source.qk_slot.field_count;
            target.receipt.hidden_width = request->hidden_width;
            target.receipt.head_rank = source.spec.head_rank;
            target.receipt.qk_relative_residual = qk_residual;
            target.receipt.vo_relative_residual = vo_residual;
            target.receipt.layer_index = source.qk_slot.layer_index;
            target.receipt.head_index = source.qk_slot.head_index;
            target.receipt.expert_index = source.qk_slot.expert_index;
            target.receipt.status = LAPLACE_TARGET_ATTENTION_PROJECTION_OK;
            AttentionValuesFingerprint(
                AttentionQDomain, sizeof(AttentionQDomain) - 1U,
                target.q, &target.receipt.q_tensor_fingerprint);
            AttentionValuesFingerprint(
                AttentionKDomain, sizeof(AttentionKDomain) - 1U,
                target.k, &target.receipt.k_tensor_fingerprint);
            AttentionValuesFingerprint(
                AttentionVDomain, sizeof(AttentionVDomain) - 1U,
                target.v, &target.receipt.v_tensor_fingerprint);
            AttentionValuesFingerprint(
                AttentionODomain, sizeof(AttentionODomain) - 1U,
                target.o, &target.receipt.o_tensor_fingerprint);
            AttentionIdentifyHead(&target.receipt);
            max_relative_residual = std::max(
                max_relative_residual, std::max(qk_residual, vo_residual));

            const std::uint64_t head_values =
                static_cast<std::uint64_t>(target.q.size() + target.k.size() +
                                           target.v.size() + target.o.size());
            if (tensor_value_count > std::numeric_limits<std::uint64_t>::max() - head_values) {
                return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_REQUEST;
            }
            tensor_value_count += head_values;
        } catch (const std::bad_alloc&) {
            return LAPLACE_TARGET_ATTENTION_PROJECTION_MEMORY_FAILURE;
        }
    }

    const bool exact_required =
        (request->flags & LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT) != 0U;
    const bool insufficient = exact_required &&
        (max_basis_unresolved > request->relative_tolerance ||
         max_relative_residual > request->relative_tolerance);
    if (insufficient) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_HIDDEN_WIDTH_INSUFFICIENT;
    }

    receipt->field_count = first_slot.field_count;
    receipt->hidden_width = request->hidden_width;
    receipt->semantic_basis_rank = static_cast<std::uint64_t>(semantic_rank);
    receipt->null_basis_rank = request->hidden_width - receipt->semantic_basis_rank;
    receipt->head_count = static_cast<std::uint64_t>(projected->heads.size());
    receipt->tensor_value_count = tensor_value_count;
    receipt->max_relative_residual = max_relative_residual;
    receipt->flags = request->flags;
    receipt->status = LAPLACE_TARGET_ATTENTION_PROJECTION_OK;
    receipt->version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;
    AttentionValuesFingerprint(
        AttentionEmbeddingDomain, sizeof(AttentionEmbeddingDomain) - 1U,
        projected->embedding, &receipt->embedding_fingerprint);
    AttentionIdentifyProjection(*projected, receipt);

    *result = projected.release();
    return LAPLACE_TARGET_ATTENTION_PROJECTION_OK;
}

extern "C" void laplace_target_attention_projection_result_destroy(
    laplace_target_attention_projection_result** const result) {
    if (result == nullptr) return;
    delete *result;
    *result = nullptr;
}

extern "C" size_t laplace_target_attention_projection_head_count(
    const laplace_target_attention_projection_result* const result) {
    return result == nullptr ? 0U : result->heads.size();
}

extern "C" laplace_target_attention_projection_status
laplace_target_attention_projection_head_receipt(
    const laplace_target_attention_projection_result* const result,
    const size_t head_index,
    laplace_target_attention_head_receipt* const receipt) {
    if (result == nullptr || receipt == nullptr || head_index >= result->heads.size()) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    }
    *receipt = result->heads[head_index].receipt;
    return LAPLACE_TARGET_ATTENTION_PROJECTION_OK;
}

extern "C" laplace_target_attention_projection_status
laplace_target_attention_projection_embedding(
    const laplace_target_attention_projection_result* const result,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (result == nullptr) return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    return AttentionCopyVector(result->embedding, output, output_capacity, required_count);
}

extern "C" laplace_target_attention_projection_status
laplace_target_attention_projection_q(
    const laplace_target_attention_projection_result* const result,
    const size_t head_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (result == nullptr || head_index >= result->heads.size()) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    }
    return AttentionCopyVector(
        result->heads[head_index].q, output, output_capacity, required_count);
}

extern "C" laplace_target_attention_projection_status
laplace_target_attention_projection_k(
    const laplace_target_attention_projection_result* const result,
    const size_t head_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (result == nullptr || head_index >= result->heads.size()) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    }
    return AttentionCopyVector(
        result->heads[head_index].k, output, output_capacity, required_count);
}

extern "C" laplace_target_attention_projection_status
laplace_target_attention_projection_v(
    const laplace_target_attention_projection_result* const result,
    const size_t head_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (result == nullptr || head_index >= result->heads.size()) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    }
    return AttentionCopyVector(
        result->heads[head_index].v, output, output_capacity, required_count);
}

extern "C" laplace_target_attention_projection_status
laplace_target_attention_projection_o(
    const laplace_target_attention_projection_result* const result,
    const size_t head_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (result == nullptr || head_index >= result->heads.size()) {
        return LAPLACE_TARGET_ATTENTION_PROJECTION_INVALID_ARGUMENT;
    }
    return AttentionCopyVector(
        result->heads[head_index].o, output, output_capacity, required_count);
}
