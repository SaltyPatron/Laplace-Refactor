#include "laplace/cognition_materialization.h"

#include "laplace/identity.h"
#include "laplace/persistence.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <vector>

namespace laplace_cognition_materialization_detail {

constexpr char ReadsetDomain[] = "laplace-cognition-materialization-readset-v1";
constexpr char OutputDomain[] = "laplace-cognition-materialization-output-v1";
constexpr char ReceiptDomain[] = "laplace-cognition-materialization-receipt-v1";

bool BytesZero(const std::uint8_t* const bytes, const std::size_t count) {
    std::uint8_t aggregate = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        aggregate = static_cast<std::uint8_t>(aggregate | bytes[index]);
    }
    return aggregate == 0U;
}

bool IdZero(const laplace_id128& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

bool DigestZero(const laplace_digest256& value) {
    return BytesZero(value.bytes, sizeof(value.bytes));
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool AddOverflow(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const result) {
    if (result == nullptr ||
        right > std::numeric_limits<std::uint64_t>::max() - left) {
        return true;
    }
    *result = left + right;
    return false;
}

bool MultiplyOverflow(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const result) {
    if (result == nullptr ||
        (left != 0U &&
         right > std::numeric_limits<std::uint64_t>::max() / left)) {
        return true;
    }
    *result = left * right;
    return false;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool RealizationValid(const laplace_cognition_realization_result& realization) {
    return realization.version == LAPLACE_COGNITION_REALIZATION_VERSION &&
        realization.disposition == LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE &&
        realization.missing_obligation_count == 0U &&
        DigestZero(realization.missing_obligation_fingerprint) &&
        !IdZero(realization.content_id) &&
        !DigestZero(realization.candidate_receipt_id) &&
        !DigestZero(realization.realization_recipe_id) &&
        realization.match_class >= LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE &&
        realization.match_class <= LAPLACE_COGNITION_REALIZATION_CANDIDATE_STRUCTURAL_FALLBACK &&
        (realization.flags & ~LAPLACE_COGNITION_REALIZATION_CANDIDATE_KNOWN_FLAGS) == 0U;
}

bool RequestValid(const laplace_cognition_materialization_request& request) {
    return request.version == LAPLACE_COGNITION_MATERIALIZATION_VERSION &&
        request.maximum_nodes > 0U &&
        request.maximum_output_bytes > 0U &&
        request.maximum_nodes <= static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()) &&
        request.maximum_trajectory_carriers <= static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()) &&
        request.maximum_output_bytes <= static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max());
}

bool ProviderValid(const laplace_cognition_materialization_provider_v1& provider) {
    return provider.abi_major == LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR &&
        provider.abi_minor <= LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR &&
        provider.resolve_node != nullptr &&
        provider.read_trajectory != nullptr &&
        !DigestZero(provider.provider_fingerprint) &&
        provider.flags == 0U && provider.reserved == 0U;
}

bool ReservedZero(const laplace_cognition_materialization_node& node) {
    return BytesZero(node.reserved8, sizeof(node.reserved8));
}

laplace_cognition_materialization_status ValidateNode(
    const laplace_cognition_materialization_node& node,
    const laplace_id128& requested) {
    if (!SameId(node.entity_id, requested) ||
        std::memcmp(node.entity_id.bytes, node.identity_witness.bytes,
                    sizeof(node.entity_id.bytes)) != 0 ||
        DigestZero(node.node_receipt_id) || !ReservedZero(node)) {
        return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
    }

    if (node.kind == LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM) {
        if (node.tier_floor != 0U || node.logical_count != 1U ||
            node.carrier_count != 0U ||
            node.atom > LAPLACE_UNICODE_POSITION_MAXIMUM ||
            !DigestZero(node.physicality_id) ||
            !DigestZero(node.trajectory_fingerprint)) {
            return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
        }
        return LAPLACE_COGNITION_MATERIALIZATION_OK;
    }

    if (node.kind == LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION) {
        if (node.tier_floor == 0U || node.logical_count <= 1U ||
            node.carrier_count == 0U || node.atom != 0U ||
            DigestZero(node.physicality_id) ||
            DigestZero(node.trajectory_fingerprint)) {
            return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
        }
        return LAPLACE_COGNITION_MATERIALIZATION_OK;
    }

    return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
}

void HashNodeRead(
    blake3_hasher* const hasher,
    const laplace_cognition_materialization_node& node) {
    HashId(hasher, node.entity_id);
    HashDigest(hasher, node.identity_witness);
    HashDigest(hasher, node.physicality_id);
    HashDigest(hasher, node.trajectory_fingerprint);
    HashDigest(hasher, node.node_receipt_id);
    HashU64(hasher, node.logical_count);
    HashU64(hasher, node.carrier_count);
    HashU32(hasher, node.atom);
    HashU32(hasher, node.kind);
    HashU32(hasher, node.tier_floor);
}

