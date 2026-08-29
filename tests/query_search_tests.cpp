#include "laplace/query_search.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index + 1U);
    }
    return value;
}

laplace_digest256 Boundary() { return Digest(200U); }
laplace_digest256 Epoch() { return Digest(220U); }

laplace_query_search_state State(
    std::uint8_t id,
    std::uint8_t anchor,
    std::uint8_t dominance,
    std::uint32_t depth,
    std::uint64_t heuristic,
    bool terminal = false) {
    laplace_query_search_state state{};
    state.state_id = Digest(id);
    state.anchor_id = Digest(anchor);
    state.dominance_key = Digest(dominance);
    state.binding_fingerprint = Digest(static_cast<std::uint8_t>(id + 40U));
    state.obligation_fingerprint = Digest(static_cast<std::uint8_t>(id + 60U));
    state.context_fingerprint = Digest(180U);
    state.evidence_epoch = Epoch();
    state.boundary_id = Boundary();
    state.heuristic_cost = heuristic;
    state.depth = depth;
    state.flags = terminal ? LAPLACE_QUERY_SEARCH_STATE_TERMINAL : 0U;
    return state;
}

struct Edge {
    std::uint8_t target{};
    std::uint8_t anchor{};
    std::uint8_t dominance{};
    std::uint64_t cost{};
    std::uint64_t heuristic{};
    bool terminal{};
    std::uint8_t evidence_root{};
};

struct FixtureProvider {
    std::map<std::uint8_t, std::vector<Edge>> edges;
    std::vector<std::vector<std::uint8_t>> calls;
    std::uint32_t limiting_disposition{};
};

int Expand(
    void* opaque,
    const laplace_query_search_state* states,
    const std::uint64_t*,
    std::size_t state_count,
    laplace_query_search_transition* transitions,
    std::size_t transition_capacity,
    std::size_t* transition_count,
    laplace_query_search_expansion_receipt* receipt) {
    auto* const fixture = static_cast<FixtureProvider*>(opaque);
    fixture->calls.emplace_back();
    std::size_t emitted = 0U;
    for (std::size_t index = 0U; index < state_count; ++index) {
        const auto id = static_cast<std::uint8_t>(states[index].state_id.bytes[0] - 1U);
        fixture->calls.back().push_back(id);
        const auto found = fixture->edges.find(id);
        if (found == fixture->edges.end()) {
            continue;
        }
        for (const auto& edge : found->second) {
            if (emitted >= transition_capacity) {
                return 1;
            }
            auto& transition = transitions[emitted++];
            transition = laplace_query_search_transition{};
            transition.transition_id = Digest(
                static_cast<std::uint8_t>(20U + id * 7U + edge.target));
            transition.source_state_id = states[index].state_id;
            transition.target = State(
                edge.target, edge.anchor, edge.dominance,
                states[index].depth + 1U, edge.heuristic, edge.terminal);
            transition.law_fingerprint = Digest(150U);
            if (edge.evidence_root != 0U) {
                transition.evidence_root_fingerprint = Digest(edge.evidence_root);
            }
            transition.calculation_receipt = Digest(160U);
            transition.cost_components[0] = edge.cost;
            transition.relation_family = 1U;
            transition.source_layer = 1U;
            transition.direction = 1U;
        }
    }
    *transition_count = emitted;
    receipt->receipt_id = Digest(
        static_cast<std::uint8_t>(100U + fixture->calls.size()));
    receipt->frontier_state_count = state_count;
    receipt->emitted_transition_count = emitted;
    receipt->rows_examined = emitted;
    receipt->index_plan_count = state_count == 0U ? 0U : 1U;
    receipt->crossing_count = emitted;
    receipt->limiting_disposition =
        emitted == 0U ? fixture->limiting_disposition : 0U;
    return 0;
}

