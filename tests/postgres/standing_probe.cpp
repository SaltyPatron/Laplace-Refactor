#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "laplace/isa.h"
#include "laplace/standing_calculation.h"
#include "../context_fixture.h"

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

void Print(const char* name, const laplace_digest256& value) {
    std::printf("%s=", name);
    for (const auto byte : value.bytes) {
        std::printf("%02x", static_cast<unsigned int>(byte));
    }
    std::putchar('\n');
}

laplace_standing_recipe Recipe() {
    laplace_standing_recipe value{};
    value.authority_receipt_id = Digest(0x10u);
    value.evaluation_law_id = Digest(0x20u);
    value.world_context_id = Digest(0x30u);
    value.language_modality_id = Digest(0x40u);
    value.valid_time_scope_id = Digest(0x50u);
    value.evidence_boundary_id = Digest(0x60u);
    value.default_rating = 1500.0;
    value.default_rating_deviation = 200.0;
    value.default_volatility = 0.06;
    value.volatility_constraint = 0.5;
    value.convergence_tolerance = 0.000001;
    value.score_numerator[LAPLACE_STANDING_OUTCOME_CONFIRM - 1u] = 1u;
    value.score_denominator[LAPLACE_STANDING_OUTCOME_CONFIRM - 1u] = 1u;
    value.score_denominator[LAPLACE_STANDING_OUTCOME_REFUTE - 1u] = 1u;
    value.score_numerator[LAPLACE_STANDING_OUTCOME_DRAW - 1u] = 1u;
    value.score_denominator[LAPLACE_STANDING_OUTCOME_DRAW - 1u] = 2u;
    value.score_numerator[LAPLACE_STANDING_OUTCOME_UNCERTAIN - 1u] = 1u;
    value.score_denominator[LAPLACE_STANDING_OUTCOME_UNCERTAIN - 1u] = 2u;
    value.rateable_outcome_mask = 0x0fu;
    value.participant_role = 1u;
    value.arena_kind = 1u;
    value.version = LAPLACE_STANDING_VERSION;
    if (laplace_standing_recipe_identify(&value, &value.recipe_id) !=
        LAPLACE_STANDING_OK) {
        std::fputs("standing recipe identity failed\n", stderr);
        std::exit(70);
    }
    return value;
}

laplace_standing_state State(std::uint8_t participant_seed) {
    const auto recipe = Recipe();
    const auto participant_id = Digest(participant_seed);
    const auto epoch = Digest(0x80u);
    laplace_standing_state state{};
    if (laplace_standing_onboard(
            &recipe, &participant_id, &epoch, &state) !=
        LAPLACE_STANDING_OK) {
        std::fputs("standing onboarding failed\n", stderr);
        std::exit(70);
    }
    return state;
}

laplace_standing_event Event(
    const laplace_standing_state& participant,
    const laplace_standing_state& opponent,
    const laplace_digest256& period,
    std::uint8_t seed,
    std::uint64_t score) {
    laplace_standing_event value{};
    value.participant_coordinate_id = participant.coordinate_id;
    value.participant_prior_state_id = participant.state_id;
    value.opponent_prior_state = opponent;
    value.period_id = period;
    value.eligible_root_id = Digest(seed);
    value.context_id = Digest(static_cast<std::uint8_t>(seed + 2u));
    value.valid_time_id = Digest(static_cast<std::uint8_t>(seed + 3u));
    value.score_numerator = score;
    value.score_denominator = 1u;
    value.outcome_kind = score == 0u
        ? LAPLACE_STANDING_OUTCOME_REFUTE
        : LAPLACE_STANDING_OUTCOME_CONFIRM;
    const auto recipe = Recipe();
    if (laplace_standing_outcome_mapping_identify(
            &recipe, value.outcome_kind, &value.outcome_mapping_id) !=
        LAPLACE_STANDING_OK) {
        std::fputs("standing outcome mapping identity failed\n", stderr);
        std::exit(71);
    }
    if (laplace_standing_event_identify(&value, &value.event_id) !=
        LAPLACE_STANDING_OK) {
        std::fputs("standing event identity failed\n", stderr);
        std::exit(71);
    }
    return value;
}

}  // namespace

