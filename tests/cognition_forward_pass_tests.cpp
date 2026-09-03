#include "laplace/cognition_forward_pass.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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

laplace_cognition_guidance_header Header() {
    laplace_cognition_guidance_header header{};
    header.program_id = Digest(1U);
    header.goal_id = Digest(2U);
    header.bindings_fingerprint = Digest(3U);
    header.scope_fingerprint = Digest(4U);
    header.world_id = Digest(5U);
    header.time_fingerprint = Digest(6U);
    header.context_fingerprint = Digest(7U);
    header.evidence_epoch = Digest(8U);
    header.authority_id = Digest(9U);
    header.result_contract_fingerprint = Digest(10U);
    header.version = LAPLACE_COGNITION_GUIDANCE_VERSION;
    return header;
}

laplace_cognition_obligation OpenObligation(const std::uint8_t seed) {
    const auto header = Header();
    laplace_cognition_obligation obligation{};
    obligation.obligation_id = Digest(seed);
    obligation.binding_fingerprint = Digest(static_cast<std::uint8_t>(seed + 30U));
    obligation.world_id = header.world_id;
    obligation.time_fingerprint = header.time_fingerprint;
    obligation.context_fingerprint = header.context_fingerprint;
    obligation.evidence_boundary = Digest(20U);
    obligation.authority_id = header.authority_id;
    obligation.result_contract_fingerprint = header.result_contract_fingerprint;
    obligation.kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    obligation.disposition = LAPLACE_COGNITION_OBLIGATION_OPEN;
    obligation.flags = LAPLACE_COGNITION_OBLIGATION_REQUIRED;
    return obligation;
}

laplace_cognition_obligation SatisfiedObligation(const std::uint8_t seed) {
    auto obligation = OpenObligation(seed);
    obligation.value_id = Digest(static_cast<std::uint8_t>(seed + 70U));
    obligation.resolution_receipt_id = Digest(static_cast<std::uint8_t>(seed + 90U));
    obligation.disposition = LAPLACE_COGNITION_OBLIGATION_SATISFIED;
    return obligation;
}

