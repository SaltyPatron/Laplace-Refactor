#include "laplace/chess_line.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include "context_fixture.h"

namespace {
struct PlanOwner {
    laplace_chess_line_plan* value{};
    ~PlanOwner() { laplace_chess_line_plan_destroy(&value); }
};
struct SetOwner {
    laplace_composition_working_set* value{};
    ~SetOwner() { laplace_composition_working_set_destroy(&value); }
};
struct Trace {
    std::vector<laplace_chess_position> positions;
    std::vector<laplace_chess_move> moves;
    std::vector<laplace_chess_line_range> lines;

    void Add(const laplace_chess_position& start, const std::vector<laplace_chess_move>& sequence) {
        lines.push_back({positions.size(), moves.size(), sequence.size()});
        positions.push_back(start);
        for (const auto& move : sequence) {
            auto next = positions.back();
            next.pieces[move.to_square] = next.pieces[move.from_square];
            next.pieces[move.from_square] = 0U;
            next.side_to_move = static_cast<std::uint8_t>(3U - next.side_to_move);
            next.legal_en_passant_square = LAPLACE_CHESS_NO_SQUARE;
            moves.push_back(move);
            positions.push_back(next);
        }
    }
    laplace_chess_line_plan_input Input() const {
        laplace_chess_line_plan_input input{};
        input.positions = positions.data(); input.position_count = positions.size();
        input.moves = moves.data(); input.move_count = moves.size();
        input.lines = lines.data(); input.line_count = lines.size();
        input.maximum_positions = 1024U; input.maximum_moves = 1024U;
        input.maximum_lines = 32U; input.maximum_requests = 10000U;
        input.maximum_operands = 100000U; input.version = LAPLACE_CHESS_LINE_VERSION;
        input.geometry_epoch = laplace_test_context(0U).epochs[3];
        return input;
    }
};
laplace_chess_position Initial() {
    laplace_chess_position p{};
    const std::array<std::uint8_t, 8> pieces{4U,2U,3U,5U,6U,3U,2U,4U};
    for (std::size_t file = 0U; file < 8U; ++file) {
        p.pieces[file] = pieces[file]; p.pieces[8U + file] = 1U;
        p.pieces[48U + file] = 9U; p.pieces[56U + file] = static_cast<std::uint8_t>(pieces[file] + 8U);
    }
    p.side_to_move = LAPLACE_CHESS_WHITE;
    p.castling_rights = 15U;
    p.legal_en_passant_square = LAPLACE_CHESS_NO_SQUARE;
    return p;
}
std::vector<laplace_chess_move> KingKnightFirst() {
    return {{6U,21U,0U,0U},{62U,45U,0U,0U},{1U,18U,0U,0U},{57U,42U,0U,0U}};
}
std::vector<laplace_chess_move> QueenKnightFirst() {
    return {{1U,18U,0U,0U},{57U,42U,0U,0U},{6U,21U,0U,0U},{62U,45U,0U,0U}};
}
bool Equal(const laplace_id128& a, const laplace_id128& b) {
    return laplace_identity_equal(&a, &b) != 0;
}
laplace_chess_line_plan_view View(const PlanOwner& plan) {
    laplace_chess_line_plan_view view{};
    EXPECT_EQ(laplace_chess_line_plan_view_get(plan.value, &view), LAPLACE_CHESS_LINE_OK);
    return view;
}

/* This is a bounded native unit geometry, not an activated Unicode/PG fixture.
 * Every atomic physicality is produced by the real native owner. Durable
 * acceptance must resolve the active admitted floor via the PG atom provider.
 */
void Execute(const laplace_chess_line_plan_view& view, SetOwner& set,
             const std::uint8_t source_variant = 0U) {
    auto context = laplace_test_context(0U);
    context.resource_grant.memory_bytes = UINT64_C(128) * 1024U * 1024U;
    context.epochs[3] = view.geometry_epoch;
    std::vector<laplace_composition_known_entity> atoms;
    for (std::uint64_t index = 0U; index < view.atom_count; ++index) {
        laplace_composition_known_entity atom{};
        atom.atom = view.atom_positions[index]; atom.has_atom = 1U;
        ASSERT_EQ(laplace_identity_codepoint_witness(atom.atom, &atom.entity_id,
            &atom.identity_witness), LAPLACE_IDENTITY_OK);
        atom.centroid.component[atom.atom % 4U] = 1.0;
        laplace_persistence_physicality_record physicality{};
        ASSERT_EQ(laplace_persistence_atomic_point_physicality(&atom.entity_id, 1U,
            &context.epochs[0], &context.epochs[3], &atom.centroid, &physicality),
            LAPLACE_PERSISTENCE_OK);
        atom.physicality_id = physicality.physicality_id;
        atoms.push_back(atom);
    }
    auto source = context.epochs[0]; source.bytes[0] ^= source_variant;
    const laplace_composition_working_set_input input{&context, &source,
        &view.recipe_fingerprint, atoms.data(), atoms.size(),
        view.operands, view.operand_count, view.requests, view.request_count, 65536U, 0U};
    ASSERT_EQ(laplace_composition_working_set_create(&input, &set.value), LAPLACE_COMPOSITION_OK);
}
laplace_composition_result Result(const SetOwner& set, const std::uint64_t index) {
    std::size_t count{};
    const auto* values = laplace_composition_working_set_results(set.value, &count);
    EXPECT_NE(values, nullptr); EXPECT_LT(index, count);
    return values != nullptr && index < count ? values[index] : laplace_composition_result{};
}
laplace_id128 StringIdentity(const std::string_view text) {
    std::vector<laplace_id128> children;
    for (const char raw : text) {
        const auto value = static_cast<unsigned char>(raw);
        laplace_id128 atom{};
        EXPECT_EQ(laplace_identity_codepoint(value, &atom), LAPLACE_IDENTITY_OK);
        children.push_back(atom);
    }
    laplace_id128 result{};
    EXPECT_EQ(laplace_identity_composite(children.data(), children.size(), &result), LAPLACE_IDENTITY_OK);
    return result;
}

TEST(ChessLine, BatchReusesExactContentWithoutManufacturingOccurrences) {
    Trace single; single.Add(Initial(), KingKnightFirst());
    auto input = single.Input(); PlanOwner one;
    ASSERT_EQ(laplace_chess_line_plan_create(&input, &one.value), LAPLACE_CHESS_LINE_OK);
    Trace doubled = single; doubled.Add(Initial(), KingKnightFirst());
    input = doubled.Input(); PlanOwner two;
    ASSERT_EQ(laplace_chess_line_plan_create(&input, &two.value), LAPLACE_CHESS_LINE_OK);
    const auto a = View(one), b = View(two);
    EXPECT_EQ(a.request_count, b.request_count);
    EXPECT_EQ(a.operand_count, b.operand_count);
    ASSERT_EQ(b.line_count, 2U);
    EXPECT_EQ(b.line_result_indexes[0], b.line_result_indexes[1]);
    for (std::uint64_t index = 0U; index < a.position_count; ++index)
        EXPECT_EQ(b.position_result_indexes[index], b.position_result_indexes[index + a.position_count]);
    for (std::uint64_t index = 0U; index < b.request_count; ++index)
        EXPECT_EQ(b.requests[index].flags, 0U);
    SetOwner executed; Execute(b, executed); ASSERT_NE(executed.value, nullptr);
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(executed.value, &summary), LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.occurrence_count, 0U);
    // Logical occurrences count ordered children within physicalities, not
    // independent PLAYING/evidence records. Repeating existing content keeps
    // that structure unchanged while occurrence_count above remains zero.
    SetOwner once; Execute(a, once); ASSERT_NE(once.value, nullptr);
    laplace_composition_working_set_summary single_summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(once.value, &single_summary),
        LAPLACE_COMPOSITION_OK);
    EXPECT_GT(summary.logical_occurrence_count, 0U);
    EXPECT_EQ(summary.logical_occurrence_count, single_summary.logical_occurrence_count);
    EXPECT_EQ(summary.unique_entity_count, single_summary.unique_entity_count);
    EXPECT_EQ(summary.unique_physicality_count, single_summary.unique_physicality_count);
    EXPECT_EQ(summary.trajectory_vertex_count, single_summary.trajectory_vertex_count);
    EXPECT_EQ(summary.semantic_calculation_count, b.request_count);
}

