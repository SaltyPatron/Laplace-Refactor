// A physical rules provider over the selected official CuteChess StandardBoard.
// Canonical identity, chess admission, testimony and persistence remain Laplace owners.
#include "cutechess_standard_board.hpp"
#include "boardtransition.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr qint64 MaximumInputBytes = 1048576;
constexpr std::uint64_t MaximumPerftVisits = 5000000;

struct Refusal : std::runtime_error {
    std::string code;
    Refusal(const char* classification, const std::string& detail)
        : std::runtime_error(detail), code(classification) {}
};
void Require(bool valid, const char* code, const char* detail) {
    if (!valid) throw Refusal(code, detail);
}

using laplace::chess_provider::StandardPosition;

QJsonValue Side(Chess::Side side) {
    if (side.isNull()) return QJsonValue(QJsonValue::Null);
    return side == Chess::Side::White ? QStringLiteral("white") : QStringLiteral("black");
}
QJsonObject Square(const Chess::Square& square) {
    return {{"file", square.file()}, {"rank", square.rank()}};
}
QString PieceName(int type) {
    switch (type) {
    case Chess::WesternBoard::Pawn: return QStringLiteral("pawn");
    case Chess::WesternBoard::Knight: return QStringLiteral("knight");
    case Chess::WesternBoard::Bishop: return QStringLiteral("bishop");
    case Chess::WesternBoard::Rook: return QStringLiteral("rook");
    case Chess::WesternBoard::Queen: return QStringLiteral("queen");
    case Chess::WesternBoard::King: return QStringLiteral("king");
    default: throw Refusal("provider_defect", "nonstandard piece in standard position");
    }
}
QJsonValue Promotion(int type) {
    return type == Chess::Piece::NoPiece ? QJsonValue(QJsonValue::Null) : QJsonValue(PieceName(type));
}
QJsonObject State(StandardPosition& board) {
    QJsonArray pieces;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const auto piece = board.pieceAt(Chess::Square(file, rank));
            if (piece.isEmpty()) pieces.append(QJsonValue(QJsonValue::Null));
            else pieces.append(QJsonObject{{"side", Side(piece.side())},
                                           {"piece", PieceName(piece.type())}});
        }
    }
    const auto legal = board.legalMoves();
    const auto legal_ep = board.legalEnPassantSquare();
    QStringList lan;
    for (const auto& move : legal)
        lan.append(board.moveString(move, Chess::Board::LongAlgebraic));
    std::sort(lan.begin(), lan.end());
    QJsonArray legal_moves;
    for (const auto& text : lan) legal_moves.append(text);
    const bool checked = board.checked();
    const QString no_move_outcome = legal.isEmpty()
        ? (checked ? QStringLiteral("checkmate") : QStringLiteral("stalemate"))
        : QStringLiteral("none");
    const auto result = board.result();
    const int reversible = board.reversibleMoveCount();
    const int prior = board.repeatCount();
    return {
        {"piece_squares_a1_to_h8", pieces},
        {"side_to_move", Side(board.sideToMove())},
        {"castling_rights", QJsonObject{
            {"white_king_side", board.castling(Chess::Side::White, true)},
            {"white_queen_side", board.castling(Chess::Side::White, false)},
            {"black_king_side", board.castling(Chess::Side::Black, true)},
            {"black_queen_side", board.castling(Chess::Side::Black, false)}}},
        {"provider_en_passant_square", board.hasProviderEnPassant()
            ? QJsonValue(Square(board.providerEnPassantSquare())) : QJsonValue(QJsonValue::Null)},
        {"legal_en_passant_square", legal_ep.isValid()
            ? QJsonValue(Square(legal_ep)) : QJsonValue(QJsonValue::Null)},
        {"reversible_plies", reversible},
        {"plies_since_setup", board.plyCount()},
        {"fen_realization", board.fenString()},
        {"in_check", checked},
        {"legal_lan_moves", legal_moves},
        {"no_legal_move_outcome", no_move_outcome},
        {"no_legal_move_winner", legal.isEmpty() && checked
            ? Side(board.sideToMove().opposite()) : QJsonValue(QJsonValue::Null)},
        {"provider_adjudication", QJsonObject{
            {"type", static_cast<int>(result.type())},
            {"winner", Side(result.winner())},
            {"result", result.toShortString()},
            {"description", result.description()}}},
        {"draw_observations", QJsonObject{
            {"fifty_move_current_counter_met", reversible >= 100},
            {"seventy_five_move_current_counter_met", reversible >= 150},
            {"provider_prior_repetitions", prior},
            {"threefold_provider_counter_met", prior >= 2},
            {"fivefold_provider_counter_met", prior >= 4},
            {"exact_repetition_verified", false},
            {"complete_dead_position_adjudication", false}}}
    };
}
QJsonObject Movement(const Chess::GenericMove& move) {
    // CuteChess GenericMove uses the rook's square as its castling target.
    // Actual piece movements are reported separately from BoardTransition.
    return {{"source", Square(move.sourceSquare())},
            {"provider_target", Square(move.targetSquare())},
            {"promotion", Promotion(move.promotion())}};
}
QJsonObject Transition(const Chess::BoardTransition& transition) {
    QJsonArray movements, changed;
    for (const auto& move : transition.moves())
        movements.append(QJsonObject{{"source", Square(move.source)},
                                     {"target", Square(move.target)}});
    for (const auto& square : transition.squares()) changed.append(Square(square));
    Require(transition.drops().isEmpty() && transition.reserve().isEmpty(),
            "provider_defect", "standard chess produced reserve/drop state");
    return {{"piece_movements", movements}, {"changed_squares", changed}};
}
Chess::Move Parse(StandardPosition& board, const QString& supplied, bool strict,
                  QString& rendered) {
    Require(!supplied.isEmpty() && supplied.size() <= 32,
            "invalid_request", "move spelling is outside the declared envelope");
    const auto move = board.moveFromString(supplied);
    Require(!move.isNull() && board.isLegalMove(move),
            "illegal_move", "upstream provider found no legal move for the supplied spelling");
    rendered = board.moveString(move, Chess::Board::StandardAlgebraic);
#if !defined(LAPLACE_TEST_ACCEPT_NONCANONICAL_SAN)
    if (strict && rendered != supplied)
        throw Refusal("noncanonical_san", (QStringLiteral("supplied=") + supplied +
            QStringLiteral("; upstream_standard_san=") + rendered).toStdString());
#else
    (void)strict;
#endif
    return move;
}
std::uint64_t Perft(StandardPosition& board, int depth, std::uint64_t& visits) {
    Require(++visits <= MaximumPerftVisits, "resource_exhausted", "perft visit budget exhausted");
    if (depth == 0) return 1u;
    std::uint64_t nodes = 0u;
    const auto moves = board.legalMoves();
    for (const auto& move : moves) {
        board.makeMove(move);
        nodes += Perft(board, depth - 1, visits);
        board.undoMove();
    }
    return nodes;
}
void Keys(const QJsonObject& object, const QStringList& allowed) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        Require(allowed.contains(it.key()), "invalid_request", "unsupported request field");
}
QJsonObject Trace(const QJsonObject& request, bool strict, std::uint64_t& total_plies,
                  std::uint64_t& perft_visits) {
    Keys(request, {"fen", "san", "perft_depth"});
    StandardPosition board;
    board.initialize();
    QString initial = board.defaultFenString();
    if (request.contains("fen")) {
        Require(request.value("fen").isString(), "invalid_request", "FEN must be text");
        initial = request.value("fen").toString();
    }
    Require(!initial.isEmpty() && initial.size() <= 256,
            "invalid_request", "setup FEN is outside the declared envelope");
    Require(board.setFenString(initial), "invalid_position", "upstream provider refused setup FEN");
    Require(request.value("san").isArray(), "invalid_request", "ordered SAN array is required");
    const auto spellings = request.value("san").toArray();
    Require(spellings.size() <= 1024, "resource_exhausted", "trace exceeds the ply envelope");
    total_plies += static_cast<std::uint64_t>(spellings.size());
    Require(total_plies <= 4096u, "resource_exhausted", "batch exceeds the ply envelope");
    const auto first = State(board);
    QJsonArray plies;
    std::vector<QJsonObject> positions;
    positions.reserve(static_cast<std::size_t>(spellings.size()));
    for (const auto& value : spellings) {
        Require(value.isString(), "invalid_request", "move spelling must be text");
        const QString supplied = value.toString();
        QString rendered;
        const auto move = Parse(board, supplied, strict, rendered);
        const auto generic = board.genericMove(move);
        const QString lan = board.moveString(move, Chess::Board::LongAlgebraic);
        Chess::BoardTransition transition;
        board.makeMove(move, &transition);
        auto state = State(board);
        positions.push_back(state);
        plies.append(QJsonObject{{"supplied_spelling", supplied}, {"canonical_san", rendered},
            {"provider_legal", true}, {"lexically_canonical_san", supplied == rendered},
            {"lan_realization", lan}, {"provider_move", Movement(generic)},
            {"transition", Transition(transition)}, {"position", state}});
    }
    const auto final = State(board);
    QJsonValue perft(QJsonValue::Null);
    if (request.contains("perft_depth")) {
        const auto value = request.value("perft_depth");
        Require(value.isDouble() && value.toDouble() == value.toInt(-1) &&
                value.toInt(-1) >= 0 && value.toInt(-1) <= 4,
                "invalid_request", "perft depth must be an integer from zero through four");
        const auto visits_before = perft_visits;
        const auto nodes = Perft(board, value.toInt(), perft_visits);
        Require(State(board) == final, "provider_defect", "perft failed to restore exact board state");
        perft = QJsonObject{{"depth", value.toInt()},
            {"leaf_nodes_decimal", QString::number(static_cast<qulonglong>(nodes))},
            {"visited_nodes_decimal", QString::number(static_cast<qulonglong>(perft_visits - visits_before))}};
    }
    for (qsizetype index = 0; index < spellings.size(); ++index) board.undoMove();
    Require(State(board) == first, "provider_defect", "undo did not restore exact setup state");
    for (qsizetype index = 0; index < spellings.size(); ++index) {
        QString rendered;
        const auto move = Parse(board, spellings.at(index).toString(), strict, rendered);
        board.makeMove(move);
        Require(State(board) == positions[static_cast<std::size_t>(index)],
                "provider_defect", "replay changed a legal transition or history observation");
    }
    return {{"initial_position", first}, {"plies", plies}, {"final_position", final},
            {"perft", perft}, {"undo_exact", true}, {"replay_exact", true}};
}
QJsonArray RuntimeQtLibraries() {
    // Runtime observations, not installation metadata or content identity.
    QFile maps(QStringLiteral("/proc/self/maps"));
    Require(maps.open(QIODevice::ReadOnly), "runtime_unavailable", "Linux loader map unavailable");
    QStringList paths;
    const auto lines = QString::fromUtf8(maps.readAll()).split('\n');
    for (const auto& line : lines) {
        if (!line.contains("/libQt6Core.so") && !line.contains("/libQt6Core5Compat.so")) continue;
        const auto start = line.indexOf('/');
        Require(start >= 0 && !line.endsWith(" (deleted)"),
                "runtime_unavailable", "selected Qt mapping has no retained file");
        const auto path = line.mid(start).trimmed();
        if (!paths.contains(path)) paths.append(path);
    }
    Require(std::any_of(paths.begin(), paths.end(), [](const QString& path) {
                return path.contains("/libQt6Core.so");
            }), "runtime_unavailable", "selected Qt Core runtime is not mapped");
    std::sort(paths.begin(), paths.end());
    QJsonArray output;
    for (const auto& path : paths) output.append(path);
    return output;
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QJsonObject response{{"schema", QStringLiteral("laplace.cutechess-rules-response/v1")},
        {"status", QStringLiteral("refused")}, {"variant", QStringLiteral("standard")},
        {"canonical_chess_admission", false}, {"postgresql_admission", false},
        {"complete_fide_adjudication", false}, {"recorded_game_rate_measured", false}};
    try {
        Require(argc == 1, "invalid_request", "requests are supplied as one JSON document on stdin");
        QFile input;
        Require(input.open(stdin, QIODevice::ReadOnly), "invalid_request", "stdin unavailable");
        const auto bytes = input.read(MaximumInputBytes + 1);
        Require(bytes.size() <= MaximumInputBytes, "resource_exhausted", "input byte envelope exceeded");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        Require(error.error == QJsonParseError::NoError && document.isObject(),
                "invalid_request", "request is not a JSON object");
        const auto request = document.object();
        Keys(request, {"schema", "notation_policy", "traces"});
        Require(request.value("schema").toString() == "laplace.cutechess-rules-request/v1",
                "invalid_request", "unsupported request schema");
        const auto policy = request.value("notation_policy").toString();
        Require(policy == "strict-san" || policy == "provider-legal",
                "invalid_request", "explicit supported notation policy is required");
        Require(request.value("traces").isArray(), "invalid_request", "trace batch is required");
        const auto traces = request.value("traces").toArray();
        Require(!traces.isEmpty() && traces.size() <= 32,
                "resource_exhausted", "trace count is outside its envelope");
        std::uint64_t total = 0u, perft_visits = 0u;
        QJsonArray output;
        for (const auto& trace : traces) {
            Require(trace.isObject(), "invalid_request", "trace must be an object");
            output.append(Trace(trace.toObject(), policy == "strict-san", total, perft_visits));
        }
        response.insert("traces", output);
        response.insert("notation_policy", policy);
        response.insert("runtime_qt_version", QString::fromLatin1(qVersion()));
        response.insert("runtime_qt_libraries", RuntimeQtLibraries());
        response.insert("status", QStringLiteral("completed"));
        const auto serialized = QJsonDocument(response).toJson(QJsonDocument::Compact);
        std::fwrite(serialized.constData(), 1u, static_cast<std::size_t>(serialized.size()), stdout);
        std::fputc('\n', stdout);
        return 0;
    } catch (const Refusal& error) {
        response.insert("refusal", QString::fromStdString(error.code));
        response.insert("detail", QString::fromUtf8(error.what()));
    } catch (const std::exception& error) {
        response.insert("refusal", QStringLiteral("implementation_failure"));
        response.insert("detail", QString::fromUtf8(error.what()));
    }
    const auto serialized = QJsonDocument(response).toJson(QJsonDocument::Compact);
    std::fwrite(serialized.constData(), 1u, static_cast<std::size_t>(serialized.size()), stdout);
    std::fputc('\n', stdout);
    return 42;
}