laplace_query_search_program Program(
    std::uint32_t flags,
    std::uint32_t requested_paths = 1U,
    std::uint32_t max_depth = 8U) {
    laplace_query_search_program program{};
    program.program_id = Digest(1U);
    program.goal_id = Digest(2U);
    program.boundary_id = Boundary();
    program.evidence_epoch = Epoch();
    if ((flags & LAPLACE_QUERY_SEARCH_FLAG_ASTAR) != 0U) {
        program.heuristic_proof_fingerprint = Digest(3U);
    }
    program.result_contract_fingerprint = Digest(4U);
    program.cost_weights[0] = 1U;
    program.budget.max_expanded_states = 100U;
    program.budget.max_transition_records = 100U;
    program.budget.max_emitted_states = 100U;
    program.budget.max_frontier_states = 100U;
    program.budget.max_memory_bytes = UINT64_C(10000000);
    program.budget.max_io_operations = 100U;
    program.budget.max_database_operations = 100U;
    program.budget.max_provider_calls = 100U;
    program.budget.max_depth = max_depth;
    program.budget.requested_path_count = requested_paths;
    program.budget.frontier_batch_width = 16U;
    program.budget.transition_batch_capacity = 32U;
    program.flags = flags;
    return program;
}

laplace_query_search_provider_v1 Provider(FixtureProvider* fixture) {
    laplace_query_search_provider_v1 provider{};
    provider.state = fixture;
    provider.expand_batch = Expand;
    provider.abi_major = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR;
    return provider;
}

class ResultHandle {
public:
    ~ResultHandle() { laplace_query_search_result_destroy(&value); }
    laplace_query_search_result* value{};
};

TEST(QuerySearch, UsesDeclaredAStarPriority) {
    FixtureProvider fixture;
    fixture.edges[10U] = {
        Edge{11U, 11U, 11U, 1U, 9U, false, 0U},
        Edge{12U, 12U, 12U, 5U, 0U, false, 0U}};
    fixture.edges[11U] = {Edge{13U, 13U, 13U, 9U, 0U, true, 0U}};
    fixture.edges[12U] = {Edge{14U, 14U, 14U, 0U, 0U, true, 0U}};
    auto program = Program(
        LAPLACE_QUERY_SEARCH_FLAG_ASTAR |
        LAPLACE_QUERY_SEARCH_FLAG_HEURISTIC_ADMISSIBLE |
        LAPLACE_QUERY_SEARCH_FLAG_HEURISTIC_CONSISTENT |
        LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE |
        LAPLACE_QUERY_SEARCH_FLAG_REQUEST_OPTIMAL);
    auto initial = State(10U, 10U, 10U, 0U, 5U);
    auto provider = Provider(&fixture);
    ResultHandle result;
    laplace_query_search_receipt receipt{};
    ASSERT_EQ(laplace_query_search_execute(
                  &program, &initial, 1U, &provider, &result.value, &receipt),
              LAPLACE_QUERY_SEARCH_OK);
    ASSERT_GE(fixture.calls.size(), 2U);
    ASSERT_EQ(fixture.calls[1U].size(), 1U);
    EXPECT_EQ(fixture.calls[1U][0U], 12U);
    laplace_query_search_path_summary path{};
    ASSERT_EQ(laplace_query_search_result_path(result.value, 0U, &path),
              LAPLACE_QUERY_SEARCH_OK);
    EXPECT_EQ(path.total_cost, 5U);
    EXPECT_EQ(receipt.disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE);
    EXPECT_NE(receipt.flags & LAPLACE_QUERY_SEARCH_RECEIPT_OPTIMAL_CERTIFIED, 0U);
}

TEST(QuerySearch, PreservesDepthFeasibleDominance) {
    FixtureProvider fixture;
    fixture.edges[20U] = {
        Edge{21U, 21U, 21U, 0U, 0U, false, 0U},
        Edge{22U, 50U, 50U, 5U, 0U, false, 0U}};
    fixture.edges[21U] = {Edge{23U, 50U, 50U, 0U, 0U, false, 0U}};
    fixture.edges[22U] = {Edge{24U, 24U, 24U, 0U, 0U, true, 0U}};
    auto program = Program(LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE, 1U, 2U);
    auto initial = State(20U, 20U, 20U, 0U, 0U);
    auto provider = Provider(&fixture);
    ResultHandle result;
    laplace_query_search_receipt receipt{};
    ASSERT_EQ(laplace_query_search_execute(
                  &program, &initial, 1U, &provider, &result.value, &receipt),
              LAPLACE_QUERY_SEARCH_OK);
    EXPECT_EQ(laplace_query_search_result_path_count(result.value), 1U);
    EXPECT_EQ(receipt.disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE);
}