TEST(ChessLine, TranspositionSharesEndpointAndActionsButPreservesExactPaths) {
    Trace trace; trace.Add(Initial(), KingKnightFirst()); trace.Add(Initial(), QueenKnightFirst());
    auto input = trace.Input(); PlanOwner plan;
    ASSERT_EQ(laplace_chess_line_plan_create(&input, &plan.value), LAPLACE_CHESS_LINE_OK);
    const auto view = View(plan);
    SetOwner set; Execute(view, set); ASSERT_NE(set.value, nullptr);
    EXPECT_TRUE(Equal(Result(set, view.position_result_indexes[0]).entity_id,
                      Result(set, view.position_result_indexes[5]).entity_id));
    EXPECT_TRUE(Equal(Result(set, view.position_result_indexes[4]).entity_id,
                      Result(set, view.position_result_indexes[9]).entity_id));
    EXPECT_TRUE(Equal(Result(set, view.move_result_indexes[0]).entity_id,
                      Result(set, view.move_result_indexes[6]).entity_id));
    EXPECT_FALSE(Equal(Result(set, view.line_result_indexes[0]).entity_id,
                       Result(set, view.line_result_indexes[1]).entity_id));
    Trace reversed; reversed.Add(Initial(), QueenKnightFirst()); reversed.Add(Initial(), KingKnightFirst());
    input = reversed.Input(); PlanOwner other;
    ASSERT_EQ(laplace_chess_line_plan_create(&input, &other.value), LAPLACE_CHESS_LINE_OK);
    const auto other_view = View(other); SetOwner other_set; Execute(other_view, other_set);
    ASSERT_NE(other_set.value, nullptr);
    EXPECT_TRUE(Equal(Result(set, view.line_result_indexes[0]).entity_id,
                      Result(other_set, other_view.line_result_indexes[1]).entity_id));
    EXPECT_TRUE(Equal(Result(set, view.line_result_indexes[1]).entity_id,
                      Result(other_set, other_view.line_result_indexes[0]).entity_id));
}

