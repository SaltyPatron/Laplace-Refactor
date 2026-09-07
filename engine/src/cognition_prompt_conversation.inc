#include "laplace/cognition_prompt_conversation.h"
#include "laplace/cognition_interpretation.h"

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

static laplace_cognition_prompt_conversation_status
ExecutePromptConversation(
    laplace_cognition_prompt_admission* const admission,
    const laplace_cognition_prompt_conversation_request* const request,
    const laplace_cognition_interpretation_result* const interpretation,
    const laplace_digest256* const goal_slot_id,
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

    std::vector<laplace_cognition_observation_candidate_provider_v1> providers;
    // Include the admission's structural provider without overflowing the
    // caller-supplied count or touching an unrepresentable descriptor range.
    if (additional_cognition_provider_count >= providers.max_size()) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_INVALID_ARGUMENT;
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

    if (interpretation != nullptr) {
        laplace_cognition_turn_input inferred_turn{};
        laplace_cognition_turn_policy inferred_policy{};
        const auto interpretation_status = laplace_cognition_interpretation_prepare_turn(
            interpretation, goal_slot_id, &conversation_request.turn,
            &conversation_request.cognition_policy, &inferred_turn, &inferred_policy);
        if (interpretation_status != LAPLACE_COGNITION_INTERPRETATION_OK) {
            laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
            return LAPLACE_COGNITION_PROMPT_CONVERSATION_INTERPRETATION_FAILURE;
        }
        conversation_request.turn = inferred_turn;
        conversation_request.cognition_policy = inferred_policy;
    }

    laplace_cognition_conversation_result conversation_result{};
    const auto conversation_status = laplace_cognition_conversation_execute(
        &conversation_request,
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
    laplace_cognition_prompt_admission* admission,
    const laplace_cognition_prompt_conversation_request* request,
    const uint8_t* previous_frame, size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* providers, size_t provider_count,
    const laplace_cognition_realization_provider_v1* realization,
    const laplace_cognition_materialization_provider_v1* materialization,
    uint8_t* output, size_t output_capacity, size_t* output_bytes,
    uint8_t* next_frame, size_t next_frame_capacity, size_t* next_frame_bytes,
    laplace_cognition_prompt_conversation_result* result) {
    return ExecutePromptConversation(admission, request, nullptr, nullptr,
        previous_frame, previous_frame_bytes, providers, provider_count,
        realization, materialization, output, output_capacity, output_bytes,
        next_frame, next_frame_capacity, next_frame_bytes, result);
}

extern "C" laplace_cognition_prompt_conversation_status
laplace_cognition_prompt_conversation_execute_interpreted(
    laplace_cognition_prompt_admission* admission,
    const laplace_cognition_prompt_conversation_request* request,
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* goal_slot_id,
    const uint8_t* previous_frame, size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* providers, size_t provider_count,
    const laplace_cognition_realization_provider_v1* realization,
    const laplace_cognition_materialization_provider_v1* materialization,
    uint8_t* output, size_t output_capacity, size_t* output_bytes,
    uint8_t* next_frame, size_t next_frame_capacity, size_t* next_frame_bytes,
    laplace_cognition_prompt_conversation_result* result) {
    if (interpretation == nullptr || goal_slot_id == nullptr) {
        if (output_bytes != nullptr) *output_bytes = 0;
        if (next_frame_bytes != nullptr) *next_frame_bytes = 0;
        if (result != nullptr) *result = {};
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_INTERPRETATION_FAILURE;
    }
    return ExecutePromptConversation(admission, request, interpretation, goal_slot_id,
        previous_frame, previous_frame_bytes, providers, provider_count,
        realization, materialization, output, output_capacity, output_bytes,
        next_frame, next_frame_capacity, next_frame_bytes, result);
}
