#include "laplace/unicode_root.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::uint64_t Bits(const double value) {
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

TEST(UnicodeNumericProvider, CalculatesBatchWithExactReceiptAndStableBits) {
    laplace_unicode_numeric_provider_v1 provider{};
    ASSERT_EQ(laplace_unicode_numeric_oneapi_provider(&provider),
              LAPLACE_UNICODE_OK);
    ASSERT_NE(provider.workspace, nullptr);
    ASSERT_NE(provider.calculate, nullptr);
    EXPECT_EQ(provider.abi_major, LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MAJOR);
    EXPECT_EQ(provider.abi_minor, LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MINOR);
    std::size_t workspace_bytes = 0u;
    ASSERT_EQ(provider.workspace(provider.state, 3u, &workspace_bytes),
              LAPLACE_UNICODE_OK);
    std::vector<std::uint8_t> workspace(workspace_bytes);
    std::array<laplace_point4d, 3> coordinates{};
    std::array<laplace_unicode_hopf_point, 3> hopf{};
    laplace_unicode_numeric_receipt receipt{};
    const auto status = provider.calculate(
        provider.state, 0u, coordinates.size(), workspace.data(),
        workspace.size(), coordinates.data(), hopf.data(), &receipt);
    ASSERT_EQ(status, LAPLACE_UNICODE_OK)
        << "threading=" << receipt.threading_layer
        << " branch=" << receipt.instruction_branch
        << " vml=" << receipt.vml_status
        << " fp=" << receipt.floating_exceptions
        << " errno=" << receipt.system_error;
    EXPECT_EQ(receipt.first_rank, 0u);
    EXPECT_EQ(receipt.rank_count, coordinates.size());
    EXPECT_EQ(receipt.status, LAPLACE_UNICODE_OK);
    EXPECT_EQ(std::memcmp(
                  receipt.provider_fingerprint.bytes,
                  provider.provider_fingerprint.bytes,
                  sizeof(receipt.provider_fingerprint.bytes)),
              0);
    const std::array<std::array<std::uint64_t, 4>, 3> expected{{
        {{UINT64_C(0x3f4177879d4012e6), UINT64_C(0xbf3a97a9556f109c),
          UINT64_C(0x3fec6ba8164af771), UINT64_C(0xbfdd69953ff06c6e)}},
        {{UINT64_C(0x3f3c4954e5352df5), UINT64_C(0x3f51a58a7b51e60c),
          UINT64_C(0xbfc1a44223c692be), UINT64_C(0x3fefb1d08759777a)}},
        {{UINT64_C(0xbf5863de68ce6456), UINT64_C(0x3f25df74ea546b84),
          UINT64_C(0xbfe7535f945d895a), UINT64_C(0xbfe5e827ef1ef2a0)}}}};
    for (std::size_t rank = 0u; rank < coordinates.size(); ++rank) {
        double coordinate_norm = 0.0;
        double hopf_norm = 0.0;
        for (std::size_t axis = 0u; axis < 4u; ++axis) {
            EXPECT_EQ(Bits(coordinates[rank].component[axis]),
                      expected[rank][axis]) << rank << ":" << axis;
            coordinate_norm += coordinates[rank].component[axis] *
                coordinates[rank].component[axis];
        }
        for (double component : hopf[rank].component) {
            hopf_norm += component * component;
        }
        EXPECT_NEAR(coordinate_norm, 1.0, 2e-15);
        EXPECT_NEAR(hopf_norm, 1.0, 4e-15);
    }

    std::array<laplace_point4d, 3> replay_coordinates{};
    std::array<laplace_unicode_hopf_point, 3> replay_hopf{};
    laplace_unicode_numeric_receipt replay{};
    ASSERT_EQ(provider.calculate(
                  provider.state, 0u, replay_coordinates.size(),
                  workspace.data(), workspace.size(), replay_coordinates.data(),
                  replay_hopf.data(), &replay),
              LAPLACE_UNICODE_OK);
    EXPECT_EQ(std::memcmp(
                  replay_coordinates.data(), coordinates.data(),
                  sizeof(coordinates)), 0);
    EXPECT_EQ(std::memcmp(replay_hopf.data(), hopf.data(), sizeof(hopf)), 0);
    EXPECT_EQ(replay.status, receipt.status);
    EXPECT_EQ(replay.threading_layer, receipt.threading_layer);
    EXPECT_EQ(replay.instruction_branch, receipt.instruction_branch);
    EXPECT_EQ(replay.vml_status, receipt.vml_status);
    EXPECT_EQ(replay.floating_exceptions, receipt.floating_exceptions);
    EXPECT_EQ(replay.system_error, receipt.system_error);
    EXPECT_EQ(std::memcmp(&replay, &receipt, sizeof(receipt)), 0)
        << "first threading=" << receipt.threading_layer
        << " branch=" << receipt.instruction_branch
        << " replay threading=" << replay.threading_layer
        << " branch=" << replay.instruction_branch;
}

}  // namespace
