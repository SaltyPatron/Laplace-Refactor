#include "laplace/cognition_conversation.h"
#include "laplace/cognition_materialization.h"
#include "laplace/identity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

struct OctetFixture {
    laplace_cognition_materialization_node node{};
    laplace_cognition_realization_result realization{};
    laplace_cognition_materialization_request request{};
    laplace_cognition_materialization_provider_v1 provider{};
    std::size_t resolve_calls{};

    static int Resolve(void* state, const laplace_id128* id,
                       laplace_cognition_materialization_node* output) {
        auto& fixture = *static_cast<OctetFixture*>(state);
        ++fixture.resolve_calls;
        if (std::memcmp(id->bytes, fixture.node.entity_id.bytes, sizeof(id->bytes)) != 0) {
            return 1;
        }
        *output = fixture.node;
        return 0;
    }

    static int Read(void*, const laplace_cognition_materialization_node*,
                    laplace_trajectory_carrier*, std::size_t, laplace_digest256*) {
        // Atom roots have no trajectory: reaching this callback is a defect.
        return 1;
    }

    void Initialize(const std::uint32_t atom) {
        node = {};
        realization = {};
        request = {};
        provider = {};
        resolve_calls = 0U;
        ASSERT_EQ(laplace_identity_codepoint_witness(
                      atom, &node.entity_id, &node.identity_witness),
                  LAPLACE_IDENTITY_OK);
        node.node_receipt_id.bytes[0] = 1U;
        node.logical_count = 1U;
        node.atom = atom;
        node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
        realization.content_id = node.entity_id;
        realization.candidate_receipt_id.bytes[0] = 2U;
        realization.realization_recipe_id.bytes[0] = 3U;
        realization.match_class = LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
        realization.disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
        realization.version = LAPLACE_COGNITION_REALIZATION_VERSION;
        request.maximum_nodes = 1U;
        request.maximum_output_bytes = 4U;
        request.version = LAPLACE_COGNITION_MATERIALIZATION_VERSION;
        provider.state = this;
        provider.provider_fingerprint.bytes[0] = 4U;
        provider.resolve_node = Resolve;
        provider.read_trajectory = Read;
        provider.abi_major = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
        provider.abi_minor = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;
    }
};

bool SameBytes(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

bool ZeroReceipt(const laplace_cognition_materialization_receipt& receipt) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&receipt);
    return std::all_of(bytes, bytes + sizeof(receipt),
                       [](unsigned char value) { return value == 0U; });
}

TEST(CognitionOutputSerialization, AllOctetsRoundTripThroughValidatedNativeRoots) {
    for (std::uint32_t atom = 0U; atom <= 255U; ++atom) {
        OctetFixture fixture;
        fixture.Initialize(atom);
        std::array<std::uint8_t, 4> output{0xa5U, 0xa5U, 0xa5U, 0xa5U};
        std::size_t count = 99U;
        laplace_cognition_materialization_receipt receipt{};
        ASSERT_EQ(laplace_cognition_realization_materialize_encoded(
                      &fixture.realization, &fixture.request, &fixture.provider,
                      LAPLACE_COGNITION_OUTPUT_OCTETS,
                      output.data(), output.size(), &count, &receipt),
                  LAPLACE_COGNITION_MATERIALIZATION_OK);
        EXPECT_EQ(count, 1U);
        EXPECT_EQ(output[0], static_cast<std::uint8_t>(atom));
        EXPECT_EQ(output[1], 0xa5U);
        EXPECT_EQ(receipt.codepoint_count, 1U);
        EXPECT_EQ(receipt.output_bytes, 1U);
        EXPECT_EQ(fixture.resolve_calls, 1U);
        EXPECT_EQ(std::memcmp(receipt.root_content_id.bytes, fixture.node.entity_id.bytes,
                              sizeof(receipt.root_content_id.bytes)), 0);
        EXPECT_TRUE(SameBytes(receipt.source_recipe_id, fixture.realization.realization_recipe_id));
    }
}

