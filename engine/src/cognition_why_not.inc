#include "laplace/cognition_why_not.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

namespace laplace_cognition_why_not_detail {

constexpr char OpenObligationsDomain[] =
    "laplace-cognition-why-not-open-obligations-v1";
constexpr char ContinuationDomain[] =
    "laplace-cognition-why-not-continuation-v1";
constexpr char WorldScopeDomain[] =
    "laplace-cognition-why-not-world-scope-v1";
constexpr char OutcomeDomain[] =
    "laplace-cognition-why-not-outcome-v1";

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

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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

bool RequiredOpen(const laplace_cognition_obligation& obligation) {
    return (obligation.flags & LAPLACE_COGNITION_OBLIGATION_REQUIRED) != 0U &&
        obligation.disposition != LAPLACE_COGNITION_OBLIGATION_SATISFIED;
}

void AppendCondition(
    std::vector<std::uint32_t>* const conditions,
    const std::uint32_t condition) {
    for (const auto present : *conditions) {
        if (present == condition) return;
    }
    conditions->push_back(condition);
}

std::uint32_t ObligationCondition(
    const laplace_cognition_obligation& obligation,
    const bool boundary_complete) {
    switch (obligation.disposition) {
        case LAPLACE_COGNITION_OBLIGATION_UNSUPPORTED:
            return LAPLACE_MACHINE_CONDITION_UNSUPPORTED_OPERATION;
        case LAPLACE_COGNITION_OBLIGATION_DENIED:
            return LAPLACE_MACHINE_CONDITION_AUTHORITY_DENIED;
        case LAPLACE_COGNITION_OBLIGATION_EXHAUSTED:
            return LAPLACE_MACHINE_CONDITION_RESOURCE_EXHAUSTED;
        case LAPLACE_COGNITION_OBLIGATION_CONTRADICTED:
            return LAPLACE_MACHINE_CONDITION_SEMANTIC_CONTRADICTION;
        case LAPLACE_COGNITION_OBLIGATION_UNKNOWN:
            return LAPLACE_MACHINE_CONDITION_UNKNOWN;
        case LAPLACE_COGNITION_OBLIGATION_OPEN:
            return boundary_complete
                ? LAPLACE_MACHINE_CONDITION_UNKNOWN
                : LAPLACE_MACHINE_CONDITION_INCOMPLETE_BOUNDARY;
        default:
            return LAPLACE_MACHINE_CONDITION_UNKNOWN;
    }
}

std::uint32_t ForwardCondition(
    const laplace_cognition_forward_receipt& receipt,
    const bool boundary_complete) {
    switch (receipt.disposition) {
        case LAPLACE_COGNITION_FORWARD_LAYER_LIMIT:
        case LAPLACE_COGNITION_FORWARD_PROVIDER_LIMIT:
        case LAPLACE_COGNITION_FORWARD_RESOURCE_LIMIT:
            return LAPLACE_MACHINE_CONDITION_RESOURCE_EXHAUSTED;
        case LAPLACE_COGNITION_FORWARD_PROVIDER_FAILURE:
            return LAPLACE_MACHINE_CONDITION_PROVIDER_UNAVAILABLE;
        case LAPLACE_COGNITION_FORWARD_NO_OPERATION:
            return boundary_complete
                ? LAPLACE_MACHINE_CONDITION_UNSUPPORTED_OPERATION
                : LAPLACE_MACHINE_CONDITION_INCOMPLETE_BOUNDARY;
        case LAPLACE_COGNITION_FORWARD_BLOCKED:
            return boundary_complete
                ? LAPLACE_MACHINE_CONDITION_UNKNOWN
                : LAPLACE_MACHINE_CONDITION_INCOMPLETE_BOUNDARY;
        default:
            return LAPLACE_MACHINE_CONDITION_UNKNOWN;
    }
}

void WorldScopeFingerprint(
    const laplace_cognition_guidance_header& header,
    laplace_digest256* const fingerprint) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, WorldScopeDomain, sizeof(WorldScopeDomain) - 1U);
    HashDigest(&hasher, header.world_id);
    HashDigest(&hasher, header.time_fingerprint);
    HashDigest(&hasher, header.context_fingerprint);
    HashDigest(&hasher, header.evidence_epoch);
    HashDigest(&hasher, header.authority_id);
    Finish(&hasher, fingerprint);
}

}  // namespace laplace_cognition_why_not_detail