struct Counters final {
    std::uint64_t nodes{};
    std::uint64_t carriers{};
    std::uint64_t codepoints{};
    std::uint32_t maximum_depth{};
};

struct Context final {
    const laplace_cognition_materialization_request* request{};
    const laplace_cognition_materialization_provider_v1* provider{};
    std::vector<std::uint8_t>* output{};
    std::vector<laplace_id128> active{};
    struct ValidatedSlice final {
        std::size_t first{};
        std::size_t bytes{};
        std::uint64_t codepoints{};
        std::uint32_t height{};
        std::uint8_t tier{};
    };
    std::map<std::array<std::uint8_t, 16>, ValidatedSlice> validated;
    Counters counters{};
    blake3_hasher readset{};
};

laplace_cognition_materialization_status ReserveOutput(
    Context& context,
    const std::uint64_t additional_bytes) {
    const auto current = static_cast<std::uint64_t>(context.output->size());
    std::uint64_t next{};
    if (AddOverflow(current, additional_bytes, &next) ||
        next > context.request->maximum_output_bytes ||
        next > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }
    try {
        if (next > context.output->max_size()) {
            return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
        }
        context.output->resize(static_cast<std::size_t>(next));
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_MATERIALIZATION_MEMORY_FAILURE;
    }
    return LAPLACE_COGNITION_MATERIALIZATION_OK;
}

laplace_cognition_materialization_status AddCodepoints(
    Context& context,
    const std::uint64_t count) {
    std::uint64_t next{};
    if (AddOverflow(context.counters.codepoints, count, &next)) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }
    context.counters.codepoints = next;
    return LAPLACE_COGNITION_MATERIALIZATION_OK;
}

laplace_cognition_materialization_status AppendAtomRun(
    Context& context,
    const std::uint32_t atom,
    const std::uint64_t multiplicity) {
    if (multiplicity == 0U) {
        return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
    }

    std::array<std::uint8_t, 4> encoded{};
    std::size_t encoded_bytes = 0U;
    if (laplace_unicode_position_encode(
            atom, encoded.data(), &encoded_bytes) != LAPLACE_IDENTITY_OK ||
        encoded_bytes == 0U) {
        return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
    }

    std::uint64_t added{};
    if (MultiplyOverflow(
            static_cast<std::uint64_t>(encoded_bytes), multiplicity, &added)) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }

    const std::size_t first = context.output->size();
    const auto reserve_status = ReserveOutput(context, added);
    if (reserve_status != LAPLACE_COGNITION_MATERIALIZATION_OK) {
        return reserve_status;
    }

    for (std::uint64_t repeat = 0U; repeat < multiplicity; ++repeat) {
        const auto repeat_offset = static_cast<std::size_t>(repeat) * encoded_bytes;
        std::memcpy(
            context.output->data() + first + repeat_offset,
            encoded.data(), encoded_bytes);
    }
    return AddCodepoints(context, multiplicity);
}

laplace_cognition_materialization_status RepeatMaterializedSlice(
    Context& context,
    const std::size_t first,
    const std::size_t unit_bytes,
    const std::uint64_t unit_codepoints,
    const std::uint64_t multiplicity) {
    if (multiplicity <= 1U) return LAPLACE_COGNITION_MATERIALIZATION_OK;

    const std::uint64_t extra_repetitions = multiplicity - 1U;
    std::uint64_t extra_bytes{};
    std::uint64_t extra_codepoints{};
    if (MultiplyOverflow(
            static_cast<std::uint64_t>(unit_bytes),
            extra_repetitions, &extra_bytes) ||
        MultiplyOverflow(
            unit_codepoints, extra_repetitions, &extra_codepoints)) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }

    const std::size_t prior_size = context.output->size();
    const auto reserve_status = ReserveOutput(context, extra_bytes);
    if (reserve_status != LAPLACE_COGNITION_MATERIALIZATION_OK) {
        return reserve_status;
    }

    for (std::uint64_t repeat = 0U; repeat < extra_repetitions; ++repeat) {
        const auto repeat_offset = static_cast<std::size_t>(repeat) * unit_bytes;
        std::memcpy(
            context.output->data() + prior_size + repeat_offset,
            context.output->data() + first,
            unit_bytes);
    }
    return AddCodepoints(context, extra_codepoints);
}

