#include "laplace/chess_line.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "blake3.h"
#include "canonical_composition_plan.hpp"

namespace {
constexpr std::uint64_t Invalid = std::numeric_limits<std::uint64_t>::max();
constexpr std::string_view RecipeLaw{
    "laplace-chess-line-plan-v1;orthodox-standard;"
    "P=Chess_Position(board[a1..h8],side-to-move,castling-rights[WK,WQ,BK,BQ],legal-en-passant);"
    "piece=Chess_Piece(side,kind);square=Chess_Square(file[0..7],rank[0..7]);"
    "M=Chess_Move(piece,from,to,promotion,form);ordinary-capture-context-in-adjacent-P;"
    "LINE=Chess_Line,P0,M0,P1,...,Pn;exact-order;shared-number-tag;"
    "EP-only-if-qualified-legal-capture;no-counters-history-provider-source-display;"
    "roles=tag1/value2<<6;occurrences=0;single-common-composition-owner"};
enum class Role : std::uint64_t { Tag = 1U, Identifier = 2U, Value = 2U };
using PositionKey = std::array<std::uint8_t, 67>;
using MoveKey = std::array<std::uint8_t, 5>;

unsigned Side(const std::uint8_t piece) {
    return piece == 0U ? 0U : (piece < 8U ? 1U : 2U);
}
unsigned Kind(const std::uint8_t piece) { return piece & 7U; }
std::uint8_t Piece(const unsigned side, const unsigned kind) {
    return static_cast<std::uint8_t>(kind + (side == LAPLACE_CHESS_BLACK ? 8U : 0U));
}
bool ValidPosition(const laplace_chess_position& p) {
    if ((p.side_to_move != LAPLACE_CHESS_WHITE && p.side_to_move != LAPLACE_CHESS_BLACK) ||
        p.castling_rights > 15U || p.legal_en_passant_square > LAPLACE_CHESS_NO_SQUARE ||
        std::any_of(std::begin(p.reserved), std::end(p.reserved),
            [](const std::uint8_t value) { return value != 0U; })) return false;
    std::array<unsigned, 3> kings{};
    for (std::size_t square = 0U; square < 64U; ++square) {
        const auto piece = p.pieces[square];
        if (piece == 0U) continue;
        if (piece == 7U || piece == 8U || piece > 14U) return false;
        if (Kind(piece) == LAPLACE_CHESS_KING) ++kings[Side(piece)];
        if (Kind(piece) == LAPLACE_CHESS_PAWN && (square < 8U || square >= 56U))
            return false;
    }
    if (kings[1] != 1U || kings[2] != 1U) return false;
    for (unsigned side = 1U; side <= 2U; ++side) {
        const unsigned base = side == 1U ? 0U : 56U;
        const unsigned shift = side == 1U ? 0U : 2U;
        const unsigned rights = (p.castling_rights >> shift) & 3U;
        if (rights != 0U && p.pieces[base + 4U] != Piece(side, LAPLACE_CHESS_KING))
            return false;
        if ((rights & 1U) != 0U && p.pieces[base + 7U] != Piece(side, LAPLACE_CHESS_ROOK))
            return false;
        if ((rights & 2U) != 0U && p.pieces[base] != Piece(side, LAPLACE_CHESS_ROOK))
            return false;
    }
    if (p.legal_en_passant_square != LAPLACE_CHESS_NO_SQUARE) {
        const unsigned target = p.legal_en_passant_square;
        const unsigned rank = p.side_to_move == LAPLACE_CHESS_WHITE ? 5U : 2U;
        const unsigned pawn_rank = p.side_to_move == LAPLACE_CHESS_WHITE ? 4U : 3U;
        const unsigned file = target % 8U;
        if (target / 8U != rank || p.pieces[target] != 0U ||
            p.pieces[pawn_rank * 8U + file] !=
                Piece(3U - p.side_to_move, LAPLACE_CHESS_PAWN)) return false;
        const auto own_pawn = Piece(p.side_to_move, LAPLACE_CHESS_PAWN);
        if (!((file > 0U && p.pieces[pawn_rank * 8U + file - 1U] == own_pawn) ||
              (file < 7U && p.pieces[pawn_rank * 8U + file + 1U] == own_pawn)))
            return false;
    }
    return true;
}

/* Exact transition transport checks, not an alternative legality engine.
 * Sliding paths, attacked squares, king safety, and legal EP normalization are
 * qualified by the native rules provider before this projection boundary.
 */
bool ValidTransition(const laplace_chess_position& before,
                     const laplace_chess_move& move,
                     const laplace_chess_position& after) {
    if (move.from_square >= 64U || move.to_square >= 64U ||
        move.from_square == move.to_square || move.form > LAPLACE_CHESS_MOVE_EN_PASSANT ||
        (move.promotion_kind != 0U &&
            (move.promotion_kind < LAPLACE_CHESS_KNIGHT ||
             move.promotion_kind > LAPLACE_CHESS_QUEEN)) ||
        after.side_to_move != 3U - before.side_to_move) return false;
    const auto moving = before.pieces[move.from_square];
    const auto captured = before.pieces[move.to_square];
    if (Side(moving) != before.side_to_move ||
        Side(captured) == before.side_to_move ||
        Kind(captured) == LAPLACE_CHESS_KING) return false;
    std::array<std::uint8_t, 64> expected{};
    std::copy(std::begin(before.pieces), std::end(before.pieces), expected.begin());
    expected[move.from_square] = 0U;
    expected[move.to_square] = moving;
    if (move.form == LAPLACE_CHESS_MOVE_CASTLE_KING ||
        move.form == LAPLACE_CHESS_MOVE_CASTLE_QUEEN) {
        const unsigned base = before.side_to_move == LAPLACE_CHESS_WHITE ? 0U : 56U;
        const bool king_side = move.form == LAPLACE_CHESS_MOVE_CASTLE_KING;
        const unsigned rook_from = base + (king_side ? 7U : 0U);
        const unsigned rook_to = base + (king_side ? 5U : 3U);
        const unsigned right = (king_side ? 1U : 2U) <<
            (before.side_to_move == LAPLACE_CHESS_WHITE ? 0U : 2U);
        if (Kind(moving) != LAPLACE_CHESS_KING || move.promotion_kind != 0U ||
            move.from_square != base + 4U || move.to_square != base + (king_side ? 6U : 2U) ||
            (before.castling_rights & right) == 0U ||
            before.pieces[rook_from] != Piece(before.side_to_move, LAPLACE_CHESS_ROOK))
            return false;
        for (unsigned square = base + (king_side ? 5U : 1U);
             square < base + (king_side ? 7U : 4U); ++square)
            if (before.pieces[square] != 0U) return false;
        expected[rook_from] = 0U;
        expected[rook_to] = Piece(before.side_to_move, LAPLACE_CHESS_ROOK);
    } else if (move.form == LAPLACE_CHESS_MOVE_EN_PASSANT) {
        const unsigned source_rank = before.side_to_move == LAPLACE_CHESS_WHITE ? 4U : 3U;
        const unsigned target_rank = before.side_to_move == LAPLACE_CHESS_WHITE ? 5U : 2U;
        const int file_delta = static_cast<int>(move.to_square % 8U) -
            static_cast<int>(move.from_square % 8U);
        const unsigned captured_square = source_rank * 8U + move.to_square % 8U;
        if (Kind(moving) != LAPLACE_CHESS_PAWN || move.promotion_kind != 0U ||
            before.legal_en_passant_square != move.to_square || captured != 0U ||
            move.from_square / 8U != source_rank || move.to_square / 8U != target_rank ||
            (file_delta != -1 && file_delta != 1) ||
            before.pieces[captured_square] != Piece(3U - before.side_to_move, LAPLACE_CHESS_PAWN))
            return false;
        expected[captured_square] = 0U;
    } else {
        const bool final_rank = move.to_square / 8U ==
            (before.side_to_move == LAPLACE_CHESS_WHITE ? 7U : 0U);
        if ((move.promotion_kind != 0U &&
                (Kind(moving) != LAPLACE_CHESS_PAWN || !final_rank)) ||
            (Kind(moving) == LAPLACE_CHESS_PAWN && final_rank && move.promotion_kind == 0U))
            return false;
        if (move.promotion_kind != 0U)
            expected[move.to_square] = Piece(before.side_to_move, move.promotion_kind);
    }
    if (!std::equal(expected.begin(), expected.end(), std::begin(after.pieces)))
        return false;
    unsigned rights = before.castling_rights;
    if (Kind(moving) == LAPLACE_CHESS_KING)
        rights &= ~(before.side_to_move == LAPLACE_CHESS_WHITE ? 3U : 12U);
    for (unsigned side = 1U; side <= 2U; ++side) {
        const unsigned base = side == 1U ? 0U : 56U;
        const unsigned shift = side == 1U ? 0U : 2U;
        for (unsigned wing = 0U; wing < 2U; ++wing) {
            const unsigned square = base + (wing == 0U ? 7U : 0U);
            if ((move.from_square == square && moving == Piece(side, LAPLACE_CHESS_ROOK)) ||
                (move.to_square == square && captured == Piece(side, LAPLACE_CHESS_ROOK)))
                rights &= ~((1U << wing) << shift);
        }
    }
    if (after.castling_rights != rights) return false;
    if (after.legal_en_passant_square != LAPLACE_CHESS_NO_SQUARE) {
        const unsigned initial_rank = before.side_to_move == LAPLACE_CHESS_WHITE ? 1U : 6U;
        const unsigned final_rank = before.side_to_move == LAPLACE_CHESS_WHITE ? 3U : 4U;
        if (move.form != LAPLACE_CHESS_MOVE_ORDINARY || Kind(moving) != LAPLACE_CHESS_PAWN ||
            move.from_square / 8U != initial_rank || move.to_square / 8U != final_rank ||
            move.from_square % 8U != move.to_square % 8U ||
            after.legal_en_passant_square != (move.from_square + move.to_square) / 2U)
            return false;
    }
    return true;
}

laplace_chess_line_status Validate(const laplace_chess_line_plan_input& input) {
    if (input.version != LAPLACE_CHESS_LINE_VERSION) return LAPLACE_CHESS_LINE_INVALID_VERSION;
    if (input.reserved != 0U || input.positions == nullptr || input.lines == nullptr ||
        input.position_count == 0U || input.line_count == 0U ||
        (input.move_count != 0U && input.moves == nullptr))
        return LAPLACE_CHESS_LINE_INVALID_ARGUMENT;
    if (input.position_count > input.maximum_positions || input.move_count > input.maximum_moves ||
        input.line_count > input.maximum_lines || input.maximum_requests == 0U ||
        input.maximum_operands < 2U ||
        input.position_count > SIZE_MAX / sizeof(laplace_chess_position) ||
        input.move_count > SIZE_MAX / sizeof(laplace_chess_move) ||
        input.line_count > SIZE_MAX / sizeof(laplace_chess_line_range))
        return LAPLACE_CHESS_LINE_LIMIT;
    std::uint64_t next_position = 0U, next_move = 0U;
    for (std::uint64_t index = 0U; index < input.line_count; ++index) {
        const auto& line = input.lines[index];
        if (line.first_position != next_position || line.first_move != next_move ||
            line.move_count > input.move_count - next_move ||
            next_position >= input.position_count ||
            line.move_count >= input.position_count - next_position)
            return LAPLACE_CHESS_LINE_INVALID_ARGUMENT;
        if (line.move_count > (input.maximum_operands - 2U) / 2U)
            return LAPLACE_CHESS_LINE_LIMIT;
        next_position += line.move_count + 1U;
        next_move += line.move_count;
    }
    if (next_position != input.position_count || next_move != input.move_count)
        return LAPLACE_CHESS_LINE_INVALID_ARGUMENT;
    for (std::uint64_t index = 0U; index < input.position_count; ++index)
        if (!ValidPosition(input.positions[index])) return LAPLACE_CHESS_LINE_POSITION_INVALID;
    for (std::uint64_t index = 0U; index < input.line_count; ++index) {
        const auto& line = input.lines[index];
        for (std::uint64_t ply = 0U; ply < line.move_count; ++ply)
            if (!ValidTransition(input.positions[line.first_position + ply],
                    input.moves[line.first_move + ply],
                    input.positions[line.first_position + ply + 1U]))
                return LAPLACE_CHESS_LINE_TRANSITION_INVALID;
    }
    return LAPLACE_CHESS_LINE_OK;
}
}  // namespace

