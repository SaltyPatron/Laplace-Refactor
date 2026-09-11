#include "laplace/cognition_prompt_admission.h"

#include "laplace/identity.h"
#include "laplace/persistence.h"

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

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_id128 ExpectedAsciiRoot(const std::string& text) {
    std::vector<laplace_id128> children;
    children.reserve(text.size());
    for (const unsigned char byte : text) {
        laplace_id128 id{};
        EXPECT_EQ(
            laplace_identity_codepoint(static_cast<std::uint32_t>(byte), &id),
            LAPLACE_IDENTITY_OK);
        children.push_back(id);
    }
    laplace_id128 root{};
    EXPECT_GT(children.size(), 1U);
    EXPECT_EQ(
        laplace_identity_composite(children.data(), children.size(), &root),
        LAPLACE_IDENTITY_OK);
    return root;
}

struct StructureFixture final {
    std::size_t apply_calls{};
};

laplace_decomposition_status StructureApplicable(
    void*,
    const laplace_decomposition_content*,
    const laplace_decomposition_span* span,
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
    ++fixture.apply_calls;
    if (span->depth != 0U) return LAPLACE_DECOMPOSITION_OK;

    /* Structural fixture only: expose two exact proper subranges. The prompt root
     * remains the complete byte range created by the decomposition owner. */
    if (content->byte_count >= 18U) {
        if (emit(
                emit_state, 0U, 3U, UINT64_C(0x50524F4D50540001),
                LAPLACE_DECOMPOSITION_SPAN_TEXT) != 0 ||
            emit(
                emit_state, 9U, 18U, UINT64_C(0x50524F4D50540002),
                LAPLACE_DECOMPOSITION_SPAN_TEXT) != 0) {
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
    bool corrupt_first{};
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
            0x40U + (positions[index] % UINT32_C(47))));
        const double a = static_cast<double>((positions[index] % UINT32_C(19)) + 1U) / 100.0;
        const double b = static_cast<double>((positions[index] % UINT32_C(23)) + 1U) / 100.0;
        const double c = static_cast<double>((positions[index] % UINT32_C(29)) + 1U) / 100.0;
        const double norm = std::sqrt(1.0 + a * a + b * b + c * c);
        value.centroid = laplace_point4d{{1.0 / norm, a / norm, b / norm, c / norm}};
        value.atom = positions[index];
        value.tier_floor = 0U;
        value.has_atom = 1U;
    }
    if (fixture.corrupt_first) {
        known[0].entity_id.bytes[0] ^= UINT8_C(0x80);
    }
    *receipt = Digest(0x72U);
    return 0;
}

laplace_cognition_prompt_atom_provider_v1 AtomProvider(AtomFixture* const fixture) {
    laplace_cognition_prompt_atom_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0x71U);
    provider.resolve = ResolveAtoms;
    provider.abi_major = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MINOR;
    return provider;
}

struct PresenceFixture final {
    bool exact_present{};
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
    const auto disposition = static_cast<std::uint8_t>(
        fixture.exact_present
            ? LAPLACE_COMPOSITION_EXACT_PRESENT
            : LAPLACE_COMPOSITION_NOVEL);
    std::fill_n(entity_dispositions, entity_count, disposition);
    if (physicality_count != 0U) {
        std::fill_n(physicality_dispositions, physicality_count, disposition);
    }
    result->provider_fingerprint = Digest(0x81U);
    result->provider_receipt_id = Digest(0x82U);
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
    laplace_decomposition_provider_v1* const structure_provider,
    const std::uint8_t scope_seed,
    const bool successor) {
    static constexpr char MediaType[] = "text/plain";
    laplace_cognition_prompt_admission_input input{};
    input.decomposition.content.bytes = reinterpret_cast<const std::uint8_t*>(prompt.data());
    input.decomposition.content.byte_count = static_cast<std::uint64_t>(prompt.size());
    input.decomposition.content.media_type = MediaType;
    input.decomposition.content.media_type_byte_count = sizeof(MediaType) - 1U;
    input.decomposition.providers = structure_provider;
    input.decomposition.provider_count = 1U;
    input.decomposition.maximum_spans = 16U;
    input.decomposition.maximum_depth = 4U;
    input.framework_context = context;
    input.source_fingerprint = Digest(static_cast<std::uint8_t>(0x20U + scope_seed));
    input.content_recipe_fingerprint = Digest(0x31U);
    input.calculation_recipe_fingerprint = Digest(0x32U);
    input.geometry_epoch = Digest(0x33U);
    input.occurrence_context_fingerprint = Digest(
        static_cast<std::uint8_t>(0x40U + scope_seed));
    input.source_ordinal_base = 100U;
    input.preferred_batch_bytes = 512U;
    input.occurrence.principal_fingerprint = Digest(
        static_cast<std::uint8_t>(0x50U + scope_seed));
    input.occurrence.session_fingerprint = Digest(
        static_cast<std::uint8_t>(0x60U + scope_seed));
    input.occurrence.discourse_id = Digest(0x91U);
    input.occurrence.world_id = Digest(0x92U);
    input.occurrence.time_fingerprint = Digest(
        static_cast<std::uint8_t>(0x70U + scope_seed));
    input.occurrence.context_fingerprint = Digest(
        static_cast<std::uint8_t>(0x80U + scope_seed));
    input.occurrence.turn_ordinal = successor ? 1U : 0U;
    input.occurrence.turn_flags = successor
        ? static_cast<std::uint32_t>(LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE)
        : UINT32_C(0);
    input.version = LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION;
    return input;
}

