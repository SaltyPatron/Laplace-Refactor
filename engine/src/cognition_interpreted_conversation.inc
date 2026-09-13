#include "laplace/cognition_interpreted_conversation.h"

extern "C" laplace_cognition_interpreted_conversation_status
laplace_cognition_interpreted_conversation_execute_encoded(
    const laplace_cognition_interpretation_result* const interpretation,
    const laplace_digest256* const goal_slot_id,
    const laplace_cognition_conversation_request* const request,
    const std::uint32_t output_encoding,
    const std::uint8_t* const previous_frame,
    const std::size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* const cognition_provider,
    const laplace_cognition_realization_provider_v1* const realization_provider,
    const laplace_cognition_materialization_provider_v1* const materialization_provider,
    std::uint8_t* const output,
    const std::size_t output_capacity,
    std::size_t* const output_bytes,
    std::uint8_t* const next_discourse_frame,
    const std::size_t next_discourse_frame_capacity,
    std::size_t* const next_discourse_frame_bytes,
    laplace_cognition_interpreted_conversation_result* const result) {
    if (output_bytes != nullptr) *output_bytes = 0U;
    if (next_discourse_frame_bytes != nullptr) *next_discourse_frame_bytes = 0U;
    if (result != nullptr) {
        *result = laplace_cognition_interpreted_conversation_result{};
        result->version = LAPLACE_COGNITION_INTERPRETED_CONVERSATION_VERSION;
    }

    if (interpretation == nullptr || goal_slot_id == nullptr || request == nullptr ||
        cognition_provider == nullptr || realization_provider == nullptr ||
        materialization_provider == nullptr || output == nullptr ||
        output_capacity == 0U || output_bytes == nullptr ||
        next_discourse_frame == nullptr || next_discourse_frame_bytes == nullptr ||
        result == nullptr) {
        return LAPLACE_COGNITION_INTERPRETED_CONVERSATION_INVALID_ARGUMENT;
    }

    result->goal_slot_id = *goal_slot_id;
    laplace_cognition_turn_input inferred_turn{};
    laplace_cognition_turn_policy inferred_policy{};
    const auto interpretation_status = laplace_cognition_interpretation_prepare_turn(
        interpretation,
        goal_slot_id,
        &request->turn,
        &request->cognition_policy,
        &inferred_turn,
        &inferred_policy);
    result->interpretation_status = static_cast<std::uint32_t>(interpretation_status);
    if (interpretation_status != LAPLACE_COGNITION_INTERPRETATION_OK) {
        return LAPLACE_COGNITION_INTERPRETED_CONVERSATION_INTERPRETATION_FAILURE;
    }
    result->interpretation_context_fingerprint = inferred_turn.context_fingerprint;

    auto interpreted_request = *request;
    interpreted_request.turn = inferred_turn;
    interpreted_request.cognition_policy = inferred_policy;

    const auto conversation_status = laplace_cognition_conversation_execute_encoded(
        &interpreted_request,
        output_encoding,
        previous_frame,
        previous_frame_bytes,
        cognition_provider,
        realization_provider,
        materialization_provider,
        output,
        output_capacity,
        output_bytes,
        next_discourse_frame,
        next_discourse_frame_capacity,
        next_discourse_frame_bytes,
        &result->conversation);
    result->conversation_status = static_cast<std::uint32_t>(conversation_status);
    if (conversation_status != LAPLACE_COGNITION_CONVERSATION_OK) {
        *output_bytes = 0U;
        *next_discourse_frame_bytes = 0U;
        return LAPLACE_COGNITION_INTERPRETED_CONVERSATION_CONVERSATION_FAILURE;
    }
    return LAPLACE_COGNITION_INTERPRETED_CONVERSATION_OK;
}

extern "C" laplace_cognition_interpreted_conversation_status
laplace_cognition_interpreted_conversation_execute(
    const laplace_cognition_interpretation_result* const interpretation,
    const laplace_digest256* const goal_slot_id,
    const laplace_cognition_conversation_request* const request,
    const std::uint8_t* const previous_frame,
    const std::size_t previous_frame_bytes,
    const laplace_cognition_observation_candidate_provider_v1* const cognition_provider,
    const laplace_cognition_realization_provider_v1* const realization_provider,
    const laplace_cognition_materialization_provider_v1* const materialization_provider,
    std::uint8_t* const output,
    const std::size_t output_capacity,
    std::size_t* const output_bytes,
    std::uint8_t* const next_discourse_frame,
    const std::size_t next_discourse_frame_capacity,
    std::size_t* const next_discourse_frame_bytes,
    laplace_cognition_interpreted_conversation_result* const result) {
    return laplace_cognition_interpreted_conversation_execute_encoded(
        interpretation,
        goal_slot_id,
        request,
        LAPLACE_COGNITION_OUTPUT_UTF8,
        previous_frame,
        previous_frame_bytes,
        cognition_provider,
        realization_provider,
        materialization_provider,
        output,
        output_capacity,
        output_bytes,
        next_discourse_frame,
        next_discourse_frame_capacity,
        next_discourse_frame_bytes,
        result);
}
