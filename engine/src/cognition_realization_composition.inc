#include "laplace/cognition_realization.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

struct laplace_cognition_realization_composition {
    laplace_composition_working_set* working_set{};
};

namespace laplace_cognition_realization_composition_detail {

constexpr char RequestDomain[] = "laplace-cognition-realization-request-v1";
constexpr char PlanDomain[] = "laplace-cognition-realization-composition-plan-v1";
constexpr char CandidateDomain[] = "laplace-cognition-realization-composed-candidate-v1";
constexpr char ReceiptDomain[] = "laplace-cognition-realization-composition-receipt-v1";

bool BytesZero(const std::uint8_t* bytes, const std::size_t count) {
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

void HashU32(blake3_hasher* hasher, const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashId(blake3_hasher* hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashDigest(blake3_hasher* hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* hasher, laplace_digest256* output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool SemanticActValid(const laplace_cognition_semantic_act& act) {
    return act.version == LAPLACE_COGNITION_SEMANTIC_ACT_VERSION &&
        (act.flags & ~LAPLACE_COGNITION_SEMANTIC_ACT_KNOWN_FLAGS) == 0U &&
        (act.flags & LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT) != 0U &&
        (act.flags & LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT) != 0U &&
        !DigestZero(act.act_id) && act.answer_count != 0U &&
        !IdZero(act.primary_answer.entity_id);
}

bool RequestValid(const laplace_cognition_realization_request& request) {
    if (request.version != LAPLACE_COGNITION_REALIZATION_VERSION ||
        request.maximum_candidates == 0U || IdZero(request.modality_id) ||
        DigestZero(request.evidence_epoch) ||
        DigestZero(request.realization_recipe_epoch) ||
        DigestZero(request.context_fingerprint) ||
        (request.flags & ~LAPLACE_COGNITION_REALIZATION_KNOWN_REQUEST_FLAGS) != 0U) {
        return false;
    }
    const bool language_present =
        (request.flags & LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT) != 0U;
    const bool register_present =
        (request.flags & LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT) != 0U;
    return language_present != IdZero(request.language_id) &&
        register_present != IdZero(request.register_id);
}

void RequestFingerprint(
    const laplace_cognition_semantic_act& act,
    const laplace_cognition_realization_request& request,
    laplace_digest256* output) {
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

void PlanFingerprint(
    const laplace_cognition_semantic_act& act,
    const laplace_cognition_realization_request& request,
    const laplace_cognition_realization_composition_input& input,
    laplace_digest256* output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, PlanDomain, sizeof(PlanDomain) - 1U);
    HashDigest(&hasher, act.act_id);
    HashDigest(&hasher, request.realization_recipe_epoch);
    HashDigest(&hasher, *input.source_fingerprint);
    HashDigest(&hasher, *input.calculation_recipe_fingerprint);
    HashDigest(&hasher, input.geometry_epoch);
    HashDigest(&hasher, request.context_fingerprint);
    HashU64(&hasher, input.source_ordinal);
    HashU64(&hasher, input.constituent_count);
    for (std::uint64_t index = 0U; index < input.constituent_count; ++index) {
        const auto& constituent = input.constituents[static_cast<std::size_t>(index)];
        HashId(&hasher, constituent.entity_id);
        HashDigest(&hasher, constituent.identity_witness);
        HashDigest(&hasher, constituent.physicality_id);
        HashU32(&hasher, constituent.atom);
        HashU32(&hasher, constituent.tier_floor);
        HashU32(&hasher, constituent.has_atom);
        HashU64(
            &hasher,
            input.relationship_metadata == nullptr
                ? 0U
                : input.relationship_metadata[static_cast<std::size_t>(index)]);
    }
    Finish(&hasher, output);
}

void CandidateReceipt(
    const laplace_digest256& request_fingerprint,
    const laplace_digest256& plan_fingerprint,
    const laplace_composition_working_set_summary& summary,
    const laplace_composition_result& result,
    const std::uint64_t generated_count,
    laplace_digest256* output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, CandidateDomain, sizeof(CandidateDomain) - 1U);
    HashDigest(&hasher, request_fingerprint);
    HashDigest(&hasher, plan_fingerprint);
    HashDigest(&hasher, summary.receipt_id);
    HashDigest(&hasher, summary.presence_receipt_id);
    HashDigest(&hasher, summary.stream_fingerprint);
    HashId(&hasher, result.entity_id);
    HashDigest(&hasher, result.identity_witness);
    HashDigest(&hasher, result.physicality_id);
    HashU64(&hasher, result.logical_count);
    HashU32(&hasher, result.tier_floor);
    HashU64(&hasher, generated_count);
    Finish(&hasher, output);
}

void ReceiptId(
    const laplace_cognition_realization_composition_receipt& receipt,
    laplace_digest256* output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ReceiptDomain, sizeof(ReceiptDomain) - 1U);
    HashDigest(&hasher, receipt.semantic_act_id);
    HashDigest(&hasher, receipt.request_fingerprint);
    HashDigest(&hasher, receipt.plan_fingerprint);
    HashDigest(&hasher, receipt.working_set_receipt_id);
    HashDigest(&hasher, receipt.presence_receipt_id);
    HashDigest(&hasher, receipt.stream_fingerprint);
    HashId(&hasher, receipt.content_id);
    HashU64(&hasher, receipt.constituent_count);
    HashU64(&hasher, receipt.reused_subtree_count);
    HashU64(&hasher, receipt.generated_composition_count);
    HashU32(&hasher, receipt.structural_tier);
    HashU32(&hasher, receipt.flags);
    Finish(&hasher, output);
}

laplace_cognition_realization_composition_status MapCreateStatus(
    const laplace_composition_status status) {
    if (status == LAPLACE_COMPOSITION_MEMORY_FAILURE) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_MEMORY_FAILURE;
    }
    if (status == LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT ||
        status == LAPLACE_COMPOSITION_COUNT_OVERFLOW ||
        status == LAPLACE_COMPOSITION_TIER_OVERFLOW) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_RANGE;
    }
    return LAPLACE_COGNITION_REALIZATION_COMPOSITION_COMPOSITION_FAILURE;
}

}  // namespace laplace_cognition_realization_composition_detail

