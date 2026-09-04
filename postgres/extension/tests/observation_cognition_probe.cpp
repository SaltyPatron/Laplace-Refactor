#include "laplace/cognition_forward_pass.h"
#include "laplace/cognition_observation_request.h"
#include "laplace/identity.h"
#include "laplace/observation_query.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

std::uint64_t Metadata(const std::uint8_t tier, const std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
        (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) |
        (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

bool Codepoint(const std::uint32_t codepoint, laplace_id128* entity) {
    laplace_digest256 witness{};
    return laplace_identity_codepoint_witness(codepoint, entity, &witness) ==
        LAPLACE_IDENTITY_OK;
}

template <typename T>
void PrintHex(const char* key, const T& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    std::printf("%s=", key);
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        std::printf("%02x", static_cast<unsigned int>(bytes[index]));
    }
    std::printf("\n");
}

void PrintUnsigned(const char* key, const std::uint64_t value) {
    std::printf("%s=%llu\n", key, static_cast<unsigned long long>(value));
}

struct Fixture final {
    laplace_id128 a{};
    laplace_id128 b{};
    laplace_id128 c{};
    laplace_id128 root{};
    std::array<laplace_trajectory_carrier, 3> carriers{};
    laplace_persistence_physicality_record physicality{};
    std::array<laplace_persistence_trajectory_segment_record, 3> segments{};
    laplace_digest256 boundary{};
    laplace_digest256 evidence_epoch{};
};

bool BuildFixture(Fixture* fixture) {
    if (fixture == nullptr ||
        !Codepoint(0x41U, &fixture->a) ||
        !Codepoint(0x42U, &fixture->b) ||
        !Codepoint(0x43U, &fixture->c) ||
        !Codepoint(0x52U, &fixture->root)) {
        return false;
    }
    fixture->boundary = Digest(180U);
    fixture->evidence_epoch = Digest(200U);

    const std::array<laplace_id128, 3> entities{{
        fixture->a, fixture->b, fixture->c}};
    const std::array<std::uint32_t, 3> atoms{{0x41U, 0x42U, 0x43U}};
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        if (laplace_trajectory_composition_encode(
                &entities[index], static_cast<std::uint64_t>(index + 1U), 1U,
                Metadata(2U, atoms[index]), &fixture->carriers[index]) !=
            LAPLACE_TRAJECTORY_OK) {
            return false;
        }
    }

    fixture->physicality.entity_id = fixture->root;
    fixture->physicality.physicality_type =
        LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    fixture->physicality.vertex_class =
        LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    fixture->physicality.recipe_version = 1U;
    fixture->physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    fixture->physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    fixture->physicality.recipe_fingerprint = Digest(20U);
    fixture->physicality.geometry_epoch = Digest(40U);
    fixture->physicality.centroid.component[0] = 0.125;
    fixture->physicality.centroid.component[1] = -0.25;
    fixture->physicality.centroid.component[2] = 0.5;
    fixture->physicality.centroid.component[3] = -0.75;
    fixture->physicality.radius = 0.875;
    fixture->physicality.logical_count = fixture->carriers.size();
    fixture->physicality.vertex_count = fixture->carriers.size();
    if (laplace_persistence_trajectory_fingerprint(
            fixture->carriers.data(), fixture->carriers.size(),
            &fixture->physicality.trajectory_fingerprint) !=
            LAPLACE_PERSISTENCE_OK ||
        laplace_persistence_physicality_identify(
            &fixture->physicality, &fixture->physicality.physicality_id) !=
            LAPLACE_PERSISTENCE_OK) {
        return false;
    }

    for (std::size_t index = 0U; index < fixture->segments.size(); ++index) {
        fixture->segments[index].physicality_id = fixture->physicality.physicality_id;
        fixture->segments[index].vertex_index = static_cast<std::uint64_t>(index);
        fixture->segments[index].carrier = fixture->carriers[index];
        if (laplace_trajectory_composition_decode_one(
                &fixture->carriers[index],
                static_cast<std::uint64_t>(index + 1U),
                &fixture->segments[index].occurrence) != LAPLACE_TRAJECTORY_OK) {
            return false;
        }
    }
    return true;
}