TEST(ChessLine, CommonComposerPreservesExactAlternatingTrajectoryAndKnownChildReplay) {
    Trace trace; trace.Add(Initial(), KingKnightFirst());
    auto input = trace.Input(); PlanOwner plan;
    ASSERT_EQ(laplace_chess_line_plan_create(&input, &plan.value), LAPLACE_CHESS_LINE_OK);
    const auto view = View(plan); SetOwner set; Execute(view, set); ASSERT_NE(set.value, nullptr);
    const auto root_index = view.line_result_indexes[0];
    const auto root = Result(set, root_index);
    const auto& request = view.requests[root_index];
    std::vector<laplace_id128> expected{StringIdentity("Chess_Line")};
    for (std::uint64_t index = 0U; index < view.position_count; ++index) {
        expected.push_back(Result(set, view.position_result_indexes[index]).entity_id);
        if (index < view.move_count)
            expected.push_back(Result(set, view.move_result_indexes[index]).entity_id);
    }
    laplace_id128 expected_id{}; laplace_digest256 expected_witness{};
    ASSERT_EQ(laplace_identity_composite_witness(expected.data(), expected.size(), nullptr,
        &expected_id, &expected_witness), LAPLACE_IDENTITY_OK);
    EXPECT_TRUE(Equal(root.entity_id, expected_id));
    EXPECT_EQ(std::memcmp(root.identity_witness.bytes, expected_witness.bytes, 32U), 0);
    EXPECT_EQ(request.operand_count, expected.size());

    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(set.value, &summary), LAPLACE_COMPOSITION_OK);
    bool found = false;
    for (std::uint64_t index = 0U; index < summary.unique_physicality_count; ++index) {
        laplace_persistence_physicality_record physicality{};
        ASSERT_EQ(laplace_composition_working_set_physicality_candidate_get(set.value, index, &physicality),
            LAPLACE_COMPOSITION_OK);
        if (std::memcmp(physicality.physicality_id.bytes, root.physicality_id.bytes, 32U) != 0) continue;
        const laplace_trajectory_carrier* carriers{}; std::size_t count{};
        ASSERT_EQ(laplace_composition_working_set_trajectory_candidate_view_get(set.value, index,
            &carriers, &count), LAPLACE_COMPOSITION_OK);
        std::uint64_t ordinal = 1U;
        for (std::size_t carrier = 0U; carrier < count; ++carrier) {
            laplace_composition_occurrence occurrence{};
            ASSERT_EQ(laplace_trajectory_composition_decode_one(&carriers[carrier], ordinal, &occurrence),
                LAPLACE_TRAJECTORY_OK);
            for (std::uint64_t run = 0U; run < occurrence.run_length; ++run) {
                ASSERT_LE(ordinal, expected.size());
                EXPECT_TRUE(Equal(occurrence.entity_id, expected[ordinal - 1U])); ++ordinal;
            }
        }
        EXPECT_EQ(ordinal, expected.size() + 1U); found = true;
    }
    EXPECT_TRUE(found);

    std::vector<laplace_composition_known_entity> known;
    std::vector<laplace_composition_operand> operands;
    for (std::uint64_t index = 0U; index < request.operand_count; ++index) {
        const auto& operand = view.operands[request.first_operand + index];
        ASSERT_EQ(operand.reference_kind, LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT);
        const auto child = Result(set, operand.reference_index);
        known.push_back({child.entity_id, child.identity_witness, child.physicality_id,
            child.centroid, 0U, child.tier_floor, 0U, 0U});
        operands.push_back({index, operand.multiplicity, operand.relationship_metadata,
            LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U});
    }
    auto replay_request = request; replay_request.first_operand = 0U;
    auto context = laplace_test_context(0U);
    context.resource_grant.memory_bytes = UINT64_C(128) * 1024U * 1024U;
    const laplace_composition_working_set_input replay{&context, &context.epochs[0],
        &view.recipe_fingerprint, known.data(), known.size(), operands.data(), operands.size(),
        &replay_request, 1U, 65536U, 0U};
    SetOwner reconstructed;
    ASSERT_EQ(laplace_composition_working_set_create(&replay, &reconstructed.value), LAPLACE_COMPOSITION_OK);
    const auto rebuilt = Result(reconstructed, 0U);
    EXPECT_TRUE(Equal(root.entity_id, rebuilt.entity_id));
    EXPECT_EQ(std::memcmp(root.identity_witness.bytes, rebuilt.identity_witness.bytes, 32U), 0);
    EXPECT_EQ(std::memcmp(root.physicality_id.bytes, rebuilt.physicality_id.bytes, 32U), 0);
}