extern "C" laplace_cognition_realization_composition_status
laplace_cognition_realization_composition_create(
    const laplace_cognition_semantic_act* const semantic_act,
    const laplace_cognition_realization_request* const request,
    const laplace_cognition_realization_composition_input* const input,
    const laplace_composition_presence_provider_v1* const presence_provider,
    laplace_cognition_realization_composition** const composition,
    laplace_cognition_realization_candidate* const candidate,
    laplace_cognition_realization_composition_receipt* const receipt) {
    using namespace laplace_cognition_realization_composition_detail;

    if (composition != nullptr) *composition = nullptr;
    if (candidate != nullptr) *candidate = laplace_cognition_realization_candidate{};
    if (receipt != nullptr) *receipt = laplace_cognition_realization_composition_receipt{};
    if (semantic_act == nullptr || request == nullptr || input == nullptr ||
        presence_provider == nullptr || composition == nullptr || candidate == nullptr ||
        receipt == nullptr || input->framework_context == nullptr ||
        input->source_fingerprint == nullptr ||
        input->calculation_recipe_fingerprint == nullptr ||
        input->constituents == nullptr || input->constituent_count < 2U ||
        input->preferred_batch_bytes == 0U || input->flags != 0U ||
        input->reserved != 0U || presence_provider->resolve == nullptr) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_INVALID_ARGUMENT;
    }
    if (!SemanticActValid(*semantic_act)) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_INVALID_SEMANTIC_ACT;
    }
    if (!RequestValid(*request)) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_INVALID_REQUEST;
    }
    if (input->constituent_count > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_RANGE;
    }

    const std::size_t constituent_count =
        static_cast<std::size_t>(input->constituent_count);
    std::vector<laplace_composition_operand> operands;
    try {
        operands.resize(constituent_count);
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_MEMORY_FAILURE;
    }
    for (std::size_t index = 0U; index < constituent_count; ++index) {
        operands[index].reference_index = static_cast<std::uint64_t>(index);
        operands[index].multiplicity = 1U;
        operands[index].relationship_metadata =
            input->relationship_metadata == nullptr
                ? 0U
                : input->relationship_metadata[index];
        operands[index].reference_kind = LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        operands[index].flags = 0U;
    }

    laplace_composition_request composition_request{};
    composition_request.first_operand = 0U;
    composition_request.operand_count = input->constituent_count;
    composition_request.source_ordinal = input->source_ordinal;
    composition_request.recipe_version = LAPLACE_COGNITION_REALIZATION_COMPOSITION_VERSION;
    composition_request.flags = 0U;
    composition_request.recipe_fingerprint = request->realization_recipe_epoch;
    composition_request.geometry_epoch = input->geometry_epoch;
    composition_request.occurrence_context_fingerprint = request->context_fingerprint;

    laplace_composition_working_set_input working_input{};
    working_input.context = input->framework_context;
    working_input.source_fingerprint = input->source_fingerprint;
    working_input.calculation_recipe_fingerprint = input->calculation_recipe_fingerprint;
    working_input.known_entities = input->constituents;
    working_input.known_entity_count = input->constituent_count;
    working_input.operands = operands.data();
    working_input.operand_count = input->constituent_count;
    working_input.requests = &composition_request;
    working_input.request_count = 1U;
    working_input.preferred_batch_bytes = input->preferred_batch_bytes;

    laplace_composition_working_set* working_set = nullptr;
    const auto create_status =
        laplace_composition_working_set_create(&working_input, &working_set);
    if (create_status != LAPLACE_COMPOSITION_OK || working_set == nullptr) {
        laplace_composition_working_set_destroy(&working_set);
        return MapCreateStatus(create_status);
    }

    laplace_composition_presence_receipt presence{};
    const auto presence_status = laplace_composition_working_set_resolve_presence(
        working_set, presence_provider, &presence);
    if (presence_status != LAPLACE_COMPOSITION_OK) {
        laplace_composition_working_set_destroy(&working_set);
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_PRESENCE_FAILURE;
    }

    laplace_composition_working_set_summary summary{};
    if (laplace_composition_working_set_summary_get(working_set, &summary) !=
            LAPLACE_COMPOSITION_OK ||
        summary.status != LAPLACE_COMPOSITION_OK ||
        summary.request_count != 1U ||
        summary.known_entity_count != input->constituent_count ||
        summary.presence_applied == 0U ||
        summary.novel_entity_count > 1U) {
        laplace_composition_working_set_destroy(&working_set);
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_RESULT_INVALID;
    }

    std::size_t result_count = 0U;
    const laplace_composition_result* results =
        laplace_composition_working_set_results(working_set, &result_count);
    if (results == nullptr || result_count != 1U ||
        IdZero(results[0].entity_id) || DigestZero(results[0].identity_witness) ||
        DigestZero(results[0].physicality_id)) {
        laplace_composition_working_set_destroy(&working_set);
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_RESULT_INVALID;
    }

    laplace_digest256 request_fingerprint{};
    laplace_digest256 plan_fingerprint{};
    RequestFingerprint(*semantic_act, *request, &request_fingerprint);
    PlanFingerprint(*semantic_act, *request, *input, &plan_fingerprint);

    const std::uint64_t generated_count = summary.novel_entity_count == 0U ? 0U : 1U;
    candidate->content_id = results[0].entity_id;
    if ((request->flags & LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT) != 0U) {
        candidate->language_id = request->language_id;
    }
    candidate->realization_recipe_id = request->realization_recipe_epoch;
    candidate->obligation_fingerprint = semantic_act->act_id;
    candidate->preference_rank = 0U;
    candidate->reused_subtree_count = input->constituent_count;
    candidate->generated_composition_count = generated_count;
    candidate->structural_tier = results[0].tier_floor;
    candidate->match_class = LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_COMPOSED;
    candidate->missing_obligation_count = 0U;
    candidate->flags = generated_count == 0U
        ? LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED
        : LAPLACE_COGNITION_REALIZATION_CANDIDATE_NEW_COMPOSITION;
    CandidateReceipt(
        request_fingerprint, plan_fingerprint, summary, results[0], generated_count,
        &candidate->candidate_receipt_id);

    receipt->semantic_act_id = semantic_act->act_id;
    receipt->request_fingerprint = request_fingerprint;
    receipt->plan_fingerprint = plan_fingerprint;
    receipt->working_set_receipt_id = summary.receipt_id;
    receipt->presence_receipt_id = summary.presence_receipt_id;
    receipt->stream_fingerprint = summary.stream_fingerprint;
    receipt->content_id = results[0].entity_id;
    receipt->constituent_count = input->constituent_count;
    receipt->reused_subtree_count = candidate->reused_subtree_count;
    receipt->generated_composition_count = candidate->generated_composition_count;
    receipt->structural_tier = candidate->structural_tier;
    receipt->flags = candidate->flags;
    receipt->version = LAPLACE_COGNITION_REALIZATION_COMPOSITION_VERSION;
    ReceiptId(*receipt, &receipt->composition_id);

    auto* created = new (std::nothrow) laplace_cognition_realization_composition{};
    if (created == nullptr) {
        laplace_composition_working_set_destroy(&working_set);
        *candidate = laplace_cognition_realization_candidate{};
        *receipt = laplace_cognition_realization_composition_receipt{};
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_MEMORY_FAILURE;
    }
    created->working_set = working_set;
    *composition = created;
    return LAPLACE_COGNITION_REALIZATION_COMPOSITION_OK;
}

extern "C" laplace_cognition_realization_composition_status
laplace_cognition_realization_composition_producer(
    laplace_cognition_realization_composition* const composition,
    laplace_framework_producer_v1* const producer) {
    if (producer != nullptr) *producer = laplace_framework_producer_v1{};
    if (composition == nullptr || composition->working_set == nullptr || producer == nullptr) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_INVALID_ARGUMENT;
    }
    const auto status =
        laplace_composition_working_set_producer(composition->working_set, producer);
    if (status == LAPLACE_COMPOSITION_OK) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_OK;
    }
    if (status == LAPLACE_COMPOSITION_MEMORY_FAILURE) {
        return LAPLACE_COGNITION_REALIZATION_COMPOSITION_MEMORY_FAILURE;
    }
    return LAPLACE_COGNITION_REALIZATION_COMPOSITION_PRODUCER_FAILURE;
}

extern "C" void laplace_cognition_realization_composition_destroy(
    laplace_cognition_realization_composition** const composition) {
    if (composition == nullptr || *composition == nullptr) return;
    laplace_composition_working_set_destroy(&(*composition)->working_set);
    delete *composition;
    *composition = nullptr;
}
