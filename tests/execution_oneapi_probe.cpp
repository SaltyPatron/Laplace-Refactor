#include "laplace/execution.h"

#include "mkl_service.h"

#include <cstdint>
#include <cstring>

namespace {

struct ProbeState final {
    std::uint32_t expected_threads;
};

laplace_execution_status VerifyMklGrant(
    void* const raw_state,
    const laplace_execution_chunk* const chunk,
    laplace_digest256* const result) {
    const auto& state = *static_cast<ProbeState*>(raw_state);
    if (mkl_get_max_threads() != static_cast<int>(state.expected_threads)) {
        return LAPLACE_EXECUTION_RESOURCE_INSUFFICIENT;
    }
    std::memset(
        result->bytes,
        static_cast<int>((chunk->chunk_index % UINT64_C(251)) + UINT64_C(1)),
        sizeof(result->bytes));
    return LAPLACE_EXECUTION_OK;
}

}  // namespace

int main() {
    const int prior_threads = mkl_get_max_threads();
    mkl_set_num_threads(7);
    const laplace_execution_grant grant{UINT64_C(4096), 2U, 0U};
    const laplace_execution_work_request request{
        8U, 256U, 16U, 2U, 2U, 1U, 0U, 0U};
    laplace_execution_oneapi_provider_state provider_state{};
    laplace_execution_runtime_provider_v1 provider{};
    if (laplace_execution_oneapi_provider(&provider_state, &provider) !=
        LAPLACE_EXECUTION_OK) {
        mkl_set_num_threads(prior_threads);
        return 2;
    }
    ProbeState task_state{1U};
    laplace_execution_work_receipt receipt{};
    const laplace_execution_status status = laplace_execution_run_work(
        &grant, &request, &provider, &task_state, VerifyMklGrant, &receipt);
    mkl_set_num_threads(prior_threads);
    return status == LAPLACE_EXECUTION_OK ? 0 : 3;
}
