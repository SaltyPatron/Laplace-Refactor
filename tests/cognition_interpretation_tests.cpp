#include "laplace/cognition_interpretation.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>

namespace {
laplace_digest256 Digest(uint8_t value) { laplace_digest256 d{}; d.bytes[0] = value; return d; }
laplace_id128 Id(uint8_t value) { laplace_id128 id{}; id.bytes[0] = value; return id; }

struct InterpretationFixture {
    std::array<laplace_digest256, 2> slots{Digest(1), Digest(2)};
    std::array<uint32_t, 1> first_slots{0};
    std::array<uint32_t, 2> second_slots{0, 1};
    std::array<laplace_id128, 1> first{Id(10)}, second{Id(20)};
    std::array<laplace_id128, 2> joined{Id(20), Id(30)};
    std::array<laplace_cognition_interpretation_row, 2> alternatives{{
        {first.data(), Digest(11), Digest(12), {}},
        {second.data(), Digest(21), Digest(22), {}}}};
    laplace_cognition_interpretation_row constraint{joined.data(), Digest(31), {}, Digest(32)};
    std::array<laplace_cognition_interpretation_factor, 2> factors{{
        {Digest(40), Digest(41), first_slots.data(), 1, alternatives.data(), 2},
        {Digest(50), Digest(51), second_slots.data(), 2, &constraint, 1}}};
    laplace_cognition_interpretation_program program{};
    laplace_cognition_interpretation_result* result{};
    laplace_cognition_interpretation_receipt receipt{};
    InterpretationFixture() {
        program.observation_root = Id(1);
        program.occurrence_id = Digest(2);
        program.world_id = Digest(3);
        program.time_fingerprint = Digest(4);
        program.context_id = Digest(5);
        program.evidence_epoch = Digest(6);
        program.boundary_id = Digest(7);
        program.authority_id = Digest(8);
        program.slot_ids = slots.data();
        program.slot_count = slots.size();
        program.maximum_comparisons = 1000;
        program.maximum_states = 100;
        program.maximum_memory_bytes = 1048576;
        program.version = LAPLACE_COGNITION_INTERPRETATION_VERSION;
    }
    ~InterpretationFixture() { laplace_cognition_interpretation_destroy(&result); }
    auto Execute() {
        return laplace_cognition_interpretation_execute(&program, factors.data(), factors.size(),
            &result, &receipt);
    }
};

TEST(CognitionInterpretation, JointBindingRetainsRejectedWitnessesAndOwnsItsInput) {
    InterpretationFixture f;
    ASSERT_EQ(f.Execute(), LAPLACE_COGNITION_INTERPRETATION_OK);
    ASSERT_EQ(f.receipt.disposition, LAPLACE_COGNITION_INTERPRETATION_UNIQUE);
    ASSERT_EQ(f.receipt.derivation_count, 1U);
    std::array<laplace_id128, 2> values{};
    std::array<uint64_t, 2> rows{};
    ASSERT_EQ(laplace_cognition_interpretation_read(f.result, 0, values.data(), 2,
        rows.data(), 2), LAPLACE_COGNITION_INTERPRETATION_OK);
    EXPECT_EQ(values[0].bytes[0], 20U);
    EXPECT_EQ(values[1].bytes[0], 30U);
    EXPECT_EQ(rows[0], 1U);
    EXPECT_EQ(rows[1], 0U);
    laplace_cognition_interpretation_witness witness{};
    ASSERT_EQ(laplace_cognition_interpretation_read_witness(f.result, 0, 0, &witness),
        LAPLACE_COGNITION_INTERPRETATION_OK);
    EXPECT_EQ(witness.observation_id.bytes[0], 11U);
    f.joined[1] = Id(99);
    laplace_id128 binding{};
    ASSERT_EQ(laplace_cognition_interpretation_read_binding(f.result, &f.slots[1], &binding),
        LAPLACE_COGNITION_INTERPRETATION_OK);
    EXPECT_EQ(binding.bytes[0], 30U);
}

TEST(CognitionInterpretation, EqualBindingsRetainDistinctDerivations) {
    InterpretationFixture f;
    f.first[0] = f.second[0];
    ASSERT_EQ(f.Execute(), LAPLACE_COGNITION_INTERPRETATION_OK);
    EXPECT_EQ(f.receipt.disposition, LAPLACE_COGNITION_INTERPRETATION_UNIQUE);
    ASSERT_EQ(f.receipt.derivation_count, 2U);
    std::array<laplace_id128, 2> values{};
    std::array<uint64_t, 2> left{}, right{};
    ASSERT_EQ(laplace_cognition_interpretation_read(f.result, 0, values.data(), 2,
        left.data(), 2), LAPLACE_COGNITION_INTERPRETATION_OK);
    ASSERT_EQ(laplace_cognition_interpretation_read(f.result, 1, values.data(), 2,
        right.data(), 2), LAPLACE_COGNITION_INTERPRETATION_OK);
    EXPECT_NE(left[0], right[0]);
}

TEST(CognitionInterpretation, AmbiguityCannotBecomeAnElectedBinding) {
    InterpretationFixture f;
    f.second_slots[0] = 1;
    f.factors[1].slot_count = 1;
    ASSERT_EQ(f.Execute(), LAPLACE_COGNITION_INTERPRETATION_OK);
    EXPECT_EQ(f.receipt.disposition, LAPLACE_COGNITION_INTERPRETATION_AMBIGUOUS);
    laplace_id128 binding{};
    EXPECT_EQ(laplace_cognition_interpretation_read_binding(f.result, &f.slots[0], &binding),
        LAPLACE_COGNITION_INTERPRETATION_NOT_UNIQUE);
}

TEST(CognitionInterpretation, ResourceExhaustionPublishesNoPartialInterpretation) {
    for (const bool memory : {false, true}) {
        InterpretationFixture f;
        if (memory) f.program.maximum_memory_bytes = 1;
        else f.program.maximum_states = 1;
        EXPECT_EQ(f.Execute(), LAPLACE_COGNITION_INTERPRETATION_EXHAUSTED);
        EXPECT_EQ(f.result, nullptr);
        EXPECT_EQ(f.receipt.disposition, LAPLACE_COGNITION_INTERPRETATION_UNRESOLVED);
    }
}

TEST(CognitionInterpretation, CompiledGuidanceRetainsEveryOpenTaskObligation) {
    InterpretationFixture f;
    ASSERT_EQ(f.Execute(), LAPLACE_COGNITION_INTERPRETATION_OK);
    std::array<laplace_cognition_interpretation_obligation_rule, 2> obligations{};
    for (size_t i = 0; i < obligations.size(); ++i) {
        obligations[i].law_id = Digest(static_cast<uint8_t>(60 + i));
        obligations[i].result_contract_fingerprint = Digest(65);
        obligations[i].operand_slots = &f.slots[i];
        obligations[i].operand_count = 1;
        obligations[i].kind = static_cast<uint32_t>(i + 1);
        obligations[i].flags = LAPLACE_COGNITION_OBLIGATION_REQUIRED;
    }
    laplace_cognition_interpretation_program_rule rule{};
    rule.program_entity_id = f.joined[0];
    rule.program_slot_id = f.slots[0];
    rule.goal_slot_id = f.slots[1];
    rule.recipe_id = Digest(70);
    rule.witness_id = Digest(71);
    rule.result_contract_fingerprint = Digest(65);
    rule.obligations = obligations.data();
    rule.obligation_count = obligations.size();
    rule.version = LAPLACE_COGNITION_INTERPRETATION_VERSION;
    laplace_cognition_guidance_state* guidance{};
    ASSERT_EQ(laplace_cognition_interpretation_compile_guidance(f.result, &rule, &guidance),
        LAPLACE_COGNITION_INTERPRETATION_OK);
    EXPECT_EQ(laplace_cognition_guidance_state_obligation_count(guidance), 2U);
    for (size_t i = 0; i < obligations.size(); ++i) {
        laplace_cognition_obligation obligation{};
        ASSERT_EQ(laplace_cognition_guidance_state_obligation(guidance, i, &obligation),
            LAPLACE_COGNITION_GUIDANCE_OK);
        EXPECT_EQ(obligation.disposition, LAPLACE_COGNITION_OBLIGATION_OPEN);
    }
    laplace_cognition_guidance_state_destroy(&guidance);
    rule.program_entity_id = Id(99);
    EXPECT_EQ(laplace_cognition_interpretation_compile_guidance(f.result, &rule, &guidance),
        LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH);
    EXPECT_EQ(guidance, nullptr);
}
} // namespace
