#include "laplace/cognition_prompt_conversation.h"
#include "laplace/cognition_interpretation.h"

#include "laplace/identity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

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

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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
    const laplace_decomposition_content*,
    const laplace_decomposition_span*,
    laplace_decomposition_emit_fn,
    void*) {
    if (opaque == nullptr) return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    ++static_cast<StructureFixture*>(opaque)->calls;
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

laplace_cognition_prompt_admission_input PromptInput(
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

struct SemanticFixture final {
    laplace_id128 source{};
    laplace_id128 target{};
    std::size_t calls{};
};

int EnumerateSemantic(
    void* const opaque,
    const laplace_observation_query_binding* const binding,
    const laplace_id128* const source_entity_ids,
    const laplace_query_search_state* const frontier_states,
    const std::uint64_t* const accumulated_costs,
    const std::size_t frontier_state_count,
    laplace_cognition_observation_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_observation_candidate_usage* const usage) {
    if (opaque == nullptr || binding == nullptr || source_entity_ids == nullptr ||
        frontier_states == nullptr || accumulated_costs == nullptr ||
        frontier_state_count == 0U || candidates == nullptr ||
        candidate_capacity == 0U || candidate_count == nullptr || usage == nullptr) {
        return 1;
    }
    auto& fixture = *static_cast<SemanticFixture*>(opaque);
    ++fixture.calls;
    *candidate_count = 0U;
    *usage = laplace_cognition_observation_candidate_usage{};
    usage->rows_examined = static_cast<std::uint64_t>(frontier_state_count);
    usage->index_plan_count = 1U;
    if ((binding->relation_mask & LAPLACE_OBSERVATION_QUERY_SEMANTIC) == 0U) return 0;

    for (std::size_t index = 0U; index < frontier_state_count; ++index) {
        (void)accumulated_costs[index];
        if (!SameId(source_entity_ids[index], fixture.source)) continue;
        if (*candidate_count >= candidate_capacity) return 2;
        auto& candidate = candidates[*candidate_count];
        candidate = laplace_cognition_observation_candidate{};
        candidate.target_entity_id = fixture.target;
        candidate.relation_id = Codepoint(static_cast<std::uint32_t>('R'));
        candidate.observation_fingerprint = Digest(0xA1U);
        candidate.evidence_root_fingerprint = Digest(0xA2U);
        candidate.source_state_index = static_cast<std::uint64_t>(index);
        candidate.source_logical_ordinal = frontier_states[index].depth;
        candidate.target_logical_ordinal =
            static_cast<std::uint64_t>(frontier_states[index].depth) + 1U;
        candidate.multiplicity = 1U;
        candidate.gap = 1U;
        candidate.relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
        candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
        candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
        candidate.flags =
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;
        ++*candidate_count;
    }
    usage->crossing_count = static_cast<std::uint64_t>(*candidate_count);
    return 0;
}

laplace_cognition_observation_candidate_provider_v1 SemanticProvider(
    SemanticFixture* const fixture) {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0xA0U);
    provider.maximum_candidate_records_per_expansion = 2U;
    provider.enumerate_candidates = EnumerateSemantic;
    provider.abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider.abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    return provider;
}

struct RealizationFixture final {
    laplace_id128 content{};
    std::size_t calls{};
};

int EnumerateRealization(
    void* const opaque,
    const laplace_cognition_semantic_act* const semantic_act,
    const laplace_cognition_realization_request* const request,
    laplace_cognition_realization_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_realization_usage* const usage) {
    if (opaque == nullptr || semantic_act == nullptr || request == nullptr ||
        candidates == nullptr || candidate_capacity == 0U ||
        candidate_count == nullptr || usage == nullptr) {
        return 1;
    }
    auto& fixture = *static_cast<RealizationFixture*>(opaque);
    ++fixture.calls;
    if (!SameId(semantic_act->primary_answer.entity_id, fixture.content)) return 2;
    *candidate_count = 1U;
    *usage = laplace_cognition_realization_usage{};
    usage->provider_receipt_id = Digest(0xB0U);
    usage->rows_examined = 1U;
    usage->exact_whole_examined = 1U;
    usage->disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    auto& candidate = candidates[0];
    candidate = laplace_cognition_realization_candidate{};
    candidate.content_id = fixture.content;
    candidate.language_id = request->language_id;
    candidate.candidate_receipt_id = Digest(0xB1U);
    candidate.realization_recipe_id = Digest(0xB2U);
    candidate.obligation_fingerprint = Digest(0xB3U);
    candidate.preference_rank = 1U;
    candidate.reused_subtree_count = 1U;
    candidate.structural_tier = 0U;
    candidate.match_class = LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
    candidate.flags = LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    return 0;
}

laplace_cognition_realization_provider_v1 RealizationProvider(
    RealizationFixture* const fixture) {
    laplace_cognition_realization_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0xB4U);
    provider.maximum_candidate_records = 1U;
    provider.enumerate = EnumerateRealization;
    provider.abi_major = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

struct MaterializationFixture final {
    laplace_cognition_materialization_node node{};
    std::size_t calls{};
};

int ResolveNode(
    void* const opaque,
    const laplace_id128* const entity_id,
    laplace_cognition_materialization_node* const node) {
    if (opaque == nullptr || entity_id == nullptr || node == nullptr) return 1;
    auto& fixture = *static_cast<MaterializationFixture*>(opaque);
    ++fixture.calls;
    if (!SameId(*entity_id, fixture.node.entity_id)) return 2;
    *node = fixture.node;
    return 0;
}

int ReadTrajectory(
    void*,
    const laplace_cognition_materialization_node*,
    laplace_trajectory_carrier*,
    std::size_t,
    laplace_digest256*) {
    return 1;
}

laplace_cognition_materialization_provider_v1 MaterializationProvider(
    MaterializationFixture* const fixture) {
    laplace_cognition_materialization_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0xC0U);
    provider.resolve_node = ResolveNode;
    provider.read_trajectory = ReadTrajectory;
    provider.abi_major = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

laplace_cognition_prompt_conversation_request ConversationPolicy(
    const laplace_id128& goal) {
    laplace_cognition_prompt_conversation_request request{};
    request.cognition_policy.goal_entity_id = goal;
    request.cognition_policy.evidence_boundary = Digest(0xD0U);
    request.cognition_policy.evidence_epoch = Digest(0xD1U);
    request.cognition_policy.authority_id = Digest(0xD2U);
    request.cognition_policy.result_contract_fingerprint = Digest(0xD3U);
    request.cognition_policy.relation_mask =
        LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
        LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    request.cognition_policy.maximum_results = 1U;
    /* GOAL_PRESENT keeps intermediate structural states expandable; marking every
     * candidate terminal here would stop at B/A before the semantic A -> C hop. */
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
    request.cognition_policy.search_budget.max_depth = 3U;
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

    request.realization_policy.modality_id = Codepoint(static_cast<std::uint32_t>('T'));
    request.realization_policy.language_id = Codepoint(static_cast<std::uint32_t>('E'));
    request.realization_policy.realization_recipe_epoch = Digest(0xE0U);
    request.realization_policy.maximum_candidates = 1U;
    request.realization_policy.flags = LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT;
    request.realization_policy.version =
        LAPLACE_COGNITION_CONVERSATION_REALIZATION_POLICY_VERSION;

    request.materialization.maximum_nodes = 8U;
    request.materialization.maximum_trajectory_carriers = 8U;
    request.materialization.maximum_output_bytes = 64U;
    request.materialization.maximum_depth = 4U;
    request.materialization.version = LAPLACE_COGNITION_MATERIALIZATION_VERSION;

    request.discourse_roots.active_entity_set_fingerprint = Digest(0xE1U);
    request.discourse_roots.flags = LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES;
    request.discourse_roots.version =
        LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_VERSION;
    request.version = LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION;
    return request;
}

struct AdmissionOwner final {
    laplace_cognition_prompt_admission* value{};
    ~AdmissionOwner() { laplace_cognition_prompt_admission_destroy(&value); }
};

TEST(CognitionPromptConversation, RawPromptTraversesStructureThenSemanticProviderToExactUtf8) {
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
    laplace_cognition_prompt_admission_view admission_view{};
    ASSERT_EQ(
        laplace_cognition_prompt_admission_view_get(
            admission.value, &admission_view),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);

    SemanticFixture semantic{Codepoint(static_cast<std::uint32_t>('A')),
                             Codepoint(static_cast<std::uint32_t>('C')), 0U};
    auto semantic_provider = SemanticProvider(&semantic);
    RealizationFixture realization{semantic.target, 0U};
    auto realization_provider = RealizationProvider(&realization);
    MaterializationFixture materialization{};
    ASSERT_EQ(
        laplace_identity_codepoint_witness(
            static_cast<std::uint32_t>('C'), &materialization.node.entity_id,
            &materialization.node.identity_witness),
        LAPLACE_IDENTITY_OK);
    materialization.node.node_receipt_id = Digest(0xC1U);
    materialization.node.logical_count = 1U;
    materialization.node.atom = static_cast<std::uint32_t>('C');
    materialization.node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
    auto materialization_provider = MaterializationProvider(&materialization);
    auto request = ConversationPolicy(semantic.target);

    std::array<std::uint8_t, 64> output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    std::size_t output_bytes = 0U;
    std::size_t frame_bytes = 0U;
    laplace_cognition_prompt_conversation_result result{};
    ASSERT_EQ(
        laplace_cognition_prompt_conversation_execute(
            admission.value, &request, nullptr, 0U, &semantic_provider, 1U,
            &realization_provider, &materialization_provider,
            output.data(), output.size(), &output_bytes,
            frame.data(), frame.size(), &frame_bytes, &result),
        LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);

    ASSERT_EQ(output_bytes, 1U);
    EXPECT_EQ(output[0], static_cast<std::uint8_t>('C'));
    EXPECT_EQ(frame_bytes, frame.size());
    EXPECT_GT(semantic.calls, 0U);
    EXPECT_EQ(realization.calls, 1U);
    EXPECT_EQ(materialization.calls, 1U);
    EXPECT_TRUE(SameDigest(
        result.prompt_admission_receipt_id,
        admission_view.admission_receipt_id));
    EXPECT_TRUE(SameDigest(
        result.prompt_exact_bytes_fingerprint,
        admission_view.exact_bytes_fingerprint));
    EXPECT_FALSE(ZeroDigest(result.composite_cognition_provider_fingerprint));
    EXPECT_TRUE(SameId(
        result.conversation.cognition_request.anchor_entity_id,
        admission_view.trunk_entity_id));
    EXPECT_TRUE(SameId(
        result.conversation.semantic_act.primary_answer.entity_id,
        semantic.target));
    EXPECT_TRUE(SameId(result.conversation.realization.content_id, semantic.target));
    EXPECT_GE(
        result.conversation.semantic_act.primary_answer.transition_count,
        UINT64_C(2));
    EXPECT_EQ(result.version, LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION);
}


// One admitted prompt fixture crosses the actual public response chain. The
// providers below are test witnesses, not a language model or product seed.
struct PromptResponseFixture final {
    const std::string prompt{"BA"};
    laplace_framework_context context{laplace_test_context(3U)};
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    laplace_decomposition_provider_v1 structure_provider{};
    laplace_cognition_prompt_atom_provider_v1 atom_provider{};
    laplace_composition_presence_provider_v1 presence_provider{};
    AdmissionOwner admission{};
    SemanticFixture semantic{};
    RealizationFixture realization{};
    MaterializationFixture materialization{};
    laplace_cognition_observation_candidate_provider_v1 semantic_provider{};
    laplace_cognition_realization_provider_v1 realization_provider{};
    laplace_cognition_materialization_provider_v1 materialization_provider{};
    laplace_cognition_prompt_conversation_request request{};
    std::array<std::uint8_t, 64> output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    std::size_t output_bytes{};
    std::size_t frame_bytes{};
    laplace_cognition_prompt_conversation_result result{};

    void Initialize(const std::uint32_t codepoint) {
        context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
        structure_provider = StructureProvider(&structure);
        atom_provider = AtomProvider(&atoms);
        presence_provider = PresenceProvider(&presence);
        auto input = PromptInput(prompt, &context, &structure_provider);
        ASSERT_EQ(laplace_cognition_prompt_admission_create(
                      &input, &atom_provider, &presence_provider, &admission.value),
                  LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
        semantic.source = Codepoint(static_cast<std::uint32_t>('A'));
        semantic.target = Codepoint(codepoint);
        semantic_provider = SemanticProvider(&semantic);
        realization.content = semantic.target;
        realization_provider = RealizationProvider(&realization);
        ASSERT_EQ(laplace_identity_codepoint_witness(
                      codepoint, &materialization.node.entity_id,
                      &materialization.node.identity_witness), LAPLACE_IDENTITY_OK);
        materialization.node.node_receipt_id = Digest(0xC1U);
        materialization.node.logical_count = 1U;
        materialization.node.atom = codepoint;
        materialization.node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
        materialization_provider = MaterializationProvider(&materialization);
        request = ConversationPolicy(semantic.target);
        output.fill(0xA5U);
        frame.fill(0xA5U);
    }

    laplace_cognition_prompt_conversation_status Execute(
        const std::uint32_t encoding, const std::size_t count = 1U) {
        return laplace_cognition_prompt_conversation_execute_encoded(
            admission.value, &request, encoding, nullptr, 0U,
            count == 0U ? nullptr : &semantic_provider, count,
            &realization_provider, &materialization_provider,
            output.data(), output.size(), &output_bytes,
            frame.data(), frame.size(), &frame_bytes, &result);
    }

    void ExpectUnpublished() const {
        EXPECT_EQ(output_bytes, 0U);
        EXPECT_EQ(frame_bytes, 0U);
        EXPECT_TRUE(std::all_of(output.begin(), output.end(),
            [](std::uint8_t value) { return value == 0xA5U; }));
        EXPECT_TRUE(std::all_of(frame.begin(), frame.end(),
            [](std::uint8_t value) { return value == 0xA5U; }));
        EXPECT_TRUE(ZeroDigest(result.conversation.conversation_id));
        EXPECT_EQ(result.version, 0U);
    }
};

TEST(CognitionPromptConversation, ExplicitUtf8PreservesCompatibilityBytesFramesAndReceipts) {
    PromptResponseFixture fixture;
    fixture.Initialize(0x80U);
    ASSERT_NE(fixture.admission.value, nullptr);
    ASSERT_EQ(fixture.Execute(LAPLACE_COGNITION_OUTPUT_UTF8),
              LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);
    const auto encoded = fixture.result;
    const auto bytes = fixture.output;
    const auto frame = fixture.frame;
    ASSERT_EQ(laplace_cognition_prompt_conversation_execute(
                  fixture.admission.value, &fixture.request, nullptr, 0U,
                  &fixture.semantic_provider, 1U, &fixture.realization_provider,
                  &fixture.materialization_provider, fixture.output.data(),
                  fixture.output.size(), &fixture.output_bytes, fixture.frame.data(),
                  fixture.frame.size(), &fixture.frame_bytes, &fixture.result),
              LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);
    EXPECT_EQ(fixture.output_bytes, 2U);
    EXPECT_EQ(fixture.output, bytes);
    EXPECT_EQ(fixture.frame, frame);
    EXPECT_TRUE(SameDigest(encoded.conversation.conversation_id,
                           fixture.result.conversation.conversation_id));
    EXPECT_TRUE(SameDigest(encoded.prompt_admission_receipt_id,
                           fixture.result.prompt_admission_receipt_id));
}

TEST(CognitionPromptConversation, OctetSelectionSurvivesWholePromptExecutionWithoutSemanticDrift) {
    for (const std::uint32_t codepoint : {0U, 0x41U, 0x80U, 0xFFU}) {
        PromptResponseFixture fixture;
        fixture.Initialize(codepoint);
        ASSERT_NE(fixture.admission.value, nullptr);
        ASSERT_EQ(fixture.Execute(LAPLACE_COGNITION_OUTPUT_UTF8),
                  LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);
        const auto text = fixture.result;
        const auto text_frame = fixture.frame;
        fixture.semantic.calls = 0U;
        fixture.realization.calls = 0U;
        fixture.materialization.calls = 0U;
        ASSERT_EQ(fixture.Execute(LAPLACE_COGNITION_OUTPUT_OCTETS),
                  LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);
        EXPECT_EQ(fixture.output_bytes, 1U);
        EXPECT_EQ(fixture.output[0], static_cast<std::uint8_t>(codepoint));
        EXPECT_EQ(fixture.frame_bytes, fixture.frame.size());
        EXPECT_EQ(fixture.frame, text_frame);
        EXPECT_EQ(fixture.realization.calls, 1U);
        EXPECT_EQ(fixture.materialization.calls, 1U);
        EXPECT_TRUE(SameDigest(text.prompt_admission_receipt_id,
                               fixture.result.prompt_admission_receipt_id));
        EXPECT_TRUE(SameDigest(text.conversation.semantic_act.act_id,
                               fixture.result.conversation.semantic_act.act_id));
        EXPECT_TRUE(SameId(text.conversation.realization.content_id,
                           fixture.result.conversation.realization.content_id));
        EXPECT_FALSE(SameDigest(text.conversation.conversation_id,
                                fixture.result.conversation.conversation_id));
    }
}

TEST(CognitionPromptConversation, UnknownEncodingRejectsBeforeProviderExecution) {
    PromptResponseFixture fixture;
    fixture.Initialize(0x80U);
    ASSERT_EQ(fixture.Execute(UINT32_MAX),
              LAPLACE_COGNITION_PROMPT_CONVERSATION_ENCODING_INVALID);
    EXPECT_EQ(fixture.semantic.calls, 0U);
    EXPECT_EQ(fixture.realization.calls, 0U);
    EXPECT_EQ(fixture.materialization.calls, 0U);
    fixture.ExpectUnpublished();
}

TEST(CognitionPromptConversation, ProviderCountOverflowRejectsWithoutCallerBufferAccess) {
    PromptResponseFixture fixture;
    fixture.Initialize(0x80U);
    ASSERT_EQ(fixture.Execute(LAPLACE_COGNITION_OUTPUT_UTF8, SIZE_MAX),
              LAPLACE_COGNITION_PROMPT_CONVERSATION_MEMORY_FAILURE);
    EXPECT_EQ(fixture.semantic.calls, 0U);
    EXPECT_EQ(fixture.realization.calls, 0U);
    EXPECT_EQ(fixture.materialization.calls, 0U);
    fixture.ExpectUnpublished();
}

TEST(CognitionPromptConversation, FailedOctetMaterializationPublishesNeitherResponseNorDiscourse) {
    PromptResponseFixture fixture;
    fixture.Initialize(0x4E8BU);
    ASSERT_EQ(fixture.Execute(LAPLACE_COGNITION_OUTPUT_OCTETS),
              LAPLACE_COGNITION_PROMPT_CONVERSATION_CONVERSATION_FAILURE);
    EXPECT_EQ(fixture.realization.calls, 1U);
    EXPECT_EQ(fixture.materialization.calls, 1U);
    fixture.ExpectUnpublished();
}

TEST(CognitionPromptConversation, StructuralPromptRemainsUsableWithoutAdditionalProviders) {
    PromptResponseFixture fixture;
    fixture.Initialize(static_cast<std::uint32_t>('A'));
    ASSERT_EQ(fixture.Execute(LAPLACE_COGNITION_OUTPUT_UTF8, 0U),
              LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);
    EXPECT_EQ(fixture.semantic.calls, 0U);
    EXPECT_EQ(fixture.realization.calls, 1U);
    EXPECT_EQ(fixture.materialization.calls, 1U);
    EXPECT_EQ(fixture.output_bytes, 1U);
    EXPECT_EQ(fixture.output[0], static_cast<std::uint8_t>('A'));
}

TEST(CognitionPromptConversation, InterpretedGoalRequiresUniqueBindingInExactPromptScope) {
    for (const auto scenario : {0U, 1U, 2U}) {
        SCOPED_TRACE(scenario);
        PromptResponseFixture fixture;
        fixture.Initialize(0x80U);
        laplace_cognition_prompt_admission_view view{};
        ASSERT_EQ(laplace_cognition_prompt_admission_view_get(fixture.admission.value, &view),
                  LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
        const auto slot_id = Digest(0xA0U);
        const std::uint32_t slot = 0U;
        const std::array<laplace_id128, 2> values{fixture.semantic.target, fixture.semantic.source};
        std::array<laplace_cognition_interpretation_row, 2> rows{};
        for (std::size_t index = 0U; index < rows.size(); ++index) {
            rows[index].values = &values[index];
            rows[index].observation_id = Digest(static_cast<std::uint8_t>(0xB0U + index));
            rows[index].evidence_root_id = Digest(static_cast<std::uint8_t>(0xC0U + index));
        }
        laplace_cognition_interpretation_factor factor{};
        factor.factor_id = Digest(0xD0U);
        factor.law_id = Digest(0xD1U);
        factor.slots = &slot;
        factor.slot_count = 1U;
        factor.rows = rows.data();
        factor.row_count = scenario == 1U ? 2U : 1U;
        laplace_cognition_interpretation_program program{};
        program.observation_root = view.turn.observation_entity_id;
        program.occurrence_id = view.turn.observation_occurrence_id;
        program.world_id = view.turn.world_id;
        program.time_fingerprint = view.turn.time_fingerprint;
        program.context_id = view.turn.context_fingerprint;
        if (scenario == 2U) program.context_id.bytes[0] ^= 1U;
        program.evidence_epoch = fixture.request.cognition_policy.evidence_epoch;
        program.boundary_id = fixture.request.cognition_policy.evidence_boundary;
        program.authority_id = fixture.request.cognition_policy.authority_id;
        program.slot_ids = &slot_id;
        program.slot_count = 1U;
        program.maximum_comparisons = 1000U;
        program.maximum_states = 64U;
        program.maximum_memory_bytes = 1024U * 1024U;
        program.version = LAPLACE_COGNITION_INTERPRETATION_VERSION;
        struct Owner {
            laplace_cognition_interpretation_result* value{};
            ~Owner() { laplace_cognition_interpretation_destroy(&value); }
        } interpretation;
        laplace_cognition_interpretation_receipt receipt{};
        ASSERT_EQ(laplace_cognition_interpretation_execute(
                      &program, &factor, 1U, &interpretation.value, &receipt),
                  LAPLACE_COGNITION_INTERPRETATION_OK);
        EXPECT_EQ(receipt.disposition, scenario == 1U
            ? LAPLACE_COGNITION_INTERPRETATION_AMBIGUOUS : LAPLACE_COGNITION_INTERPRETATION_UNIQUE);
        fixture.request.cognition_policy.goal_entity_id = {};
        fixture.request.cognition_policy.request_flags &=
            ~static_cast<std::uint32_t>(LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT);
        const auto status = laplace_cognition_prompt_conversation_execute_interpreted(
            fixture.admission.value, &fixture.request, interpretation.value, &slot_id,
            nullptr, 0U, &fixture.semantic_provider, 1U,
            &fixture.realization_provider, &fixture.materialization_provider,
            fixture.output.data(), fixture.output.size(), &fixture.output_bytes,
            fixture.frame.data(), fixture.frame.size(), &fixture.frame_bytes, &fixture.result);
        if (scenario == 0U) {
            EXPECT_EQ(status, LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);
            EXPECT_EQ(fixture.output_bytes, 2U);
            EXPECT_EQ(fixture.output[0], 0xC2U);
            EXPECT_EQ(fixture.output[1], 0x80U);
            EXPECT_TRUE(SameId(fixture.result.conversation.semantic_act.primary_answer.entity_id,
                               fixture.semantic.target));
        } else {
            EXPECT_EQ(status, LAPLACE_COGNITION_PROMPT_CONVERSATION_INTERPRETATION_FAILURE);
            EXPECT_EQ(fixture.semantic.calls, 0U);
            EXPECT_EQ(fixture.realization.calls, 0U);
            EXPECT_EQ(fixture.materialization.calls, 0U);
            fixture.ExpectUnpublished();
        }
    }
}

}  // namespace
