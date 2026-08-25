#include "laplace/isa.h"
#include "laplace/trajectory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::uint8_t HexNibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    return static_cast<std::uint8_t>(value - 'a' + 10);
}

laplace_id128 ParseId(std::string_view hex) {
    laplace_id128 id{};
    for (std::size_t index = 0; index < sizeof(id.bytes); ++index) {
        id.bytes[index] = static_cast<std::uint8_t>(
            (HexNibble(hex[index * 2]) << 4) | HexNibble(hex[index * 2 + 1]));
    }
    return id;
}

template <typename Bytes>
void PrintHex(const char* name, const Bytes& bytes) {
    std::cout << name << '=';
    for (const auto byte : bytes) {
        std::cout << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<unsigned int>(byte);
    }
    std::cout << std::dec << '\n';
}

void PrintDigest(const char* name, const laplace_isa_digest256& digest) {
    PrintHex(name, digest.bytes);
}

laplace_isa_program Program(
    laplace_isa_instruction* instruction,
    laplace_isa_value_view* values) {
    return laplace_isa_program{
        instruction,
        values,
        1u,
        2u,
        LAPLACE_ISA_MAJOR,
        LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL,
        0u};
}

void PrintReceipt(const char* prefix, const laplace_isa_receipt& receipt) {
    const std::string prefix_value(prefix);
    PrintDigest((prefix_value + "_RECEIPT").c_str(), receipt.receipt_id);
    PrintDigest((prefix_value + "_PROGRAM").c_str(), receipt.program_fingerprint);
    PrintDigest((prefix_value + "_INPUT").c_str(), receipt.input_fingerprint);
    PrintDigest((prefix_value + "_OUTPUT").c_str(), receipt.output_fingerprint);
}

}  // namespace

int main() {
    std::array<std::uint32_t, 3> positions{{50u, 53u, 53u}};
    std::array<laplace_id128, 3> identities{};
    std::array<laplace_isa_value_view, 2> identity_values{{
        {positions.data(), positions.size(), positions.size(),
         static_cast<std::uint32_t>(sizeof(positions[0])),
         LAPLACE_ISA_VALUE_U32_VECTOR, LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {identities.data(), 0u, identities.size(),
         static_cast<std::uint32_t>(sizeof(identities[0])),
         LAPLACE_ISA_VALUE_ID128_VECTOR, LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction identity_instruction{
        LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto identity_program = Program(&identity_instruction, identity_values.data());
    laplace_isa_receipt identity_receipt{};
    laplace_isa_error identity_error{};
    if (laplace_isa_execute(
            &identity_program, &identity_receipt, &identity_error) != LAPLACE_ISA_OK) {
        return 1;
    }
    PrintReceipt("IDENTITY", identity_receipt);
    PrintHex("IDENTITY_ENTITY_0", identities[0].bytes);
    PrintHex("IDENTITY_ENTITY_1", identities[1].bytes);
    PrintHex("IDENTITY_ENTITY_2", identities[2].bytes);

    const auto trajectory_entity = ParseId(LAPLACE_TRAJECTORY_VECTOR_ENTITY_HEX);
    laplace_trajectory_payload payload{};
    std::memcpy(payload.lane128, trajectory_entity.bytes, sizeof(payload.lane128));
    payload.ordinal = LAPLACE_TRAJECTORY_VECTOR_ORDINAL;
    payload.run_length = LAPLACE_TRAJECTORY_VECTOR_RUN;
    payload.metadata = LAPLACE_TRAJECTORY_VECTOR_METADATA_BITS;
    laplace_trajectory_carrier carrier{};
    if (laplace_trajectory_carrier_encode(&payload, &carrier) !=
        LAPLACE_TRAJECTORY_OK) {
        return 2;
    }
    std::array<std::uint8_t, sizeof(carrier)> canonical_carrier{};
    for (std::size_t slot = 0; slot < LAPLACE_TRAJECTORY_SLOT_COUNT; ++slot) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &carrier.slots[slot], sizeof(bits));
        for (std::size_t byte = 0; byte < sizeof(bits); ++byte) {
            canonical_carrier[slot * sizeof(bits) + byte] =
                static_cast<std::uint8_t>(bits >> (byte * 8u));
        }
    }
    PrintHex("TRAJECTORY_CARRIER", canonical_carrier);

    laplace_composition_occurrence occurrence{};
    std::array<laplace_isa_value_view, 2> trajectory_values{{
        {&carrier, 1u, 1u, static_cast<std::uint32_t>(sizeof(carrier)),
         LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {&occurrence, 0u, 1u, static_cast<std::uint32_t>(sizeof(occurrence)),
         LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction trajectory_instruction{
        LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto trajectory_program = Program(&trajectory_instruction, trajectory_values.data());
    laplace_isa_receipt trajectory_receipt{};
    laplace_isa_error trajectory_error{};
    if (laplace_isa_execute(
            &trajectory_program, &trajectory_receipt, &trajectory_error) !=
        LAPLACE_ISA_OK) {
        return 3;
    }
    PrintReceipt("TRAJECTORY", trajectory_receipt);
    PrintHex("TRAJECTORY_ENTITY", occurrence.entity_id.bytes);
    return 0;
}
