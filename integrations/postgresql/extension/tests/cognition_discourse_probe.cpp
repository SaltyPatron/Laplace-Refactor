#include "laplace/cognition_discourse.h"
#include "laplace/cognition_discourse_frame.h"
#include "laplace/identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

laplace_digest256 Digest(const std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(seed) +
            static_cast<unsigned int>(index) + 1U);
    }
    return value;
}

laplace_id128 Codepoint(const std::uint32_t codepoint) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    if (laplace_identity_codepoint_witness(codepoint, &entity, &witness) !=
        LAPLACE_IDENTITY_OK) {
        std::cerr << "identity failure\n";
        std::exit(2);
    }
    return entity;
}

laplace_cognition_semantic_act SemanticAct(const std::uint8_t seed) {
    laplace_cognition_semantic_act act{};
    act.act_id = Digest(seed);
    act.request_fingerprint = Digest(static_cast<std::uint8_t>(seed + 1U));
    act.result_contract_fingerprint = Digest(static_cast<std::uint8_t>(seed + 2U));
    act.forward_receipt_id = Digest(static_cast<std::uint8_t>(seed + 3U));
    act.forward_output_fingerprint = Digest(static_cast<std::uint8_t>(seed + 4U));
    act.final_state_id = Digest(static_cast<std::uint8_t>(seed + 5U));
    act.answer_set_fingerprint = Digest(static_cast<std::uint8_t>(seed + 6U));
    act.primary_answer.entity_id = Codepoint(0x41U);
    act.answer_count = 1U;
    act.act_kind = LAPLACE_COGNITION_OPERATION_ANSWER;
    act.producer_operation_kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    act.flags = LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_PRODUCER_OPERATION_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT;
    act.version = LAPLACE_COGNITION_SEMANTIC_ACT_VERSION;
    return act;
}

laplace_cognition_discourse_input BaseInput(
    const laplace_digest256& discourse_id,
    const std::uint8_t seed,
    const std::uint64_t turn_ordinal) {
    laplace_cognition_discourse_input input{};
    input.discourse_id = discourse_id;
    input.observation_entity_id = Codepoint(
        static_cast<std::uint32_t>(0x42U + turn_ordinal));
    input.observation_occurrence_id = Digest(seed);
    input.active_entity_set_fingerprint = Digest(static_cast<std::uint8_t>(seed + 1U));
    input.proposition_set_fingerprint = Digest(static_cast<std::uint8_t>(seed + 2U));
    input.referent_set_fingerprint = Digest(static_cast<std::uint8_t>(seed + 3U));
    input.receipt_set_fingerprint = Digest(static_cast<std::uint8_t>(seed + 4U));
    input.world_id = Digest(180U);
    input.time_fingerprint = Digest(static_cast<std::uint8_t>(190U + turn_ordinal));
    input.context_fingerprint = Digest(200U);
    input.turn_ordinal = turn_ordinal;
    input.flags = LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES |
        LAPLACE_COGNITION_DISCOURSE_HAS_PROPOSITIONS |
        LAPLACE_COGNITION_DISCOURSE_HAS_REFERENTS |
        LAPLACE_COGNITION_DISCOURSE_HAS_RECEIPTS;
    input.version = LAPLACE_COGNITION_DISCOURSE_VERSION;
    return input;
}

struct EncodedState final {
    laplace_cognition_discourse_state state{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
};

EncodedState Build(
    const laplace_cognition_discourse_input& input,
    const laplace_cognition_semantic_act& act) {
    EncodedState result{};
    if (laplace_cognition_discourse_state_create(&input, &act, &result.state) !=
        LAPLACE_COGNITION_DISCOURSE_OK) {
        std::cerr << "discourse state failure\n";
        std::exit(3);
    }
    std::size_t written = 0U;
    laplace_cognition_discourse_frame_receipt receipt{};
    if (laplace_cognition_discourse_frame_encode(
            &result.state, result.frame.data(), result.frame.size(),
            &written, &receipt) != LAPLACE_COGNITION_DISCOURSE_FRAME_OK ||
        written != result.frame.size()) {
        std::cerr << "discourse frame failure\n";
        std::exit(4);
    }
    return result;
}

std::string Hex(const std::uint8_t* bytes, const std::size_t count) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0U; index < count; ++index) {
        stream << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return stream.str();
}

template <std::size_t N>
std::string Hex(const std::array<std::uint8_t, N>& bytes) {
    return Hex(bytes.data(), bytes.size());
}

}  // namespace

int main() {
    const auto discourse_id = Digest(10U);
    auto root_input = BaseInput(discourse_id, 20U, 0U);
    const auto root = Build(root_input, SemanticAct(30U));

    auto successor_input = BaseInput(discourse_id, 40U, 1U);
    successor_input.previous_state_id = root.state.state_id;
    successor_input.flags |= LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE;
    const auto successor = Build(successor_input, SemanticAct(50U));

    auto missing_input = BaseInput(discourse_id, 60U, 1U);
    missing_input.previous_state_id = Digest(250U);
    missing_input.flags |= LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE;
    const auto missing = Build(missing_input, SemanticAct(70U));

    auto corrupt = root.frame;
    corrupt.back() ^= UINT8_C(1);

    std::cout << "ROOT_FRAME=" << Hex(root.frame) << '\n';
    std::cout << "ROOT_STATE_ID="
              << Hex(root.state.state_id.bytes, sizeof(root.state.state_id.bytes)) << '\n';
    std::cout << "SUCCESSOR_FRAME=" << Hex(successor.frame) << '\n';
    std::cout << "SUCCESSOR_STATE_ID="
              << Hex(successor.state.state_id.bytes, sizeof(successor.state.state_id.bytes)) << '\n';
    std::cout << "DISCOURSE_ID="
              << Hex(discourse_id.bytes, sizeof(discourse_id.bytes)) << '\n';
    std::cout << "MISSING_PREDECESSOR_FRAME=" << Hex(missing.frame) << '\n';
    std::cout << "CORRUPT_FRAME=" << Hex(corrupt) << '\n';
    return 0;
}
