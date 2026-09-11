#include "laplace/cognition_prompt_admission.h"

#include "laplace/identity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <tuple>
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
    std::uint8_t disposition{LAPLACE_COMPOSITION_NOVEL};
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
        fixture.disposition);
    if (physicality_count != 0U) {
        std::fill_n(
            physicality_dispositions, physicality_count,
            fixture.disposition);
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
    ASSERT_EQ(laplace_observation_query_binding_identify(
                  &binding, &binding.binding_fingerprint), LAPLACE_OBSERVATION_QUERY_OK);
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


// These tests execute admitted compositions and packed trajectories, not an
// endpoint-edge fixture or a text-to-operation dispatcher.
struct PromptIndexFixture final {
    laplace_framework_context context{laplace_test_context(3U)};
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    AdmissionOwner admission{};
    laplace_cognition_prompt_admission_view view{};
    laplace_cognition_observation_candidate_provider_v1 provider{};

    explicit PromptIndexFixture(
        const std::string& prompt, const bool already_present = false) {
        context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
        if (already_present) presence.disposition = LAPLACE_COMPOSITION_EXACT_PRESENT;
        auto structure_provider = StructureProvider(&structure);
        auto atom_provider = AtomProvider(&atoms);
        auto presence_provider = PresenceProvider(&presence);
        auto input = Input(prompt, &context, &structure_provider);
        input.decomposition.maximum_spans =
            static_cast<std::uint64_t>(prompt.size()) * 2U + 8U;
        view = Admit(input, atom_provider, presence_provider, admission);
        if (admission.value != nullptr) {
            EXPECT_EQ(laplace_cognition_prompt_admission_structural_provider(
                          admission.value, &provider),
                      LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
        }
    }
};

laplace_observation_query_binding Binding(
    const laplace_id128& anchor, const std::uint32_t relations) {
    laplace_observation_query_binding binding{};
    binding.anchor_entity_id = anchor;
    binding.relation_mask = relations;
    binding.maximum_results = 64U;
    EXPECT_EQ(laplace_observation_query_binding_identify(
                  &binding, &binding.binding_fingerprint), LAPLACE_OBSERVATION_QUERY_OK);
    return binding;
}

struct CandidateBatch final {
    std::vector<laplace_cognition_observation_candidate> values;
    laplace_cognition_observation_candidate_usage usage{};
    int status{};
};

CandidateBatch Query(
    const laplace_cognition_observation_candidate_provider_v1& provider,
    const laplace_observation_query_binding& binding,
    const std::vector<laplace_id128>& sources,
    const std::uint32_t depth = 0U,
    const std::size_t capacity = 1024U) {
    CandidateBatch batch{};
    batch.values.resize(capacity);
    std::vector<laplace_query_search_state> states(sources.size());
    std::vector<std::uint64_t> costs(sources.size(), depth);
    for (auto& state : states) state.depth = depth;
    std::size_t count = 0U;
    if (provider.enumerate_candidates == nullptr) {
        ADD_FAILURE() << "admission did not produce a structural provider";
        batch.status = -1;
        batch.values.clear();
        return batch;
    }
    batch.status = provider.enumerate_candidates(
        provider.state, &binding, sources.data(), states.data(), costs.data(),
        sources.size(), batch.values.data(), batch.values.size(), &count, &batch.usage);
    EXPECT_LE(count, capacity);
    batch.values.resize(std::min(count, capacity));
    return batch;
}

template<std::size_t Size>
std::array<std::uint8_t, Size> Bytes(const std::uint8_t (&value)[Size]) {
    std::array<std::uint8_t, Size> bytes{};
    std::copy_n(value, Size, bytes.data());
    return bytes;
}

auto CandidateFields(const laplace_cognition_observation_candidate& candidate) {
    return std::make_tuple(
        Bytes(candidate.target_entity_id.bytes), Bytes(candidate.relation_id.bytes),
        Bytes(candidate.observation_fingerprint.bytes),
        Bytes(candidate.evidence_root_fingerprint.bytes), candidate.source_state_index,
        candidate.source_logical_ordinal, candidate.target_logical_ordinal,
        candidate.multiplicity, candidate.gap, candidate.relation_family,
        candidate.source_layer, candidate.direction, candidate.flags, candidate.reserved);
}

void ExpectEqualCandidates(const CandidateBatch& left, const CandidateBatch& right) {
    ASSERT_EQ(left.status, 0);
    ASSERT_EQ(right.status, 0);
    ASSERT_EQ(left.usage.limiting_disposition, 0U);
    ASSERT_EQ(right.usage.limiting_disposition, 0U);
    ASSERT_EQ(left.values.size(), right.values.size());
    for (std::size_t i = 0U; i < left.values.size(); ++i) {
        EXPECT_EQ(CandidateFields(left.values[i]), CandidateFields(right.values[i]));
    }
}

TEST(CognitionPromptStructuralProvider, OccurrenceCoordinatesDoNotDependOnSearchDepth) {
    PromptIndexFixture fixture("abc");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto binding = Binding(fixture.view.trunk_entity_id,
        LAPLACE_OBSERVATION_QUERY_CONTAINER | LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
    const std::vector<laplace_id128> sources{fixture.view.trunk_entity_id, Codepoint('b')};
    const auto shallow = Query(fixture.provider, binding, sources);
    const auto deep = Query(fixture.provider, binding, sources, 123U);
    ASSERT_FALSE(shallow.values.empty());
    ExpectEqualCandidates(shallow, deep);
    EXPECT_EQ(shallow.usage.rows_examined, deep.usage.rows_examined);
}

TEST(CognitionPromptStructuralProvider, PackedOrderSuppliesPredecessorSuccessorAndCooccurrence) {
    PromptIndexFixture fixture("ab");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto a = Codepoint('a'), b = Codepoint('b');
    const auto binding = Binding(fixture.view.trunk_entity_id,
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR | LAPLACE_OBSERVATION_QUERY_SUCCESSOR |
        LAPLACE_OBSERVATION_QUERY_COOCCUR);
    const auto batch = Query(fixture.provider, binding, {a, b});
    ASSERT_EQ(batch.status, 0);
    ASSERT_EQ(batch.usage.limiting_disposition, 0U);
    ASSERT_EQ(batch.values.size(), 4U);
    bool successor = false, predecessor = false, forward_cooccurs = false, reverse_cooccurs = false;
    for (const auto& candidate : batch.values) {
        EXPECT_EQ(candidate.source_layer, LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY);
        EXPECT_TRUE(ZeroDigest(candidate.evidence_root_fingerprint));
        EXPECT_EQ(Bytes(candidate.observation_fingerprint.bytes),
                  Bytes(fixture.view.trunk_physicality_id.bytes));
        EXPECT_EQ(candidate.multiplicity, UINT64_C(1));
        if (candidate.source_state_index == 0U) {
            EXPECT_TRUE(SameId(candidate.target_entity_id, b));
            EXPECT_EQ(candidate.source_logical_ordinal, UINT64_C(1));
            EXPECT_EQ(candidate.target_logical_ordinal, UINT64_C(2));
            successor |= candidate.relation_family == LAPLACE_OBSERVATION_QUERY_SUCCESSOR;
            forward_cooccurs |= candidate.relation_family == LAPLACE_OBSERVATION_QUERY_COOCCUR;
        } else {
            EXPECT_EQ(candidate.source_state_index, UINT64_C(1));
            EXPECT_TRUE(SameId(candidate.target_entity_id, a));
            EXPECT_EQ(candidate.source_logical_ordinal, UINT64_C(2));
            EXPECT_EQ(candidate.target_logical_ordinal, UINT64_C(1));
            predecessor |= candidate.relation_family == LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
            reverse_cooccurs |= candidate.relation_family == LAPLACE_OBSERVATION_QUERY_COOCCUR;
        }
        if (candidate.relation_family == LAPLACE_OBSERVATION_QUERY_COOCCUR) {
            EXPECT_EQ(candidate.gap, UINT64_C(1));
            EXPECT_EQ(candidate.direction, LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC);
        }
    }
    EXPECT_TRUE(successor);
    EXPECT_TRUE(predecessor);
    EXPECT_TRUE(forward_cooccurs);
    EXPECT_TRUE(reverse_cooccurs);
}

TEST(CognitionPromptStructuralProvider, NonadjacentRepeatedConstituentsRetainEveryOrdinal) {
    PromptIndexFixture fixture("ababa");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto binding = Binding(fixture.view.trunk_entity_id, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
    const auto batch = Query(fixture.provider, binding, {fixture.view.trunk_entity_id});
    ASSERT_EQ(batch.status, 0);
    std::vector<std::uint64_t> ordinals;
    for (const auto& candidate : batch.values) {
        if (SameId(candidate.target_entity_id, Codepoint('a')) &&
            Bytes(candidate.observation_fingerprint.bytes) == Bytes(fixture.view.trunk_physicality_id.bytes)) {
            ordinals.push_back(candidate.target_logical_ordinal);
            EXPECT_EQ(candidate.multiplicity, UINT64_C(1));
        }
    }
    std::sort(ordinals.begin(), ordinals.end());
    EXPECT_EQ(ordinals, (std::vector<std::uint64_t>{1U, 3U, 5U}));
    EXPECT_EQ(fixture.view.semantic_attestation_count, UINT64_C(0));
}

TEST(CognitionPromptStructuralProvider, PackedRunsRetainInternalTransitionsWithoutExpansion) {
    PromptIndexFixture fixture("aaaa");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto binding = Binding(fixture.view.trunk_entity_id,
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR | LAPLACE_OBSERVATION_QUERY_SUCCESSOR |
        LAPLACE_OBSERVATION_QUERY_COOCCUR);
    const auto batch = Query(fixture.provider, binding, {Codepoint('a')});
    ASSERT_EQ(batch.status, 0);
    ASSERT_EQ(batch.usage.limiting_disposition, 0U);
    ASSERT_EQ(batch.values.size(), 3U);
    for (const auto& candidate : batch.values) {
        EXPECT_TRUE(SameId(candidate.target_entity_id, Codepoint('a')));
        EXPECT_EQ(candidate.multiplicity, UINT64_C(3));
        EXPECT_TRUE(ZeroDigest(candidate.evidence_root_fingerprint));
        const bool reverse = candidate.relation_family == LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
        EXPECT_EQ(candidate.source_logical_ordinal, reverse ? UINT64_C(2) : UINT64_C(1));
        EXPECT_EQ(candidate.target_logical_ordinal, reverse ? UINT64_C(1) : UINT64_C(2));
    }
    EXPECT_EQ(batch.usage.rows_examined, UINT64_C(3));
    EXPECT_EQ(fixture.view.composition_summary.trajectory_vertex_count, UINT64_C(1));
}

TEST(CognitionPromptStructuralProvider, PublishedAndAlreadyPresentStructuresHaveTheSameCandidates) {
    PromptIndexFixture novel("ababa");
    PromptIndexFixture present("ababa", true);
    ASSERT_NE(novel.provider.state, nullptr);
    ASSERT_NE(present.provider.state, nullptr);
    EXPECT_EQ(Bytes(novel.view.trunk_entity_id.bytes), Bytes(present.view.trunk_entity_id.bytes));
    const auto binding = Binding(novel.view.trunk_entity_id,
        LAPLACE_OBSERVATION_QUERY_CONTAINER | LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR | LAPLACE_OBSERVATION_QUERY_SUCCESSOR |
        LAPLACE_OBSERVATION_QUERY_COOCCUR);
    const std::vector<laplace_id128> sources{novel.view.trunk_entity_id, Codepoint('a'), Codepoint('b')};
    ExpectEqualCandidates(Query(novel.provider, binding, sources), Query(present.provider, binding, sources));
    EXPECT_EQ(Bytes(novel.provider.provider_fingerprint.bytes), Bytes(present.provider.provider_fingerprint.bytes));
    EXPECT_EQ(present.view.composition_summary.novel_physicality_count, UINT64_C(0));
    EXPECT_EQ(present.view.semantic_attestation_count, UINT64_C(0));
}

TEST(CognitionPromptStructuralProvider, CapacityFailurePublishesNoPrefixOrFalseAbsence) {
    PromptIndexFixture fixture("ababa");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto binding = Binding(fixture.view.trunk_entity_id, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
    std::array<laplace_cognition_observation_candidate, 1> buffer{};
    buffer[0].multiplicity = 9876U;
    buffer[0].target_entity_id = Codepoint('z');
    const auto before = CandidateFields(buffer[0]);
    laplace_query_search_state state{};
    std::uint64_t cost = 0U;
    std::size_t count = 999U;
    laplace_cognition_observation_candidate_usage usage{};
    EXPECT_EQ(fixture.provider.enumerate_candidates(
        fixture.provider.state, &binding, &fixture.view.trunk_entity_id, &state, &cost,
        1U, buffer.data(), buffer.size(), &count, &usage), 0);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
    EXPECT_EQ(CandidateFields(buffer[0]), before);
    EXPECT_GT(usage.rows_examined, UINT64_C(0));
    EXPECT_GT(usage.index_plan_count, UINT64_C(0));
    EXPECT_EQ(usage.crossing_count, UINT64_C(0));
    const auto empty = Query(fixture.provider, binding, {fixture.view.trunk_entity_id}, 0U, 0U);
    EXPECT_EQ(empty.status, 0);
    EXPECT_TRUE(empty.values.empty());
    EXPECT_EQ(empty.usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);
}

TEST(CognitionPromptStructuralProvider, BindingIdentityAndEmptyBatchesAreValidated) {
    PromptIndexFixture fixture("abc");
    ASSERT_NE(fixture.provider.state, nullptr);
    auto binding = Binding(fixture.view.trunk_entity_id, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);
    auto empty = Query(fixture.provider, binding, {}, 0U, 0U);
    EXPECT_EQ(empty.status, 0);
    EXPECT_TRUE(empty.values.empty());
    EXPECT_EQ(empty.usage.limiting_disposition, 0U);
    EXPECT_EQ(empty.usage.rows_examined, UINT64_C(0));
    binding.relation_mask = LAPLACE_OBSERVATION_QUERY_CONTAINER;
    auto invalid = Query(fixture.provider, binding, {fixture.view.trunk_entity_id});
    EXPECT_NE(invalid.status, 0);
    EXPECT_TRUE(invalid.values.empty());
    EXPECT_EQ(invalid.usage.rows_examined, UINT64_C(0));
}

TEST(CognitionPromptStructuralProvider, RetainedIndexSupportsConcurrentReplayWithoutRecomposition) {
    PromptIndexFixture fixture("ababa");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto binding = Binding(fixture.view.trunk_entity_id,
        LAPLACE_OBSERVATION_QUERY_CONTAINER | LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR | LAPLACE_OBSERVATION_QUERY_SUCCESSOR |
        LAPLACE_OBSERVATION_QUERY_COOCCUR);
    const std::vector<laplace_id128> sources{fixture.view.trunk_entity_id, Codepoint('a')};
    const auto baseline = Query(fixture.provider, binding, sources);
    const auto structure_calls = fixture.structure.calls;
    const auto atom_calls = fixture.atoms.calls;
    const auto presence_calls = fixture.presence.calls;
    std::vector<std::future<CandidateBatch>> workers;
    for (unsigned i = 0U; i < 8U; ++i) {
        workers.push_back(std::async(std::launch::async, [&] {
            laplace_cognition_observation_candidate_provider_v1 provider{};
            EXPECT_EQ(laplace_cognition_prompt_admission_structural_provider(
                          fixture.admission.value, &provider), LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
            EXPECT_EQ(provider.state, fixture.provider.state);
            EXPECT_EQ(Bytes(provider.provider_fingerprint.bytes), Bytes(fixture.provider.provider_fingerprint.bytes));
            return Query(provider, binding, sources, 37U);
        }));
    }
    for (auto& worker : workers) ExpectEqualCandidates(baseline, worker.get());
    EXPECT_EQ(fixture.structure.calls, structure_calls);
    EXPECT_EQ(fixture.atoms.calls, atom_calls);
    EXPECT_EQ(fixture.presence.calls, presence_calls);
}

TEST(CognitionPromptStructuralProvider, BatchAndScalarProjectionKeepTheSameOccurrences) {
    PromptIndexFixture fixture("ababa");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto binding = Binding(fixture.view.trunk_entity_id,
        LAPLACE_OBSERVATION_QUERY_CONTAINER | LAPLACE_OBSERVATION_QUERY_CONSTITUENT |
        LAPLACE_OBSERVATION_QUERY_PREDECESSOR | LAPLACE_OBSERVATION_QUERY_SUCCESSOR |
        LAPLACE_OBSERVATION_QUERY_COOCCUR);
    const std::vector<laplace_id128> sources{fixture.view.trunk_entity_id, Codepoint('a'), Codepoint('b')};
    auto batched = Query(fixture.provider, binding, sources);
    CandidateBatch scalar{};
    for (std::size_t i = 0U; i < sources.size(); ++i) {
        auto one = Query(fixture.provider, binding, {sources[i]});
        ASSERT_EQ(one.status, 0);
        ASSERT_EQ(one.usage.limiting_disposition, 0U);
        for (auto candidate : one.values) {
            candidate.source_state_index = static_cast<std::uint64_t>(i);
            scalar.values.push_back(candidate);
        }
        scalar.usage.rows_examined += one.usage.rows_examined;
    }
    const auto less = [](const auto& a, const auto& b) { return CandidateFields(a) < CandidateFields(b); };
    std::sort(batched.values.begin(), batched.values.end(), less);
    std::sort(scalar.values.begin(), scalar.values.end(), less);
    ExpectEqualCandidates(batched, scalar);
    EXPECT_EQ(batched.usage.rows_examined, scalar.usage.rows_examined);
}

TEST(CognitionPromptStructuralProvider, UnicodeOrderUsesTheSamePackedCandidateGenerator) {
    // U+0061 followed by U+3042: one ASCII codepoint and one three-byte codepoint.
    PromptIndexFixture fixture("a\xe3\x81\x82");
    ASSERT_NE(fixture.provider.state, nullptr);
    const auto binding = Binding(fixture.view.trunk_entity_id, LAPLACE_OBSERVATION_QUERY_SUCCESSOR);
    const auto batch = Query(fixture.provider, binding, {Codepoint('a')});
    ASSERT_EQ(batch.status, 0);
    ASSERT_EQ(batch.values.size(), 1U);
    EXPECT_TRUE(SameId(batch.values[0].target_entity_id, Codepoint(0x3042U)));
    EXPECT_EQ(batch.values[0].source_logical_ordinal, UINT64_C(1));
    EXPECT_EQ(batch.values[0].target_logical_ordinal, UINT64_C(2));
    EXPECT_EQ(fixture.view.semantic_attestation_count, UINT64_C(0));
}

}  // namespace