class StateHandle {
public:
    StateHandle() = default;
    StateHandle(const StateHandle&) = delete;
    StateHandle& operator=(const StateHandle&) = delete;
    StateHandle(StateHandle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    ~StateHandle() { laplace_cognition_guidance_state_destroy(&value); }
    laplace_cognition_guidance_state* value{};
};

class ResultHandle {
public:
    ResultHandle() = default;
    ResultHandle(const ResultHandle&) = delete;
    ResultHandle& operator=(const ResultHandle&) = delete;
    ~ResultHandle() { laplace_cognition_forward_result_destroy(&value); }
    laplace_cognition_forward_result* value{};
};

StateHandle MakeState(const std::vector<laplace_cognition_obligation>& obligations) {
    StateHandle state;
    const auto header = Header();
    EXPECT_EQ(
        laplace_cognition_guidance_state_create(
            &header, obligations.data(), obligations.size(), &state.value),
        LAPLACE_COGNITION_GUIDANCE_OK);
    return state;
}

laplace_cognition_forward_program Program() {
    laplace_cognition_forward_program program{};
    program.program_id = Digest(180U);
    program.result_contract_fingerprint = Header().result_contract_fingerprint;
    program.max_layers = 8U;
    program.max_provider_calls = 16U;
    program.max_projected_queries = 32U;
    program.max_candidate_operations = 32U;
    program.max_resolutions = 16U;
    program.max_resource_cost = 64U;
    program.max_io_operations = 64U;
    program.max_database_operations = 64U;
    program.candidate_operation_capacity = 8U;
    program.resolution_capacity = 4U;
    program.flags =
        LAPLACE_COGNITION_FORWARD_REQUIRE_STATE_PROGRESS |
        LAPLACE_COGNITION_FORWARD_REQUIRE_PROJECTED_QUERY_CONSUMPTION |
        LAPLACE_COGNITION_FORWARD_REQUIRE_RECEIPTED_EXECUTION;
    program.version = LAPLACE_COGNITION_FORWARD_VERSION;
    return program;
}

struct ProviderState {
    bool reverse_enumeration{};
    bool malformed_enumeration_receipt{};
    bool expensive_operations{};
    std::size_t enumerate_calls{};
    std::size_t execute_calls{};
};

laplace_cognition_guidance_operation MakeOperation(
    const laplace_cognition_query_projection& projection,
    const bool expensive) {
    laplace_cognition_guidance_operation operation{};
    const auto identity_seed = static_cast<std::uint8_t>(
        80U + static_cast<unsigned int>(projection.obligation_id.bytes[0]) % 80U);
    operation.operation_id = Digest(identity_seed);
    operation.target_obligation_id = projection.obligation_id;
    operation.operands_fingerprint = Digest(static_cast<std::uint8_t>(identity_seed + 1U));
    operation.preconditions_fingerprint = Digest(static_cast<std::uint8_t>(identity_seed + 2U));
    operation.predicted_effect_fingerprint = Digest(static_cast<std::uint8_t>(identity_seed + 3U));
    operation.authority_id = projection.authority_id;
    operation.receipt_contract_fingerprint = Digest(
        static_cast<std::uint8_t>(identity_seed + 4U));
    operation.expected_obligation_reduction = 1U;
    operation.information_value = 10U;
    operation.resource_cost = expensive ? 100U : 1U;
    operation.novelty = 0U;
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
        grant == nullptr || operations == nullptr || operation_count == nullptr ||
        receipt == nullptr || projection_count == 0U ||
        projection_count > operation_capacity) {
        return 1;
    }
    auto* const provider = static_cast<ProviderState*>(provider_state);
    ++provider->enumerate_calls;
    std::vector<laplace_cognition_guidance_operation> candidate;
    candidate.reserve(projection_count);
    for (std::size_t index = 0U; index < projection_count; ++index) {
        candidate.push_back(MakeOperation(projections[index], provider->expensive_operations));
    }
    if (provider->reverse_enumeration) {
        std::reverse(candidate.begin(), candidate.end());
    }
    std::copy(candidate.begin(), candidate.end(), operations);
    *operation_count = candidate.size();

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 2;
    }
    *receipt = laplace_cognition_forward_enumeration_receipt{};
    receipt->receipt_id = Digest(static_cast<std::uint8_t>(
        20U + static_cast<unsigned int>(state_id.bytes[0]) % 160U));
    receipt->state_id = provider->malformed_enumeration_receipt
        ? Digest(240U)
        : state_id;
    ProjectionFingerprint(
        state_id, projections, projection_count, &receipt->projection_fingerprint);
    receipt->projected_query_count = static_cast<std::uint64_t>(projection_count);
    receipt->candidate_operation_count = static_cast<std::uint64_t>(candidate.size());
    receipt->resource_cost = 0U;
    receipt->io_operations = 0U;
    receipt->database_operations = 0U;
    (void)grant;
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
        projections == nullptr || grant == nullptr || resolutions == nullptr ||
        resolution_count == nullptr || receipt == nullptr || resolution_capacity == 0U) {
        return 1;
    }
    auto* const provider = static_cast<ProviderState*>(provider_state);
    ++provider->execute_calls;
    bool projected = false;
    for (std::size_t index = 0U; index < projection_count; ++index) {
        projected = projected || Same(
            projections[index].obligation_id, operation->target_obligation_id);
    }
    if (!projected) return 2;

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 3;
    }
    const auto target_seed = static_cast<std::uint8_t>(
        30U + static_cast<unsigned int>(operation->target_obligation_id.bytes[0]) % 150U);
    resolutions[0] = laplace_cognition_resolution{
        operation->target_obligation_id,
        Digest(target_seed),
        Digest(static_cast<std::uint8_t>(target_seed + 1U)),
        LAPLACE_COGNITION_OBLIGATION_SATISFIED,
        0U};
    *resolution_count = 1U;

    *receipt = laplace_cognition_forward_execution_receipt{};
    receipt->receipt_id = Digest(static_cast<std::uint8_t>(target_seed + 2U));
    receipt->state_id = state_id;
    receipt->operation_id = operation->operation_id;
    receipt->result_fingerprint = Digest(static_cast<std::uint8_t>(target_seed + 3U));
    receipt->resolution_count = 1U;
    receipt->resource_cost = 1U;
    receipt->io_operations = 0U;
    receipt->database_operations = 0U;
    (void)grant;
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

TEST(CognitionForwardPass, CompletesOnlyAfterAllObligationsResolve) {
    const std::vector obligations{OpenObligation(30U), OpenObligation(31U)};
    auto state = MakeState(obligations);
    auto program = Program();
    ProviderState provider_state{};
    auto provider = Provider(&provider_state);
    ResultHandle result;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, state.value, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.layer_count, 2U);
    EXPECT_EQ(receipt.provider_call_count, 4U);
    EXPECT_EQ(receipt.final_remaining_required_count, 0U);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
    EXPECT_EQ(laplace_cognition_forward_result_layer_count(result.value), 2U);
    laplace_cognition_forward_layer_receipt first{};
    laplace_cognition_forward_layer_receipt second{};
    ASSERT_EQ(
        laplace_cognition_forward_result_layer(result.value, 0U, &first),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_EQ(
        laplace_cognition_forward_result_layer(result.value, 1U, &second),
        LAPLACE_COGNITION_FORWARD_OK);
    EXPECT_EQ(first.remaining_open_count, 1U);
    EXPECT_EQ(second.remaining_open_count, 0U);
}

