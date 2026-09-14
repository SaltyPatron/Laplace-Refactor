#include "laplace/cognition_packet.h"
#include "laplace/cognition_packet_compile.h"
#include "laplace/cognition_runtime.h"
#include "laplace/observation_query.h"
#include "laplace/standing_calculation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_digest256 StandingPacketDigest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

laplace_id128 StandingPacketId(const std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

laplace_cognition_operator_field StandingPacketField(const std::uint8_t seed) {
    laplace_cognition_operator_field field{};
    field.field_id = StandingPacketDigest(seed);
    field.entity_id = StandingPacketId(static_cast<std::uint8_t>(seed + 20U));
    field.recipe_fingerprint = StandingPacketDigest(
        static_cast<std::uint8_t>(seed + 40U));
    field.value_dimension = 1U;
    return field;
}

laplace_standing_state StandingPacketState() {
    laplace_standing_state state{};
    state.coordinate_id = StandingPacketDigest(80U);
    state.arena_scope_id = StandingPacketDigest(81U);
    state.prior_state_id = StandingPacketDigest(82U);
    state.epoch_id = StandingPacketDigest(83U);
    state.rating_recipe_id = StandingPacketDigest(84U);
    state.rating = 1612.0;
    state.rating_deviation = 72.0;
    state.volatility = 0.059;
    state.eligible_match_count = 19U;
    state.period_ordinal = 7U;
    state.rating_recipe_version = 1U;
    state.flags = 0U;
    EXPECT_EQ(
        laplace_standing_state_identify(&state, &state.state_id),
        LAPLACE_STANDING_OK);
    return state;
}

laplace_cognition_operator_program StandingPacketProgram(
    const std::array<std::uint32_t, 1>& families,
    const std::uint32_t source_class) {
    laplace_cognition_operator_program program{};
    program.program_id = StandingPacketDigest(40U);
    program.boundary_id = StandingPacketDigest(41U);
    program.context_fingerprint = StandingPacketDigest(42U);
    program.evidence_epoch = StandingPacketDigest(43U);
    program.result_contract_fingerprint = StandingPacketDigest(44U);
    program.eligible_relation_families = families.data();
    program.eligible_relation_family_count = families.size();
    program.eligible_source_mask = UINT32_C(1) << (source_class - 1U);
    program.flags =
        LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_POSITIVE_SEMIDEFINITE_PRECISION;
    program.numeric_tolerance = 1e-12;
    program.version = LAPLACE_COGNITION_OPERATOR_VERSION;
    return program;
}

laplace_cognition_solver_program StandingPacketSolverProgram() {
    laplace_cognition_solver_program program{};
    program.program_id = StandingPacketDigest(50U);
    program.result_contract_fingerprint = StandingPacketDigest(51U);
    program.max_iterations = 64U;
    program.absolute_residual_tolerance = 1e-10;
    program.relative_residual_tolerance = 1e-10;
    program.regularization = 1e-3;
    program.method = LAPLACE_COGNITION_SOLVER_METHOD_CONJUGATE_GRADIENT;
    program.flags = LAPLACE_COGNITION_SOLVER_REQUIRE_PSD_OPERATOR;
    program.version = LAPLACE_COGNITION_SOLVER_VERSION;
    return program;
}

laplace_cognition_operator_constraint StandingPacketConstraint(
    const std::uint32_t source_class) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = StandingPacketDigest(30U);
    constraint.plane_id = StandingPacketDigest(31U);
    constraint.law_fingerprint = StandingPacketDigest(32U);
    constraint.units_fingerprint = StandingPacketDigest(33U);
    constraint.calculation_receipt_id = StandingPacketDigest(34U);
    constraint.source_field_index = 0U;
    constraint.target_field_index = 1U;
    constraint.transport_scale = 1.0;
    constraint.transport_offset = 0.0;
    constraint.target_value = 0.0;
    constraint.precision = 2.0;
    constraint.relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    constraint.source_class = source_class;
    constraint.direction = LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET;
    constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
    if (source_class == LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING) {
        constraint.standing = StandingPacketState();
    }
    return constraint;
}

