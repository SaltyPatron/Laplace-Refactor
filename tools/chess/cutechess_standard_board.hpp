#ifndef LAPLACE_CUTECHESS_STANDARD_BOARD_HPP
#define LAPLACE_CUTECHESS_STANDARD_BOARD_HPP

#include "standardboard.h"

namespace laplace::chess_provider {

// A read-only typed view of the selected upstream mechanism. No canonical
// identity, admission, repetition proof or alternative chess rules live here.
class StandardPosition final : public Chess::StandardBoard {
public:
    bool checked() const { return inCheck(sideToMove()); }
    bool castling(Chess::Side side, bool king_side) const {
        return hasCastlingRight(side, king_side ? KingSide : QueenSide);
    }
    bool hasProviderEnPassant() const { return enpassantSquare() != 0; }
    Chess::Square providerEnPassantSquare() const {
        return hasProviderEnPassant() ? chessSquare(enpassantSquare()) : Chess::Square();
    }
    Chess::Square legalEnPassantSquare() {
        if (!hasProviderEnPassant()) return Chess::Square();
        const auto target = providerEnPassantSquare();
        for (const auto& move : legalMoves()) {
            const auto generic = genericMove(move);
            if (generic.targetSquare() == target &&
                generic.sourceSquare().file() != target.file() &&
                pieceAt(generic.sourceSquare()).type() == Chess::WesternBoard::Pawn)
                return target;
        }
        return Chess::Square();
    }
};

}  // namespace laplace::chess_provider
#endif