TEST(ChessLine, SourceAndGeometryDoNotSaltContentAndNumeralsUseCommonIdentity) {
    Trace trace; trace.Add(Initial(), KingKnightFirst());
    auto input = trace.Input(); PlanOwner first, other;
    ASSERT_EQ(laplace_chess_line_plan_create(&input, &first.value), LAPLACE_CHESS_LINE_OK);
    input.geometry_epoch.bytes[0] ^= 1U;
    ASSERT_EQ(laplace_chess_line_plan_create(&input, &other.value), LAPLACE_CHESS_LINE_OK);
    const auto a = View(first), b = View(other);
    SetOwner original, source_changed, geometry_changed;
    Execute(a, original); Execute(a, source_changed, 1U); Execute(b, geometry_changed);
    ASSERT_NE(original.value, nullptr); ASSERT_NE(source_changed.value, nullptr); ASSERT_NE(geometry_changed.value, nullptr);
    const auto root = Result(original, a.line_result_indexes[0]);
    const auto from_source = Result(source_changed, a.line_result_indexes[0]);
    const auto from_geometry = Result(geometry_changed, b.line_result_indexes[0]);
    EXPECT_TRUE(Equal(root.entity_id, from_source.entity_id));
    EXPECT_EQ(std::memcmp(root.physicality_id.bytes, from_source.physicality_id.bytes, 32U), 0);
    EXPECT_TRUE(Equal(root.entity_id, from_geometry.entity_id));
    EXPECT_NE(std::memcmp(root.physicality_id.bytes, from_geometry.physicality_id.bytes, 32U), 0);
    const std::array<laplace_id128, 2> numeral_children{StringIdentity("number"), StringIdentity("6")};
    laplace_id128 numeral{};
    ASSERT_EQ(laplace_identity_composite(numeral_children.data(), numeral_children.size(), &numeral),
        LAPLACE_IDENTITY_OK);
    std::size_t count{};
    const auto* candidates = laplace_composition_working_set_entity_candidates(original.value, &count);
    ASSERT_NE(candidates, nullptr);
    EXPECT_TRUE(std::any_of(candidates, candidates + count,
        [&](const auto& candidate) { return Equal(candidate.entity.entity_id, numeral); }));
}