bool ActiveContains(const Context& context, const laplace_id128& entity) {
    return std::any_of(
        context.active.begin(), context.active.end(),
        [&](const laplace_id128& current) { return SameId(current, entity); });
}

laplace_cognition_materialization_status MaterializeEntity(
    Context& context,
    const laplace_id128& entity,
    const std::uint32_t depth,
    const bool has_expected_tier,
    const std::uint8_t expected_tier,
    std::uint32_t* const height) {
    *height = 0U;
    if (depth > context.request->maximum_depth) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }
    context.counters.maximum_depth =
        std::max(context.counters.maximum_depth, depth);

    if (ActiveContains(context, entity)) {
        return LAPLACE_COGNITION_MATERIALIZATION_CYCLE;
    }
    std::array<std::uint8_t, 16> key{};
    std::copy(std::begin(entity.bytes), std::end(entity.bytes), key.begin());
    const auto cached = context.validated.find(key);
    if (cached != context.validated.end()) {
        const auto& slice = cached->second;
        if (has_expected_tier && slice.tier != expected_tier) {
            return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
        }
        if (slice.height > context.request->maximum_depth - depth) {
            return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
        }
        *height = slice.height;
        context.counters.maximum_depth = std::max(
            context.counters.maximum_depth, depth + slice.height);
        return RepeatMaterializedSlice(
            context, slice.first, slice.bytes, slice.codepoints, 2U);
    }
    if (context.counters.nodes >= context.request->maximum_nodes) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }

    laplace_cognition_materialization_node node{};
    if (context.provider->resolve_node(
            context.provider->state, &entity, &node) != 0) {
        return LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_FAILURE;
    }
    ++context.counters.nodes;

    const auto node_status = ValidateNode(node, entity);
    if (node_status != LAPLACE_COGNITION_MATERIALIZATION_OK) {
        return node_status;
    }
    if (has_expected_tier && node.tier_floor != expected_tier) {
        return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
    }
    HashNodeRead(&context.readset, node);
    const std::size_t subtree_first = context.output->size();
    const std::uint64_t subtree_codepoints = context.counters.codepoints;

    if (node.kind == LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM) {
        laplace_id128 expected_id{};
        laplace_digest256 expected_witness{};
        if (laplace_identity_codepoint_witness(
                node.atom, &expected_id, &expected_witness) != LAPLACE_IDENTITY_OK ||
            !SameId(expected_id, node.entity_id) ||
            !SameDigest(expected_witness, node.identity_witness)) {
            return LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH;
        }
        const auto status = AppendAtomRun(context, node.atom, 1U);
        if (status == LAPLACE_COGNITION_MATERIALIZATION_OK) {
            context.validated.emplace(key, Context::ValidatedSlice{
                subtree_first, context.output->size() - subtree_first, 1U, 0U, 0U});
        }
        return status;
    }

    if (context.counters.carriers >
        context.request->maximum_trajectory_carriers) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }
    const std::uint64_t remaining_carriers =
        context.request->maximum_trajectory_carriers - context.counters.carriers;
    if (node.carrier_count > remaining_carriers ||
        node.carrier_count > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }

    std::vector<laplace_trajectory_carrier> carriers;
    std::vector<laplace_composition_occurrence> occurrences;
    if (node.carrier_count > carriers.max_size() ||
        node.carrier_count > occurrences.max_size()) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    }
    try {
        const auto carrier_count = static_cast<std::size_t>(node.carrier_count);
        carriers.resize(carrier_count);
        occurrences.resize(carrier_count);
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_MATERIALIZATION_MEMORY_FAILURE;
    }

    laplace_digest256 trajectory_read_receipt{};
    if (context.provider->read_trajectory(
            context.provider->state, &node, carriers.data(), carriers.size(),
            &trajectory_read_receipt) != 0 ||
        DigestZero(trajectory_read_receipt)) {
        return LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_FAILURE;
    }
    context.counters.carriers += node.carrier_count;
    HashDigest(&context.readset, trajectory_read_receipt);

    laplace_digest256 trajectory_fingerprint{};
    if (laplace_persistence_trajectory_fingerprint(
            carriers.data(), carriers.size(), &trajectory_fingerprint) !=
            LAPLACE_PERSISTENCE_OK ||
        !SameDigest(trajectory_fingerprint, node.trajectory_fingerprint)) {
        return LAPLACE_COGNITION_MATERIALIZATION_TRAJECTORY_INVALID;
    }

    std::uint64_t logical_count = 0U;
    if (laplace_trajectory_composition_decode(
            carriers.data(), carriers.size(), occurrences.data(),
            occurrences.size(), &logical_count) != LAPLACE_TRAJECTORY_OK ||
        logical_count != node.logical_count) {
        return LAPLACE_COGNITION_MATERIALIZATION_TRAJECTORY_INVALID;
    }

    std::vector<laplace_id_run> runs;
    try {
        runs.reserve(occurrences.size());
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_MATERIALIZATION_MEMORY_FAILURE;
    }

    for (const auto& occurrence : occurrences) {
        if (occurrence.run_length == 0U || occurrence.tier >= node.tier_floor) {
            return LAPLACE_COGNITION_MATERIALIZATION_TRAJECTORY_INVALID;
        }
        const auto occurrence_count =
            static_cast<std::uint64_t>(occurrence.run_length);
        if (!runs.empty() && SameId(runs.back().id, occurrence.entity_id)) {
            std::uint64_t merged{};
            if (AddOverflow(runs.back().count, occurrence_count, &merged)) {
                return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
            }
            runs.back().count = merged;
        } else {
            runs.push_back(laplace_id_run{occurrence.entity_id, occurrence_count});
        }
    }

    std::uint64_t identity_logical_count = 0U;
    laplace_id128 reconstructed_id{};
    laplace_digest256 reconstructed_witness{};
    if (laplace_identity_composite_runs_witness(
            runs.data(), runs.size(), nullptr, &identity_logical_count,
            &reconstructed_id, &reconstructed_witness) != LAPLACE_IDENTITY_OK ||
        identity_logical_count != node.logical_count ||
        !SameId(reconstructed_id, node.entity_id) ||
        !SameDigest(reconstructed_witness, node.identity_witness)) {
        return LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH;
    }

    context.active.push_back(node.entity_id);
    for (const auto& occurrence : occurrences) {
        if (occurrence.has_atom != 0U) {
            if (occurrence.tier != 0U ||
                depth == std::numeric_limits<std::uint32_t>::max() ||
                depth + 1U > context.request->maximum_depth) {
                context.active.pop_back();
                return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
            }

            laplace_id128 expected_id{};
            laplace_digest256 expected_witness{};
            if (laplace_identity_codepoint_witness(
                    occurrence.atom, &expected_id, &expected_witness) !=
                    LAPLACE_IDENTITY_OK ||
                !SameId(expected_id, occurrence.entity_id)) {
                context.active.pop_back();
                return LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH;
            }

            context.counters.maximum_depth =
                std::max(context.counters.maximum_depth, depth + 1U);
            *height = std::max(*height, 1U);
            const auto atom_status = AppendAtomRun(
                context, occurrence.atom,
                static_cast<std::uint64_t>(occurrence.run_length));
            if (atom_status != LAPLACE_COGNITION_MATERIALIZATION_OK) {
                context.active.pop_back();
                return atom_status;
            }
            continue;
        }

        if (depth == std::numeric_limits<std::uint32_t>::max()) {
            context.active.pop_back();
            return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
        }

        const std::size_t first = context.output->size();
        const std::uint64_t codepoints_before = context.counters.codepoints;
        std::uint32_t child_height = 0U;
        const auto child_status = MaterializeEntity(
            context, occurrence.entity_id, depth + 1U, true, occurrence.tier,
            &child_height);
        if (child_status != LAPLACE_COGNITION_MATERIALIZATION_OK) {
            context.active.pop_back();
            return child_status;
        }
        *height = std::max(*height, child_height + 1U);

        const std::size_t unit_bytes = context.output->size() - first;
        const std::uint64_t unit_codepoints =
            context.counters.codepoints - codepoints_before;
        if (unit_bytes == 0U || unit_codepoints == 0U) {
            context.active.pop_back();
            return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
        }

        const auto repeat_status = RepeatMaterializedSlice(
            context, first, unit_bytes, unit_codepoints,
            static_cast<std::uint64_t>(occurrence.run_length));
        if (repeat_status != LAPLACE_COGNITION_MATERIALIZATION_OK) {
            context.active.pop_back();
            return repeat_status;
        }
    }
    context.active.pop_back();
    context.validated.emplace(key, Context::ValidatedSlice{
        subtree_first, context.output->size() - subtree_first,
        context.counters.codepoints - subtree_codepoints, *height, node.tier_floor});
    return LAPLACE_COGNITION_MATERIALIZATION_OK;
}

