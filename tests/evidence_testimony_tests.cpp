#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "laplace/evidence_testimony.h"

namespace {

laplace_digest256 digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_evidence_testimony_record make_record(
    std::uint8_t seed,
    const laplace_digest256& profile = digest(0x40)) {
    laplace_evidence_testimony_record value{};
    value.evidence_node_id = digest(seed);
    value.source_profile_id = profile;
    value.recipe_receipt_id = digest(static_cast<std::uint8_t>(seed + 1u));
    value.trust_input_id = digest(static_cast<std::uint8_t>(seed + 2u));
    value.outcome_detail_id = digest(static_cast<std::uint8_t>(seed + 3u));
    value.uncertainty_numerator = 1u;
    value.uncertainty_denominator = 4u;
    value.sample_count = 3u;
    value.source_type = LAPLACE_EVIDENCE_SOURCE_CURATED_DATASET;
    value.outcome_type = LAPLACE_EVIDENCE_OUTCOME_ASSERTION;
    value.disposition = LAPLACE_EVIDENCE_DISPOSITION_PERSISTED;
    value.flags = LAPLACE_EVIDENCE_TESTIMONY_FLAGS_NONE;
    EXPECT_EQ(
        laplace_evidence_testimony_identify(&value, &value.testimony_id),
        LAPLACE_EVIDENCE_TESTIMONY_OK);
    return value;
}

void sort_records(std::vector<laplace_evidence_testimony_record>& records) {
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.testimony_id.bytes, right.testimony_id.bytes, 32u) < 0;
    });
}

laplace_evidence_testimony_status execute(
    const std::vector<laplace_evidence_testimony_record>& records,
    laplace_evidence_testimony_receipt* receipt = nullptr,
    laplace_evidence_testimony_error* error = nullptr) {
    laplace_evidence_testimony_receipt local_receipt{};
    laplace_evidence_testimony_error local_error{};
    return laplace_evidence_record_testimony_batch(
        records.data(), records.size(),
        receipt == nullptr ? &local_receipt : receipt,
        error == nullptr ? &local_error : error);
}

TEST(EvidenceTestimony, BindsCompleteProfileScopedTestimonyAndExactCounts) {
    std::vector<laplace_evidence_testimony_record> records{
        make_record(0x10), make_record(0x20), make_record(0x30)};
    records[0].uncertainty_numerator = 0u;
    records[0].uncertainty_denominator = 1u;
    records[0].sample_count = 2u;
    records[0].disposition = LAPLACE_EVIDENCE_DISPOSITION_REUSED;
    records[1].disposition = LAPLACE_EVIDENCE_DISPOSITION_UNSUPPORTED;
    records[2].disposition = LAPLACE_EVIDENCE_DISPOSITION_CONTRADICTED;
    for (auto& record : records) {
        ASSERT_EQ(laplace_evidence_testimony_identify(
                      &record, &record.testimony_id),
                  LAPLACE_EVIDENCE_TESTIMONY_OK);
    }
    sort_records(records);
    laplace_evidence_testimony_receipt first{};
    laplace_evidence_testimony_receipt second{};
    ASSERT_EQ(execute(records, &first), LAPLACE_EVIDENCE_TESTIMONY_OK);
    ASSERT_EQ(execute(records, &second), LAPLACE_EVIDENCE_TESTIMONY_OK);
    EXPECT_EQ(std::memcmp(&first, &second, sizeof(first)), 0);
    EXPECT_EQ(first.testimony_count, 3u);
    EXPECT_EQ(first.sample_count, 8u);
    EXPECT_EQ(first.uncertain_count, 2u);
    EXPECT_EQ(first.negative_disposition_count, 2u);
    EXPECT_EQ(first.version, LAPLACE_EVIDENCE_TESTIMONY_VERSION);
    EXPECT_EQ(first.status, LAPLACE_EVIDENCE_TESTIMONY_OK);
    EXPECT_EQ(std::memcmp(
        first.source_profile_id.bytes,
        records[0].source_profile_id.bytes, 32u), 0);
}

