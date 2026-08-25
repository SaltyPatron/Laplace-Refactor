#include "laplace/unicode_root.h"

#include "laplace/contract/oneapi-provider.h"

#include "blake3.h"
#include "mkl_service.h"
#include "mkl_vml.h"

#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <limits>
#include <mutex>

namespace {

constexpr std::uint8_t ProviderDomain[] =
    "laplace-unicode-root-numeric-provider-v1";
constexpr std::uint8_t EnvironmentDomain[] =
    "laplace-unicode-root-numeric-environment-v1";
constexpr std::uint8_t InputDomain[] =
    "laplace-unicode-root-numeric-input-v1";
constexpr std::uint8_t OutputDomain[] =
    "laplace-unicode-root-numeric-output-v1";
constexpr std::uint8_t ReceiptDomain[] =
    "laplace-unicode-root-numeric-receipt-v1";
constexpr double Phi = 0x1.6a09e667f3bcdp+0;
constexpr double Psi = 0x1.88a3eaa601609p+0;
constexpr double TwoPi = 0x1.921fb54442d18p+2;
constexpr std::uint32_t MxcsrFtz = UINT32_C(1) << 15U;
constexpr std::uint32_t MxcsrDaz = UINT32_C(1) << 6U;
constexpr std::uint32_t MxcsrExceptionFlags = UINT32_C(0x3f);
constexpr MKL_INT64 VmlMode =
    static_cast<MKL_INT64>(VML_HA | VML_FTZDAZ_OFF | VML_ERRMODE_DEFAULT);
std::mutex ProviderMutex;
bool RuntimeConfigured = false;

void HashU32(blake3_hasher* hasher, const std::uint32_t value) {
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

void HashU64(blake3_hasher* hasher, const std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (std::size_t index = 0U; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

void HashDouble(blake3_hasher* hasher, const double value) {
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    HashU64(hasher, bits);
}

void Finish(blake3_hasher* hasher, laplace_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

laplace_digest256 ProviderFingerprint() {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ProviderDomain, sizeof(ProviderDomain) - 1U);
    const char* const bindings[] = {
        LAPLACE_ONEAPI_SELECTION_SHA256,
        LAPLACE_ONEAPI_RUNTIME_VERSION,
        LAPLACE_ONEAPI_MKL_VERSION,
        "IntelLLVM-2026.1.1",
        "-O3|-march=haswell|-fp-model=strict|-fno-fast-math|-ffp-contract=off",
        "vmdSqrt|vmdSinCos|VML_HA|VML_FTZDAZ_OFF|VML_ERRMODE_DEFAULT",
        "MKL_THREADING_SEQUENTIAL|MKL_ENABLE_AVX2"};
    for (const char* const binding : bindings) {
        const std::size_t bytes = std::strlen(binding);
        HashU64(&hasher, bytes);
        blake3_hasher_update(&hasher, binding, bytes);
    }
    HashDouble(&hasher, Phi);
    HashDouble(&hasher, Psi);
    HashDouble(&hasher, TwoPi);
    HashU32(&hasher, LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MAJOR);
    HashU32(&hasher, LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MINOR);
    Finish(&hasher, &result);
    return result;
}

laplace_digest256 EnvironmentFingerprint(const std::uint32_t mxcsr) {
    blake3_hasher hasher{};
    laplace_digest256 result{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, EnvironmentDomain, sizeof(EnvironmentDomain) - 1U);
    HashU32(&hasher, FE_TONEAREST);
    HashU32(&hasher, mxcsr);
    HashU64(&hasher, static_cast<std::uint64_t>(VmlMode));
    HashU32(&hasher, MKL_THREADING_SEQUENTIAL);
    HashU32(&hasher, MKL_ENABLE_AVX2);
    Finish(&hasher, &result);
    return result;
}

laplace_unicode_status Workspace(
    void*, const std::size_t rank_count, std::size_t* const workspace_bytes) {
    constexpr std::size_t Arrays = 8U;
    if (workspace_bytes == nullptr || rank_count == 0U) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    if (rank_count >
        (std::numeric_limits<std::size_t>::max() - 63U) /
            (Arrays * sizeof(double))) {
        return LAPLACE_UNICODE_SIZE_OVERFLOW;
    }
    *workspace_bytes = rank_count * Arrays * sizeof(double) + 63U;
    return LAPLACE_UNICODE_OK;
}

void HashReceipt(laplace_unicode_numeric_receipt* const receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ReceiptDomain, sizeof(ReceiptDomain) - 1U);
    HashU32(&hasher, receipt->status);
    HashU32(&hasher, receipt->threading_layer);
    HashU32(&hasher, receipt->instruction_branch);
    HashU32(&hasher, receipt->vml_status);
    HashU32(&hasher, receipt->floating_exceptions);
    HashU32(&hasher, receipt->system_error);
    HashU64(&hasher, receipt->first_rank);
    HashU64(&hasher, receipt->rank_count);
    blake3_hasher_update(
        &hasher, receipt->provider_fingerprint.bytes,
        sizeof(receipt->provider_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->environment_fingerprint.bytes,
        sizeof(receipt->environment_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->input_fingerprint.bytes,
        sizeof(receipt->input_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->output_fingerprint.bytes,
        sizeof(receipt->output_fingerprint.bytes));
    Finish(&hasher, &receipt->receipt_id);
}

laplace_unicode_status Calculate(
    void*, const std::uint32_t first_rank, const std::size_t rank_count,
    void* const raw_workspace, const std::size_t workspace_bytes,
    laplace_point4d* const coordinates,
    laplace_unicode_hopf_point* const hopf_points,
    laplace_unicode_numeric_receipt* const receipt) {
    std::size_t required = 0U;
    if (raw_workspace == nullptr || coordinates == nullptr ||
        hopf_points == nullptr || receipt == nullptr ||
        Workspace(nullptr, rank_count, &required) != LAPLACE_UNICODE_OK ||
        workspace_bytes < required ||
        rank_count > static_cast<std::size_t>(LAPLACE_UNICODE_ROOT_POPULATION) ||
        first_rank >= LAPLACE_UNICODE_ROOT_POPULATION ||
        rank_count > static_cast<std::size_t>(LAPLACE_UNICODE_ROOT_POPULATION -
                                              first_rank) ||
        rank_count > static_cast<std::size_t>(
            std::numeric_limits<MKL_INT>::max())) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    std::memset(receipt, 0, sizeof(*receipt));
    receipt->provider_fingerprint = ProviderFingerprint();
    receipt->first_rank = first_rank;
    receipt->rank_count = rank_count;
    const auto address = reinterpret_cast<std::uintptr_t>(raw_workspace);
    const auto aligned = (address + std::uintptr_t{63U}) &
        ~std::uintptr_t{63U};
    auto* const base = reinterpret_cast<double*>(aligned);
    double* const radius = base;
    double* const complement_radius = radius + rank_count;
    double* const alpha = complement_radius + rank_count;
    double* const beta = alpha + rank_count;
    double* const sin_alpha = beta + rank_count;
    double* const cos_alpha = sin_alpha + rank_count;
    double* const sin_beta = cos_alpha + rank_count;
    double* const cos_beta = sin_beta + rank_count;

    std::lock_guard<std::mutex> lock(ProviderMutex);
    const int prior_rounding = std::fegetround();
    const std::uint32_t prior_mxcsr = _mm_getcsr();
    const std::uint32_t canonical_mxcsr = prior_mxcsr &
        ~(MxcsrFtz | MxcsrDaz | MxcsrExceptionFlags);
    if (std::fesetround(FE_TONEAREST) != 0) {
        receipt->status = LAPLACE_UNICODE_PROVIDER_FAILURE;
        HashReceipt(receipt);
        return LAPLACE_UNICODE_PROVIDER_FAILURE;
    }
    _mm_setcsr(canonical_mxcsr);
    const int threading_layer = RuntimeConfigured
        ? MKL_THREADING_SEQUENTIAL
        : mkl_set_threading_layer(MKL_THREADING_SEQUENTIAL);
    const int instruction_branch = mkl_enable_instructions(MKL_ENABLE_AVX2);
    receipt->threading_layer = static_cast<std::uint32_t>(threading_layer);
    receipt->instruction_branch = static_cast<std::uint32_t>(instruction_branch);
    if (threading_layer != MKL_THREADING_SEQUENTIAL ||
        instruction_branch == 0) {
        _mm_setcsr(prior_mxcsr);
        (void)std::fesetround(prior_rounding);
        receipt->status = LAPLACE_UNICODE_PROVIDER_FAILURE;
        HashReceipt(receipt);
        return LAPLACE_UNICODE_PROVIDER_FAILURE;
    }
    RuntimeConfigured = true;
    const int prior_threads = mkl_set_num_threads_local(1);
    receipt->environment_fingerprint = EnvironmentFingerprint(canonical_mxcsr);
    blake3_hasher input_hasher{};
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(&input_hasher, InputDomain, sizeof(InputDomain) - 1U);
    HashU32(&input_hasher, first_rank);
    HashU64(&input_hasher, rank_count);
    Finish(&input_hasher, &receipt->input_fingerprint);

    for (std::size_t index = 0U; index < rank_count; ++index) {
        const double s = static_cast<double>(first_rank) +
            static_cast<double>(index) + 0.5;
        const double t = s / static_cast<double>(LAPLACE_UNICODE_ROOT_POPULATION);
        radius[index] = t;
        complement_radius[index] = 1.0 - t;
        const double d = TwoPi * s;
        alpha[index] = d / Phi;
        beta[index] = d / Psi;
    }
    errno = 0;
    std::feclearexcept(FE_ALL_EXCEPT);
    (void)vmlClearErrStatus();
    const MKL_INT count = static_cast<MKL_INT>(rank_count);
    vmdSqrt(count, radius, radius, VmlMode);
    vmdSqrt(count, complement_radius, complement_radius, VmlMode);
    vmdSinCos(count, alpha, sin_alpha, cos_alpha, VmlMode);
    vmdSinCos(count, beta, sin_beta, cos_beta, VmlMode);
    const int vml_status = vmlGetErrStatus();
    const int floating_status = std::fetestexcept(
        FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);
    const int system_error = errno;
    receipt->vml_status = static_cast<std::uint32_t>(vml_status);
    receipt->floating_exceptions = static_cast<std::uint32_t>(floating_status);
    receipt->system_error = static_cast<std::uint32_t>(system_error);

    blake3_hasher output_hasher{};
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, OutputDomain, sizeof(OutputDomain) - 1U);
    for (std::size_t index = 0U; index < rank_count; ++index) {
#if defined(LAPLACE_TEST_UNICODE_NUMERIC_SWAP_FIRST_AXES)
        coordinates[index].component[0] = radius[index] * cos_alpha[index];
        coordinates[index].component[1] = radius[index] * sin_alpha[index];
#else
        coordinates[index].component[0] = radius[index] * sin_alpha[index];
        coordinates[index].component[1] = radius[index] * cos_alpha[index];
#endif
        coordinates[index].component[2] =
            complement_radius[index] * sin_beta[index];
        coordinates[index].component[3] =
            complement_radius[index] * cos_beta[index];
        const double x = coordinates[index].component[0];
        const double y = coordinates[index].component[1];
        const double z = coordinates[index].component[2];
        const double w = coordinates[index].component[3];
        hopf_points[index].component[0] = 2.0 * (x * z + y * w);
        hopf_points[index].component[1] = 2.0 * (y * z - x * w);
        hopf_points[index].component[2] = x * x + y * y - z * z - w * w;
        for (double component : coordinates[index].component) {
            HashDouble(&output_hasher, component);
        }
        for (double component : hopf_points[index].component) {
            HashDouble(&output_hasher, component);
        }
    }
    Finish(&output_hasher, &receipt->output_fingerprint);
    (void)mkl_set_num_threads_local(prior_threads);
    _mm_setcsr(prior_mxcsr);
    (void)std::fesetround(prior_rounding);
    if (vml_status != 0 || floating_status != 0 || system_error != 0) {
        receipt->status = LAPLACE_UNICODE_PROVIDER_FAILURE;
        HashReceipt(receipt);
        return LAPLACE_UNICODE_PROVIDER_FAILURE;
    }
    receipt->status = LAPLACE_UNICODE_OK;
    HashReceipt(receipt);
    return LAPLACE_UNICODE_OK;
}

}  // namespace

extern "C" laplace_unicode_status laplace_unicode_numeric_oneapi_provider(
    laplace_unicode_numeric_provider_v1* const provider) {
    if (provider == nullptr) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    laplace_unicode_numeric_provider_v1 value{};
    value.provider_fingerprint = ProviderFingerprint();
    value.workspace = Workspace;
    value.calculate = Calculate;
    value.abi_major = LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MAJOR;
    value.abi_minor = LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MINOR;
    *provider = value;
    return LAPLACE_UNICODE_OK;
}
