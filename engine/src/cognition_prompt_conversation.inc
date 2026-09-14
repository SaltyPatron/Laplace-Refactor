#include "laplace/cognition_prompt_conversation.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace laplace_cognition_prompt_conversation_detail {

constexpr char OrientationProgramDomain[] =
    "laplace-cognition-prompt-orientation-program-v1";
constexpr char OrientationContextDomain[] =
    "laplace-cognition-prompt-orientation-context-v1";

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
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

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_cognition_prompt_conversation_status ProviderSetStatus(
    const laplace_cognition_observation_provider_set_status status) {
    if (status == LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_MEMORY_FAILURE) {
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE;
    }
    return LAPLACE_COGNITION_PROMPT_CONVERSATION_PROVIDER_SET_FAILURE;
}

bool BuildOrientationProgram(
    const laplace_cognition_prompt_admission_view& admission,
    const laplace_cognition_turn_policy& policy,
    const laplace_cognition_observation_candidate_provider_v1& provider,
    laplace_cognition_response_scan_program* const program) {
    if (program == nullptr ||
        policy.search_budget.max_frontier_states == 0U ||
        policy.search_budget.max_transition_records == 0U ||
        policy.search_budget.max_provider_calls == 0U ||
        policy.search_budget.max_depth == 0U ||
        policy.search_budget.max_frontier_states >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        policy.search_budget.max_transition_records >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    *program = laplace_cognition_response_scan_program{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, OrientationProgramDomain, sizeof(OrientationProgramDomain) - 1U);
    HashDigest(&hasher, admission.admission_receipt_id);
    HashDigest(&hasher, admission.exact_bytes_fingerprint);
    HashDigest(&hasher, provider.provider_fingerprint);
    HashDigest(&hasher, policy.evidence_boundary);
    HashDigest(&hasher, policy.evidence_epoch);
    HashDigest(&hasher, policy.authority_id);
    HashU64(&hasher, policy.search_budget.max_frontier_states);
    HashU64(&hasher, policy.search_budget.max_transition_records);
    HashU64(&hasher, policy.search_budget.max_provider_calls);
    HashU32(&hasher, policy.search_budget.max_depth);
    Finish(&hasher, &program->program_id);
    program->context_fingerprint = admission.turn.context_fingerprint;
    program->evidence_boundary = policy.evidence_boundary;
    program->evidence_epoch = policy.evidence_epoch;
    program->max_frontier_states = policy.search_budget.max_frontier_states;
    program->max_candidate_records = policy.search_budget.max_transition_records;
    program->max_provider_calls = policy.search_budget.max_provider_calls;
    program->max_depth = policy.search_budget.max_depth;
    /* Raw orientation is deliberately broader than the eventual request. The
     * caller's relation mask is applied only after the observed response field
     * is known, as an admissibility envelope rather than an intent selector. */
    program->relation_mask = LAPLACE_OBSERVATION_QUERY_RELATION_MASK;
    program->version = LAPLACE_COGNITION_RESPONSE_SCAN_VERSION;
    return true;
}

laplace_digest256 OrientationContext(
    const laplace_cognition_prompt_admission_view& admission,
    const laplace_cognition_response_scan_receipt& receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, OrientationContextDomain, sizeof(OrientationContextDomain) - 1U);
    HashDigest(&hasher, admission.turn.context_fingerprint);
    HashDigest(&hasher, admission.admission_receipt_id);
    HashDigest(&hasher, receipt.receipt_id);
    HashDigest(&hasher, receipt.field_receipt_id);
    HashU32(&hasher, receipt.relation_family_mask);
    HashU32(&hasher, receipt.source_layer_mask);
    laplace_digest256 output{};
    Finish(&hasher, &output);
    return output;
}

bool ContainsGoal(
    const std::vector<laplace_cognition_response_entry>& responses,
    const laplace_id128& goal) {
    for (const auto& response : responses) {
        if (SameId(response.entity_id, goal)) return true;
    }
    return false;
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
    using namespace laplace_cognition_prompt_conversation_detail;

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
        return ProviderSetStatus(provider_status);
    }

    result->prompt_admission_receipt_id = admission_view.admission_receipt_id;
    result->prompt_exact_bytes_fingerprint = admission_view.exact_bytes_fingerprint;
    result->composite_cognition_provider_fingerprint =
        composite_provider.provider_fingerprint;
    result->version = LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION;

    laplace_cognition_response_scan_program orientation_program{};
    if (!BuildOrientationProgram(
            admission_view, request->cognition_policy, composite_provider,
            &orientation_program)) {
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_ORIENTATION_FAILURE;
    }

    std::vector<laplace_cognition_observation_candidate> orientation_candidates;
    std::vector<laplace_cognition_response_entry> orientation_responses;
    try {
        orientation_candidates.resize(static_cast<std::size_t>(
            orientation_program.max_candidate_records));
        orientation_responses.resize(static_cast<std::size_t>(
            orientation_program.max_candidate_records));
    } catch (const std::bad_alloc&) {
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE;
    }

    std::size_t orientation_candidate_count = 0U;
    std::size_t orientation_response_count = 0U;
    const auto orientation_status = laplace_cognition_response_field_scan(
        &orientation_program, &admission_view.trunk_entity_id, 1U,
        &composite_provider,
        orientation_candidates.data(), orientation_candidates.size(),
        &orientation_candidate_count,
        orientation_responses.data(), orientation_responses.size(),
        &orientation_response_count, &result->orientation_receipt);
    if (orientation_status != LAPLACE_COGNITION_RESPONSE_OK) {
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_ORIENTATION_FAILURE;
    }
    orientation_candidates.resize(orientation_candidate_count);
    orientation_responses.resize(orientation_response_count);

    const std::uint32_t orientation_mask =
        result->orientation_receipt.relation_family_mask &
        request->cognition_policy.relation_mask;
    if (orientation_mask == 0U ||
        ((request->cognition_policy.request_flags &
          LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT) != 0U &&
         !ContainsGoal(
             orientation_responses, request->cognition_policy.goal_entity_id))) {
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_ORIENTATION_FAILURE;
    }
    result->orientation_relation_mask = orientation_mask;
    result->orientation_context_fingerprint =
        OrientationContext(admission_view, result->orientation_receipt);

    laplace_cognition_conversation_request conversation_request{};
    conversation_request.turn = admission_view.turn;
    conversation_request.turn.context_fingerprint =
        result->orientation_context_fingerprint;
    conversation_request.cognition_policy = request->cognition_policy;
    conversation_request.cognition_policy.relation_mask = orientation_mask;
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
    result->conversation = conversation_result;

    if (conversation_status == LAPLACE_COGNITION_CONVERSATION_WHY_NOT) {
        *output_bytes = 0U;
        *next_discourse_frame_bytes = 0U;
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_WHY_NOT;
    }
    if (conversation_status != LAPLACE_COGNITION_CONVERSATION_OK) {
        *output_bytes = 0U;
        *next_discourse_frame_bytes = 0U;
        return LAPLACE_COGNITION_PROMPT_CONVERSATION_CONVERSATION_FAILURE;
    }
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