TEST(EvidenceTestimony, RejectsProfileMixingWithinOneBatch) {
    std::vector<laplace_evidence_testimony_record> records{
        make_record(0x10), make_record(0x20, digest(0x70))};
    sort_records(records);
    laplace_evidence_testimony_error error{};
    EXPECT_EQ(execute(records, nullptr, &error),
              LAPLACE_EVIDENCE_TESTIMONY_PROFILE_MISMATCH);
#if defined(LAPLACE_TEST_TESTIMONY_PROFILE_HOMOGENEITY_BYPASS)
    ADD_FAILURE() << "profile-homogeneity mutant survived";
#else
    EXPECT_EQ(error.record_index, 1u);
#endif
}

TEST(EvidenceTestimony, IdentityBindsRecipeTrustOutcomeUncertaintyAndCount) {
    const auto original = make_record(0x18);
    std::array<laplace_evidence_testimony_record, 9> mutations{};
    mutations.fill(original);
    mutations[0].recipe_receipt_id.bytes[0] ^= 1u;
    mutations[1].trust_input_id.bytes[0] ^= 1u;
    mutations[2].outcome_detail_id.bytes[0] ^= 1u;
    mutations[3].uncertainty_numerator = 2u;
    mutations[3].uncertainty_denominator = 5u;
    mutations[4].sample_count += 1u;
    mutations[5].source_type = LAPLACE_EVIDENCE_SOURCE_MODEL;
    mutations[6].outcome_type = LAPLACE_EVIDENCE_OUTCOME_PREDICTION;
    mutations[7].disposition = LAPLACE_EVIDENCE_DISPOSITION_DERIVED;
    mutations[8].source_profile_id.bytes[0] ^= 1u;
    for (const auto& mutation : mutations) {
        laplace_digest256 changed{};
        ASSERT_EQ(laplace_evidence_testimony_identify(&mutation, &changed),
                  LAPLACE_EVIDENCE_TESTIMONY_OK);
        EXPECT_NE(std::memcmp(
            changed.bytes, original.testimony_id.bytes, 32u), 0);
    }
}

TEST(EvidenceTestimony, RequiresReducedExactUncertainty) {
    auto record = make_record(0x22);
    record.uncertainty_numerator = 2u;
    record.uncertainty_denominator = 4u;
    EXPECT_EQ(laplace_evidence_testimony_identify(
                  &record, &record.testimony_id),
              LAPLACE_EVIDENCE_TESTIMONY_UNCERTAINTY_INVALID);
    record.uncertainty_numerator = 0u;
    record.uncertainty_denominator = 9u;
    EXPECT_EQ(laplace_evidence_testimony_identify(
                  &record, &record.testimony_id),
              LAPLACE_EVIDENCE_TESTIMONY_UNCERTAINTY_INVALID);
    record.uncertainty_numerator = 5u;
    record.uncertainty_denominator = 4u;
    EXPECT_EQ(laplace_evidence_testimony_identify(
                  &record, &record.testimony_id),
              LAPLACE_EVIDENCE_TESTIMONY_UNCERTAINTY_INVALID);
}

TEST(EvidenceTestimony, RejectsIdentityDriftAndUnsortedInput) {
    std::vector<laplace_evidence_testimony_record> records{
        make_record(0x10), make_record(0x20)};
    sort_records(records);
    auto drift = records;
    drift[0].sample_count += 1u;
    EXPECT_EQ(execute(drift), LAPLACE_EVIDENCE_TESTIMONY_IDENTITY_MISMATCH);
    std::reverse(records.begin(), records.end());
    EXPECT_EQ(execute(records), LAPLACE_EVIDENCE_TESTIMONY_ORDER_INVALID);
}

TEST(EvidenceTestimony, RejectsMissingBindingsAndZeroSamples) {
    auto record = make_record(0x28);
    std::memset(&record.source_profile_id, 0, sizeof(record.source_profile_id));
    EXPECT_EQ(laplace_evidence_testimony_identify(
                  &record, &record.testimony_id),
              LAPLACE_EVIDENCE_TESTIMONY_RECORD_INVALID);
    record = make_record(0x28);
    record.sample_count = 0u;
    EXPECT_EQ(laplace_evidence_testimony_identify(
                  &record, &record.testimony_id),
              LAPLACE_EVIDENCE_TESTIMONY_RECORD_INVALID);
}

}  // namespace
