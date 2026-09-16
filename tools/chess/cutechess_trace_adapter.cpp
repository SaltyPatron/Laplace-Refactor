#include "cutechess_trace_adapter.hpp"
#include "cutechess_standard_board.hpp"
#include "boardtransition.h"
#include <QString>
#include <QStringList>
#include <algorithm>
#include <climits>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace laplace::chess_provider {
namespace {
struct Rejected { TraceStatus status; };
void Require(bool condition, TraceStatus status) {
    if (!condition) throw Rejected{status};
}
std::uint8_t SquareCode(const Chess::Square& square) {
    Require(square.isValid() && square.file() < 8 && square.rank() < 8,
            TraceStatus::ProviderDefect);
    return static_cast<std::uint8_t>(square.rank() * 8 + square.file());
}
std::uint8_t PieceKind(int kind) {
    switch (kind) {
    case Chess::WesternBoard::Pawn: return LAPLACE_CHESS_PAWN;
    case Chess::WesternBoard::Knight: return LAPLACE_CHESS_KNIGHT;
    case Chess::WesternBoard::Bishop: return LAPLACE_CHESS_BISHOP;
    case Chess::WesternBoard::Rook: return LAPLACE_CHESS_ROOK;
    case Chess::WesternBoard::Queen: return LAPLACE_CHESS_QUEEN;
    case Chess::WesternBoard::King: return LAPLACE_CHESS_KING;
    default: throw Rejected{TraceStatus::ProviderDefect};
    }
}
std::uint8_t PieceCode(Chess::Piece piece) {
    if (piece.isEmpty()) return LAPLACE_CHESS_EMPTY;
    Require(!piece.side().isNull(), TraceStatus::ProviderDefect);
    return static_cast<std::uint8_t>(PieceKind(piece.type()) +
        (piece.side() == Chess::Side::Black ? LAPLACE_CHESS_BLACK_PIECE_OFFSET : 0));
}
bool Ascii(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](char value_byte) {
        const auto byte = static_cast<unsigned char>(value_byte);
        return byte >= 0x20U && byte < 0x7fU;
    });
}
QString Text(std::string_view value) {
    Require(value.size() <= 256U && Ascii(value), TraceStatus::InvalidArgument);
    return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size()));
}
void Setup(StandardPosition& board, const TraceInput& input) {
    board.initialize();
    const auto fen = input.initial_fen.empty() ? board.defaultFenString() : Text(input.initial_fen);
    // Board owns FEN parsing. These envelope checks preserve a declared EP field
    // even when Board discards it because no adjacent pawn exists, and prevent
    // its signed fullmove/halfmove counters from overflowing during the trace.
    const auto fields = fen.split(' ', Qt::SkipEmptyParts);
    Require(fields.size() == 6, TraceStatus::UnsupportedSetup);
    bool half_ok = false, full_ok = false;
    const auto half = fields[4].toULongLong(&half_ok);
    const auto full = fields[5].toULongLong(&full_ok);
    const auto budget = static_cast<qulonglong>(input.ply_count);
    Require(half_ok && full_ok && full != 0U &&
        half <= static_cast<qulonglong>(INT_MAX) - budget &&
        full <= (static_cast<qulonglong>(INT_MAX) - budget) / 2U,
        TraceStatus::InvalidSetup);
    for (const auto symbol : fields[2])
        Require(symbol == 'K' || symbol == 'Q' || symbol == 'k' || symbol == 'q' ||
                (symbol == '-' && fields[2].size() == 1), TraceStatus::UnsupportedSetup);
    Require(board.setFenString(fen), TraceStatus::InvalidSetup);
    if (fields[3] != "-") {
        Require(fields[3].size() == 2 && fields[3][0] >= 'a' && fields[3][0] <= 'h' &&
                (fields[3][1] == '3' || fields[3][1] == '6'), TraceStatus::InvalidSetup);
        const Chess::Square declared(fields[3][0].unicode() - 'a', fields[3][1].unicode() - '1');
        Require(board.consistentEnPassantSquare(declared), TraceStatus::InvalidSetup);
    }
    for (const auto side : {Chess::Side(Chess::Side::White), Chess::Side(Chess::Side::Black)}) {
        const int rank = side == Chess::Side::White ? 0 : 7;
        if (board.castling(side, true) || board.castling(side, false))
            Require(board.pieceAt(Chess::Square(4, rank)) == Chess::Piece(side, Chess::WesternBoard::King),
                    TraceStatus::UnsupportedSetup);
        for (const bool king_side : {false, true})
            if (board.castling(side, king_side))
                Require(board.pieceAt(Chess::Square(king_side ? 7 : 0, rank)) ==
                        Chess::Piece(side, Chess::WesternBoard::Rook), TraceStatus::UnsupportedSetup);
    }
    for (int file = 0; file < 8; ++file)
        Require(board.pieceAt(Chess::Square(file, 0)).type() != Chess::WesternBoard::Pawn &&
                board.pieceAt(Chess::Square(file, 7)).type() != Chess::WesternBoard::Pawn,
                TraceStatus::InvalidSetup);
}
void Position(StandardPosition& board, TraceBatch& batch) {
    laplace_chess_position position{};
    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            position.pieces[rank * 8 + file] = PieceCode(board.pieceAt(Chess::Square(file, rank)));
    position.side_to_move = board.sideToMove() == Chess::Side::White ? LAPLACE_CHESS_WHITE : LAPLACE_CHESS_BLACK;
    position.castling_rights = static_cast<std::uint8_t>(
        (board.castling(Chess::Side::White, true) ? 1U : 0U) |
        (board.castling(Chess::Side::White, false) ? 2U : 0U) |
        (board.castling(Chess::Side::Black, true) ? 4U : 0U) |
        (board.castling(Chess::Side::Black, false) ? 8U : 0U));
    const auto legal_ep = board.legalEnPassantSquare();
    position.legal_en_passant_square = legal_ep.isValid() ? SquareCode(legal_ep) :
        static_cast<std::uint8_t>(LAPLACE_CHESS_NO_SQUARE);
    batch.positions.push_back(position);
    batch.position_observations.push_back({
        board.fenString().toStdString(),
        board.hasProviderEnPassant() ? SquareCode(board.providerEnPassantSquare()) :
            static_cast<std::uint8_t>(LAPLACE_CHESS_NO_SQUARE),
        board.reversibleMoveCount(), board.plyCount(), board.repeatCount()});
}
}  // namespace

