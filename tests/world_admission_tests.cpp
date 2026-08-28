#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "laplace/world_admission.h"

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0u; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_world_admission_record MakeAdmission(
    std::uint8_t seed,
    const laplace_digest256& boundary = Digest(0xe0u)) {
    laplace_world_admission_record value{};
    value.source_profile_id = Digest(seed);
    value.selected_boundary_fingerprint = boundary;
    value.source_profile_receipt_id = Digest(static_cast<std::uint8_t>(seed + 1u));
    value.recipe_receipt_id = Digest(static_cast<std::uint8_t>(seed + 2u));
    value.composition_working_set_receipt_id = Digest(static_cast<std::uint8_t>(seed + 3u));
    value.composition_presence_receipt_id = Digest(static_cast<std::uint8_t>(seed + 4u));
    value.composition_producer_receipt_id = Digest(static_cast<std::uint8_t>(seed + 5u));
    value.composition_stream_receipt_id = Digest(static_cast<std::uint8_t>(seed + 6u));
    value.evidence_lineage_receipt_id = Digest(static_cast<std::uint8_t>(seed + 7u));
    value.evidence_testimony_receipt_id = Digest(static_cast<std::uint8_t>(seed + 8u));
    value.readback_fingerprint = Digest(static_cast<std::uint8_t>(seed + 9u));
    value.profile_occurrence_count = 4u;
    value.composition_occurrence_count = 4u;
    value.profile_claim_count = 3u;
    value.evidence_node_count = 2u;
    value.testimony_count = 3u;
    value.profile_bound_testimony_count = 3u;
    value.recipe_bound_testimony_count = 3u;
    value.lineage_bound_testimony_count = 3u;
    value.closure_subject_count = 7u;
    value.closed_subject_count = 7u;
    value.reconstruction_class = 1u;
    value.flags = LAPLACE_WORLD_ADMISSION_FLAGS_NONE;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_OK);
    return value;
}

laplace_world_admission_status Execute(
    const std::vector<laplace_world_admission_record>& values,
    laplace_world_admission_receipt* receipt = nullptr,
    laplace_world_admission_error* error = nullptr) {
    laplace_world_admission_receipt local{};
    return laplace_world_admission_close_batch(
        values.data(), values.size(), receipt == nullptr ? &local : receipt, error);
}

void Sort(std::vector<laplace_world_admission_record>& values) {
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.admission_id.bytes, right.admission_id.bytes, 32u) < 0;
    });
}

TEST(WorldAdmission, ClosesCompleteReceiptBoundAdmissionsDeterministically) {
    std::vector<laplace_world_admission_record> values{
        MakeAdmission(0x10u), MakeAdmission(0x50u)};
    Sort(values);
    laplace_world_admission_receipt first{};
    laplace_world_admission_receipt replay{};
    ASSERT_EQ(Execute(values, &first), LAPLACE_WORLD_ADMISSION_OK);
    ASSERT_EQ(Execute(values, &replay), LAPLACE_WORLD_ADMISSION_OK);
    EXPECT_EQ(std::memcmp(&first, &replay, sizeof(first)), 0);
    EXPECT_EQ(first.admission_count, 2u);
    EXPECT_EQ(first.occurrence_count, 8u);
    EXPECT_EQ(first.claim_count, 6u);
    EXPECT_EQ(first.evidence_node_count, 4u);
    EXPECT_EQ(first.testimony_count, 6u);
    EXPECT_EQ(first.closure_subject_count, 14u);
    EXPECT_EQ(first.version, LAPLACE_WORLD_ADMISSION_VERSION);
    EXPECT_EQ(first.status, LAPLACE_WORLD_ADMISSION_OK);
}

