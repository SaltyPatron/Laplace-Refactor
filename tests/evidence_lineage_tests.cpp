#include "laplace/evidence_lineage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_id128 Id128(std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

bool DigestLess(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

bool DigestEqual(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_evidence_lineage_record Node(
    std::uint8_t seed,
    const laplace_id128& proposition,
    std::uint32_t kind = LAPLACE_EVIDENCE_KIND_TESTIMONY) {
    laplace_evidence_lineage_record value{};
    value.proposition_id = proposition;
    value.occurrence_id = Digest(static_cast<std::uint8_t>(seed + 0x10u));
    value.source_id = Digest(static_cast<std::uint8_t>(seed + 0x30u));
    value.context_id = Digest(static_cast<std::uint8_t>(seed + 0x50u));
    value.source_ordinal = static_cast<std::uint64_t>(seed) + 1u;
    value.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
    value.epistemic_kind = kind;
    EXPECT_EQ(laplace_evidence_node_identify(&value, &value.node_id),
              LAPLACE_EVIDENCE_LINEAGE_OK);
    return value;
}

laplace_evidence_lineage_record Edge(
    const laplace_digest256& child,
    const laplace_digest256& parent) {
    laplace_evidence_lineage_record value{};
    value.node_id = child;
    value.parent_node_id = parent;
    value.record_kind = LAPLACE_EVIDENCE_RECORD_DEPENDENCE_EDGE;
    return value;
}

std::vector<laplace_evidence_lineage_record> Ordered(
    std::vector<laplace_evidence_lineage_record> nodes,
    std::vector<laplace_evidence_lineage_record> edges) {
    std::sort(nodes.begin(), nodes.end(), [](const auto& left, const auto& right) {
        return DigestLess(left.node_id, right.node_id);
    });
    std::sort(edges.begin(), edges.end(), [](const auto& left, const auto& right) {
        if (!DigestEqual(left.node_id, right.node_id)) {
            return DigestLess(left.node_id, right.node_id);
        }
        return DigestLess(left.parent_node_id, right.parent_node_id);
    });
    nodes.insert(nodes.end(), edges.begin(), edges.end());
    return nodes;
}

struct Result {
    laplace_evidence_lineage_status status{};
    std::vector<laplace_evidence_root_record> roots;
    laplace_evidence_lineage_receipt receipt{};
    laplace_evidence_lineage_error error{};
    std::array<laplace_digest256, 64> cycle{};
};

Result Execute(
    const std::vector<laplace_evidence_lineage_record>& records,
    std::size_t capacity,
    std::uint64_t memory = UINT64_C(64) * 1024u * 1024u) {
    Result result;
    result.roots.resize(capacity);
    result.error.cycle_path = result.cycle.data();
    result.error.cycle_path_capacity = result.cycle.size();
    std::size_t count = 0u;
    result.status = laplace_evidence_record_lineage_batch(
        records.data(), records.size(), memory, result.roots.data(),
        result.roots.size(), &count, &result.receipt, &result.error);
    result.roots.resize(count);
    return result;
}

TEST(EvidenceLineage, CopiesCollapseToOneRootWhileIndependentWitnessesRemain) {
    const auto proposition = Id128(0x21u);
    const auto primary = Node(1u, proposition, LAPLACE_EVIDENCE_KIND_OBSERVED);
    const auto independent_a = Node(2u, proposition, LAPLACE_EVIDENCE_KIND_OBSERVED);
    const auto independent_b = Node(3u, proposition, LAPLACE_EVIDENCE_KIND_OBSERVED);
    std::vector<laplace_evidence_lineage_record> nodes{
        primary, independent_a, independent_b};
    std::vector<laplace_evidence_lineage_record> edges;
    for (std::uint8_t seed = 10u; seed < 20u; ++seed) {
        const auto copy = Node(seed, proposition);
        nodes.push_back(copy);
        edges.push_back(Edge(copy.node_id, primary.node_id));
    }
    const auto result = Execute(Ordered(nodes, edges), 32u);
    ASSERT_EQ(result.status, LAPLACE_EVIDENCE_LINEAGE_OK);
    EXPECT_EQ(result.receipt.node_count, 13u);
    EXPECT_EQ(result.receipt.edge_count, 10u);
    EXPECT_EQ(result.roots.size(), 13u);
    std::vector<laplace_digest256> roots;
    for (const auto& relation : result.roots) {
        if (std::none_of(roots.begin(), roots.end(), [&](const auto& existing) {
                return DigestEqual(existing, relation.root_node_id);
            })) {
            roots.push_back(relation.root_node_id);
        }
    }
    EXPECT_EQ(roots.size(), 3u);
}

TEST(EvidenceLineage, OneThousandDerivedDescendantsDoNotManufactureRoots) {
    const auto proposition = Id128(0x42u);
    std::vector<laplace_evidence_lineage_record> nodes;
    std::vector<laplace_evidence_lineage_record> edges;
    auto parent = Node(1u, proposition, LAPLACE_EVIDENCE_KIND_OBSERVED);
    nodes.push_back(parent);
    for (std::size_t index = 0u; index < 1000u; ++index) {
        auto child = Node(
            static_cast<std::uint8_t>(index % 251u + 2u), proposition,
            LAPLACE_EVIDENCE_KIND_DERIVATION);
        child.source_ordinal += static_cast<std::uint64_t>(index) * 257u;
        ASSERT_EQ(laplace_evidence_node_identify(&child, &child.node_id),
                  LAPLACE_EVIDENCE_LINEAGE_OK);
        nodes.push_back(child);
        edges.push_back(Edge(child.node_id, parent.node_id));
        parent = child;
    }
    const auto root = nodes.front().node_id;
    const auto result = Execute(Ordered(nodes, edges), 1001u);
    ASSERT_EQ(result.status, LAPLACE_EVIDENCE_LINEAGE_OK);
    ASSERT_EQ(result.roots.size(), 1001u);
    for (const auto& relation : result.roots) {
        EXPECT_TRUE(DigestEqual(relation.root_node_id, root));
    }
    EXPECT_EQ(result.receipt.root_relation_count, 1001u);
}

TEST(EvidenceLineage, ModelDescendantsRetainFamilyRootAndUnrelatedModelStaysIndependent) {
    const auto proposition = Id128(0x73u);
    const auto base = Node(1u, proposition, LAPLACE_EVIDENCE_KIND_MODEL_BEHAVIOR);
    const auto unrelated = Node(2u, proposition, LAPLACE_EVIDENCE_KIND_MODEL_BEHAVIOR);
    const auto tune_a = Node(3u, proposition, LAPLACE_EVIDENCE_KIND_MODEL_BEHAVIOR);
    const auto tune_b = Node(4u, proposition, LAPLACE_EVIDENCE_KIND_MODEL_BEHAVIOR);
    const auto distilled = Node(5u, proposition, LAPLACE_EVIDENCE_KIND_MODEL_BEHAVIOR);
    auto records = Ordered(
        {base, unrelated, tune_a, tune_b, distilled},
        {Edge(tune_a.node_id, base.node_id), Edge(tune_b.node_id, base.node_id),
         Edge(distilled.node_id, tune_a.node_id)});
    const auto result = Execute(records, 8u);
    ASSERT_EQ(result.status, LAPLACE_EVIDENCE_LINEAGE_OK);
    std::vector<laplace_digest256> unique;
    for (const auto& relation : result.roots) {
        if (std::none_of(unique.begin(), unique.end(), [&](const auto& value) {
                return DigestEqual(value, relation.root_node_id);
            })) {
            unique.push_back(relation.root_node_id);
        }
    }
    EXPECT_EQ(unique.size(), 2u);
}

TEST(EvidenceLineage, CycleRejectsTheBatchWithExactClosedOffendingPath) {
    const auto proposition = Id128(0x94u);
    const auto a = Node(1u, proposition, LAPLACE_EVIDENCE_KIND_DERIVATION);
    const auto b = Node(2u, proposition, LAPLACE_EVIDENCE_KIND_DERIVATION);
    const auto result = Execute(
        Ordered({a, b}, {Edge(a.node_id, b.node_id), Edge(b.node_id, a.node_id)}),
        4u);
    ASSERT_EQ(result.status, LAPLACE_EVIDENCE_LINEAGE_CYCLE);
    ASSERT_EQ(result.error.cycle_path_count, 3u);
    EXPECT_TRUE(DigestEqual(result.cycle[0], result.cycle[2]));
    EXPECT_FALSE(DigestEqual(result.cycle[0], result.cycle[1]));
    EXPECT_TRUE(result.roots.empty());
}

TEST(EvidenceLineage, EpistemicKindAndSourceRemainIdentityInputs) {
    const auto proposition = Id128(0xb5u);
    auto testimony = Node(1u, proposition, LAPLACE_EVIDENCE_KIND_TESTIMONY);
    auto calculation = testimony;
    calculation.epistemic_kind = LAPLACE_EVIDENCE_KIND_CALCULATION;
    ASSERT_EQ(laplace_evidence_node_identify(&calculation, &calculation.node_id),
              LAPLACE_EVIDENCE_LINEAGE_OK);
    EXPECT_FALSE(DigestEqual(testimony.node_id, calculation.node_id));
    calculation = testimony;
    calculation.source_id.bytes[0] ^= 0x80u;
    ASSERT_EQ(laplace_evidence_node_identify(&calculation, &calculation.node_id),
              LAPLACE_EVIDENCE_LINEAGE_OK);
    EXPECT_FALSE(DigestEqual(testimony.node_id, calculation.node_id));
}

TEST(EvidenceLineage, DeclaredMemoryAndOutputBoundsFailBeforePublishing) {
    const auto proposition = Id128(0xd6u);
    const auto root = Node(1u, proposition, LAPLACE_EVIDENCE_KIND_OBSERVED);
    const auto child = Node(2u, proposition, LAPLACE_EVIDENCE_KIND_TESTIMONY);
    const auto records = Ordered({root, child}, {Edge(child.node_id, root.node_id)});
    auto memory_result = Execute(records, 2u, 1u);
    EXPECT_EQ(memory_result.status, LAPLACE_EVIDENCE_LINEAGE_RESOURCE_INSUFFICIENT);
    EXPECT_TRUE(memory_result.roots.empty());
    auto capacity_result = Execute(records, 1u);
    EXPECT_EQ(capacity_result.status, LAPLACE_EVIDENCE_LINEAGE_CAPACITY_INSUFFICIENT);
    EXPECT_TRUE(capacity_result.roots.empty());
}

}  // namespace
