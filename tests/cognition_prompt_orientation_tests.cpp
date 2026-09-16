#include "laplace/cognition_prompt_conversation.h"
#include "laplace/identity.h"
#include <cstring>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "context_fixture.h"
#include "prompt_admission_fixture.h"

namespace {

using namespace laplace_test::admission;

struct AdmissionOwner final {
    laplace_cognition_prompt_admission* value{};
    ~AdmissionOwner() { laplace_cognition_prompt_admission_destroy(&value); }
};

laplace_cognition_prompt_conversation_request OrientationRequest(
    const laplace_id128& goal) {
    laplace_cognition_prompt_conversation_request request{};
    request.cognition_policy.goal_entity_id = goal;
    request.cognition_policy.evidence_boundary = Digest(0xD0U);
    request.cognition_policy.evidence_epoch = Digest(0xD1U);
    request.cognition_policy.authority_id = Digest(0xD2U);
    request.cognition_policy.result_contract_fingerprint = Digest(0xD3U);
    request.cognition_policy.relation_mask = LAPLACE_OBSERVATION_QUERY_CONSTITUENT;
    request.cognition_policy.maximum_results = 1U;
    request.cognition_policy.request_flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    request.cognition_policy.version = LAPLACE_COGNITION_TURN_POLICY_VERSION;
    request.cognition_policy.search_budget.max_expanded_states = 16U;
    request.cognition_policy.search_budget.max_transition_records = 64U;
    request.cognition_policy.search_budget.max_emitted_states = 64U;
    request.cognition_policy.search_budget.max_frontier_states = 32U;
    request.cognition_policy.search_budget.max_memory_bytes = UINT64_C(1048576);
    request.cognition_policy.search_budget.max_io_operations = 16U;
    request.cognition_policy.search_budget.max_database_operations = 16U;
    request.cognition_policy.search_budget.max_provider_calls = 16U;
    request.cognition_policy.search_budget.max_depth = 2U;
    request.cognition_policy.search_budget.requested_path_count = 1U;
    request.cognition_policy.search_budget.frontier_batch_width = 8U;
    request.cognition_policy.search_budget.transition_batch_capacity = 64U;
    request.cognition_policy.forward_limits.max_layers = 2U;
    request.cognition_policy.forward_limits.max_provider_calls = 4U;
    request.cognition_policy.forward_limits.max_projected_queries = 4U;
    request.cognition_policy.forward_limits.max_candidate_operations = 8U;
    request.cognition_policy.forward_limits.max_resolutions = 2U;
    request.cognition_policy.forward_limits.max_resource_cost = 512U;
    request.cognition_policy.forward_limits.max_io_operations = 16U;
    request.cognition_policy.forward_limits.max_database_operations = 16U;
    request.cognition_policy.forward_limits.candidate_operation_capacity = 4U;
    request.cognition_policy.forward_limits.resolution_capacity = 2U;
    request.version = LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION;
    return request;
}

TEST(CognitionPromptOrientation, RejectsCallerGoalThatDidNotTugBackFromPrompt) {
    const std::string prompt = "BA";
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);
    auto prompt_input = PromptInput(prompt, &context, &structure_provider);

    AdmissionOwner admission{};
    ASSERT_EQ(
        laplace_cognition_prompt_admission_create(
            &prompt_input, &atom_provider, &presence_provider, &admission.value),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    ASSERT_NE(admission.value, nullptr);

    const auto unrelated_goal = Codepoint(static_cast<std::uint32_t>('Z'));
    auto request = OrientationRequest(unrelated_goal);
    laplace_cognition_realization_provider_v1 unused_realization{};
    laplace_cognition_materialization_provider_v1 unused_materialization{};
    std::array<std::uint8_t, 8> output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    std::size_t output_bytes = 0U;
    std::size_t frame_bytes = 0U;
    laplace_cognition_prompt_conversation_result result{};

    EXPECT_EQ(
        laplace_cognition_prompt_conversation_execute(
            admission.value, &request, nullptr, 0U, nullptr, 0U,
            &unused_realization, &unused_materialization,
            output.data(), output.size(), &output_bytes,
            frame.data(), frame.size(), &frame_bytes, &result),
        LAPLACE_COGNITION_PROMPT_CONVERSATION_ORIENTATION_FAILURE);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_EQ(frame_bytes, 0U);
    EXPECT_EQ(result.orientation_receipt.status, LAPLACE_COGNITION_RESPONSE_OK);
    EXPECT_GT(result.orientation_receipt.response_count, 0U);
    EXPECT_NE(
        result.orientation_receipt.relation_family_mask &
            LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
        0U);
    EXPECT_EQ(result.orientation_relation_mask, 0U);
}

}  // namespace

#include "cognition_prompt_orientation_tests_part00.inc"

#include "cognition_prompt_orientation_tests_part01.inc"

#include "cognition_prompt_orientation_tests_part02.inc"

#include "cognition_prompt_orientation_tests_part03.inc"
