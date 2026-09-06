#include "laplace/composition_execution.h"
#include "laplace/tabular_source.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "context_fixture.h"
#include "sha256_internal.hpp"

namespace {

void Fill(laplace_digest256& value, const std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

void Fill(laplace_id128& value, const std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

bool SameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_source_profile_manifest Declaration(const std::uint8_t seed) {
    laplace_source_profile_manifest value{};
    value.coordinate.kind = 17U;
    Fill(value.coordinate.authority, seed);
    Fill(value.coordinate.release, static_cast<std::uint8_t>(seed + 0x10U));
    Fill(value.coordinate.name_space, static_cast<std::uint8_t>(seed + 0x20U));
    Fill(value.coordinate.local_identifier, static_cast<std::uint8_t>(seed + 0x30U));
    value.coordinate.version = 1U;
    Fill(value.authority_release_fingerprint, static_cast<std::uint8_t>(seed + 0x40U));
    Fill(value.license_fingerprint, static_cast<std::uint8_t>(seed + 0x41U));
    Fill(value.syntax_authority_fingerprint, static_cast<std::uint8_t>(seed + 0x42U));
    Fill(value.recipe_program_fingerprint, static_cast<std::uint8_t>(seed + 0x43U));
    Fill(value.universal_ast_mapping_fingerprint, static_cast<std::uint8_t>(seed + 0x44U));
    Fill(value.highway_references_fingerprint, static_cast<std::uint8_t>(seed + 0x45U));
    Fill(value.epistemic_witnessing_fingerprint, static_cast<std::uint8_t>(seed + 0x46U));
    Fill(value.denominator_declaration_fingerprint, static_cast<std::uint8_t>(seed + 0x47U));
    Fill(value.conformance_fingerprint, static_cast<std::uint8_t>(seed + 0x48U));
    Fill(value.completion_law_fingerprint, static_cast<std::uint8_t>(seed + 0x49U));
    Fill(value.selected_boundary_fingerprint, static_cast<std::uint8_t>(seed + 0x4AU));
    value.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    return value;
}

struct SourceFixture final {
    std::string text;
    std::array<laplace_tabular_column, 2> columns{};
    std::array<laplace_tabular_fixed_width_field, 2> fixed_fields{};
    laplace_tabular_artifact artifact{};
    laplace_source_profile_manifest declaration{};
    laplace_tabular_source_input input{};

    SourceFixture(const bool fixed_width, const std::uint8_t seed)
        : text(fixed_width
              ? "IDName \n01Alpha\n02Beta \n"
              : "Id\tName\neng\tEnglish\njpn\t日本語\n"),
          declaration(Declaration(seed)) {
        static constexpr std::array<const char*, 2> Names{{"Id", "Name"}};
        for (std::size_t index = 0U; index < columns.size(); ++index) {
            columns[index].bytes = reinterpret_cast<const std::uint8_t*>(Names[index]);
            columns[index].byte_count = std::strlen(Names[index]);
        }

        artifact.bytes = reinterpret_cast<const std::uint8_t*>(text.data());
        artifact.byte_count = text.size();
        artifact.name = fixed_width ? "people.fixed" : "languages.tsv";
        artifact.name_byte_count = std::strlen(artifact.name);
        artifact.media_type = fixed_width
            ? "text/plain; charset=utf-8"
            : "text/tab-separated-values; charset=utf-8";
        artifact.media_type_byte_count = std::strlen(artifact.media_type);
        artifact.columns = columns.data();
        artifact.expected_record_count = 3U;
        artifact.expected_field_count = 6U;
        artifact.expected_column_count = 2U;
        artifact.header_record_count = 1U;
        artifact.outcome_type = 5U;
        artifact.flags = LAPLACE_TABULAR_ARTIFACT_CONTAINER |
            LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION;
        artifact.line_terminator = LAPLACE_TABULAR_TERMINATOR_LF;

        if (fixed_width) {
            fixed_fields[0] = laplace_tabular_fixed_width_field{
                2U, LAPLACE_TABULAR_FIXED_WIDTH_TRIM_LEFT |
                        LAPLACE_TABULAR_FIXED_WIDTH_TRIM_RIGHT};
            fixed_fields[1] = laplace_tabular_fixed_width_field{
                5U, LAPLACE_TABULAR_FIXED_WIDTH_TRIM_LEFT |
                        LAPLACE_TABULAR_FIXED_WIDTH_TRIM_RIGHT};
            artifact.mode = LAPLACE_TABULAR_ARTIFACT_FIXED_WIDTH;
            artifact.fixed_width_fields = fixed_fields.data();
            artifact.padding_byte = static_cast<std::uint32_t>(' ');
            artifact.overflow_field_index =
                LAPLACE_TABULAR_FIXED_WIDTH_NO_OVERFLOW_FIELD;
        } else {
            artifact.mode = LAPLACE_TABULAR_ARTIFACT_DELIMITED;
            artifact.delimiter = static_cast<std::uint32_t>('\t');
        }

        const auto sha = laplace::internal::Sha256(
            artifact.bytes, static_cast<std::size_t>(artifact.byte_count));
        std::memcpy(artifact.expected_sha256, sha.data(), sha.size());
        std::memcpy(artifact.artifact_id.bytes, sha.data(), sha.size());

        EXPECT_EQ(
            laplace_tabular_source_graph_identify(
                &artifact, 1U, nullptr, 0U, nullptr, 0U,
                &declaration.artifact_graph_fingerprint),
            LAPLACE_TABULAR_SOURCE_OK);

        input.profile_declaration = declaration;
        Fill(input.geometry_epoch, static_cast<std::uint8_t>(seed + 0x50U));
        Fill(input.occurrence_context_fingerprint,
             static_cast<std::uint8_t>(seed + 0x60U));
        input.artifacts = &artifact;
        input.artifact_count = 1U;
        input.preferred_batch_bytes = 4096U;
    }
};

struct PlanHandle final {
    laplace_tabular_source_plan* value{};
    ~PlanHandle() { laplace_tabular_source_plan_destroy(&value); }
};

struct WorkingSetHandle final {
    laplace_composition_working_set* value{};
    ~WorkingSetHandle() { laplace_composition_working_set_destroy(&value); }
};

laplace_composition_status ResolveAllNovel(
    void*,
    const laplace_composition_entity_candidate*,
    const std::size_t entity_count,
    const laplace_persistence_physicality_record*,
    const std::size_t physicality_count,
    std::uint8_t* const entity_dispositions,
    std::uint8_t* const physicality_dispositions,
    laplace_composition_presence_provider_result* const result) {
    if (result == nullptr ||
        (entity_count != 0U && entity_dispositions == nullptr) ||
        (physicality_count != 0U && physicality_dispositions == nullptr)) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    std::fill_n(
        entity_dispositions, entity_count,
        static_cast<std::uint8_t>(LAPLACE_COMPOSITION_NOVEL));
    std::fill_n(
        physicality_dispositions, physicality_count,
        static_cast<std::uint8_t>(LAPLACE_COMPOSITION_NOVEL));
    Fill(result->provider_fingerprint, 0xB1U);
    Fill(result->provider_receipt_id, 0xC1U);
    result->returned_entity_count = entity_count;
    result->returned_physicality_count = physicality_count;
    result->entity_round_count = entity_count == 0U ? 0U : 1U;
    result->physicality_round_count = physicality_count == 0U ? 0U : 1U;
    return LAPLACE_COMPOSITION_OK;
}

laplace_composition_known_entity KnownAtom(const std::uint32_t position) {
    laplace_composition_known_entity result{};
    EXPECT_EQ(
        laplace_identity_codepoint_witness(
            position, &result.entity_id, &result.identity_witness),
        LAPLACE_IDENTITY_OK);
    result.physicality_id = result.identity_witness;
    result.centroid.component[position % 4U] = 1.0;
    result.atom = position;
    result.has_atom = 1U;
    return result;
}

struct ExecutionObservation final {
    laplace_digest256 source_fingerprint{};
    laplace_digest256 reconstruction_fingerprint{};
    laplace_digest256 semantic_fingerprint{};
    laplace_composition_working_set_summary summary{};
    std::vector<laplace_execution_work_receipt> frontier_receipts;
    std::vector<std::uint8_t> reconstruction;
};

ExecutionObservation Execute(
    const SourceFixture& fixture,
    const std::uint32_t cpu_slots,
    const std::uint64_t preferred_batch_bytes) {
    auto source_input = fixture.input;
    source_input.preferred_batch_bytes = preferred_batch_bytes;

    PlanHandle plan;
    EXPECT_EQ(
        laplace_tabular_source_plan_create(&source_input, &plan.value),
        LAPLACE_TABULAR_SOURCE_OK);
    EXPECT_NE(plan.value, nullptr);

    laplace_tabular_source_plan_view view{};
    EXPECT_EQ(
        laplace_tabular_source_plan_view_get(plan.value, &view),
        LAPLACE_TABULAR_SOURCE_OK);

    std::vector<laplace_composition_known_entity> known;
    known.reserve(static_cast<std::size_t>(view.atom_count));
    for (std::uint64_t index = 0U; index < view.atom_count; ++index) {
        known.push_back(KnownAtom(view.atom_positions[index]));
    }

    auto context = laplace_test_context(0x31U);
    context.resource_grant.memory_bytes = UINT64_C(128) * 1024U * 1024U;
    context.resource_grant.cpu_slots = cpu_slots;
    context.resource_grant.io_slots = 1U;

    laplace_composition_working_set_input composition_input{};
    composition_input.context = &context;
    composition_input.source_fingerprint = &view.source_fingerprint;
    composition_input.calculation_recipe_fingerprint =
        &view.profile.recipe_program_fingerprint;
    composition_input.known_entities = known.data();
    composition_input.known_entity_count = known.size();
    composition_input.operands = view.operands;
    composition_input.operand_count = view.operand_count;
    composition_input.requests = view.requests;
    composition_input.request_count = view.request_count;
    composition_input.preferred_batch_bytes = preferred_batch_bytes;

    WorkingSetHandle working_set;
    EXPECT_EQ(
        laplace_composition_working_set_create(
            &composition_input, &working_set.value),
        LAPLACE_COMPOSITION_OK);
    EXPECT_NE(working_set.value, nullptr);

    laplace_composition_presence_provider_v1 provider{};
    provider.resolve = ResolveAllNovel;
    provider.abi_major = LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI;
    provider.abi_minor = LAPLACE_COMPOSITION_ABI_MINOR;
    laplace_composition_presence_receipt presence{};
    EXPECT_EQ(
        laplace_composition_working_set_resolve_presence(
            working_set.value, &provider, &presence),
        LAPLACE_COMPOSITION_OK);

    ExecutionObservation observation{};
    observation.source_fingerprint = view.source_fingerprint;
    observation.reconstruction_fingerprint = view.reconstruction_fingerprint;
    EXPECT_EQ(
        laplace_composition_working_set_summary_get(
            working_set.value, &observation.summary),
        LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(
        laplace_composition_working_set_semantic_fingerprint(
            working_set.value, &observation.semantic_fingerprint),
        LAPLACE_COMPOSITION_OK);

    std::size_t receipt_count = 0U;
    const auto* receipts =
        laplace_composition_working_set_frontier_execution_receipts(
            working_set.value, &receipt_count);
    EXPECT_GT(receipt_count, 0U);
    EXPECT_NE(receipts, nullptr);
    if (receipts != nullptr) {
        observation.frontier_receipts.assign(receipts, receipts + receipt_count);
    }

    observation.reconstruction.resize(fixture.artifact.byte_count);
    std::size_t reconstructed_bytes = 0U;
    EXPECT_EQ(
        laplace_tabular_source_recompose_artifact(
            plan.value, 0U, observation.reconstruction.data(),
            observation.reconstruction.size(), &reconstructed_bytes),
        LAPLACE_TABULAR_SOURCE_OK);
    observation.reconstruction.resize(reconstructed_bytes);
    return observation;
}

void ExpectPhysicalPlanInvariant(SourceFixture& fixture) {
    const auto scalar = Execute(fixture, 1U, 1U);
    const auto wide = Execute(fixture, 8U, UINT64_C(1) << 20U);

    ASSERT_FALSE(scalar.frontier_receipts.empty());
    ASSERT_FALSE(wide.frontier_receipts.empty());
    EXPECT_TRUE(SameDigest(
        scalar.source_fingerprint, wide.source_fingerprint));
    EXPECT_TRUE(SameDigest(
        scalar.reconstruction_fingerprint,
        wide.reconstruction_fingerprint));
    EXPECT_TRUE(SameDigest(
        scalar.semantic_fingerprint, wide.semantic_fingerprint));
    EXPECT_TRUE(SameDigest(
        scalar.summary.stream_fingerprint,
        wide.summary.stream_fingerprint));

    EXPECT_EQ(scalar.summary.semantic_calculation_count,
              scalar.summary.request_count);
    EXPECT_EQ(wide.summary.semantic_calculation_count,
              wide.summary.request_count);
    EXPECT_EQ(scalar.summary.request_count, wide.summary.request_count);
    EXPECT_EQ(scalar.summary.unique_entity_count, wide.summary.unique_entity_count);
    EXPECT_EQ(scalar.summary.unique_physicality_count,
              wide.summary.unique_physicality_count);
    EXPECT_EQ(scalar.summary.occurrence_count, wide.summary.occurrence_count);
    EXPECT_EQ(scalar.reconstruction.size(), fixture.artifact.byte_count);
    EXPECT_EQ(wide.reconstruction.size(), fixture.artifact.byte_count);
    EXPECT_EQ(
        scalar.reconstruction,
        std::vector<std::uint8_t>(
            fixture.artifact.bytes,
            fixture.artifact.bytes + fixture.artifact.byte_count));
    EXPECT_EQ(wide.reconstruction, scalar.reconstruction);

    /* The two executions are physically different and must remain visible as
     * such even though their canonical semantic state agrees. */
    EXPECT_FALSE(SameDigest(
        scalar.summary.context_fingerprint,
        wide.summary.context_fingerprint));
    EXPECT_FALSE(SameDigest(
        scalar.frontier_receipts.front().grant_fingerprint,
        wide.frontier_receipts.front().grant_fingerprint));
    EXPECT_NE(scalar.summary.batch_count, wide.summary.batch_count);
}

}  // namespace

TEST(SourceAdmissionInvariance, DelimitedSourceSurvivesWorkerAndDepositPlanChanges) {
    SourceFixture fixture(false, 0x11U);
    ExpectPhysicalPlanInvariant(fixture);
}

TEST(SourceAdmissionInvariance, FixedWidthSourceSurvivesWorkerAndDepositPlanChanges) {
    SourceFixture fixture(true, 0x21U);
    ExpectPhysicalPlanInvariant(fixture);
}
