#include "laplace/execution.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

laplace_execution_status Prepare(
    void*,
    const laplace_execution_grant*,
    const laplace_execution_work_plan*) {
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status Task(
    void*,
    const laplace_execution_chunk* const chunk,
    laplace_digest256* const result) {
    std::memset(
        result->bytes,
        static_cast<int>((chunk->chunk_index % UINT64_C(251)) + UINT64_C(1)),
        sizeof(result->bytes));
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status Run(
    void*,
    const laplace_execution_work_plan*,
    const laplace_execution_chunk* const chunks,
    const std::size_t chunk_count,
    void* const task_state,
    const laplace_execution_work_task_fn task,
    laplace_execution_chunk_result* const results) {
    for (std::size_t index = 0U; index < chunk_count; ++index) {
        const laplace_execution_status status = task(
            task_state, &chunks[index], &results[index].result_fingerprint);
        if (status != LAPLACE_EXECUTION_OK) {
            results[index].state = LAPLACE_EXECUTION_CHUNK_FAILED;
            results[index].task_status = static_cast<std::uint32_t>(status);
            return status;
        }
        results[index].state = LAPLACE_EXECUTION_CHUNK_COMPLETE;
        results[index].task_status = LAPLACE_EXECUTION_OK;
    }
    return LAPLACE_EXECUTION_OK;
}

laplace_execution_status Finish(void*) {
    return LAPLACE_EXECUTION_OK;
}

void Abort(void*) {}

}  // namespace

int main() {
    const laplace_execution_grant grant{4096U, 2U, 1U};
    const laplace_execution_work_request request{
        8U, 256U, 16U, 2U, 2U, 1U, 0U, 0U};
    laplace_execution_runtime_provider_v1 provider{};
    provider.prepare = Prepare;
    provider.run = Run;
    provider.finish = Finish;
    provider.abort = Abort;
    provider.abi_major = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_EXECUTION_RUNTIME_PROVIDER_ABI_MINOR;
    laplace_execution_work_receipt receipt{};
    return laplace_execution_run_work(
               &grant, &request, &provider, nullptr, Task, &receipt) ==
            LAPLACE_EXECUTION_OK
        ? 0
        : 2;
}
