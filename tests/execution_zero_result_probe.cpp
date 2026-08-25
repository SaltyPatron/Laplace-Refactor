#include "laplace/execution.h"

#include <cstring>

namespace {

laplace_execution_status ZeroFingerprint(
    void*,
    const laplace_execution_chunk*,
    laplace_digest256* const result) {
    std::memset(result, 0, sizeof(*result));
    return LAPLACE_EXECUTION_OK;
}

}  // namespace

int main() {
    const laplace_execution_grant grant{4096U, 2U, 1U};
    const laplace_execution_work_request request{
        8U, 256U, 16U, 2U, 2U, 1U, 0U, 0U};
    laplace_execution_runtime_provider_v1 provider{};
    if (laplace_execution_serial_provider(&provider) != LAPLACE_EXECUTION_OK) {
        return 2;
    }
    laplace_execution_work_receipt receipt{};
    return laplace_execution_run_work(
               &grant, &request, &provider, nullptr, ZeroFingerprint, &receipt) ==
            LAPLACE_EXECUTION_OK
        ? 0
        : 3;
}
