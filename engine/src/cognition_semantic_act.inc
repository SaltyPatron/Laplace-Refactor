#include "laplace/cognition_semantic_act.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace laplace_cognition_semantic_act_detail {

constexpr char AnswerSetDomain[] = "laplace-cognition-semantic-answer-set-v1";
constexpr char SemanticActDomain[] = "laplace-cognition-semantic-act-v1";

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

void HashAnswer(
    blake3_hasher* const hasher,
    const laplace_cognition_observation_answer& answer) {
    HashId(hasher, answer.entity_id);
    HashDigest(hasher, answer.path_id);
    HashDigest(hasher, answer.terminal_state_id);
    HashU64(hasher, answer.total_cost);
    HashU64(hasher, answer.transition_count);
    HashU64(hasher, answer.independent_evidence_root_count);
    HashU32(hasher, answer.relation_family);
    HashU32(hasher, answer.source_layer);
    HashU32(hasher, answer.direction);
    HashU32(hasher, answer.rank);
    HashU32(hasher, answer.flags);
    HashU32(hasher, answer.reserved);
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool BetterPrimary(
    const laplace_cognition_observation_answer& candidate,
    const laplace_cognition_observation_answer& incumbent) {
    if (candidate.rank != incumbent.rank) return candidate.rank < incumbent.rank;
    const int path_order = std::memcmp(
        candidate.path_id.bytes, incumbent.path_id.bytes,
        sizeof(candidate.path_id.bytes));
    if (path_order != 0) return path_order < 0;
    return std::memcmp(
               candidate.entity_id.bytes, incumbent.entity_id.bytes,
               sizeof(candidate.entity_id.bytes)) < 0;
}

bool KnownProducerOperation(const std::uint32_t kind) {
    return kind >= LAPLACE_COGNITION_OPERATION_STRUCTURAL_CALCULATION &&
        kind <= LAPLACE_COGNITION_OPERATION_EXECUTE;
}

}  // namespace laplace_cognition_semantic_act_detail

extern "C" laplace_cognition_semantic_act_status
laplace_cognition_observation_semantic_act_select(
    const laplace_cognition_observation_request* const request,
    const laplace_cognition_observation_result* const observation_result,
    const laplace_cognition_forward_result* const forward_result,
    const laplace_cognition_forward_receipt* const forward_receipt,
    laplace_cognition_semantic_act* const act) {
    using namespace laplace_cognition_semantic_act_detail;

    if (act != nullptr) *act = laplace_cognition_semantic_act{};
    if (request == nullptr || observation_result == nullptr ||
        forward_result == nullptr || forward_receipt == nullptr || act == nullptr) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_INVALID_ARGUMENT;
    }

    if (forward_receipt->status != LAPLACE_COGNITION_FORWARD_OK ||
        forward_receipt->disposition != LAPLACE_COGNITION_FORWARD_COMPLETE ||
        forward_receipt->final_completion != LAPLACE_COGNITION_COMPLETION_COMPLETE ||
        forward_receipt->final_remaining_required_count != 0U) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_INCOMPLETE;
    }

    const std::size_t layer_count =
        laplace_cognition_forward_result_layer_count(forward_result);
    if (layer_count == 0U) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_NO_PRODUCER_OPERATION;
    }
    if (forward_receipt->layer_count != static_cast<std::uint64_t>(layer_count)) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_FORWARD_RESULT_FAILURE;
    }

    laplace_cognition_forward_layer_receipt final_layer{};
    if (laplace_cognition_forward_result_layer(
            forward_result, layer_count - 1U, &final_layer) !=
        LAPLACE_COGNITION_FORWARD_OK) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_FORWARD_RESULT_FAILURE;
    }
    if (final_layer.completion != LAPLACE_COGNITION_COMPLETION_COMPLETE ||
        !SameDigest(final_layer.next_state_id, forward_receipt->final_state_id)) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_FORWARD_RESULT_FAILURE;
    }
    if (!KnownProducerOperation(final_layer.selected_kind)) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_NO_PRODUCER_OPERATION;
    }

    const std::size_t answer_count =
        laplace_cognition_observation_result_answer_count(observation_result);
    if (answer_count == 0U) return LAPLACE_COGNITION_SEMANTIC_ACT_NO_ANSWER;
    if (answer_count > static_cast<std::size_t>(
            std::numeric_limits<std::uint64_t>::max())) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_RANGE;
    }

    blake3_hasher answer_hasher;
    blake3_hasher_init(&answer_hasher);
    blake3_hasher_update(
        &answer_hasher, AnswerSetDomain, sizeof(AnswerSetDomain) - 1U);
    HashU64(&answer_hasher, static_cast<std::uint64_t>(answer_count));

    laplace_cognition_observation_answer primary{};
    bool primary_present = false;
    for (std::size_t index = 0U; index < answer_count; ++index) {
        laplace_cognition_observation_answer answer{};
        if (laplace_cognition_observation_result_answer(
                observation_result, index, &answer) !=
            LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
            return LAPLACE_COGNITION_SEMANTIC_ACT_RESULT_FAILURE;
        }
        HashAnswer(&answer_hasher, answer);
        if (!primary_present || BetterPrimary(answer, primary)) {
            primary = answer;
            primary_present = true;
        }
    }

    laplace_digest256 request_fingerprint{};
    if (laplace_cognition_observation_request_identify(
            request, &request_fingerprint) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        return LAPLACE_COGNITION_SEMANTIC_ACT_REQUEST_FAILURE;
    }

    laplace_digest256 answer_set_fingerprint{};
    blake3_hasher_finalize(
        &answer_hasher, answer_set_fingerprint.bytes,
        sizeof(answer_set_fingerprint.bytes));

    act->request_fingerprint = request_fingerprint;
    act->result_contract_fingerprint = request->result_contract_fingerprint;
    act->forward_receipt_id = forward_receipt->receipt_id;
    act->forward_output_fingerprint = forward_receipt->output_fingerprint;
    act->final_state_id = forward_receipt->final_state_id;
    act->answer_set_fingerprint = answer_set_fingerprint;
    act->primary_answer = primary;
    act->answer_count = static_cast<std::uint64_t>(answer_count);
    act->act_kind = LAPLACE_COGNITION_OPERATION_ANSWER;
    act->producer_operation_kind = final_layer.selected_kind;
    act->flags = LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_PRODUCER_OPERATION_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT;
    act->version = LAPLACE_COGNITION_SEMANTIC_ACT_VERSION;

    blake3_hasher act_hasher;
    blake3_hasher_init(&act_hasher);
    blake3_hasher_update(
        &act_hasher, SemanticActDomain, sizeof(SemanticActDomain) - 1U);
    HashDigest(&act_hasher, act->request_fingerprint);
    HashDigest(&act_hasher, act->result_contract_fingerprint);
    HashDigest(&act_hasher, act->forward_receipt_id);
    HashDigest(&act_hasher, act->forward_output_fingerprint);
    HashDigest(&act_hasher, act->final_state_id);
    HashDigest(&act_hasher, act->answer_set_fingerprint);
    HashAnswer(&act_hasher, act->primary_answer);
    HashU64(&act_hasher, act->answer_count);
    HashU32(&act_hasher, act->act_kind);
    HashU32(&act_hasher, act->producer_operation_kind);
    HashU32(&act_hasher, act->flags);
    HashU32(&act_hasher, act->version);
    blake3_hasher_finalize(
        &act_hasher, act->act_id.bytes, sizeof(act->act_id.bytes));
    return LAPLACE_COGNITION_SEMANTIC_ACT_OK;
}
