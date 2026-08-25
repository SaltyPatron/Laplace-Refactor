#include "laplace/isa.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace {

laplace_isa_value_view Input(std::uint32_t* data) {
    return laplace_isa_value_view{
        data,
        1u,
        1u,
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view Output(laplace_id128* data) {
    return laplace_isa_value_view{
        data,
        0u,
        1u,
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_ID128_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_instruction Instruction(std::uint32_t input, std::uint32_t output) {
    return laplace_isa_instruction{
        LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH,
        input,
        output,
        LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

}  // namespace

int main() {
    std::uint32_t valid_position = 0x41u;
    std::uint32_t invalid_position = 0x110000u;
    laplace_id128 first_output{};
    laplace_id128 second_output{};
    std::memset(&first_output, 0xa5, sizeof(first_output));
    std::memset(&second_output, 0x5a, sizeof(second_output));
    const auto first_before = first_output;
    const auto second_before = second_output;
    std::array<laplace_isa_value_view, 4> values{{
        Input(&valid_position),
        Output(&first_output),
        Input(&invalid_position),
        Output(&second_output)}};
    std::array<laplace_isa_instruction, 2> instructions{{
        Instruction(0u, 1u),
        Instruction(2u, 3u)}};
    laplace_isa_program program{
        instructions.data(),
        values.data(),
        instructions.size(),
        values.size(),
        LAPLACE_ISA_MAJOR,
        LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL,
        0u};
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    const auto status = laplace_isa_execute(&program, &receipt, &error);
    if (status != LAPLACE_ISA_INPUT_OUT_OF_RANGE ||
        receipt.executed_instruction_count != 0u ||
        values[1].count != 0u || values[3].count != 0u ||
        std::memcmp(&first_output, &first_before, sizeof(first_output)) != 0 ||
        std::memcmp(&second_output, &second_before, sizeof(second_output)) != 0) {
        std::fputs("isa-partial-preflight-write\n", stderr);
        return 2;
    }
    return 0;
}
