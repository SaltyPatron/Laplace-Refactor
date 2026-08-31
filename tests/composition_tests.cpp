#include "laplace/composition.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "context_fixture.h"

namespace {

void Fill(laplace_digest256& digest, std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(digest.bytes); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_composition_known_entity Atom(
    std::uint32_t position,
    const laplace_point4d& point,
    std::uint8_t physicality_seed) {
    laplace_composition_known_entity result{};
    EXPECT_EQ(laplace_identity_codepoint_witness(
                  position, &result.entity_id, &result.identity_witness),
              LAPLACE_IDENTITY_OK);
    Fill(result.physicality_id, physicality_seed);
    result.centroid = point;
    result.atom = position;
    result.has_atom = 1U;
    return result;
}

laplace_composition_request Request(
    std::uint64_t first,
    std::uint64_t count,
    std::uint64_t source_ordinal,
    std::uint8_t seed) {
    laplace_composition_request result{};
    result.first_operand = first;
    result.operand_count = count;
    result.source_ordinal = source_ordinal;
    result.recipe_version = 1U;
    result.flags = LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE;
    Fill(result.recipe_fingerprint, seed);
    Fill(result.geometry_epoch, static_cast<std::uint8_t>(seed + 0x20U));
    Fill(result.occurrence_context_fingerprint,
         static_cast<std::uint8_t>(seed + 0x40U));
    return result;
}

struct PresenceFixture {
    std::vector<std::uint8_t> entity_dispositions;
    std::vector<std::uint8_t> physicality_dispositions;
    std::uint64_t entity_round_count{};
    std::uint64_t physicality_round_count{};
    bool truncate_entity_result{};
    std::uint8_t provider_fingerprint_seed{0xA1U};
    std::uint8_t provider_receipt_seed{0xC1U};
};

laplace_composition_status ResolvePresence(
    void* opaque,
    const laplace_composition_entity_candidate*,
    std::size_t entity_candidate_count,
    const laplace_persistence_physicality_record*,
    std::size_t physicality_candidate_count,
    std::uint8_t* entity_dispositions,
    std::uint8_t* physicality_dispositions,
    laplace_composition_presence_provider_result* result) {
    if (opaque == nullptr || entity_dispositions == nullptr || result == nullptr) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    auto& fixture = *static_cast<PresenceFixture*>(opaque);
    if (fixture.entity_dispositions.size() != entity_candidate_count ||
        fixture.physicality_dispositions.size() != physicality_candidate_count ||
        (physicality_candidate_count != 0U &&
         physicality_dispositions == nullptr)) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    std::copy(
        fixture.entity_dispositions.begin(),
        fixture.entity_dispositions.end(), entity_dispositions);
    std::copy(
        fixture.physicality_dispositions.begin(),
        fixture.physicality_dispositions.end(), physicality_dispositions);
    Fill(result->provider_fingerprint, fixture.provider_fingerprint_seed);
    Fill(result->provider_receipt_id, fixture.provider_receipt_seed);
    result->returned_entity_count = fixture.truncate_entity_result
        ? static_cast<std::uint64_t>(entity_candidate_count - 1U)
        : static_cast<std::uint64_t>(entity_candidate_count);
    result->returned_physicality_count =
        static_cast<std::uint64_t>(physicality_candidate_count);
    result->entity_round_count = fixture.entity_round_count;
    result->physicality_round_count = fixture.physicality_round_count;
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_presence_provider_v1 Provider(PresenceFixture& fixture) {
    return laplace_composition_presence_provider_v1{
        &fixture,
        ResolvePresence,
        LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI,
        0U,
        0U,
        0U};
}

struct PublicationSink {
    std::vector<std::uint8_t> bytes;
    std::uint64_t expected_bytes{};
    std::uint32_t record_type{};
    bool begun{};
    bool sealed{};
    bool aborted{};
};

laplace_framework_status BeginPublication(
    void* opaque,
    const laplace_framework_context*,
    std::uint32_t record_type,
    std::uint64_t,
    std::uint64_t total_bytes) {
    auto& sink = *static_cast<PublicationSink*>(opaque);
    sink.bytes.clear();
    sink.expected_bytes = total_bytes;
    sink.record_type = record_type;
    sink.begun = true;
    sink.sealed = false;
    sink.aborted = false;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status StagePublication(
    void* opaque,
    const laplace_framework_canonical_batch* batch) {
    auto& sink = *static_cast<PublicationSink*>(opaque);
    if (!sink.begun || sink.sealed || batch == nullptr ||
        batch->record_type != sink.record_type) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    try {
        sink.bytes.insert(
            sink.bytes.end(), batch->canonical_bytes,
            batch->canonical_bytes + batch->byte_count);
    } catch (...) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SealPublication(
    void* opaque,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    auto& sink = *static_cast<PublicationSink*>(opaque);
    if (!sink.begun || sink.bytes.size() != sink.expected_bytes ||
        stream_fingerprint == nullptr || artifact_fingerprint == nullptr) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    *artifact_fingerprint = *stream_fingerprint;
    sink.sealed = true;
    return LAPLACE_FRAMEWORK_OK;
}

void AbortPublication(void* opaque) {
    auto& sink = *static_cast<PublicationSink*>(opaque);
    sink.bytes.clear();
    sink.sealed = false;
    sink.aborted = true;
}

laplace_framework_sink_v1 PublicationProvider(PublicationSink& sink) {
    return laplace_framework_sink_v1{
        &sink,
        BeginPublication,
        StagePublication,
        SealPublication,
        AbortPublication,
        LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,
        LAPLACE_FRAMEWORK_SINK_ABI_MINOR,
        0U,
        0U};
}

struct PublicationControl {
    std::vector<laplace_framework_replay_checkpoint> checkpoints;
    std::uint64_t cancel_after_batches{UINT64_MAX};
    bool cancelled{};
};

int PublicationCancellationRequested(void* opaque) {
    return static_cast<PublicationControl*>(opaque)->cancelled ? 1 : 0;
}

void ObservePublication(
    void* opaque,
    const laplace_framework_replay_checkpoint* checkpoint) {
    auto& control = *static_cast<PublicationControl*>(opaque);
    control.checkpoints.push_back(*checkpoint);
    if (checkpoint->completed_batches == control.cancel_after_batches) {
        control.cancelled = true;
    }
}

laplace_framework_producer_control_v1 PublicationController(
    PublicationControl& control,
    const laplace_framework_replay_checkpoint* replay = nullptr) {
    return laplace_framework_producer_control_v1{
        &control,
        replay,
        PublicationCancellationRequested,
        ObservePublication,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR,
        LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR,
        0U,
        0U};
}

}  // namespace

TEST(CompositionWorkingSet, BuildsDeduplicatedTopologicalDagAndProducer) {
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x11U);
    Fill(calculation_recipe, 0x31U);
    const std::array<laplace_composition_known_entity, 3> known{{
        Atom('2', laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x51U),
        Atom('5', laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x71U),
        Atom('x', laplace_point4d{{0.0, 0.0, 1.0, 0.0}}, 0x91U)}};
    const std::array<laplace_composition_operand, 7> operands{{
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {1U, 2U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {1U, 2U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT, 0U},
        {2U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {2U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT, 0U}}};
    const std::array<laplace_composition_request, 4> requests{{
        Request(0U, 2U, 1U, 0x20U),
        Request(2U, 2U, 2U, 0x20U),
        Request(4U, 2U, 3U, 0x30U),
        Request(6U, 1U, 4U, 0x40U)}};
    const laplace_composition_working_set_input input{
        &context,
        &source,
        &calculation_recipe,
        known.data(),
        known.size(),
        operands.data(),
        operands.size(),
        requests.data(),
        requests.size(),
        400U,
        0U};

    laplace_composition_working_set* working_set = nullptr;
    ASSERT_EQ(laplace_composition_working_set_create(&input, &working_set),
              LAPLACE_COMPOSITION_OK);
    ASSERT_NE(working_set, nullptr);
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.known_entity_count, 3U);
    EXPECT_EQ(summary.request_count, 4U);
    EXPECT_EQ(summary.unique_entity_count, 5U);
    EXPECT_EQ(summary.unique_physicality_count, 2U);
    EXPECT_EQ(summary.trajectory_vertex_count, 4U);
    EXPECT_EQ(summary.occurrence_count, 4U);
    EXPECT_EQ(summary.logical_occurrence_count, 5U);
    EXPECT_EQ(summary.collapsed_request_count, 1U);
    EXPECT_EQ(summary.deduplicated_entity_count, 2U);
    EXPECT_EQ(summary.maximum_tier_floor, 2U);
    EXPECT_EQ(summary.presence_applied, 0U);
    EXPECT_EQ(summary.batch_count, 0U);
    EXPECT_EQ(summary.stream_record_count, 0U);

    std::size_t result_count = 0U;
    const auto* results = laplace_composition_working_set_results(
        working_set, &result_count);
    ASSERT_NE(results, nullptr);
    ASSERT_EQ(result_count, requests.size());
    EXPECT_EQ(std::memcmp(
                  results[0].entity_id.bytes, results[1].entity_id.bytes,
                  sizeof(results[0].entity_id.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  results[0].physicality_id.bytes, results[1].physicality_id.bytes,
                  sizeof(results[0].physicality_id.bytes)),
              0);
    EXPECT_EQ(results[0].tier_floor, 1U);
    EXPECT_EQ(results[2].tier_floor, 2U);
    EXPECT_EQ(results[3].collapsed, 1U);
    EXPECT_EQ(std::memcmp(
                  results[2].entity_id.bytes, results[3].entity_id.bytes,
                  sizeof(results[2].entity_id.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  results[2].physicality_id.bytes, results[3].physicality_id.bytes,
                  sizeof(results[2].physicality_id.bytes)),
              0);

    laplace_framework_producer_v1 producer{};
    ASSERT_EQ(laplace_composition_working_set_producer(working_set, &producer),
              LAPLACE_COMPOSITION_PRESENCE_REQUIRED);
    EXPECT_EQ(producer.state, nullptr);
    std::size_t entity_candidate_count = 0U;
    ASSERT_NE(laplace_composition_working_set_entity_candidates(
                  working_set, &entity_candidate_count),
              nullptr);
    ASSERT_EQ(entity_candidate_count, summary.unique_entity_count);
    const auto physicality_candidate_count = static_cast<std::size_t>(
        summary.unique_physicality_count);
    PresenceFixture presence{
        std::vector<std::uint8_t>(
            entity_candidate_count, LAPLACE_COMPOSITION_NOVEL),
        std::vector<std::uint8_t>(
            physicality_candidate_count, LAPLACE_COMPOSITION_NOVEL),
        3U,
        1U,
        false};
    auto provider = Provider(presence);
    laplace_composition_presence_receipt presence_receipt{};
    presence.entity_dispositions[0] = 2U;
    EXPECT_EQ(laplace_composition_working_set_resolve_presence(
                  working_set, &provider, &presence_receipt),
              LAPLACE_COMPOSITION_PRESENCE_INVALID);
    EXPECT_EQ(presence_receipt.status, LAPLACE_COMPOSITION_PRESENCE_INVALID);
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.presence_applied, 0U);
    EXPECT_EQ(summary.stream_record_count, 0U);
    presence.entity_dispositions[0] = LAPLACE_COMPOSITION_NOVEL;
    ASSERT_EQ(laplace_composition_working_set_resolve_presence(
                  working_set, &provider, &presence_receipt),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(presence_receipt.status, LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(presence_receipt.entity_candidate_count, entity_candidate_count);
    EXPECT_EQ(presence_receipt.physicality_candidate_count,
              physicality_candidate_count);
    EXPECT_EQ(presence_receipt.participating_tier_count, 3U);
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(std::memcmp(
                  presence_receipt.semantic_receipt_id.bytes,
                  summary.presence_receipt_id.bytes,
                  sizeof(presence_receipt.semantic_receipt_id.bytes)),
              0);
    EXPECT_EQ(summary.presence_applied, 1U);
    EXPECT_EQ(summary.novel_entity_count, summary.unique_entity_count);
    EXPECT_EQ(summary.novel_physicality_count,
              summary.unique_physicality_count);
    EXPECT_EQ(summary.novel_trajectory_vertex_count,
              summary.trajectory_vertex_count);
    EXPECT_GT(summary.batch_count, 1U);
    ASSERT_EQ(laplace_composition_working_set_producer(working_set, &producer),
              LAPLACE_COMPOSITION_OK);
    laplace_framework_producer_plan plan{};
    ASSERT_EQ(producer.prepare(
                  producer.state, &context, &source, &calculation_recipe, &plan),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(plan.batch_count, summary.batch_count);
    EXPECT_EQ(plan.total_records, summary.stream_record_count);
    std::vector<laplace_framework_canonical_batch> batches;
    for (std::uint64_t index = 0U; index < plan.batch_count; ++index) {
        laplace_framework_canonical_batch batch{};
        laplace_digest256 cursor{};
        ASSERT_EQ(producer.next(
                      producer.state, index, &batch, &cursor),
                  LAPLACE_FRAMEWORK_OK);
        batches.push_back(batch);
    }
    laplace_digest256 completion{};
    ASSERT_EQ(producer.finish(producer.state, &completion), LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(std::memcmp(
                  completion.bytes, summary.receipt_id.bytes,
                  sizeof(completion.bytes)),
              0);
    laplace_persistence_summary persisted{};
    ASSERT_EQ(laplace_persistence_validate_stream(
                  batches.data(), batches.size(), &persisted),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(persisted.entity_count, summary.unique_entity_count);
    EXPECT_EQ(persisted.physicality_count, summary.unique_physicality_count);
    EXPECT_EQ(persisted.trajectory_segment_count,
              summary.trajectory_vertex_count);
    EXPECT_EQ(persisted.attestation_count, summary.occurrence_count);
    EXPECT_EQ(laplace_composition_working_set_resolve_presence(
                  working_set, &provider, &presence_receipt),
              LAPLACE_COMPOSITION_PRESENCE_ALREADY_APPLIED);

    auto invalid_operands = operands;
    invalid_operands.back().reference_index = 99U;
    auto invalid_input = input;
    invalid_input.operands = invalid_operands.data();
    laplace_composition_working_set* rejected =
        reinterpret_cast<laplace_composition_working_set*>(UINTPTR_MAX);
    EXPECT_EQ(laplace_composition_working_set_create(&invalid_input, &rejected),
              LAPLACE_COMPOSITION_REFERENCE_INVALID);
    EXPECT_EQ(rejected, nullptr);

    auto insufficient_context = context;
    insufficient_context.resource_grant.memory_bytes = 1U;
    auto insufficient_input = input;
    insufficient_input.context = &insufficient_context;
    EXPECT_EQ(laplace_composition_working_set_create(
                  &insufficient_input, &rejected),
              LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT);
    EXPECT_EQ(rejected, nullptr);

    auto colliding_known = std::array<laplace_composition_known_entity, 2>{
        known[0], known[0]};
    colliding_known[0].has_atom = 0U;
    colliding_known[0].atom = 0U;
    colliding_known[0].tier_floor = 1U;
    colliding_known[1] = colliding_known[0];
    colliding_known[1].identity_witness.bytes[20] ^= 0x80U;
    const laplace_composition_operand collision_operand{
        0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U};
    const auto collision_request = Request(0U, 1U, 10U, 0x61U);
    const laplace_composition_working_set_input collision_input{
        &context,
        &source,
        &calculation_recipe,
        colliding_known.data(),
        colliding_known.size(),
        &collision_operand,
        1U,
        &collision_request,
        1U,
        0U,
        0U};
    EXPECT_EQ(laplace_composition_working_set_create(
                  &collision_input, &rejected),
              LAPLACE_COMPOSITION_IDENTITY_COLLISION);
    EXPECT_EQ(rejected, nullptr);

    laplace_composition_working_set_destroy(&working_set);
    EXPECT_EQ(working_set, nullptr);
}

TEST(CompositionWorkingSet, CanonicalCalculationDoesNotImplicitlyEmitOccurrence) {
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(4) * 1024U * 1024U;
    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x11U);
    Fill(calculation_recipe, 0x31U);
    const std::array<laplace_composition_known_entity, 2> known{{
        Atom('a', laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x51U),
        Atom('b', laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x71U)}};
    const std::array<laplace_composition_operand, 2> operands{{
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {1U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U}}};
    auto request = Request(0U, operands.size(), 1U, 0x21U);
    request.flags = 0U;
    const laplace_composition_working_set_input input{
        &context,
        &source,
        &calculation_recipe,
        known.data(),
        known.size(),
        operands.data(),
        operands.size(),
        &request,
        1U,
        256U,
        0U};
    laplace_composition_working_set* working_set = nullptr;
    ASSERT_EQ(laplace_composition_working_set_create(&input, &working_set),
              LAPLACE_COMPOSITION_OK);
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.occurrence_count, 0U);

    PresenceFixture presence{
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(summary.unique_entity_count),
            LAPLACE_COMPOSITION_NOVEL),
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(summary.unique_physicality_count),
            LAPLACE_COMPOSITION_NOVEL),
        2U,
        1U,
        false};
    auto provider = Provider(presence);
    laplace_composition_presence_receipt presence_receipt{};
    ASSERT_EQ(laplace_composition_working_set_resolve_presence(
                  working_set, &provider, &presence_receipt),
              LAPLACE_COMPOSITION_OK);
    laplace_framework_producer_v1 producer{};
    ASSERT_EQ(laplace_composition_working_set_producer(working_set, &producer),
              LAPLACE_COMPOSITION_OK);
    laplace_framework_producer_plan plan{};
    ASSERT_EQ(producer.prepare(
                  producer.state, &context, &source, &calculation_recipe, &plan),
              LAPLACE_FRAMEWORK_OK);
    std::vector<laplace_framework_canonical_batch> batches;
    for (std::uint64_t index = 0U; index < plan.batch_count; ++index) {
        laplace_framework_canonical_batch batch{};
        laplace_digest256 cursor{};
        ASSERT_EQ(producer.next(producer.state, index, &batch, &cursor),
                  LAPLACE_FRAMEWORK_OK);
        batches.push_back(batch);
    }
    laplace_persistence_summary persisted{};
    ASSERT_EQ(laplace_persistence_validate_stream(
                  batches.data(), batches.size(), &persisted),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(persisted.attestation_count, 0U);
    laplace_composition_working_set_destroy(&working_set);
}

TEST(CompositionWorkingSet, PresenceFiltersExactStateBeforePublication) {
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(4) * 1024U * 1024U;
    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x12U);
    Fill(calculation_recipe, 0x32U);
    const std::array<laplace_composition_known_entity, 2> known{{
        Atom('a', laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x52U),
        Atom('b', laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x72U)}};
    const std::array<laplace_composition_operand, 2> operands{{
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {1U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U}}};
    const auto request = Request(0U, operands.size(), 1U, 0x22U);
    const laplace_composition_working_set_input input{
        &context,
        &source,
        &calculation_recipe,
        known.data(),
        known.size(),
        operands.data(),
        operands.size(),
        &request,
        1U,
        256U,
        0U};
    laplace_composition_working_set* working_set = nullptr;
    ASSERT_EQ(laplace_composition_working_set_create(&input, &working_set),
              LAPLACE_COMPOSITION_OK);
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    ASSERT_EQ(summary.unique_entity_count, 3U);
    ASSERT_EQ(summary.unique_physicality_count, 1U);
    ASSERT_EQ(summary.trajectory_vertex_count, 2U);
    PresenceFixture presence{
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(summary.unique_entity_count),
            LAPLACE_COMPOSITION_EXACT_PRESENT),
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(summary.unique_physicality_count),
            LAPLACE_COMPOSITION_EXACT_PRESENT),
        2U,
        1U,
        false};
    auto provider = Provider(presence);
    laplace_composition_presence_receipt presence_receipt{};
    ASSERT_EQ(laplace_composition_working_set_resolve_presence(
                  working_set, &provider, &presence_receipt),
              LAPLACE_COMPOSITION_OK);
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.novel_entity_count, 0U);
    EXPECT_EQ(summary.novel_physicality_count, 0U);
    EXPECT_EQ(summary.novel_trajectory_vertex_count, 0U);
    EXPECT_EQ(summary.stream_record_count, summary.occurrence_count);
    laplace_framework_producer_v1 producer{};
    ASSERT_EQ(laplace_composition_working_set_producer(working_set, &producer),
              LAPLACE_COMPOSITION_OK);
    laplace_framework_producer_plan plan{};
    ASSERT_EQ(producer.prepare(
                  producer.state, &context, &source, &calculation_recipe, &plan),
              LAPLACE_FRAMEWORK_OK);
    std::vector<laplace_framework_canonical_batch> batches;
    for (std::uint64_t index = 0U; index < plan.batch_count; ++index) {
        laplace_framework_canonical_batch batch{};
        laplace_digest256 cursor{};
        ASSERT_EQ(producer.next(producer.state, index, &batch, &cursor),
                  LAPLACE_FRAMEWORK_OK);
        batches.push_back(batch);
    }
    laplace_persistence_summary persisted{};
    ASSERT_EQ(laplace_persistence_validate_stream(
                  batches.data(), batches.size(), &persisted),
              LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(persisted.entity_count, 0U);
    EXPECT_EQ(persisted.physicality_count, 0U);
    EXPECT_EQ(persisted.trajectory_segment_count, 0U);
    EXPECT_EQ(persisted.attestation_count, 1U);
    laplace_composition_working_set_destroy(&working_set);
}

TEST(CompositionWorkingSet, PresenceSemanticsSurviveProviderSubstitution) {
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(4) * 1024U * 1024U;
    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x14U);
    Fill(calculation_recipe, 0x34U);
    const std::array<laplace_composition_known_entity, 2> known{{
        Atom('e', laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x54U),
        Atom('f', laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x74U)}};
    const std::array<laplace_composition_operand, 2> operands{{
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {1U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U}}};
    const auto request = Request(0U, operands.size(), 1U, 0x24U);
    const laplace_composition_working_set_input input{
        &context,
        &source,
        &calculation_recipe,
        known.data(),
        known.size(),
        operands.data(),
        operands.size(),
        &request,
        1U,
        256U,
        0U};

    laplace_composition_working_set* direct_working_set = nullptr;
    laplace_composition_working_set* substituted_working_set = nullptr;
    ASSERT_EQ(laplace_composition_working_set_create(
                  &input, &direct_working_set),
              LAPLACE_COMPOSITION_OK);
    ASSERT_EQ(laplace_composition_working_set_create(
                  &input, &substituted_working_set),
              LAPLACE_COMPOSITION_OK);

    laplace_composition_working_set_summary initial{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  direct_working_set, &initial),
              LAPLACE_COMPOSITION_OK);
    PresenceFixture direct_presence{
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(initial.unique_entity_count),
            LAPLACE_COMPOSITION_NOVEL),
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(initial.unique_physicality_count),
            LAPLACE_COMPOSITION_NOVEL),
        2U,
        1U,
        false,
        0xA1U,
        0xC1U};
    auto substituted_presence = direct_presence;
    substituted_presence.provider_fingerprint_seed = 0xA2U;
    substituted_presence.provider_receipt_seed = 0xC2U;
    auto direct_provider = Provider(direct_presence);
    auto substituted_provider = Provider(substituted_presence);
    laplace_composition_presence_receipt direct_receipt{};
    laplace_composition_presence_receipt substituted_receipt{};
    ASSERT_EQ(laplace_composition_working_set_resolve_presence(
                  direct_working_set, &direct_provider, &direct_receipt),
              LAPLACE_COMPOSITION_OK);
    ASSERT_EQ(laplace_composition_working_set_resolve_presence(
                  substituted_working_set, &substituted_provider,
                  &substituted_receipt),
              LAPLACE_COMPOSITION_OK);

