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
};

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
    const auto* entry = Find(state, node->entity_id);
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

}  // namespace