struct AdmissionOwner final {
    laplace_cognition_prompt_admission* value{};
    ~AdmissionOwner() { laplace_cognition_prompt_admission_destroy(&value); }
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
    EXPECT_NE(owner.value, nullptr);
    laplace_cognition_prompt_admission_view view{};
    if (owner.value != nullptr) {
        EXPECT_EQ(
            laplace_cognition_prompt_admission_view_get(owner.value, &view),
            LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    }
    return view;
}

TEST(CognitionPromptAdmission, ReusesWholeTrunkAcrossIndependentOccurrences) {
    const std::string prompt = "How does lightning work?";
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);

    const auto first_input = Input(prompt, &context, &structure_provider, 1U, false);
    AdmissionOwner first_owner{};
    const auto first = Admit(
        first_input, atom_provider, presence_provider, first_owner);

    const auto second_input = Input(prompt, &context, &structure_provider, 7U, true);
    AdmissionOwner second_owner{};
    const auto second = Admit(
        second_input, atom_provider, presence_provider, second_owner);

    const auto expected = ExpectedAsciiRoot(prompt);
    EXPECT_TRUE(SameId(first.trunk_entity_id, expected));
    EXPECT_TRUE(SameId(second.trunk_entity_id, expected));
    EXPECT_TRUE(SameId(first.trunk_entity_id, second.trunk_entity_id));
    EXPECT_TRUE(SameDigest(
        first.trunk_identity_witness, second.trunk_identity_witness));
    EXPECT_TRUE(SameDigest(
        first.trunk_physicality_id, second.trunk_physicality_id));
    EXPECT_TRUE(SameDigest(
        first.exact_bytes_fingerprint, second.exact_bytes_fingerprint));
    EXPECT_FALSE(SameDigest(first.occurrence_id, second.occurrence_id));
    EXPECT_FALSE(SameDigest(
        first.admission_receipt_id, second.admission_receipt_id));
    EXPECT_TRUE(SameId(first.turn.observation_entity_id, first.trunk_entity_id));
    EXPECT_TRUE(SameId(second.turn.observation_entity_id, second.trunk_entity_id));
    EXPECT_TRUE(SameDigest(
        first.turn.observation_occurrence_id, first.occurrence_id));
    EXPECT_TRUE(SameDigest(
        second.turn.observation_occurrence_id, second.occurrence_id));
    EXPECT_EQ(first.semantic_attestation_count, 0U);
    EXPECT_EQ(second.semantic_attestation_count, 0U);
    EXPECT_EQ(first.composition_summary.occurrence_count, 0U);
    EXPECT_EQ(second.composition_summary.occurrence_count, 0U);
    EXPECT_GT(first.decomposition_summary.span_count, 1U);
    EXPECT_GT(first.request_count, 0U);

