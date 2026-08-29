#include "laplace/query_search.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace {

using DigestKey = std::array<std::uint8_t, 32>;
constexpr std::size_t CostCount = LAPLACE_QUERY_SEARCH_COST_COMPONENT_COUNT;
constexpr std::size_t EstimatedNodeOverhead = 96U;

bool Zero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

DigestKey Key(const laplace_digest256& value) {
    DigestKey key{};
    std::copy(std::begin(value.bytes), std::end(value.bytes), key.begin());
    return key;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

void HashState(blake3_hasher* const hasher, const laplace_query_search_state& state) {
    HashDigest(hasher, state.state_id);
    HashDigest(hasher, state.anchor_id);
    HashDigest(hasher, state.dominance_key);
    HashDigest(hasher, state.binding_fingerprint);
    HashDigest(hasher, state.obligation_fingerprint);
    HashDigest(hasher, state.context_fingerprint);
    HashDigest(hasher, state.evidence_epoch);
    HashDigest(hasher, state.boundary_id);
    HashU64(hasher, state.heuristic_cost);
    HashU32(hasher, state.depth);
    HashU32(hasher, state.flags);
}

bool SameState(
    const laplace_query_search_state& left,
    const laplace_query_search_state& right) {
    return Same(left.state_id, right.state_id) &&
        Same(left.anchor_id, right.anchor_id) &&
        Same(left.dominance_key, right.dominance_key) &&
        Same(left.binding_fingerprint, right.binding_fingerprint) &&
        Same(left.obligation_fingerprint, right.obligation_fingerprint) &&
        Same(left.context_fingerprint, right.context_fingerprint) &&
        Same(left.evidence_epoch, right.evidence_epoch) &&
        Same(left.boundary_id, right.boundary_id) &&
        left.heuristic_cost == right.heuristic_cost &&
        left.depth == right.depth && left.flags == right.flags;
}

bool StateValid(
    const laplace_query_search_program& program,
    const laplace_query_search_state& state) {
    if (Zero(state.state_id) || Zero(state.anchor_id) ||
        Zero(state.dominance_key) || Zero(state.obligation_fingerprint) ||
        Zero(state.context_fingerprint) || Zero(state.evidence_epoch) ||
        Zero(state.boundary_id) ||
        (state.flags & ~LAPLACE_QUERY_SEARCH_STATE_KNOWN_MASK) != 0U ||
        !Same(state.boundary_id, program.boundary_id) ||
        !Same(state.evidence_epoch, program.evidence_epoch) ||
        state.depth > program.budget.max_depth) {
        return false;
    }
    if ((program.flags & LAPLACE_QUERY_SEARCH_FLAG_ASTAR) == 0U &&
        state.heuristic_cost != 0U) {
        return false;
    }
    if ((state.flags & LAPLACE_QUERY_SEARCH_STATE_TERMINAL) != 0U &&
        (program.flags & LAPLACE_QUERY_SEARCH_FLAG_REQUEST_OPTIMAL) != 0U &&
        state.heuristic_cost != 0U) {
        return false;
    }
    return true;
}

bool ProgramValid(const laplace_query_search_program& program) {
    if (Zero(program.program_id) || Zero(program.goal_id) ||
        Zero(program.boundary_id) || Zero(program.evidence_epoch) ||
        Zero(program.result_contract_fingerprint) || program.reserved != 0U ||
        (program.flags & ~LAPLACE_QUERY_SEARCH_FLAG_KNOWN_MASK) != 0U ||
        program.budget.max_expanded_states == 0U ||
        program.budget.max_transition_records == 0U ||
        program.budget.max_emitted_states == 0U ||
        program.budget.max_frontier_states == 0U ||
        program.budget.max_memory_bytes == 0U ||
        program.budget.max_provider_calls == 0U ||
        program.budget.max_depth == 0U ||
        program.budget.requested_path_count == 0U ||
        program.budget.frontier_batch_width == 0U ||
        program.budget.transition_batch_capacity == 0U) {
        return false;
    }
    bool any_weight = false;
    for (const auto weight : program.cost_weights) {
        any_weight = any_weight || weight != 0U;
    }
    if (!any_weight) {
        return false;
    }
    if ((program.flags & LAPLACE_QUERY_SEARCH_FLAG_REQUIRE_SET_ORIENTED) != 0U &&
        program.budget.frontier_batch_width < 2U) {
        return false;
    }
    const bool astar =
        (program.flags & LAPLACE_QUERY_SEARCH_FLAG_ASTAR) != 0U;
    const bool admissible =
        (program.flags & LAPLACE_QUERY_SEARCH_FLAG_HEURISTIC_ADMISSIBLE) != 0U;
    const bool consistent =
        (program.flags & LAPLACE_QUERY_SEARCH_FLAG_HEURISTIC_CONSISTENT) != 0U;
    const bool reopen =
        (program.flags & LAPLACE_QUERY_SEARCH_FLAG_ALLOW_REOPEN) != 0U;
    const bool optimal =
        (program.flags & LAPLACE_QUERY_SEARCH_FLAG_REQUEST_OPTIMAL) != 0U;
    if (!astar && (admissible || consistent || reopen ||
                   !Zero(program.heuristic_proof_fingerprint))) {
        return false;
    }
    if (optimal && astar && (!admissible ||
                             Zero(program.heuristic_proof_fingerprint))) {
        return false;
    }
    if (astar && !consistent && !reopen) {
        return false;
    }
    return true;
}

void HashProgram(
    const laplace_query_search_program& program,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_QUERY_SEARCH_PROGRAM_DOMAIN,
        sizeof(LAPLACE_QUERY_SEARCH_PROGRAM_DOMAIN) - 1U);
    HashDigest(&hasher, program.program_id);
    HashDigest(&hasher, program.goal_id);
    HashDigest(&hasher, program.boundary_id);
    HashDigest(&hasher, program.evidence_epoch);
    HashDigest(&hasher, program.heuristic_proof_fingerprint);
    HashDigest(&hasher, program.result_contract_fingerprint);
    for (const auto weight : program.cost_weights) {
        HashU64(&hasher, weight);
    }
