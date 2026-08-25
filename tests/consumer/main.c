#include <stdint.h>
#include <string.h>

#include "laplace/identity.h"
#include "laplace/isa.h"
#include "laplace/execution.h"
#include "laplace/trajectory.h"

int main(void) {
    static const uint8_t expected[LAPLACE_IDENTITY_BYTES] = {
        0x81u, 0x3eu, 0x9bu, 0x72u, 0x91u, 0x41u, 0xe7u, 0xf3u,
        0x85u, 0xafu, 0xa0u, 0xa2u, 0xd0u, 0xdfu, 0x3eu, 0x6cu
    };
    laplace_id128 identity;
    laplace_id128 batch_identity;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt receipt;
    laplace_isa_error error;
    laplace_trajectory_carrier carrier;
    laplace_composition_occurrence direct_occurrence;
    laplace_composition_occurrence isa_occurrence;
    uint64_t logical_count;
    uint64_t metadata;
    uint32_t position = UINT32_C(0x32);
    laplace_execution_topology_size topology_size;
    size_t index;
    if (laplace_identity_codepoint(position, &identity) !=
        LAPLACE_IDENTITY_OK) {
        return 2;
    }
    for (index = 0; index < LAPLACE_IDENTITY_BYTES; ++index) {
        if (identity.bytes[index] != expected[index]) {
            return 3;
        }
    }
    memset(&batch_identity, 0, sizeof(batch_identity));
    memset(values, 0, sizeof(values));
    memset(&instruction, 0, sizeof(instruction));
    memset(&program, 0, sizeof(program));
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));

    values[0].data = &position;
    values[0].count = 1;
    values[0].capacity = 1;
    values[0].stride_bytes = (uint32_t)sizeof(position);
    values[0].type = LAPLACE_ISA_VALUE_U32_VECTOR;
    values[0].flags = LAPLACE_ISA_KNOWN_VALUE_FLAGS;
    values[1].data = &batch_identity;
    values[1].capacity = 1;
    values[1].stride_bytes = (uint32_t)sizeof(batch_identity);
    values[1].type = LAPLACE_ISA_VALUE_ID128_VECTOR;
    values[1].flags = LAPLACE_ISA_KNOWN_VALUE_FLAGS;

    instruction.opcode = LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH;
    instruction.input_value = 0;
    instruction.output_value = 1;
    instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH;
    instruction.flags = LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS;

    program.instructions = &instruction;
    program.values = values;
    program.instruction_count = 1;
    program.value_count = 2;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.flags = LAPLACE_ISA_KNOWN_PROGRAM_FLAGS;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;

    if (laplace_isa_execute(&program, &receipt, &error) != LAPLACE_ISA_OK) {
        return 4;
    }
    if (values[1].count != 1 || receipt.executed_instruction_count != 1 ||
        receipt.status != LAPLACE_ISA_OK ||
        !laplace_identity_equal(&identity, &batch_identity)) {
        return 5;
    }

    metadata = (UINT64_C(2) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
               (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) |
               ((uint64_t)position << LAPLACE_TRAJECTORY_ATOM_SHIFT);
    memset(&carrier, 0, sizeof(carrier));
    memset(&direct_occurrence, 0, sizeof(direct_occurrence));
    logical_count = 0;
    if (laplace_trajectory_composition_encode(
            &identity, 1, 1, metadata, &carrier) != LAPLACE_TRAJECTORY_OK ||
        laplace_trajectory_composition_decode(
            &carrier, 1, &direct_occurrence, 1, &logical_count) !=
            LAPLACE_TRAJECTORY_OK ||
        logical_count != 1 || direct_occurrence.tier != 2 ||
        direct_occurrence.has_atom != 1 ||
        direct_occurrence.atom != position) {
        return 6;
    }

    memset(&isa_occurrence, 0, sizeof(isa_occurrence));
    memset(values, 0, sizeof(values));
    memset(&instruction, 0, sizeof(instruction));
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    values[0].data = &carrier;
    values[0].count = 1;
    values[0].capacity = 1;
    values[0].stride_bytes = (uint32_t)sizeof(carrier);
    values[0].type = LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR;
    values[0].flags = LAPLACE_ISA_KNOWN_VALUE_FLAGS;
    values[1].data = &isa_occurrence;
    values[1].capacity = 1;
    values[1].stride_bytes = (uint32_t)sizeof(isa_occurrence);
    values[1].type = LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR;
    values[1].flags = LAPLACE_ISA_KNOWN_VALUE_FLAGS;
    instruction.opcode = LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH;
    instruction.input_value = 0;
    instruction.output_value = 1;
    instruction.version =
        LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH;
    instruction.flags = LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS;
    program.instructions = &instruction;
    program.values = values;
    if (laplace_isa_execute(&program, &receipt, &error) != LAPLACE_ISA_OK ||
        values[1].count != 1 ||
        memcmp(&direct_occurrence, &isa_occurrence, sizeof(isa_occurrence)) != 0) {
        return 7;
    }
    memset(&topology_size, 0, sizeof(topology_size));
    if (laplace_execution_topology_measure_host(&topology_size) != LAPLACE_EXECUTION_OK ||
        topology_size.processor_count == 0 || topology_size.memory_domain_count == 0) {
        return 8;
    }
    return 0;
}