TEST(CognitionForwardPass, DoesNotTerminateAfterOneLayerWhenObligationRemains) {
    const std::vector obligations{OpenObligation(30U), OpenObligation(31U)};
    auto state = MakeState(obligations);
    auto program = Program();
    program.max_layers = 1U;
    ProviderState provider_state{};
    auto provider = Provider(&provider_state);
    ResultHandle result;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, state.value, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_LAYER_LIMIT);
    EXPECT_EQ(receipt.layer_count, 1U);
    EXPECT_EQ(receipt.final_remaining_required_count, 1U);
    EXPECT_EQ(receipt.final_completion, LAPLACE_COGNITION_COMPLETION_INCOMPLETE);
}

TEST(CognitionForwardPass, ProviderEnumerationOrderCannotChangeReceipt) {
    const std::vector obligations{OpenObligation(30U), OpenObligation(31U)};
    auto left_state = MakeState(obligations);
    auto right_state = MakeState(obligations);
    auto program = Program();

    ProviderState left_provider_state{};
    auto left_provider = Provider(&left_provider_state);
    ResultHandle left_result;
    laplace_cognition_forward_receipt left_receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, left_state.value, &left_provider,
            &left_result.value, &left_receipt),
        LAPLACE_COGNITION_FORWARD_OK);

    ProviderState right_provider_state{};
    right_provider_state.reverse_enumeration = true;
    auto right_provider = Provider(&right_provider_state);
    ResultHandle right_result;
    laplace_cognition_forward_receipt right_receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, right_state.value, &right_provider,
            &right_result.value, &right_receipt),
        LAPLACE_COGNITION_FORWARD_OK);

    EXPECT_TRUE(Same(left_receipt.receipt_id, right_receipt.receipt_id));
    EXPECT_TRUE(Same(left_receipt.output_fingerprint, right_receipt.output_fingerprint));
    EXPECT_TRUE(Same(left_receipt.final_state_id, right_receipt.final_state_id));
    EXPECT_EQ(left_receipt.layer_count, right_receipt.layer_count);
}

TEST(CognitionForwardPass, RejectsMalformedEnumerationReceipt) {
    const std::vector obligations{OpenObligation(30U)};
    auto state = MakeState(obligations);
    auto program = Program();
    ProviderState provider_state{};
    provider_state.malformed_enumeration_receipt = true;
    auto provider = Provider(&provider_state);
    ResultHandle result;
    laplace_cognition_forward_receipt receipt{};
    EXPECT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, state.value, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT);
    EXPECT_EQ(result.value, nullptr);
    EXPECT_EQ(receipt.status, LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT);
    EXPECT_EQ(provider_state.execute_calls, 0U);
}

TEST(CognitionForwardPass, StopsBeforeExecutingUnaffordableOperation) {
    const std::vector obligations{OpenObligation(30U)};
    auto state = MakeState(obligations);
    auto program = Program();
    program.max_resource_cost = 8U;
    ProviderState provider_state{};
    provider_state.expensive_operations = true;
    auto provider = Provider(&provider_state);
    ResultHandle result;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, state.value, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_RESOURCE_LIMIT);
    EXPECT_EQ(receipt.layer_count, 0U);
    EXPECT_EQ(receipt.provider_call_count, 1U);
    EXPECT_EQ(provider_state.execute_calls, 0U);
    EXPECT_EQ(receipt.final_remaining_required_count, 1U);
}

TEST(CognitionForwardPass, AlreadyCompleteStateDoesNotCallProvider) {
    const std::vector obligations{SatisfiedObligation(30U)};
    auto state = MakeState(obligations);
    auto program = Program();
    ProviderState provider_state{};
    auto provider = Provider(&provider_state);
    ResultHandle result;
    laplace_cognition_forward_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_forward_pass_execute(
            &program, state.value, &provider, &result.value, &receipt),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_FORWARD_COMPLETE);
    EXPECT_EQ(receipt.layer_count, 0U);
    EXPECT_EQ(receipt.provider_call_count, 0U);
    EXPECT_EQ(provider_state.enumerate_calls, 0U);
    EXPECT_EQ(provider_state.execute_calls, 0U);

    laplace_cognition_guidance_state* clone = nullptr;
    ASSERT_EQ(
        laplace_cognition_forward_result_final_state_clone(result.value, &clone),
        LAPLACE_COGNITION_FORWARD_OK);
    ASSERT_NE(clone, nullptr);
    laplace_digest256 clone_id{};
    ASSERT_EQ(
        laplace_cognition_guidance_state_identify(clone, &clone_id),
        LAPLACE_COGNITION_GUIDANCE_OK);
    EXPECT_TRUE(Same(clone_id, receipt.final_state_id));
    laplace_cognition_guidance_state_destroy(&clone);
}

}  // namespace