TEST(ChessLine, CastlingPromotionAndEnPassantPreserveExactNativeBoardDelta) {
    laplace_chess_position start{};
    start.pieces[4] = 6U; start.pieces[7] = 4U; start.pieces[60] = 14U;
    start.side_to_move = 1U; start.castling_rights = 1U; start.legal_en_passant_square = 64U;
    auto after = start; after.pieces[4] = 0U; after.pieces[7] = 0U;
    after.pieces[6] = 6U; after.pieces[5] = 4U; after.side_to_move = 2U; after.castling_rights = 0U;
    const std::array<laplace_chess_move, 3> actions{{
        {4U,6U,0U,LAPLACE_CHESS_MOVE_CASTLE_KING},
        {48U,56U,LAPLACE_CHESS_QUEEN,LAPLACE_CHESS_MOVE_ORDINARY},
        {36U,43U,0U,LAPLACE_CHESS_MOVE_EN_PASSANT}}};
    for (std::size_t variant = 0U; variant < actions.size(); ++variant) {
        if (variant != 0U) {
            start = {}; start.pieces[4] = 6U; start.pieces[60] = 14U;
            start.side_to_move = 1U; start.legal_en_passant_square = 64U;
            if (variant == 1U) start.pieces[48] = 1U;
            else { start.pieces[36] = 1U; start.pieces[35] = 9U; start.legal_en_passant_square = 43U; }
            after = start; after.side_to_move = 2U; after.legal_en_passant_square = 64U;
            if (variant == 1U) { after.pieces[48] = 0U; after.pieces[56] = 5U; }
            else { after.pieces[36] = 0U; after.pieces[35] = 0U; after.pieces[43] = 1U; }
        }
        Trace trace; trace.positions = {start, after}; trace.moves = {actions[variant]}; trace.lines = {{0U,0U,1U}};
        auto input = trace.Input(); PlanOwner valid;
        ASSERT_EQ(laplace_chess_line_plan_create(&input, &valid.value), LAPLACE_CHESS_LINE_OK) << variant;
        trace.positions[1].pieces[16] = 2U;
        input = trace.Input(); PlanOwner rejected;
        EXPECT_EQ(laplace_chess_line_plan_create(&input, &rejected.value), LAPLACE_CHESS_LINE_TRANSITION_INVALID);
        EXPECT_EQ(rejected.value, nullptr);
    }
}

TEST(ChessLine, LimitsAndMalformedInputsPublishNoPartialPlan) {
    Trace trace; trace.Add(Initial(), KingKnightFirst());
    const auto original = trace.Input();
    for (unsigned variant = 0U; variant < 5U; ++variant) {
        auto input = original; PlanOwner rejected;
        if (variant == 0U) input.maximum_positions = 1U;
        if (variant == 1U) input.maximum_moves = 1U;
        if (variant == 2U) input.maximum_lines = 0U;
        if (variant == 3U) input.maximum_requests = 1U;
        if (variant == 4U) input.maximum_operands = 1U;
        EXPECT_EQ(laplace_chess_line_plan_create(&input, &rejected.value), LAPLACE_CHESS_LINE_LIMIT);
        EXPECT_EQ(rejected.value, nullptr);
    }
    auto input = original; input.position_count = std::numeric_limits<std::uint64_t>::max();
    input.maximum_positions = input.position_count;
    PlanOwner excessive;
    EXPECT_EQ(laplace_chess_line_plan_create(&input, &excessive.value), LAPLACE_CHESS_LINE_LIMIT);
    EXPECT_EQ(excessive.value, nullptr);
    trace.positions[0].reserved[0] = 1U; input = trace.Input(); PlanOwner reserved;
    EXPECT_EQ(laplace_chess_line_plan_create(&input, &reserved.value), LAPLACE_CHESS_LINE_POSITION_INVALID);
    trace.positions[0].reserved[0] = 0U;
    trace.lines[0].first_move = 1U; input = trace.Input(); PlanOwner range;
    EXPECT_EQ(laplace_chess_line_plan_create(&input, &range.value), LAPLACE_CHESS_LINE_INVALID_ARGUMENT);
    EXPECT_EQ(range.value, nullptr);
}
}  // namespace