TEST(QuerySearch, DoesNotPromoteKnownPathToOptimalAcrossIncompleteBoundary) {
    FixtureProvider fixture;
    fixture.edges[30U] = {Edge{31U, 31U, 31U, 1U, 0U, true, 0U}};
    auto program = Program(
        LAPLACE_QUERY_SEARCH_FLAG_ASTAR |
        LAPLACE_QUERY_SEARCH_FLAG_HEURISTIC_ADMISSIBLE |
        LAPLACE_QUERY_SEARCH_FLAG_HEURISTIC_CONSISTENT |
        LAPLACE_QUERY_SEARCH_FLAG_REQUEST_OPTIMAL);
    auto initial = State(30U, 30U, 30U, 0U, 1U);
    auto provider = Provider(&fixture);
    ResultHandle result;
    laplace_query_search_receipt receipt{};
    ASSERT_EQ(laplace_query_search_execute(
                  &program, &initial, 1U, &provider, &result.value, &receipt),
              LAPLACE_QUERY_SEARCH_OK);
    EXPECT_EQ(receipt.disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_UPPER_BOUND);
    EXPECT_EQ(receipt.flags & LAPLACE_QUERY_SEARCH_RECEIPT_OPTIMAL_CERTIFIED, 0U);
}

TEST(QuerySearch, ExecutesRequestedPathMultiplicity) {
    FixtureProvider fixture;
    fixture.edges[40U] = {
        Edge{41U, 41U, 41U, 1U, 0U, true, 0U},
        Edge{42U, 42U, 42U, 2U, 0U, true, 0U}};
    auto program = Program(LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE, 2U);
    auto initial = State(40U, 40U, 40U, 0U, 0U);
    auto provider = Provider(&fixture);
    ResultHandle result;
    laplace_query_search_receipt receipt{};
    ASSERT_EQ(laplace_query_search_execute(
                  &program, &initial, 1U, &provider, &result.value, &receipt),
              LAPLACE_QUERY_SEARCH_OK);
    EXPECT_EQ(laplace_query_search_result_path_count(result.value), 2U);
    EXPECT_EQ(receipt.path_count, 2U);
    EXPECT_EQ(receipt.disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE);
}

TEST(QuerySearch, CollapsesEvidenceRootsAndBatchesFrontier) {
    FixtureProvider fixture;
    fixture.edges[50U] = {
        Edge{51U, 51U, 51U, 1U, 0U, false, 70U},
        Edge{52U, 52U, 52U, 1U, 0U, false, 70U}};
    fixture.edges[51U] = {Edge{53U, 53U, 53U, 1U, 0U, true, 71U}};
    fixture.edges[52U] = {Edge{54U, 54U, 54U, 2U, 0U, true, 70U}};
    auto program = Program(
        LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE |
        LAPLACE_QUERY_SEARCH_FLAG_REQUIRE_SET_ORIENTED);
    auto initial = State(50U, 50U, 50U, 0U, 0U);
    auto provider = Provider(&fixture);
    ResultHandle result;
    laplace_query_search_receipt receipt{};
    ASSERT_EQ(laplace_query_search_execute(
                  &program, &initial, 1U, &provider, &result.value, &receipt),
              LAPLACE_QUERY_SEARCH_OK);
    ASSERT_GE(fixture.calls.size(), 2U);
    EXPECT_EQ(fixture.calls[1U].size(), 2U);
    laplace_query_search_path_summary path{};
    ASSERT_EQ(laplace_query_search_result_path(result.value, 0U, &path),
              LAPLACE_QUERY_SEARCH_OK);
    EXPECT_EQ(path.independent_evidence_root_count, 2U);
}

TEST(QuerySearch, PropagatesTypedWhyNotDisposition) {
    FixtureProvider fixture;
    fixture.limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_DENIED;
    auto program = Program(LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE);
    auto initial = State(60U, 60U, 60U, 0U, 0U);
    auto provider = Provider(&fixture);
    ResultHandle result;
    laplace_query_search_receipt receipt{};
    ASSERT_EQ(laplace_query_search_execute(
                  &program, &initial, 1U, &provider, &result.value, &receipt),
              LAPLACE_QUERY_SEARCH_OK);
    EXPECT_EQ(receipt.disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_DENIED);
    EXPECT_EQ(receipt.path_count, 0U);
}

}  // namespace
