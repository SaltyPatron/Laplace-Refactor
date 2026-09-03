#include "laplace/highway.h"
#include "context_fixture.h"

#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

laplace_id128 Id(std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_highway_key Key(std::uint32_t kind = LAPLACE_HIGHWAY_KIND_LANGUAGE) {
    return {kind, 0u, Id(0x10u), Id(0x30u), Id(0x50u), Id(0x70u), 1u};
}

bool Same(const laplace_highway_coordinate& left,
          const laplace_highway_coordinate& right) {
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

laplace_highway_coordinate Calculate(const laplace_highway_key& key) {
    laplace_highway_coordinate value{};
    EXPECT_EQ(laplace_highway_coordinate_calculate(&key, &value),
              LAPLACE_HIGHWAY_OK);
    return value;
}

}  // namespace

TEST(HighwayCoordinate, ExactScopeAndVersionDetermineDistinctCoordinates) {
    const auto base_key = Key();
    const auto base = Calculate(base_key);
    const auto replay = Calculate(base_key);
    EXPECT_TRUE(Same(base, replay));
    EXPECT_EQ(base.kind, LAPLACE_HIGHWAY_KIND_LANGUAGE);
    EXPECT_EQ(base.version, 1u);
    EXPECT_NE(std::memcmp(base.coordinate.bytes,
                          base_key.local_identifier.bytes,
                          sizeof(base.coordinate.bytes)), 0);

    auto changed = base_key;
    changed.authority.bytes[0] ^= 0x80u;
    EXPECT_FALSE(Same(base, Calculate(changed)));
    changed = base_key;
    changed.release.bytes[0] ^= 0x80u;
    EXPECT_FALSE(Same(base, Calculate(changed)));
    changed = base_key;
    changed.name_space.bytes[0] ^= 0x80u;
    EXPECT_FALSE(Same(base, Calculate(changed)));
    changed = base_key;
    changed.local_identifier.bytes[0] ^= 0x80u;
    EXPECT_FALSE(Same(base, Calculate(changed)));
    changed = base_key;
    changed.kind = LAPLACE_HIGHWAY_KIND_SCRIPT;
    EXPECT_FALSE(Same(base, Calculate(changed)));
    changed = base_key;
    changed.version = 2u;
    EXPECT_FALSE(Same(base, Calculate(changed)));
}

TEST(HighwayCoordinate, EveryMachineKindUsesOneOperation) {
    std::array<laplace_highway_key, LAPLACE_HIGHWAY_KIND_COUNT> keys{};
    std::array<laplace_highway_coordinate, LAPLACE_HIGHWAY_KIND_COUNT> outputs{};
    for (std::uint32_t index = 0; index < keys.size(); ++index) {
        keys[index] = Key(index + 1u);
        EXPECT_TRUE(laplace_highway_kind_valid(index + 1u));
    }
    EXPECT_FALSE(laplace_highway_kind_valid(0u));
    EXPECT_FALSE(laplace_highway_kind_valid(
        static_cast<std::uint32_t>(keys.size() + 1u)));
    EXPECT_EQ(laplace_highway_coordinate_calculate_batch(
                  keys.data(), keys.size(), outputs.data()),
              LAPLACE_HIGHWAY_OK);
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        EXPECT_EQ(outputs[index].kind, index + 1u);
        for (std::size_t other = index + 1; other < outputs.size(); ++other) {
            EXPECT_NE(std::memcmp(outputs[index].coordinate.bytes,
                                  outputs[other].coordinate.bytes,
                                  sizeof(outputs[index].coordinate.bytes)), 0);
        }
    }
}

TEST(HighwayCoordinate, ZeroAbsenceAndInvalidInputsNeverPublishPartialOutput) {
    std::array<laplace_highway_key, 3> keys{{Key(), Key(), Key()}};
    std::array<laplace_highway_coordinate, 3> outputs{};
    std::memset(outputs.data(), 0xA5, sizeof(outputs));
    const auto before = outputs;

    keys[1].release = {};
    EXPECT_EQ(laplace_highway_coordinate_calculate_batch(
                  keys.data(), keys.size(), outputs.data()),
              LAPLACE_HIGHWAY_ZERO_SCOPE);
    EXPECT_EQ(std::memcmp(outputs.data(), before.data(), sizeof(outputs)), 0);

    auto key = Key();
    key.version = 0u;
    laplace_highway_coordinate output{};
    EXPECT_EQ(laplace_highway_coordinate_calculate(&key, &output),
              LAPLACE_HIGHWAY_ZERO_VERSION);
    key = Key();
    key.reserved = 1u;
    EXPECT_EQ(laplace_highway_coordinate_calculate(&key, &output),
              LAPLACE_HIGHWAY_INVALID_ARGUMENT);
    key = Key(LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE + 1u);
    EXPECT_EQ(laplace_highway_coordinate_calculate(&key, &output),
              LAPLACE_HIGHWAY_UNKNOWN_KIND);
}

