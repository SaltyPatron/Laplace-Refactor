#include "laplace/cognition_realization.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace laplace_cognition_realization_detail {

constexpr char RequestDomain[] = "laplace-cognition-realization-request-v1";
constexpr char ResultDomain[] = "laplace-cognition-realization-result-v1";
constexpr char ReceiptDomain[] = "laplace-cognition-realization-receipt-v1";

bool BytesZero(const std::uint8_t* const bytes, const std::size_t count) {
    std::uint8_t aggregate = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        aggregate = static_cast<std::uint8_t>(aggregate | bytes[index]);
    }
    return aggregate == 0U;
}

bool IdZero(const laplace_id128& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

bool DigestZero(const laplace_digest256& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

int CompareId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes));
}

int CompareDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes));
}

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

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool SemanticActValid(const laplace_cognition_semantic_act& act) {
    return act.version == LAPLACE_COGNITION_SEMANTIC_ACT_VERSION &&
        (act.flags & ~LAPLACE_COGNITION_SEMANTIC_ACT_KNOWN_FLAGS) == 0U &&
        (act.flags & LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT) != 0U &&
        (act.flags & LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT) != 0U &&
        !DigestZero(act.act_id) && act.answer_count > 0U &&
        !IdZero(act.primary_answer.entity_id);
}

laplace_cognition_realization_status RequestValid(
    const laplace_cognition_realization_request& request) {
    if (request.version != LAPLACE_COGNITION_REALIZATION_VERSION ||
        request.maximum_candidates == 0U || IdZero(request.modality_id) ||
        DigestZero(request.evidence_epoch) ||
        DigestZero(request.realization_recipe_epoch) ||
        DigestZero(request.context_fingerprint)) {
        return LAPLACE_COGNITION_REALIZATION_INVALID_ARGUMENT;
    }
    if ((request.flags & ~LAPLACE_COGNITION_REALIZATION_KNOWN_REQUEST_FLAGS) != 0U) {
        return LAPLACE_COGNITION_REALIZATION_INVALID_FLAGS;
    }
    const bool language_present =
        (request.flags & LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT) != 0U;
    const bool register_present =
        (request.flags & LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT) != 0U;
    if (language_present == IdZero(request.language_id) ||
        register_present == IdZero(request.register_id)) {
        return LAPLACE_COGNITION_REALIZATION_INVALID_FLAGS;
    }
    return LAPLACE_COGNITION_REALIZATION_OK;
}

bool ProviderValid(const laplace_cognition_realization_provider_v1& provider) {
    return provider.abi_major == LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <= LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR &&
        provider.enumerate != nullptr && provider.maximum_candidate_records > 0U &&
        !DigestZero(provider.provider_fingerprint) && provider.flags == 0U &&
        provider.reserved == 0U;
}

bool CandidateValid(
    const laplace_cognition_realization_candidate& candidate,
    const laplace_cognition_realization_request& request) {
    if (IdZero(candidate.content_id) ||
        DigestZero(candidate.candidate_receipt_id) ||
        DigestZero(candidate.realization_recipe_id) ||
        DigestZero(candidate.obligation_fingerprint) ||
        (candidate.flags & ~LAPLACE_COGNITION_REALIZATION_CANDIDATE_KNOWN_FLAGS) != 0U ||
        candidate.match_class < LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE ||
        candidate.match_class > LAPLACE_COGNITION_REALIZATION_CANDIDATE_STRUCTURAL_FALLBACK) {
        return false;
    }
    if ((request.flags & LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT) != 0U) {
        if (!SameId(candidate.language_id, request.language_id)) return false;
    } else if (!IdZero(candidate.language_id)) {
        return false;
    }
    return true;
}

void IdentifyRequest(
    const laplace_cognition_semantic_act& act,
    const laplace_cognition_realization_request& request,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, RequestDomain, sizeof(RequestDomain) - 1U);
    HashDigest(&hasher, act.act_id);
    HashDigest(&hasher, act.answer_set_fingerprint);
    HashId(&hasher, request.modality_id);
    HashId(&hasher, request.language_id);
    HashId(&hasher, request.register_id);
    HashDigest(&hasher, request.evidence_epoch);
    HashDigest(&hasher, request.realization_recipe_epoch);
    HashDigest(&hasher, request.context_fingerprint);
    HashU32(&hasher, request.maximum_candidates);
    HashU32(&hasher, request.flags);
    Finish(&hasher, output);
}