    laplace_framework_producer_v1 producer{};
    ASSERT_EQ(
        laplace_cognition_prompt_admission_producer(first_owner.value, &producer),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    laplace_framework_producer_plan plan{};
    ASSERT_EQ(
        producer.prepare(
            producer.state, &context, &first_input.source_fingerprint,
            &first_input.calculation_recipe_fingerprint, &plan),
        LAPLACE_FRAMEWORK_OK);
    std::vector<laplace_framework_canonical_batch> batches;
    batches.reserve(static_cast<std::size_t>(plan.batch_count));
    for (std::uint64_t index = 0U; index < plan.batch_count; ++index) {
        laplace_framework_canonical_batch batch{};
        laplace_digest256 cursor{};
        ASSERT_EQ(
            producer.next(producer.state, index, &batch, &cursor),
            LAPLACE_FRAMEWORK_OK);
        batches.push_back(batch);
    }
    laplace_digest256 completion{};
    ASSERT_EQ(producer.finish(producer.state, &completion), LAPLACE_FRAMEWORK_OK);
    EXPECT_TRUE(SameDigest(completion, first.composition_summary.receipt_id));

    laplace_persistence_summary persisted{};
    ASSERT_EQ(
        laplace_persistence_validate_stream(
            batches.data(), batches.size(), &persisted),
        LAPLACE_PERSISTENCE_OK);
    EXPECT_GT(persisted.entity_count, 0U);
    EXPECT_GT(persisted.physicality_count, 0U);
    EXPECT_GT(persisted.trajectory_segment_count, 0U);
    EXPECT_EQ(persisted.attestation_count, 0U);
    EXPECT_EQ(persisted.consensus_count, 0U);
}

TEST(CognitionPromptAdmission, AlreadyPresentTrunkRequiresNoPublicationStream) {
    const std::string prompt = "AA";
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    presence.exact_present = true;
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);
    const auto input = Input(prompt, &context, &structure_provider, 4U, false);
    AdmissionOwner owner{};
    const auto view = Admit(input, atom_provider, presence_provider, owner);

    EXPECT_TRUE(SameId(view.trunk_entity_id, ExpectedAsciiRoot(prompt)));
    EXPECT_EQ(view.composition_summary.novel_entity_count, 0U);
    EXPECT_EQ(view.composition_summary.novel_physicality_count, 0U);
    EXPECT_EQ(view.composition_summary.novel_trajectory_vertex_count, 0U);
    EXPECT_EQ(view.composition_summary.batch_count, 0U);
    EXPECT_EQ(view.composition_summary.stream_record_count, 0U);
    EXPECT_EQ(view.composition_summary.stream_byte_count, 0U);

    laplace_framework_producer_v1 producer{};
    EXPECT_EQ(
        laplace_cognition_prompt_admission_producer(owner.value, &producer),
        LAPLACE_COGNITION_PROMPT_ADMISSION_NO_PUBLICATION_REQUIRED);
    EXPECT_EQ(producer.state, nullptr);
    EXPECT_EQ(producer.prepare, nullptr);
    EXPECT_EQ(producer.next, nullptr);
    EXPECT_EQ(producer.finish, nullptr);
    EXPECT_EQ(producer.abort, nullptr);
}

TEST(CognitionPromptAdmission, PunctuationChangesTrunkAndNoConstituentIsPrivileged) {
    const std::string question = "How does lightning work?";
    const std::string exclamation = "How does lightning work!";
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);

    const auto question_input = Input(
        question, &context, &structure_provider, 2U, false);
    AdmissionOwner question_owner{};
    const auto question_view = Admit(
        question_input, atom_provider, presence_provider, question_owner);

    const auto exclamation_input = Input(
        exclamation, &context, &structure_provider, 2U, false);
    AdmissionOwner exclamation_owner{};
    const auto exclamation_view = Admit(
        exclamation_input, atom_provider, presence_provider, exclamation_owner);

    const auto question_expected = ExpectedAsciiRoot(question);
    const auto exclamation_expected = ExpectedAsciiRoot(exclamation);
    const auto constituent = ExpectedAsciiRoot("lightning");
    EXPECT_TRUE(SameId(question_view.trunk_entity_id, question_expected));
    EXPECT_TRUE(SameId(exclamation_view.trunk_entity_id, exclamation_expected));
    EXPECT_FALSE(SameId(
        question_view.trunk_entity_id, exclamation_view.trunk_entity_id));
    EXPECT_FALSE(SameId(question_view.trunk_entity_id, constituent));
    EXPECT_FALSE(SameId(exclamation_view.trunk_entity_id, constituent));
    EXPECT_TRUE(SameId(
        question_view.turn.observation_entity_id,
        question_view.trunk_entity_id));
    EXPECT_TRUE(SameId(
        exclamation_view.turn.observation_entity_id,
        exclamation_view.trunk_entity_id));
}

TEST(CognitionPromptAdmission, RejectsAtomProviderThatRemintsUnicodeIdentity) {
    const std::string prompt = "How does lightning work?";
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    atoms.corrupt_first = true;
    PresenceFixture presence{};
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);
    const auto input = Input(prompt, &context, &structure_provider, 3U, false);

    laplace_cognition_prompt_admission* admission = nullptr;
    EXPECT_EQ(
        laplace_cognition_prompt_admission_create(
            &input, &atom_provider, &presence_provider, &admission),
        LAPLACE_COGNITION_PROMPT_ADMISSION_ATOM_PROVIDER_INVALID);
    EXPECT_EQ(admission, nullptr);
}

}  // namespace