struct laplace_chess_line_plan {
    laplace_chess_line_plan_view view{};
    laplace_digest256 no_occurrence{};
    std::vector<std::uint32_t> atoms;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::vector<std::uint64_t> positions, moves, lines;
};

namespace {
class Builder final : public laplace::detail::CanonicalCompositionPlanBuilder<Role> {
public:
    Builder(laplace_chess_line_plan& plan, const laplace_chess_line_plan_input& input)
        : CanonicalCompositionPlanBuilder(plan.atoms, plan.operands, plan.requests,
              plan.view.recipe_fingerprint, plan.view.geometry_epoch, plan.no_occurrence,
              LAPLACE_CHESS_LINE_VERSION, 0U, 6U, 0U,
              input.maximum_requests, input.maximum_operands), plan_(plan) {
        pieces_.fill(Invalid);
        squares_.fill(Invalid);
    }
    bool Build(const laplace_chess_line_plan_input& input) {
        number_tag_ = Text(laplace::detail::CanonicalNumberTag);
        for (std::uint64_t index = 0U; index < input.position_count; ++index) {
            const auto root = Position(input.positions[index]);
            if (root == Invalid) return false;
            plan_.positions.push_back(root);
        }
        for (std::uint64_t index = 0U; index < input.line_count; ++index) {
            const auto& line = input.lines[index];
            std::vector<std::uint64_t> sequence{Text("Chess_Line"),
                plan_.positions[line.first_position]};
            for (std::uint64_t ply = 0U; ply < line.move_count; ++ply) {
                const auto& move = input.moves[line.first_move + ply];
                const auto moving = input.positions[line.first_position + ply].pieces[move.from_square];
                const auto root = Move(move, moving);
                if (root == Invalid) return false;
                plan_.moves.push_back(root);
                sequence.push_back(root);
                sequence.push_back(plan_.positions[line.first_position + ply + 1U]);
            }
            const auto root = Record(sequence);
            if (root == Invalid) return false;
            plan_.lines.push_back(root);
        }
        return true;
    }
private:
    std::uint64_t Text(const std::string_view text) {
        const auto value = String(text);
        return value.has_value ? value.index : Invalid;
    }
    std::uint64_t Record(const std::vector<std::uint64_t>& children) {
        if (std::find(children.begin(), children.end(), Invalid) != children.end()) return Invalid;
        const auto prior = records_.find(children);
        if (prior != records_.end()) return prior->second;
        std::vector<std::pair<std::uint64_t, Role>> tagged;
        tagged.reserve(children.size());
        for (std::size_t index = 0U; index < children.size(); ++index)
            tagged.emplace_back(children[index], index == 0U ? Role::Tag : Role::Value);
        const auto result = Node(tagged);
        if (result != Invalid) records_.emplace(children, result);
        return result;
    }
    std::uint64_t Field(const std::string_view name, const std::uint64_t value) {
        return Record({Text(name), value});
    }
    std::uint64_t PieceNode(const std::uint8_t piece) {
        if (pieces_[piece] != Invalid) return pieces_[piece];
        static constexpr std::array<std::string_view, 7> names{
            "empty", "pawn", "knight", "bishop", "rook", "queen", "king"};
        pieces_[piece] = piece == 0U ? Text("empty") : Record({Text("Chess_Piece"),
            Text(Side(piece) == LAPLACE_CHESS_WHITE ? "white" : "black"), Text(names[Kind(piece)])});
        return pieces_[piece];
    }
    std::uint64_t Square(const std::uint8_t square) {
        if (square == LAPLACE_CHESS_NO_SQUARE) return Text("none");
        if (squares_[square] == Invalid)
            squares_[square] = Record({Text("Chess_Square"),
                Field("file", Number(square % 8U, number_tag_)),
                Field("rank", Number(square / 8U, number_tag_))});
        return squares_[square];
    }
    std::uint64_t Position(const laplace_chess_position& value) {
        PositionKey key{};
        std::copy(std::begin(value.pieces), std::end(value.pieces), key.begin());
        key[64] = value.side_to_move;
        key[65] = value.castling_rights;
        key[66] = value.legal_en_passant_square;
        const auto prior = positions_.find(key);
        if (prior != positions_.end()) return prior->second;
        std::vector<std::uint64_t> board{Text("board")};
        for (const auto piece : value.pieces) board.push_back(PieceNode(piece));
        std::vector<std::uint64_t> rights{Text("castling-rights")};
        for (unsigned bit = 0U; bit < 4U; ++bit)
            rights.push_back(Text((value.castling_rights & (1U << bit)) != 0U ? "true" : "false"));
        const auto root = Record({Text("Chess_Position"), Record(board),
            Field("side-to-move", Text(value.side_to_move == LAPLACE_CHESS_WHITE ? "white" : "black")),
            Record(rights), Field("legal-en-passant", Square(value.legal_en_passant_square))});
        if (root != Invalid) positions_.emplace(key, root);
        return root;
    }
    std::uint64_t Move(const laplace_chess_move& move, const std::uint8_t moving) {
        const MoveKey key{move.from_square, move.to_square, move.promotion_kind, move.form, moving};
        const auto prior = moves_.find(key);
        if (prior != moves_.end()) return prior->second;
        static constexpr std::array<std::string_view, 4> forms{
            "ordinary", "castle-king", "castle-queen", "en-passant"};
        static constexpr std::array<std::string_view, 6> promotions{
            "none", "invalid", "knight", "bishop", "rook", "queen"};
        const auto root = Record({Text("Chess_Move"), Field("piece", PieceNode(moving)),
            Field("from", Square(move.from_square)), Field("to", Square(move.to_square)),
            Field("promotion", Text(promotions[move.promotion_kind])),
            Field("form", Text(forms[move.form]))});
        if (root != Invalid) moves_.emplace(key, root);
        return root;
    }
    laplace_chess_line_plan& plan_;
    std::uint64_t number_tag_{Invalid};
    std::array<std::uint64_t, 15> pieces_{};
    std::array<std::uint64_t, 64> squares_{};
    std::map<PositionKey, std::uint64_t> positions_;
    std::map<MoveKey, std::uint64_t> moves_;
    std::map<std::vector<std::uint64_t>, std::uint64_t> records_;
};
}  // namespace