TEST(CognitionOutputSerialization, ExplicitEncodingChangesBytesWithoutChangingIdentity) {
    OctetFixture fixture;
    fixture.Initialize(0x80U);
    std::array<std::uint8_t, 4> text{};
    std::array<std::uint8_t, 4> binary{};
    std::size_t text_count = 0U, binary_count = 0U;
    laplace_cognition_materialization_receipt text_receipt{}, binary_receipt{};
    ASSERT_EQ(laplace_cognition_realization_materialize_encoded(
                  &fixture.realization, &fixture.request, &fixture.provider,
                  LAPLACE_COGNITION_OUTPUT_UTF8, text.data(), text.size(),
                  &text_count, &text_receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
    ASSERT_EQ(laplace_cognition_realization_materialize_encoded(
                  &fixture.realization, &fixture.request, &fixture.provider,
                  LAPLACE_COGNITION_OUTPUT_OCTETS, binary.data(), binary.size(),
                  &binary_count, &binary_receipt), LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(text_count, 2U);
    EXPECT_EQ(text[0], 0xc2U);
    EXPECT_EQ(text[1], 0x80U);
    EXPECT_EQ(binary_count, 1U);
    EXPECT_EQ(binary[0], 0x80U);
    EXPECT_EQ(std::memcmp(text_receipt.root_content_id.bytes, binary_receipt.root_content_id.bytes,
                          sizeof(text_receipt.root_content_id.bytes)), 0);
    EXPECT_TRUE(SameBytes(text_receipt.readset_fingerprint, binary_receipt.readset_fingerprint));
    EXPECT_FALSE(SameBytes(text_receipt.materialization_id, binary_receipt.materialization_id));
}

TEST(CognitionOutputSerialization, IdenticalAsciiOutputsStillReceiptTheSelectedEncoding) {
    OctetFixture fixture;
    fixture.Initialize(0x41U);
    std::array<std::uint8_t, 4> output{};
    std::size_t count = 0U;
    laplace_cognition_materialization_receipt text{}, binary{}, replay{};
    ASSERT_EQ(laplace_cognition_realization_materialize_utf8(
                  &fixture.realization, &fixture.request, &fixture.provider,
                  output.data(), output.size(), &count, &text),
              LAPLACE_COGNITION_MATERIALIZATION_OK);
    ASSERT_EQ(laplace_cognition_realization_materialize_encoded(
                  &fixture.realization, &fixture.request, &fixture.provider,
                  LAPLACE_COGNITION_OUTPUT_OCTETS, output.data(), output.size(), &count, &binary),
              LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_EQ(count, 1U);
    EXPECT_EQ(output[0], 0x41U);
    EXPECT_FALSE(SameBytes(text.output_fingerprint, binary.output_fingerprint));
    EXPECT_FALSE(SameBytes(text.materialization_id, binary.materialization_id));
    ASSERT_EQ(laplace_cognition_realization_materialize_encoded(
                  &fixture.realization, &fixture.request, &fixture.provider,
                  LAPLACE_COGNITION_OUTPUT_UTF8, output.data(), output.size(), &count, &replay),
              LAPLACE_COGNITION_MATERIALIZATION_OK);
    EXPECT_TRUE(SameBytes(text.materialization_id, replay.materialization_id));
}

TEST(CognitionOutputSerialization, WidePositionsAreRejectedWithoutTruncationOrPublication) {
    for (const std::uint32_t atom : {0x100U, 0x4e8bU, 0xd800U, 0x10ffffU}) {
        OctetFixture fixture;
        fixture.Initialize(atom);
        std::array<std::uint8_t, 4> output{0xa5U, 0xa5U, 0xa5U, 0xa5U};
        std::size_t count = 99U;
        laplace_cognition_materialization_receipt receipt{};
        receipt.output_bytes = 99U;
        EXPECT_EQ(laplace_cognition_realization_materialize_encoded(
                      &fixture.realization, &fixture.request, &fixture.provider,
                      LAPLACE_COGNITION_OUTPUT_OCTETS, output.data(), output.size(), &count, &receipt),
                  LAPLACE_COGNITION_MATERIALIZATION_ENCODING_RANGE);
        EXPECT_EQ(count, 0U);
        EXPECT_TRUE(ZeroReceipt(receipt));
        EXPECT_TRUE(std::all_of(output.begin(), output.end(),
                                [](std::uint8_t value) { return value == 0xa5U; }));
    }
}

TEST(CognitionOutputSerialization, UnknownEncodingIsRejectedBeforeProviderAccess) {
    OctetFixture fixture;
    fixture.Initialize(0x41U);
    std::array<std::uint8_t, 4> output{0xa5U, 0xa5U, 0xa5U, 0xa5U};
    std::size_t count = 99U;
    laplace_cognition_materialization_receipt receipt{};
    EXPECT_EQ(laplace_cognition_realization_materialize_encoded(
                  &fixture.realization, &fixture.request, &fixture.provider, UINT32_MAX,
                  output.data(), output.size(), &count, &receipt),
              LAPLACE_COGNITION_MATERIALIZATION_ENCODING_INVALID);
    EXPECT_EQ(fixture.resolve_calls, 0U);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(output[0], 0xa5U);
    EXPECT_TRUE(ZeroReceipt(receipt));
}

TEST(CognitionOutputSerialization, OctetSelectionCannotBypassIdentityValidation) {
    OctetFixture fixture;
    fixture.Initialize(0xffU);
    fixture.node.identity_witness.bytes[20] ^= 1U;
    std::array<std::uint8_t, 4> output{0xa5U, 0xa5U, 0xa5U, 0xa5U};
    std::size_t count = 99U;
    laplace_cognition_materialization_receipt receipt{};
    EXPECT_EQ(laplace_cognition_realization_materialize_encoded(
                  &fixture.realization, &fixture.request, &fixture.provider,
                  LAPLACE_COGNITION_OUTPUT_OCTETS, output.data(), output.size(), &count, &receipt),
              LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(output[0], 0xa5U);
    EXPECT_TRUE(ZeroReceipt(receipt));
}

TEST(CognitionOutputSerialization, CapacityFailurePublishesNoOctetsOrReceipt) {
    OctetFixture fixture;
    fixture.Initialize(0xffU);
    std::uint8_t output = 0xa5U;
    std::size_t count = 99U;
    laplace_cognition_materialization_receipt receipt{};
    EXPECT_EQ(laplace_cognition_realization_materialize_encoded(
                  &fixture.realization, &fixture.request, &fixture.provider,
                  LAPLACE_COGNITION_OUTPUT_OCTETS, &output, 0U, &count, &receipt),
              LAPLACE_COGNITION_MATERIALIZATION_CAPACITY);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(output, 0xa5U);
    EXPECT_TRUE(ZeroReceipt(receipt));
}

TEST(CognitionOutputSerialization, ConversationRejectsUnknownEncodingBeforeExecution) {
    laplace_cognition_conversation_request request{};
    laplace_cognition_observation_candidate_provider_v1 cognition{};
    laplace_cognition_realization_provider_v1 realization{};
    laplace_cognition_materialization_provider_v1 materialization{};
    laplace_cognition_conversation_result result{};
    std::uint8_t output = 0xa5U, frame = 0xa5U;
    std::size_t count = 99U, frame_count = 99U;
    EXPECT_EQ(laplace_cognition_conversation_execute_encoded(
                  &request, UINT32_MAX, nullptr, 0U, &cognition, &realization, &materialization,
                  &output, 1U, &count, &frame, 1U, &frame_count, &result),
              LAPLACE_COGNITION_CONVERSATION_ENCODING_INVALID);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(frame_count, 0U);
    EXPECT_EQ(output, 0xa5U);
    EXPECT_EQ(frame, 0xa5U);
}

}  // namespace
