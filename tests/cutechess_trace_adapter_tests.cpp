#include "cutechess_trace_adapter.hpp"
#include "cutechess_standard_board.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStringList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace laplace::chess_provider;
constexpr TraceLimits Limits{64U, 4096U, 1048576U};
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool Equal(const laplace_chess_position& a, const laplace_chess_position& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
TraceBatch Build(std::string_view fen, std::initializer_list<std::string_view> moves,
                 NotationPolicy policy = NotationPolicy::StrictSan) {
    TraceBatch output;
    const TraceInput input{fen, moves.begin(), moves.size(), policy};
    Check(BuildTraceBatch(&input, 1U, Limits, output).status == TraceStatus::Ok, "valid native trace refused");
    return output;
}
void Refuse(std::string_view fen, TraceStatus expected) {
    TraceBatch output = Build({}, {"e4"});
    const TraceInput input{fen, nullptr, 0U, NotationPolicy::StrictSan};
    Check(BuildTraceBatch(&input, 1U, Limits, output).status == expected, "wrong setup refusal");
    Check(output.positions.empty() && output.moves.empty() && output.lines.empty() &&
          output.position_observations.empty() && output.supplied_setups.empty(), "partial output escaped refusal");
}
}  // namespace
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QJsonObject receipt{{"schema", "laplace.cutechess-native-trace-conformance/v1"},
        {"status", "failed"}, {"canonical_chess_admission", false},
        {"postgresql_admission", false}, {"recorded_game_rate_measured", false},
        {"complete_fide_adjudication", false}, {"scope", "Native rules-to-planner trace transport"}};
    QJsonArray controls;
    try {
        Check(argc == 5, "receipt, native engine, Qt prefix and Qt version required");
        Check(QString::fromLatin1(qVersion()) == QString::fromLocal8Bit(argv[4]), "wrong Qt runtime version");
        const auto engine = QFileInfo(QString::fromLocal8Bit(argv[2])).canonicalFilePath();
        const auto qt_prefix = QFileInfo(QString::fromLocal8Bit(argv[3])).canonicalFilePath() + '/';
        Check(!engine.isEmpty() && qt_prefix != "/", "selected runtime paths unavailable");
        QFile maps(QStringLiteral("/proc/self/maps"));
        Check(maps.open(QIODevice::ReadOnly), "runtime loader map unavailable");
        QStringList paths;
        for (const auto& line : QString::fromUtf8(maps.readAll()).split('\n')) {
            if (!line.contains("/liblaplace_engine.so") && !line.contains("/libQt6Core.so") &&
                !line.contains("/libQt6Core5Compat.so")) continue;
            Check(!line.endsWith(" (deleted)"), "runtime dependency was deleted");
            const auto start = line.indexOf('/');
            Check(start >= 0, "runtime dependency lacks path");
            const auto path = QFileInfo(line.mid(start).trimmed()).canonicalFilePath();
            Check(!path.isEmpty(), "mapped runtime file unavailable");
            if (!paths.contains(path)) paths.append(path);
        }
        QJsonArray libraries;
        bool engine_mapped = false, qt_mapped = false;
        for (const auto& path : paths) {
            if (path == engine) engine_mapped = true;
            else {
                Check(path.startsWith(qt_prefix), "runtime Qt escaped selected prefix");
                if (QFileInfo(path).fileName().startsWith("libQt6Core.so")) qt_mapped = true;
            }
            QFile file(path);
            Check(file.open(QIODevice::ReadOnly), "cannot hash runtime dependency");
            QCryptographicHash hash(QCryptographicHash::Sha256);
            Check(hash.addData(&file), "cannot hash complete runtime dependency");
            libraries.append(QJsonObject{{"path", path}, {"byte_count", file.size()},
                {"sha256", QString::fromLatin1(hash.result().toHex())}});
        }
        Check(engine_mapped && qt_mapped, "selected native engine and Qt Core were not mapped");
        receipt.insert("runtime_libraries", libraries);
        auto trace = Build({}, {"e4", "e5", "Nf3"});
        Check(trace.positions.size() == 4U && trace.moves.size() == 3U &&
            trace.lines[0].move_count == 3U && trace.positions[0].pieces[4] == 6U &&
            trace.positions[0].pieces[60] == 14U && trace.positions[0].castling_rights == 15U &&
            trace.moves[0].from_square == 12U && trace.moves[0].to_square == 28U &&
            trace.positions[3].pieces[21] == 2U, "native standard piece mapping");
        controls.append("native piece, square, side and ordinary transition mapping");

        trace = Build("4k3/8/8/8/8/8/8/4K2R w K - 0 1", {"O-O"});
        Check(trace.moves[0].from_square == 4U && trace.moves[0].to_square == 6U &&
            trace.moves[0].form == LAPLACE_CHESS_MOVE_CASTLE_KING &&
            trace.move_observations[0].provider_target_square == 7U &&
            trace.positions[1].pieces[5] == 4U && trace.positions[1].pieces[6] == 6U,
            "castle actual movement differs from provider rook target");
        controls.append("native castle king destination and rook board delta");

        trace = Build("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", {"b8=Q+"});
        Check(trace.moves[0].promotion_kind == LAPLACE_CHESS_QUEEN &&
            trace.positions[1].pieces[57] == 5U, "native promotion mapping");
        controls.append("promotion and exact SAN suffix");

        trace = Build("k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1", {"exd6"});
        Check(trace.positions[0].legal_en_passant_square == 43U &&
            trace.moves[0].form == LAPLACE_CHESS_MOVE_EN_PASSANT &&
            trace.positions[1].pieces[35] == 0U && trace.positions[1].pieces[43] == 1U,
            "native legal EP mapping");
        controls.append("legal EP square, actual capture and board delta");

        auto pinned = Build("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", {});
        auto absent = Build("k3r3/8/8/3pP3/8/8/8/4K3 w - - 0 1", {});
        Check(Equal(pinned.positions[0], absent.positions[0]) &&
            pinned.positions[0].legal_en_passant_square == 64U &&
            pinned.position_observations[0].provider_en_passant_square == 43U &&
            absent.position_observations[0].provider_en_passant_square == 64U,
            "pinned EP changed canonical input or lost raw observation");
        controls.append("pinned EP shares normalized position and retains distinct raw observation");

        const auto clock_a = Build("4k3/8/8/8/8/8/8/4K3 w - - 0 1", {});
        const auto clock_b = Build("4k3/8/8/8/8/8/8/4K3 w - - 99 50", {});
        Check(Equal(clock_a.positions[0], clock_b.positions[0]) &&
            clock_a.position_observations[0].reversible_plies == 0 &&
            clock_b.position_observations[0].reversible_plies == 99 &&
            clock_a.position_observations[0].provider_fen != clock_b.position_observations[0].provider_fen,
            "clock observations salt normalized position or are lost");
        controls.append("clocks and fullmove observations stay separate from normalized position");

        Refuse("k7/8/3n4/3pP3/8/8/8/4K3 w - d6 0 1", TraceStatus::InvalidSetup);
        Refuse("k7/8/8/4P3/8/8/8/4K3 w - d6 0 1", TraceStatus::InvalidSetup);
        Refuse("k7/3n4/8/3pP3/8/8/8/4K3 w - d6 0 1", TraceStatus::InvalidSetup);
        Refuse("k7/8/8/3pP3/8/8/8/4K3 w - d6 1 1", TraceStatus::InvalidSetup);
        Refuse("k7/8/8/3p4/8/8/8/4K3 w - d6 1 1", TraceStatus::InvalidSetup);
        Refuse("k7/8/8/8/8/8/8/P3K3 w - - 0 1", TraceStatus::InvalidSetup);
        Refuse("4k3/8/8/8/8/8/8/4K3 w - - 0 2147483647", TraceStatus::InvalidSetup);
        Refuse("4k3/8/8/8/8/8/8/4K3 w - -", TraceStatus::UnsupportedSetup);
        controls.append("inconsistent EP, pawn rank, counter overflow and unsupported setup refuse atomically");

        // Preserve an upstream behavior witness: the selected parser accepts
        // occupied-target/missing-pawn EP. Do not execute moves from that setup.
        QJsonArray malformed;
        for (const auto fen : {"k7/8/3n4/3pP3/8/8/8/4K3 w - d6 0 1",
                               "k7/8/8/4P3/8/8/8/4K3 w - d6 0 1"}) {
            StandardPosition board; board.initialize();
            const bool accepted = board.setFenString(QString::fromLatin1(fen));
            Check(accepted && board.hasProviderEnPassant() &&
                !board.consistentEnPassantSquare(board.providerEnPassantSquare()) &&
                !board.legalEnPassantSquare().isValid(), "upstream setup boundary changed");
            malformed.append(QJsonObject{{"fen", QString::fromLatin1(fen)},
                {"upstream_set_fen_accepted", accepted}, {"adapter_refused", true},
                {"moves_executed", false}});
        }
        receipt.insert("upstream_inconsistent_setup_observations", malformed);
        controls.append("actual upstream permissive setup observations retained without unsafe move execution");

        const std::array<std::string_view, 1> false_check{{"e4+"}};
        TraceInput spelling{{}, false_check.data(), false_check.size(), NotationPolicy::StrictSan};
        TraceBatch output;
        Check(BuildTraceBatch(&spelling, 1U, Limits, output).status == TraceStatus::NoncanonicalSpelling &&
            output.positions.empty(), "strict lexical refusal");
        spelling.notation = NotationPolicy::ProviderLegal;
        Check(BuildTraceBatch(&spelling, 1U, Limits, output).status == TraceStatus::Ok &&
            !output.move_observations[0].lexically_canonical &&
            output.move_observations[0].supplied_spelling == "e4+" &&
            output.move_observations[0].canonical_san == "e4", "legal spelling provenance");
        controls.append("legality and lexical canonicality remain distinct facts");

        const std::array<std::string_view, 1> illegal{{"e5"}};
        const std::array<TraceInput, 2> mixed{{spelling,
            {{}, illegal.data(), illegal.size(), NotationPolicy::StrictSan}}};
        const auto refusal = BuildTraceBatch(mixed.data(), mixed.size(), Limits, output);
        Check(refusal.status == TraceStatus::IllegalMove && refusal.line == 1U &&
            output.positions.empty() && output.lines.empty(), "partial batch output on illegal second trace");
        auto small = Limits; small.maximum_plies = 0U;
        Check(BuildTraceBatch(&spelling, 1U, small, output).status == TraceStatus::Limit &&
            output.positions.empty(), "native ply bound");
        controls.append("late batch failure and resource limit publish no partial trace");

        const std::array<std::string_view, 117> corpus{{"d4","Nf6","Bg5","d5","Bxf6","exf6","c4","c6","e3","Bb4+","Nc3","O-O","Bd3","f5","Nge2","Qg5","O-O","dxc4","Bxc4","Bd6","f4","Qe7","Rf3","Nd7","Bd3","Nf6","h3","g6","Rc1","Be6","a3","a5","Na4","Bd5","Nec3","Bxf3","Qxf3","Rfe8","Nb6","Rad8","Nc4","Bc7","Kf2","Nd5","Nxd5","Rxd5","Ne5","Bxe5","fxe5","Rdd8","Rc5","Ra8","Bc4","Kg7","Qf4","h6","h4","a4","g3","Rac8","Qf3","Qd7","g4","b6","e6","fxe6","Re5","b5","Ba2","Re7","h5","Rf8","Ke2","f4","hxg6","fxe3","Qxe3","Rf6","g5","Rxg6","gxh6+","Kh8","h7","Re8","Bb1","Rg2+","Ke1","Qf7","Rg5","Rxg5","Qxg5","Rf8","Qe5+","Qf6","Qxf6+","Rxf6","Kd2","Rf3","Bd3","Rh3","Kc3","Kg7","Kd2","Kf6","Be4","Kg7","Bxc6","Kxh7","Bd7","Rh2+","Kc3","Rh3+","Kc2","Rh2+","Kc3","Rh3+","Kc2"}};
        const TraceInput game{{}, corpus.data(), corpus.size(), NotationPolicy::ProviderLegal};
        TraceBatch single, doubled;
        Check(BuildTraceBatch(&game, 1U, Limits, single).status == TraceStatus::Ok &&
            single.moves.size() == 117U && single.positions.size() == 118U &&
            single.positions.back().side_to_move == LAPLACE_CHESS_BLACK,
            "full 117-ply corpus trace");
        const std::array<TraceInput, 2> games{{game, game}};
        Check(BuildTraceBatch(games.data(), games.size(), Limits, doubled).status == TraceStatus::Ok &&
            doubled.lines[1].first_position == 118U && doubled.lines[1].first_move == 117U &&
            doubled.positions.size() == 236U && doubled.moves.size() == 234U,
            "direct native batch partition");
        for (std::size_t index = 0U; index < single.positions.size(); ++index)
            Check(Equal(single.positions[index], doubled.positions[index]) &&
                Equal(single.positions[index], doubled.positions[index + single.positions.size()]),
                "native scalar/batch position mismatch");
        for (std::size_t index = 0U; index < single.moves.size(); ++index)
            Check(std::memcmp(&single.moves[index], &doubled.moves[index], sizeof(laplace_chess_move)) == 0 &&
                std::memcmp(&single.moves[index], &doubled.moves[index + single.moves.size()],
                            sizeof(laplace_chess_move)) == 0, "native scalar/batch action mismatch");
        controls.append("full 117-ply trace and duplicate batch retain exact typed parity");
        laplace_chess_line_plan_input plan_input{};
        plan_input.positions = doubled.positions.data();
        plan_input.position_count = doubled.positions.size();
        plan_input.moves = doubled.moves.data();
        plan_input.move_count = doubled.moves.size();
        plan_input.lines = doubled.lines.data();
        plan_input.line_count = doubled.lines.size();
        plan_input.maximum_positions = 1000U; plan_input.maximum_moves = 1000U;
        plan_input.maximum_lines = 64U; plan_input.maximum_requests = 100000U;
        plan_input.maximum_operands = 1000000U;
        plan_input.version = LAPLACE_CHESS_LINE_VERSION;
        laplace_chess_line_plan* plan = nullptr;
        const auto planned = laplace_chess_line_plan_create(&plan_input, &plan);
        Check(planned == LAPLACE_CHESS_LINE_OK, "native Board trace refused by canonical LINE planner");
        laplace_chess_line_plan_view view{};
        const auto viewed = laplace_chess_line_plan_view_get(plan, &view);
        const bool exact = viewed == LAPLACE_CHESS_LINE_OK && view.line_count == 2U &&
            view.position_count == 236U && view.move_count == 234U &&
            view.line_result_indexes[0] == view.line_result_indexes[1];
        bool no_occurrences = exact;
        if (exact) for (std::uint64_t index = 0U; index < view.request_count; ++index)
            no_occurrences = no_occurrences && view.requests[index].flags == 0U;
        receipt.insert("planner_request_count", static_cast<qint64>(view.request_count));
        receipt.insert("planner_operand_count", static_cast<qint64>(view.operand_count));
        laplace_chess_line_plan_destroy(&plan);
        Check(exact && no_occurrences, "common LINE planner loses reuse or creates occurrences");
        controls.append("actual native Board batch enters common LINE planner with identical reusable roots");
        receipt.insert("native_trace_execution", true);
        receipt.insert("native_planner_execution", true);
        receipt.insert("corpus_plies", 117);
        receipt.insert("status", "passed");
    } catch (const std::exception& error) {
        receipt.insert("error", QString::fromUtf8(error.what()));
    }
    receipt.insert("controls", controls);
    receipt.insert("runtime_qt_version", QString::fromLatin1(qVersion()));
    const auto encoded = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
    std::fwrite(encoded.constData(), 1U, static_cast<std::size_t>(encoded.size()), stdout);
    if (argc >= 2) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size()) return 2;
    }
    return receipt.value("status").toString() == "passed" ? 0 : 1;
}
