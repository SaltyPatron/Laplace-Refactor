#include "laplace/persistence.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>
#include <utility>

#include <gtest/gtest.h>

namespace {

void Fill(laplace_digest256* digest, std::uint8_t seed) {
    for (std::size_t index = 0; index < sizeof(digest->bytes); ++index) {
        digest->bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

std::uint64_t Metadata(std::uint8_t tier, std::uint32_t atom) {
    return (static_cast<std::uint64_t>(tier) << LAPLACE_TRAJECTORY_TIER_SHIFT) |
           (UINT64_C(1) << LAPLACE_TRAJECTORY_HAS_ATOM_BIT) |
           (static_cast<std::uint64_t>(atom) << LAPLACE_TRAJECTORY_ATOM_SHIFT);
}

struct Fixture final {
    laplace_id128 a{};
    laplace_id128 b{};
    laplace_digest256 a_witness{};
    laplace_digest256 b_witness{};
    std::array<laplace_trajectory_carrier, 3> carriers{};
    laplace_persistence_physicality_record physicality{};
    laplace_persistence_occurrence_record occurrence{};
    std::vector<std::uint8_t> bytes;
    std::uint64_t records{};
};

template <typename Encoder, typename... Arguments>
void AppendFrame(
    Fixture* fixture,
    std::uint16_t kind,
    Encoder encoder,
    Arguments&&... arguments) {
    const auto frame_bytes = laplace_persistence_frame_bytes(kind);
    const auto offset = fixture->bytes.size();
    fixture->bytes.resize(offset + frame_bytes);
    std::size_t written = 0;
    ASSERT_EQ(encoder(
                  std::forward<Arguments>(arguments)...,
                  fixture->bytes.data() + offset, frame_bytes, &written),
              LAPLACE_PERSISTENCE_OK);
    ASSERT_EQ(written, frame_bytes);
    fixture->records += 1u;
}

Fixture BuildFixture() {
    Fixture fixture{};
    EXPECT_EQ(laplace_identity_codepoint_witness(
                  0x41u, &fixture.a, &fixture.a_witness),
              LAPLACE_IDENTITY_OK);
    EXPECT_EQ(laplace_identity_codepoint_witness(
                  0x42u, &fixture.b, &fixture.b_witness),
              LAPLACE_IDENTITY_OK);
    EXPECT_EQ(laplace_trajectory_composition_encode(
                  &fixture.a, 1u, 1u, Metadata(2u, 0x41u), &fixture.carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(laplace_trajectory_composition_encode(
                  &fixture.b, 2u, 3u, Metadata(2u, 0x42u), &fixture.carriers[1]),
              LAPLACE_TRAJECTORY_OK);
    EXPECT_EQ(laplace_trajectory_composition_encode(
                  &fixture.a, 5u, 1u, Metadata(2u, 0x41u), &fixture.carriers[2]),
              LAPLACE_TRAJECTORY_OK);

    fixture.physicality.entity_id = fixture.a;
    fixture.physicality.physicality_type = LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    fixture.physicality.vertex_class = LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    fixture.physicality.recipe_version = 1u;
    fixture.physicality.structural_form = LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    fixture.physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    fixture.physicality.flags = LAPLACE_PERSISTENCE_PHYSICALITY_FLAGS_NONE;
    Fill(&fixture.physicality.recipe_fingerprint, 0x10u);
    Fill(&fixture.physicality.geometry_epoch, 0x40u);
    EXPECT_EQ(laplace_persistence_trajectory_fingerprint(
                  fixture.carriers.data(), fixture.carriers.size(),
                  &fixture.physicality.trajectory_fingerprint),
              LAPLACE_PERSISTENCE_OK);
    fixture.physicality.centroid.component[0] = 0.125;
    fixture.physicality.centroid.component[1] = -0.25;
    fixture.physicality.centroid.component[2] = 0.5;
    fixture.physicality.centroid.component[3] = -0.75;
    fixture.physicality.radius = 0.875;
    fixture.physicality.logical_count = 5u;
    fixture.physicality.vertex_count = fixture.carriers.size();
    EXPECT_EQ(laplace_persistence_physicality_identify(
                  &fixture.physicality, &fixture.physicality.physicality_id),
              LAPLACE_PERSISTENCE_OK);

    fixture.occurrence.entity_id = fixture.a;
    fixture.occurrence.physicality_id = fixture.physicality.physicality_id;
    Fill(&fixture.occurrence.source_fingerprint, 0x70u);
    Fill(&fixture.occurrence.context_fingerprint, 0xa0u);
    fixture.occurrence.source_ordinal = 1u;
    fixture.occurrence.flags = LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY;
    EXPECT_EQ(laplace_persistence_occurrence_identify(
                  &fixture.occurrence, &fixture.occurrence.occurrence_id),
              LAPLACE_PERSISTENCE_OK);

    std::array<std::pair<laplace_id128, laplace_digest256>, 2> entities{{
        {fixture.a, fixture.a_witness}, {fixture.b, fixture.b_witness}}};
    std::sort(entities.begin(), entities.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.first.bytes, right.first.bytes, sizeof(left.first.bytes)) < 0;
    });
    for (const auto& entity : entities) {
        AppendFrame(
            &fixture, LAPLACE_PERSISTENCE_RECORD_ENTITY,
            laplace_persistence_frame_encode_entity, &entity.first, &entity.second);
    }
    AppendFrame(
        &fixture, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
        laplace_persistence_frame_encode_physicality, &fixture.physicality);
    for (std::size_t index = 0; index < fixture.carriers.size(); ++index) {
        AppendFrame(
            &fixture, LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX,
            laplace_persistence_frame_encode_trajectory,
            &fixture.physicality.physicality_id, static_cast<std::uint64_t>(index),
            &fixture.carriers[index]);
    }
    AppendFrame(
        &fixture, LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE,
        laplace_persistence_frame_encode_occurrence, &fixture.occurrence);
    return fixture;
}

laplace_framework_canonical_batch Batch(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    std::uint64_t record_count,
    std::uint64_t first_ordinal) {
    return laplace_framework_canonical_batch{
        bytes,
        static_cast<std::uint64_t>(byte_count),
        record_count,
        first_ordinal,
        LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE,
        0u};
}

TEST(PersistenceContract, WholeTypedStreamValidatesAcrossBatchBoundaries) {
    const auto fixture = BuildFixture();
    const auto entity_frame_bytes =
        laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_ENTITY);
    const auto split = entity_frame_bytes * 2u;
    const std::array<laplace_framework_canonical_batch, 2> batches{{
        Batch(fixture.bytes.data(), split, 2u, 0u),
        Batch(fixture.bytes.data() + split, fixture.bytes.size() - split,
              fixture.records - 2u, 2u)}};
    laplace_persistence_summary summary{};
    ASSERT_EQ(laplace_persistence_validate_stream(
                  batches.data(), batches.size(), &summary),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(summary.entity_count, 2u);
    EXPECT_EQ(summary.physicality_count, 1u);
    EXPECT_EQ(summary.trajectory_vertex_count, 3u);
    EXPECT_EQ(summary.logical_occurrence_count, 5u);
    EXPECT_EQ(summary.occurrence_count, 1u);
    EXPECT_EQ(summary.frame_count, fixture.records);
    EXPECT_EQ(summary.byte_count, fixture.bytes.size());
}

TEST(PersistenceContract, LegalRecordFamiliesDepositIndependently) {
    const auto fixture = BuildFixture();
    const auto occurrence_bytes = laplace_persistence_frame_bytes(
        LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE);
    const auto* occurrence = fixture.bytes.data() +
        (fixture.bytes.size() - occurrence_bytes);
    const auto batch = Batch(occurrence, occurrence_bytes, 1U, 0U);
    laplace_persistence_summary summary{};
    ASSERT_EQ(laplace_persistence_validate_stream(&batch, 1U, &summary),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(summary.entity_count, 0U);
    EXPECT_EQ(summary.physicality_count, 0U);
    EXPECT_EQ(summary.trajectory_vertex_count, 0U);
    EXPECT_EQ(summary.occurrence_count, 1U);
    EXPECT_EQ(summary.frame_count, 1U);
}

TEST(PersistenceContract, CarrierIsExactTypedPayloadAndNotGeometry) {
    const auto fixture = BuildFixture();
    std::size_t offset =
        2u * laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_ENTITY) +
        laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_PHYSICALITY);
    laplace_persistence_record record{};
    std::size_t consumed = 0;
    ASSERT_EQ(laplace_persistence_frame_decode(
                  fixture.bytes.data() + offset, fixture.bytes.size() - offset,
                  &record, &consumed),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(record.kind, LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX);
    EXPECT_EQ(std::memcmp(
                  &record.value.trajectory.carrier, &fixture.carriers[0],
                  sizeof(fixture.carriers[0])),
              0);
    EXPECT_EQ(consumed,
              laplace_persistence_frame_bytes(
                  LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX));
}

TEST(PersistenceContract, ProvenanceReceiptIsNotARealizationIdentityInput) {
    auto fixture = BuildFixture();
    laplace_digest256 before{};
    laplace_digest256 after{};
    ASSERT_EQ(laplace_persistence_physicality_identify(
                  &fixture.physicality, &before),
              LAPLACE_PERSISTENCE_OK);
    /* Execution/package/resource receipts belong to the stream deposit receipt,
       not to this semantic realization record. No such field can perturb it. */
    ASSERT_EQ(laplace_persistence_physicality_identify(
                  &fixture.physicality, &after),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(std::memcmp(before.bytes, after.bytes, sizeof(before.bytes)), 0);
}

TEST(PersistenceContract, AtomicPointHasCanonicalInactiveTrajectoryState) {
    laplace_id128 entity{};
    laplace_digest256 witness{};
    laplace_digest256 recipe{};
    laplace_digest256 geometry_epoch{};
    laplace_point4d point{{0.125, -0.25, 0.5, -0.75}};
    ASSERT_EQ(laplace_identity_codepoint_witness(
                  0x41u, &entity, &witness),
              LAPLACE_IDENTITY_OK);
    Fill(&recipe, 0x20u);
    Fill(&geometry_epoch, 0x40u);
    laplace_persistence_physicality_record physicality{};
    ASSERT_EQ(laplace_persistence_atomic_point_physicality(
                  &entity, 1u, &recipe, &geometry_epoch, &point, &physicality),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(physicality.physicality_type,
              LAPLACE_PERSISTENCE_PHYSICALITY_ATOMIC_POINT);
    EXPECT_EQ(physicality.vertex_class, LAPLACE_PERSISTENCE_VERTEX_NONE);
    EXPECT_EQ(physicality.structural_form,
              LAPLACE_PERSISTENCE_STRUCTURAL_ATOMIC_POINT);
    EXPECT_EQ(physicality.logical_count, 1u);
    EXPECT_EQ(physicality.vertex_count, 0u);
    EXPECT_TRUE(std::all_of(
        std::begin(physicality.trajectory_fingerprint.bytes),
        std::end(physicality.trajectory_fingerprint.bytes),
        [](std::uint8_t value) { return value == 0u; }));
    std::uint64_t radius_bits = UINT64_MAX;
    std::memcpy(&radius_bits, &physicality.radius, sizeof(radius_bits));
    EXPECT_EQ(radius_bits, 0u);

    std::array<std::uint8_t, 232> frame{};
    std::size_t written = 0u;
    ASSERT_EQ(laplace_persistence_frame_encode_physicality(
                  &physicality, frame.data(), frame.size(), &written),
              LAPLACE_PERSISTENCE_OK);
    laplace_persistence_record decoded{};
    std::size_t consumed = 0u;
    ASSERT_EQ(laplace_persistence_frame_decode(
                  frame.data(), frame.size(), &decoded, &consumed),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(consumed, frame.size());
    EXPECT_EQ(std::memcmp(
                  &decoded.value.physicality, &physicality,
                  sizeof(physicality)),
              0);

    auto active_trajectory = physicality;
    active_trajectory.trajectory_fingerprint.bytes[0] = 1u;
    laplace_digest256 rejected{};
    EXPECT_EQ(laplace_persistence_physicality_identify(
                  &active_trajectory, &rejected),
              LAPLACE_PERSISTENCE_RECORD_INVALID);

    auto negative_zero = physicality;
    const std::uint64_t negative_zero_bits = UINT64_C(1) << 63u;
    std::memcpy(
        &negative_zero.radius, &negative_zero_bits,
        sizeof(negative_zero.radius));
    EXPECT_EQ(laplace_persistence_physicality_identify(
                  &negative_zero, &rejected),
              LAPLACE_PERSISTENCE_RECORD_INVALID);

    auto changed_point = point;
    changed_point.component[3] = -0.5;
    laplace_persistence_physicality_record changed{};
    ASSERT_EQ(laplace_persistence_atomic_point_physicality(
                  &entity, 1u, &recipe, &geometry_epoch, &changed_point,
                  &changed),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_NE(std::memcmp(
                  changed.physicality_id.bytes,
                  physicality.physicality_id.bytes,
                  sizeof(changed.physicality_id.bytes)),
              0);
}

TEST(PersistenceContract, AtomicPointStreamRequiresNoTrajectoryRows) {
    Fixture fixture{};
    ASSERT_EQ(laplace_identity_codepoint_witness(
                  0x41u, &fixture.a, &fixture.a_witness),
              LAPLACE_IDENTITY_OK);
    laplace_digest256 recipe{};
    laplace_digest256 geometry_epoch{};
    Fill(&recipe, 0x20u);
    Fill(&geometry_epoch, 0x40u);
    const laplace_point4d point{{0.125, -0.25, 0.5, -0.75}};
    ASSERT_EQ(laplace_persistence_atomic_point_physicality(
                  &fixture.a, 1u, &recipe, &geometry_epoch, &point,
                  &fixture.physicality),
              LAPLACE_PERSISTENCE_OK);
    fixture.occurrence.entity_id = fixture.a;
    fixture.occurrence.physicality_id = fixture.physicality.physicality_id;
    Fill(&fixture.occurrence.source_fingerprint, 0x70u);
    Fill(&fixture.occurrence.context_fingerprint, 0xa0u);
    fixture.occurrence.source_ordinal = 1u;
    fixture.occurrence.flags = LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY;
    ASSERT_EQ(laplace_persistence_occurrence_identify(
                  &fixture.occurrence, &fixture.occurrence.occurrence_id),
              LAPLACE_PERSISTENCE_OK);
    AppendFrame(
        &fixture, LAPLACE_PERSISTENCE_RECORD_ENTITY,
        laplace_persistence_frame_encode_entity,
        &fixture.a, &fixture.a_witness);
    AppendFrame(
        &fixture, LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
        laplace_persistence_frame_encode_physicality, &fixture.physicality);
    AppendFrame(
        &fixture, LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE,
        laplace_persistence_frame_encode_occurrence, &fixture.occurrence);
    const auto batch = Batch(
        fixture.bytes.data(), fixture.bytes.size(), fixture.records, 0u);
    laplace_persistence_summary summary{};
    ASSERT_EQ(laplace_persistence_validate_stream(&batch, 1u, &summary),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(summary.entity_count, 1u);
    EXPECT_EQ(summary.physicality_count, 1u);
    EXPECT_EQ(summary.trajectory_vertex_count, 0u);
    EXPECT_EQ(summary.logical_occurrence_count, 0u);
    EXPECT_EQ(summary.occurrence_count, 1u);
}

TEST(PersistenceContract, LaterTrajectoryDefectCannotPublishPartialSummary) {
    auto fixture = BuildFixture();
    const auto trajectory_frame =
        laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX);
    const auto physicality_frame =
        laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_PHYSICALITY);
    const auto entity_frame =
        laplace_persistence_frame_bytes(LAPLACE_PERSISTENCE_RECORD_ENTITY);
    const auto third_carrier_offset =
        entity_frame * 2u + physicality_frame + trajectory_frame * 2u + 8u + 40u;
    fixture.bytes[third_carrier_offset] ^= UINT8_C(1);
    const auto batch = Batch(
        fixture.bytes.data(), fixture.bytes.size(), fixture.records, 0u);
    laplace_persistence_summary summary{};
    std::memset(&summary, 0xa5, sizeof(summary));
    const auto before = summary;
    EXPECT_NE(laplace_persistence_validate_stream(&batch, 1u, &summary),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(std::memcmp(&summary, &before, sizeof(summary)), 0);
}

TEST(PersistenceContract, InvalidSemanticTagAndIncompleteTrajectoryAreTypedFailures) {
    auto fixture = BuildFixture();
    fixture.physicality.physicality_type = 9001u;
    laplace_digest256 invalid{};
    EXPECT_EQ(laplace_persistence_physicality_identify(
                  &fixture.physicality, &invalid),
              LAPLACE_PERSISTENCE_RECORD_INVALID);

    fixture = BuildFixture();
    const auto final_frame = laplace_persistence_frame_bytes(
        LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE);
    const auto batch = Batch(
        fixture.bytes.data(), fixture.bytes.size() - final_frame -
            laplace_persistence_frame_bytes(
                LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX),
        fixture.records - 2u, 0u);
    laplace_persistence_summary summary{};
    EXPECT_EQ(laplace_persistence_validate_stream(&batch, 1u, &summary),
              LAPLACE_PERSISTENCE_TRAJECTORY_INVALID);
}

TEST(PersistenceContract, PlanReceiptBindsTheExactVersionedPlanSequence) {
    const std::array<std::uint32_t, LAPLACE_PERSISTENCE_PG_PLAN_COUNT> plans{{
        LAPLACE_PERSISTENCE_PG_PLAN_REFERENCE_PREFLIGHT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_VERIFY,
        LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_INSERT,
        LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_VERIFY}};
    laplace_digest256 accepted{};
    ASSERT_EQ(laplace_persistence_plan_sequence_fingerprint(
                  plans.data(), plans.size(), &accepted),
              LAPLACE_PERSISTENCE_OK);

    auto reordered = plans;
    std::swap(reordered[1], reordered[2]);
    laplace_digest256 rejected{};
    EXPECT_EQ(laplace_persistence_plan_sequence_fingerprint(
                  reordered.data(), reordered.size(), &rejected),
              LAPLACE_PERSISTENCE_RECORD_INVALID);
    EXPECT_EQ(laplace_persistence_plan_sequence_fingerprint(
                  plans.data(), plans.size() - 1u, &rejected),
              LAPLACE_PERSISTENCE_INVALID_ARGUMENT);
}

TEST(PersistenceContract, ZeroIdentityAndFullWitnessAreValuesNotAbsence) {
    const laplace_id128 entity{};
    const laplace_digest256 witness{};
    std::array<std::uint8_t, 56> frame{};
    std::size_t written = 0;
    ASSERT_EQ(laplace_persistence_frame_encode_entity(
                  &entity, &witness, frame.data(), frame.size(), &written),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(written, frame.size());

    laplace_persistence_record decoded{};
    std::size_t consumed = 0;
    ASSERT_EQ(laplace_persistence_frame_decode(
                  frame.data(), frame.size(), &decoded, &consumed),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(consumed, frame.size());
    EXPECT_EQ(std::memcmp(
                  decoded.value.entity.entity_id.bytes,
                  entity.bytes, sizeof(entity.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  decoded.value.entity.identity_witness.bytes,
                  witness.bytes, sizeof(witness.bytes)),
              0);

    auto mismatched_witness = witness;
    mismatched_witness.bytes[0] = 1u;
    EXPECT_EQ(laplace_persistence_frame_encode_entity(
                  &entity, &mismatched_witness,
                  frame.data(), frame.size(), &written),
              LAPLACE_PERSISTENCE_RECORD_INVALID);
}

TEST(PersistenceContract, ZeroDigestPatternsUseExplicitTypedPresence) {
    const laplace_id128 zero_entity{};
    const laplace_digest256 zero_digest{};
    laplace_trajectory_carrier carrier{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &zero_entity, 1u, 1u, 0u, &carrier),
              LAPLACE_TRAJECTORY_OK);

    laplace_persistence_physicality_record physicality{};
    physicality.entity_id = zero_entity;
    physicality.physicality_type = LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    physicality.vertex_class = LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    physicality.recipe_version = 1u;
    physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    physicality.flags = LAPLACE_PERSISTENCE_PHYSICALITY_FLAGS_NONE;
    physicality.logical_count = 1u;
    physicality.vertex_count = 1u;
    ASSERT_EQ(laplace_persistence_physicality_identify(
                  &physicality, &physicality.physicality_id),
              LAPLACE_PERSISTENCE_OK);
    std::array<std::uint8_t, 232> physicality_frame{};
    std::size_t written = 0;
    EXPECT_EQ(laplace_persistence_frame_encode_physicality(
                  &physicality, physicality_frame.data(),
                  physicality_frame.size(), &written),
              LAPLACE_PERSISTENCE_OK);

    std::array<std::uint8_t, 80> trajectory_frame{};
    ASSERT_EQ(laplace_persistence_frame_encode_trajectory(
                  &zero_digest, 0u, &carrier,
                  trajectory_frame.data(), trajectory_frame.size(), &written),
              LAPLACE_PERSISTENCE_OK);
    laplace_persistence_record decoded{};
    std::size_t consumed = 0;
    EXPECT_EQ(laplace_persistence_frame_decode(
                  trajectory_frame.data(), trajectory_frame.size(),
                  &decoded, &consumed),
              LAPLACE_PERSISTENCE_OK);

    laplace_persistence_occurrence_record occurrence{};
    occurrence.entity_id = zero_entity;
    occurrence.physicality_id = zero_digest;
    occurrence.source_ordinal = 1u;
    occurrence.flags = LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY;
    ASSERT_EQ(laplace_persistence_occurrence_identify(
                  &occurrence, &occurrence.occurrence_id),
              LAPLACE_PERSISTENCE_OK);
    std::array<std::uint8_t, 168> occurrence_frame{};
    EXPECT_EQ(laplace_persistence_frame_encode_occurrence(
                  &occurrence, occurrence_frame.data(),
                  occurrence_frame.size(), &written),
              LAPLACE_PERSISTENCE_OK);

    occurrence.flags = 0u;
    occurrence.physicality_id = zero_digest;
    EXPECT_EQ(laplace_persistence_occurrence_identify(
                  &occurrence, &occurrence.occurrence_id),
              LAPLACE_PERSISTENCE_OK);
    occurrence.physicality_id.bytes[0] = 1u;
    EXPECT_EQ(laplace_persistence_occurrence_identify(
                  &occurrence, &occurrence.occurrence_id),
              LAPLACE_PERSISTENCE_RECORD_INVALID);
}

}  // namespace