TEST(CognitionStandingPacket, RoundTripsStandingReceiptWithoutWireShift) {
    const std::array<std::uint32_t, 1> families{{
        LAPLACE_OBSERVATION_QUERY_SEMANTIC}};
    const std::array<laplace_cognition_operator_field, 2> fields{{
        StandingPacketField(10U), StandingPacketField(11U)}};
    const auto constraint = StandingPacketConstraint(
        LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING);
    const auto operator_program = StandingPacketProgram(
        families, LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING);
    const auto solver_program = StandingPacketSolverProgram();
    const std::array<double, 2> initial{{1.0, -1.0}};

    laplace_cognition_runtime_request request{};
    request.operator_program = operator_program;
    request.fields = fields.data();
    request.constraints = &constraint;
    request.initial_state = initial.data();
    request.field_count = fields.size();
    request.constraint_count = 1U;
    request.initial_state_count = initial.size();
    request.solver_program = solver_program;

    std::size_t request_word_count = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_request_required_words(
            &request, &request_word_count),
        LAPLACE_COGNITION_PACKET_OK);
    EXPECT_EQ(request_word_count, 297U);
    std::vector<std::uint32_t> request_words(request_word_count);
    std::size_t encoded_request_words = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_encode_request_words(
            &request,
            request_words.data(),
            request_words.size(),
            &encoded_request_words),
        LAPLACE_COGNITION_PACKET_OK);
    ASSERT_EQ(encoded_request_words, request_words.size());
    ASSERT_GT(request_words.size(), 2U);
    EXPECT_EQ(request_words[2], 2U);

    std::size_t result_word_count = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_required_result_words(
            request_words.data(), request_words.size(), &result_word_count),
        LAPLACE_COGNITION_PACKET_OK);
    EXPECT_EQ(result_word_count, 149U);
    std::vector<std::uint32_t> result_words(result_word_count);
    std::size_t executed_result_words = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_execute_words(
            request_words.data(),
            request_words.size(),
            result_words.data(),
            result_words.size(),
            &executed_result_words),
        LAPLACE_COGNITION_PACKET_OK);
    ASSERT_EQ(executed_result_words, result_words.size());
    ASSERT_GT(result_words.size(), 2U);
    EXPECT_EQ(result_words[2], 2U);

    std::array<double, 2> solution{};
    laplace_cognition_runtime_result decoded{};
    decoded.solution = solution.data();
    decoded.solution_capacity = solution.size();
    ASSERT_EQ(
        laplace_cognition_packet_decode_result_words(
            result_words.data(), result_words.size(), &decoded),
        LAPLACE_COGNITION_PACKET_OK);
    EXPECT_EQ(decoded.status, LAPLACE_COGNITION_RUNTIME_OK);
    EXPECT_EQ(decoded.solution_count, solution.size());
    EXPECT_EQ(decoded.operator_receipt.field_count, fields.size());
    EXPECT_EQ(decoded.operator_receipt.input_constraint_count, 1U);
    EXPECT_EQ(decoded.operator_receipt.selected_constraint_count, 1U);
    EXPECT_EQ(decoded.operator_receipt.physicality_constraint_count, 0U);
    EXPECT_EQ(decoded.operator_receipt.testimony_constraint_count, 0U);
    EXPECT_EQ(decoded.operator_receipt.derived_constraint_count, 0U);
    EXPECT_EQ(decoded.operator_receipt.status, LAPLACE_COGNITION_OPERATOR_OK);
    EXPECT_EQ(decoded.solver_receipt.status, LAPLACE_COGNITION_SOLVER_OK);
}

TEST(CognitionStandingPacket, NonStandingRequestPreservesCanonicalV1Wire) {
    const std::array<std::uint32_t, 1> families{{
        LAPLACE_OBSERVATION_QUERY_SEMANTIC}};
    const std::array<laplace_cognition_operator_field, 2> fields{{
        StandingPacketField(10U), StandingPacketField(11U)}};
    const auto constraint = StandingPacketConstraint(
        LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY);
    const auto operator_program = StandingPacketProgram(
        families, LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY);
    const auto solver_program = StandingPacketSolverProgram();
    const std::array<double, 2> initial{{1.0, -1.0}};

    laplace_cognition_runtime_request request{};
    request.operator_program = operator_program;
    request.fields = fields.data();
    request.constraints = &constraint;
    request.initial_state = initial.data();
    request.field_count = fields.size();
    request.constraint_count = 1U;
    request.initial_state_count = initial.size();
    request.solver_program = solver_program;

    std::size_t request_word_count = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_request_required_words(
            &request, &request_word_count),
        LAPLACE_COGNITION_PACKET_OK);
    EXPECT_EQ(request_word_count, 237U);
    std::vector<std::uint32_t> request_words(request_word_count);
    std::size_t encoded_request_words = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_encode_request_words(
            &request,
            request_words.data(),
            request_words.size(),
            &encoded_request_words),
        LAPLACE_COGNITION_PACKET_OK);
    ASSERT_EQ(encoded_request_words, request_words.size());
    ASSERT_GT(request_words.size(), 2U);
    EXPECT_EQ(request_words[2], 1U);

    std::size_t result_word_count = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_required_result_words(
            request_words.data(), request_words.size(), &result_word_count),
        LAPLACE_COGNITION_PACKET_OK);
    EXPECT_EQ(result_word_count, 149U);
    std::vector<std::uint32_t> result_words(result_word_count);
    std::size_t executed_result_words = 0U;
    ASSERT_EQ(
        laplace_cognition_packet_execute_words(
            request_words.data(),
            request_words.size(),
            result_words.data(),
            result_words.size(),
            &executed_result_words),
        LAPLACE_COGNITION_PACKET_OK);
    ASSERT_EQ(executed_result_words, result_words.size());
    ASSERT_GT(result_words.size(), 2U);
    EXPECT_EQ(result_words[2], 1U);

    std::array<double, 2> solution{};
    laplace_cognition_runtime_result decoded{};
    decoded.solution = solution.data();
    decoded.solution_capacity = solution.size();
    ASSERT_EQ(
        laplace_cognition_packet_decode_result_words(
            result_words.data(), result_words.size(), &decoded),
        LAPLACE_COGNITION_PACKET_OK);
    EXPECT_EQ(decoded.status, LAPLACE_COGNITION_RUNTIME_OK);
    EXPECT_EQ(decoded.operator_receipt.physicality_constraint_count, 1U);
    EXPECT_EQ(decoded.operator_receipt.testimony_constraint_count, 0U);
    EXPECT_EQ(decoded.operator_receipt.derived_constraint_count, 0U);
    EXPECT_EQ(decoded.operator_receipt.status, LAPLACE_COGNITION_OPERATOR_OK);
    EXPECT_EQ(decoded.solver_receipt.status, LAPLACE_COGNITION_SOLVER_OK);
}

}  // namespace
