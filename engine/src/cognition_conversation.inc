#include "laplace/cognition_conversation.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace laplace_cognition_conversation_detail {

constexpr char ConversationDomain[] = "laplace-cognition-conversation-v1";

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool RequestVersionValid(const laplace_cognition_conversation_request& request) {
    return request.version == LAPLACE_COGNITION_CONVERSATION_VERSION &&
        request.reserved == 0U &&
        request.realization_policy.version ==
            LAPLACE_COGNITION_CONVERSATION_REALIZATION_POLICY_VERSION &&
        request.discourse_roots.version ==
            LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_VERSION;
}

bool DiscourseRootsFlagsValid(
    const laplace_cognition_conversation_discourse_roots& roots) {
    return (roots.flags &
            ~LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_KNOWN_FLAGS) == 0U;
}

void BindDiscourseRoots(
    const laplace_cognition_conversation_request& request,
    const laplace_cognition_observation_request& cognition_request,
    const laplace_cognition_turn_receipt& turn_receipt,
    laplace_cognition_discourse_input* const discourse) {
    *discourse = laplace_cognition_discourse_input{};
    discourse->discourse_id = request.turn.discourse_id;
    discourse->previous_state_id = turn_receipt.previous_state_id;
    discourse->observation_entity_id = request.turn.observation_entity_id;
    discourse->observation_occurrence_id = request.turn.observation_occurrence_id;
    discourse->active_entity_set_fingerprint =
        request.discourse_roots.active_entity_set_fingerprint;
    discourse->proposition_set_fingerprint =
        request.discourse_roots.proposition_set_fingerprint;
    discourse->referent_set_fingerprint =
        request.discourse_roots.referent_set_fingerprint;
    discourse->unresolved_question_set_fingerprint =
        request.discourse_roots.unresolved_question_set_fingerprint;
    discourse->correction_set_fingerprint =
        request.discourse_roots.correction_set_fingerprint;
    discourse->ellipsis_binding_set_fingerprint =
        request.discourse_roots.ellipsis_binding_set_fingerprint;
    discourse->cross_modal_artifact_set_fingerprint =
        request.discourse_roots.cross_modal_artifact_set_fingerprint;
    discourse->rejected_interpretation_set_fingerprint =
        request.discourse_roots.rejected_interpretation_set_fingerprint;
    discourse->prior_program_set_fingerprint =
        request.discourse_roots.prior_program_set_fingerprint;
    discourse->receipt_set_fingerprint =
        request.discourse_roots.receipt_set_fingerprint;
    discourse->world_id = request.turn.world_id;
    discourse->time_fingerprint = request.turn.time_fingerprint;
    discourse->context_fingerprint = cognition_request.context_fingerprint;
    discourse->turn_ordinal = request.turn.turn_ordinal;
    discourse->flags = request.discourse_roots.flags;
    if ((request.turn.flags & LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE) != 0U) {
        discourse->flags |= LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE;
    }
    discourse->version = LAPLACE_COGNITION_DISCOURSE_VERSION;
}

laplace_cognition_realization_request BindRealizationRequest(
    const laplace_cognition_conversation_request& request,
    const laplace_cognition_observation_request& cognition_request) {
    laplace_cognition_realization_request realization{};
    realization.modality_id = request.realization_policy.modality_id;
    realization.language_id = request.realization_policy.language_id;
    realization.register_id = request.realization_policy.register_id;
    realization.evidence_epoch = request.cognition_policy.evidence_epoch;
    realization.realization_recipe_epoch =
        request.realization_policy.realization_recipe_epoch;
    realization.context_fingerprint = cognition_request.context_fingerprint;
    realization.maximum_candidates = request.realization_policy.maximum_candidates;
    realization.flags = request.realization_policy.flags;
    realization.version = LAPLACE_COGNITION_REALIZATION_VERSION;
    return realization;
}

void ConversationIdentity(
    const laplace_cognition_conversation_result& result,
    laplace_digest256* const identity) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, ConversationDomain, sizeof(ConversationDomain) - 1U);
    HashDigest(&hasher, result.turn_receipt.turn_fingerprint);
    HashDigest(&hasher, result.turn_receipt.request_fingerprint);
    HashDigest(&hasher, result.forward_receipt.receipt_id);
    HashDigest(&hasher, result.semantic_act.act_id);
    HashDigest(&hasher, result.realization_receipt.realization_id);
    HashDigest(&hasher, result.materialization_receipt.materialization_id);
    HashDigest(&hasher, result.discourse_state.state_id);
    HashDigest(&hasher, result.discourse_frame_receipt.frame_fingerprint);
    HashU64(&hasher, result.output_bytes);
    HashU64(&hasher, result.discourse_frame_bytes);
    HashU32(&hasher, result.version);
    Finish(&hasher, identity);
}

struct NativeResultOwner final {
    laplace_cognition_observation_result* observation{};
    laplace_cognition_forward_result* forward{};

    ~NativeResultOwner() {
        laplace_cognition_observation_result_destroy(&observation);
        laplace_cognition_forward_result_destroy(&forward);
    }
};

}  // namespace laplace_cognition_conversation_detail