extern "C" laplace_chess_line_status laplace_chess_line_plan_create(
    const laplace_chess_line_plan_input* input, laplace_chess_line_plan** plan) {
    if (plan == nullptr) return LAPLACE_CHESS_LINE_INVALID_ARGUMENT;
    *plan = nullptr;
    if (input == nullptr) return LAPLACE_CHESS_LINE_INVALID_ARGUMENT;
    const auto status = Validate(*input);
    if (status != LAPLACE_CHESS_LINE_OK) return status;
    try {
        auto created = std::make_unique<laplace_chess_line_plan>();
        created->view.version = LAPLACE_CHESS_LINE_VERSION;
        created->view.geometry_epoch = input->geometry_epoch;
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, RecipeLaw.data(), RecipeLaw.size());
        blake3_hasher_finalize(&hasher, created->view.recipe_fingerprint.bytes,
            sizeof(created->view.recipe_fingerprint.bytes));
        Builder builder(*created, *input);
        if (!builder.Build(*input)) return LAPLACE_CHESS_LINE_LIMIT;
        *plan = created.release();
        return LAPLACE_CHESS_LINE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_CHESS_LINE_MEMORY_FAILURE;
    } catch (...) {
        return LAPLACE_CHESS_LINE_LIMIT;
    }
}

extern "C" laplace_chess_line_status laplace_chess_line_plan_view_get(
    const laplace_chess_line_plan* plan, laplace_chess_line_plan_view* view) {
    if (view == nullptr) return LAPLACE_CHESS_LINE_INVALID_ARGUMENT;
    *view = {};
    if (plan == nullptr) return LAPLACE_CHESS_LINE_INVALID_ARGUMENT;
    *view = plan->view;
    view->atom_positions = plan->atoms.data(); view->atom_count = plan->atoms.size();
    view->operands = plan->operands.data(); view->operand_count = plan->operands.size();
    view->requests = plan->requests.data(); view->request_count = plan->requests.size();
    view->position_result_indexes = plan->positions.data(); view->position_count = plan->positions.size();
    view->move_result_indexes = plan->moves.data(); view->move_count = plan->moves.size();
    view->line_result_indexes = plan->lines.data(); view->line_count = plan->lines.size();
    return LAPLACE_CHESS_LINE_OK;
}

extern "C" void laplace_chess_line_plan_destroy(laplace_chess_line_plan** plan) {
    if (plan != nullptr) { delete *plan; *plan = nullptr; }
}
