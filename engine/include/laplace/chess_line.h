#ifndef LAPLACE_CHESS_LINE_H
#define LAPLACE_CHESS_LINE_H

#include <stddef.h>
#include <stdint.h>
#include "laplace/composition.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native structural projection of an already-qualified standard-chess trace.
 * This planner does not replace a rules provider. The calling native Board
 * adapter must validate legality and normalize EP using actual legal moves.
 * Raw FEN/SAN/LAN, provider EP, clocks, repetition history, source and player
 * observations are retained by their occurrence owner, not accepted as identity
 * inputs here. No PLAYING or evidence occurrence is manufactured by this API.
 */
enum {
    LAPLACE_CHESS_LINE_VERSION = 1,
    LAPLACE_CHESS_EMPTY = 0,
    LAPLACE_CHESS_WHITE = 1,
    LAPLACE_CHESS_BLACK = 2,
    LAPLACE_CHESS_PAWN = 1,
    LAPLACE_CHESS_KNIGHT = 2,
    LAPLACE_CHESS_BISHOP = 3,
    LAPLACE_CHESS_ROOK = 4,
    LAPLACE_CHESS_QUEEN = 5,
    LAPLACE_CHESS_KING = 6,
    LAPLACE_CHESS_BLACK_PIECE_OFFSET = 8,
    LAPLACE_CHESS_NO_SQUARE = 64,
    LAPLACE_CHESS_CASTLE_WHITE_KING = 1,
    LAPLACE_CHESS_CASTLE_WHITE_QUEEN = 2,
    LAPLACE_CHESS_CASTLE_BLACK_KING = 4,
    LAPLACE_CHESS_CASTLE_BLACK_QUEEN = 8,
    LAPLACE_CHESS_MOVE_ORDINARY = 0,
    LAPLACE_CHESS_MOVE_CASTLE_KING = 1,
    LAPLACE_CHESS_MOVE_CASTLE_QUEEN = 2,
    LAPLACE_CHESS_MOVE_EN_PASSANT = 3
};

/* Squares are rank-major: a1=0, h1=7, a8=56, h8=63.
 * Pieces are 0 empty, 1..6 white P/N/B/R/Q/K, 9..14 black P/N/B/R/Q/K.
 * This is repetition-normalized position content, not complete draw/search
 * state: counters/history stay outside P and cannot be discarded by a caller
 * claiming fifty-move, repetition, or full search-state equivalence.
 */
typedef struct laplace_chess_position {
    uint8_t pieces[64];
    uint8_t side_to_move;
    uint8_t castling_rights;
    uint8_t legal_en_passant_square;
    uint8_t reserved[5];
} laplace_chess_position;

/* to_square is the piece's actual destination, including g/c for castling.
 * promotion_kind is 0 or N/B/R/Q (2..5). An ordinary move's capture context
 * belongs to adjacent P states; M records the piece action, not its notation.
 */
typedef struct laplace_chess_move {
    uint8_t from_square;
    uint8_t to_square;
    uint8_t promotion_kind;
    uint8_t form;
} laplace_chess_move;

/* Ranges exactly partition their arrays in batch order. Every line contains
 * move_count+1 positions and move_count moves, including a zero-move line.
 */
typedef struct laplace_chess_line_range {
    uint64_t first_position;
    uint64_t first_move;
    uint64_t move_count;
} laplace_chess_line_range;

typedef struct laplace_chess_line_plan_input {
    const laplace_chess_position* positions;
    uint64_t position_count;
    const laplace_chess_move* moves;
    uint64_t move_count;
    const laplace_chess_line_range* lines;
    uint64_t line_count;
    laplace_digest256 geometry_epoch;
    uint64_t maximum_positions;
    uint64_t maximum_moves;
    uint64_t maximum_lines;
    uint64_t maximum_requests;
    uint64_t maximum_operands;
    uint32_t version;
    uint32_t reserved;
} laplace_chess_line_plan_input;

typedef struct laplace_chess_line_plan laplace_chess_line_plan;
typedef struct laplace_chess_line_plan_view {
    const uint32_t* atom_positions;
    uint64_t atom_count;
    const laplace_composition_operand* operands;
    uint64_t operand_count;
    const laplace_composition_request* requests;
    uint64_t request_count;
    const uint64_t* position_result_indexes;
    uint64_t position_count;
    const uint64_t* move_result_indexes;
    uint64_t move_count;
    const uint64_t* line_result_indexes;
    uint64_t line_count;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    uint32_t version;
    uint32_t reserved;
} laplace_chess_line_plan_view;

typedef enum laplace_chess_line_status {
    LAPLACE_CHESS_LINE_OK = 0,
    LAPLACE_CHESS_LINE_INVALID_ARGUMENT = 1,
    LAPLACE_CHESS_LINE_INVALID_VERSION = 2,
    LAPLACE_CHESS_LINE_LIMIT = 3,
    LAPLACE_CHESS_LINE_POSITION_INVALID = 4,
    LAPLACE_CHESS_LINE_TRANSITION_INVALID = 5,
    LAPLACE_CHESS_LINE_MEMORY_FAILURE = 6
} laplace_chess_line_status;

/* Outputs ordinary topological composition requests. Resolve atom_positions
 * using the common active atom provider, then use the common composition /
 * PostgreSQL deposition owner. Every request has flags=0. Plan failure leaves
 * *plan null; borrowed view storage remains valid until destroy.
 */
LAPLACE_API laplace_chess_line_status laplace_chess_line_plan_create(
    const laplace_chess_line_plan_input* input, laplace_chess_line_plan** plan);
LAPLACE_API laplace_chess_line_status laplace_chess_line_plan_view_get(
    const laplace_chess_line_plan* plan, laplace_chess_line_plan_view* view);
LAPLACE_API void laplace_chess_line_plan_destroy(laplace_chess_line_plan** plan);

#ifdef __cplusplus
}
#endif
#endif