void IdentifyResult(
    const laplace_cognition_realization_result& result,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ResultDomain, sizeof(ResultDomain) - 1U);
    HashId(&hasher, result.content_id);
    HashId(&hasher, result.language_id);
    HashDigest(&hasher, result.candidate_receipt_id);
    HashDigest(&hasher, result.realization_recipe_id);
    HashDigest(&hasher, result.missing_obligation_fingerprint);
    HashU64(&hasher, result.preference_rank);
    HashU64(&hasher, result.reused_subtree_count);
    HashU64(&hasher, result.generated_composition_count);
    HashU32(&hasher, result.structural_tier);
    HashU32(&hasher, result.match_class);
    HashU32(&hasher, result.missing_obligation_count);
    HashU32(&hasher, result.disposition);
    HashU32(&hasher, result.flags);
    Finish(&hasher, output);
}

void IdentifyReceipt(
    const laplace_cognition_realization_receipt& receipt,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ReceiptDomain, sizeof(ReceiptDomain) - 1U);
    HashDigest(&hasher, receipt.semantic_act_id);
    HashDigest(&hasher, receipt.provider_fingerprint);
    HashDigest(&hasher, receipt.provider_receipt_id);
    HashDigest(&hasher, receipt.request_fingerprint);
    HashDigest(&hasher, receipt.result_fingerprint);
    HashU64(&hasher, receipt.candidate_count);
    HashU64(&hasher, receipt.eligible_candidate_count);
    HashU32(&hasher, receipt.disposition);
    Finish(&hasher, output);
}

bool BetterCandidate(
    const laplace_cognition_realization_candidate& candidate,
    const laplace_cognition_realization_candidate& current) {
    if (candidate.match_class != current.match_class) {
        return candidate.match_class < current.match_class;
    }
    if (candidate.preference_rank != current.preference_rank) {
        return candidate.preference_rank < current.preference_rank;
    }
    const int content_order = CompareId(candidate.content_id, current.content_id);
    if (content_order != 0) return content_order < 0;
    return CompareDigest(candidate.candidate_receipt_id, current.candidate_receipt_id) < 0;
}

bool SemanticAmbiguity(
    const laplace_cognition_realization_candidate& candidate,
    const laplace_cognition_realization_candidate& selected) {
    return candidate.match_class == selected.match_class &&
        candidate.preference_rank == selected.preference_rank &&
        !SameId(candidate.content_id, selected.content_id);
}

laplace_cognition_realization_status StatusForDisposition(const std::uint32_t disposition) {
    switch (disposition) {
        case LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED:
            return LAPLACE_COGNITION_REALIZATION_UNSUPPORTED;
        case LAPLACE_COGNITION_REALIZATION_DISPOSITION_INCOMPLETE:
            return LAPLACE_COGNITION_REALIZATION_INCOMPLETE;
        case LAPLACE_COGNITION_REALIZATION_DISPOSITION_AMBIGUOUS:
            return LAPLACE_COGNITION_REALIZATION_AMBIGUOUS;
        default:
            return LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE;
    }
}

}  // namespace laplace_cognition_realization_detail

