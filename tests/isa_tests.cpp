#include "laplace/isa.h"
#include "laplace/trajectory.h"
#include "context_fixture.h"

#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

laplace_isa_value_view InputView(std::uint32_t* data, std::size_t count) {
    return laplace_isa_value_view{
        data,
        static_cast<std::uint64_t>(count),
        static_cast<std::uint64_t>(count),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view OutputView(laplace_id128* data, std::size_t capacity) {
    return laplace_isa_value_view{
        data,
        0u,
        static_cast<std::uint64_t>(capacity),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_ID128_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view TrajectoryInputView(
    laplace_trajectory_carrier* data,
    std::size_t count) {
    return laplace_isa_value_view{
        data,
        static_cast<std::uint64_t>(count),
        static_cast<std::uint64_t>(count),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view OccurrenceOutputView(
    laplace_composition_occurrence* data,
    std::size_t capacity) {
    return laplace_isa_value_view{
        data,
        0u,
        static_cast<std::uint64_t>(capacity),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_instruction IdentityInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return laplace_isa_instruction{
        LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH,
        input,
        output,
        LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction TrajectoryDecodeInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return laplace_isa_instruction{
        LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        input,
        output,
        LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_program Program(
    laplace_isa_instruction* instructions,
    std::size_t instruction_count,
    laplace_isa_value_view* values,
    std::size_t value_count) {
    static const laplace_framework_context context = laplace_test_context(0u);
    return laplace_isa_program{
        instructions,
        values,
        &context,
        static_cast<std::uint64_t>(instruction_count),
        static_cast<std::uint64_t>(value_count),
        LAPLACE_ISA_MAJOR,
        LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL,
        0u};
}

TEST(IsaAbi, ContractAssignmentsAreStable) {
    static_assert(LAPLACE_ISA_MAJOR == 1u);
    static_assert(LAPLACE_ISA_MINOR == 2u);
    static_assert(LAPLACE_ISA_VALUE_U32_VECTOR != LAPLACE_ISA_VALUE_ID128_VECTOR);
    static_assert(sizeof(laplace_isa_digest256) == 32u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH, 0x00020001u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH, 0x00030001u);
}

TEST(IsaContext, IsMandatoryAndBoundToProgramAndReceiptIdentity) {
    std::uint32_t position = 0x41u;
    laplace_id128 output_a{};
    laplace_id128 output_b{};
    std::array<laplace_isa_value_view, 2> values_a{{
        InputView(&position, 1u), OutputView(&output_a, 1u)}};
    std::array<laplace_isa_value_view, 2> values_b{{
        InputView(&position, 1u), OutputView(&output_b, 1u)}};
    auto instruction_a = IdentityInstruction(0u, 1u);
    auto instruction_b = IdentityInstruction(0u, 1u);
    auto program_a = Program(&instruction_a, 1u, values_a.data(), values_a.size());
    auto program_b = Program(&instruction_b, 1u, values_b.data(), values_b.size());
    const auto context_a = laplace_test_context(0u);
    const auto context_b = laplace_test_context(1u);
    program_a.context = &context_a;
    program_b.context = &context_b;
    laplace_isa_receipt receipt_a{};
    laplace_isa_receipt receipt_b{};
    laplace_isa_error error{};

    program_a.context = nullptr;
    EXPECT_EQ(laplace_isa_validate(&program_a, &error), LAPLACE_ISA_CONTEXT_INVALID);
    program_a.context = &context_a;
    ASSERT_EQ(laplace_isa_execute(&program_a, &receipt_a, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(laplace_isa_execute(&program_b, &receipt_b, &error), LAPLACE_ISA_OK);
    EXPECT_NE(std::memcmp(receipt_a.context_fingerprint.bytes,
                          receipt_b.context_fingerprint.bytes,
                          sizeof(receipt_a.context_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.program_fingerprint.bytes,
                          receipt_b.program_fingerprint.bytes,
                          sizeof(receipt_a.program_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_b.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);
}

TEST(IsaExecution, CodepointBatchMatchesCanonicalIdentity) {
    std::array<std::uint32_t, 5> positions{{0u, 0x41u, 0xd800u, 0x10000u, 0x10ffffu}};
    std::array<laplace_id128, 5> outputs{};
    std::array<laplace_isa_value_view, 2> values{{
        InputView(positions.data(), positions.size()),
        OutputView(outputs.data(), outputs.size())}};
    auto instruction = IdentityInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    EXPECT_EQ(values[1].count, positions.size());
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
    EXPECT_EQ(receipt.status, LAPLACE_ISA_OK);
    EXPECT_EQ(error.status, LAPLACE_ISA_OK);
    for (std::size_t index = 0; index < positions.size(); ++index) {
        laplace_id128 expected{};
        ASSERT_EQ(laplace_identity_codepoint(positions[index], &expected),
                  LAPLACE_IDENTITY_OK);
        EXPECT_TRUE(laplace_identity_equal(&outputs[index], &expected));
    }
}

TEST(IsaExecution, OneElementAndManyElementProgramsAgree) {
    std::array<std::uint32_t, 3> positions{{0x61u, 0x732bu, 0x1f680u}};
    std::array<laplace_id128, 3> batch_outputs{};
    std::array<laplace_isa_value_view, 2> batch_values{{
        InputView(positions.data(), positions.size()),
        OutputView(batch_outputs.data(), batch_outputs.size())}};
    auto batch_instruction = IdentityInstruction(0u, 1u);
    auto batch_program =
        Program(&batch_instruction, 1u, batch_values.data(), batch_values.size());
    laplace_isa_receipt batch_receipt{};
    laplace_isa_error batch_error{};
    ASSERT_EQ(laplace_isa_execute(&batch_program, &batch_receipt, &batch_error),
              LAPLACE_ISA_OK);

    for (std::size_t index = 0; index < positions.size(); ++index) {
        laplace_id128 single_output{};
        std::array<laplace_isa_value_view, 2> single_values{{
            InputView(&positions[index], 1u),
            OutputView(&single_output, 1u)}};
        auto single_instruction = IdentityInstruction(0u, 1u);
        auto single_program = Program(
            &single_instruction, 1u, single_values.data(), single_values.size());
        laplace_isa_receipt single_receipt{};
        laplace_isa_error single_error{};
        ASSERT_EQ(laplace_isa_execute(&single_program, &single_receipt, &single_error),
                  LAPLACE_ISA_OK);
        EXPECT_TRUE(laplace_identity_equal(&batch_outputs[index], &single_output));
    }
}

TEST(IsaExecution, TrajectoryDecodeMatchesCanonicalStructuralCalculation) {
    laplace_id128 a{};
    laplace_id128 b{};
    ASSERT_EQ(laplace_identity_codepoint(0x41u, &a), LAPLACE_IDENTITY_OK);
    ASSERT_EQ(laplace_identity_codepoint(0x42u, &b), LAPLACE_IDENTITY_OK);
    std::array<laplace_trajectory_carrier, 2> carriers{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &a, 1u, 1u, 0u, &carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &b, 2u, 3u, 0u, &carriers[1]),
              LAPLACE_TRAJECTORY_OK);
    std::array<laplace_composition_occurrence, 2> expected{};
    std::uint64_t logical_count = 0;
    ASSERT_EQ(laplace_trajectory_composition_decode(
                  carriers.data(), carriers.size(), expected.data(), expected.size(),
                  &logical_count),
              LAPLACE_TRAJECTORY_OK);

    std::array<laplace_composition_occurrence, 2> outputs{};
    std::array<laplace_isa_value_view, 2> values{{
        TrajectoryInputView(carriers.data(), carriers.size()),
        OccurrenceOutputView(outputs.data(), outputs.size())}};
    auto instruction = TrajectoryDecodeInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    EXPECT_EQ(values[1].count, carriers.size());
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
    EXPECT_EQ(std::memcmp(outputs.data(), expected.data(), sizeof(outputs)), 0);
}

TEST(IsaVersioning, MinorZeroAcceptsIdentityAndRejectsTrajectoryDecode) {
    std::uint32_t position = 0x41u;
    laplace_id128 identity{};
    std::array<laplace_isa_value_view, 2> identity_values{{
        InputView(&position, 1u),
        OutputView(&identity, 1u)}};
    auto identity_instruction = IdentityInstruction(0u, 1u);
    auto identity_program = Program(
        &identity_instruction, 1u, identity_values.data(), identity_values.size());
    identity_program.minor = 0u;
    laplace_isa_error error{};
    EXPECT_EQ(laplace_isa_validate(&identity_program, &error), LAPLACE_ISA_OK);

    laplace_trajectory_carrier carrier{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &identity, 1u, 1u, 0u, &carrier),
              LAPLACE_TRAJECTORY_OK);
    laplace_composition_occurrence occurrence{};
    std::array<laplace_isa_value_view, 2> trajectory_values{{
        TrajectoryInputView(&carrier, 1u),
        OccurrenceOutputView(&occurrence, 1u)}};
    auto trajectory_instruction = TrajectoryDecodeInstruction(0u, 1u);
    auto trajectory_program = Program(
        &trajectory_instruction, 1u, trajectory_values.data(),
        trajectory_values.size());
    trajectory_program.minor = 0u;
    EXPECT_EQ(laplace_isa_validate(&trajectory_program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

TEST(IsaValidation, InvalidLaterTrajectoryCannotPartiallyWriteResults) {
    laplace_id128 id{};
    ASSERT_EQ(laplace_identity_codepoint(0x41u, &id), LAPLACE_IDENTITY_OK);
    std::array<laplace_trajectory_carrier, 2> carriers{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &id, 1u, 1u, 0u, &carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    carriers[1] = {};
    std::array<laplace_composition_occurrence, 2> outputs{};
    std::memset(outputs.data(), 0xa5, sizeof(outputs));
    const auto before = outputs;
    std::array<laplace_isa_value_view, 2> values{{
        TrajectoryInputView(carriers.data(), carriers.size()),
        OccurrenceOutputView(outputs.data(), outputs.size())}};
    auto instruction = TrajectoryDecodeInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    EXPECT_EQ(laplace_isa_execute(&program, &receipt, &error),
              LAPLACE_ISA_INPUT_OUT_OF_RANGE);
    EXPECT_EQ(receipt.executed_instruction_count, 0u);
    EXPECT_EQ(values[1].count, 0u);
    EXPECT_EQ(std::memcmp(outputs.data(), before.data(), sizeof(outputs)), 0);
}

TEST(IsaValidation, LaterInvalidInstructionCannotPartiallyExecuteEarlierWork) {
    std::uint32_t valid_position = 0x41u;
    std::uint32_t invalid_position = 0x110000u;
    laplace_id128 first_output{};
    laplace_id128 second_output{};
    std::memset(&first_output, 0xa5, sizeof(first_output));
    std::memset(&second_output, 0x5a, sizeof(second_output));
    const auto first_before = first_output;
    const auto second_before = second_output;
    std::array<laplace_isa_value_view, 4> values{{
        InputView(&valid_position, 1u),
        OutputView(&first_output, 1u),
        InputView(&invalid_position, 1u),
        OutputView(&second_output, 1u)}};
    std::array<laplace_isa_instruction, 2> instructions{{
        IdentityInstruction(0u, 1u),
        IdentityInstruction(2u, 3u)}};
    auto program = Program(
        instructions.data(), instructions.size(), values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    EXPECT_EQ(laplace_isa_execute(&program, &receipt, &error),
              LAPLACE_ISA_INPUT_OUT_OF_RANGE);
    EXPECT_EQ(error.instruction_index, 1u);
    EXPECT_EQ(receipt.executed_instruction_count, 0u);
    EXPECT_EQ(values[1].count, 0u);
    EXPECT_EQ(values[3].count, 0u);
    EXPECT_EQ(std::memcmp(&first_output, &first_before, sizeof(first_output)), 0);
    EXPECT_EQ(std::memcmp(&second_output, &second_before, sizeof(second_output)), 0);
}

TEST(IsaValidation, RejectsUnknownOpcodeVersionTypeCapacityAndOverlap) {
    std::uint32_t position = 0x41u;
    laplace_id128 wrong_input{};
    laplace_id128 output{};
    std::array<laplace_isa_value_view, 2> values{{
        InputView(&position, 1u),
        OutputView(&output, 1u)}};
    auto instruction = IdentityInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_error error{};

    instruction.opcode += 1u;
    EXPECT_EQ(laplace_isa_validate(&program, &error), LAPLACE_ISA_UNKNOWN_OPCODE);
    instruction = IdentityInstruction(0u, 1u);
    instruction.version += 1u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
    instruction = IdentityInstruction(0u, 1u);
    values[0] = OutputView(&wrong_input, 1u);
    values[0].count = 1u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_VALUE_TYPE_MISMATCH);
    values[0] = InputView(&position, 1u);
    values[1].capacity = 0u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT);

    alignas(laplace_id128) std::array<std::uint8_t, sizeof(laplace_id128)> shared{};
    values[0] = InputView(reinterpret_cast<std::uint32_t*>(shared.data()), 1u);
    values[1] = OutputView(reinterpret_cast<laplace_id128*>(shared.data()), 1u);
    EXPECT_EQ(laplace_isa_validate(&program, &error), LAPLACE_ISA_VALUE_OVERLAP);
}

TEST(IsaReceipt, RepeatedExecutionIsDeterministicAndInputSensitive) {
    std::array<std::uint32_t, 2> positions_a{{0x41u, 0x42u}};
    std::array<std::uint32_t, 2> positions_b = positions_a;
    std::array<laplace_id128, 2> outputs_a{};
    std::array<laplace_id128, 2> outputs_b{};
    std::array<laplace_isa_value_view, 2> values_a{{
        InputView(positions_a.data(), positions_a.size()),
        OutputView(outputs_a.data(), outputs_a.size())}};
    std::array<laplace_isa_value_view, 2> values_b{{
        InputView(positions_b.data(), positions_b.size()),
        OutputView(outputs_b.data(), outputs_b.size())}};
    auto instruction_a = IdentityInstruction(0u, 1u);
    auto instruction_b = IdentityInstruction(0u, 1u);
    auto program_a = Program(&instruction_a, 1u, values_a.data(), values_a.size());
    auto program_b = Program(&instruction_b, 1u, values_b.data(), values_b.size());
    laplace_isa_receipt receipt_a{};
    laplace_isa_receipt receipt_b{};
    laplace_isa_error error_a{};
    laplace_isa_error error_b{};

    ASSERT_EQ(laplace_isa_execute(&program_a, &receipt_a, &error_a), LAPLACE_ISA_OK);
    ASSERT_EQ(laplace_isa_execute(&program_b, &receipt_b, &error_b), LAPLACE_ISA_OK);
    EXPECT_EQ(std::memcmp(&receipt_a, &receipt_b, sizeof(receipt_a)), 0);

    positions_b[1] = 0x43u;
    values_b[1].count = 0u;
    ASSERT_EQ(laplace_isa_execute(&program_b, &receipt_b, &error_b), LAPLACE_ISA_OK);
    EXPECT_NE(std::memcmp(receipt_a.input_fingerprint.bytes,
                          receipt_b.input_fingerprint.bytes,
                          sizeof(receipt_a.input_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_b.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);
}

}  // namespace
