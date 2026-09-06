#include "laplace/cognition_observation_request.h"

extern "C" laplace_cognition_observation_request_status
laplace_cognition_observation_request_execute_with_provider(
    const laplace_cognition_observation_request* const request,
    const laplace_cognition_forward_provider_v1* const provider,
    laplace_cognition_forward_result** const result,
    laplace_cognition_forward_receipt* const receipt) {
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) *receipt = laplace_cognition_forward_receipt{};
    if (request == nullptr || provider == nullptr || result == nullptr ||
        receipt == nullptr || provider->enumerate == nullptr ||
        provider->execute == nullptr) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_INVALID_ARGUMENT;
    }
    if (provider->abi_major != LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR ||
        provider->abi_minor > LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR ||
        provider->flags != 0U || provider->reserved != 0U) {
        return LAPLACE_COGNITION_OBSERVATION_REQUEST_PROVIDER_FAILURE;
    }

    laplace_cognition_observation_compiled_request compiled{};
    auto status = laplace_cognition_observation_request_compile(request, &compiled);
    if (status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) return status;

    const auto forward_status = laplace_cognition_forward_pass_execute(
        &compiled.forward_program,
        compiled.guidance_state,
        provider,
        result,
        receipt);
    if (forward_status != LAPLACE_COGNITION_FORWARD_OK) {
        laplace_cognition_forward_result_destroy(result);
        *receipt = laplace_cognition_forward_receipt{};
        status = LAPLACE_COGNITION_OBSERVATION_REQUEST_EXECUTION_FAILURE;
    }

    laplace_cognition_observation_compiled_request_destroy(&compiled);
    return status;
}
