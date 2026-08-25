#include "laplace/framework.h"
#include "laplace/isa.h"
#include "laplace/trajectory.h"
#include "../context_fixture.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

namespace {

static_assert(std::endian::native == std::endian::little);
static_assert(std::is_standard_layout_v<laplace_digest256>);
static_assert(std::is_standard_layout_v<laplace_id128>);
static_assert(std::is_standard_layout_v<laplace_trajectory_carrier>);
static_assert(std::is_standard_layout_v<laplace_composition_occurrence>);
static_assert(std::is_standard_layout_v<laplace_isa_value_view>);
static_assert(std::is_standard_layout_v<laplace_isa_instruction>);
static_assert(std::is_standard_layout_v<laplace_isa_program>);
static_assert(std::is_standard_layout_v<laplace_isa_error>);
static_assert(std::is_standard_layout_v<laplace_isa_receipt>);
static_assert(std::is_standard_layout_v<laplace_framework_context>);

template <typename Value>
void Write(std::ofstream& output, const Value& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!output) {
        std::fputs("cannot write managed parity fixture\n", stderr);
        std::exit(70);
    }
}

template <typename Value, std::size_t Size>
void Write(std::ofstream& output, const std::array<Value, Size>& values) {
    output.write(
        reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(sizeof(Value) * values.size()));
    if (!output) {
        std::fputs("cannot write managed parity fixture array\n", stderr);
        std::exit(70);
    }
}

laplace_isa_program Program(
    laplace_isa_instruction* instruction,
    laplace_isa_value_view* values,
    const laplace_framework_context* context) {
    return laplace_isa_program{
        instruction,
        values,
        context,
        1u,
        2u,
        LAPLACE_ISA_MAJOR,
        LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL,
        0u};
}

