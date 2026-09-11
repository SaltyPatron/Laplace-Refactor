#ifndef LAPLACE_TEST_PROMPT_ADMISSION_FIXTURE_H
#define LAPLACE_TEST_PROMPT_ADMISSION_FIXTURE_H
#include "laplace/cognition_prompt_admission.h"
#include "laplace/identity.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <gtest/gtest.h>
namespace laplace_test::admission {
inline laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

inline bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

inline bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

inline bool ZeroDigest(const laplace_digest256& value) {
    return std::all_of(
        value.bytes, value.bytes + sizeof(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

inline laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    EXPECT_EQ(laplace_identity_codepoint(codepoint, &entity), LAPLACE_IDENTITY_OK);
    return entity;
}

struct StructureFixture final {
    std::size_t calls{};
};

inline laplace_decomposition_status StructureApplicable(
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

inline laplace_decomposition_status StructureApply(
    void* const opaque,
    const laplace_decomposition_content*,
    const laplace_decomposition_span*,
    laplace_decomposition_emit_fn,
    void*) {
    if (opaque == nullptr) return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    ++static_cast<StructureFixture*>(opaque)->calls;
    return LAPLACE_DECOMPOSITION_OK;
}

inline laplace_decomposition_provider_v1 StructureProvider(StructureFixture* const fixture) {
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

inline int ResolveAtoms(
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

inline laplace_cognition_prompt_atom_provider_v1 AtomProvider(AtomFixture* const fixture) {
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

inline laplace_composition_status ResolvePresence(
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

inline laplace_composition_presence_provider_v1 PresenceProvider(
    PresenceFixture* const fixture) {
    laplace_composition_presence_provider_v1 provider{};
    provider.state = fixture;
    provider.resolve = ResolvePresence;
    provider.abi_major = LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI;
    provider.abi_minor = 0U;
    return provider;
}

inline laplace_cognition_prompt_admission_input PromptInput(
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

}
#endif
