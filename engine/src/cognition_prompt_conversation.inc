#include "laplace/cognition_prompt_conversation.h"

#include <new>
#include <stdexcept>
#include <vector>

namespace laplace_cognition_prompt_conversation_detail {

laplace_cognition_prompt_conversation_status ProviderSetStatus(
    const laplace_cognition_observation_provider_set_status status) {
    if (status == LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_MEMORY_FAILURE) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE;
    }
    return LAPLACE_COGNITION_PROMPT_CONVERSATION_PROVIDER_SET_FAILURE;
}

}  // namespace laplace_cognition_prompt_conversation_detail

extern "C" laplace_cognition_prompt_conversation_status
laplace_cognition_prompt_conversation_execute_encoded(
    laplace_cognition_prompt_admission* const admission,
    const laplace_cognition_prompt_conversation_request* const request,
    const std::uint32_t output_encoding,
    const std::uint8_t* const previous_frame,
    const std::size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* const additional_cognition_providers,
    const std::size_t additional_cognition_provider_count,
    const laplace_cognition_realization_provider_v1* const realization_provider,
    const laplace_cognition_materialization_provider_v1* const materialization_provider,
    std::uint8_t* const output,
    const std::size_t output_capacity,
    std::size_t* const output_bytes,
    std::uint8_t* const next_discourse_frame,
    const std::size_t next_discourse_frame_capacity,
    std::size_t* const next_discourse_frame_bytes,
    laplace_cognition_prompt_conversation_result* const result) {
    if (output_bytes != nullptr) *output_bytes = 0U;
    if (next_discourse_frame_bytes != nullptr) *next_discourse_frame_bytes = 0U;
    if (result != nullptr) *result = laplace_cognition_prompt_conversation_result{};

    if (admission == nullptr || request == nullptr || realization_provider == nullptr ||
        materialization_provider == nullptr || output == nullptr || output_bytes == nullptr ||
        next_discourse_frame == nullptr || next_discourse_frame_bytes == nullptr ||
        result == nullptr ||
        (additional_cognition_provider_count != 0U &&
         additional_cognition_providers == nullptr)) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_INVALID_ARGUMENT;
    }
    if (request->version != LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION ||
        request->reserved != 0U) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_INVALID_VERSION;
    }

    if (output_encoding != LAPLACE_COGNITION_OUTPUT_UTF8 &&
        output_encoding != LAPLACE_COGNITION_OUTPUT_OCTETS) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_ENCODING_INVALID;
    }

    std::vector<laplace_cognition_observation_candidate_provider_v1> providers;
    // One descriptor is reserved for the prompt's structural provider. Check
    // before +1 or indexing a caller buffer, including SIZE_MAX-sized requests.
    if (additional_cognition_provider_count >= providers.max_size()) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE;
    }

    laplace_cognition_prompt_admission_view admission_view{};
    if (laplace_cognition_prompt_admission_view_get(admission, &admission_view) !=
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_ADMISSION_FAILURE;
    }

    laplace_cognition_observation_candidate_provider_v1 structural_provider{};
    if (laplace_cognition_prompt_admission_structural_provider(
            admission, &structural_provider) != LAPLACE_COGNITION_PROMPT_ADMISSION_OK) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_STRUCTURAL_PROVIDER_FAILURE;
    }

    try {
        providers.reserve(additional_cognition_provider_count + 1U);
        providers.push_back(structural_provider);
        for (std::size_t index = 0U; index < additional_cognition_provider_count; ++index) {
            providers.push_back(additional_cognition_providers[index]);
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE;
    }

    laplace_cognition_observation_candidate_provider_set* provider_set = nullptr;
    laplace_cognition_observation_candidate_provider_v1 composite_provider{};
    const auto provider_status =
        laplace_cognition_observation_candidate_provider_set_create(
            providers.data(), providers.size(), &provider_set, &composite_provider);
    if (provider_status != LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK) {
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        return laplace_cognition_prompt_conversation_detail::ProviderSetStatus(
            provider_status);
    }

    laplace_cognition_conversation_request conversation_request{};
    conversation_request.turn = admission_view.turn;
    conversation_request.cognition_policy = request->cognition_policy;
    conversation_request.realization_policy = request->realization_policy;
    conversation_request.materialization = request->materialization;
    conversation_request.discourse_roots = request->discourse_roots;
    conversation_request.version = LAPLACE_COGNITION_CONVERSATION_VERSION;

    laplace_cognition_conversation_result conversation_result{};
    const auto conversation_status = laplace_cognition_conversation_execute_encoded(
        &conversation_request, output_encoding,
        previous_frame,
        previous_frame_bytes,
        &composite_provider,
        realization_provider,
        materialization_provider,
        output,
        output_capacity,
        output_bytes,
        next_discourse_frame,
        next_discourse_frame_capacity,
        next_discourse_frame_bytes,
        &conversation_result);

    laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
    if (conversation_status != LAPLACE_COGNITION_CONVERSATION_OK) {
        *output_bytes = 0U;
        *next_discourse_frame_bytes = 0U;
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_CONVERSATION_FAILURE;
    }

    result->prompt_admission_receipt_id = admission_view.admission_receipt_id;
    result->prompt_exact_bytes_fingerprint = admission_view.exact_bytes_fingerprint;
    result->composite_cognition_provider_fingerprint =
        composite_provider.provider_fingerprint;
    result->conversation = conversation_result;
    result->version = LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION;
    return LAPLACE_COGNITION_PROMPT_CONVERSATION_OK;
}


extern "C" laplace_cognition_prompt_conversation_status
laplace_cognition_prompt_conversation_execute(
    laplace_cognition_prompt_admission* const admission,
    const laplace_cognition_prompt_conversation_request* const request,
    const std::uint8_t* const previous_frame,
    const std::size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* const additional_cognition_providers,
    const std::size_t additional_cognition_provider_count,
    const laplace_cognition_realization_provider_v1* const realization_provider,
    const laplace_cognition_materialization_provider_v1* const materialization_provider,
    std::uint8_t* const output,
    const std::size_t output_capacity,
    std::size_t* const output_bytes,
    std::uint8_t* const next_discourse_frame,
    const std::size_t next_discourse_frame_capacity,
    std::size_t* const next_discourse_frame_bytes,
    laplace_cognition_prompt_conversation_result* const result) {
    return laplace_cognition_prompt_conversation_execute_encoded(
        admission, request, LAPLACE_COGNITION_OUTPUT_UTF8,
        previous_frame, previous_frame_bytes,
        additional_cognition_providers, additional_cognition_provider_count,
        realization_provider, materialization_provider,
        output, output_capacity, output_bytes,
        next_discourse_frame, next_discourse_frame_capacity,
        next_discourse_frame_bytes, result);
}