extern "C" laplace_cognition_why_not_status
laplace_cognition_forward_why_not(
    const laplace_cognition_observation_request* const request,
    const laplace_cognition_forward_result* const forward_result,
    laplace_cognition_why_not_result* const result) {
    using namespace laplace_cognition_why_not_detail;

    if (result != nullptr) *result = laplace_cognition_why_not_result{};
    if (request == nullptr || forward_result == nullptr || result == nullptr) {
        return LAPLACE_COGNITION_WHY_NOT_INVALID_ARGUMENT;
    }

    laplace_cognition_forward_receipt receipt{};
    laplace_cognition_guidance_header header{};
    laplace_digest256 request_fingerprint{};
    if (laplace_cognition_forward_result_receipt(forward_result, &receipt) !=
            LAPLACE_COGNITION_FORWARD_OK ||
        laplace_cognition_forward_result_header(forward_result, &header) !=
            LAPLACE_COGNITION_FORWARD_OK) {
        return LAPLACE_COGNITION_WHY_NOT_FORWARD_RESULT_FAILURE;
    }
    if (laplace_cognition_observation_request_identify(
            request, &request_fingerprint) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        return LAPLACE_COGNITION_WHY_NOT_REQUEST_FAILURE;
    }
    if (!SameDigest(header.world_id, request->world_id) ||
        !SameDigest(header.time_fingerprint, request->time_fingerprint) ||
        !SameDigest(header.context_fingerprint, request->context_fingerprint) ||
        !SameDigest(header.evidence_epoch, request->evidence_epoch) ||
        !SameDigest(header.authority_id, request->authority_id) ||
        !SameDigest(
            header.result_contract_fingerprint,
            request->result_contract_fingerprint)) {
        return LAPLACE_COGNITION_WHY_NOT_REQUEST_FAILURE;
    }
    if (receipt.status != LAPLACE_COGNITION_FORWARD_OK) {
        return LAPLACE_COGNITION_WHY_NOT_FORWARD_RESULT_FAILURE;
    }
    if (receipt.disposition == LAPLACE_COGNITION_FORWARD_COMPLETE &&
        receipt.final_completion == LAPLACE_COGNITION_COMPLETION_COMPLETE &&
        receipt.final_remaining_required_count == 0U) {
        return LAPLACE_COGNITION_WHY_NOT_NOT_APPLICABLE;
    }

    const std::size_t obligation_count =
        laplace_cognition_forward_result_obligation_count(forward_result);
    std::vector<laplace_cognition_obligation> obligations;
    std::vector<std::uint32_t> conditions;
    try {
        obligations.resize(obligation_count);
        conditions.reserve(obligation_count + 1U);
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_WHY_NOT_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_COGNITION_WHY_NOT_MEMORY_FAILURE;
    }
    if (obligation_count != 0U &&
        laplace_cognition_forward_result_obligations(
            forward_result, 0U, obligations.data(), obligation_count) !=
            LAPLACE_COGNITION_FORWARD_OK) {
        return LAPLACE_COGNITION_WHY_NOT_FORWARD_RESULT_FAILURE;
    }

    const bool boundary_complete =
        (request->flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE) != 0U;
    std::uint64_t open_count = 0U;
    blake3_hasher open_hasher{};
    blake3_hasher_init(&open_hasher);
    blake3_hasher_update(
        &open_hasher, OpenObligationsDomain,
        sizeof(OpenObligationsDomain) - 1U);
    for (const auto& obligation : obligations) {
        if (!RequiredOpen(obligation)) continue;
        ++open_count;
        HashObligation(&open_hasher, obligation);
        AppendCondition(
            &conditions, ObligationCondition(obligation, boundary_complete));
    }
    HashU64(&open_hasher, open_count);
    laplace_digest256 open_fingerprint{};
    Finish(&open_hasher, &open_fingerprint);
    if (open_count != receipt.final_remaining_required_count) {
        return LAPLACE_COGNITION_WHY_NOT_FORWARD_RESULT_FAILURE;
    }

    AppendCondition(
        &conditions, ForwardCondition(receipt, boundary_complete));
    if (conditions.empty()) {
        AppendCondition(&conditions, LAPLACE_MACHINE_CONDITION_UNKNOWN);
    }

    laplace_machine_exception_binding binding{};
    binding.program_fingerprint = receipt.program_fingerprint;
    WorldScopeFingerprint(header, &binding.world_scope_fingerprint);
    binding.last_valid_receipt_fingerprint = receipt.receipt_id;
    binding.presence_mask =
        LAPLACE_MACHINE_EXCEPTION_BIND_PROGRAM |
        LAPLACE_MACHINE_EXCEPTION_BIND_WORLD_SCOPE |
        LAPLACE_MACHINE_EXCEPTION_BIND_LAST_VALID_RECEIPT;
    binding.affected_instruction_index = receipt.layer_count;

    laplace_machine_exception_receipt exception{};
    if (laplace_machine_exception_classify(
            conditions.data(), conditions.size(), &binding, &exception) !=
        LAPLACE_MACHINE_EXCEPTION_OK) {
        return LAPLACE_COGNITION_WHY_NOT_MACHINE_EXCEPTION_FAILURE;
    }

    blake3_hasher continuation_hasher{};
    blake3_hasher_init(&continuation_hasher);
    blake3_hasher_update(
        &continuation_hasher, ContinuationDomain,
        sizeof(ContinuationDomain) - 1U);
    HashDigest(&continuation_hasher, request_fingerprint);
    HashDigest(&continuation_hasher, receipt.receipt_id);
    HashDigest(&continuation_hasher, receipt.final_state_id);
    HashDigest(&continuation_hasher, receipt.output_fingerprint);
    HashDigest(&continuation_hasher, open_fingerprint);
    HashU64(&continuation_hasher, receipt.layer_count);
    HashU64(&continuation_hasher, receipt.final_remaining_required_count);
    HashU32(&continuation_hasher, receipt.disposition);
    HashU32(&continuation_hasher, receipt.final_completion);
    HashU32(&continuation_hasher, exception.selected.condition);
    laplace_digest256 continuation_fingerprint{};
    Finish(&continuation_hasher, &continuation_fingerprint);

    laplace_machine_why_not why_not{};
    if (laplace_machine_exception_why_not(
            &exception,
            receipt.layer_count,
            open_count,
            &open_fingerprint,
            &continuation_fingerprint,
            &why_not) != LAPLACE_MACHINE_EXCEPTION_OK) {
        return LAPLACE_COGNITION_WHY_NOT_MACHINE_EXCEPTION_FAILURE;
    }

    result->request_fingerprint = request_fingerprint;
    result->forward_receipt_id = receipt.receipt_id;
    result->open_obligations_fingerprint = open_fingerprint;
    result->continuation_condition_fingerprint = continuation_fingerprint;
    result->exception_receipt = exception;
    result->why_not = why_not;
    result->forward_disposition = receipt.disposition;
    result->forward_completion = receipt.final_completion;
    result->version = LAPLACE_COGNITION_WHY_NOT_VERSION;

    blake3_hasher outcome_hasher{};
    blake3_hasher_init(&outcome_hasher);
    blake3_hasher_update(
        &outcome_hasher, OutcomeDomain, sizeof(OutcomeDomain) - 1U);
    HashDigest(&outcome_hasher, result->request_fingerprint);
    HashDigest(&outcome_hasher, result->forward_receipt_id);
    HashDigest(&outcome_hasher, result->open_obligations_fingerprint);
    HashDigest(&outcome_hasher, result->continuation_condition_fingerprint);
    HashU64(&outcome_hasher, result->why_not.completed_work_units);
    HashU64(&outcome_hasher, result->why_not.open_obligation_count);
    HashU32(&outcome_hasher, result->why_not.limiting_condition);
    HashU32(&outcome_hasher, result->why_not.recovery_disposition);
    HashU32(&outcome_hasher, result->forward_disposition);
    HashU32(&outcome_hasher, result->forward_completion);
    HashU32(&outcome_hasher, result->version);
    Finish(&outcome_hasher, &result->outcome_id);
    return LAPLACE_COGNITION_WHY_NOT_OK;
}
