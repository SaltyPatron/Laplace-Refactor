#include "laplace/cognition_turn.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace laplace_cognition_turn_detail {

constexpr char ContextDomain[] = "laplace-cognition-turn-context-v1";
constexpr char TurnDomain[] = "laplace-cognition-turn-receipt-v1";

bool BytesZero(const std::uint8_t* const bytes, const std::size_t count) {
    std::uint8_t aggregate = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        aggregate = static_cast<std::uint8_t>(aggregate | bytes[index]);
    }
    return aggregate == 0U;
}

bool DigestZero(const laplace_digest256& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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

void TurnContext(
    const laplace_cognition_turn_input& input,
    const laplace_digest256& previous_state_id,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ContextDomain, sizeof(ContextDomain) - 1U);
    HashDigest(&hasher, input.context_fingerprint);
    HashDigest(&hasher, input.discourse_id);
    HashDigest(&hasher, previous_state_id);
    HashDigest(&hasher, input.observation_occurrence_id);
    HashU64(&hasher, input.turn_ordinal);
    Finish(&hasher, output);
}

void TurnReceiptIdentity(
    const laplace_cognition_turn_input& input,
    const laplace_digest256& request_fingerprint,
    const laplace_digest256& previous_state_id,
    const laplace_digest256& previous_frame_fingerprint,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, TurnDomain, sizeof(TurnDomain) - 1U);
    HashDigest(&hasher, request_fingerprint);
    HashDigest(&hasher, input.discourse_id);
    HashDigest(&hasher, previous_state_id);
    HashDigest(&hasher, previous_frame_fingerprint);
    HashDigest(&hasher, input.observation_occurrence_id);
    HashU64(&hasher, input.turn_ordinal);
    Finish(&hasher, output);
}

laplace_cognition_turn_status ValidateInput(
    const laplace_cognition_turn_input& input,
    const laplace_cognition_turn_policy& policy,
    const std::uint8_t* const previous_frame,
    const std::size_t previous_frame_bytes) {
    if (input.version != LAPLACE_COGNITION_TURN_VERSION ||
        policy.version != LAPLACE_COGNITION_TURN_POLICY_VERSION) {
        return LAPLACE_COGNITION_TURN_INVALID_ARGUMENT;
    }
    if ((input.flags & ~LAPLACE_COGNITION_TURN_KNOWN_FLAGS) != 0U) {
        return LAPLACE_COGNITION_TURN_INVALID_FLAGS;
    }
    if (DigestZero(input.discourse_id) ||
        DigestZero(input.observation_occurrence_id) ||
        DigestZero(input.world_id) ||
        DigestZero(input.time_fingerprint) ||
        DigestZero(input.context_fingerprint)) {
        return LAPLACE_COGNITION_TURN_INVALID_ARGUMENT;
    }

    const bool has_previous =
        (input.flags & LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE) != 0U;
    if (has_previous) {
        if (previous_frame == nullptr || previous_frame_bytes == 0U) {
            return LAPLACE_COGNITION_TURN_INVALID_ARGUMENT;
        }
    } else {
        if (previous_frame != nullptr || previous_frame_bytes != 0U) {
            return LAPLACE_COGNITION_TURN_INVALID_FLAGS;
        }
        if (input.turn_ordinal != 0U) {
            return LAPLACE_COGNITION_TURN_SEQUENCE_MISMATCH;
        }
    }
    return LAPLACE_COGNITION_TURN_OK;
}

}  // namespace laplace_cognition_turn_detail