TraceFailure BuildTraceBatch(const TraceInput* input, std::size_t count,
                             const TraceLimits& limits, TraceBatch& output) {
    output = {};
    TraceFailure failure{};
    try {
        Require(input != nullptr && count != 0U, TraceStatus::InvalidArgument);
        Require(count <= limits.maximum_lines && limits.maximum_lines <= 65536U &&
            limits.maximum_plies <= 1048576U && limits.maximum_input_bytes <= 67108864U,
            TraceStatus::Limit);
        std::size_t plies = 0U, bytes = 0U;
        for (std::size_t index = 0U; index < count; ++index) {
            failure.line = index;
            const auto& trace = input[index];
            Require(trace.notation == NotationPolicy::StrictSan ||
                    trace.notation == NotationPolicy::ProviderLegal, TraceStatus::InvalidArgument);
            Require(trace.ply_count == 0U || trace.spellings != nullptr, TraceStatus::InvalidArgument);
            Require(trace.ply_count <= limits.maximum_plies - plies, TraceStatus::Limit);
            plies += trace.ply_count;
            Require(trace.initial_fen.size() <= limits.maximum_input_bytes - bytes, TraceStatus::Limit);
            bytes += trace.initial_fen.size();
            for (std::size_t ply = 0U; ply < trace.ply_count; ++ply) {
                failure.ply = ply;
                Require(!trace.spellings[ply].empty() && trace.spellings[ply].size() <= 32U,
                        TraceStatus::InvalidArgument);
                Require(trace.spellings[ply].size() <= limits.maximum_input_bytes - bytes, TraceStatus::Limit);
                bytes += trace.spellings[ply].size();
            }
        }
        TraceBatch batch;
        batch.positions.reserve(plies + count);
        batch.moves.reserve(plies);
        batch.lines.reserve(count);
        batch.position_observations.reserve(plies + count);
        batch.move_observations.reserve(plies);
        batch.supplied_setups.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            failure.line = index;
            failure.ply = 0U;
            const auto& trace = input[index];
            StandardPosition board;
            Setup(board, trace);
            batch.lines.push_back({batch.positions.size(), batch.moves.size(), trace.ply_count});
            batch.supplied_setups.emplace_back(trace.initial_fen);
            Position(board, batch);
            for (std::size_t ply = 0U; ply < trace.ply_count; ++ply) {
                failure.ply = ply;
                const auto supplied = Text(trace.spellings[ply]);
                const auto move = board.moveFromString(supplied);
                Require(!move.isNull() && board.isLegalMove(move), TraceStatus::IllegalMove);
                const auto rendered = board.moveString(move, Chess::Board::StandardAlgebraic);
                Require(trace.notation != NotationPolicy::StrictSan || supplied == rendered,
                        TraceStatus::NoncanonicalSpelling);
                const auto generic = board.genericMove(move);
                const auto moving = board.pieceAt(generic.sourceSquare());
                const auto target_piece = board.pieceAt(generic.targetSquare());
                const bool castle = moving.type() == Chess::WesternBoard::King &&
                    target_piece == Chess::Piece(moving.side(), Chess::WesternBoard::Rook);
                const bool ep = moving.type() == Chess::WesternBoard::Pawn &&
                    generic.sourceSquare().file() != generic.targetSquare().file() &&
                    target_piece.isEmpty();
                laplace_chess_move action{SquareCode(generic.sourceSquare()),
                    SquareCode(generic.targetSquare()),
                    generic.promotion() == Chess::Piece::NoPiece ? std::uint8_t{0U} : PieceKind(generic.promotion()),
                    static_cast<std::uint8_t>(ep ? LAPLACE_CHESS_MOVE_EN_PASSANT : LAPLACE_CHESS_MOVE_ORDINARY)};
                MoveObservation observed{std::string(trace.spellings[ply]), rendered.toStdString(),
                    board.moveString(move, Chess::Board::LongAlgebraic).toStdString(),
                    SquareCode(generic.targetSquare()), supplied == rendered};
                Chess::BoardTransition transition;
                board.makeMove(move, &transition);
                Require(transition.drops().isEmpty() && transition.reserve().isEmpty(),
                        TraceStatus::ProviderDefect);
                unsigned matches = 0U;
                for (const auto& movement : transition.moves()) {
                    if (movement.source == generic.sourceSquare()) {
                        action.to_square = SquareCode(movement.target);
                        ++matches;
                    }
                }
                Require(matches == 1U, TraceStatus::ProviderDefect);
                if (castle) {
                    Require(transition.moves().size() == 2 &&
                        (action.to_square % 8U == 2U || action.to_square % 8U == 6U),
                        TraceStatus::ProviderDefect);
                    action.form = static_cast<std::uint8_t>(action.to_square % 8U == 6U ?
                        LAPLACE_CHESS_MOVE_CASTLE_KING : LAPLACE_CHESS_MOVE_CASTLE_QUEEN);
                } else Require(transition.moves().size() == 1, TraceStatus::ProviderDefect);
                batch.moves.push_back(action);
                batch.move_observations.push_back(std::move(observed));
                Position(board, batch);
            }
        }
        output = std::move(batch);
        return {};
    } catch (const Rejected& rejected) {
        failure.status = rejected.status;
    } catch (const std::bad_alloc&) {
        failure.status = TraceStatus::MemoryFailure;
    } catch (const std::length_error&) {
        failure.status = TraceStatus::Limit;
    } catch (const std::exception&) {
        failure.status = TraceStatus::ProviderDefect;
    }
    return failure;
}
}  // namespace laplace::chess_provider
