#include "laplace/isa.h"
#include "laplace/trajectory.h"

#include <array>
#include <cstdint>
#include <cstdio>

int main() {
    laplace_id128 entity{};
    if (laplace_identity_codepoint(UINT32_C(0x41), &entity) !=
        LAPLACE_IDENTITY_OK) {
        return 1;
    }
    laplace_trajectory_carrier carrier{};
    if (laplace_trajectory_composition_encode(
            &entity, UINT16_C(1), UINT16_C(1), UINT64_C(0), &carrier) !=
        LAPLACE_TRAJECTORY_OK) {
        return 1;
    }
    laplace_composition_occurrence occurrence{};
    std::array<laplace_isa_value_view, 2> values{{
        {&carrier, 1u, 1u, static_cast<std::uint32_t>(sizeof(carrier)),
         LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {&occurrence, 0u, 1u, static_cast<std::uint32_t>(sizeof(occurrence)),
         LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction instruction{
        LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    laplace_isa_program program{
        &instruction,
        values.data(),
        1u,
        values.size(),
        LAPLACE_ISA_MAJOR,
        LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL,
        0u};
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};
    if (laplace_isa_execute(&program, &receipt, &error) != LAPLACE_ISA_OK ||
        receipt.executed_instruction_count != 1u || values[1].count != 1u) {
        std::fputs("isa-generated-registry-dispatch\n", stderr);
        return 2;
    }
    return 0;
}
