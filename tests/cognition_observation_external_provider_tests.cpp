#include "laplace/cognition_observation_request.h"

#include "blake3.h"
#include "laplace/identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

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

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void ProjectionFingerprint(
    const laplace_digest256& state_id,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_FORWARD_ENUMERATION_DOMAIN,
        sizeof(LAPLACE_COGNITION_FORWARD_ENUMERATION_DOMAIN) - 1U);
    HashDigest(&hasher, state_id);
    HashU64(&hasher, static_cast<std::uint64_t>(projection_count));
    for (std::size_t index = 0U; index < projection_count; ++index) {
        const auto& projection = projections[index];
        HashDigest(&hasher, projection.projection_id);
        HashDigest(&hasher, projection.obligation_id);
        HashDigest(&hasher, projection.binding_fingerprint);
        HashDigest(&hasher, projection.world_id);
        HashDigest(&hasher, projection.time_fingerprint);
        HashDigest(&hasher, projection.context_fingerprint);
        HashDigest(&hasher, projection.evidence_boundary);
        HashDigest(&hasher, projection.authority_id);
        HashDigest(&hasher, projection.result_contract_fingerprint);
        HashU32(&hasher, projection.query_kind);
        HashU32(&hasher, projection.obligation_flags);
    }
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(codepoint, &entity, &witness),
        LAPLACE_IDENTITY_OK);
    return entity;
}

laplace_cognition_observation_request Request() {
    laplace_cognition_observation_request request{};
    request.anchor_entity_id = Codepoint(0x42U);
    request.world_id = Digest(60U);
    request.time_fingerprint = Digest(61U);
    request.context_fingerprint = Digest(62U);
    request.evidence_boundary = Digest(63U);
    request.evidence_epoch = Digest(64U);
    request.authority_id = Digest(65U);
    request.result_contract_fingerprint = Digest(66U);
    request.relation_mask = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
    request.maximum_results = 1U;
    request.flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    request.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    request.search_budget.max_expanded_states = 1U;
    request.search_budget.max_transition_records = 8U;
    request.search_budget.max_emitted_states = 9U;
    request.search_budget.max_frontier_states = 9U;
    request.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.search_budget.max_io_operations = 8U;
    request.search_budget.max_database_operations = 8U;
    request.search_budget.max_provider_calls = 2U;
    request.search_budget.max_depth = 1U;
    request.search_budget.requested_path_count = request.maximum_results;
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

struct ProviderState {
    std::size_t enumerate_calls{};
    std::size_t execute_calls{};
};

laplace_cognition_guidance_operation MakeOperation(
    const laplace_cognition_query_projection& projection) {
    laplace_cognition_guidance_operation operation{};
    const auto seed = static_cast<std::uint8_t>(
        80U + static_cast<unsigned int>(projection.obligation_id.bytes[0]) % 80U);
    operation.operation_id = Digest(seed);
    operation.target_obligation_id = projection.obligation_id;
    operation.operands_fingerprint = Digest(static_cast<std::uint8_t>(seed + 1U));
    operation.preconditions_fingerprint = Digest(static_cast<std::uint8_t>(seed + 2U));
    operation.predicted_effect_fingerprint = Digest(static_cast<std::uint8_t>(seed + 3U));
    operation.authority_id = projection.authority_id;
    operation.receipt_contract_fingerprint = Digest(static_cast<std::uint8_t>(seed + 4U));
    operation.expected_obligation_reduction = 1U;
    operation.information_value = 10U;
    operation.resource_cost = 1U;
    operation.kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    operation.flags = LAPLACE_COGNITION_OPERATION_INFORMATION_SEEKING;
    return operation;
}

int Enumerate(
    void* const provider_state,
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_guidance_operation* const operations,
    const std::size_t operation_capacity,
    std::size_t* const operation_count,
    laplace_cognition_forward_enumeration_receipt* const receipt) {
    if (provider_state == nullptr || state == nullptr || projections == nullptr ||
        projection_count != 1U || grant == nullptr || operations == nullptr ||
        operation_capacity < 1U || operation_count == nullptr || receipt == nullptr) {
        return 1;
    }
    auto* const provider = static_cast<ProviderState*>(provider_state);
    ++provider->enumerate_calls;
    operations[0] = MakeOperation(projections[0]);
    *operation_count = 1U;

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 2;
    }
    *receipt = laplace_cognition_forward_enumeration_receipt{};
    receipt->receipt_id = Digest(180U);
    receipt->state_id = state_id;
    ProjectionFingerprint(state_id, projections, projection_count, &receipt->projection_fingerprint);
    receipt->projected_query_count = 1U;
    receipt->candidate_operation_count = 1U;
    return 0;
}

int Execute(
    void* const provider_state,
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_guidance_operation* const operation,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_resolution* const resolutions,
    const std::size_t resolution_capacity,
    std::size_t* const resolution_count,
    laplace_cognition_forward_execution_receipt* const receipt) {
    if (provider_state == nullptr || state == nullptr || operation == nullptr ||
        projections == nullptr || projection_count != 1U || grant == nullptr ||
        resolutions == nullptr || resolution_capacity < 1U ||
        resolution_count == nullptr || receipt == nullptr ||
        !Same(projections[0].obligation_id, operation->target_obligation_id)) {
        return 1;
    }
    auto* const provider = static_cast<ProviderState*>(provider_state);
    ++provider->execute_calls;

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 2;
    }
    resolutions[0] = laplace_cognition_resolution{
        operation->target_obligation_id,
        Digest(181U),
        Digest(182U),
        LAPLACE_COGNITION_OBLIGATION_SATISFIED,
        0U};
    *resolution_count = 1U;

    *receipt = laplace_cognition_forward_execution_receipt{};
    receipt->receipt_id = Digest(183U);
    receipt->state_id = state_id;
    receipt->operation_id = operation->operation_id;
    receipt->result_fingerprint = Digest(184U);
    receipt->resolution_count = 1U;
    receipt->resource_cost = 1U;
    return 0;
}

laplace_cognition_forward_provider_v1 Provider(ProviderState* const state) {
    return laplace_cognition_forward_provider_v1{
        state,
        Enumerate,
        Execute,
        LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR,
        LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR,
        0U,
        0U};
}

class ResultHandle final {
public:
    ~ResultHandle() { laplace_cognition_forward_result_destroy(&value); }
    laplace_cognition_forward_result* value{};
};

TEST(CognitionObservationExternalProvider, NativeBoundaryOwnsCompileGuidanceAndForwardLifecycle) {
    const auto request = Request();
    ProviderState provider_state{};
    const auto provider = Provider(&provider_state);
    ResultHandle result;
    laplace_cognition_forward_receipt receipt{};

    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_provider(
            &request, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(provider_state.enumerate_calls, 1U);
    EXPECT_EQ(provider_state.execute_calls, 1U);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_EQ(receipt.layer_count, 1U);
}

TEST(CognitionObservationExternalProvider, RejectsProviderAbiDriftBeforeExecution) {
    const auto request = Request();
    ProviderState provider_state{};
    auto provider = Provider(&provider_state);
    provider.abi_major = static_cast<std::uint16_t>(
        LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR + 1U);
    ResultHandle result;
    laplace_cognition_forward_receipt receipt{};

    EXPECT_EQ(
        laplace_cognition_observation_request_execute_with_provider(
            &request, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_PROVIDER_FAILURE);
    EXPECT_EQ(result.value, nullptr);
    EXPECT_EQ(provider_state.enumerate_calls, 0U);
    EXPECT_EQ(provider_state.execute_calls, 0U);
}

}  // namespace
