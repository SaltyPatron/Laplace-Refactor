#include "laplace/composition.h"
#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/isa.h"
#include "laplace/highway.h"
#include "laplace/persistence.h"
#include "laplace/reference_mapping.h"
#include "laplace/source_profile.h"
#include "laplace/tabular_source.h"
#include "laplace/trajectory.h"
#include "laplace/world_admission.h"
#include "../context_fixture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

laplace_digest256 ParseDigest(std::string_view hex) {
    laplace_digest256 digest{};
    for (std::size_t index = 0; index < sizeof(digest.bytes); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(
            (HexNibble(hex[index * 2]) << 4) | HexNibble(hex[index * 2 + 1]));
    }
    return digest;
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
    static const laplace_framework_context context = laplace_test_context(0u);
    return laplace_isa_program{
        instruction,
        values,
        &context,
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
    PrintDigest((prefix_value + "_CONTEXT").c_str(), receipt.context_fingerprint);
    PrintDigest((prefix_value + "_PROGRAM").c_str(), receipt.program_fingerprint);
    PrintDigest((prefix_value + "_INPUT").c_str(), receipt.input_fingerprint);
    PrintDigest((prefix_value + "_OUTPUT").c_str(), receipt.output_fingerprint);
}

void Fill(laplace_digest256* digest, std::uint8_t seed) {
    for (std::size_t index = 0; index < sizeof(digest->bytes); ++index) {
        digest->bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

void Repeat(laplace_digest256* digest, std::uint8_t value) {
    std::fill(std::begin(digest->bytes), std::end(digest->bytes), value);
}

laplace_id128 HighwayId(std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_composition_status ResolveNovelCompositionPresence(
    void*,
    const laplace_composition_entity_candidate* entity_candidates,
    std::size_t entity_candidate_count,
    const laplace_persistence_physicality_record*,
    std::size_t physicality_candidate_count,
    std::uint8_t* entity_dispositions,
    std::uint8_t* physicality_dispositions,
    laplace_composition_presence_provider_result* result) {
    if (entity_candidates == nullptr || entity_candidate_count == 0U ||
        entity_dispositions == nullptr || result == nullptr ||
        (physicality_candidate_count != 0U &&
         physicality_dispositions == nullptr)) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    std::array<bool, LAPLACE_COMPOSITION_TIER_MAXIMUM + 1U> tiers{};
    std::uint64_t tier_count = 0U;
    for (std::size_t index = 0U; index < entity_candidate_count; ++index) {
        entity_dispositions[index] = LAPLACE_COMPOSITION_NOVEL;
        const auto tier = entity_candidates[index].tier_floor;
        if (!tiers[tier]) {
            tiers[tier] = true;
            ++tier_count;
        }
    }
    if (physicality_candidate_count != 0U) {
        std::fill(
            physicality_dispositions,
            physicality_dispositions + physicality_candidate_count,
            static_cast<std::uint8_t>(LAPLACE_COMPOSITION_NOVEL));
    }
    Repeat(&result->provider_fingerprint, 0x51U);
    Repeat(&result->provider_receipt_id, 0x71U);
    result->returned_entity_count = entity_candidate_count;
    result->returned_physicality_count = physicality_candidate_count;
    result->entity_round_count = tier_count;
    result->physicality_round_count = physicality_candidate_count == 0U ? 0U : 1U;
    return LAPLACE_COMPOSITION_OK;
}

std::uint64_t Metadata(std::uint8_t tier, std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
           (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) |
           (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

template <typename Encoder, typename... Arguments>
bool AppendPersistenceFrame(
    std::vector<std::vector<std::uint8_t>>* frames,
    std::uint16_t kind,
    Encoder encoder,
    Arguments&&... arguments) {
    frames->emplace_back(laplace_persistence_frame_bytes(kind));
    auto& frame = frames->back();
    std::size_t written = 0;
    return encoder(
               std::forward<Arguments>(arguments)...,
               frame.data(), frame.size(), &written) == LAPLACE_PERSISTENCE_OK &&
           written == frame.size();
}

}  // namespace

int main() {
    const std::array<std::uint8_t, 5> source_archive{{0u, 1u, 255u, 'P', 'K'}};
    const std::string source_text{"Id\tName\neng\tEnglish\njpn\t日本語\n"};
    static constexpr std::array<std::string_view, 2> source_column_names{{
        "Id", "Name"}};
    std::array<laplace_tabular_column, 2> source_columns{};
    for (std::size_t index = 0u; index < source_columns.size(); ++index) {
        source_columns[index].bytes = reinterpret_cast<const std::uint8_t*>(
            source_column_names[index].data());
        source_columns[index].byte_count = source_column_names[index].size();
    }
    std::array<laplace_tabular_artifact, 2> source_artifacts{};
    source_artifacts[0].artifact_id = ParseDigest(
        "3caaab6abbbcc0bc44f88ef7b56033746fa2f37a94067e43df296518eba3cef5");
    std::memcpy(
        source_artifacts[0].expected_sha256,
        source_artifacts[0].artifact_id.bytes,
        sizeof(source_artifacts[0].expected_sha256));
    source_artifacts[0].bytes = source_archive.data();
    source_artifacts[0].name = "release.zip";
    source_artifacts[0].byte_count = source_archive.size();
    source_artifacts[0].name_byte_count = std::strlen(source_artifacts[0].name);
    source_artifacts[0].mode = LAPLACE_TABULAR_ARTIFACT_RAW;
    source_artifacts[0].flags = LAPLACE_TABULAR_ARTIFACT_CONTAINER;
    source_artifacts[1].artifact_id = ParseDigest(
        "0f572261737480d63a5b9d2298e95c0ad5b6964062efc263f0a5707c21c7e01c");
    source_artifacts[1].parent_artifact_id = source_artifacts[0].artifact_id;
    std::memcpy(
        source_artifacts[1].expected_sha256,
        source_artifacts[1].artifact_id.bytes,
        sizeof(source_artifacts[1].expected_sha256));
    source_artifacts[1].bytes = reinterpret_cast<const std::uint8_t*>(
        source_text.data());
    source_artifacts[1].name = "tables/languages.tab";
    source_artifacts[1].columns = source_columns.data();
    source_artifacts[1].byte_count = source_text.size();
    source_artifacts[1].name_byte_count = std::strlen(source_artifacts[1].name);
    source_artifacts[1].expected_record_count = 3u;
    source_artifacts[1].expected_field_count = 6u;
    source_artifacts[1].reference_column_mask = 3u;
    source_artifacts[1].mode = LAPLACE_TABULAR_ARTIFACT_DELIMITED;
    source_artifacts[1].delimiter = '\t';
    source_artifacts[1].line_terminator = LAPLACE_TABULAR_TERMINATOR_LF;
    source_artifacts[1].expected_column_count = 2u;
    source_artifacts[1].header_record_count = 1u;
    source_artifacts[1].outcome_type = LAPLACE_EVIDENCE_OUTCOME_MAPPING;
    source_artifacts[1].flags = LAPLACE_TABULAR_ARTIFACT_MEMBER |
        LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;
    std::array<laplace_tabular_reference_rule, 2> source_reference_rules{};
    source_reference_rules[0].artifact_index = 1u;
    source_reference_rules[0].column_index = 0u;
    std::memset(
        source_reference_rules[0].name_space.bytes, 0x50,
        sizeof(source_reference_rules[0].name_space.bytes));
    source_reference_rules[0].kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
    source_reference_rules[0].flags = LAPLACE_REFERENCE_RULE_ENDPOINT |
        LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION;
    source_reference_rules[1].artifact_index = 1u;
    source_reference_rules[1].column_index = 1u;
    std::memset(
        source_reference_rules[1].name_space.bytes, 0x51,
        sizeof(source_reference_rules[1].name_space.bytes));
    source_reference_rules[1].kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
    source_reference_rules[1].flags = LAPLACE_REFERENCE_RULE_ENDPOINT |
        LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION;
    static constexpr std::string_view source_relation{"="};
    std::array<laplace_tabular_mapping_rule, 1> source_mapping_rules{};
    source_mapping_rules[0].relation_content =
        reinterpret_cast<const std::uint8_t*>(source_relation.data());
    source_mapping_rules[0].relation_content_byte_count =
        source_relation.size();
    source_mapping_rules[0].artifact_index = 1u;
    source_mapping_rules[0].left_column_index = 0u;
    source_mapping_rules[0].right_column_index = 1u;
    source_mapping_rules[0].relation_version = 1u;
    source_mapping_rules[0].relation_kind = LAPLACE_HIGHWAY_KIND_RELATION;
    source_mapping_rules[0].flags = LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED;
    laplace_digest256 source_artifact_graph{};
    if (laplace_tabular_source_graph_identify(
            source_artifacts.data(), source_artifacts.size(),
            source_reference_rules.data(), source_reference_rules.size(),
            source_mapping_rules.data(), source_mapping_rules.size(),
            &source_artifact_graph) != LAPLACE_TABULAR_SOURCE_OK) {
        return 48;
    }
    PrintDigest("TABULAR_ARTIFACT_GRAPH", source_artifact_graph);
    PrintDigest("TABULAR_ARCHIVE_ID", source_artifacts[0].artifact_id);
    PrintDigest("TABULAR_TEXT_ID", source_artifacts[1].artifact_id);

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

    std::array<laplace_highway_key, 2> highway_keys{{
        {LAPLACE_HIGHWAY_KIND_LANGUAGE, 0u, HighwayId(0x10u), HighwayId(0x30u),
         HighwayId(0x50u), HighwayId(0x70u), 1u},
        {LAPLACE_HIGHWAY_KIND_EFFECT, 0u, HighwayId(0x10u), HighwayId(0x31u),
         HighwayId(0x50u), HighwayId(0x70u), 1u}}};
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
        0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_COORDINATE_CALCULATE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto highway_program = Program(&highway_instruction, highway_values.data());
    laplace_isa_receipt highway_receipt{};
    laplace_isa_error highway_error{};
    if (laplace_isa_execute(
            &highway_program, &highway_receipt, &highway_error) != LAPLACE_ISA_OK) {
        return 31;
    }
    PrintReceipt("HIGHWAY", highway_receipt);
    PrintHex("HIGHWAY_COORDINATE_0", highway_coordinates[0].coordinate.bytes);
    PrintHex("HIGHWAY_COORDINATE_1", highway_coordinates[1].coordinate.bytes);
    PrintHex("HIGHWAY_FINGERPRINT_0",
             highway_coordinates[0].collision_fingerprint.bytes);
    PrintHex("HIGHWAY_FINGERPRINT_1",
             highway_coordinates[1].collision_fingerprint.bytes);

    std::array<std::uint32_t, 1> registry_versions{{
        LAPLACE_HIGHWAY_REGISTRY_VERSION}};
    std::array<laplace_highway_registry_receipt, 1> registry_outputs{};
    std::array<laplace_isa_value_view, 2> registry_values{{
        {registry_versions.data(), registry_versions.size(),
         registry_versions.size(),
         static_cast<std::uint32_t>(sizeof(registry_versions[0])),
         LAPLACE_ISA_VALUE_U32_VECTOR, LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {registry_outputs.data(), 0u, registry_outputs.size(),
         static_cast<std::uint32_t>(sizeof(registry_outputs[0])),
         LAPLACE_ISA_VALUE_HIGHWAY_REGISTRY_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction registry_instruction{
        LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH,
        0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_REGISTRY_MATERIALIZE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto registry_program = Program(&registry_instruction, registry_values.data());
    laplace_isa_receipt registry_isa_receipt{};
    laplace_isa_error registry_error{};
    if (laplace_isa_execute(
            &registry_program, &registry_isa_receipt, &registry_error) !=
        LAPLACE_ISA_OK) {
        return 32;
    }
    PrintReceipt("REGISTRY_ISA", registry_isa_receipt);
    PrintHex("REGISTRY_MATERIALIZATION_RECEIPT",
             registry_outputs[0].receipt_id.bytes);
    PrintHex("REGISTRY_FINGERPRINT",
             registry_outputs[0].registry_fingerprint.bytes);
    PrintHex("REGISTRY_EPOCH_ID",
             registry_outputs[0].activation_epoch_id.bytes);
    PrintHex("REGISTRY_EPOCH_FINGERPRINT",
             registry_outputs[0].activation_epoch_fingerprint.bytes);

    laplace_id128 entity_a{};
    laplace_id128 entity_b{};
    laplace_digest256 witness_a{};
    laplace_digest256 witness_b{};
    if (laplace_identity_codepoint_witness(0x41u, &entity_a, &witness_a) !=
            LAPLACE_IDENTITY_OK ||
        laplace_identity_codepoint_witness(0x42u, &entity_b, &witness_b) !=
            LAPLACE_IDENTITY_OK) {
        return 4;
    }
    std::array<laplace_trajectory_carrier, 3> persistence_carriers{};
    if (laplace_trajectory_composition_encode(
            &entity_a, 1u, 1u, Metadata(2u, 0x41u),
            &persistence_carriers[0]) != LAPLACE_TRAJECTORY_OK ||
        laplace_trajectory_composition_encode(
            &entity_b, 2u, 3u, Metadata(2u, 0x42u),
            &persistence_carriers[1]) != LAPLACE_TRAJECTORY_OK ||
        laplace_trajectory_composition_encode(
            &entity_a, 5u, 1u, Metadata(2u, 0x41u),
            &persistence_carriers[2]) != LAPLACE_TRAJECTORY_OK) {
        return 5;
    }
    laplace_persistence_physicality_record physicality{};
    physicality.entity_id = entity_a;
    physicality.physicality_type = LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    physicality.vertex_class = LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    physicality.recipe_version = 1u;
    physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    Fill(&physicality.recipe_fingerprint, 0x10u);
    Fill(&physicality.geometry_epoch, 0x40u);
    if (laplace_persistence_trajectory_fingerprint(
            persistence_carriers.data(), persistence_carriers.size(),
            &physicality.trajectory_fingerprint) != LAPLACE_PERSISTENCE_OK) {
        return 6;
    }
    physicality.centroid.component[0] = 0.125;
    physicality.centroid.component[1] = -0.25;
    physicality.centroid.component[2] = 0.5;
    physicality.centroid.component[3] = -0.75;
    physicality.radius = 0.875;
    physicality.logical_count = 5u;
    physicality.vertex_count = persistence_carriers.size();
    if (laplace_persistence_physicality_identify(
            &physicality, &physicality.physicality_id) != LAPLACE_PERSISTENCE_OK) {
        return 7;
    }
    laplace_persistence_attestation_record observed{};
    observed.entity_id = entity_a;
    observed.physicality_id = physicality.physicality_id;
    Fill(&observed.source_fingerprint, 0x70u);
    Fill(&observed.context_fingerprint, 0xa0u);
    observed.source_ordinal = 1u;
    observed.flags = LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY;
    observed.attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
    if (laplace_persistence_attestation_identify(
            &observed, &observed.attestation_id) != LAPLACE_PERSISTENCE_OK) {
        return 8;
    }
    std::vector<std::vector<std::uint8_t>> persistence_frames;
    const bool a_first = std::memcmp(
        entity_a.bytes, entity_b.bytes, sizeof(entity_a.bytes)) < 0;
    if (!AppendPersistenceFrame(
            &persistence_frames, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity,
            a_first ? &entity_a : &entity_b,
            a_first ? &witness_a : &witness_b) ||
        !AppendPersistenceFrame(
            &persistence_frames, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity,
            a_first ? &entity_b : &entity_a,
            a_first ? &witness_b : &witness_a) ||
        !AppendPersistenceFrame(
            &persistence_frames, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
            laplace_persistence_frame_encode_physicality, &physicality)) {
        return 9;
    }
    for (std::size_t index = 0; index < persistence_carriers.size(); ++index) {
        if (!AppendPersistenceFrame(
                &persistence_frames,
                LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT,
                laplace_persistence_frame_encode_trajectory_segment,
                &physicality.physicality_id,
                static_cast<std::uint64_t>(index),
                &persistence_carriers[index])) {
            return 10;
        }
    }
    if (!AppendPersistenceFrame(
            &persistence_frames,
            LAPLACE_PERSISTENCE_RECORD_ATTESTATION,
            laplace_persistence_frame_encode_attestation, &observed)) {
        return 11;
    }
    const std::array<std::uint32_t, 11> plans{{
        LAPLACE_PERSISTENCE_PG_PLAN_REFERENCE_PREFLIGHT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_ATTESTATION_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_ATTESTATION_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_CONSENSUS_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_CONSENSUS_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_VERIFY}};
    laplace_digest256 plan_fingerprint{};
    if (laplace_persistence_plan_sequence_fingerprint(
            plans.data(), plans.size(), &plan_fingerprint) !=
        LAPLACE_PERSISTENCE_OK) {
        return 12;
    }
    laplace_digest256 persistence_source{};
    laplace_digest256 persistence_recipe{};
    Fill(&persistence_source, 0xc0u);
    Fill(&persistence_recipe, 0xe0u);
    PrintDigest("PERSISTENCE_SOURCE", persistence_source);
    PrintDigest("PERSISTENCE_RECIPE", persistence_recipe);
    PrintHex("PERSISTENCE_ENTITY_A", entity_a.bytes);
    PrintDigest("PERSISTENCE_ENTITY_A_WITNESS", witness_a);
    PrintHex("PERSISTENCE_ENTITY_B", entity_b.bytes);
    PrintDigest("PERSISTENCE_ENTITY_B_WITNESS", witness_b);
    PrintDigest("PERSISTENCE_PHYSICALITY", physicality.physicality_id);
    PrintDigest("PERSISTENCE_TRAJECTORY", physicality.trajectory_fingerprint);
    PrintDigest("PERSISTENCE_OCCURRENCE", observed.attestation_id);
    PrintDigest("PERSISTENCE_PLAN_SEQUENCE", plan_fingerprint);
    for (std::size_t index = 0; index < persistence_frames.size(); ++index) {
        const auto name = std::string("PERSISTENCE_FRAME_") +
                          std::to_string(index);
        PrintHex(name.c_str(), persistence_frames[index]);
    }

    laplace_evidence_lineage_record evidence_root{};
    evidence_root.proposition_id = entity_a;
    evidence_root.occurrence_id = observed.attestation_id;
    Fill(&evidence_root.source_id, 0x11u);
    Fill(&evidence_root.context_id, 0x21u);
    evidence_root.source_ordinal = 1u;
    evidence_root.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
    evidence_root.epistemic_kind = LAPLACE_EVIDENCE_KIND_OBSERVED;
    if (laplace_evidence_node_identify(
            &evidence_root, &evidence_root.node_id) !=
        LAPLACE_EVIDENCE_LINEAGE_OK) {
        return 33;
    }
    auto evidence_copy = evidence_root;
    Fill(&evidence_copy.source_id, 0x31u);
    Fill(&evidence_copy.context_id, 0x41u);
    evidence_copy.source_ordinal = 2u;
    evidence_copy.epistemic_kind = LAPLACE_EVIDENCE_KIND_TESTIMONY;
    if (laplace_evidence_node_identify(
            &evidence_copy, &evidence_copy.node_id) !=
        LAPLACE_EVIDENCE_LINEAGE_OK) {
        return 34;
    }
    auto evidence_independent = evidence_root;
    Fill(&evidence_independent.source_id, 0x51u);
    Fill(&evidence_independent.context_id, 0x61u);
    evidence_independent.source_ordinal = 3u;
    if (laplace_evidence_node_identify(
            &evidence_independent, &evidence_independent.node_id) !=
        LAPLACE_EVIDENCE_LINEAGE_OK) {
        return 35;
    }
    std::array<laplace_evidence_lineage_record, 3> evidence_nodes{{
        evidence_root, evidence_copy, evidence_independent}};
    std::sort(evidence_nodes.begin(), evidence_nodes.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.node_id.bytes, right.node_id.bytes,
                           sizeof(left.node_id.bytes)) < 0;
    });
    laplace_evidence_lineage_record evidence_edge{};
    evidence_edge.node_id = evidence_copy.node_id;
    evidence_edge.parent_node_id = evidence_root.node_id;
    evidence_edge.record_kind = LAPLACE_EVIDENCE_RECORD_DEPENDENCE_EDGE;
    std::array<laplace_evidence_lineage_record, 4> evidence_records{{
        evidence_nodes[0], evidence_nodes[1], evidence_nodes[2], evidence_edge}};
    std::array<laplace_evidence_root_record, 3> evidence_outputs{};
    std::array<laplace_isa_value_view, 2> evidence_values{{
        {evidence_records.data(), evidence_records.size(), evidence_records.size(),
         static_cast<std::uint32_t>(sizeof(evidence_records[0])),
         LAPLACE_ISA_VALUE_EVIDENCE_LINEAGE_RECORD_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {evidence_outputs.data(), 0u, evidence_outputs.size(),
         static_cast<std::uint32_t>(sizeof(evidence_outputs[0])),
         LAPLACE_ISA_VALUE_EVIDENCE_ROOT_RECORD_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction evidence_instruction{
        LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_LINEAGE_BATCH,
        0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_LINEAGE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto evidence_context = laplace_test_context(0u);
    evidence_context.resource_grant.memory_bytes = UINT64_C(16777216);
    evidence_context.major = 1u;
    evidence_context.minor = 2u;
    evidence_context.flags = 0u;
    auto evidence_program = Program(&evidence_instruction, evidence_values.data());
    evidence_program.context = &evidence_context;
    laplace_isa_receipt evidence_isa_receipt{};
    laplace_isa_error evidence_isa_error{};
    if (laplace_isa_execute(
            &evidence_program, &evidence_isa_receipt, &evidence_isa_error) !=
        LAPLACE_ISA_OK) {
        return 36;
    }
    laplace_evidence_lineage_receipt evidence_lineage_receipt{};
    laplace_evidence_lineage_error evidence_lineage_error{};
    std::size_t evidence_output_count = 0u;
    if (laplace_evidence_record_lineage_batch(
            evidence_records.data(), evidence_records.size(), UINT64_C(1048576),
            evidence_outputs.data(), evidence_outputs.size(), &evidence_output_count,
            &evidence_lineage_receipt, &evidence_lineage_error) !=
            LAPLACE_EVIDENCE_LINEAGE_OK || evidence_output_count != 3u) {
        return 37;
    }
    PrintDigest("EVIDENCE_ROOT_NODE", evidence_root.node_id);
    PrintDigest("EVIDENCE_COPY_NODE", evidence_copy.node_id);
    PrintDigest("EVIDENCE_INDEPENDENT_NODE", evidence_independent.node_id);
    PrintDigest("EVIDENCE_ROOT_SOURCE", evidence_root.source_id);
    PrintDigest("EVIDENCE_ROOT_CONTEXT", evidence_root.context_id);
    PrintDigest("EVIDENCE_COPY_SOURCE", evidence_copy.source_id);
    PrintDigest("EVIDENCE_COPY_CONTEXT", evidence_copy.context_id);
    PrintDigest("EVIDENCE_INDEPENDENT_SOURCE", evidence_independent.source_id);
    PrintDigest("EVIDENCE_INDEPENDENT_CONTEXT", evidence_independent.context_id);
    PrintDigest("EVIDENCE_LINEAGE_RECEIPT", evidence_lineage_receipt.receipt_id);
    PrintDigest("EVIDENCE_LINEAGE_INPUT", evidence_lineage_receipt.input_fingerprint);
    PrintDigest("EVIDENCE_LINEAGE_OUTPUT", evidence_lineage_receipt.output_fingerprint);
    PrintDigest("EVIDENCE_ISA_RECEIPT", evidence_isa_receipt.receipt_id);

    laplace_digest256 testimony_profile{};
    laplace_digest256 testimony_recipe{};
    laplace_digest256 testimony_trust{};
    Fill(&testimony_profile, 0x71u);
    Fill(&testimony_recipe, 0x72u);
    Fill(&testimony_trust, 0x73u);
    auto make_testimony = [&](const laplace_digest256& node,
                              std::uint8_t outcome_seed,
                              std::uint32_t source_type,
                              std::uint32_t outcome_type,
                              std::uint32_t disposition,
                              std::uint64_t numerator,
                              std::uint64_t denominator,
                              std::uint64_t samples) {
        laplace_evidence_testimony_record record{};
        record.evidence_node_id = node;
        record.source_profile_id = testimony_profile;
        record.recipe_receipt_id = testimony_recipe;
        record.trust_input_id = testimony_trust;
        Fill(&record.outcome_detail_id, outcome_seed);
        record.uncertainty_numerator = numerator;
        record.uncertainty_denominator = denominator;
        record.sample_count = samples;
        record.source_type = source_type;
        record.outcome_type = outcome_type;
        record.disposition = disposition;
        record.flags = LAPLACE_EVIDENCE_TESTIMONY_FLAGS_NONE;
        if (laplace_evidence_testimony_identify(
                &record, &record.testimony_id) !=
            LAPLACE_EVIDENCE_TESTIMONY_OK) {
            std::exit(38);
        }
        return record;
    };
    std::array<laplace_evidence_testimony_record, 3> testimony_records{{
        make_testimony(
            evidence_root.node_id, 0x81u,
            LAPLACE_EVIDENCE_SOURCE_STANDARD,
            LAPLACE_EVIDENCE_OUTCOME_MAPPING,
            LAPLACE_EVIDENCE_DISPOSITION_PERSISTED, 0u, 1u, 1u),
        make_testimony(
            evidence_copy.node_id, 0x91u,
            LAPLACE_EVIDENCE_SOURCE_CORPUS,
            LAPLACE_EVIDENCE_OUTCOME_ASSERTION,
            LAPLACE_EVIDENCE_DISPOSITION_UNSUPPORTED, 1u, 4u, 10u),
        make_testimony(
            evidence_independent.node_id, 0xa1u,
            LAPLACE_EVIDENCE_SOURCE_DIRECT_OBSERVATION,
            LAPLACE_EVIDENCE_OUTCOME_MEASUREMENT,
            LAPLACE_EVIDENCE_DISPOSITION_PERSISTED, 1u, 10u, 2u)}};
    std::sort(
        testimony_records.begin(), testimony_records.end(),
        [](const auto& left, const auto& right) {
            return std::memcmp(
                left.testimony_id.bytes, right.testimony_id.bytes,
                sizeof(left.testimony_id.bytes)) < 0;
        });
    laplace_evidence_testimony_receipt testimony_output{};
    std::array<laplace_isa_value_view, 2> testimony_values{{
        {testimony_records.data(), testimony_records.size(), testimony_records.size(),
         static_cast<std::uint32_t>(sizeof(testimony_records[0])),
         LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECORD_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {&testimony_output, 0u, 1u,
         static_cast<std::uint32_t>(sizeof(testimony_output)),
         LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction testimony_instruction{
        LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_TESTIMONY_BATCH,
        0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_TESTIMONY_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto testimony_program = Program(&testimony_instruction, testimony_values.data());
    testimony_program.context = &evidence_context;
    laplace_isa_receipt testimony_isa_receipt{};
    laplace_isa_error testimony_isa_error{};
    if (laplace_isa_execute(
            &testimony_program, &testimony_isa_receipt, &testimony_isa_error) !=
            LAPLACE_ISA_OK || testimony_values[1].count != 1u) {
        return 39;
    }
    laplace_evidence_testimony_receipt testimony_native_receipt{};
    laplace_evidence_testimony_error testimony_native_error{};
    if (laplace_evidence_record_testimony_batch(
            testimony_records.data(), testimony_records.size(),
            &testimony_native_receipt, &testimony_native_error) !=
            LAPLACE_EVIDENCE_TESTIMONY_OK ||
        std::memcmp(
            &testimony_output, &testimony_native_receipt,
            sizeof(testimony_output)) != 0) {
        return 40;
    }
    PrintDigest("TESTIMONY_PROFILE", testimony_profile);
    PrintDigest("TESTIMONY_RECIPE", testimony_recipe);
    PrintDigest("TESTIMONY_TRUST", testimony_trust);
    for (std::size_t index = 0; index < testimony_records.size(); ++index) {
        const auto prefix = std::string("TESTIMONY_") + std::to_string(index);
        PrintDigest((prefix + "_ID").c_str(), testimony_records[index].testimony_id);
        PrintDigest((prefix + "_NODE").c_str(), testimony_records[index].evidence_node_id);
        PrintDigest((prefix + "_OUTCOME").c_str(), testimony_records[index].outcome_detail_id);
        std::cout << prefix << "_SOURCE_TYPE=" << std::dec
                  << testimony_records[index].source_type << '\n';
        std::cout << prefix << "_OUTCOME_TYPE="
                  << testimony_records[index].outcome_type << '\n';
        std::cout << prefix << "_DISPOSITION="
                  << testimony_records[index].disposition << '\n';
        std::cout << prefix << "_UNCERTAINTY_NUMERATOR="
                  << testimony_records[index].uncertainty_numerator << '\n';
        std::cout << prefix << "_UNCERTAINTY_DENOMINATOR="
                  << testimony_records[index].uncertainty_denominator << '\n';
        std::cout << prefix << "_SAMPLE_COUNT="
                  << testimony_records[index].sample_count << '\n';
    }
    PrintDigest("TESTIMONY_RECEIPT", testimony_native_receipt.receipt_id);
    PrintDigest("TESTIMONY_INPUT", testimony_native_receipt.input_fingerprint);
    PrintDigest("TESTIMONY_OUTPUT", testimony_native_receipt.output_fingerprint);
    PrintDigest("TESTIMONY_ISA_RECEIPT", testimony_isa_receipt.receipt_id);

    laplace_digest256 source_profile_boundary{};
    Repeat(&source_profile_boundary, 0xd0u);
    auto make_source_profile = [&](std::uint8_t scope_seed,
                                   std::uint8_t fingerprint_seed) {
        laplace_source_profile_manifest profile{};
        profile.coordinate.kind = LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE;
        profile.coordinate.authority = HighwayId(scope_seed);
        profile.coordinate.release = HighwayId(
            static_cast<std::uint8_t>(scope_seed + 0x20u));
        profile.coordinate.name_space = HighwayId(
            static_cast<std::uint8_t>(scope_seed + 0x40u));
        profile.coordinate.local_identifier = HighwayId(
            static_cast<std::uint8_t>(scope_seed + 0x60u));
        profile.coordinate.version = 1u;
        Repeat(&profile.authority_release_fingerprint, fingerprint_seed);
        Repeat(&profile.license_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 1u));
        Repeat(&profile.artifact_graph_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 2u));
        Repeat(&profile.syntax_authority_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 3u));
        Repeat(&profile.recipe_program_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 4u));
        Repeat(&profile.universal_ast_mapping_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 5u));
        Repeat(&profile.highway_references_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 6u));
        Repeat(&profile.epistemic_witnessing_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 7u));
        Repeat(&profile.denominator_declaration_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 8u));
        Repeat(&profile.conformance_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 9u));
        Repeat(&profile.completion_law_fingerprint,
               static_cast<std::uint8_t>(fingerprint_seed + 10u));
        profile.selected_boundary_fingerprint = source_profile_boundary;
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
            std::exit(41);
        }
        return profile;
    };
    const auto source_profile_a = make_source_profile(0x10u, 0xa0u);
    const auto source_profile_b = make_source_profile(0x11u, 0xb0u);
    std::array<laplace_source_profile_manifest, 2> source_profiles{{
        source_profile_a, source_profile_b}};
    std::sort(
        source_profiles.begin(), source_profiles.end(),
        [](const auto& left, const auto& right) {
            return std::memcmp(
                left.profile_id.bytes, right.profile_id.bytes,
                sizeof(left.profile_id.bytes)) < 0;
        });
    laplace_source_profile_receipt source_profile_output{};
    std::array<laplace_isa_value_view, 2> source_profile_values{{
        {source_profiles.data(), source_profiles.size(), source_profiles.size(),
         static_cast<std::uint32_t>(sizeof(source_profiles[0])),
         LAPLACE_ISA_VALUE_SOURCE_PROFILE_MANIFEST_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {&source_profile_output, 0u, 1u,
         static_cast<std::uint32_t>(sizeof(source_profile_output)),
         LAPLACE_ISA_VALUE_SOURCE_PROFILE_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction source_profile_instruction{
        LAPLACE_ISA_OPCODE_SOURCE_PROFILE_VALIDATE_BATCH,
        0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_SOURCE_PROFILE_VALIDATE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto source_profile_program = Program(
        &source_profile_instruction, source_profile_values.data());
    source_profile_program.context = &evidence_context;
    laplace_isa_receipt source_profile_isa_receipt{};
    laplace_isa_error source_profile_isa_error{};
    if (laplace_isa_execute(
            &source_profile_program, &source_profile_isa_receipt,
            &source_profile_isa_error) != LAPLACE_ISA_OK ||
        source_profile_values[1].count != 1u) {
        return 42;
    }
    laplace_source_profile_receipt source_profile_native_receipt{};
    laplace_source_profile_error source_profile_native_error{};
    if (laplace_source_profile_validate_batch(
            source_profiles.data(), source_profiles.size(),
            &source_profile_native_receipt, &source_profile_native_error) !=
            LAPLACE_SOURCE_PROFILE_OK ||
        std::memcmp(
            &source_profile_output, &source_profile_native_receipt,
            sizeof(source_profile_output)) != 0) {
        return 43;
    }
    PrintDigest("SOURCE_PROFILE_A_ID", source_profile_a.profile_id);
    PrintDigest("SOURCE_PROFILE_B_ID", source_profile_b.profile_id);
    PrintDigest("SOURCE_PROFILE_RECEIPT", source_profile_native_receipt.receipt_id);
    PrintDigest("SOURCE_PROFILE_BOUNDARY",
                source_profile_native_receipt.selected_boundary_fingerprint);
    PrintDigest("SOURCE_PROFILE_INPUT",
                source_profile_native_receipt.input_fingerprint);
    PrintDigest("SOURCE_PROFILE_OUTPUT",
                source_profile_native_receipt.output_fingerprint);
    PrintDigest("SOURCE_PROFILE_ISA_RECEIPT", source_profile_isa_receipt.receipt_id);

    auto make_mapping_coordinate = [](std::uint8_t seed) {
        laplace_highway_key key{};
        key.kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
        key.authority = HighwayId(static_cast<std::uint8_t>(seed + 0x10u));
        key.release = HighwayId(static_cast<std::uint8_t>(seed + 0x20u));
        key.name_space = HighwayId(static_cast<std::uint8_t>(seed + 0x30u));
        key.local_identifier = HighwayId(
            static_cast<std::uint8_t>(seed + 0x40u));
        key.version = 1u;
        laplace_highway_coordinate coordinate{};
        if (laplace_highway_coordinate_calculate(&key, &coordinate) !=
            LAPLACE_HIGHWAY_OK) {
            std::exit(44);
        }
        return coordinate;
    };
    const std::array<laplace_highway_coordinate, 3> mapping_coordinates{{
        make_mapping_coordinate(0x01u),
        make_mapping_coordinate(0x02u),
        make_mapping_coordinate(0x03u)}};
    auto make_mapping_candidate = [&](std::uint8_t witness,
                                      const laplace_digest256& profile_id,
                                      const laplace_digest256& left_reference_id,
                                      const laplace_digest256& right_reference_id,
                                      std::size_t right_coordinate,
                                      std::uint64_t source_ordinal,
                                      std::uint64_t row_ordinal) {
        laplace_reference_mapping_candidate candidate{};
        candidate.boundary_id = source_profile_boundary;
        candidate.source_profile_id = profile_id;
        candidate.left_reference_id = left_reference_id;
        candidate.right_reference_id = right_reference_id;
        candidate.left_coordinate = mapping_coordinates[0];
        candidate.right_coordinate = mapping_coordinates[right_coordinate];
        std::memset(candidate.relation_id.bytes, 0x50, 16u);
        std::memset(candidate.row_entity_id.bytes, 0x60 + witness, 16u);
        std::memset(candidate.left_field_entity_id.bytes, 0x70, 16u);
        std::memset(candidate.left_value_entity_id.bytes, 0x80 + witness, 16u);
        std::memset(candidate.right_field_entity_id.bytes, 0x90, 16u);
        std::memset(candidate.right_value_entity_id.bytes, 0xa0 + witness, 16u);
        candidate.source_ordinal = source_ordinal;
        candidate.artifact_ordinal = 1u;
        candidate.row_ordinal = row_ordinal;
        candidate.relation_version = 1u;
        candidate.relation_kind = LAPLACE_HIGHWAY_KIND_RELATION;
        candidate.flags = LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED;
        candidate.left_disposition = LAPLACE_REFERENCE_DISPOSITION_PRESENT;
        candidate.right_disposition =
            right_coordinate == 2u
                ? LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED
                : LAPLACE_REFERENCE_DISPOSITION_PRESENT;
        return candidate;
    };
    laplace_digest256 mapping_left_a{};
    laplace_digest256 mapping_right_a{};
    laplace_digest256 mapping_left_b{};
    laplace_digest256 mapping_right_b{};
    laplace_digest256 mapping_left_unresolved{};
    laplace_digest256 mapping_right_unresolved{};
    Repeat(&mapping_left_a, 0x31u);
    Repeat(&mapping_right_a, 0x41u);
    Repeat(&mapping_left_b, 0x32u);
    Repeat(&mapping_right_b, 0x42u);
    Repeat(&mapping_left_unresolved, 0x33u);
    Repeat(&mapping_right_unresolved, 0x43u);
    std::array<laplace_reference_mapping_candidate, 3> mapping_candidates{{
        make_mapping_candidate(
            0u, source_profile_a.profile_id,
            mapping_left_a, mapping_right_a, 1u, 1u, 1u),
        make_mapping_candidate(
            1u, source_profile_b.profile_id,
            mapping_left_b, mapping_right_b, 1u, 1u, 1u),
        make_mapping_candidate(
            2u, source_profile_a.profile_id,
            mapping_left_unresolved, mapping_right_unresolved, 2u, 2u, 2u)}};
    std::array<laplace_reference_mapping_record, 3> mapping_records{};
    laplace_reference_mapping_receipt mapping_native_receipt{};
    laplace_reference_mapping_error mapping_native_error{};
    if (laplace_reference_mapping_resolve_batch(
            mapping_candidates.data(), mapping_candidates.size(),
            mapping_records.data(), &mapping_native_receipt,
            &mapping_native_error) != LAPLACE_REFERENCE_MAPPING_OK) {
        return 44;
    }
    std::array<laplace_isa_value_view, 2> mapping_values{{
        {mapping_candidates.data(), mapping_candidates.size(),
         mapping_candidates.size(),
         static_cast<std::uint32_t>(sizeof(mapping_candidates[0])),
         LAPLACE_ISA_VALUE_REFERENCE_MAPPING_CANDIDATE_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {mapping_records.data(), 0u, mapping_records.size(),
         static_cast<std::uint32_t>(sizeof(mapping_records[0])),
         LAPLACE_ISA_VALUE_REFERENCE_MAPPING_RECORD_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction mapping_instruction{
        LAPLACE_ISA_OPCODE_REFERENCE_MAPPING_RESOLVE_BATCH,
        0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_REFERENCE_MAPPING_RESOLVE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    auto mapping_program = Program(
        &mapping_instruction, mapping_values.data());
    mapping_program.context = &evidence_context;
    laplace_isa_receipt mapping_isa_receipt{};
    laplace_isa_error mapping_isa_error{};
    if (laplace_isa_execute(
            &mapping_program, &mapping_isa_receipt, &mapping_isa_error) !=
            LAPLACE_ISA_OK || mapping_values[1].count != 3u) {
        return 45;
    }
    laplace_reference_mapping_receipt mapping_reconstructed_receipt{};
    std::array<laplace_reference_mapping_record, 3>
        mapping_reconstructed_records{};
    if (laplace_reference_mapping_resolve_batch(
            mapping_candidates.data(), mapping_candidates.size(),
            mapping_reconstructed_records.data(),
            &mapping_reconstructed_receipt, nullptr) !=
            LAPLACE_REFERENCE_MAPPING_OK ||
        std::memcmp(mapping_records.data(), mapping_reconstructed_records.data(),
                    sizeof(mapping_records)) != 0 ||
        std::memcmp(&mapping_native_receipt, &mapping_reconstructed_receipt,
                    sizeof(mapping_native_receipt)) != 0) {
        return 46;
    }
    for (std::size_t index = 0u; index < mapping_records.size(); ++index) {
        const std::string suffix = std::to_string(index);
        PrintDigest(
            ("REFERENCE_MAPPING_ID_" + suffix).c_str(),
            mapping_records[index].mapping_id);
        PrintDigest(
            ("REFERENCE_MAPPING_PROPOSITION_" + suffix).c_str(),
            mapping_records[index].proposition_id);
        PrintDigest(
            ("REFERENCE_MAPPING_OCCURRENCE_" + suffix).c_str(),
            mapping_records[index].occurrence_id);
    }
    PrintDigest(
        "REFERENCE_MAPPING_RECEIPT", mapping_native_receipt.receipt_id);
    PrintDigest(
        "REFERENCE_MAPPING_BOUNDARY", mapping_native_receipt.boundary_id);
    PrintDigest(
        "REFERENCE_MAPPING_INPUT", mapping_native_receipt.input_fingerprint);
    PrintDigest(
        "REFERENCE_MAPPING_OUTPUT", mapping_native_receipt.output_fingerprint);
    PrintDigest("REFERENCE_MAPPING_ISA_RECEIPT", mapping_isa_receipt.receipt_id);

    const laplace_id128 zero_entity{};
    const laplace_digest256 zero_witness{};
    std::array<laplace_trajectory_carrier, 1> zero_carriers{};
    if (laplace_trajectory_composition_encode(
            &zero_entity, 1u, 1u, 0u, &zero_carriers[0]) !=
        LAPLACE_TRAJECTORY_OK) {
        return 18;
    }
    laplace_persistence_physicality_record zero_physicality{};
    zero_physicality.entity_id = zero_entity;
    zero_physicality.physicality_type =
        LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    zero_physicality.vertex_class =
        LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    zero_physicality.recipe_version = 1u;
    zero_physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    zero_physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    zero_physicality.logical_count = 1u;
    zero_physicality.vertex_count = 1u;
    if (laplace_persistence_trajectory_fingerprint(
            zero_carriers.data(), zero_carriers.size(),
            &zero_physicality.trajectory_fingerprint) !=
            LAPLACE_PERSISTENCE_OK ||
        laplace_persistence_physicality_identify(
            &zero_physicality, &zero_physicality.physicality_id) !=
            LAPLACE_PERSISTENCE_OK) {
        return 19;
    }
    laplace_persistence_attestation_record zero_occurrence{};
    zero_occurrence.entity_id = zero_entity;
    zero_occurrence.physicality_id = zero_physicality.physicality_id;
    zero_occurrence.source_ordinal = 1u;
    zero_occurrence.flags = LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY;
    zero_occurrence.attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
    if (laplace_persistence_attestation_identify(
            &zero_occurrence, &zero_occurrence.attestation_id) !=
        LAPLACE_PERSISTENCE_OK) {
        return 20;
    }
    std::vector<std::vector<std::uint8_t>> zero_frames;
    if (!AppendPersistenceFrame(
            &zero_frames, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity,
            &zero_entity, &zero_witness) ||
        !AppendPersistenceFrame(
            &zero_frames, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
            laplace_persistence_frame_encode_physicality, &zero_physicality) ||
        !AppendPersistenceFrame(
            &zero_frames, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT,
            laplace_persistence_frame_encode_trajectory_segment,
            &zero_physicality.physicality_id, 0u, &zero_carriers[0]) ||
        !AppendPersistenceFrame(
            &zero_frames, LAPLACE_PERSISTENCE_RECORD_ATTESTATION,
            laplace_persistence_frame_encode_attestation, &zero_occurrence)) {
        return 21;
    }
    PrintHex("PERSISTENCE_ZERO_ENTITY", zero_entity.bytes);
    PrintDigest("PERSISTENCE_ZERO_PHYSICALITY", zero_physicality.physicality_id);
    PrintDigest("PERSISTENCE_ZERO_OCCURRENCE", zero_occurrence.attestation_id);
    for (std::size_t index = 0; index < zero_frames.size(); ++index) {
        const auto name = std::string("PERSISTENCE_ZERO_FRAME_") +
                          std::to_string(index);
        PrintHex(name.c_str(), zero_frames[index]);
    }

    auto concurrent_physicality = physicality;
    Fill(&concurrent_physicality.recipe_fingerprint, 0x20u);
    if (laplace_persistence_physicality_identify(
            &concurrent_physicality,
            &concurrent_physicality.physicality_id) != LAPLACE_PERSISTENCE_OK) {
        return 13;
    }
    auto concurrent_occurrence = observed;
    concurrent_occurrence.physicality_id = concurrent_physicality.physicality_id;
    concurrent_occurrence.source_ordinal = 2u;
    if (laplace_persistence_attestation_identify(
            &concurrent_occurrence,
            &concurrent_occurrence.attestation_id) != LAPLACE_PERSISTENCE_OK) {
        return 14;
    }
    std::vector<std::vector<std::uint8_t>> concurrent_frames;
    if (!AppendPersistenceFrame(
            &concurrent_frames, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity,
            a_first ? &entity_a : &entity_b,
            a_first ? &witness_a : &witness_b) ||
        !AppendPersistenceFrame(
            &concurrent_frames, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity,
            a_first ? &entity_b : &entity_a,
            a_first ? &witness_b : &witness_a) ||
        !AppendPersistenceFrame(
            &concurrent_frames, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
            laplace_persistence_frame_encode_physicality,
            &concurrent_physicality)) {
        return 15;
    }
    for (std::size_t index = 0; index < persistence_carriers.size(); ++index) {
        if (!AppendPersistenceFrame(
                &concurrent_frames,
                LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT,
                laplace_persistence_frame_encode_trajectory_segment,
                &concurrent_physicality.physicality_id,
                static_cast<std::uint64_t>(index),
                &persistence_carriers[index])) {
            return 16;
        }
    }
    if (!AppendPersistenceFrame(
            &concurrent_frames,
            LAPLACE_PERSISTENCE_RECORD_ATTESTATION,
            laplace_persistence_frame_encode_attestation,
            &concurrent_occurrence)) {
        return 17;
    }
    PrintDigest(
        "PERSISTENCE_CONCURRENT_PHYSICALITY",
        concurrent_physicality.physicality_id);
    PrintDigest(
        "PERSISTENCE_CONCURRENT_OCCURRENCE",
        concurrent_occurrence.attestation_id);
    for (std::size_t index = 0; index < concurrent_frames.size(); ++index) {
        const auto name = std::string("PERSISTENCE_CONCURRENT_FRAME_") +
                          std::to_string(index);
        PrintHex(name.c_str(), concurrent_frames[index]);
    }

    constexpr std::size_t bulk_vertex_count = 4096u;
    std::vector<laplace_trajectory_carrier> bulk_carriers(bulk_vertex_count);
    for (std::size_t index = 0; index < bulk_carriers.size(); ++index) {
        const bool rare = index == 123u;
        const auto* entity = rare ? &entity_a : &entity_b;
        const auto atom = rare ? 0x41u : 0x42u;
        if (laplace_trajectory_composition_encode(
                entity, static_cast<std::uint64_t>(index + 1u), 1u,
                Metadata(2u, atom), &bulk_carriers[index]) !=
            LAPLACE_TRAJECTORY_OK) {
            return 18;
        }
    }
    auto bulk_physicality = physicality;
    Fill(&bulk_physicality.recipe_fingerprint, 0x30u);
    if (laplace_persistence_trajectory_fingerprint(
            bulk_carriers.data(), bulk_carriers.size(),
            &bulk_physicality.trajectory_fingerprint) != LAPLACE_PERSISTENCE_OK) {
        return 19;
    }
    bulk_physicality.centroid.component[0] = -0.125;
    bulk_physicality.logical_count = bulk_carriers.size();
    bulk_physicality.vertex_count = bulk_carriers.size();
    if (laplace_persistence_physicality_identify(
            &bulk_physicality, &bulk_physicality.physicality_id) !=
        LAPLACE_PERSISTENCE_OK) {
        return 20;
    }
    auto bulk_occurrence = observed;
    bulk_occurrence.physicality_id = bulk_physicality.physicality_id;
    bulk_occurrence.source_ordinal = 3u;
    if (laplace_persistence_attestation_identify(
            &bulk_occurrence, &bulk_occurrence.attestation_id) !=
        LAPLACE_PERSISTENCE_OK) {
        return 21;
    }
    std::vector<std::vector<std::uint8_t>> bulk_frames;
    if (!AppendPersistenceFrame(
            &bulk_frames, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity,
            a_first ? &entity_a : &entity_b,
            a_first ? &witness_a : &witness_b) ||
        !AppendPersistenceFrame(
            &bulk_frames, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity,
            a_first ? &entity_b : &entity_a,
            a_first ? &witness_b : &witness_a) ||
        !AppendPersistenceFrame(
            &bulk_frames, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
            laplace_persistence_frame_encode_physicality, &bulk_physicality)) {
        return 22;
    }
    for (std::size_t index = 0; index < bulk_carriers.size(); ++index) {
        if (!AppendPersistenceFrame(
                &bulk_frames,
                LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT,
                laplace_persistence_frame_encode_trajectory_segment,
                &bulk_physicality.physicality_id,
                static_cast<std::uint64_t>(index), &bulk_carriers[index])) {
            return 23;
        }
    }
    if (!AppendPersistenceFrame(
            &bulk_frames,
            LAPLACE_PERSISTENCE_RECORD_ATTESTATION,
            laplace_persistence_frame_encode_attestation, &bulk_occurrence)) {
        return 24;
    }
    std::vector<std::uint8_t> bulk_stream;
    for (const auto& frame : bulk_frames) {
        bulk_stream.insert(bulk_stream.end(), frame.begin(), frame.end());
    }
    PrintDigest("PERSISTENCE_BULK_PHYSICALITY", bulk_physicality.physicality_id);
    PrintDigest("PERSISTENCE_BULK_OCCURRENCE", bulk_occurrence.attestation_id);
    PrintHex("PERSISTENCE_BULK_STREAM", bulk_stream);

    auto composition_context = laplace_test_context(0U);
    composition_context.flags = 0U;
    composition_context.resource_grant.memory_bytes = UINT64_C(67108864);
    std::array<laplace_composition_known_entity, 2> composition_known{};
    composition_known[0].entity_id = entity_a;
    composition_known[0].identity_witness = witness_a;
    Repeat(&composition_known[0].physicality_id, 0xe1U);
    composition_known[0].centroid.component[0] = 1.0;
    composition_known[1].entity_id = entity_b;
    composition_known[1].identity_witness = witness_b;
    Repeat(&composition_known[1].physicality_id, 0xe2U);
    composition_known[1].centroid.component[1] = 1.0;
    const std::array<laplace_composition_operand, 2> composition_operands{{
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {1U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U}}};
    laplace_composition_request composition_request{};
    composition_request.first_operand = 0U;
    composition_request.operand_count = composition_operands.size();
    composition_request.source_ordinal = 1U;
    composition_request.recipe_version = 1U;
    Repeat(&composition_request.recipe_fingerprint, 0xb1U);
    Repeat(&composition_request.geometry_epoch, 0xc1U);
    Repeat(&composition_request.occurrence_context_fingerprint, 0xd1U);
    laplace_digest256 composition_source{};
    laplace_digest256 composition_recipe{};
    Repeat(&composition_source, 0x91U);
    Repeat(&composition_recipe, 0xa1U);
    const laplace_composition_working_set_input composition_input{
        &composition_context,
        &composition_source,
        &composition_recipe,
        composition_known.data(),
        composition_known.size(),
        composition_operands.data(),
        composition_operands.size(),
        &composition_request,
        1U,
        256U,
        0U};
    laplace_composition_working_set* composition_working_set = nullptr;
    if (laplace_composition_working_set_create(
            &composition_input, &composition_working_set) !=
            LAPLACE_COMPOSITION_OK ||
        composition_working_set == nullptr) {
        return 25;
    }
    laplace_composition_presence_provider_v1 composition_provider{};
    composition_provider.resolve = ResolveNovelCompositionPresence;
    composition_provider.abi_major =
        LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI;
    composition_provider.abi_minor = LAPLACE_COMPOSITION_ABI_MINOR;
    laplace_composition_presence_receipt composition_presence{};
    if (laplace_composition_working_set_resolve_presence(
            composition_working_set, &composition_provider,
            &composition_presence) != LAPLACE_COMPOSITION_OK) {
        laplace_composition_working_set_destroy(&composition_working_set);
        return 26;
    }
    laplace_composition_working_set_summary composition_summary{};
    if (laplace_composition_working_set_summary_get(
            composition_working_set, &composition_summary) !=
            LAPLACE_COMPOSITION_OK) {
        laplace_composition_working_set_destroy(&composition_working_set);
        return 27;
    }
    std::size_t composition_result_count = 0U;
    const auto* composition_results = laplace_composition_working_set_results(
        composition_working_set, &composition_result_count);
    std::size_t entity_disposition_count = 0U;
    const auto* entity_dispositions =
        laplace_composition_working_set_entity_dispositions(
            composition_working_set, &entity_disposition_count);
    std::size_t physicality_disposition_count = 0U;
    const auto* physicality_dispositions =
        laplace_composition_working_set_physicality_dispositions(
            composition_working_set, &physicality_disposition_count);
    if (composition_results == nullptr || composition_result_count != 1U ||
        entity_dispositions == nullptr || entity_disposition_count == 0U ||
        physicality_dispositions == nullptr ||
        physicality_disposition_count == 0U) {
        laplace_composition_working_set_destroy(&composition_working_set);
        return 28;
    }
    const std::vector<std::uint8_t> entity_disposition_bytes(
        entity_dispositions,
        entity_dispositions + entity_disposition_count);
    const std::vector<std::uint8_t> physicality_disposition_bytes(
        physicality_dispositions,
        physicality_dispositions + physicality_disposition_count);
    const std::array<std::uint8_t, 1> composition_tier{{
        static_cast<std::uint8_t>(composition_results[0].tier_floor)}};
    PrintHex("COMPOSITION_RESULT_ENTITY", composition_results[0].entity_id.bytes);
    PrintDigest(
        "COMPOSITION_RESULT_PHYSICALITY",
        composition_results[0].physicality_id);
    PrintHex("COMPOSITION_RESULT_TIER", composition_tier);
    PrintDigest("COMPOSITION_WORKING_SET_RECEIPT", composition_summary.receipt_id);
    PrintDigest(
        "COMPOSITION_PRESENCE_SEMANTIC_RECEIPT",
        composition_presence.semantic_receipt_id);
    PrintDigest(
        "COMPOSITION_PRESENCE_EXECUTION_RECEIPT",
        composition_presence.execution_receipt_id);
    PrintDigest(
        "COMPOSITION_PRESENCE_CANDIDATE_FINGERPRINT",
        composition_presence.candidate_fingerprint);
    PrintDigest(
        "COMPOSITION_PRESENCE_DISPOSITION_FINGERPRINT",
        composition_presence.disposition_fingerprint);
    PrintDigest("COMPOSITION_STREAM_FINGERPRINT", composition_summary.stream_fingerprint);
    PrintHex("COMPOSITION_ENTITY_DISPOSITIONS", entity_disposition_bytes);
    PrintHex("COMPOSITION_PHYSICALITY_DISPOSITIONS", physicality_disposition_bytes);

    auto world_profile = source_profile_a;
    world_profile.recipe_program_fingerprint = composition_recipe;
    world_profile.occurrence_count = composition_summary.logical_occurrence_count;
    world_profile.claim_count = 1u;
    world_profile.not_applicable_mask &= ~(UINT64_C(1) << 11u);
    if (laplace_source_profile_identify(
            &world_profile, &world_profile.profile_id) !=
        LAPLACE_SOURCE_PROFILE_OK) {
        laplace_composition_working_set_destroy(&composition_working_set);
        return 44;
    }
    laplace_persistence_attestation_record world_occurrence{};
    world_occurrence.entity_id = composition_results[0].entity_id;
    world_occurrence.physicality_id = composition_results[0].physicality_id;
    world_occurrence.source_fingerprint = composition_source;
    world_occurrence.context_fingerprint =
        composition_request.occurrence_context_fingerprint;
    world_occurrence.source_ordinal = composition_request.source_ordinal;
    world_occurrence.flags = LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY;
    world_occurrence.attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
    if (laplace_persistence_attestation_identify(
            &world_occurrence, &world_occurrence.attestation_id) !=
        LAPLACE_PERSISTENCE_OK) {
        laplace_composition_working_set_destroy(&composition_working_set);
        return 45;
    }
    laplace_evidence_lineage_record world_evidence{};
    world_evidence.proposition_id = composition_results[0].entity_id;
    world_evidence.occurrence_id = world_occurrence.attestation_id;
    Repeat(&world_evidence.source_id, 0x44u);
    Repeat(&world_evidence.context_id, 0x45u);
    world_evidence.source_ordinal = 1u;
    world_evidence.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
    world_evidence.epistemic_kind = LAPLACE_EVIDENCE_KIND_OBSERVED;
    if (laplace_evidence_node_identify(
            &world_evidence, &world_evidence.node_id) !=
        LAPLACE_EVIDENCE_LINEAGE_OK) {
        laplace_composition_working_set_destroy(&composition_working_set);
        return 46;
    }
    laplace_evidence_testimony_record world_testimony{};
    world_testimony.evidence_node_id = world_evidence.node_id;
    world_testimony.source_profile_id = world_profile.profile_id;
    world_testimony.recipe_receipt_id = composition_recipe;
    Repeat(&world_testimony.trust_input_id, 0x46u);
    Repeat(&world_testimony.outcome_detail_id, 0x47u);
    world_testimony.uncertainty_numerator = 0u;
    world_testimony.uncertainty_denominator = 1u;
    world_testimony.sample_count = 1u;
    world_testimony.source_type = LAPLACE_EVIDENCE_SOURCE_STANDARD;
    world_testimony.outcome_type = LAPLACE_EVIDENCE_OUTCOME_MAPPING;
    world_testimony.disposition = LAPLACE_EVIDENCE_DISPOSITION_PERSISTED;
    world_testimony.flags = LAPLACE_EVIDENCE_TESTIMONY_FLAGS_NONE;
    if (laplace_evidence_testimony_identify(
            &world_testimony, &world_testimony.testimony_id) !=
        LAPLACE_EVIDENCE_TESTIMONY_OK) {
        laplace_composition_working_set_destroy(&composition_working_set);
        return 47;
    }
    PrintDigest("WORLD_PROFILE_ID", world_profile.profile_id);
    PrintDigest("WORLD_OCCURRENCE_ID", world_occurrence.attestation_id);
    PrintDigest("WORLD_EVIDENCE_NODE", world_evidence.node_id);
    PrintDigest("WORLD_EVIDENCE_SOURCE", world_evidence.source_id);
    PrintDigest("WORLD_EVIDENCE_CONTEXT", world_evidence.context_id);
    PrintDigest("WORLD_TESTIMONY_ID", world_testimony.testimony_id);
    PrintDigest("WORLD_TESTIMONY_TRUST", world_testimony.trust_input_id);
    PrintDigest("WORLD_TESTIMONY_OUTCOME", world_testimony.outcome_detail_id);
    laplace_composition_working_set_destroy(&composition_working_set);
    return 0;
}