extern "C" laplace_cognition_conversation_status
laplace_cognition_conversation_execute(
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
    laplace_cognition_conversation_result* const result) {
    using namespace laplace_cognition_conversation_detail;

    if (output_bytes != nullptr) *output_bytes = 0U;
    if (next_discourse_frame_bytes != nullptr) *next_discourse_frame_bytes = 0U;
    if (result != nullptr) *result = laplace_cognition_conversation_result{};

    if (request == nullptr || cognition_provider == nullptr ||
        realization_provider == nullptr || materialization_provider == nullptr ||
        output == nullptr || output_capacity == 0U || output_bytes == nullptr ||
        next_discourse_frame == nullptr || next_discourse_frame_bytes == nullptr ||
        result == nullptr) {
        return LAPLACE_COGNITION_CONVERSATION_INVALID_ARGUMENT;
    }
    if (!RequestVersionValid(*request)) {
        return LAPLACE_COGNITION_CONVERSATION_INVALID_VERSION;
    }
    if (!DiscourseRootsFlagsValid(request->discourse_roots)) {
        return LAPLACE_COGNITION_CONVERSATION_INVALID_DISCOURSE_ROOTS;
    }
    if (next_discourse_frame_capacity < LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES) {
        return LAPLACE_COGNITION_CONVERSATION_CAPACITY;
    }

    std::vector<std::uint8_t> staged_output;
    try {
        staged_output.resize(output_capacity);
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_CONVERSATION_MEMORY_FAILURE;
    }
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES>
        staged_discourse_frame{};

    laplace_cognition_conversation_result resolved{};
    resolved.version = LAPLACE_COGNITION_CONVERSATION_VERSION;

    if (laplace_cognition_turn_compile(
            &request->turn, &request->cognition_policy,
            previous_frame, previous_frame_bytes,
            &resolved.cognition_request, &resolved.turn_receipt) !=
        LAPLACE_COGNITION_TURN_OK) {
        return LAPLACE_COGNITION_CONVERSATION_TURN_FAILURE;
    }

    NativeResultOwner native_results{};
    if (laplace_cognition_observation_request_execute_with_candidate_provider(
            &resolved.cognition_request, cognition_provider,
            &native_results.observation, &native_results.forward,
            &resolved.forward_receipt) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        return LAPLACE_COGNITION_CONVERSATION_COGNITION_FAILURE;
    }

    if (laplace_cognition_observation_semantic_act_select(
            &resolved.cognition_request, native_results.observation,
            native_results.forward, &resolved.forward_receipt,
            &resolved.semantic_act) != LAPLACE_COGNITION_SEMANTIC_ACT_OK) {
        return LAPLACE_COGNITION_CONVERSATION_SEMANTIC_ACT_FAILURE;
    }

    resolved.realization_request =
        BindRealizationRequest(*request, resolved.cognition_request);
    if (laplace_cognition_semantic_act_realize(
            &resolved.semantic_act, &resolved.realization_request,
            realization_provider, &resolved.realization,
            &resolved.realization_receipt) != LAPLACE_COGNITION_REALIZATION_OK) {
        return LAPLACE_COGNITION_CONVERSATION_REALIZATION_FAILURE;
    }

    std::size_t staged_output_bytes = 0U;
    if (laplace_cognition_realization_materialize_utf8(
            &resolved.realization, &request->materialization,
            materialization_provider, staged_output.data(), staged_output.size(),
            &staged_output_bytes, &resolved.materialization_receipt) !=
        LAPLACE_COGNITION_MATERIALIZATION_OK) {
        return LAPLACE_COGNITION_CONVERSATION_MATERIALIZATION_FAILURE;
    }

    laplace_cognition_discourse_input discourse_input{};
    BindDiscourseRoots(
        *request, resolved.cognition_request, resolved.turn_receipt,
        &discourse_input);
    if (laplace_cognition_discourse_state_create(
            &discourse_input, &resolved.semantic_act,
            &resolved.discourse_state) != LAPLACE_COGNITION_DISCOURSE_OK) {
        return LAPLACE_COGNITION_CONVERSATION_DISCOURSE_FAILURE;
    }

    std::size_t staged_frame_bytes = 0U;
    if (laplace_cognition_discourse_frame_encode(
            &resolved.discourse_state, staged_discourse_frame.data(),
            staged_discourse_frame.size(), &staged_frame_bytes,
            &resolved.discourse_frame_receipt) !=
        LAPLACE_COGNITION_DISCOURSE_FRAME_OK) {
        return LAPLACE_COGNITION_CONVERSATION_FRAME_FAILURE;
    }
    if (staged_frame_bytes != LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES) {
        return LAPLACE_COGNITION_CONVERSATION_FRAME_FAILURE;
    }

    resolved.output_bytes = static_cast<std::uint64_t>(staged_output_bytes);
    resolved.discourse_frame_bytes = static_cast<std::uint64_t>(staged_frame_bytes);
    ConversationIdentity(resolved, &resolved.conversation_id);

    std::memcpy(output, staged_output.data(), staged_output_bytes);
    std::memcpy(
        next_discourse_frame, staged_discourse_frame.data(), staged_frame_bytes);
    *output_bytes = staged_output_bytes;
    *next_discourse_frame_bytes = staged_frame_bytes;
    *result = resolved;
    return LAPLACE_COGNITION_CONVERSATION_OK;
}