    EXPECT_EQ(std::memcmp(
                  direct_receipt.candidate_fingerprint.bytes,
                  substituted_receipt.candidate_fingerprint.bytes,
                  sizeof(direct_receipt.candidate_fingerprint.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  direct_receipt.disposition_fingerprint.bytes,
                  substituted_receipt.disposition_fingerprint.bytes,
                  sizeof(direct_receipt.disposition_fingerprint.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  direct_receipt.semantic_receipt_id.bytes,
                  substituted_receipt.semantic_receipt_id.bytes,
                  sizeof(direct_receipt.semantic_receipt_id.bytes)),
              0);
    EXPECT_NE(std::memcmp(
                  direct_receipt.execution_receipt_id.bytes,
                  substituted_receipt.execution_receipt_id.bytes,
                  sizeof(direct_receipt.execution_receipt_id.bytes)),
              0);

    laplace_composition_working_set_summary direct_summary{};
    laplace_composition_working_set_summary substituted_summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  direct_working_set, &direct_summary),
              LAPLACE_COMPOSITION_OK);
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  substituted_working_set, &substituted_summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(std::memcmp(
                  direct_summary.presence_receipt_id.bytes,
                  substituted_summary.presence_receipt_id.bytes,
                  sizeof(direct_summary.presence_receipt_id.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  direct_summary.stream_fingerprint.bytes,
                  substituted_summary.stream_fingerprint.bytes,
                  sizeof(direct_summary.stream_fingerprint.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  direct_summary.receipt_id.bytes,
                  substituted_summary.receipt_id.bytes,
                  sizeof(direct_summary.receipt_id.bytes)),
              0);
    EXPECT_EQ(direct_summary.stream_record_count,
              substituted_summary.stream_record_count);
    EXPECT_EQ(direct_summary.stream_byte_count,
              substituted_summary.stream_byte_count);

    laplace_framework_producer_v1 direct_publication{};
    ASSERT_EQ(laplace_composition_working_set_producer(
                  direct_working_set, &direct_publication),
              LAPLACE_COMPOSITION_OK);
    ASSERT_GT(direct_summary.batch_count, 1U);
    PublicationControl interrupted_control{};
    interrupted_control.cancel_after_batches = 1U;
    auto interruption = PublicationController(interrupted_control);
    PublicationSink interrupted_sink{};
    auto interrupted_provider = PublicationProvider(interrupted_sink);
    laplace_framework_producer_receipt interrupted_receipt{};
    ASSERT_EQ(laplace_framework_run_producer(
                  &context, &source, &calculation_recipe,
                  &direct_publication, &interruption,
                  &interrupted_provider, 1U, &interrupted_receipt),
              LAPLACE_FRAMEWORK_PRODUCER_CANCELLED);
    EXPECT_EQ(interrupted_receipt.checkpoint.completed_batches, 1U);
    EXPECT_EQ(interrupted_receipt.stream.effect_disposition,
              LAPLACE_FRAMEWORK_EFFECT_NONE);
    EXPECT_TRUE(interrupted_sink.aborted);
    EXPECT_TRUE(interrupted_sink.bytes.empty());

    PublicationControl replay_control{};
    auto replay = PublicationController(
        replay_control, &interrupted_receipt.checkpoint);
    PublicationSink replay_sink{};
    auto replay_provider = PublicationProvider(replay_sink);
    laplace_framework_producer_receipt replay_receipt{};
    ASSERT_EQ(laplace_framework_run_producer(
                  &context, &source, &calculation_recipe,
                  &direct_publication, &replay,
                  &replay_provider, 1U, &replay_receipt),
              LAPLACE_FRAMEWORK_OK);
    EXPECT_EQ(replay_receipt.replay_verified, 1U);
    EXPECT_EQ(std::memcmp(
                  replay_receipt.replay_checkpoint_id.bytes,
                  interrupted_receipt.checkpoint.checkpoint_id.bytes,
                  sizeof(replay_receipt.replay_checkpoint_id.bytes)),
              0);
    EXPECT_EQ(std::memcmp(
                  replay_receipt.stream.stream_fingerprint.bytes,
                  direct_summary.stream_fingerprint.bytes,
                  sizeof(direct_summary.stream_fingerprint.bytes)),
              0);
    EXPECT_EQ(replay_receipt.stream.total_records,
              direct_summary.stream_record_count);
    EXPECT_EQ(replay_receipt.stream.total_bytes,
              direct_summary.stream_byte_count);
    EXPECT_TRUE(replay_sink.sealed);
    EXPECT_FALSE(replay_sink.aborted);
    EXPECT_EQ(replay_sink.bytes.size(), direct_summary.stream_byte_count);

    laplace_composition_working_set_destroy(&direct_working_set);
    laplace_composition_working_set_destroy(&substituted_working_set);
}

TEST(CompositionWorkingSet, RejectsPartialPresenceProviderBeforePublication) {
    auto context = laplace_test_context(3U);
    context.resource_grant.memory_bytes = UINT64_C(4) * 1024U * 1024U;
    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    Fill(source, 0x13U);
    Fill(calculation_recipe, 0x33U);
    const std::array<laplace_composition_known_entity, 2> known{{
        Atom('c', laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x53U),
        Atom('d', laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x73U)}};
    const std::array<laplace_composition_operand, 2> operands{{
        {0U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U},
        {1U, 1U, 0U, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U}}};
    const auto request = Request(0U, operands.size(), 1U, 0x23U);
    const laplace_composition_working_set_input input{
        &context,
        &source,
        &calculation_recipe,
        known.data(),
        known.size(),
        operands.data(),
        operands.size(),
        &request,
        1U,
        256U,
        0U};
    laplace_composition_working_set* working_set = nullptr;
    ASSERT_EQ(laplace_composition_working_set_create(&input, &working_set),
              LAPLACE_COMPOSITION_OK);
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    PresenceFixture presence{
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(summary.unique_entity_count),
            LAPLACE_COMPOSITION_NOVEL),
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(summary.unique_physicality_count),
            LAPLACE_COMPOSITION_NOVEL),
        2U,
        1U,
        true};
    auto provider = Provider(presence);
    laplace_composition_presence_receipt receipt{};
    EXPECT_EQ(laplace_composition_working_set_resolve_presence(
                  working_set, &provider, &receipt),
              LAPLACE_COMPOSITION_PRESENCE_INVALID);
    EXPECT_EQ(receipt.status, LAPLACE_COMPOSITION_PRESENCE_INVALID);
    ASSERT_EQ(laplace_composition_working_set_summary_get(
                  working_set, &summary),
              LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.presence_applied, 0U);
    EXPECT_EQ(summary.stream_record_count, 0U);
    laplace_framework_producer_v1 producer{};
    EXPECT_EQ(laplace_composition_working_set_producer(working_set, &producer),
              LAPLACE_COMPOSITION_PRESENCE_REQUIRED);
    laplace_composition_working_set_destroy(&working_set);
}
