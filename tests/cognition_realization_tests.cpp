#include "laplace/cognition_realization.h"

#include "laplace/identity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t i = 0U; i < sizeof(value.bytes); ++i) {
        value.bytes[i] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) + static_cast<unsigned int>(i) + 1U);
    }
    return value;
}

bool ZeroDigest(const laplace_digest256& value) {
    return std::all_of(value.bytes, value.bytes + sizeof(value.bytes),
                       [](const std::uint8_t b) { return b == 0U; });
}

bool SameDigest(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

bool SameId(const laplace_id128& a, const laplace_id128& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 id{};
    laplace_digest256 witness{};
    EXPECT_EQ(laplace_identity_codepoint_witness(codepoint, &id, &witness),
              LAPLACE_IDENTITY_OK);
    return id;
}

laplace_cognition_semantic_act Act() {
    laplace_cognition_semantic_act act{};
    act.act_id = Digest(1U);
    act.request_fingerprint = Digest(2U);
    act.result_contract_fingerprint = Digest(3U);
    act.forward_receipt_id = Digest(4U);
    act.forward_output_fingerprint = Digest(5U);
    act.final_state_id = Digest(6U);
    act.answer_set_fingerprint = Digest(7U);
    act.primary_answer.entity_id = Codepoint(0x41U);
    act.answer_count = 1U;
    act.act_kind = LAPLACE_COGNITION_OPERATION_ANSWER;
    act.producer_operation_kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    act.flags = LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_PRODUCER_OPERATION_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT;
    act.version = LAPLACE_COGNITION_SEMANTIC_ACT_VERSION;
    return act;
}

laplace_cognition_realization_request Request() {
    laplace_cognition_realization_request request{};
    request.modality_id = Codepoint(0x54U);
    request.language_id = Codepoint(0x4AU);
    request.evidence_epoch = Digest(20U);
    request.realization_recipe_epoch = Digest(21U);
    request.context_fingerprint = Digest(22U);
    request.maximum_candidates = 8U;
    request.flags = LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT;
    request.version = LAPLACE_COGNITION_REALIZATION_VERSION;
    return request;
}

laplace_cognition_realization_candidate Candidate(
    const std::uint32_t output_codepoint,
    const std::uint32_t match_class,
    const std::uint64_t preference_rank,
    const std::uint32_t missing,
    const std::uint8_t seed) {
    laplace_cognition_realization_candidate candidate{};
    candidate.content_id = Codepoint(output_codepoint);
    candidate.language_id = Codepoint(0x4AU);
    candidate.candidate_receipt_id = Digest(seed);
    candidate.realization_recipe_id = Digest(static_cast<std::uint8_t>(seed + 1U));
    candidate.obligation_fingerprint = Digest(static_cast<std::uint8_t>(seed + 2U));
    candidate.preference_rank = preference_rank;
    candidate.reused_subtree_count = match_class == LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE ? 1U : 0U;
    candidate.generated_composition_count = match_class == LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_COMPOSED ? 1U : 0U;
    candidate.structural_tier = match_class == LAPLACE_COGNITION_REALIZATION_CANDIDATE_STRUCTURAL_FALLBACK ? 1U : 3U;
    candidate.match_class = match_class;
    candidate.missing_obligation_count = missing;
    candidate.flags = match_class == LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_COMPOSED
        ? LAPLACE_COGNITION_REALIZATION_CANDIDATE_NEW_COMPOSITION
        : LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    return candidate;
}

struct ProviderState final {
    std::vector<laplace_cognition_realization_candidate> candidates;
    laplace_cognition_realization_usage usage{};
    int return_code{};
};

int Enumerate(
    void* opaque,
    const laplace_cognition_semantic_act* semantic_act,
    const laplace_cognition_realization_request* request,
    laplace_cognition_realization_candidate* candidates,
    const std::size_t candidate_capacity,
    std::size_t* candidate_count,
    laplace_cognition_realization_usage* usage) {
    if (opaque == nullptr || semantic_act == nullptr || request == nullptr ||
        candidates == nullptr || candidate_count == nullptr || usage == nullptr) {
        return 99;
    }
    auto& state = *static_cast<ProviderState*>(opaque);
    if (state.return_code != 0) return state.return_code;
    if (state.candidates.size() > candidate_capacity) return 98;
    std::copy(state.candidates.begin(), state.candidates.end(), candidates);
    *candidate_count = state.candidates.size();
    *usage = state.usage;
    return 0;
}

laplace_cognition_realization_provider_v1 Provider(ProviderState* state) {
    laplace_cognition_realization_provider_v1 provider{};
    provider.state = state;
    provider.provider_fingerprint = Digest(80U);
    provider.maximum_candidate_records = 8U;
    provider.enumerate = Enumerate;
    provider.abi_major = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

laplace_cognition_realization_usage CompleteUsage() {
    laplace_cognition_realization_usage usage{};
    usage.provider_receipt_id = Digest(90U);
    usage.disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    return usage;
}

TEST(CognitionRealization, ExactWholeReusePrecedesSmallerStructuralFallback) {
    auto act = Act();
    auto request = Request();
    ProviderState state{};
    state.candidates = {
        Candidate(0x43U, LAPLACE_COGNITION_REALIZATION_CANDIDATE_STRUCTURAL_FALLBACK, 0U, 0U, 30U),
        Candidate(0x42U, LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE, 100U, 0U, 40U)};
    state.usage = CompleteUsage();
    auto provider = Provider(&state);
    laplace_cognition_realization_result result{};
    laplace_cognition_realization_receipt receipt{};

    ASSERT_EQ(laplace_cognition_semantic_act_realize(
                  &act, &request, &provider, &result, &receipt),
              LAPLACE_COGNITION_REALIZATION_OK);
    EXPECT_TRUE(SameId(result.content_id, Codepoint(0x42U)));
    EXPECT_EQ(result.match_class, LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE);
    EXPECT_EQ(result.disposition, LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE);
    EXPECT_EQ(receipt.candidate_count, 2U);
    EXPECT_EQ(receipt.eligible_candidate_count, 2U);
    EXPECT_FALSE(ZeroDigest(receipt.realization_id));
    EXPECT_TRUE(SameDigest(receipt.semantic_act_id, act.act_id));
}

TEST(CognitionRealization, IncompleteLargerCandidateFallsBackWithoutDroppingObligations) {
    auto act = Act();
    auto request = Request();
    ProviderState state{};
    state.candidates = {
        Candidate(0x42U, LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE, 0U, 2U, 30U),
        Candidate(0x43U, LAPLACE_COGNITION_REALIZATION_CANDIDATE_STRUCTURAL_FALLBACK, 5U, 0U, 40U)};
    state.usage = CompleteUsage();
    auto provider = Provider(&state);
    laplace_cognition_realization_result result{};
    laplace_cognition_realization_receipt receipt{};

    ASSERT_EQ(laplace_cognition_semantic_act_realize(
                  &act, &request, &provider, &result, &receipt),
              LAPLACE_COGNITION_REALIZATION_OK);
    EXPECT_TRUE(SameId(result.content_id, Codepoint(0x43U)));
    EXPECT_EQ(result.match_class,
              LAPLACE_COGNITION_REALIZATION_CANDIDATE_STRUCTURAL_FALLBACK);
    EXPECT_EQ(receipt.eligible_candidate_count, 1U);
}

TEST(CognitionRealization, MissingLanguageReadinessReturnsTypedWhyNotWithoutOutput) {
    auto act = Act();
    auto request = Request();
    ProviderState state{};
    state.usage.provider_receipt_id = Digest(90U);
    state.usage.missing_obligation_fingerprint = Digest(91U);
    state.usage.missing_obligation_count = 3U;
    state.usage.disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED;
    auto provider = Provider(&state);
    laplace_cognition_realization_result result{};
    laplace_cognition_realization_receipt receipt{};

    ASSERT_EQ(laplace_cognition_semantic_act_realize(
                  &act, &request, &provider, &result, &receipt),
              LAPLACE_COGNITION_REALIZATION_UNSUPPORTED);
    EXPECT_EQ(result.disposition,
              LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED);
    EXPECT_EQ(result.missing_obligation_count, 3U);
    EXPECT_TRUE(SameDigest(result.missing_obligation_fingerprint,
                           state.usage.missing_obligation_fingerprint));
    laplace_id128 zero{};
    EXPECT_TRUE(SameId(result.content_id, zero));
    EXPECT_FALSE(ZeroDigest(receipt.realization_id));
}

TEST(CognitionRealization, EqualBestDistinctOutputsRemainAmbiguous) {
    auto act = Act();
    auto request = Request();
    ProviderState state{};
    state.candidates = {
        Candidate(0x42U, LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_COMPOSED, 7U, 0U, 30U),
        Candidate(0x43U, LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_COMPOSED, 7U, 0U, 40U)};
    state.usage = CompleteUsage();
    auto provider = Provider(&state);
    laplace_cognition_realization_result result{};
    laplace_cognition_realization_receipt receipt{};

    ASSERT_EQ(laplace_cognition_semantic_act_realize(
                  &act, &request, &provider, &result, &receipt),
              LAPLACE_COGNITION_REALIZATION_AMBIGUOUS);
    EXPECT_EQ(result.disposition,
              LAPLACE_COGNITION_REALIZATION_DISPOSITION_AMBIGUOUS);
    laplace_id128 zero{};
    EXPECT_TRUE(SameId(result.content_id, zero));
    EXPECT_EQ(receipt.eligible_candidate_count, 2U);
}

TEST(CognitionRealization, ProviderCannotSilentlySubstituteAnotherLanguage) {
    auto act = Act();
    auto request = Request();
    ProviderState state{};
    auto wrong_language = Candidate(
        0x42U, LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE, 0U, 0U, 30U);
    wrong_language.language_id = Codepoint(0x45U);
    state.candidates = {wrong_language};
    state.usage = CompleteUsage();
    auto provider = Provider(&state);
    laplace_cognition_realization_result result{};
    laplace_cognition_realization_receipt receipt{};

    EXPECT_EQ(laplace_cognition_semantic_act_realize(
                  &act, &request, &provider, &result, &receipt),
              LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE);
    EXPECT_TRUE(ZeroDigest(receipt.realization_id));
}

}  // namespace
