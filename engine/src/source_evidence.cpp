#include "laplace/source_evidence.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

int CompareDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes));
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return CompareDigest(left, right) == 0;
}

bool NodeLess(
    const laplace_evidence_lineage_record& left,
    const laplace_evidence_lineage_record& right) {
    return CompareDigest(left.node_id, right.node_id) < 0;
}

bool EdgeLess(
    const laplace_evidence_lineage_record& left,
    const laplace_evidence_lineage_record& right) {
    const int child = CompareDigest(left.node_id, right.node_id);
    return child != 0 ? child < 0 :
        CompareDigest(left.parent_node_id, right.parent_node_id) < 0;
}

bool TestimonyLess(
    const laplace_evidence_testimony_record& left,
    const laplace_evidence_testimony_record& right) {
    return CompareDigest(left.testimony_id, right.testimony_id) < 0;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_source_evidence_status Fail(
    laplace_source_evidence_batch_receipt* const receipt,
    const laplace_source_evidence_status status) {
    if (receipt != nullptr) receipt->status = static_cast<std::uint32_t>(status);
    return status;
}

}  // namespace

extern "C" laplace_source_evidence_status laplace_source_evidence_close_batch(
    const laplace_source_evidence_batch_input* const input,
    laplace_evidence_lineage_record* const lineage_records,
    const std::size_t lineage_capacity,
    std::size_t* const lineage_count,
    laplace_evidence_root_record* const root_relations,
    const std::size_t root_capacity,
    std::size_t* const root_count,
    laplace_evidence_testimony_record* const testimonies,
    const std::size_t testimony_capacity,
    std::size_t* const testimony_count,
    laplace_source_evidence_batch_receipt* const receipt) {
    if (lineage_count != nullptr) *lineage_count = 0u;
    if (root_count != nullptr) *root_count = 0u;
    if (testimony_count != nullptr) *testimony_count = 0u;
    if (receipt != nullptr) *receipt = laplace_source_evidence_batch_receipt{};
    if (input == nullptr || input->claims == nullptr || input->claim_count == 0u ||
        input->dependence_root_source_ordinal == 0u ||
        input->lineage_memory_limit_bytes == 0u || input->flags != 0u ||
        input->reserved != 0u || lineage_records == nullptr ||
        lineage_count == nullptr || root_relations == nullptr || root_count == nullptr ||
        testimonies == nullptr || testimony_count == nullptr || receipt == nullptr) {
        return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_INVALID_ARGUMENT);
    }
    if (input->claim_count > (std::numeric_limits<std::size_t>::max() - 1u) / 2u) {
        return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_OVERFLOW);
    }
    const std::size_t node_count = input->claim_count + 1u;
    const std::size_t required_lineage = input->claim_count * 2u + 1u;
    const std::size_t required_roots = node_count;
    if (lineage_capacity < required_lineage || root_capacity < required_roots ||
        testimony_capacity < input->claim_count) {
        return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_CAPACITY_INSUFFICIENT);
    }

    try {
        std::vector<laplace_evidence_lineage_record> nodes(node_count);
        std::vector<laplace_evidence_lineage_record> edges(input->claim_count);
        std::vector<laplace_evidence_lineage_record> staged_lineage(required_lineage);
        std::vector<laplace_evidence_root_record> staged_roots(required_roots);
        std::vector<laplace_evidence_testimony_record> staged_testimony(
            input->claim_count);

        auto& dependence_root = nodes[0];
        dependence_root.proposition_id = input->dependence_root_proposition_id;
        dependence_root.occurrence_id = input->dependence_root_occurrence_id;
        dependence_root.source_id = input->source_fingerprint;
        dependence_root.context_id = input->context_fingerprint;
        dependence_root.source_ordinal = input->dependence_root_source_ordinal;
        dependence_root.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
        dependence_root.epistemic_kind = input->dependence_root_epistemic_kind;
        if (laplace_evidence_node_identify(
                &dependence_root, &dependence_root.node_id) !=
            LAPLACE_EVIDENCE_LINEAGE_OK) {
            return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_LINEAGE_FAILURE);
        }
        const laplace_digest256 dependence_root_node_id = dependence_root.node_id;

        for (std::size_t index = 0u; index < input->claim_count; ++index) {
            const auto& claim = input->claims[index];
            if (claim.source_ordinal == 0u || claim.flags != 0u ||
                claim.reserved != 0u) {
                return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_INVALID_ARGUMENT);
            }
            auto& node = nodes[index + 1u];
            node.proposition_id = claim.proposition_id;
            node.occurrence_id = claim.occurrence_id;
            node.source_id = input->source_fingerprint;
            node.context_id = input->context_fingerprint;
            node.source_ordinal = claim.source_ordinal;
            node.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
            node.epistemic_kind = LAPLACE_EVIDENCE_KIND_TESTIMONY;
            if (laplace_evidence_node_identify(&node, &node.node_id) !=
                LAPLACE_EVIDENCE_LINEAGE_OK) {
                return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_LINEAGE_FAILURE);
            }

            auto& edge = edges[index];
            edge.node_id = node.node_id;
            edge.parent_node_id = dependence_root_node_id;
            edge.record_kind = LAPLACE_EVIDENCE_RECORD_DEPENDENCE_EDGE;

            auto& testimony = staged_testimony[index];
            testimony.evidence_node_id = node.node_id;
            testimony.source_profile_id = input->source_profile_id;
            testimony.recipe_receipt_id = input->recipe_receipt_id;
            testimony.trust_input_id = input->trust_input_id;
            testimony.outcome_detail_id = claim.outcome_detail_id;
            testimony.uncertainty_numerator = claim.uncertainty_numerator;
            testimony.uncertainty_denominator = claim.uncertainty_denominator;
            testimony.sample_count = claim.sample_count;
            testimony.source_type = input->source_type;
            testimony.outcome_type = claim.outcome_type;
            testimony.disposition = claim.disposition;
            if (laplace_evidence_testimony_identify(
                    &testimony, &testimony.testimony_id) !=
                LAPLACE_EVIDENCE_TESTIMONY_OK) {
                return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_TESTIMONY_FAILURE);
            }
        }

        std::sort(nodes.begin(), nodes.end(), NodeLess);
        std::sort(edges.begin(), edges.end(), EdgeLess);
        std::copy(nodes.begin(), nodes.end(), staged_lineage.begin());
        std::copy(edges.begin(), edges.end(), staged_lineage.begin() +
            static_cast<std::ptrdiff_t>(nodes.size()));

        laplace_evidence_lineage_receipt lineage_receipt{};
        laplace_evidence_lineage_error lineage_error{};
        std::size_t staged_root_count = 0u;
        if (laplace_evidence_record_lineage_batch(
                staged_lineage.data(), staged_lineage.size(),
                input->lineage_memory_limit_bytes,
                staged_roots.data(), staged_roots.size(), &staged_root_count,
                &lineage_receipt, &lineage_error) != LAPLACE_EVIDENCE_LINEAGE_OK ||
            staged_root_count != required_roots) {
            return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_LINEAGE_FAILURE);
        }

        /* A source batch is not permitted to manufacture one independent root
         * per claim. Every emitted node must resolve to the exact common
         * source/release root, with the root itself at depth zero and each claim
         * at depth one. */
        for (const auto& node : nodes) {
            const bool is_root = SameDigest(node.node_id, dependence_root_node_id);
            std::size_t matches = 0u;
            for (std::size_t root_index = 0u;
                 root_index < staged_root_count; ++root_index) {
                const auto& relation = staged_roots[root_index];
                if (!SameDigest(relation.node_id, node.node_id)) continue;
                ++matches;
                if (!SameDigest(
                        relation.root_node_id, dependence_root_node_id) ||
                    !SameId(relation.proposition_id, node.proposition_id) ||
                    relation.root_epistemic_kind !=
                        input->dependence_root_epistemic_kind ||
                    relation.path_depth != (is_root ? 0u : 1u)) {
                    return Fail(
                        receipt, LAPLACE_SOURCE_EVIDENCE_DEPENDENCE_FAILURE);
                }
            }
            if (matches != 1u) {
                return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_DEPENDENCE_FAILURE);
            }
        }

        std::sort(
            staged_testimony.begin(), staged_testimony.end(), TestimonyLess);
        laplace_evidence_testimony_receipt testimony_receipt{};
        laplace_evidence_testimony_error testimony_error{};
        if (laplace_evidence_record_testimony_batch(
                staged_testimony.data(), staged_testimony.size(),
                &testimony_receipt, &testimony_error) !=
            LAPLACE_EVIDENCE_TESTIMONY_OK) {
            return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_TESTIMONY_FAILURE);
        }

        std::copy(staged_lineage.begin(), staged_lineage.end(), lineage_records);
        std::copy_n(staged_roots.data(), staged_root_count, root_relations);
        std::copy(staged_testimony.begin(), staged_testimony.end(), testimonies);
        *lineage_count = staged_lineage.size();
        *root_count = staged_root_count;
        *testimony_count = staged_testimony.size();
        receipt->dependence_root_node_id = dependence_root_node_id;
        receipt->lineage_receipt = lineage_receipt;
        receipt->testimony_receipt = testimony_receipt;
        receipt->claim_count = static_cast<std::uint64_t>(input->claim_count);
        receipt->lineage_record_count =
            static_cast<std::uint64_t>(staged_lineage.size());
        receipt->root_relation_count =
            static_cast<std::uint64_t>(staged_root_count);
        receipt->testimony_count =
            static_cast<std::uint64_t>(staged_testimony.size());
        receipt->status = LAPLACE_SOURCE_EVIDENCE_OK;
        return LAPLACE_SOURCE_EVIDENCE_OK;
    } catch (const std::bad_alloc&) {
        return Fail(receipt, LAPLACE_SOURCE_EVIDENCE_MEMORY_FAILURE);
    }
}