laplace_cognition_observation_request Request(const Fixture& fixture) {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = fixture.c;
    request.goal_entity_id = fixture.a;
    request.world_id = Digest(60U);
    request.time_fingerprint = Digest(61U);
    request.context_fingerprint = Digest(62U);
    request.evidence_boundary = fixture.boundary;
    request.evidence_epoch = fixture.evidence_epoch;
    request.authority_id = Digest(63U);
    request.result_contract_fingerprint = Digest(64U);
    request.relation_mask = LAPLACE_OBSERVATION_RELATION_PREDECESSOR;
    request.maximum_results = 1U;
    request.flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;
    request.search_budget.max_expanded_states = 2U;
    request.search_budget.max_transition_records = 8U;
    request.search_budget.max_emitted_states = 8U;
    request.search_budget.max_frontier_states = 8U;
    request.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.search_budget.max_io_operations = 8U;
    request.search_budget.max_database_operations = 8U;
    request.search_budget.max_provider_calls = 2U;
    request.search_budget.max_depth = 2U;
    request.search_budget.requested_path_count = 1U;
    request.search_budget.frontier_batch_width = 2U;
    request.search_budget.transition_batch_capacity = 8U;
    request.forward_limits.max_layers = 2U;
    request.forward_limits.max_provider_calls = 4U;
    request.forward_limits.max_projected_queries = 4U;
    request.forward_limits.max_candidate_operations = 4U;
    request.forward_limits.max_resolutions = 2U;
    request.forward_limits.max_resource_cost = 128U;
    request.forward_limits.max_io_operations = 8U;
    request.forward_limits.max_database_operations = 8U;
    request.forward_limits.candidate_operation_capacity = 4U;
    request.forward_limits.resolution_capacity = 2U;
    return request;
}

}  // namespace

