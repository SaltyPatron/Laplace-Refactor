#include "laplace/cognition_discourse_frame.h"
#include "laplace/identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 v{};
    for (std::size_t i = 0; i < sizeof(v.bytes); ++i) v.bytes[i] = static_cast<std::uint8_t>(seed + i + 1U);
    return v;
}
laplace_id128 Codepoint(std::uint32_t cp) {
    laplace_id128 id{}; laplace_digest256 w{};
    EXPECT_EQ(laplace_identity_codepoint_witness(cp, &id, &w), LAPLACE_IDENTITY_OK);
    return id;
}
bool SameDigest(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

laplace_cognition_discourse_state State() {
    laplace_cognition_semantic_act act{};
    act.act_id = Digest(40); act.answer_count = 1;
    act.act_kind = LAPLACE_COGNITION_OPERATION_ANSWER;
    act.producer_operation_kind = LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH;
    act.flags = LAPLACE_COGNITION_SEMANTIC_ACT_PRIMARY_ANSWER_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_PRODUCER_OPERATION_PRESENT |
        LAPLACE_COGNITION_SEMANTIC_ACT_KIND_PRESENT;
    act.version = LAPLACE_COGNITION_SEMANTIC_ACT_VERSION;

    laplace_cognition_discourse_input input{};
    input.discourse_id = Digest(1);
    input.observation_entity_id = Codepoint(0x41U);
    input.observation_occurrence_id = Digest(2);
    input.active_entity_set_fingerprint = Digest(3);
    input.proposition_set_fingerprint = Digest(4);
    input.world_id = Digest(5); input.time_fingerprint = Digest(6); input.context_fingerprint = Digest(7);
    input.flags = LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES |
        LAPLACE_COGNITION_DISCOURSE_HAS_PROPOSITIONS;
    input.version = LAPLACE_COGNITION_DISCOURSE_VERSION;
    laplace_cognition_discourse_state state{};
    EXPECT_EQ(laplace_cognition_discourse_state_create(&input, &act, &state), LAPLACE_COGNITION_DISCOURSE_OK);
    return state;
}

TEST(CognitionDiscourseFrame, ExactRoundTripPreservesNativeDiscourseIdentity) {
    const auto original = State();
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> bytes{};
    std::size_t written = 0;
    laplace_cognition_discourse_frame_receipt encoded{};
    ASSERT_EQ(laplace_cognition_discourse_frame_encode(&original, bytes.data(), bytes.size(), &written, &encoded),
              LAPLACE_COGNITION_DISCOURSE_FRAME_OK);
    ASSERT_EQ(written, bytes.size());
    laplace_cognition_discourse_state decoded{};
    laplace_cognition_discourse_frame_receipt readback{};
    ASSERT_EQ(laplace_cognition_discourse_frame_decode(bytes.data(), bytes.size(), &decoded, &readback),
              LAPLACE_COGNITION_DISCOURSE_FRAME_OK);
    EXPECT_TRUE(SameDigest(original.state_id, decoded.state_id));
    EXPECT_TRUE(SameDigest(encoded.frame_fingerprint, readback.frame_fingerprint));
    EXPECT_TRUE(SameDigest(encoded.state_id, readback.state_id));
}

TEST(CognitionDiscourseFrame, CorruptionFailsBeforeStatePublication) {
    const auto original = State();
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> bytes{};
    std::size_t written = 0; laplace_cognition_discourse_frame_receipt encoded{};
    ASSERT_EQ(laplace_cognition_discourse_frame_encode(&original, bytes.data(), bytes.size(), &written, &encoded),
              LAPLACE_COGNITION_DISCOURSE_FRAME_OK);
    bytes.back() ^= 1U;
    laplace_cognition_discourse_state decoded{}; laplace_cognition_discourse_frame_receipt readback{};
    EXPECT_EQ(laplace_cognition_discourse_frame_decode(bytes.data(), bytes.size(), &decoded, &readback),
              LAPLACE_COGNITION_DISCOURSE_FRAME_CHECKSUM_MISMATCH);
    EXPECT_TRUE(SameDigest(decoded.state_id, laplace_digest256{}));
}

}  // namespace
