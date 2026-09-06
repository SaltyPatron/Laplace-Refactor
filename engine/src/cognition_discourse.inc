#include "laplace/cognition_discourse.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace laplace_cognition_discourse_detail {

constexpr char StateDomain[] = "laplace-cognition-discourse-state-v1";

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

bool InactivePayloadCanonical(
    const std::uint32_t flags,
    const std::uint32_t presence_flag,
    const laplace_digest256& value) {
    return (flags & presence_flag) != 0U || DigestZero(value);
}

bool SemanticActValid(const laplace_cognition_semantic_act& act) {
    const std::uint32_t required_flags =
        LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_PRODUCER_OPERATION_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT;
    return act.version == LAPLACE_COGNITION_SEMANTIC_ACT_VERSION &&
        (act.flags & ~LAPLACE_COGNITION_SEMANTIC_ACT_KNOWN_FLAGS) == 0U &&
        (act.flags & required_flags) == required_flags &&
        act.answer_count != 0U &&
        act.act_kind >= LAPLACE_COGNITION_OPERATION_ANSWER &&
        act.act_kind <= LAPLACE_COGNITION_OPERATION_EXECUTE &&
        act.producer_operation_kind >=
            LAPLACE_COGNITION_OPERATION_STRUCTURAL_CALCULATION &&
        act.producer_operation_kind <= LAPLACE_COGNITION_OPERATION_EXECUTE;
}

laplace_cognition_discourse_status ValidateInput(
    const laplace_cognition_discourse_input& input) {
    if (input.version != LAPLACE_COGNITION_DISCOURSE_VERSION) {
        return LAPLACE_COGNITION_DISCOURSE_INVALID_ARGUMENT;
    }
    if ((input.flags & ~LAPLACE_COGNITION_DISCOURSE_KNOWN_FLAGS) != 0U) {
        return LAPLACE_COGNITION_DISCOURSE_INVALID_FLAGS;
    }
    if (DigestZero(input.discourse_id) ||
        DigestZero(input.observation_occurrence_id) ||
        DigestZero(input.world_id) ||
        DigestZero(input.time_fingerprint) ||
        DigestZero(input.context_fingerprint)) {
        return LAPLACE_COGNITION_DISCOURSE_INVALID_ARGUMENT;
    }
    if (!InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE,
            input.previous_state_id) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES,
            input.active_entity_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_PROPOSITIONS,
            input.proposition_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_REFERENTS,
            input.referent_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_UNRESOLVED_QUESTIONS,
            input.unresolved_question_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_CORRECTIONS,
            input.correction_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_ELLIPSIS_BINDINGS,
            input.ellipsis_binding_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_CROSS_MODAL_ARTIFACTS,
            input.cross_modal_artifact_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_REJECTED_INTERPRETATIONS,
            input.rejected_interpretation_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_PRIOR_PROGRAMS,
            input.prior_program_set_fingerprint) ||
        !InactivePayloadCanonical(
            input.flags, LAPLACE_COGNITION_DISCOURSE_HAS_RECEIPTS,
            input.receipt_set_fingerprint)) {
        return LAPLACE_COGNITION_DISCOURSE_INACTIVE_PAYLOAD;
    }
    return LAPLACE_COGNITION_DISCOURSE_OK;
}

laplace_cognition_discourse_status ValidateStateShape(
    const laplace_cognition_discourse_state& state) {
    laplace_cognition_discourse_input input{};
    input.discourse_id = state.discourse_id;
    input.previous_state_id = state.previous_state_id;
    input.observation_entity_id = state.observation_entity_id;
    input.observation_occurrence_id = state.observation_occurrence_id;
    input.active_entity_set_fingerprint = state.active_entity_set_fingerprint;
    input.proposition_set_fingerprint = state.proposition_set_fingerprint;
    input.referent_set_fingerprint = state.referent_set_fingerprint;
    input.unresolved_question_set_fingerprint =
        state.unresolved_question_set_fingerprint;
    input.correction_set_fingerprint = state.correction_set_fingerprint;
    input.ellipsis_binding_set_fingerprint = state.ellipsis_binding_set_fingerprint;
    input.cross_modal_artifact_set_fingerprint =
        state.cross_modal_artifact_set_fingerprint;
    input.rejected_interpretation_set_fingerprint =
        state.rejected_interpretation_set_fingerprint;
    input.prior_program_set_fingerprint = state.prior_program_set_fingerprint;
    input.receipt_set_fingerprint = state.receipt_set_fingerprint;
    input.world_id = state.world_id;
    input.time_fingerprint = state.time_fingerprint;
    input.context_fingerprint = state.context_fingerprint;
    input.turn_ordinal = state.turn_ordinal;
    input.flags = state.flags;
    input.version = state.version;
    return ValidateInput(input);
}