int main() {
    Fixture fixture{};
    if (!BuildFixture(&fixture)) {
        return 1;
    }

    const laplace_cognition_observation_request request = Request(fixture);
    laplace_observation_query_index_base_input index_input{};
    index_input.physicalities = &fixture.physicality;
    index_input.physicality_count = 1U;
    index_input.trajectory_segments = fixture.segments.data();
    index_input.trajectory_segment_count = fixture.segments.size();
    index_input.boundary_id = fixture.boundary;
    index_input.evidence_epoch = fixture.evidence_epoch;
    index_input.maximum_candidate_records_per_expansion = 8U;

    laplace_observation_query_index* index = nullptr;
    if (laplace_observation_query_index_create_base(&index_input, &index) !=
            LAPLACE_OBSERVATION_QUERY_OK ||
        index == nullptr) {
        return 2;
    }
    laplace_observation_query_index_summary index_summary{};
    if (laplace_observation_query_index_summary_get(index, &index_summary) !=
        LAPLACE_OBSERVATION_QUERY_OK) {
        laplace_observation_query_index_destroy(&index);
        return 3;
    }

    laplace_cognition_observation_compiled_request compiled{};
    if (laplace_cognition_observation_request_compile(&request, &compiled) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        laplace_observation_query_index_destroy(&index);
        return 4;
    }

    laplace_cognition_observation_request_provider* provider_state = nullptr;
    laplace_cognition_forward_provider_v1 provider{};
    if (laplace_cognition_observation_request_cognition_provider(
            index, &compiled, &provider_state, &provider) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        laplace_observation_query_index_destroy(&index);
        return 5;
    }

    laplace_cognition_forward_result* result = nullptr;
    laplace_cognition_forward_receipt receipt{};
    if (laplace_cognition_forward_pass_execute(
            &compiled.forward_program, compiled.guidance_state, &provider,
            &result, &receipt) != LAPLACE_COGNITION_FORWARD_OK ||
        result == nullptr) {
        laplace_cognition_observation_request_provider_destroy(&provider_state);
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        laplace_observation_query_index_destroy(&index);
        return 6;
    }

    laplace_cognition_guidance_state* final_state = nullptr;
    laplace_cognition_obligation obligation{};
    if (laplace_cognition_forward_result_final_state_clone(result, &final_state) !=
            LAPLACE_COGNITION_FORWARD_OK ||
        final_state == nullptr ||
        laplace_cognition_guidance_state_obligation_count(final_state) != 1U ||
        laplace_cognition_guidance_state_obligation(
            final_state, 0U, &obligation) != LAPLACE_COGNITION_GUIDANCE_OK) {
        laplace_cognition_guidance_state_destroy(&final_state);
        laplace_cognition_forward_result_destroy(&result);
        laplace_cognition_observation_request_provider_destroy(&provider_state);
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        laplace_observation_query_index_destroy(&index);
        return 7;
    }

    PrintHex("ENTITY_A", fixture.a);
    PrintHex("ENTITY_B", fixture.b);
    PrintHex("ENTITY_C", fixture.c);
    PrintHex("ENTITY_ROOT", fixture.root);
    PrintHex("BOUNDARY", fixture.boundary);
    PrintHex("EVIDENCE_EPOCH", fixture.evidence_epoch);
    PrintHex("RECIPE_FINGERPRINT", fixture.physicality.recipe_fingerprint);
    PrintHex("GEOMETRY_EPOCH", fixture.physicality.geometry_epoch);
    PrintHex("TRAJECTORY_FINGERPRINT", fixture.physicality.trajectory_fingerprint);
    PrintHex("PHYSICALITY_ID", fixture.physicality.physicality_id);
    PrintHex("WORLD_ID", request.world_id);
    PrintHex("TIME_FINGERPRINT", request.time_fingerprint);
    PrintHex("CONTEXT_FINGERPRINT", request.context_fingerprint);
    PrintHex("AUTHORITY_ID", request.authority_id);
    PrintHex("RESULT_CONTRACT_FINGERPRINT", request.result_contract_fingerprint);
    PrintHex("REQUEST_FINGERPRINT", compiled.request_fingerprint);
    PrintHex("INDEX_FINGERPRINT", index_summary.index_fingerprint);
    PrintHex("RESULT_FINGERPRINT", obligation.value_id);
    PrintHex("RESOLUTION_RECEIPT_ID", obligation.resolution_receipt_id);
    PrintHex("FORWARD_RECEIPT_ID", receipt.receipt_id);
    PrintHex("FORWARD_PROGRAM_FINGERPRINT", receipt.program_fingerprint);
    PrintHex("INITIAL_STATE_ID", receipt.initial_state_id);
    PrintHex("FINAL_STATE_ID", receipt.final_state_id);
    PrintHex("LAYER_TRACE_FINGERPRINT", receipt.layer_trace_fingerprint);
    PrintHex("OUTPUT_FINGERPRINT", receipt.output_fingerprint);

    for (std::size_t index_value = 0U; index_value < fixture.segments.size(); ++index_value) {
        char carrier_key[32];
        char metadata_key[32];
        char atom_key[32];
        char ordinal_key[32];
        char run_key[32];
        char tier_key[32];
        char logical_key[32];
        std::snprintf(carrier_key, sizeof(carrier_key), "CARRIER_%zu", index_value);
        std::snprintf(metadata_key, sizeof(metadata_key), "METADATA_%zu", index_value);
        std::snprintf(atom_key, sizeof(atom_key), "ATOM_%zu", index_value);
        std::snprintf(ordinal_key, sizeof(ordinal_key), "PACKED_ORDINAL_%zu", index_value);
        std::snprintf(run_key, sizeof(run_key), "RUN_LENGTH_%zu", index_value);
        std::snprintf(tier_key, sizeof(tier_key), "TIER_%zu", index_value);
        std::snprintf(logical_key, sizeof(logical_key), "LOGICAL_ORDINAL_%zu", index_value);
        PrintHex(carrier_key, fixture.carriers[index_value]);
        PrintUnsigned(metadata_key, fixture.segments[index_value].occurrence.metadata);
        PrintUnsigned(atom_key, fixture.segments[index_value].occurrence.atom);
        PrintUnsigned(ordinal_key, fixture.segments[index_value].occurrence.packed_ordinal);
        PrintUnsigned(run_key, fixture.segments[index_value].occurrence.run_length);
        PrintUnsigned(tier_key, fixture.segments[index_value].occurrence.tier);
        PrintUnsigned(logical_key, fixture.segments[index_value].occurrence.logical_ordinal);
    }

    PrintUnsigned("EXPECTED_LAYER_COUNT", receipt.layer_count);
    PrintUnsigned("EXPECTED_PROVIDER_CALL_COUNT", receipt.provider_call_count);
    PrintUnsigned("EXPECTED_PROJECTED_QUERY_COUNT", receipt.projected_query_count);
    PrintUnsigned("EXPECTED_CANDIDATE_OPERATION_COUNT", receipt.candidate_operation_count);
    PrintUnsigned("EXPECTED_RESOLUTION_COUNT", receipt.resolution_count);
    PrintUnsigned("EXPECTED_RESOURCE_COST", receipt.resource_cost);
    PrintUnsigned("EXPECTED_IO_OPERATIONS", receipt.io_operations);
    PrintUnsigned("EXPECTED_DATABASE_OPERATIONS", receipt.database_operations);
    PrintUnsigned("EXPECTED_REMAINING_REQUIRED", receipt.final_remaining_required_count);
    PrintUnsigned("EXPECTED_COMPLETION", receipt.final_completion);
    PrintUnsigned("EXPECTED_DISPOSITION", receipt.disposition);
    PrintUnsigned("EXPECTED_OBLIGATION_DISPOSITION", obligation.disposition);
    PrintUnsigned("EXPECTED_STATUS", receipt.status);
    PrintUnsigned("EXPECTED_VERSION", receipt.version);
    PrintUnsigned("EXPECTED_FLAGS", receipt.flags);

    laplace_cognition_guidance_state_destroy(&final_state);
    laplace_cognition_forward_result_destroy(&result);
    laplace_cognition_observation_request_provider_destroy(&provider_state);
    laplace_cognition_observation_compiled_request_destroy(&compiled);
    laplace_observation_query_index_destroy(&index);
    return 0;
}