std::uint64_t ContentMetadata(
    std::uint8_t tier,
    bool has_atom,
    std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
           (has_atom ? (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) : 0u) |
           (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

constexpr std::array<std::uint8_t, 8> MAGIC{{'L', 'P', 'D', 'N', 'E', 'T', '1', 0}};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: laplace_dotnet_native_expectations OUTPUT\n", stderr);
        return 64;
    }

    const std::array<std::size_t, 57> native_layout{{
        sizeof(laplace_digest256),
        sizeof(laplace_id128),
        sizeof(laplace_trajectory_carrier),
        sizeof(laplace_composition_occurrence),
        offsetof(laplace_composition_occurrence, entity_id),
        offsetof(laplace_composition_occurrence, logical_ordinal),
        offsetof(laplace_composition_occurrence, metadata),
        offsetof(laplace_composition_occurrence, atom),
        offsetof(laplace_composition_occurrence, packed_ordinal),
        offsetof(laplace_composition_occurrence, run_length),
        offsetof(laplace_composition_occurrence, tier),
        offsetof(laplace_composition_occurrence, has_atom),
        offsetof(laplace_composition_occurrence, reserved),
        sizeof(laplace_isa_value_view),
        offsetof(laplace_isa_value_view, data),
        offsetof(laplace_isa_value_view, count),
        offsetof(laplace_isa_value_view, capacity),
        offsetof(laplace_isa_value_view, stride_bytes),
        offsetof(laplace_isa_value_view, type),
        offsetof(laplace_isa_value_view, flags),
        offsetof(laplace_isa_value_view, reserved),
        sizeof(laplace_isa_instruction),
        offsetof(laplace_isa_instruction, opcode),
        offsetof(laplace_isa_instruction, input_value),
        offsetof(laplace_isa_instruction, output_value),
        offsetof(laplace_isa_instruction, version),
        offsetof(laplace_isa_instruction, flags),
        sizeof(laplace_isa_program),
        offsetof(laplace_isa_program, instructions),
        offsetof(laplace_isa_program, values),
        offsetof(laplace_isa_program, context),
        offsetof(laplace_isa_program, instruction_count),
        offsetof(laplace_isa_program, value_count),
        offsetof(laplace_isa_program, major),
        offsetof(laplace_isa_program, minor),
        offsetof(laplace_isa_program, flags),
        offsetof(laplace_isa_program, receipt_detail),
        offsetof(laplace_isa_program, reserved),
        sizeof(laplace_isa_error),
        offsetof(laplace_isa_error, status),
        offsetof(laplace_isa_error, instruction_index),
        offsetof(laplace_isa_error, value_index),
        offsetof(laplace_isa_error, reserved),
        sizeof(laplace_isa_receipt),
        offsetof(laplace_isa_receipt, receipt_id),
        offsetof(laplace_isa_receipt, context_fingerprint),
        offsetof(laplace_isa_receipt, program_fingerprint),
        offsetof(laplace_isa_receipt, input_fingerprint),
        offsetof(laplace_isa_receipt, output_fingerprint),
        offsetof(laplace_isa_receipt, instruction_count),
        offsetof(laplace_isa_receipt, executed_instruction_count),
        offsetof(laplace_isa_receipt, major),
        offsetof(laplace_isa_receipt, minor),
        offsetof(laplace_isa_receipt, receipt_detail),
        offsetof(laplace_isa_receipt, status),
        offsetof(laplace_isa_receipt, reserved),
        sizeof(laplace_framework_context),
    }};
    std::array<std::uint32_t, native_layout.size()> layout{};
    for (std::size_t index = 0; index < native_layout.size(); ++index) {
        if (native_layout[index] > std::numeric_limits<std::uint32_t>::max()) {
            std::fputs("native ABI layout value exceeds fixture encoding\n", stderr);
            return 1;
        }
        layout[index] = static_cast<std::uint32_t>(native_layout[index]);
    }

    const laplace_framework_context context = laplace_test_context(7u);

    std::array<std::uint32_t, 4> positions{{0u, 0x41u, 0xd800u, 0x10ffffu}};
    std::array<laplace_id128, 4> identities{};
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
    auto identity_program = Program(
        &identity_instruction, identity_values.data(), &context);
    laplace_isa_receipt identity_receipt{};
    laplace_isa_error identity_error{};
    if (laplace_isa_execute(
            &identity_program, &identity_receipt, &identity_error) !=
        LAPLACE_ISA_OK) {
        std::fputs("direct native identity ISA execution failed\n", stderr);
        return 2;
    }

    laplace_id128 a{};
    laplace_id128 b{};
    if (laplace_identity_codepoint(0x41u, &a) != LAPLACE_IDENTITY_OK ||
        laplace_identity_codepoint(0x42u, &b) != LAPLACE_IDENTITY_OK) {
        return 3;
    }
    std::array<laplace_trajectory_carrier, 3> carriers{};
    if (laplace_trajectory_composition_encode(
            &a, 1u, 1u, ContentMetadata(2u, true, 0x41u), &carriers[0]) !=
            LAPLACE_TRAJECTORY_OK ||
        laplace_trajectory_composition_encode(
            &b, 2u, 3u, ContentMetadata(3u, false, 0u), &carriers[1]) !=
            LAPLACE_TRAJECTORY_OK ||
        laplace_trajectory_composition_encode(
            &a, 5u, 1u, ContentMetadata(2u, true, 0x41u), &carriers[2]) !=
            LAPLACE_TRAJECTORY_OK) {
        std::fputs("direct native trajectory encoding failed\n", stderr);
        return 4;
    }
    std::array<laplace_composition_occurrence, 3> occurrences{};
    std::array<laplace_isa_value_view, 2> trajectory_values{{
        {carriers.data(), carriers.size(), carriers.size(),
         static_cast<std::uint32_t>(sizeof(carriers[0])),
         LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {occurrences.data(), 0u, occurrences.size(),
         static_cast<std::uint32_t>(sizeof(occurrences[0])),
         LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction trajectory_instruction{
        LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto trajectory_program = Program(
        &trajectory_instruction, trajectory_values.data(), &context);
    laplace_isa_receipt trajectory_receipt{};
    laplace_isa_error trajectory_error{};
    if (laplace_isa_execute(
            &trajectory_program, &trajectory_receipt, &trajectory_error) !=
        LAPLACE_ISA_OK) {
        std::fputs("direct native trajectory ISA execution failed\n", stderr);
        return 5;
    }

    const std::filesystem::path target(argv[1]);
    std::filesystem::create_directories(target.parent_path());
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fputs("cannot open managed parity fixture\n", stderr);
        return 73;
    }
    Write(output, MAGIC);
    const std::uint32_t fixture_version = 1u;
    const std::uint32_t layout_count = static_cast<std::uint32_t>(layout.size());
    const std::uint32_t identity_count = static_cast<std::uint32_t>(positions.size());
    const std::uint32_t trajectory_count = static_cast<std::uint32_t>(carriers.size());
    Write(output, fixture_version);
    Write(output, layout_count);
    Write(output, identity_count);
    Write(output, trajectory_count);
    Write(output, layout);
    Write(output, context);
    Write(output, positions);
    Write(output, identities);
    Write(output, identity_receipt);
    Write(output, identity_error);
    Write(output, carriers);
    Write(output, occurrences);
    Write(output, trajectory_receipt);
    Write(output, trajectory_error);
    output.close();
    return output ? 0 : 74;
}
