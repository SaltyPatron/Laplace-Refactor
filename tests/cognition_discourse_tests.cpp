#include "laplace/cognition_discourse.h"

#include "laplace/identity.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool Zero(const laplace_digest256& value) {
    return std::all_of(
        value.bytes, value.bytes + sizeof(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

laplace_cognition_semantic_act SemanticAct() {
    laplace_cognition_semantic_act act{};
    act.act_id = Digest(90U);
    act.request_fingerprint = Digest(91U);
    act.result_contract_fingerprint = Digest(92U);
    act.forward_receipt_id = Digest(93U);
    act.forward_output_fingerprint = Digest(94U);
    act.final_state_id = Digest(95U);
    act.answer_set_fingerprint = Digest(96U);
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

laplace_cognition_discourse_input LayeredInput() {
    laplace_cognition_discourse_input input{};
    input.discourse_id = Digest(1U);
    input.observation_entity_id = Codepoint(0x42U);
    input.observation_occurrence_id = Digest(2U);
    input.active_entity_set_fingerprint = Digest(3U);
    input.proposition_set_fingerprint = Digest(4U);
    input.referent_set_fingerprint = Digest(5U);
    input.unresolved_question_set_fingerprint = Digest(6U);
    input.correction_set_fingerprint = Digest(7U);
    input.ellipsis_binding_set_fingerprint = Digest(8U);
    input.cross_modal_artifact_set_fingerprint = Digest(9U);
    input.rejected_interpretation_set_fingerprint = Digest(10U);
    input.prior_program_set_fingerprint = Digest(11U);
    input.receipt_set_fingerprint = Digest(12U);
    input.world_id = Digest(13U);
    input.time_fingerprint = Digest(14U);
    input.context_fingerprint = Digest(15U);
    input.turn_ordinal = 0U;
    input.flags = LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES |
        LAPLACE_COGNITION_DISCOURSE_HAS_PROPOSITIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_REFERENTS |
        LAPLACE_COGNITION_DISCOURSE_HAS_UNRESOLVED_QUESTIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_CORRECTIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_ELLIPSIS_BINDINGS |
        LAPLACE_COGNITION_DISCOURSE_HAS_CROSS_MODAL_ARTIFACTS |
        LAPLACE_COGNITION_DISCOURSE_HAS_REJECTED_INTERPRETATIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_PRIOR_PROGRAMS |
        LAPLACE_COGNITION_DISCOURSE_HAS_RECEIPTS;
    input.version = LAPLACE_COGNITION_DISCOURSE_VERSION;
    return input;
}

TEST(CognitionDiscourse, LayeredStateRetainsCorrectionEllipsisAndCrossModalRoots) {
    const auto input = LayeredInput();
    const auto act = SemanticAct();
    laplace_cognition_discourse_state state{};
    ASSERT_EQ(
        laplace_cognition_discourse_state_create(&input, &act, &state),
        LAPLACE_COGNITION_DISCOURSE_OK);
    EXPECT_FALSE(Zero(state.state_id));
    EXPECT_TRUE(SameDigest(state.discourse_id, input.discourse_id));
    EXPECT_TRUE(SameId(state.observation_entity_id, input.observation_entity_id));
    EXPECT_TRUE(SameDigest(state.semantic_act_id, act.act_id));
    EXPECT_TRUE(SameDigest(
        state.active_entity_set_fingerprint,
        input.active_entity_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.proposition_set_fingerprint,
        input.proposition_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.referent_set_fingerprint,
        input.referent_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.unresolved_question_set_fingerprint,
        input.unresolved_question_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.correction_set_fingerprint,
        input.correction_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.ellipsis_binding_set_fingerprint,
        input.ellipsis_binding_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.cross_modal_artifact_set_fingerprint,
        input.cross_modal_artifact_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.rejected_interpretation_set_fingerprint,
        input.rejected_interpretation_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.prior_program_set_fingerprint,
        input.prior_program_set_fingerprint));
    EXPECT_TRUE(SameDigest(
        state.receipt_set_fingerprint,
        input.receipt_set_fingerprint));
    EXPECT_EQ(laplace_cognition_discourse_state_validate(&state),
              LAPLACE_COGNITION_DISCOURSE_OK);
}

TEST(CognitionDiscourse, PreviousSnapshotChangesIdentityWithoutChangingCurrentObservation) {
    auto input = LayeredInput();
    const auto act = SemanticAct();
    laplace_cognition_discourse_state first{};
    ASSERT_EQ(
        laplace_cognition_discourse_state_create(&input, &act, &first),
        LAPLACE_COGNITION_DISCOURSE_OK);

    input.previous_state_id = first.state_id;
    input.turn_ordinal = 1U;
    input.flags |= LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE;
    laplace_cognition_discourse_state second{};
    ASSERT_EQ(
        laplace_cognition_discourse_state_create(&input, &act, &second),
        LAPLACE_COGNITION_DISCOURSE_OK);

    EXPECT_TRUE(SameId(first.observation_entity_id, second.observation_entity_id));
    EXPECT_TRUE(SameDigest(second.previous_state_id, first.state_id));
    EXPECT_FALSE(SameDigest(first.state_id, second.state_id));
    EXPECT_EQ(second.turn_ordinal, 1U);
}

TEST(CognitionDiscourse, CorrectionAndRejectedInterpretationRemainIndependentIdentityInputs) {
    auto input = LayeredInput();
    const auto act = SemanticAct();
    laplace_cognition_discourse_state baseline{};
    ASSERT_EQ(
        laplace_cognition_discourse_state_create(&input, &act, &baseline),
        LAPLACE_COGNITION_DISCOURSE_OK);

    auto corrected_input = input;
    corrected_input.correction_set_fingerprint = Digest(40U);
    laplace_cognition_discourse_state corrected{};
    ASSERT_EQ(
        laplace_cognition_discourse_state_create(
            &corrected_input, &act, &corrected),
        LAPLACE_COGNITION_DISCOURSE_OK);
    EXPECT_FALSE(SameDigest(baseline.state_id, corrected.state_id));
    EXPECT_TRUE(SameDigest(
        corrected.rejected_interpretation_set_fingerprint,
        baseline.rejected_interpretation_set_fingerprint));

    auto rejected_input = input;
    rejected_input.rejected_interpretation_set_fingerprint = Digest(41U);
    laplace_cognition_discourse_state rejected{};
    ASSERT_EQ(
        laplace_cognition_discourse_state_create(
            &rejected_input, &act, &rejected),
        LAPLACE_COGNITION_DISCOURSE_OK);
    EXPECT_FALSE(SameDigest(baseline.state_id, rejected.state_id));
    EXPECT_TRUE(SameDigest(
        rejected.correction_set_fingerprint,
        baseline.correction_set_fingerprint));
    EXPECT_FALSE(SameDigest(corrected.state_id, rejected.state_id));
}

TEST(CognitionDiscourse, InactiveOptionalPayloadCannotMasqueradeAsActiveState) {
    auto input = LayeredInput();
    const auto act = SemanticAct();
    input.flags &= ~LAPLACE_COGNITION_DISCOURSE_HAS_CORRECTIONS;
    laplace_cognition_discourse_state state{};
    state.state_id = Digest(200U);
    EXPECT_EQ(
        laplace_cognition_discourse_state_create(&input, &act, &state),
        LAPLACE_COGNITION_DISCOURSE_INACTIVE_PAYLOAD);
    EXPECT_TRUE(Zero(state.state_id));
}

}  // namespace