TEST(WorldAdmission, IdentityBindsEveryComponentCountAndReadback) {
    const auto original = MakeAdmission(0x20u);
    std::array<laplace_world_admission_record, 17> changes{};
    changes.fill(original);
    changes[0].source_profile_id.bytes[0] ^= 1u;
    changes[1].selected_boundary_fingerprint.bytes[0] ^= 1u;
    changes[2].source_profile_receipt_id.bytes[0] ^= 1u;
    changes[3].recipe_receipt_id.bytes[0] ^= 1u;
    changes[4].composition_working_set_receipt_id.bytes[0] ^= 1u;
    changes[5].composition_presence_receipt_id.bytes[0] ^= 1u;
    changes[6].composition_producer_receipt_id.bytes[0] ^= 1u;
    changes[7].composition_stream_receipt_id.bytes[0] ^= 1u;
    changes[8].evidence_lineage_receipt_id.bytes[0] ^= 1u;
    changes[9].evidence_testimony_receipt_id.bytes[0] ^= 1u;
    changes[10].readback_fingerprint.bytes[0] ^= 1u;
    changes[11].profile_occurrence_count += 1u;
    changes[11].composition_occurrence_count += 1u;
    changes[12].profile_claim_count += 1u;
    changes[12].testimony_count += 1u;
    changes[12].profile_bound_testimony_count += 1u;
    changes[12].recipe_bound_testimony_count += 1u;
    changes[12].lineage_bound_testimony_count += 1u;
    changes[13].evidence_node_count += 1u;
    changes[14].profile_bound_testimony_count += 1u;
    changes[14].testimony_count += 1u;
    changes[14].profile_claim_count += 1u;
    changes[14].recipe_bound_testimony_count += 1u;
    changes[14].lineage_bound_testimony_count += 1u;
    changes[15].closure_subject_count += 1u;
    changes[15].closed_subject_count += 1u;
    changes[16].reconstruction_class = 2u;
    for (std::size_t index = 0u; index < changes.size(); ++index) {
        laplace_digest256 changed_id{};
        ASSERT_EQ(laplace_world_admission_identify(&changes[index], &changed_id),
                  LAPLACE_WORLD_ADMISSION_OK) << index;
        EXPECT_NE(std::memcmp(changed_id.bytes, original.admission_id.bytes, 32u), 0)
            << index;
    }
}

TEST(WorldAdmission, RejectsMissingComponentsAndIncompleteClosure) {
    auto value = MakeAdmission(0x30u);
    std::memset(value.composition_stream_receipt_id.bytes, 0, 32u);
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_COMPONENT_MISSING);
    value = MakeAdmission(0x30u);
    value.closed_subject_count -= 1u;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_CLOSURE_MISMATCH);
}

TEST(WorldAdmission, RejectsCountAndBindingDrift) {
    auto value = MakeAdmission(0x40u);
    value.composition_occurrence_count += 1u;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_OCCURRENCE_MISMATCH);
    value = MakeAdmission(0x40u);
    value.testimony_count += 1u;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_CLAIM_MISMATCH);
    value = MakeAdmission(0x40u);
    value.profile_bound_testimony_count -= 1u;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_PROFILE_BINDING_MISMATCH);
    value = MakeAdmission(0x40u);
    value.recipe_bound_testimony_count -= 1u;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_RECIPE_BINDING_MISMATCH);
}

TEST(WorldAdmission, RejectsTestimonyWithoutDurableLineageBinding) {
    auto value = MakeAdmission(0x50u);
    value.lineage_bound_testimony_count -= 1u;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_LINEAGE_BINDING_MISMATCH);
#if defined(LAPLACE_TEST_WORLD_ADMISSION_LINEAGE_BINDING_BYPASS)
    ADD_FAILURE() << "lineage-binding mutant survived";
#endif
}

TEST(WorldAdmission, RejectsIdentityOrderAndBoundaryDrift) {
    std::vector<laplace_world_admission_record> values{
        MakeAdmission(0x10u), MakeAdmission(0x50u)};
    Sort(values);
    auto drift = values;
    drift[0].readback_fingerprint.bytes[0] ^= 1u;
    EXPECT_EQ(Execute(drift), LAPLACE_WORLD_ADMISSION_IDENTITY_MISMATCH);
    std::reverse(values.begin(), values.end());
    EXPECT_EQ(Execute(values), LAPLACE_WORLD_ADMISSION_ORDER_INVALID);
    values = {MakeAdmission(0x10u), MakeAdmission(0x50u, Digest(0xf0u))};
    Sort(values);
    EXPECT_EQ(Execute(values), LAPLACE_WORLD_ADMISSION_BOUNDARY_MISMATCH);
}

}  // namespace