extern "C" laplace_cognition_turn_status
laplace_cognition_turn_compile(
    const laplace_cognition_turn_input* const input,
    const laplace_cognition_turn_policy* const policy,
    const std::uint8_t* const previous_frame,
    const std::size_t previous_frame_bytes,
    laplace_cognition_observation_request* const request,
    laplace_cognition_turn_receipt* const receipt) {
    using namespace laplace_cognition_turn_detail;

    if (request != nullptr) *request = laplace_cognition_observation_request{};
    if (receipt != nullptr) *receipt = laplace_cognition_turn_receipt{};
    if (input == nullptr || policy == nullptr || request == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_TURN_INVALID_ARGUMENT;
    }

    const auto input_status =
        ValidateInput(*input, *policy, previous_frame, previous_frame_bytes);
    if (input_status != LAPLACE_COGNITION_TURN_OK) return input_status;

    laplace_digest256 previous_state_id{};
    laplace_digest256 previous_frame_fingerprint{};
    if ((input->flags & LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE) != 0U) {
        laplace_cognition_discourse_state previous{};
        laplace_cognition_discourse_frame_receipt frame_receipt{};
        const auto frame_status = laplace_cognition_discourse_frame_decode(
            previous_frame, previous_frame_bytes, &previous, &frame_receipt);
        if (frame_status != LAPLACE_COGNITION_DISCOURSE_FRAME_OK) {
            return LAPLACE_COGNITION_TURN_PREVIOUS_FRAME_INVALID;
        }
        if (!SameDigest(previous.discourse_id, input->discourse_id)) {
            return LAPLACE_COGNITION_TURN_DISCOURSE_MISMATCH;
        }
        if (!SameDigest(previous.world_id, input->world_id)) {
            return LAPLACE_COGNITION_TURN_WORLD_MISMATCH;
        }
        if (previous.turn_ordinal == std::numeric_limits<std::uint64_t>::max() ||
            input->turn_ordinal != previous.turn_ordinal + 1U) {
            return LAPLACE_COGNITION_TURN_SEQUENCE_MISMATCH;
        }
        previous_state_id = previous.state_id;
        previous_frame_fingerprint = frame_receipt.frame_fingerprint;
    }

    laplace_cognition_observation_request compiled{};
    compiled.anchor_entity_id = input->observation_entity_id;
    compiled.goal_entity_id = policy->goal_entity_id;
    compiled.world_id = input->world_id;
    compiled.time_fingerprint = input->time_fingerprint;
    TurnContext(*input, previous_state_id, &compiled.context_fingerprint);
    compiled.evidence_boundary = policy->evidence_boundary;
    compiled.evidence_epoch = policy->evidence_epoch;
    compiled.authority_id = policy->authority_id;
    compiled.result_contract_fingerprint = policy->result_contract_fingerprint;
    compiled.search_budget = policy->search_budget;
    compiled.forward_limits = policy->forward_limits;
    compiled.relation_mask = policy->relation_mask;
    compiled.maximum_results = policy->maximum_results;
    compiled.flags = policy->request_flags;
    compiled.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    laplace_digest256 request_fingerprint{};
    if (laplace_cognition_observation_request_identify(
            &compiled, &request_fingerprint) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        return LAPLACE_COGNITION_TURN_REQUEST_INVALID;
    }

    laplace_cognition_turn_receipt compiled_receipt{};
    compiled_receipt.request_fingerprint = request_fingerprint;
    compiled_receipt.previous_state_id = previous_state_id;
    compiled_receipt.previous_frame_fingerprint = previous_frame_fingerprint;
    compiled_receipt.previous_frame_bytes =
        (input->flags & LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE) != 0U
            ? static_cast<std::uint64_t>(previous_frame_bytes)
            : 0U;
    compiled_receipt.turn_ordinal = input->turn_ordinal;
    compiled_receipt.flags = input->flags;
    compiled_receipt.version = LAPLACE_COGNITION_TURN_RECEIPT_VERSION;
    TurnReceiptIdentity(
        *input, request_fingerprint, previous_state_id,
        previous_frame_fingerprint, &compiled_receipt.turn_fingerprint);

    *request = compiled;
    *receipt = compiled_receipt;
    return LAPLACE_COGNITION_TURN_OK;
}
