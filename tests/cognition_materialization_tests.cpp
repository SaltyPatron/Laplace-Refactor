#include "laplace/cognition_materialization.h"

#include "laplace/identity.h"
#include "laplace/persistence.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool ZeroDigest(const laplace_digest256& value) {
    return std::all_of(
        value.bytes, value.bytes + sizeof(value.bytes),
        [](const std::uint8_t byte) { return byte == 0U; });
}

laplace_id128 Codepoint(const std::uint32_t atom) {
    laplace_id128 id{};
    laplace_digest256 witness{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(atom, &id, &witness),
        LAPLACE_IDENTITY_OK);
    return id;
}

std::uint64_t Metadata(
    const std::uint8_t tier,
    const bool has_atom,
    const std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
        (has_atom ? (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) : 0U) |
        (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

struct DirectChild final {
    laplace_id128 entity{};
    std::uint64_t multiplicity{1U};
    std::uint32_t atom{};
    std::uint8_t tier{};
    bool has_atom{};
};

struct NodeEntry final {
    laplace_cognition_materialization_node node{};
    std::vector<laplace_trajectory_carrier> carriers;
    laplace_digest256 trajectory_read_receipt{};
};

NodeEntry Composite(
    const std::vector<DirectChild>& children,
    const std::uint8_t tier,
    const std::uint8_t seed) {
    NodeEntry entry{};
    std::vector<laplace_id_run> runs;
    std::uint64_t ordinal = 1U;
    for (const auto& child : children) {
        EXPECT_GT(child.multiplicity, 0U);
        runs.push_back(laplace_id_run{child.entity, child.multiplicity});
        std::uint64_t remaining = child.multiplicity;
        while (remaining != 0U) {
            const auto run = static_cast<std::uint16_t>(std::min<std::uint64_t>(
                remaining, static_cast<std::uint64_t>(UINT16_MAX)));
            laplace_trajectory_carrier carrier{};
            EXPECT_EQ(
                laplace_trajectory_composition_encode(
                    &child.entity, ordinal, run,
                    Metadata(child.tier, child.has_atom, child.atom), &carrier),
                LAPLACE_TRAJECTORY_OK);
            entry.carriers.push_back(carrier);
            ordinal += run;
            remaining -= run;
        }
    }

    std::uint64_t logical_count = 0U;
    EXPECT_EQ(
        laplace_identity_composite_runs_witness(
            runs.data(), runs.size(), nullptr, &logical_count,
            &entry.node.entity_id, &entry.node.identity_witness),
        LAPLACE_IDENTITY_OK);
    EXPECT_GT(logical_count, 1U);
    entry.node.physicality_id = Digest(seed);
    entry.node.node_receipt_id = Digest(static_cast<std::uint8_t>(seed + 1U));
    entry.trajectory_read_receipt = Digest(static_cast<std::uint8_t>(seed + 2U));
    entry.node.logical_count = logical_count;
    entry.node.carrier_count = entry.carriers.size();
    entry.node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    entry.node.tier_floor = tier;
    EXPECT_EQ(
        laplace_persistence_trajectory_fingerprint(
            entry.carriers.data(), entry.carriers.size(),
            &entry.node.trajectory_fingerprint),
        LAPLACE_PERSISTENCE_OK);
    return entry;
}

NodeEntry Atom(const std::uint32_t atom, const std::uint8_t seed) {
    NodeEntry entry{};
    entry.node.entity_id = Codepoint(atom);
    EXPECT_EQ(
        laplace_identity_codepoint_witness(
            atom, &entry.node.entity_id, &entry.node.identity_witness),
        LAPLACE_IDENTITY_OK);
    entry.node.node_receipt_id = Digest(seed);
    entry.node.logical_count = 1U;
    entry.node.atom = atom;
    entry.node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
    entry.node.tier_floor = 0U;
    return entry;
}

std::vector<std::uint8_t> ExpectedBytes(
    const std::vector<std::uint32_t>& atoms) {
    std::vector<std::uint8_t> bytes;
    for (const auto atom : atoms) {
        std::array<std::uint8_t, 4> encoded{};
        std::size_t encoded_bytes = 0U;
        EXPECT_EQ(
            laplace_unicode_position_encode(
                atom, encoded.data(), &encoded_bytes),
            LAPLACE_IDENTITY_OK);
        bytes.insert(bytes.end(), encoded.begin(),
                     encoded.begin() + static_cast<std::ptrdiff_t>(encoded_bytes));
    }
    return bytes;
}

laplace_cognition_realization_result Realization(const laplace_id128& content) {
    laplace_cognition_realization_result realization{};
    realization.content_id = content;
    realization.language_id = Codepoint(0x4AU);
    realization.candidate_receipt_id = Digest(100U);
    realization.realization_recipe_id = Digest(101U);
    realization.preference_rank = 1U;
    realization.reused_subtree_count = 1U;
    realization.structural_tier = 1U;
    realization.match_class =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
    realization.disposition =
        LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    realization.flags =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    realization.version = LAPLACE_COGNITION_REALIZATION_VERSION;
    return realization;
}

laplace_cognition_materialization_request Request() {
    laplace_cognition_materialization_request request{};
    request.maximum_nodes = 32U;
    request.maximum_trajectory_carriers = 256U;
    request.maximum_output_bytes = 4096U;
    request.maximum_depth = 16U;
    request.version = LAPLACE_COGNITION_MATERIALIZATION_VERSION;
    return request;
}

struct ProviderState final {
    std::vector<NodeEntry> entries;
    std::uint64_t resolve_calls{};
    std::uint64_t read_calls{};
    std::uint64_t begin_calls{};
    laplace_digest256 scope_receipt{};
    laplace_id128 begun_root{};
    laplace_digest256 begun_source{};
    laplace_digest256 begun_recipe{};
    bool begin_failure{};
    std::size_t selected_root{};
    unsigned selection_failure{};
    std::uint8_t binding_seed{140U};
    std::uint64_t selected_resolve_calls{};
};

int BeginRead(void* opaque, const laplace_id128* root,
    const laplace_digest256* source, const laplace_digest256* recipe,
    laplace_digest256* scope) {
    auto& state = *static_cast<ProviderState*>(opaque);
    ++state.begin_calls;
    state.begun_root = *root;
    state.begun_source = *source;
    state.begun_recipe = *recipe;
    *scope = state.scope_receipt;
    return state.begin_failure ? 1 : 0;
}

const NodeEntry* Find(
    const ProviderState& state,
    const laplace_id128& entity) {
    const auto found = std::find_if(
        state.entries.begin(), state.entries.end(),
        [&](const NodeEntry& entry) {
            return SameId(entry.node.entity_id, entity);
        });
    return found == state.entries.end() ? nullptr : &*found;
}

int ResolveNode(
    void* const opaque,
    const laplace_id128* const entity,
    laplace_cognition_materialization_node* const node) {
    if (opaque == nullptr || entity == nullptr || node == nullptr) return 1;
    auto& state = *static_cast<ProviderState*>(opaque);
    ++state.resolve_calls;
    const auto* entry = Find(state, *entity);
    if (entry == nullptr) return 2;
    *node = entry->node;
    return 0;
}

int ReadTrajectory(
    void* const opaque,
    const laplace_cognition_materialization_node* const node,
    laplace_trajectory_carrier* const carriers,
    const std::size_t carrier_count,
    laplace_digest256* const read_receipt) {
    if (opaque == nullptr || node == nullptr || carriers == nullptr ||
        read_receipt == nullptr) {
        return 1;
    }
    auto& state = *static_cast<ProviderState*>(opaque);
    ++state.read_calls;
    const auto found = std::find_if(state.entries.begin(), state.entries.end(),
        [&](const NodeEntry& candidate) {
            return SameId(candidate.node.entity_id, node->entity_id) &&
                std::memcmp(&candidate.node.physicality_id, &node->physicality_id,
                    sizeof(node->physicality_id)) == 0;
        });
    const auto* entry = found == state.entries.end() ? nullptr : &*found;
    if (entry == nullptr || entry->carriers.size() != carrier_count) return 2;
    std::copy(entry->carriers.begin(), entry->carriers.end(), carriers);
    *read_receipt = entry->trajectory_read_receipt;
    return 0;
}

laplace_cognition_materialization_provider_v1 Provider(ProviderState* state) {
    laplace_cognition_materialization_provider_v1 provider{};
    provider.state = state;
    provider.provider_fingerprint = Digest(110U);
    provider.resolve_node = ResolveNode;
    provider.read_trajectory = ReadTrajectory;
    provider.abi_major = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

TEST(CognitionMaterialization, ExactCjkTrajectoryRoundTripsWithoutEnglishIntermediate) {
    const std::vector<std::uint32_t> atoms{
        0x4E8BU, 0x4EF6U, 0x306EU, 0x3053U, 0x3068U,
        0x306AU, 0x3093U, 0x3060U, 0x3051U, 0x3069U,
        0x002EU, 0x002EU, 0x002EU};
    std::vector<DirectChild> children;
    for (const auto atom : atoms) {
        children.push_back(DirectChild{Codepoint(atom), 1U, atom, 0U, true});
    }
    auto root = Composite(children, 1U, 20U);
    ProviderState state{{root}};
    auto provider = Provider(&state);
    auto realization = Realization(root.node.entity_id);
    auto request = Request();
    std::array<std::uint8_t, 512> output{};
    std::size_t output_bytes = 0U;
    laplace_cognition_materialization_receipt receipt{};

    ASSERT_EQ(
        laplace_cognition_realization_materialize_utf8(
            &realization, &request, &provider, output.data(), output.size(),
            &output_bytes, &receipt),
        LAPLACE_COGNITION_MATERIALIZATION_OK);

    const auto expected = ExpectedBytes(atoms);
    ASSERT_EQ(output_bytes, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), output.begin()));
    EXPECT_EQ(receipt.codepoint_count, atoms.size());
    EXPECT_EQ(receipt.resolved_node_count, 1U);
    EXPECT_EQ(receipt.trajectory_carrier_count, atoms.size());
    EXPECT_EQ(receipt.maximum_depth_observed, 1U);
    EXPECT_FALSE(ZeroDigest(receipt.materialization_id));
    EXPECT_FALSE(ZeroDigest(receipt.output_fingerprint));
    EXPECT_EQ(state.resolve_calls, 1U);
    EXPECT_EQ(state.read_calls, 1U);
}

TEST(CognitionMaterialization, NestedCompositionDescendsToAtomsAndPreservesRuns) {
    auto child = Composite(
        {{Codepoint(0x41U), 1U, 0x41U, 0U, true},
         {Codepoint(0x42U), 1U, 0x42U, 0U, true}},
        1U, 30U);
    auto root = Composite(
        {{child.node.entity_id, 2U, 0U, 1U, false},
         {Codepoint(0x21U), 1U, 0x21U, 0U, true}},
        2U, 40U);
    ProviderState state{{root, child}};
    auto provider = Provider(&state);
    auto realization = Realization(root.node.entity_id);
    realization.structural_tier = 2U;
    auto request = Request();
    std::array<std::uint8_t, 64> output{};
    std::size_t output_bytes = 0U;
    laplace_cognition_materialization_receipt receipt{};

    ASSERT_EQ(
        laplace_cognition_realization_materialize_utf8(
            &realization, &request, &provider, output.data(), output.size(),
            &output_bytes, &receipt),
        LAPLACE_COGNITION_MATERIALIZATION_OK);

    const auto expected = ExpectedBytes({0x41U, 0x42U, 0x41U, 0x42U, 0x21U});
    ASSERT_EQ(output_bytes, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), output.begin()));
    EXPECT_EQ(receipt.resolved_node_count, 2U);
    EXPECT_EQ(receipt.trajectory_carrier_count, 4U);
    EXPECT_EQ(receipt.codepoint_count, 5U);
    EXPECT_EQ(receipt.maximum_depth_observed, 2U);
    EXPECT_EQ(state.resolve_calls, 2U);
    EXPECT_EQ(state.read_calls, 2U);
}

