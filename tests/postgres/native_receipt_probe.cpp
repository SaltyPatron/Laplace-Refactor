#include "laplace/isa.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"
#include "../context_fixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
    laplace_persistence_occurrence_record observed{};
    observed.entity_id = entity_a;
    observed.physicality_id = physicality.physicality_id;
    Fill(&observed.source_fingerprint, 0x70u);
    Fill(&observed.context_fingerprint, 0xa0u);
    observed.source_ordinal = 1u;
    observed.flags = LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY;
    if (laplace_persistence_occurrence_identify(
            &observed, &observed.occurrence_id) != LAPLACE_PERSISTENCE_OK) {
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
                LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX,
                laplace_persistence_frame_encode_trajectory,
                &physicality.physicality_id,
                static_cast<std::uint64_t>(index),
                &persistence_carriers[index])) {
            return 10;
        }
    }
    if (!AppendPersistenceFrame(
            &persistence_frames,
            LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE,
            laplace_persistence_frame_encode_occurrence, &observed)) {
        return 11;
    }
    const std::array<std::uint32_t, 11> plans{{
        LAPLACE_PERSISTENCE_PG_PLAN_REFERENCE_PREFLIGHT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_VERIFY,
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
    PrintDigest("PERSISTENCE_OCCURRENCE", observed.occurrence_id);
    PrintDigest("PERSISTENCE_PLAN_SEQUENCE", plan_fingerprint);
    for (std::size_t index = 0; index < persistence_frames.size(); ++index) {
        const auto name = std::string("PERSISTENCE_FRAME_") +
                          std::to_string(index);
        PrintHex(name.c_str(), persistence_frames[index]);
    }

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
    laplace_persistence_occurrence_record zero_occurrence{};
    zero_occurrence.entity_id = zero_entity;
    zero_occurrence.physicality_id = zero_physicality.physicality_id;
    zero_occurrence.source_ordinal = 1u;
    zero_occurrence.flags = LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY;
    if (laplace_persistence_occurrence_identify(
            &zero_occurrence, &zero_occurrence.occurrence_id) !=
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
            &zero_frames, LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX,
            laplace_persistence_frame_encode_trajectory,
            &zero_physicality.physicality_id, 0u, &zero_carriers[0]) ||
        !AppendPersistenceFrame(
            &zero_frames, LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE,
            laplace_persistence_frame_encode_occurrence, &zero_occurrence)) {
        return 21;
    }
    PrintHex("PERSISTENCE_ZERO_ENTITY", zero_entity.bytes);
    PrintDigest("PERSISTENCE_ZERO_PHYSICALITY", zero_physicality.physicality_id);
    PrintDigest("PERSISTENCE_ZERO_OCCURRENCE", zero_occurrence.occurrence_id);
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
    if (laplace_persistence_occurrence_identify(
            &concurrent_occurrence,
            &concurrent_occurrence.occurrence_id) != LAPLACE_PERSISTENCE_OK) {
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
                LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX,
                laplace_persistence_frame_encode_trajectory,
                &concurrent_physicality.physicality_id,
                static_cast<std::uint64_t>(index),
                &persistence_carriers[index])) {
            return 16;
        }
    }
    if (!AppendPersistenceFrame(
            &concurrent_frames,
            LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE,
            laplace_persistence_frame_encode_occurrence,
            &concurrent_occurrence)) {
        return 17;
    }
    PrintDigest(
        "PERSISTENCE_CONCURRENT_PHYSICALITY",
        concurrent_physicality.physicality_id);
    PrintDigest(
        "PERSISTENCE_CONCURRENT_OCCURRENCE",
        concurrent_occurrence.occurrence_id);
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
    if (laplace_persistence_occurrence_identify(
            &bulk_occurrence, &bulk_occurrence.occurrence_id) !=
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
                LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX,
                laplace_persistence_frame_encode_trajectory,
                &bulk_physicality.physicality_id,
                static_cast<std::uint64_t>(index), &bulk_carriers[index])) {
            return 23;
        }
    }
    if (!AppendPersistenceFrame(
            &bulk_frames,
            LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE,
            laplace_persistence_frame_encode_occurrence, &bulk_occurrence)) {
        return 24;
    }
    std::vector<std::uint8_t> bulk_stream;
    for (const auto& frame : bulk_frames) {
        bulk_stream.insert(bulk_stream.end(), frame.begin(), frame.end());
    }
    PrintDigest("PERSISTENCE_BULK_PHYSICALITY", bulk_physicality.physicality_id);
    PrintDigest("PERSISTENCE_BULK_OCCURRENCE", bulk_occurrence.occurrence_id);
    PrintHex("PERSISTENCE_BULK_STREAM", bulk_stream);
    return 0;
}
