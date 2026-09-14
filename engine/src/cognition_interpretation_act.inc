#include "laplace/cognition_interpretation_act.h"

#include "blake3.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace laplace_cognition_interpretation_act_detail {

constexpr char RequestSetDomain[] =
    "laplace-cognition-interpreted-request-set-v1";
constexpr char AnswerSetDomain[] =
    "laplace-cognition-interpreted-semantic-answer-set-v1";
constexpr char ActDomain[] =
    "laplace-cognition-interpreted-semantic-act-v1";

using laplace_cognition_semantic_act_detail::BetterPrimary;
using laplace_cognition_semantic_act_detail::HashAnswer;
using laplace_cognition_semantic_act_detail::HashDigest;
using laplace_cognition_semantic_act_detail::HashId;
using laplace_cognition_semantic_act_detail::HashU32;
using laplace_cognition_semantic_act_detail::HashU64;
using laplace_cognition_semantic_act_detail::KnownProducerOperation;
using laplace_cognition_semantic_act_detail::SameDigest;

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameHeader(
    const laplace_cognition_guidance_header& left,
    const laplace_cognition_guidance_header& right) {
    return SameDigest(left.program_id, right.program_id) &&
        SameDigest(left.goal_id, right.goal_id) &&
        SameDigest(left.bindings_fingerprint, right.bindings_fingerprint) &&
        SameDigest(left.scope_fingerprint, right.scope_fingerprint) &&
        SameDigest(left.world_id, right.world_id) &&
        SameDigest(left.time_fingerprint, right.time_fingerprint) &&
        SameDigest(left.context_fingerprint, right.context_fingerprint) &&
        SameDigest(left.evidence_epoch, right.evidence_epoch) &&
        SameDigest(left.authority_id, right.authority_id) &&
        SameDigest(left.result_contract_fingerprint,
                   right.result_contract_fingerprint) &&
        left.version == right.version && left.flags == right.flags;
}

bool SameReceipt(
    const laplace_cognition_forward_receipt& left,
    const laplace_cognition_forward_receipt& right) {
    return SameDigest(left.receipt_id, right.receipt_id) &&
        SameDigest(left.program_fingerprint, right.program_fingerprint) &&
        SameDigest(left.initial_state_id, right.initial_state_id) &&
        SameDigest(left.final_state_id, right.final_state_id) &&
        SameDigest(left.layer_trace_fingerprint, right.layer_trace_fingerprint) &&
        SameDigest(left.output_fingerprint, right.output_fingerprint) &&
        left.layer_count == right.layer_count &&
        left.provider_call_count == right.provider_call_count &&
        left.projected_query_count == right.projected_query_count &&
        left.candidate_operation_count == right.candidate_operation_count &&
        left.resolution_count == right.resolution_count &&
        left.resource_cost == right.resource_cost &&
        left.io_operations == right.io_operations &&
        left.database_operations == right.database_operations &&
        left.final_remaining_required_count == right.final_remaining_required_count &&
        left.final_completion == right.final_completion &&
        left.disposition == right.disposition && left.status == right.status &&
        left.version == right.version && left.flags == right.flags;
}

void HashObligation(
    blake3_hasher* const hasher,
    const laplace_cognition_obligation& obligation) {
    HashDigest(hasher, obligation.obligation_id);
    HashDigest(hasher, obligation.value_id);
    HashDigest(hasher, obligation.binding_fingerprint);
    HashDigest(hasher, obligation.world_id);
    HashDigest(hasher, obligation.time_fingerprint);
    HashDigest(hasher, obligation.context_fingerprint);
    HashDigest(hasher, obligation.evidence_boundary);
    HashDigest(hasher, obligation.authority_id);
    HashDigest(hasher, obligation.result_contract_fingerprint);
    HashDigest(hasher, obligation.resolution_receipt_id);
    HashU32(hasher, obligation.kind);
    HashU32(hasher, obligation.disposition);
    HashU32(hasher, obligation.flags);
    HashU32(hasher, obligation.reserved);
}

}  // namespace laplace_cognition_interpretation_act_detail

