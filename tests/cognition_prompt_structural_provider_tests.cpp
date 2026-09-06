#include "laplace/cognition_prompt_admission.h"

#include "laplace/identity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "context_fixture.h"

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

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool ZeroDigest(const laplace_digest256& value) {
    return std::all_of(
        value.bytes, value.bytes + sizeof(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    EXPECT_EQ(laplace_identity_codepoint(codepoint, &entity), LAPLACE_IDENTITY_OK);
    return entity;
}

laplace_id128 Composite(const std::string& text) {
    std::vector<laplace_id128> children;
    for (const unsigned char byte : text) {
        children.push_back(Codepoint(static_cast<std::uint32_t>(byte)));
    }
    laplace_id128 entity{};
    EXPECT_EQ(
        laplace_identity_composite(children.data(), children.size(), &entity),
        LAPLACE_IDENTITY_OK);
    return entity;
}

struct StructureFixture final {
    std::size_t calls{};
};

laplace_decomposition_status StructureApplicable(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* const span,
    int* const applicable) {
    if (span == nullptr || applicable == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *applicable = span->depth == 0U ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_status StructureApply(
    void* const opaque,
    const laplace_decomposition_content* const content,
    const laplace_decomposition_span* const span,
    const laplace_decomposition_emit_fn emit,
    void* const emit_state) {
    if (opaque == nullptr || content == nullptr || span == nullptr || emit == nullptr) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    auto& fixture = *static_cast<StructureFixture*>(opaque);
    ++fixture.calls;
    if (span->depth == 0U && content->byte_count == 3U) {
        if (emit(
                emit_state, UINT64_C(1), UINT64_C(3),
                UINT64_C(0x5355425452454501), LAPLACE_DECOMPOSITION_SPAN_TEXT) != 0) {
            return LAPLACE_DECOMPOSITION_PROVIDER_FAILURE;
        }
    }
    return LAPLACE_DECOMPOSITION_OK;
}

laplace_decomposition_provider_v1 StructureProvider(StructureFixture* const fixture) {
    laplace_decomposition_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0x11U);
    provider.applicable = StructureApplicable;
    provider.apply = StructureApply;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return provider;
}

struct AtomFixture final {
    std::size_t calls{};
};

int ResolveAtoms(
    void* const opaque,
    const std::uint32_t* const positions,
    const std::size_t count,
    laplace_composition_known_entity* const known,
    laplace_digest256* const receipt) {
    if (opaque == nullptr || positions == nullptr || count == 0U ||
        known == nullptr || receipt == nullptr) {
        return 1;
    }
    auto& fixture = *static_cast<AtomFixture*>(opaque);
    ++fixture.calls;
    for (std::size_t index = 0U; index < count; ++index) {
        auto& value = known[index];
        value = laplace_composition_known_entity{};
        if (laplace_identity_codepoint_witness(
                positions[index], &value.entity_id,
                &value.identity_witness) != LAPLACE_IDENTITY_OK) {
            return 2;
        }
        value.physicality_id = Digest(static_cast<std::uint8_t>(
            0x30U + (positions[index] % UINT32_C(31))));
        const double a = static_cast<double>((positions[index] % UINT32_C(17)) + 1U) / 100.0;
        const double b = static_cast<double>((positions[index] % UINT32_C(19)) + 1U) / 100.0;
        const double c = static_cast<double>((positions[index] % UINT32_C(23)) + 1U) / 100.0;
        const double norm = std::sqrt(1.0 + a * a + b * b + c * c);
        value.centroid = laplace_point4d{{1.0 / norm, a / norm, b / norm, c / norm}};
        value.atom = positions[index];
        value.tier_floor = 0U;
        value.has_atom = 1U;
    }
    *receipt = Digest(0x51U);
    return 0;
}

laplace_cognition_prompt_atom_provider_v1 AtomProvider(AtomFixture* const fixture) {
    laplace_cognition_prompt_atom_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0x50U);
    provider.resolve = ResolveAtoms;
    provider.abi_major = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MINOR;
    return provider;
}

struct PresenceFixture final {
    std::size_t calls{};
};

laplace_composition_status ResolvePresence(
    void* const opaque,
    const laplace_composition_entity_candidate*,
    const std::size_t entity_count,
    const laplace_persistence_physicality_record*,
    const std::size_t physicality_count,
    std::uint8_t* const entity_dispositions,
    std::uint8_t* const physicality_dispositions,
    laplace_composition_presence_provider_result* const result) {
    if (opaque == nullptr || entity_dispositions == nullptr || result == nullptr ||
        (physicality_count != 0U && physicality_dispositions == nullptr)) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    auto& fixture = *static_cast<PresenceFixture*>(opaque);
    ++fixture.calls;
    std::fill_n(
        entity_dispositions, entity_count,
        static_cast<std::uint8_t>(LAPLACE_COMPOSITION_NOVEL));
    if (physicality_count != 0U) {
        std::fill_n(
            physicality_dispositions, physicality_count,
            static_cast<std::uint8_t>(LAPLACE_COMPOSITION_NOVEL));
    }
    result->provider_fingerprint = Digest(0x61U);
    result->provider_receipt_id = Digest(0x62U);
    result->returned_entity_count = static_cast<std::uint64_t>(entity_count);
    result->returned_physicality_count = static_cast<std::uint64_t>(physicality_count);
    result->entity_round_count = entity_count == 0U ? 0U : 1U;
    result->physicality_round_count = physicality_count == 0U ? 0U : 1U;
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_presence_provider_v1 PresenceProvider(
    PresenceFixture* const fixture) {
    laplace_composition_presence_provider_v1 provider{};
    provider.state = fixture;
    provider.resolve = ResolvePresence;
    provider.abi_major = LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI;
    provider.abi_minor = 0U;
    return provider;
}

laplace_cognition_prompt_admission_input Input(
    const std::string& prompt,
    const laplace_framework_context* const context,
    laplace_decomposition_provider_v1* const structure_provider) {
    static constexpr char MediaType[] = "text/plain";
    laplace_cognition_prompt_admission_input input{};
    input.decomposition.content.bytes = reinterpret_cast<const std::uint8_t*>(prompt.data());
    input.decomposition.content.byte_count = static_cast<std::uint64_t>(prompt.size());
    input.decomposition.content.media_type = MediaType;
    input.decomposition.content.media_type_byte_count = sizeof(MediaType) - 1U;
    input.decomposition.providers = structure_provider;
    input.decomposition.provider_count = 1U;
    input.decomposition.maximum_spans = 8U;
    input.decomposition.maximum_depth = 3U;
    input.framework_context = context;
    input.source_fingerprint = Digest(0x70U);
    input.content_recipe_fingerprint = Digest(0x71U);
    input.calculation_recipe_fingerprint = Digest(0x72U);
    input.geometry_epoch = Digest(0x73U);
    input.occurrence_context_fingerprint = Digest(0x74U);
    input.source_ordinal_base = 1U;
    input.preferred_batch_bytes = 512U;
    input.occurrence.principal_fingerprint = Digest(0x80U);
    input.occurrence.session_fingerprint = Digest(0x81U);
    input.occurrence.discourse_id = Digest(0x82U);
    input.occurrence.world_id = Digest(0x83U);
    input.occurrence.time_fingerprint = Digest(0x84U);
    input.occurrence.context_fingerprint = Digest(0x85U);
    input.occurrence.turn_ordinal = 0U;
    input.version = LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION;
    return input;
}

laplace_cognition_turn_policy Policy(const laplace_id128& goal) {
    laplace_cognition_turn_policy policy{};
    policy.goal_entity_id = goal;
    policy.evidence_boundary = Digest(0x90U);
    policy.evidence_epoch = Digest(0x91U);
    policy.authority_id = Digest(0x92U);
    policy.result_contract_fingerprint = Digest(0x93U);
    policy.relation_mask = LAPLACE_OBSERVATION_QUERY_CONSTITUENT;
    policy.maximum_results = 1U;
    policy.request_flags =
        LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    policy.version = LAPLACE_COGNITION_TURN_POLICY_VERSION;

    policy.search_budget.max_expanded_states = 8U;
    policy.search_budget.max_transition_records = 32U;
    policy.search_budget.max_emitted_states = 32U;
    policy.search_budget.max_frontier_states = 16U;
    policy.search_budget.max_memory_bytes = UINT64_C(1048576);
    policy.search_budget.max_io_operations = 8U;
    policy.search_budget.max_database_operations = 8U;
    policy.search_budget.max_provider_calls = 8U;
    policy.search_budget.max_depth = 3U;
    policy.search_budget.requested_path_count = 1U;
    policy.search_budget.frontier_batch_width = 4U;
    policy.search_budget.transition_batch_capacity = 32U;

    policy.forward_limits.max_layers = 2U;
    policy.forward_limits.max_provider_calls = 4U;
    policy.forward_limits.max_projected_queries = 4U;
    policy.forward_limits.max_candidate_operations = 8U;
    policy.forward_limits.max_resolutions = 2U;
    policy.forward_limits.max_resource_cost = 256U;
    policy.forward_limits.max_io_operations = 8U;
    policy.forward_limits.max_database_operations = 8U;
    policy.forward_limits.candidate_operation_capacity = 4U;
    policy.forward_limits.resolution_capacity = 2U;
    return policy;
}

struct AdmissionOwner final {
    laplace_cognition_prompt_admission* value{};
    ~AdmissionOwner() { laplace_cognition_prompt_admission_destroy(&value); }
};

struct ObservationOwner final {
    laplace_cognition_observation_result* value{};
    ~ObservationOwner() { laplace_cognition_observation_result_destroy(&value); }
};

struct ForwardOwner final {
    laplace_cognition_forward_result* value{};
    ~ForwardOwner() { laplace_cognition_forward_result_destroy(&value); }
};

laplace_cognition_prompt_admission_view Admit(
    const laplace_cognition_prompt_admission_input& input,
    laplace_cognition_prompt_atom_provider_v1& atom_provider,
    laplace_composition_presence_provider_v1& presence_provider,
    AdmissionOwner& owner) {
    EXPECT_EQ(
        laplace_cognition_prompt_admission_create(
            &input, &atom_provider, &presence_provider, &owner.value),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    laplace_cognition_prompt_admission_view view{};
    if (owner.value != nullptr) {
        EXPECT_EQ(
            laplace_cognition_prompt_admission_view_get(owner.value, &view),
            LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    }
    return view;
}

TEST(CognitionPromptStructuralProvider, EnumeratesExactSubtreeAndCodepointFallbackOnly) {
    const std::string prompt = "abc";
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);
    const auto input = Input(prompt, &context, &structure_provider);
    AdmissionOwner admission{};
    const auto view = Admit(input, atom_provider, presence_provider, admission);
    ASSERT_NE(admission.value, nullptr);
    EXPECT_TRUE(SameId(view.trunk_entity_id, Composite(prompt)));

    laplace_cognition_observation_candidate_provider_v1 provider{};
    ASSERT_EQ(
        laplace_cognition_prompt_admission_structural_provider(
            admission.value, &provider),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    EXPECT_FALSE(ZeroDigest(provider.provider_fingerprint));
    EXPECT_GE(provider.maximum_candidate_records_per_expansion, UINT64_C(4));

    laplace_observation_query_binding binding{};
    binding.anchor_entity_id = view.trunk_entity_id;
    binding.relation_mask =
        LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
        LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    binding.maximum_results = 16U;
    laplace_query_search_state frontier{};
    frontier.depth = 0U;
    const std::uint64_t cost = 0U;
    std::array<laplace_cognition_observation_candidate, 32> candidates{};
    std::size_t count = 0U;
    laplace_cognition_observation_candidate_usage usage{};
    ASSERT_EQ(
        provider.enumerate_candidates(
            provider.state, &binding, &view.trunk_entity_id, &frontier, &cost, 1U,
            candidates.data(), candidates.size(), &count, &usage),
        0);
    ASSERT_GT(count, 0U);
    EXPECT_EQ(usage.crossing_count, static_cast<std::uint64_t>(count));

    bool saw_subtree = false;
    bool saw_a = false;
    bool saw_b = false;
    bool saw_c = false;
    const auto subtree = Composite("bc");
    const auto a = Codepoint(static_cast<std::uint32_t>('a'));
    const auto b = Codepoint(static_cast<std::uint32_t>('b'));
    const auto c = Codepoint(static_cast<std::uint32_t>('c'));
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& candidate = candidates[index];
        EXPECT_EQ(candidate.relation_family, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
        EXPECT_EQ(candidate.source_layer, LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY);
        EXPECT_EQ(candidate.direction, LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD);
        EXPECT_EQ(candidate.flags, 0U);
        saw_subtree = saw_subtree || SameId(candidate.target_entity_id, subtree);
        saw_a = saw_a || SameId(candidate.target_entity_id, a);
        saw_b = saw_b || SameId(candidate.target_entity_id, b);
        saw_c = saw_c || SameId(candidate.target_entity_id, c);
    }
    EXPECT_TRUE(saw_subtree);
    EXPECT_TRUE(saw_a);
    EXPECT_TRUE(saw_b);
    EXPECT_TRUE(saw_c);
}

TEST(CognitionPromptStructuralProvider, TurnAndForwardReceiptBeginAtWholeTrunk) {
    const std::string prompt = "abc";
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);
    const auto input = Input(prompt, &context, &structure_provider);
    AdmissionOwner admission{};
    const auto view = Admit(input, atom_provider, presence_provider, admission);
    ASSERT_NE(admission.value, nullptr);

    laplace_cognition_observation_candidate_provider_v1 provider{};
    ASSERT_EQ(
        laplace_cognition_prompt_admission_structural_provider(
            admission.value, &provider),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);

    const auto goal = Codepoint(static_cast<std::uint32_t>('b'));
    const auto policy = Policy(goal);
    laplace_cognition_observation_request request{};
    laplace_cognition_turn_receipt turn_receipt{};
    ASSERT_EQ(
        laplace_cognition_turn_compile(
            &view.turn, &policy, nullptr, 0U, &request, &turn_receipt),
        LAPLACE_COGNITION_TURN_OK);
    EXPECT_TRUE(SameId(request.anchor_entity_id, view.trunk_entity_id));
    EXPECT_TRUE(SameId(request.goal_entity_id, goal));
    EXPECT_TRUE(SameId(view.turn.observation_entity_id, view.trunk_entity_id));
    EXPECT_FALSE(ZeroDigest(turn_receipt.turn_fingerprint));

    ObservationOwner observation{};
    ForwardOwner forward{};
    laplace_cognition_forward_receipt forward_receipt{};
    ASSERT_EQ(
        laplace_cognition_observation_request_execute_with_candidate_provider(
            &request, &provider, &observation.value, &forward.value,
            &forward_receipt),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    ASSERT_NE(observation.value, nullptr);
    ASSERT_NE(forward.value, nullptr);
    ASSERT_EQ(
        laplace_cognition_observation_result_answer_count(observation.value),
        1U);
    laplace_cognition_observation_answer answer{};
    ASSERT_EQ(
        laplace_cognition_observation_result_answer(
            observation.value, 0U, &answer),
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK);
    EXPECT_TRUE(SameId(answer.entity_id, goal));
    EXPECT_EQ(answer.relation_family, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
    EXPECT_EQ(answer.source_layer, LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY);
    EXPECT_EQ(answer.transition_count, UINT64_C(1));

    ASSERT_EQ(laplace_cognition_forward_result_layer_count(forward.value), 1U);
    laplace_cognition_forward_layer_receipt layer{};
    ASSERT_EQ(
        laplace_cognition_forward_result_layer(forward.value, 0U, &layer),
        LAPLACE_COGNITION_FORWARD_OK);
    EXPECT_FALSE(ZeroDigest(layer.selected_operation_id));
    EXPECT_FALSE(ZeroDigest(layer.decision_receipt_id));
    EXPECT_FALSE(ZeroDigest(layer.execution_receipt_id));
    EXPECT_FALSE(ZeroDigest(forward_receipt.program_fingerprint));
    EXPECT_EQ(forward_receipt.final_completion, LAPLACE_COGNITION_COMPLETION_COMPLETE);
}

}  // namespace