TEST(HighwayRegistry, GeneratedRowsAndMaterializationReceiptAreExact) {
    size_t kind_count = 0u;
    size_t alias_count = 0u;
    size_t disposition_count = 0u;
    const auto* kinds = laplace_highway_registry_kinds(&kind_count);
    const auto* aliases = laplace_highway_registry_aliases(&alias_count);
    const auto* dispositions =
        laplace_highway_registry_dispositions(&disposition_count);
    ASSERT_NE(kinds, nullptr);
    ASSERT_NE(dispositions, nullptr);
    EXPECT_EQ(kind_count, LAPLACE_HIGHWAY_KIND_COUNT);
    EXPECT_EQ(alias_count, LAPLACE_HIGHWAY_ALIAS_COUNT);
    EXPECT_EQ(aliases, nullptr);
    EXPECT_EQ(disposition_count, LAPLACE_HIGHWAY_DISPOSITION_COUNT);
    EXPECT_EQ(kinds[0].id, LAPLACE_HIGHWAY_KIND_GRAMMAR_SYMBOL);
    EXPECT_STREQ(kinds[0].name, "grammar-symbol");
    EXPECT_EQ(kinds[0].introduced, 1u);
    EXPECT_EQ(kinds[0].retired, 0u);
    EXPECT_EQ(kinds[kind_count - 1u].id,
              LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE);
    EXPECT_STREQ(kinds[kind_count - 1u].name, "source-profile");
    EXPECT_EQ(kinds[kind_count - 1u].introduced,
              LAPLACE_HIGHWAY_REGISTRY_VERSION);
    EXPECT_EQ(dispositions[0].id, LAPLACE_HIGHWAY_DISPOSITION_PRESENT);
    EXPECT_STREQ(dispositions[0].name, "present");
    EXPECT_EQ(dispositions[disposition_count - 1u].id,
              LAPLACE_HIGHWAY_DISPOSITION_COLLISION);

    auto context = laplace_test_context(12u);
    laplace_highway_registry_receipt first{};
    laplace_highway_registry_receipt replay{};
    ASSERT_EQ(laplace_highway_registry_materialize(&context, &first),
              LAPLACE_HIGHWAY_OK);
    ASSERT_EQ(laplace_highway_registry_materialize(&context, &replay),
              LAPLACE_HIGHWAY_OK);
    EXPECT_EQ(std::memcmp(&first, &replay, sizeof(first)), 0);
    EXPECT_EQ(first.registry_version, LAPLACE_HIGHWAY_REGISTRY_VERSION);
    EXPECT_EQ(first.kind_count, kind_count);
    EXPECT_EQ(first.alias_count, alias_count);
    EXPECT_EQ(first.disposition_count, disposition_count);
    EXPECT_EQ(first.status, LAPLACE_HIGHWAY_OK);
    EXPECT_EQ(std::memcmp(first.activation_epoch_id.bytes,
                          first.activation_epoch_fingerprint.bytes,
                          sizeof(first.activation_epoch_id.bytes)), 0);

    context.authority_fingerprint.bytes[0] ^= 0x80u;
    laplace_highway_registry_receipt other_context{};
    ASSERT_EQ(laplace_highway_registry_materialize(
                  &context, &other_context), LAPLACE_HIGHWAY_OK);
    EXPECT_NE(std::memcmp(first.receipt_id.bytes, other_context.receipt_id.bytes,
                          sizeof(first.receipt_id.bytes)), 0);
    EXPECT_EQ(std::memcmp(first.activation_epoch_fingerprint.bytes,
                          other_context.activation_epoch_fingerprint.bytes,
                          sizeof(first.activation_epoch_fingerprint.bytes)), 0);
}

TEST(HighwayRegistry, AstRequestsEveryObservedOccurrenceExplicitly) {
    auto context = laplace_test_context(12u);
    laplace_highway_registry_receipt receipt{};
    ASSERT_EQ(laplace_highway_registry_materialize(&context, &receipt),
              LAPLACE_HIGHWAY_OK);
    laplace_highway_registry_ast_plan* plan = nullptr;
    ASSERT_EQ(laplace_highway_registry_ast_plan_create(
                  &receipt.activation_epoch_fingerprint,
                  &receipt.receipt_id,
                  &plan),
              LAPLACE_HIGHWAY_OK);
    laplace_highway_registry_ast_view view{};
    ASSERT_EQ(laplace_highway_registry_ast_plan_view(plan, &view),
              LAPLACE_HIGHWAY_OK);
    ASSERT_NE(view.requests, nullptr);
    ASSERT_GT(view.request_count, 0U);
    for (std::uint64_t index = 0U; index < view.request_count; ++index) {
        EXPECT_EQ(view.requests[index].flags,
                  LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE);
    }
    laplace_highway_registry_ast_plan_destroy(&plan);
    EXPECT_EQ(plan, nullptr);
}