extern "C" laplace_cognition_realization_status
laplace_cognition_semantic_act_realize(
    const laplace_cognition_semantic_act* const semantic_act,
    const laplace_cognition_realization_request* const request,
    const laplace_cognition_realization_provider_v1* const provider,
    laplace_cognition_realization_result* const result,
    laplace_cognition_realization_receipt* const receipt) {
    using namespace laplace_cognition_realization_detail;

    if (result != nullptr) *result = laplace_cognition_realization_result{};
    if (receipt != nullptr) *receipt = laplace_cognition_realization_receipt{};
    if (semantic_act == nullptr || request == nullptr || provider == nullptr ||
        result == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_REALIZATION_INVALID_ARGUMENT;
    }
    if (!SemanticActValid(*semantic_act)) {
        return LAPLACE_COGNITION_REALIZATION_INVALID_SEMANTIC_ACT;
    }
    const auto request_status = RequestValid(*request);
    if (request_status != LAPLACE_COGNITION_REALIZATION_OK) return request_status;
    if (!ProviderValid(*provider)) {
        return LAPLACE_COGNITION_REALIZATION_INVALID_PROVIDER;
    }

    const std::uint64_t bounded_capacity_u64 = std::min(
        static_cast<std::uint64_t>(request->maximum_candidates),
        provider->maximum_candidate_records);
    if (bounded_capacity_u64 == 0U ||
        bounded_capacity_u64 > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_COGNITION_REALIZATION_RANGE;
    }
    const auto capacity = static_cast<std::size_t>(bounded_capacity_u64);

    std::vector<laplace_cognition_realization_candidate> candidates;
    try {
        candidates.resize(capacity);
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_REALIZATION_RANGE;
    }

    std::size_t candidate_count = 0U;
    laplace_cognition_realization_usage usage{};
    if (provider->enumerate(
            provider->state, semantic_act, request, candidates.data(), capacity,
            &candidate_count, &usage) != 0) {
        return LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE;
    }
    if (candidate_count > capacity ||
        usage.disposition > LAPLACE_COGNITION_REALIZATION_DISPOSITION_AMBIGUOUS ||
        DigestZero(usage.provider_receipt_id)) {
        return LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE;
    }

    for (std::size_t index = 0U; index < candidate_count; ++index) {
        if (!CandidateValid(candidates[index], *request)) {
            return LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE;
        }
    }

    laplace_digest256 request_fingerprint{};
    IdentifyRequest(*semantic_act, *request, &request_fingerprint);

    std::size_t eligible_count = 0U;
    const laplace_cognition_realization_candidate* selected = nullptr;
    for (std::size_t index = 0U; index < candidate_count; ++index) {
        const auto& candidate = candidates[index];
        if (candidate.missing_obligation_count != 0U) continue;
        ++eligible_count;
        if (selected == nullptr || BetterCandidate(candidate, *selected)) {
            selected = &candidate;
        }
    }

    laplace_cognition_realization_result resolved{};
    resolved.version = LAPLACE_COGNITION_REALIZATION_VERSION;

    laplace_cognition_realization_status status = LAPLACE_COGNITION_REALIZATION_OK;
    if (selected != nullptr) {
        if (usage.disposition != LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE) {
            return LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE;
        }
        bool ambiguous = false;
        for (std::size_t index = 0U; index < candidate_count; ++index) {
            const auto& candidate = candidates[index];
            if (candidate.missing_obligation_count == 0U &&
                SemanticAmbiguity(candidate, *selected)) {
                ambiguous = true;
                break;
            }
        }
        if (ambiguous) {
            resolved.disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_AMBIGUOUS;
            status = LAPLACE_COGNITION_REALIZATION_AMBIGUOUS;
        } else {
            resolved.content_id = selected->content_id;
            resolved.language_id = selected->language_id;
            resolved.candidate_receipt_id = selected->candidate_receipt_id;
            resolved.realization_recipe_id = selected->realization_recipe_id;
            resolved.preference_rank = selected->preference_rank;
            resolved.reused_subtree_count = selected->reused_subtree_count;
            resolved.generated_composition_count = selected->generated_composition_count;
            resolved.structural_tier = selected->structural_tier;
            resolved.match_class = selected->match_class;
            resolved.disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
            resolved.flags = selected->flags;
        }
    } else {
        if (usage.disposition == LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE) {
            return LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE;
        }
        resolved.missing_obligation_fingerprint = usage.missing_obligation_fingerprint;
        resolved.missing_obligation_count = usage.missing_obligation_count;
        resolved.disposition = usage.disposition;
        if (resolved.missing_obligation_count == 0U ||
            DigestZero(resolved.missing_obligation_fingerprint)) {
            return LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE;
        }
        status = StatusForDisposition(usage.disposition);
        if (status == LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE) return status;
    }

    laplace_digest256 result_fingerprint{};
    IdentifyResult(resolved, &result_fingerprint);

    laplace_cognition_realization_receipt resolved_receipt{};
    resolved_receipt.semantic_act_id = semantic_act->act_id;
    resolved_receipt.provider_fingerprint = provider->provider_fingerprint;
    resolved_receipt.provider_receipt_id = usage.provider_receipt_id;
    resolved_receipt.request_fingerprint = request_fingerprint;
    resolved_receipt.result_fingerprint = result_fingerprint;
    resolved_receipt.candidate_count = static_cast<std::uint64_t>(candidate_count);
    resolved_receipt.eligible_candidate_count = static_cast<std::uint64_t>(eligible_count);
    resolved_receipt.disposition = resolved.disposition;
    resolved_receipt.version = LAPLACE_COGNITION_REALIZATION_RECEIPT_VERSION;
    IdentifyReceipt(resolved_receipt, &resolved_receipt.realization_id);

    *result = resolved;
    *receipt = resolved_receipt;
    return status;
}
