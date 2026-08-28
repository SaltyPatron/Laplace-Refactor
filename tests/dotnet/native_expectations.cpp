#include "laplace/evidence_testimony.h"
#include "laplace/framework.h"
#include "laplace/highway.h"
#include "laplace/isa.h"
#include "laplace/source_profile.h"
#include "laplace/trajectory.h"
#include "laplace/world_admission.h"
#include "../context_fixture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static_assert(std::is_standard_layout_v<laplace_evidence_testimony_record>);
static_assert(std::is_standard_layout_v<laplace_evidence_testimony_receipt>);
static_assert(std::is_standard_layout_v<laplace_source_profile_manifest>);
static_assert(std::is_standard_layout_v<laplace_source_profile_receipt>);
static_assert(std::is_standard_layout_v<laplace_world_admission_record>);
static_assert(std::is_standard_layout_v<laplace_world_admission_receipt>);
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

void Fill(laplace_digest256* digest, std::uint8_t value) {
    std::memset(digest->bytes, value, sizeof(digest->bytes));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: laplace_dotnet_native_expectations OUTPUT\n", stderr);
        return 64;
    }

    const std::array<std::size_t, 207> native_layout{{
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
        sizeof(laplace_evidence_testimony_record),
        offsetof(laplace_evidence_testimony_record, testimony_id),
        offsetof(laplace_evidence_testimony_record, evidence_node_id),
        offsetof(laplace_evidence_testimony_record, source_profile_id),
        offsetof(laplace_evidence_testimony_record, recipe_receipt_id),
        offsetof(laplace_evidence_testimony_record, trust_input_id),
        offsetof(laplace_evidence_testimony_record, outcome_detail_id),
        offsetof(laplace_evidence_testimony_record, uncertainty_numerator),
        offsetof(laplace_evidence_testimony_record, uncertainty_denominator),
        offsetof(laplace_evidence_testimony_record, sample_count),
        offsetof(laplace_evidence_testimony_record, source_type),
        offsetof(laplace_evidence_testimony_record, outcome_type),
        offsetof(laplace_evidence_testimony_record, disposition),
        offsetof(laplace_evidence_testimony_record, flags),
        sizeof(laplace_evidence_testimony_receipt),
        offsetof(laplace_evidence_testimony_receipt, receipt_id),
        offsetof(laplace_evidence_testimony_receipt, source_profile_id),
        offsetof(laplace_evidence_testimony_receipt, input_fingerprint),
        offsetof(laplace_evidence_testimony_receipt, output_fingerprint),
        offsetof(laplace_evidence_testimony_receipt, testimony_count),
        offsetof(laplace_evidence_testimony_receipt, sample_count),
        offsetof(laplace_evidence_testimony_receipt, uncertain_count),
        offsetof(laplace_evidence_testimony_receipt, negative_disposition_count),
        offsetof(laplace_evidence_testimony_receipt, version),
        offsetof(laplace_evidence_testimony_receipt, status),
        sizeof(laplace_source_profile_manifest),
        offsetof(laplace_source_profile_manifest, profile_id),
        offsetof(laplace_source_profile_manifest, coordinate),
        offsetof(laplace_source_profile_manifest, authority_release_fingerprint),
        offsetof(laplace_source_profile_manifest, license_fingerprint),
        offsetof(laplace_source_profile_manifest, artifact_graph_fingerprint),
        offsetof(laplace_source_profile_manifest, syntax_authority_fingerprint),
        offsetof(laplace_source_profile_manifest, recipe_program_fingerprint),
        offsetof(laplace_source_profile_manifest, universal_ast_mapping_fingerprint),
        offsetof(laplace_source_profile_manifest, highway_references_fingerprint),
        offsetof(laplace_source_profile_manifest, epistemic_witnessing_fingerprint),
        offsetof(laplace_source_profile_manifest, denominator_declaration_fingerprint),
        offsetof(laplace_source_profile_manifest, conformance_fingerprint),
        offsetof(laplace_source_profile_manifest, completion_law_fingerprint),
        offsetof(laplace_source_profile_manifest, selected_boundary_fingerprint),
        offsetof(laplace_source_profile_manifest, byte_count),
        offsetof(laplace_source_profile_manifest, container_count),
        offsetof(laplace_source_profile_manifest, member_count),
        offsetof(laplace_source_profile_manifest, file_count),
        offsetof(laplace_source_profile_manifest, record_count),
        offsetof(laplace_source_profile_manifest, field_count),
        offsetof(laplace_source_profile_manifest, syntax_node_count),
        offsetof(laplace_source_profile_manifest, span_count),
        offsetof(laplace_source_profile_manifest, edge_count),
        offsetof(laplace_source_profile_manifest, reference_count),
        offsetof(laplace_source_profile_manifest, occurrence_count),
        offsetof(laplace_source_profile_manifest, claim_count),
        offsetof(laplace_source_profile_manifest, mapping_count),
        offsetof(laplace_source_profile_manifest, error_count),
        offsetof(laplace_source_profile_manifest, unknown_count),
        offsetof(laplace_source_profile_manifest, transformation_count),
        offsetof(laplace_source_profile_manifest, output_count),
        offsetof(laplace_source_profile_manifest, closure_subject_count),
        offsetof(laplace_source_profile_manifest, accepted_count),
        offsetof(laplace_source_profile_manifest, rejected_count),
        offsetof(laplace_source_profile_manifest, duplicate_count),
        offsetof(laplace_source_profile_manifest, reused_count),
        offsetof(laplace_source_profile_manifest, transformed_count),
        offsetof(laplace_source_profile_manifest, lossy_count),
        offsetof(laplace_source_profile_manifest, unsupported_count),
        offsetof(laplace_source_profile_manifest, malformed_count),
        offsetof(laplace_source_profile_manifest, unresolved_count),
        offsetof(laplace_source_profile_manifest, persisted_count),
        offsetof(laplace_source_profile_manifest, derived_count),
        offsetof(laplace_source_profile_manifest, not_applicable_mask),
        offsetof(laplace_source_profile_manifest, reconstruction_class),
        offsetof(laplace_source_profile_manifest, flags),
        sizeof(laplace_source_profile_receipt),
        offsetof(laplace_source_profile_receipt, receipt_id),
        offsetof(laplace_source_profile_receipt, selected_boundary_fingerprint),
        offsetof(laplace_source_profile_receipt, input_fingerprint),
        offsetof(laplace_source_profile_receipt, output_fingerprint),
        offsetof(laplace_source_profile_receipt, profile_count),
        offsetof(laplace_source_profile_receipt, closure_subject_count),
        offsetof(laplace_source_profile_receipt, persisted_count),
        offsetof(laplace_source_profile_receipt, negative_count),
        offsetof(laplace_source_profile_receipt, exact_reconstruction_count),
        offsetof(laplace_source_profile_receipt, semantic_reconstruction_count),
        offsetof(laplace_source_profile_receipt, no_reconstruction_count),
        offsetof(laplace_source_profile_receipt, version),
        offsetof(laplace_source_profile_receipt, status),
        sizeof(laplace_world_admission_record),
        offsetof(laplace_world_admission_record, admission_id),
        offsetof(laplace_world_admission_record, source_profile_id),
        offsetof(laplace_world_admission_record, selected_boundary_fingerprint),
        offsetof(laplace_world_admission_record, source_profile_receipt_id),
        offsetof(laplace_world_admission_record, recipe_receipt_id),
        offsetof(laplace_world_admission_record, composition_working_set_receipt_id),
        offsetof(laplace_world_admission_record, composition_presence_receipt_id),
        offsetof(laplace_world_admission_record, composition_producer_receipt_id),
        offsetof(laplace_world_admission_record, composition_stream_receipt_id),
        offsetof(laplace_world_admission_record, evidence_lineage_receipt_id),
        offsetof(laplace_world_admission_record, evidence_testimony_receipt_id),
        offsetof(laplace_world_admission_record, readback_fingerprint),
        offsetof(laplace_world_admission_record, profile_occurrence_count),
        offsetof(laplace_world_admission_record, composition_occurrence_count),
        offsetof(laplace_world_admission_record, profile_claim_count),
        offsetof(laplace_world_admission_record, evidence_node_count),
        offsetof(laplace_world_admission_record, testimony_count),
        offsetof(laplace_world_admission_record, profile_bound_testimony_count),
        offsetof(laplace_world_admission_record, recipe_bound_testimony_count),
        offsetof(laplace_world_admission_record, lineage_bound_testimony_count),
        offsetof(laplace_world_admission_record, closure_subject_count),
        offsetof(laplace_world_admission_record, closed_subject_count),
        offsetof(laplace_world_admission_record, reconstruction_class),
        offsetof(laplace_world_admission_record, flags),
        sizeof(laplace_world_admission_receipt),
        offsetof(laplace_world_admission_receipt, receipt_id),
        offsetof(laplace_world_admission_receipt, selected_boundary_fingerprint),
        offsetof(laplace_world_admission_receipt, input_fingerprint),
        offsetof(laplace_world_admission_receipt, output_fingerprint),
        offsetof(laplace_world_admission_receipt, admission_count),
        offsetof(laplace_world_admission_receipt, occurrence_count),
        offsetof(laplace_world_admission_receipt, claim_count),
        offsetof(laplace_world_admission_receipt, evidence_node_count),
        offsetof(laplace_world_admission_receipt, testimony_count),
        offsetof(laplace_world_admission_receipt, closure_subject_count),
        offsetof(laplace_world_admission_receipt, version),
        offsetof(laplace_world_admission_receipt, status),
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

    laplace_digest256 testimony_profile{};
    laplace_digest256 testimony_recipe{};
    laplace_digest256 testimony_trust{};
    Fill(&testimony_profile, 0x71u);
    Fill(&testimony_recipe, 0x72u);
    Fill(&testimony_trust, 0x73u);
    auto make_testimony = [&](std::uint8_t node_seed,
                              std::uint8_t outcome_seed,
                              std::uint32_t source_type,
                              std::uint32_t outcome_type,
                              std::uint32_t disposition,
                              std::uint64_t uncertainty_numerator,
                              std::uint64_t uncertainty_denominator,
                              std::uint64_t sample_count) {
        laplace_evidence_testimony_record record{};
        Fill(&record.evidence_node_id, node_seed);
        record.source_profile_id = testimony_profile;
        record.recipe_receipt_id = testimony_recipe;
        record.trust_input_id = testimony_trust;
        Fill(&record.outcome_detail_id, outcome_seed);
        record.uncertainty_numerator = uncertainty_numerator;
        record.uncertainty_denominator = uncertainty_denominator;
        record.sample_count = sample_count;
        record.source_type = source_type;
        record.outcome_type = outcome_type;
        record.disposition = disposition;
        record.flags = LAPLACE_EVIDENCE_TESTIMONY_FLAGS_NONE;
        if (laplace_evidence_testimony_identify(
                &record, &record.testimony_id) !=
            LAPLACE_EVIDENCE_TESTIMONY_OK) {
            std::fputs("direct native testimony identity failed\n", stderr);
            std::exit(8);
        }
        return record;
    };
    std::array<laplace_evidence_testimony_record, 2> testimony_records{{
        make_testimony(
            0x41u, 0x51u, LAPLACE_EVIDENCE_SOURCE_STANDARD,
            LAPLACE_EVIDENCE_OUTCOME_MAPPING,
            LAPLACE_EVIDENCE_DISPOSITION_PERSISTED, 0u, 1u, 1u),
        make_testimony(
            0x42u, 0x52u, LAPLACE_EVIDENCE_SOURCE_CORPUS,
            LAPLACE_EVIDENCE_OUTCOME_ASSERTION,
            LAPLACE_EVIDENCE_DISPOSITION_UNSUPPORTED, 1u, 4u, 7u)}};
    std::sort(
        testimony_records.begin(), testimony_records.end(),
        [](const auto& left, const auto& right) {
            return std::memcmp(
                       left.testimony_id.bytes, right.testimony_id.bytes,
                       sizeof(left.testimony_id.bytes)) < 0;
        });
    std::array<laplace_evidence_testimony_receipt, 2> testimony_output_capacity{};
    std::array<laplace_isa_value_view, 2> testimony_values{{
        {testimony_records.data(), testimony_records.size(),
         testimony_records.size(),
         static_cast<std::uint32_t>(sizeof(testimony_records[0])),
         LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECORD_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {testimony_output_capacity.data(), 0u, testimony_output_capacity.size(),
         static_cast<std::uint32_t>(sizeof(testimony_output_capacity[0])),
         LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction testimony_instruction{
        LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_TESTIMONY_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_TESTIMONY_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto testimony_program = Program(
        &testimony_instruction, testimony_values.data(), &context);
    laplace_isa_receipt testimony_receipt{};
    laplace_isa_error testimony_error{};
    if (laplace_isa_execute(
            &testimony_program, &testimony_receipt, &testimony_error) !=
        LAPLACE_ISA_OK) {
        std::fputs("direct native testimony ISA execution failed\n", stderr);
        return 9;
    }
    const std::array<laplace_evidence_testimony_receipt, 1> testimony_outputs{{
        testimony_output_capacity[0]}};

    laplace_digest256 selected_boundary{};
    Fill(&selected_boundary, 0x91u);
    auto make_source_profile = [&](std::uint8_t seed, bool reverse_scope) {
        laplace_source_profile_manifest profile{};
        profile.coordinate.kind = LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE;
        profile.coordinate.authority = reverse_scope ? identities[3] : identities[0];
        profile.coordinate.release = reverse_scope ? identities[2] : identities[1];
        profile.coordinate.name_space = reverse_scope ? identities[1] : identities[2];
        profile.coordinate.local_identifier = reverse_scope ? identities[0] : identities[3];
        profile.coordinate.version = 1u;
        Fill(&profile.authority_release_fingerprint, seed);
        Fill(&profile.license_fingerprint, static_cast<std::uint8_t>(seed + 1u));
        Fill(&profile.artifact_graph_fingerprint, static_cast<std::uint8_t>(seed + 2u));
        Fill(&profile.syntax_authority_fingerprint, static_cast<std::uint8_t>(seed + 3u));
        Fill(&profile.recipe_program_fingerprint, static_cast<std::uint8_t>(seed + 4u));
        Fill(&profile.universal_ast_mapping_fingerprint, static_cast<std::uint8_t>(seed + 5u));
        Fill(&profile.highway_references_fingerprint, static_cast<std::uint8_t>(seed + 6u));
        Fill(&profile.epistemic_witnessing_fingerprint, static_cast<std::uint8_t>(seed + 7u));
        Fill(&profile.denominator_declaration_fingerprint, static_cast<std::uint8_t>(seed + 8u));
        Fill(&profile.conformance_fingerprint, static_cast<std::uint8_t>(seed + 9u));
        Fill(&profile.completion_law_fingerprint, static_cast<std::uint8_t>(seed + 10u));
        profile.selected_boundary_fingerprint = selected_boundary;
        profile.byte_count = 64u;
        profile.container_count = 1u;
        profile.member_count = 1u;
        profile.file_count = 1u;
        profile.record_count = 1u;
        profile.field_count = 2u;
        profile.syntax_node_count = 3u;
        profile.span_count = 2u;
        profile.occurrence_count = 2u;
        profile.output_count = 1u;
        profile.closure_subject_count = 1u;
        profile.persisted_count = 1u;
        profile.not_applicable_mask =
            (UINT64_C(1) << 8u) | (UINT64_C(1) << 9u) |
            (UINT64_C(1) << 11u) | (UINT64_C(1) << 12u) |
            (UINT64_C(1) << 13u) | (UINT64_C(1) << 14u) |
            (UINT64_C(1) << 15u);
        profile.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
        if (laplace_source_profile_identify(&profile, &profile.profile_id) !=
            LAPLACE_SOURCE_PROFILE_OK) {
            std::fputs("direct native source-profile identity failed\n", stderr);
            std::exit(10);
        }
        return profile;
    };
    std::array<laplace_source_profile_manifest, 2> source_profiles{{
        make_source_profile(0xa1u, false),
        make_source_profile(0xb1u, true)}};
    std::sort(
        source_profiles.begin(), source_profiles.end(),
        [](const auto& left, const auto& right) {
            return std::memcmp(
                       left.profile_id.bytes, right.profile_id.bytes,
                       sizeof(left.profile_id.bytes)) < 0;
        });
    std::array<laplace_source_profile_receipt, 2> source_profile_output_capacity{};
    std::array<laplace_isa_value_view, 2> source_profile_values{{
        {source_profiles.data(), source_profiles.size(), source_profiles.size(),
         static_cast<std::uint32_t>(sizeof(source_profiles[0])),
         LAPLACE_ISA_VALUE_SOURCE_PROFILE_MANIFEST_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {source_profile_output_capacity.data(), 0u,
         source_profile_output_capacity.size(),
         static_cast<std::uint32_t>(sizeof(source_profile_output_capacity[0])),
         LAPLACE_ISA_VALUE_SOURCE_PROFILE_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction source_profile_instruction{
        LAPLACE_ISA_OPCODE_SOURCE_PROFILE_VALIDATE_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_SOURCE_PROFILE_VALIDATE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto source_profile_program = Program(
        &source_profile_instruction, source_profile_values.data(), &context);
    laplace_isa_receipt source_profile_receipt{};
    laplace_isa_error source_profile_error{};
    if (laplace_isa_execute(
            &source_profile_program, &source_profile_receipt,
            &source_profile_error) != LAPLACE_ISA_OK) {
        std::fputs("direct native source-profile ISA execution failed\n", stderr);
        return 11;
    }
    const std::array<laplace_source_profile_receipt, 1> source_profile_outputs{{
        source_profile_output_capacity[0]}};

    std::array<laplace_world_admission_record, 1> world_admissions{};
    auto& world_admission = world_admissions[0];
    world_admission.source_profile_id = source_profiles[0].profile_id;
    world_admission.selected_boundary_fingerprint = selected_boundary;
    world_admission.source_profile_receipt_id =
        source_profile_outputs[0].receipt_id;
    world_admission.recipe_receipt_id =
        source_profiles[0].recipe_program_fingerprint;
    Fill(&world_admission.composition_working_set_receipt_id, 0xc1u);
    Fill(&world_admission.composition_presence_receipt_id, 0xc2u);
    Fill(&world_admission.composition_producer_receipt_id, 0xc3u);
    Fill(&world_admission.composition_stream_receipt_id, 0xc4u);
    Fill(&world_admission.evidence_lineage_receipt_id, 0xc5u);
    world_admission.evidence_testimony_receipt_id =
        testimony_outputs[0].receipt_id;
    Fill(&world_admission.readback_fingerprint, 0xc6u);
    world_admission.profile_occurrence_count = 2u;
    world_admission.composition_occurrence_count = 2u;
    world_admission.profile_claim_count = 1u;
    world_admission.evidence_node_count = 1u;
    world_admission.testimony_count = 1u;
    world_admission.profile_bound_testimony_count = 1u;
    world_admission.recipe_bound_testimony_count = 1u;
    world_admission.lineage_bound_testimony_count = 1u;
    world_admission.closure_subject_count = 1u;
    world_admission.closed_subject_count = 1u;
    world_admission.reconstruction_class =
        LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    world_admission.flags = LAPLACE_WORLD_ADMISSION_FLAGS_NONE;
    if (laplace_world_admission_identify(
            &world_admission, &world_admission.admission_id) !=
        LAPLACE_WORLD_ADMISSION_OK) {
        std::fputs("direct native world-admission identity failed\n", stderr);
        return 12;
    }
    std::array<laplace_world_admission_receipt, 1> world_output_capacity{};
    std::array<laplace_isa_value_view, 2> world_values{{
        {world_admissions.data(), world_admissions.size(), world_admissions.size(),
         static_cast<std::uint32_t>(sizeof(world_admissions[0])),
         LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECORD_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {world_output_capacity.data(), 0u, world_output_capacity.size(),
         static_cast<std::uint32_t>(sizeof(world_output_capacity[0])),
         LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction world_instruction{
        LAPLACE_ISA_OPCODE_WORLD_ADMISSION_CLOSE_BATCH,
        0u,
        1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_WORLD_ADMISSION_CLOSE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto world_program = Program(
        &world_instruction, world_values.data(), &context);
    laplace_isa_receipt world_receipt{};
    laplace_isa_error world_error{};
    if (laplace_isa_execute(
            &world_program, &world_receipt, &world_error) != LAPLACE_ISA_OK) {
        std::fputs("direct native world-admission ISA execution failed\n", stderr);
        return 13;
    }
    const std::array<laplace_world_admission_receipt, 1> world_outputs{{
        world_output_capacity[0]}};

    const std::filesystem::path target(argv[1]);
    std::filesystem::create_directories(target.parent_path());
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fputs("cannot open managed parity fixture\n", stderr);
        return 73;
    }
    Write(output, MAGIC);
    const std::uint32_t fixture_version = 6u;
    const std::uint32_t layout_count = static_cast<std::uint32_t>(layout.size());
    const std::uint32_t identity_count = static_cast<std::uint32_t>(positions.size());
    const std::uint32_t trajectory_count = static_cast<std::uint32_t>(carriers.size());
    const std::uint32_t highway_count =
        static_cast<std::uint32_t>(highway_keys.size());
    const std::uint32_t highway_registry_count =
        static_cast<std::uint32_t>(highway_registry_versions.size());
    const std::uint32_t testimony_count =
        static_cast<std::uint32_t>(testimony_records.size());
    const std::uint32_t source_profile_count =
        static_cast<std::uint32_t>(source_profiles.size());
    const std::uint32_t world_admission_count =
        static_cast<std::uint32_t>(world_admissions.size());
    Write(output, fixture_version);
    Write(output, layout_count);
    Write(output, identity_count);
    Write(output, trajectory_count);
    Write(output, highway_count);
    Write(output, highway_registry_count);
    Write(output, testimony_count);
    Write(output, source_profile_count);
    Write(output, world_admission_count);
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
    Write(output, testimony_records);
    Write(output, testimony_outputs);
    Write(output, testimony_receipt);
    Write(output, testimony_error);
    Write(output, source_profiles);
    Write(output, source_profile_outputs);
    Write(output, source_profile_receipt);
    Write(output, source_profile_error);
    Write(output, world_admissions);
    Write(output, world_outputs);
    Write(output, world_receipt);
    Write(output, world_error);
    output.close();
    return output ? 0 : 74;
}