int main() {
    const auto recipe = Recipe();
    const auto participant = State(0x01u);
    const auto opponent_a = State(0x02u);
    const auto opponent_b = State(0x03u);
    const auto period = Digest(0x90u);
    std::array<laplace_standing_period_input, 2> inputs{};
    inputs[0].recipe = recipe;
    inputs[0].prior_state = participant;
    inputs[0].event = Event(participant, opponent_a, period, 0xa0u, 1u);
    inputs[1] = inputs[0];
    inputs[1].event = Event(participant, opponent_b, period, 0xb0u, 0u);
    laplace_standing_period_result output{};
    laplace_standing_error standing_error{};
    if (laplace_standing_calculate_period_batch(
            inputs.data(), inputs.size(), &output, &standing_error) !=
        LAPLACE_STANDING_OK) {
        std::fputs("standing calculation failed\n", stderr);
        return 72;
    }
    auto isa_inputs = inputs;
    std::sort(isa_inputs.begin(), isa_inputs.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.event.event_id.bytes, right.event.event_id.bytes, 32u) < 0;
    });
    laplace_isa_value_view values[2]{};
    values[0] = {isa_inputs.data(), isa_inputs.size(), isa_inputs.size(), sizeof(isa_inputs[0]),
                 LAPLACE_ISA_VALUE_STANDING_PERIOD_INPUT_VECTOR, 0u, 0u};
    laplace_standing_period_result isa_output{};
    values[1] = {&isa_output, 0u, 1u, sizeof(isa_output),
                 LAPLACE_ISA_VALUE_STANDING_PERIOD_RESULT_VECTOR, 0u, 0u};
    laplace_isa_instruction instruction{
        LAPLACE_ISA_OPCODE_EVIDENCE_CALCULATE_STANDING_BATCH, 0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_CALCULATE_STANDING_BATCH, 0u};
    const auto context = laplace_test_context(0u);
    laplace_isa_program program{
        &instruction, values, &context, 1u, 2u,
        LAPLACE_ISA_MAJOR, LAPLACE_ISA_MINOR, 0u,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL, 0u};
    laplace_isa_receipt isa_receipt{};
    laplace_isa_error isa_error{};
    if (laplace_isa_execute(&program, &isa_receipt, &isa_error) != LAPLACE_ISA_OK ||
        std::memcmp(&output, &isa_output, sizeof(output)) != 0) {
        std::fputs("standing ISA execution failed\n", stderr);
        return 73;
    }
    Print("STANDING_PARTICIPANT_STATE", participant.state_id);
    Print("STANDING_RECIPE", recipe.recipe_id);
    Print("STANDING_AUTHORITY_RECEIPT", recipe.authority_receipt_id);
    laplace_digest256 confirm_mapping{};
    laplace_digest256 refute_mapping{};
    if (laplace_standing_outcome_mapping_identify(
            &recipe, LAPLACE_STANDING_OUTCOME_CONFIRM, &confirm_mapping) !=
            LAPLACE_STANDING_OK ||
        laplace_standing_outcome_mapping_identify(
            &recipe, LAPLACE_STANDING_OUTCOME_REFUTE, &refute_mapping) !=
            LAPLACE_STANDING_OK) {
        return 76;
    }
    Print("STANDING_CONFIRM_MAPPING", confirm_mapping);
    Print("STANDING_REFUTE_MAPPING", refute_mapping);
    Print("STANDING_PARTICIPANT_COORDINATE", participant.coordinate_id);
    Print("STANDING_ARENA", participant.arena_scope_id);
    Print("STANDING_OPPONENT_A_STATE", opponent_a.state_id);
    Print("STANDING_OPPONENT_A_COORDINATE", opponent_a.coordinate_id);
    Print("STANDING_OPPONENT_B_STATE", opponent_b.state_id);
    Print("STANDING_OPPONENT_B_COORDINATE", opponent_b.coordinate_id);
    Print("STANDING_EVENT_A", inputs[0].event.event_id);
    Print("STANDING_EVENT_B", inputs[1].event.event_id);
    Print("STANDING_SUCCESSOR_STATE", output.successor_state.state_id);
    Print("STANDING_RECEIPT", output.receipt.receipt_id);
    Print("STANDING_INPUT", output.receipt.input_fingerprint);
    Print("STANDING_OUTPUT", output.receipt.output_fingerprint);
    Print("STANDING_ISA_RECEIPT", isa_receipt.receipt_id);
    Print("STANDING_ISA_INPUT", isa_receipt.input_fingerprint);
    Print("STANDING_ISA_OUTPUT", isa_receipt.output_fingerprint);
    std::printf("STANDING_RATING=%.17g\n", output.successor_state.rating);
    std::printf("STANDING_DEVIATION=%.17g\n", output.successor_state.rating_deviation);
    std::printf("STANDING_VOLATILITY=%.17g\n", output.successor_state.volatility);

    const auto repeated_root_event = Event(
        output.successor_state, opponent_a, Digest(0xbfu), 0xa0u, 1u);
    Print("STANDING_REPEATED_ROOT_EVENT", repeated_root_event.event_id);

    const auto second_period = Digest(0x91u);
    std::array<laplace_standing_period_input, 1> second_inputs{};
    second_inputs[0].recipe = recipe;
    second_inputs[0].prior_state = output.successor_state;
    second_inputs[0].event = Event(
        output.successor_state, opponent_a, second_period, 0xc0u, 1u);
    laplace_standing_period_result second_output{};
    if (laplace_standing_calculate_period_batch(
            second_inputs.data(), second_inputs.size(), &second_output,
            &standing_error) != LAPLACE_STANDING_OK) {
        std::fputs("second standing calculation failed\n", stderr);
        return 74;
    }
    laplace_standing_period_result second_isa_output{};
    values[0] = {second_inputs.data(), second_inputs.size(), second_inputs.size(),
                 sizeof(second_inputs[0]),
                 LAPLACE_ISA_VALUE_STANDING_PERIOD_INPUT_VECTOR, 0u, 0u};
    values[1] = {&second_isa_output, 0u, 1u, sizeof(second_isa_output),
                 LAPLACE_ISA_VALUE_STANDING_PERIOD_RESULT_VECTOR, 0u, 0u};
    laplace_isa_receipt second_isa_receipt{};
    if (laplace_isa_execute(
            &program, &second_isa_receipt, &isa_error) != LAPLACE_ISA_OK ||
        std::memcmp(&second_output, &second_isa_output,
                    sizeof(second_output)) != 0) {
        std::fputs("second standing ISA execution failed\n", stderr);
        return 75;
    }
    Print("STANDING_SECOND_EVENT", second_inputs[0].event.event_id);
    Print("STANDING_SECOND_STATE", second_output.successor_state.state_id);
    Print("STANDING_SECOND_RECEIPT", second_output.receipt.receipt_id);
    Print("STANDING_SECOND_INPUT", second_output.receipt.input_fingerprint);
    Print("STANDING_SECOND_OUTPUT", second_output.receipt.output_fingerprint);
    Print("STANDING_SECOND_ISA_RECEIPT", second_isa_receipt.receipt_id);
    std::printf("STANDING_SECOND_RATING=%.17g\n",
                second_output.successor_state.rating);
    std::printf("STANDING_SECOND_DEVIATION=%.17g\n",
                second_output.successor_state.rating_deviation);
    std::printf("STANDING_SECOND_VOLATILITY=%.17g\n",
                second_output.successor_state.volatility);
    return 0;
}
