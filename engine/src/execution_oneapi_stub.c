#include "laplace/execution.h"

laplace_execution_status laplace_execution_oneapi_provider(
    laplace_execution_oneapi_provider_state* state,
    laplace_execution_runtime_provider_v1* provider) {
    (void)state;
    (void)provider;
    return LAPLACE_EXECUTION_PLATFORM_UNSUPPORTED;
}
