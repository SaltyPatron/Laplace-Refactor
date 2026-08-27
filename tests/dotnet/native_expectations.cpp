#include "laplace/framework.h"
#include "laplace/highway.h"
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
static_assert(std::is_standard_layout_v<laplace_highway_key>);
static_assert(std::is_standard_layout_v<laplace_highway_coordinate>);
static_assert(std::is_standard_layout_v<laplace_highway_registry_receipt>);
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

    const std::array<std::size_t, 83> native_layout{{
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
        sizeof(laplace_highway_key),
        offsetof(laplace_highway_key, kind),
        offsetof(laplace_highway_key, reserved),
        offsetof(laplace_highway_key, authority),
        offsetof(laplace_highway_key, release),
        offsetof(laplace_highway_key, name_space),
        offsetof(laplace_highway_key, local_identifier),
        offsetof(laplace_highway_key, version),
        sizeof(laplace_highway_coordinate),
        offsetof(laplace_highway_coordinate, coordinate),
        offsetof(laplace_highway_coordinate, collision_fingerprint),
        offsetof(laplace_highway_coordinate, kind),
        offsetof(laplace_highway_coordinate, reserved),
        offsetof(laplace_highway_coordinate, version),
        sizeof(laplace_highway_registry_receipt),
        offsetof(laplace_highway_registry_receipt, receipt_id),
        offsetof(laplace_highway_registry_receipt, context_fingerprint),
        offsetof(laplace_highway_registry_receipt, registry_fingerprint),
        offsetof(laplace_highway_registry_receipt, activation_epoch_id),
        offsetof(laplace_highway_registry_receipt, activation_epoch_fingerprint),
        offsetof(laplace_highway_registry_receipt, registry_version),
        offsetof(laplace_highway_registry_receipt, kind_count),
        offsetof(laplace_highway_registry_receipt, alias_count),
        offsetof(laplace_highway_registry_receipt, disposition_count),
        offsetof(laplace_highway_registry_receipt, status),
        offsetof(laplace_highway_registry_receipt, reserved),
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

    std::array<laplace_highway_key, 2> highway_keys{{
        {LAPLACE_HIGHWAY_KIND_LANGUAGE, 0u,
         identities[0], identities[1], identities[2], identities[3], 1u},
        {LAPLACE_HIGHWAY_KIND_OPERATION, 0u,
         identities[3], identities[2], identities[1], identities[0], 7u},
    }};
    std::array<laplace_highway_coordinate, 2> highway_coordinates{};
    std::array<laplace_isa_value_view, 2> highway_values{{
        {highway_keys.data(), highway_keys.size(), highway_keys.size(),
         static_cast<std::uint32_t>(sizeof(highway_keys[0])),
         LAPLACE_ISA_VALUE_HIGHWAY_KEY_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {highway_coordinates.data(), 0u, highway_coordinates.size(),
         static_cast<std::uint32_t>(sizeof(highway_coordinates[0])),
         LAPLACE_ISA_VALUE_HIGHWAY_COORDINATE_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction highway_instruction{
        LAPLACE_ISA_OPCODE_HIGHWAY_COORDINATE_CALCULATE_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_COORDINATE_CALCULATE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto highway_program = Program(
        &highway_instruction, highway_values.data(), &context);
    laplace_isa_receipt highway_receipt{};
    laplace_isa_error highway_error{};
    if (laplace_isa_execute(
            &highway_program, &highway_receipt, &highway_error) !=
        LAPLACE_ISA_OK) {
        std::fputs("direct native highway ISA execution failed\n", stderr);
        return 6;
    }

    std::array<std::uint32_t, 1> highway_registry_versions{{
        LAPLACE_HIGHWAY_REGISTRY_VERSION}};
    std::array<laplace_highway_registry_receipt, 1> highway_registry_outputs{};
    std::array<laplace_isa_value_view, 2> highway_registry_values{{
        {highway_registry_versions.data(), highway_registry_versions.size(),
         highway_registry_versions.size(),
         static_cast<std::uint32_t>(sizeof(highway_registry_versions[0])),
         LAPLACE_ISA_VALUE_U32_VECTOR, LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {highway_registry_outputs.data(), 0u, highway_registry_outputs.size(),
         static_cast<std::uint32_t>(sizeof(highway_registry_outputs[0])),
         LAPLACE_ISA_VALUE_HIGHWAY_REGISTRY_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction highway_registry_instruction{
        LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_REGISTRY_MATERIALIZE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto highway_registry_program = Program(
        &highway_registry_instruction, highway_registry_values.data(), &context);
    laplace_isa_receipt highway_registry_receipt{};
    laplace_isa_error highway_registry_error{};
    if (laplace_isa_execute(
            &highway_registry_program, &highway_registry_receipt,
            &highway_registry_error) != LAPLACE_ISA_OK) {
        std::fputs("direct native highway registry ISA execution failed\n", stderr);
        return 7;
    }

    const std::filesystem::path target(argv[1]);
    std::filesystem::create_directories(target.parent_path());
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fputs("cannot open managed parity fixture\n", stderr);
        return 73;
    }
    Write(output, MAGIC);
    const std::uint32_t fixture_version = 3u;
    const std::uint32_t layout_count = static_cast<std::uint32_t>(layout.size());
    const std::uint32_t identity_count = static_cast<std::uint32_t>(positions.size());
    const std::uint32_t trajectory_count = static_cast<std::uint32_t>(carriers.size());
    const std::uint32_t highway_count =
        static_cast<std::uint32_t>(highway_keys.size());
    const std::uint32_t highway_registry_count =
        static_cast<std::uint32_t>(highway_registry_versions.size());
    Write(output, fixture_version);
    Write(output, layout_count);
    Write(output, identity_count);
    Write(output, trajectory_count);
    Write(output, highway_count);
    Write(output, highway_registry_count);
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
    Write(output, highway_keys);
    Write(output, highway_coordinates);
    Write(output, highway_receipt);
    Write(output, highway_error);
    Write(output, highway_registry_versions);
    Write(output, highway_registry_outputs);
    Write(output, highway_registry_receipt);
    Write(output, highway_registry_error);
    output.close();
    return output ? 0 : 74;
}