void OutputFingerprint(
    const std::vector<std::uint8_t>& output,
    laplace_digest256* const fingerprint) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, OutputDomain, sizeof(OutputDomain) - 1U);
    if (!output.empty()) {
        blake3_hasher_update(&hasher, output.data(), output.size());
    }
    Finish(&hasher, fingerprint);
}

void ReceiptIdentity(
    const laplace_cognition_materialization_receipt& receipt,
    laplace_digest256* const identity) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, ReceiptDomain, sizeof(ReceiptDomain) - 1U);
    HashDigest(&hasher, receipt.source_candidate_receipt_id);
    HashDigest(&hasher, receipt.source_recipe_id);
    HashDigest(&hasher, receipt.provider_fingerprint);
    HashDigest(&hasher, receipt.readset_fingerprint);
    HashDigest(&hasher, receipt.output_fingerprint);
    HashId(&hasher, receipt.root_content_id);
    HashU64(&hasher, receipt.resolved_node_count);
    HashU64(&hasher, receipt.trajectory_carrier_count);
    HashU64(&hasher, receipt.codepoint_count);
    HashU64(&hasher, receipt.output_bytes);
    HashU32(&hasher, receipt.maximum_depth_observed);
    Finish(&hasher, identity);
}

}  // namespace laplace_cognition_materialization_detail