TEST(CognitionMaterialization, SharedSubtreeAcrossParentsIsReadOnceWithExactOccurrenceOrder) {
    auto child = Composite(
        {{Codepoint(0x41U), 1U, 0x41U, 0U, true},
         {Codepoint(0x42U), 1U, 0x42U, 0U, true}}, 1U, 30U);
    auto parent = Composite(
        {{Codepoint(0x3FU), 1U, 0x3FU, 0U, true},
         {child.node.entity_id, 2U, 0U, 1U, false}}, 2U, 40U);
    auto root = Composite(
        {{child.node.entity_id, 1U, 0U, 1U, false},
         {parent.node.entity_id, 1U, 0U, 2U, false},
         {child.node.entity_id, 1U, 0U, 1U, false}}, 3U, 50U);
    ProviderState state{{root, parent, child}};
    auto provider = Provider(&state);
    auto realization = Realization(root.node.entity_id);
    auto request = Request();
    request.maximum_nodes = 3U;
    request.maximum_trajectory_carriers = 7U;
    std::array<std::uint8_t, 64> output{};
    std::size_t output_bytes = 0U;
    laplace_cognition_materialization_receipt receipt{};
    ASSERT_EQ(laplace_cognition_realization_materialize_utf8(
        &realization, &request, &provider, output.data(), output.size(),
        &output_bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
    const auto expected = ExpectedBytes(
        {0x41U, 0x42U, 0x3FU, 0x41U, 0x42U, 0x41U, 0x42U, 0x41U, 0x42U});
    ASSERT_EQ(output_bytes, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), output.begin()));
    EXPECT_EQ(state.resolve_calls, 3U);
    EXPECT_EQ(state.read_calls, 3U);
    EXPECT_EQ(receipt.resolved_node_count, 3U);
    EXPECT_EQ(receipt.trajectory_carrier_count, 7U);
    EXPECT_EQ(receipt.maximum_depth_observed, 3U);
    EXPECT_EQ(receipt.codepoint_count, 9U);

    // Reuse at a deeper occurrence must still enforce that occurrence's depth.
    request.maximum_depth = 2U;
    output.fill(0xA5U);
    EXPECT_EQ(laplace_cognition_realization_materialize_utf8(
        &realization, &request, &provider, output.data(), output.size(),
        &output_bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_LIMIT);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_TRUE(std::all_of(output.begin(), output.end(),
        [](std::uint8_t byte) { return byte == 0xA5U; }));

    request.maximum_depth = 3U;
    request.maximum_output_bytes = expected.size() - 1U;
    EXPECT_EQ(laplace_cognition_realization_materialize_utf8(
        &realization, &request, &provider, output.data(), output.size(),
        &output_bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_LIMIT);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_TRUE(std::all_of(output.begin(), output.end(),
        [](std::uint8_t byte) { return byte == 0xA5U; }));

    // A later call must revalidate the provider's content, never reuse a prior
    // call's successful slice as authority for a changed full witness.
    request.maximum_output_bytes = 64U;
    state.entries[2].node.identity_witness.bytes[31] ^= 1U;
    EXPECT_EQ(laplace_cognition_realization_materialize_utf8(
        &realization, &request, &provider, output.data(), output.size(),
        &output_bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_TRUE(std::all_of(output.begin(), output.end(),
        [](std::uint8_t byte) { return byte == 0xA5U; }));
}

TEST(CognitionMaterialization, SharedContentDoesNotEraseOccurrenceTierValidation) {
    auto child = Composite(
        {{Codepoint(0x41U), 1U, 0x41U, 0U, true},
         {Codepoint(0x42U), 1U, 0x42U, 0U, true}}, 1U, 30U);
    auto root = Composite(
        {{child.node.entity_id, 1U, 0U, 1U, false},
         {Codepoint(0x3FU), 1U, 0x3FU, 0U, true},
         {child.node.entity_id, 1U, 0U, 2U, false}}, 3U, 50U);
    ProviderState state{{root, child}};
    auto provider = Provider(&state);
    auto realization = Realization(root.node.entity_id);
    auto request = Request();
    std::array<std::uint8_t, 64> output{};
    output.fill(0xA5U);
    std::size_t output_bytes = 0U;
    laplace_cognition_materialization_receipt receipt{};
    EXPECT_EQ(laplace_cognition_realization_materialize_utf8(
        &realization, &request, &provider, output.data(), output.size(),
        &output_bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_TRUE(std::all_of(output.begin(), output.end(),
        [](std::uint8_t byte) { return byte == 0xA5U; }));
}

TEST(CognitionMaterialization, UnrepresentableProviderTrajectoryReturnsLimitWithoutPublication) {
    auto root = Composite(
        {{Codepoint(0x41U), 1U, 0x41U, 0U, true},
         {Codepoint(0x42U), 1U, 0x42U, 0U, true}}, 1U, 30U);
    root.node.carrier_count = static_cast<std::uint64_t>(
        std::vector<laplace_trajectory_carrier>{}.max_size()) + 1U;
    ProviderState state{{root}};
    auto provider = Provider(&state);
    auto realization = Realization(root.node.entity_id);
    auto request = Request();
    request.maximum_trajectory_carriers = root.node.carrier_count;
    std::array<std::uint8_t, 64> output{};
    output.fill(0xA5U);
    std::size_t output_bytes = 99U;
    laplace_cognition_materialization_receipt receipt{};
    receipt.materialization_id = Digest(90U);
    EXPECT_EQ(laplace_cognition_realization_materialize_utf8(
        &realization, &request, &provider, output.data(), output.size(),
        &output_bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_LIMIT);
    EXPECT_EQ(state.resolve_calls, 1U);
    EXPECT_EQ(state.read_calls, 0U);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_TRUE(ZeroDigest(receipt.materialization_id));
    EXPECT_TRUE(std::all_of(output.begin(), output.end(),
        [](std::uint8_t byte) { return byte == 0xA5U; }));
}

TEST(CognitionMaterialization, ValidTrajectoryWithWrongOrderFailsContentIdentity) {
    auto root = Composite(
        {{Codepoint(0x41U), 1U, 0x41U, 0U, true},
         {Codepoint(0x42U), 1U, 0x42U, 0U, true},
         {Codepoint(0x43U), 1U, 0x43U, 0U, true}},
        1U, 50U);

    const auto b = Codepoint(0x42U);
    const auto a = Codepoint(0x41U);
    ASSERT_EQ(
        laplace_trajectory_composition_encode(
            &b, 1U, 1U, Metadata(0U, true, 0x42U), &root.carriers[0]),
        LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(
        laplace_trajectory_composition_encode(
            &a, 2U, 1U, Metadata(0U, true, 0x41U), &root.carriers[1]),
        LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(
        laplace_persistence_trajectory_fingerprint(
            root.carriers.data(), root.carriers.size(),
            &root.node.trajectory_fingerprint),
        LAPLACE_PERSISTENCE_OK);

    ProviderState state{{root}};
    auto provider = Provider(&state);
    auto realization = Realization(root.node.entity_id);
    auto request = Request();
    std::array<std::uint8_t, 64> output{};
    output.fill(UINT8_C(0xA5));
    std::size_t output_bytes = 99U;
    laplace_cognition_materialization_receipt receipt{};

    EXPECT_EQ(
        laplace_cognition_realization_materialize_utf8(
            &realization, &request, &provider, output.data(), output.size(),
            &output_bytes, &receipt),
        LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_EQ(output.front(), UINT8_C(0xA5));
    EXPECT_TRUE(ZeroDigest(receipt.materialization_id));
}

TEST(CognitionMaterialization, CapacityFailurePublishesNoPartialOutput) {
    auto root = Composite(
        {{Codepoint(0x4E8BU), 1U, 0x4E8BU, 0U, true},
         {Codepoint(0x4EF6U), 1U, 0x4EF6U, 0U, true}},
        1U, 60U);
    ProviderState state{{root}};
    auto provider = Provider(&state);
    auto realization = Realization(root.node.entity_id);
    auto request = Request();
    std::array<std::uint8_t, 2> output{{UINT8_C(0x7A), UINT8_C(0x7B)}};
    std::size_t output_bytes = 9U;
    laplace_cognition_materialization_receipt receipt{};

    EXPECT_EQ(
        laplace_cognition_realization_materialize_utf8(
            &realization, &request, &provider, output.data(), output.size(),
            &output_bytes, &receipt),
        LAPLACE_COGNITION_MATERIALIZATION_CAPACITY);
    EXPECT_EQ(output_bytes, 0U);
    EXPECT_EQ(output[0], UINT8_C(0x7A));
    EXPECT_EQ(output[1], UINT8_C(0x7B));
    EXPECT_TRUE(ZeroDigest(receipt.materialization_id));
}

TEST(CognitionMaterialization, IncompleteRealizationCannotReachPersistenceProvider) {
    const auto atom = Atom(0x41U, 70U);
    ProviderState state{{atom}};
    auto provider = Provider(&state);
    auto realization = Realization(atom.node.entity_id);
    realization.content_id = laplace_id128{};
    realization.disposition =
        LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED;
    realization.missing_obligation_count = 1U;
    realization.missing_obligation_fingerprint = Digest(120U);
    auto request = Request();
    std::array<std::uint8_t, 8> output{};
    std::size_t output_bytes = 0U;
    laplace_cognition_materialization_receipt receipt{};

    EXPECT_EQ(
        laplace_cognition_realization_materialize_utf8(
            &realization, &request, &provider, output.data(), output.size(),
            &output_bytes, &receipt),
        LAPLACE_COGNITION_MATERIALIZATION_INVALID_REALIZATION);
    EXPECT_EQ(state.resolve_calls, 0U);
    EXPECT_EQ(state.read_calls, 0U);
    EXPECT_EQ(output_bytes, 0U);
}

TEST(ContentMaterialization, SourceBoundReadbackSharesExactUnicodeKernelWithoutRealization) {
    auto atom = Atom(0x03bbU, 10U);
    ProviderState state{{atom}};
    auto provider = Provider(&state);
    auto request = Request();
    auto source = Digest(51U), recipe = Digest(52U);
    std::array<std::uint8_t, 32> output{};
    std::size_t bytes = 0;
    laplace_content_materialization_receipt receipt{}, replay{}, changed{};
    ASSERT_EQ(laplace_content_materialize_encoded(&atom.node.entity_id, &source, &recipe,
        &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8, output.data(), output.size(),
        &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(output.begin(),output.begin()+static_cast<std::ptrdiff_t>(bytes)), ExpectedBytes({0x03bbU}));
    EXPECT_EQ(std::memcmp(receipt.source_receipt_id.bytes, source.bytes,32u),0);
    ASSERT_EQ(laplace_content_materialize_encoded(&atom.node.entity_id, &source, &recipe,
        &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8, output.data(), output.size(),
        &bytes, &replay), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(std::memcmp(&receipt,&replay,sizeof(receipt)),0);
    source.bytes[0] ^= 1u;
    ASSERT_EQ(laplace_content_materialize_encoded(&atom.node.entity_id, &source, &recipe,
        &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8, output.data(), output.size(),
        &bytes, &changed), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_NE(std::memcmp(receipt.materialization_id.bytes,changed.materialization_id.bytes,32u),0);
    auto realization=Realization(atom.node.entity_id);
    realization.candidate_receipt_id=receipt.source_receipt_id;
    realization.realization_recipe_id=recipe;
    laplace_cognition_materialization_receipt cognition{};
    ASSERT_EQ(laplace_cognition_realization_materialize_utf8(&realization,&request,&provider,
        output.data(),output.size(),&bytes,&cognition),LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(std::memcmp(receipt.output_fingerprint.bytes,cognition.output_fingerprint.bytes,32u),0);
    EXPECT_NE(std::memcmp(receipt.materialization_id.bytes,cognition.materialization_id.bytes,32u),0);
}

TEST(ContentMaterialization, ExplicitOctetsPreserveBytesAndRejectUnicodeOutsideByteRange) {
    auto atom = Atom(0xe9U, 10U);
    ProviderState state{{atom}};
    auto provider=Provider(&state); auto request=Request();
    auto source=Digest(51U),recipe=Digest(52U);
    std::array<std::uint8_t,32> output{};
    std::size_t bytes=0;
    laplace_content_materialization_receipt octets{},utf8{};
    ASSERT_EQ(laplace_content_materialize_encoded(&atom.node.entity_id,&source,&recipe,
        &request,&provider,LAPLACE_COGNITION_OUTPUT_OCTETS,output.data(),output.size(),
        &bytes,&octets),LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(bytes,1u); EXPECT_EQ(output[0],0xe9u);
    ASSERT_EQ(laplace_content_materialize_encoded(&atom.node.entity_id,&source,&recipe,
        &request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,output.data(),output.size(),
        &bytes,&utf8),LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(bytes,2u);
    EXPECT_NE(std::memcmp(octets.output_fingerprint.bytes,utf8.output_fingerprint.bytes,32u),0);
    state.entries[0]=Atom(0x03bbU,11U); output.fill(0xa5);
    EXPECT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,&source,&recipe,
        &request,&provider,LAPLACE_COGNITION_OUTPUT_OCTETS,output.data(),output.size(),
        &bytes,&octets),LAPLACE_COGNITION_MATERIALIZATION_ENCODING_RANGE);
    EXPECT_EQ(bytes,0u); EXPECT_TRUE(ZeroDigest(octets.materialization_id));
    EXPECT_TRUE(std::all_of(output.begin(),output.end(),[](auto b){return b==0xa5;}));
}

TEST(ContentMaterialization, InvalidIdentityMissingBindingAndFiniteLimitPublishNoOutput) {
    auto atom=Atom(0x03bbU,10U);
    ProviderState state{{atom}};
    auto provider=Provider(&state); auto request=Request();
    auto source=Digest(51U),recipe=Digest(52U);
    std::array<std::uint8_t,32> output{};
    std::size_t bytes=0;
    laplace_content_materialization_receipt receipt{};
    for(int failure=0;failure<3;++failure) {
        state.entries[0]=atom;request=Request();source=Digest(51U);output.fill(0xa5);
        if(failure==0) source={};
        if(failure==1) state.entries[0].node.identity_witness.bytes[20]^=1u;
        if(failure==2) request.maximum_output_bytes=1u;
        EXPECT_NE(laplace_content_materialize_encoded(&atom.node.entity_id,&source,&recipe,
            &request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,output.data(),output.size(),
            &bytes,&receipt),LAPLACE_COGNITION_MATERIALIZATION_OK);
        EXPECT_EQ(bytes,0u);EXPECT_TRUE(ZeroDigest(receipt.materialization_id));
        EXPECT_TRUE(std::all_of(output.begin(),output.end(),[](auto b){return b==0xa5;}));
    }
}

TEST(ContentMaterialization, SourceRootUsesSharedTrajectoryValidationAndPublishesAtomically) {
    const auto a=Codepoint(0x03bbU),b=Codepoint(0x61U);
    auto root=Composite({DirectChild{a,1u,0x03bbU,0u,true},
        DirectChild{b,1u,0x61U,0u,true}},1u,20u);
    ProviderState state{{root}};
    auto provider=Provider(&state);auto request=Request();
    auto source=Digest(51U),recipe=Digest(52U);
    std::array<std::uint8_t,32> output{};
    std::size_t bytes=0;
    laplace_content_materialization_receipt receipt{};
    ASSERT_EQ(laplace_content_materialize_encoded(&root.node.entity_id,&source,&recipe,
        &request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,output.data(),output.size(),
        &bytes,&receipt),LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(output.begin(),
        output.begin()+static_cast<std::ptrdiff_t>(bytes)),ExpectedBytes({0x03bbU,0x61U}));
    ASSERT_EQ(laplace_trajectory_composition_encode(&b,1u,1u,Metadata(0u,true,0x61U),
        &state.entries[0].carriers[0]),LAPLACE_TRAJECTORY_OK);
    output.fill(0xa5);
    EXPECT_NE(laplace_content_materialize_encoded(&root.node.entity_id,&source,&recipe,
        &request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,output.data(),output.size(),
        &bytes,&receipt),LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(bytes,0u);EXPECT_TRUE(ZeroDigest(receipt.materialization_id));
    EXPECT_TRUE(std::all_of(output.begin(),output.end(),[](auto value){return value==0xa5;}));
}

TEST(ContentMaterialization, EveryRootBeginsItsOwnAuthenticatedScope) {
    ProviderState state{{Atom(0x41U,10U), Atom(0x42U,11U)}};
    auto provider = Provider(&state);
    provider.begin_read = BeginRead;
    auto request = Request();
    const auto source = Digest(51U), recipe = Digest(52U);
    std::array<std::uint8_t,32> output{};
    std::array<laplace_content_materialization_receipt,3> receipts{};
    for (std::size_t call=0; call<3; ++call) {
        const auto index = call % 2U;
        const auto root = state.entries[index].node.entity_id;
        state.scope_receipt = Digest(static_cast<std::uint8_t>(60U+index));
        std::size_t bytes=0;
        ASSERT_EQ(laplace_content_materialize_encoded(&root,&source,&recipe,
            &request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,output.data(),output.size(),
            &bytes,&receipts[call]),LAPLACE_COGNITION_MATERIALIZATION_OK);
        EXPECT_EQ(bytes,1U);
        EXPECT_EQ(output[0],static_cast<std::uint8_t>(0x41U+index));
        EXPECT_EQ(state.begin_calls,call+1U);
        EXPECT_EQ(state.resolve_calls,call+1U);
        EXPECT_TRUE(SameId(state.begun_root,root));
        EXPECT_EQ(std::memcmp(&state.begun_source,&source,sizeof(source)),0);
        EXPECT_EQ(std::memcmp(&state.begun_recipe,&recipe,sizeof(recipe)),0);
    }
    EXPECT_EQ(std::memcmp(&receipts[0],&receipts[2],sizeof(receipts[0])),0);
    // Only the authenticated scope changes: content, source, provider and all
    // node bytes stay identical. Omitting scope from the readset must fail.
    state.scope_receipt = Digest(99U);
    std::size_t bytes=0;
    laplace_content_materialization_receipt changed{};
    ASSERT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
        &source,&recipe,&request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(),output.size(),&bytes,&changed),LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_NE(std::memcmp(&receipts[0].readset_fingerprint,&changed.readset_fingerprint,
        sizeof(changed.readset_fingerprint)),0);
    EXPECT_NE(std::memcmp(&receipts[0].materialization_id,&changed.materialization_id,
        sizeof(changed.materialization_id)),0);
}

TEST(ContentMaterialization, BeginFailureAndInvalidMinorCannotReadOrPublish) {
    ProviderState state{{Atom(0x41U,10U)}};
    auto provider=Provider(&state);provider.begin_read=BeginRead;
    auto request=Request();const auto source=Digest(51U),recipe=Digest(52U);
    std::array<std::uint8_t,32> output{};output.fill(0xa5U);
    state.begin_failure=true;
    for (unsigned attempt=0;attempt<2;++attempt) {
        if (attempt==1U) ++provider.abi_minor;
        std::size_t bytes=17U;
        laplace_content_materialization_receipt receipt{};
        EXPECT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
            &source,&recipe,&request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,
            output.data(),output.size(),&bytes,&receipt), attempt==0U ?
            LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_FAILURE :
            LAPLACE_COGNITION_MATERIALIZATION_INVALID_PROVIDER);
        EXPECT_EQ(bytes,0U);EXPECT_TRUE(ZeroDigest(receipt.materialization_id));
        EXPECT_EQ(state.begin_calls,1U);EXPECT_EQ(state.resolve_calls,0U);
        EXPECT_EQ(state.read_calls,0U);
        EXPECT_TRUE(std::all_of(output.begin(),output.end(),[](auto v){return v==0xa5U;}));
    }
}

TEST(ContentMaterialization, MinorZeroIgnoresTailAndPreservesExistingReceipt) {
    ProviderState state{{Atom(0x41U,10U)}};
    auto provider=Provider(&state);provider.abi_minor=0U;
    provider.begin_read=BeginRead;state.begin_failure=true;
    auto request=Request();const auto source=Digest(51U),recipe=Digest(52U);
    std::array<std::uint8_t,32> output{};std::size_t bytes=0;
    laplace_content_materialization_receipt legacy{},current{};
    ASSERT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
        &source,&recipe,&request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(),output.size(),&bytes,&legacy),LAPLACE_COGNITION_MATERIALIZATION_OK);
    provider.abi_minor=LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;
    provider.begin_read=nullptr;
    ASSERT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
        &source,&recipe,&request,&provider,LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(),output.size(),&bytes,&current),LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(state.begin_calls,0U);
    EXPECT_EQ(std::memcmp(&legacy,&current,sizeof(legacy)),0);
}

int SelectOccurrence(void* opaque,
    const laplace_cognition_materialization_reference* reference,
    laplace_cognition_materialization_selection* selection) {
    auto& state = *static_cast<ProviderState*>(opaque);
    *selection = {};
    selection->contiguous_run_length = reference->occurrence.run_length;
    if (ZeroDigest(reference->parent_physicality_id)) {
        EXPECT_EQ(reference->occurrence.logical_ordinal, 0U);
        EXPECT_EQ(reference->occurrence.run_length, 1U);
        selection->physicality_id = state.entries[state.selected_root].node.physicality_id;
        selection->binding_receipt_id = Digest(state.binding_seed);
    } else if (std::memcmp(&reference->parent_physicality_id,
        &state.entries[state.selected_root].node.physicality_id,
        sizeof(reference->parent_physicality_id)) == 0) {
        const auto ordinal = reference->occurrence.logical_ordinal;
        if (ordinal < 1U || ordinal > 2U) return 1;
        EXPECT_EQ(reference->occurrence.metadata, Metadata(1U, false, 0U));
        EXPECT_EQ(reference->occurrence.run_length, 3U - ordinal);
        const auto index = 2U + ((static_cast<std::size_t>(ordinal) - 1U +
            state.selected_root) % 2U);
        selection->physicality_id = state.entries[index].node.physicality_id;
        selection->binding_receipt_id = Digest(static_cast<std::uint8_t>(
            state.binding_seed + ordinal));
        selection->contiguous_run_length = 1U;
    }
    switch (state.selection_failure) {
        case 1U: selection->contiguous_run_length = 0U; break;
        case 2U: selection->contiguous_run_length = reference->occurrence.run_length + 1U; break;
        case 3U: selection->binding_receipt_id = {}; break;
        case 4U: return 1;
        default: break;
    }
    return 0;
}

int ResolveSelected(void* opaque, const laplace_id128* entity,
    const laplace_digest256* physicality,
    laplace_cognition_materialization_node* node) {
    auto& state = *static_cast<ProviderState*>(opaque);
    ++state.selected_resolve_calls;
    const auto found = std::find_if(state.entries.begin(), state.entries.end(),
        [&](const NodeEntry& candidate) {
            return SameId(candidate.node.entity_id, *entity) &&
                std::memcmp(&candidate.node.physicality_id, physicality,
                    sizeof(*physicality)) == 0;
        });
    if (found == state.entries.end()) return 1;
    *node = found->node;
    if (state.selection_failure == 5U) node->physicality_id = Digest(199U);
    return 0;
}

ProviderState OccurrenceFixture() {
    const std::vector<DirectChild> atoms{
        {Codepoint(0x41U), 1U, 0x41U, 0U, true},
        {Codepoint(0x42U), 1U, 0x42U, 0U, true}};
    auto first = Composite(atoms, 1U, 20U);
    auto second = Composite(atoms, 1U, 30U);
    const std::vector<DirectChild> repeated{
        {first.node.entity_id, 2U, 0U, 1U, false}};
    auto root_a = Composite(repeated, 2U, 40U);
    auto root_b = Composite(repeated, 2U, 50U);
    EXPECT_EQ(root_a.carriers.size(), 1U);
    EXPECT_TRUE(SameId(first.node.entity_id, second.node.entity_id));
    return ProviderState{{root_a, root_b, first, second}};
}

TEST(ContentMaterialization, OccurrenceSelectionsSplitOneStoredRunWithoutCollapsingPhysicalities) {
    auto state = OccurrenceFixture();
    auto provider = Provider(&state);
    provider.begin_read = BeginRead;
    provider.select_reference = SelectOccurrence;
    provider.resolve_selected = ResolveSelected;
    auto request = Request();
    request.maximum_nodes = 3U;
    request.maximum_trajectory_carriers = 5U;
    const auto recipe = Digest(120U);
    std::array<std::uint8_t, 32> output{};
    std::array<laplace_content_materialization_receipt, 3> receipts{};
    for (std::size_t call = 0U; call < receipts.size(); ++call) {
        state.selected_root = call % 2U;
        const auto source = Digest(static_cast<std::uint8_t>(121U + state.selected_root));
        const auto& root = state.entries[state.selected_root].node.entity_id;
        std::size_t bytes = 0U;
        ASSERT_EQ(laplace_content_materialize_encoded(&root, &source, &recipe,
            &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8, output.data(),
            output.size(), &bytes, &receipts[call]), LAPLACE_COGNITION_MATERIALIZATION_OK);
        EXPECT_EQ(bytes, 4U);
        EXPECT_EQ(std::memcmp(output.data(), "ABAB", 4U), 0);
        EXPECT_EQ(receipts[call].resolved_node_count, 3U);
        EXPECT_EQ(receipts[call].trajectory_carrier_count, 5U);
        EXPECT_EQ(receipts[call].codepoint_count, 4U);
        EXPECT_EQ(state.selected_resolve_calls, 3U * (call + 1U));
        EXPECT_EQ(state.read_calls, 3U * (call + 1U));
        EXPECT_EQ(state.resolve_calls, 0U);
    }
    EXPECT_EQ(std::memcmp(&receipts[0], &receipts[2], sizeof(receipts[0])), 0);
    EXPECT_NE(std::memcmp(&receipts[0].readset_fingerprint,
        &receipts[1].readset_fingerprint, sizeof(laplace_digest256)), 0);
    // Only the retained binding receipt changes; content, physicalities, source
    // and provider remain exact. The occurrence provenance must still bind it.
    ++state.binding_seed;
    const auto source = Digest(121U);
    std::size_t bytes = 0U;
    laplace_content_materialization_receipt changed{};
    ASSERT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &changed), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_NE(std::memcmp(&receipts[0].readset_fingerprint,
        &changed.readset_fingerprint, sizeof(laplace_digest256)), 0);
}

TEST(ContentMaterialization, InvalidOccurrenceSelectionsPublishNoPartialOutput) {
    for (unsigned failure = 1U; failure <= 5U; ++failure) {
        auto state = OccurrenceFixture();
        state.selection_failure = failure;
        auto provider = Provider(&state);
        provider.select_reference = SelectOccurrence;
        provider.resolve_selected = ResolveSelected;
        const auto request = Request();
        const auto source = Digest(121U), recipe = Digest(120U);
        std::array<std::uint8_t, 32> output{};
        output.fill(0xa5U);
        std::size_t bytes = 17U;
        laplace_content_materialization_receipt receipt{};
        EXPECT_NE(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
            &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
            output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
        EXPECT_EQ(bytes, 0U);
        EXPECT_TRUE(ZeroDigest(receipt.materialization_id));
        EXPECT_EQ(state.read_calls, 0U);
        EXPECT_EQ(state.selected_resolve_calls, failure == 5U ? 1U : 0U);
        EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](auto byte) { return byte == 0xa5U; }));
    }
}

TEST(ContentMaterialization, MinorOneIgnoresOccurrenceTailAndRejectsPartialCurrentCallbacks) {
    ProviderState state{{Atom(0x41U, 10U)}};
    auto provider = Provider(&state);
    provider.abi_minor = 1U;
    provider.select_reference = SelectOccurrence;
    provider.resolve_selected = nullptr;
    const auto request = Request();
    const auto source = Digest(121U), recipe = Digest(120U);
    std::array<std::uint8_t, 32> output{};
    std::size_t bytes = 0U;
    laplace_content_materialization_receipt legacy{}, current{};
    ASSERT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &legacy), LAPLACE_COGNITION_MATERIALIZATION_OK);
    provider.abi_minor = 2U;
    EXPECT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &current), LAPLACE_COGNITION_MATERIALIZATION_INVALID_PROVIDER);
    provider.select_reference = nullptr;
    ASSERT_EQ(laplace_content_materialize_encoded(&state.entries[0].node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &current), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(std::memcmp(&legacy, &current, sizeof(legacy)), 0);
}

int SelectSingleChild(void* opaque,
    const laplace_cognition_materialization_reference* reference,
    laplace_cognition_materialization_selection* selection) {
    auto& state = *static_cast<ProviderState*>(opaque);
    *selection = {};
    selection->contiguous_run_length = reference->occurrence.run_length;
    if (ZeroDigest(reference->parent_physicality_id)) {
        selection->physicality_id = state.entries[0].node.physicality_id;
        selection->binding_receipt_id = Digest(150U);
    } else if (std::memcmp(&reference->parent_physicality_id,
            &state.entries[0].node.physicality_id, sizeof(laplace_digest256)) == 0 &&
        SameId(reference->occurrence.entity_id, state.entries[1].node.entity_id)) {
        selection->physicality_id = state.entries[state.selected_root == 0U ? 1U : 0U].node.physicality_id;
        selection->binding_receipt_id = Digest(151U);
    }
    return 0;
}

TEST(ContentMaterialization, BoundAtomicPhysicalityIsValidatedWithoutErasingItsSelection) {
    auto atom = Atom(0x41U, 20U);
    atom.node.physicality_id = Digest(21U);
    auto root = Composite({{atom.node.entity_id, 1U, 0x41U, 0U, true},
        {Codepoint(0x42U), 1U, 0x42U, 0U, true}}, 1U, 30U);
    ProviderState state{{root, atom}};
    auto provider = Provider(&state);
    provider.select_reference = SelectSingleChild;
    provider.resolve_selected = ResolveSelected;
    const auto request = Request();
    const auto source = Digest(121U), recipe = Digest(120U);
    std::array<std::uint8_t, 32> output{};
    std::size_t bytes = 0U;
    laplace_content_materialization_receipt receipt{};
    ASSERT_EQ(laplace_content_materialize_encoded(&root.node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(bytes, 2U);
    EXPECT_EQ(std::memcmp(output.data(), "AB", 2U), 0);
    EXPECT_EQ(receipt.resolved_node_count, 2U);
    EXPECT_EQ(state.selected_resolve_calls, 2U);
    state.entries[1].node.identity_witness.bytes[31] ^= 1U;
    EXPECT_EQ(laplace_content_materialize_encoded(&root.node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH);
    EXPECT_EQ(bytes, 0U);
}

TEST(ContentMaterialization, SingletonFormPreservesContentTierThroughItsExactChildPhysicality) {
    auto child = Composite({{Codepoint(0x41U), 1U, 0x41U, 0U, true},
        {Codepoint(0x42U), 1U, 0x42U, 0U, true}}, 1U, 30U);
    NodeEntry wrapper{};
    wrapper.node = child.node;
    wrapper.node.logical_count = 1U;
    wrapper.node.carrier_count = 1U;
    wrapper.node.tier_floor = child.node.tier_floor;
    wrapper.node.physicality_id = Digest(40U);
    wrapper.node.node_receipt_id = Digest(41U);
    wrapper.trajectory_read_receipt = Digest(42U);
    wrapper.carriers.resize(1U);
    ASSERT_EQ(laplace_trajectory_composition_encode(&child.node.entity_id, 1U, 1U,
        Metadata(1U, false, 0U), wrapper.carriers.data()), LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_persistence_trajectory_fingerprint(wrapper.carriers.data(), 1U,
        &wrapper.node.trajectory_fingerprint), LAPLACE_PERSISTENCE_OK);
    ProviderState state{{wrapper, child}};
    auto provider = Provider(&state);
    provider.select_reference = SelectSingleChild;
    provider.resolve_selected = ResolveSelected;
    const auto request = Request();
    const auto source = Digest(121U), recipe = Digest(120U);
    std::array<std::uint8_t, 32> output{};
    std::size_t bytes = 0U;
    laplace_content_materialization_receipt receipt{};
    ASSERT_EQ(laplace_content_materialize_encoded(&wrapper.node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(bytes, 2U);
    EXPECT_EQ(std::memcmp(output.data(), "AB", 2U), 0);
    EXPECT_EQ(receipt.resolved_node_count, 2U);
    EXPECT_EQ(receipt.trajectory_carrier_count, 3U);
    state.entries[0].node.identity_witness.bytes[31] ^= 1U;
    EXPECT_EQ(laplace_content_materialize_encoded(&wrapper.node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH);
    EXPECT_EQ(bytes, 0U);
    state.entries[0].node.identity_witness.bytes[31] ^= 1U;
    state.selected_root = 1U; // An actual P -> same P cycle, not a transparent alternate P.
    EXPECT_EQ(laplace_content_materialize_encoded(&wrapper.node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_CYCLE);
    EXPECT_EQ(bytes, 0U);
}

TEST(ContentMaterialization, SelectedUnicodeSingletonRetainsAtomContentTierInsideItsParent) {
    auto wrapper = Atom(0x41U, 40U);
    wrapper.node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    wrapper.node.atom = 0U;
    wrapper.node.physicality_id = Digest(41U);
    wrapper.node.carrier_count = 1U;
    wrapper.trajectory_read_receipt = Digest(42U);
    wrapper.carriers.resize(1U);
    ASSERT_EQ(laplace_trajectory_composition_encode(&wrapper.node.entity_id, 1U, 1U,
        Metadata(0U, true, 0x41U), wrapper.carriers.data()), LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_persistence_trajectory_fingerprint(wrapper.carriers.data(), 1U,
        &wrapper.node.trajectory_fingerprint), LAPLACE_PERSISTENCE_OK);
    auto root = Composite({{wrapper.node.entity_id, 1U, 0x41U, 0U, true},
        {Codepoint(0x42U), 1U, 0x42U, 0U, true}}, 1U, 30U);
    ProviderState state{{root, wrapper}};
    auto provider = Provider(&state);
    provider.select_reference = SelectSingleChild;
    provider.resolve_selected = ResolveSelected;
    const auto request = Request();
    const auto source = Digest(121U), recipe = Digest(120U);
    std::array<std::uint8_t, 32> output{};
    std::size_t bytes = 0U;
    laplace_content_materialization_receipt receipt{};
    ASSERT_EQ(laplace_content_materialize_encoded(&root.node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(bytes, 2U);
    EXPECT_EQ(std::memcmp(output.data(), "AB", 2U), 0);
    EXPECT_EQ(receipt.resolved_node_count, 2U);
    EXPECT_EQ(receipt.trajectory_carrier_count, 3U);
    state.entries[1].node.tier_floor = 1U;
    EXPECT_EQ(laplace_content_materialize_encoded(&root.node.entity_id,
        &source, &recipe, &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8,
        output.data(), output.size(), &bytes, &receipt), LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID);
    EXPECT_EQ(bytes, 0U);
}

}  // namespace