extern "C" laplace_cognition_interpretation_act_status
laplace_cognition_interpretation_semantic_act_select(
    const laplace_cognition_interpretation_program_rule* const program_rule,
    const laplace_cognition_interpretation_act_rule* const act_rule,
    const laplace_cognition_interpretation_execution_result* const execution,
    const laplace_cognition_forward_receipt* const receipt,
    laplace_cognition_semantic_act* const act) {
    using namespace laplace_cognition_interpretation_act_detail;

    if (act != nullptr) *act = laplace_cognition_semantic_act{};
    if (program_rule == nullptr || act_rule == nullptr || execution == nullptr ||
        receipt == nullptr || act == nullptr || execution->forward == nullptr ||
        act_rule->version != LAPLACE_COGNITION_INTERPRETATION_ACT_RULE_VERSION ||
        act_rule->reserved != 0U ||
        act_rule->act_kind < LAPLACE_COGNITION_OPERATION_ANSWER ||
        act_rule->act_kind > LAPLACE_COGNITION_OPERATION_EXECUTE ||
        program_rule->obligations == nullptr || program_rule->obligation_count == 0U ||
        act_rule->primary_obligation_index >= program_rule->obligation_count ||
        execution->obligation_ids.size() != program_rule->obligation_count ||
        execution->request_fingerprints.size() != program_rule->obligation_count ||
        execution->answers.size() != program_rule->obligation_count ||
        !SameDigest(execution->query_program_id, program_rule->recipe_id) ||
        !SameDigest(execution->header.result_contract_fingerprint,
                    program_rule->result_contract_fingerprint)) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_INVALID_ARGUMENT;
    }

    laplace_cognition_forward_receipt owned{};
    laplace_cognition_guidance_header owned_header{};
    if (laplace_cognition_forward_result_receipt(execution->forward, &owned) !=
            LAPLACE_COGNITION_FORWARD_OK ||
        laplace_cognition_forward_result_header(execution->forward, &owned_header) !=
            LAPLACE_COGNITION_FORWARD_OK ||
        !SameReceipt(owned, *receipt) ||
        !SameHeader(owned_header, execution->header)) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_RESULT_FAILURE;
    }
    if (owned.status != LAPLACE_COGNITION_FORWARD_OK ||
        owned.disposition != LAPLACE_COGNITION_FORWARD_COMPLETE ||
        owned.final_completion != LAPLACE_COGNITION_COMPLETION_COMPLETE ||
        owned.final_remaining_required_count != 0U) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_INCOMPLETE;
    }

    const std::size_t layer_count =
        laplace_cognition_forward_result_layer_count(execution->forward);
    if (layer_count == 0U || owned.layer_count != layer_count) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_RESULT_FAILURE;
    }
    laplace_cognition_forward_layer_receipt final_layer{};
    if (laplace_cognition_forward_result_layer(
            execution->forward, layer_count - 1U, &final_layer) !=
            LAPLACE_COGNITION_FORWARD_OK ||
        final_layer.completion != LAPLACE_COGNITION_COMPLETION_COMPLETE ||
        !SameDigest(final_layer.next_state_id, owned.final_state_id) ||
        !KnownProducerOperation(final_layer.selected_kind)) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_RESULT_FAILURE;
    }

    const std::size_t obligation_count =
        laplace_cognition_forward_result_obligation_count(execution->forward);
    if (obligation_count != program_rule->obligation_count) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_RESULT_FAILURE;
    }
    std::vector<laplace_cognition_obligation> obligations(obligation_count);
    if (laplace_cognition_forward_result_obligations(
            execution->forward, 0U, obligations.data(), obligations.size()) !=
        LAPLACE_COGNITION_FORWARD_OK) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_RESULT_FAILURE;
    }

    blake3_hasher request_hasher{};
    blake3_hasher_init(&request_hasher);
    blake3_hasher_update(
        &request_hasher, RequestSetDomain, sizeof(RequestSetDomain) - 1U);
    HashDigest(&request_hasher, execution->header.program_id);
    HashDigest(&request_hasher, program_rule->recipe_id);
    HashDigest(&request_hasher, program_rule->witness_id);
    HashU64(&request_hasher, static_cast<std::uint64_t>(obligation_count));
    for (const auto& fingerprint : execution->request_fingerprints) {
        HashDigest(&request_hasher, fingerprint);
    }
    laplace_digest256 request_set_fingerprint{};
    blake3_hasher_finalize(
        &request_hasher, request_set_fingerprint.bytes,
        sizeof(request_set_fingerprint.bytes));

    blake3_hasher answer_hasher{};
    blake3_hasher_init(&answer_hasher);
    blake3_hasher_update(
        &answer_hasher, AnswerSetDomain, sizeof(AnswerSetDomain) - 1U);
    HashDigest(&answer_hasher, execution->header.program_id);
    HashDigest(&answer_hasher, program_rule->recipe_id);
    HashDigest(&answer_hasher, program_rule->witness_id);
    HashDigest(&answer_hasher, program_rule->result_contract_fingerprint);
    HashDigest(&answer_hasher, owned.receipt_id);
    HashDigest(&answer_hasher, owned.output_fingerprint);
    HashU32(&answer_hasher, act_rule->primary_obligation_index);
    HashU32(&answer_hasher, act_rule->act_kind);
    HashU64(&answer_hasher, static_cast<std::uint64_t>(obligation_count));

    std::uint64_t total_answers = 0U;
    laplace_cognition_observation_answer primary{};
    bool primary_present = false;
    for (std::size_t index = 0U; index < obligation_count; ++index) {
        const auto& obligation = obligations[index];
        if (!SameDigest(obligation.obligation_id, execution->obligation_ids[index]) ||
            obligation.disposition == LAPLACE_COGNITION_OBLIGATION_OPEN) {
            return LAPLACE_COGNITION_INTERPRETATION_ACT_RESULT_FAILURE;
        }
        HashObligation(&answer_hasher, obligation);
        const auto& answers = execution->answers[index];
        if (answers.size() > static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max()) ||
            total_answers > std::numeric_limits<std::uint64_t>::max() -
                static_cast<std::uint64_t>(answers.size())) {
            return LAPLACE_COGNITION_INTERPRETATION_ACT_RANGE;
        }
        total_answers += static_cast<std::uint64_t>(answers.size());
        HashU64(&answer_hasher, static_cast<std::uint64_t>(answers.size()));
        for (const auto& answer : answers) HashAnswer(&answer_hasher, answer);

        if (index == act_rule->primary_obligation_index) {
            if (obligation.disposition != LAPLACE_COGNITION_OBLIGATION_SATISFIED ||
                answers.empty()) {
                return LAPLACE_COGNITION_INTERPRETATION_ACT_INCOMPLETE;
            }
            for (const auto& answer : answers) {
                if (!primary_present) {
                    primary = answer;
                    primary_present = true;
                } else if (!SameId(primary.entity_id, answer.entity_id)) {
                    return LAPLACE_COGNITION_INTERPRETATION_ACT_AMBIGUOUS;
                } else if (BetterPrimary(answer, primary)) {
                    primary = answer;
                }
            }
        }
    }
    if (!primary_present || total_answers == 0U) {
        return LAPLACE_COGNITION_INTERPRETATION_ACT_INCOMPLETE;
    }

    laplace_digest256 answer_set_fingerprint{};
    blake3_hasher_finalize(
        &answer_hasher, answer_set_fingerprint.bytes,
        sizeof(answer_set_fingerprint.bytes));

    act->request_fingerprint = request_set_fingerprint;
    act->result_contract_fingerprint = program_rule->result_contract_fingerprint;
    act->forward_receipt_id = owned.receipt_id;
    act->forward_output_fingerprint = owned.output_fingerprint;
    act->final_state_id = owned.final_state_id;
    act->answer_set_fingerprint = answer_set_fingerprint;
    act->primary_answer = primary;
    act->answer_count = total_answers;
    act->act_kind = act_rule->act_kind;
    act->producer_operation_kind = final_layer.selected_kind;
    act->flags = LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_PRODUCER_OPERATION_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT;
    act->version = LAPLACE_COGNITION_SEMANTIC_ACT_VERSION;

    blake3_hasher act_hasher{};
    blake3_hasher_init(&act_hasher);
    blake3_hasher_update(&act_hasher, ActDomain, sizeof(ActDomain) - 1U);
    HashDigest(&act_hasher, execution->header.program_id);
    HashDigest(&act_hasher, program_rule->recipe_id);
    HashDigest(&act_hasher, program_rule->witness_id);
    HashU32(&act_hasher, act_rule->primary_obligation_index);
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
    return LAPLACE_COGNITION_INTERPRETATION_ACT_OK;
}