void IdentifyUnchecked(
    const laplace_cognition_discourse_state& state,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, StateDomain, sizeof(StateDomain) - 1U);
    HashDigest(&hasher, state.discourse_id);
    HashDigest(&hasher, state.previous_state_id);
    HashId(&hasher, state.observation_entity_id);
    HashDigest(&hasher, state.observation_occurrence_id);
    HashDigest(&hasher, state.semantic_act_id);
    HashDigest(&hasher, state.active_entity_set_fingerprint);
    HashDigest(&hasher, state.proposition_set_fingerprint);
    HashDigest(&hasher, state.referent_set_fingerprint);
    HashDigest(&hasher, state.unresolved_question_set_fingerprint);
    HashDigest(&hasher, state.correction_set_fingerprint);
    HashDigest(&hasher, state.ellipsis_binding_set_fingerprint);
    HashDigest(&hasher, state.cross_modal_artifact_set_fingerprint);
    HashDigest(&hasher, state.rejected_interpretation_set_fingerprint);
    HashDigest(&hasher, state.prior_program_set_fingerprint);
    HashDigest(&hasher, state.receipt_set_fingerprint);
    HashDigest(&hasher, state.world_id);
    HashDigest(&hasher, state.time_fingerprint);
    HashDigest(&hasher, state.context_fingerprint);
    HashU64(&hasher, state.turn_ordinal);
    HashU32(&hasher, state.flags);
    HashU32(&hasher, state.version);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

}  // namespace laplace_cognition_discourse_detail

extern "C" laplace_cognition_discourse_status
laplace_cognition_discourse_state_create(
    const laplace_cognition_discourse_input* const input,
    const laplace_cognition_semantic_act* const semantic_act,
    laplace_cognition_discourse_state* const state) {
    using namespace laplace_cognition_discourse_detail;
    if (state != nullptr) *state = laplace_cognition_discourse_state{};
    if (input == nullptr || semantic_act == nullptr || state == nullptr) {
        return LAPLACE_COGNITION_DISCOURSE_INVALID_ARGUMENT;
    }
    const auto input_status = ValidateInput(*input);
    if (input_status != LAPLACE_COGNITION_DISCOURSE_OK) return input_status;
    if (!SemanticActValid(*semantic_act)) {
        return LAPLACE_COGNITION_DISCOURSE_SEMANTIC_ACT_INVALID;
    }

    state->discourse_id = input->discourse_id;
    state->previous_state_id = input->previous_state_id;
    state->observation_entity_id = input->observation_entity_id;
    state->observation_occurrence_id = input->observation_occurrence_id;
    state->semantic_act_id = semantic_act->act_id;
    state->active_entity_set_fingerprint = input->active_entity_set_fingerprint;
    state->proposition_set_fingerprint = input->proposition_set_fingerprint;
    state->referent_set_fingerprint = input->referent_set_fingerprint;
    state->unresolved_question_set_fingerprint =
        input->unresolved_question_set_fingerprint;
    state->correction_set_fingerprint = input->correction_set_fingerprint;
    state->ellipsis_binding_set_fingerprint = input->ellipsis_binding_set_fingerprint;
    state->cross_modal_artifact_set_fingerprint =
        input->cross_modal_artifact_set_fingerprint;
    state->rejected_interpretation_set_fingerprint =
        input->rejected_interpretation_set_fingerprint;
    state->prior_program_set_fingerprint = input->prior_program_set_fingerprint;
    state->receipt_set_fingerprint = input->receipt_set_fingerprint;
    state->world_id = input->world_id;
    state->time_fingerprint = input->time_fingerprint;
    state->context_fingerprint = input->context_fingerprint;
    state->turn_ordinal = input->turn_ordinal;
    state->flags = input->flags;
    state->version = input->version;
    IdentifyUnchecked(*state, &state->state_id);
    return LAPLACE_COGNITION_DISCOURSE_OK;
}

extern "C" laplace_cognition_discourse_status
laplace_cognition_discourse_state_identify(
    const laplace_cognition_discourse_state* const state,
    laplace_digest256* const state_id) {
    using namespace laplace_cognition_discourse_detail;
    if (state == nullptr || state_id == nullptr) {
        return LAPLACE_COGNITION_DISCOURSE_INVALID_ARGUMENT;
    }
    const auto status = ValidateStateShape(*state);
    if (status != LAPLACE_COGNITION_DISCOURSE_OK) return status;
    IdentifyUnchecked(*state, state_id);
    return LAPLACE_COGNITION_DISCOURSE_OK;
}

extern "C" laplace_cognition_discourse_status
laplace_cognition_discourse_state_validate(
    const laplace_cognition_discourse_state* const state) {
    using namespace laplace_cognition_discourse_detail;
    if (state == nullptr) return LAPLACE_COGNITION_DISCOURSE_INVALID_ARGUMENT;
    const auto status = ValidateStateShape(*state);
    if (status != LAPLACE_COGNITION_DISCOURSE_OK) return status;
    laplace_digest256 expected{};
    IdentifyUnchecked(*state, &expected);
    return SameDigest(expected, state->state_id)
        ? LAPLACE_COGNITION_DISCOURSE_OK
        : LAPLACE_COGNITION_DISCOURSE_IDENTITY_MISMATCH;
}
