#ifndef LAPLACE_CUTECHESS_TRACE_ADAPTER_HPP
#define LAPLACE_CUTECHESS_TRACE_ADAPTER_HPP

#include "laplace/chess_line.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace laplace::chess_provider {

enum class NotationPolicy { StrictSan, ProviderLegal };
enum class TraceStatus {
    Ok, InvalidArgument, Limit, InvalidSetup, UnsupportedSetup,
    IllegalMove, NoncanonicalSpelling, ProviderDefect, MemoryFailure
};
struct TraceInput {
    std::string_view initial_fen;  // Empty selects the upstream standard start.
    const std::string_view* spellings{};
    std::size_t ply_count{};
    NotationPolicy notation{NotationPolicy::StrictSan};
};
struct TraceLimits {
    std::size_t maximum_lines{};
    std::size_t maximum_plies{};
    std::size_t maximum_input_bytes{};
};
struct TraceFailure {
    TraceStatus status{TraceStatus::Ok};
    std::size_t line{};
    std::size_t ply{};
};
// These observations stay beside the canonical planner input. They are not P/M
// identity fields and cannot be discarded by a caller claiming draw equivalence.
struct PositionObservation {
    std::string provider_fen;
    std::uint8_t provider_en_passant_square{LAPLACE_CHESS_NO_SQUARE};
    int reversible_plies{};
    int plies_since_setup{};
    int provider_prior_repetitions{};  // Upstream hash counter, not an exact proof.
};
struct MoveObservation {
    std::string supplied_spelling, canonical_san, provider_lan;
    std::uint8_t provider_target_square{};
    bool lexically_canonical{};
};
struct TraceBatch {
    std::vector<laplace_chess_position> positions;
    std::vector<laplace_chess_move> moves;
    std::vector<laplace_chess_line_range> lines;
    std::vector<PositionObservation> position_observations;
    std::vector<MoveObservation> move_observations;
    std::vector<std::string> supplied_setups;
};
// Native in-process batch transport. No JSON, subprocess, identity generator,
// persistence or PLAYING occurrence is created. Every move passes the selected
// upstream Board's legal-move mechanism. Outputs are cleared on any failure.
// Supported setup input is six-field orthodox FEN; inconsistent EP and
// nonorthodox castling setups are explicit refusals, never normalized silently.
TraceFailure BuildTraceBatch(const TraceInput* input, std::size_t count,
                             const TraceLimits& limits, TraceBatch& output);
}  // namespace laplace::chess_provider
#endif