extern "C" laplace_cognition_materialization_status
laplace_cognition_realization_materialize_utf8(
    const laplace_cognition_realization_result* const realization,
    const laplace_cognition_materialization_request* const request,
    const laplace_cognition_materialization_provider_v1* const provider,
    std::uint8_t* const output,
    const std::size_t output_capacity,
    std::size_t* const output_bytes,
    laplace_cognition_materialization_receipt* const receipt) {
    using namespace laplace_cognition_materialization_detail;

    if (output_bytes != nullptr) *output_bytes = 0U;
    if (receipt != nullptr) *receipt = laplace_cognition_materialization_receipt{};
    if (realization == nullptr || request == nullptr || provider == nullptr ||
        output == nullptr || output_bytes == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_MATERIALIZATION_INVALID_ARGUMENT;
    }
    if (!RealizationValid(*realization)) {
        return LAPLACE_COGNITION_MATERIALIZATION_INVALID_REALIZATION;
    }
    if (!RequestValid(*request)) {
        return LAPLACE_COGNITION_MATERIALIZATION_INVALID_ARGUMENT;
    }
    if (!ProviderValid(*provider)) {
        return LAPLACE_COGNITION_MATERIALIZATION_INVALID_PROVIDER;
    }

    std::vector<std::uint8_t> materialized;
    try {
        materialized.reserve(static_cast<std::size_t>(
            std::min<std::uint64_t>(
                request->maximum_output_bytes, UINT64_C(4096))));
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_MATERIALIZATION_MEMORY_FAILURE;
    }

    Context context{};
    context.request = request;
    context.provider = provider;
    context.output = &materialized;
    blake3_hasher_init(&context.readset);
    blake3_hasher_update(
        &context.readset, ReadsetDomain, sizeof(ReadsetDomain) - 1U);

    std::uint32_t height = 0U;
    laplace_cognition_materialization_status status;
    try {
        status = MaterializeEntity(
            context, realization->content_id, 0U, false, 0U, &height);
    } catch (const std::length_error&) {
        return LAPLACE_COGNITION_MATERIALIZATION_LIMIT;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_MATERIALIZATION_MEMORY_FAILURE;
    }
    if (status != LAPLACE_COGNITION_MATERIALIZATION_OK) {
        return status;
    }
    if (materialized.empty()) {
        return LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID;
    }
    if (materialized.size() > output_capacity) {
        return LAPLACE_COGNITION_MATERIALIZATION_CAPACITY;
    }

    laplace_cognition_materialization_receipt resolved{};
    resolved.source_candidate_receipt_id = realization->candidate_receipt_id;
    resolved.source_recipe_id = realization->realization_recipe_id;
    resolved.provider_fingerprint = provider->provider_fingerprint;
    Finish(&context.readset, &resolved.readset_fingerprint);
    OutputFingerprint(materialized, &resolved.output_fingerprint);
    resolved.root_content_id = realization->content_id;
    resolved.resolved_node_count = context.counters.nodes;
    resolved.trajectory_carrier_count = context.counters.carriers;
    resolved.codepoint_count = context.counters.codepoints;
    resolved.output_bytes = static_cast<std::uint64_t>(materialized.size());
    resolved.maximum_depth_observed = context.counters.maximum_depth;
    resolved.version = LAPLACE_COGNITION_MATERIALIZATION_RECEIPT_VERSION;
    ReceiptIdentity(resolved, &resolved.materialization_id);

    std::memcpy(output, materialized.data(), materialized.size());
    *output_bytes = materialized.size();
    *receipt = resolved;
    return LAPLACE_COGNITION_MATERIALIZATION_OK;
}
