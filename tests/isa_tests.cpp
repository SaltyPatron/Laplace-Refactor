#include "laplace/isa.h"
#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/highway.h"
#include "laplace/reference_mapping.h"
#include "laplace/reference_topology.h"
#include "laplace/source_profile.h"
#include "laplace/trajectory.h"
#include "laplace/world_admission.h"
#include "context_fixture.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace {

laplace_isa_value_view InputView(std::uint32_t* data, std::size_t count) {
    return laplace_isa_value_view{
        data,
        static_cast<std::uint64_t>(count),
        static_cast<std::uint64_t>(count),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_U32_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view OutputView(laplace_id128* data, std::size_t capacity) {
    return laplace_isa_value_view{
        data,
        0u,
        static_cast<std::uint64_t>(capacity),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_ID128_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view TrajectoryInputView(
    laplace_trajectory_carrier* data,
    std::size_t count) {
    return laplace_isa_value_view{
        data,
        static_cast<std::uint64_t>(count),
        static_cast<std::uint64_t>(count),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view OccurrenceOutputView(
    laplace_composition_occurrence* data,
    std::size_t capacity) {
    return laplace_isa_value_view{
        data,
        0u,
        static_cast<std::uint64_t>(capacity),
        static_cast<std::uint32_t>(sizeof(*data)),
        LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR,
        LAPLACE_ISA_KNOWN_VALUE_FLAGS,
        0u};
}

laplace_isa_value_view HighwayInputView(
    laplace_highway_key* data,
    std::size_t count) {
    return {data, static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(count),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_HIGHWAY_KEY_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view HighwayOutputView(
    laplace_highway_coordinate* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_HIGHWAY_COORDINATE_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view HighwayRegistryOutputView(
    laplace_highway_registry_receipt* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_HIGHWAY_REGISTRY_RECEIPT_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view EvidenceInputView(
    laplace_evidence_lineage_record* data,
    std::size_t count) {
    return {data, static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(count),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_EVIDENCE_LINEAGE_RECORD_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view EvidenceOutputView(
    laplace_evidence_root_record* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_EVIDENCE_ROOT_RECORD_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view TestimonyInputView(
    laplace_evidence_testimony_record* data,
    std::size_t count) {
    return {data, static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(count),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECORD_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view TestimonyOutputView(
    laplace_evidence_testimony_receipt* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECEIPT_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view SourceProfileInputView(
    laplace_source_profile_manifest* data,
    std::size_t count) {
    return {data, static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(count),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_SOURCE_PROFILE_MANIFEST_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view SourceProfileOutputView(
    laplace_source_profile_receipt* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_SOURCE_PROFILE_RECEIPT_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view WorldAdmissionInputView(
    laplace_world_admission_record* data,
    std::size_t count) {
    return {data, static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(count),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECORD_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view WorldAdmissionOutputView(
    laplace_world_admission_receipt* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECEIPT_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view ReferenceCandidateInputView(
    laplace_reference_candidate* data,
    std::size_t count) {
    return {data, static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(count),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_REFERENCE_CANDIDATE_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view ReferenceRecordOutputView(
    laplace_reference_record* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_REFERENCE_RECORD_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view ReferenceMappingCandidateInputView(
    laplace_reference_mapping_candidate* data,
    std::size_t count) {
    return {data, static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(count),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_REFERENCE_MAPPING_CANDIDATE_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_value_view ReferenceMappingRecordOutputView(
    laplace_reference_mapping_record* data,
    std::size_t capacity) {
    return {data, 0u, static_cast<std::uint64_t>(capacity),
            static_cast<std::uint32_t>(sizeof(*data)),
            LAPLACE_ISA_VALUE_REFERENCE_MAPPING_RECORD_VECTOR,
            LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u};
}

laplace_isa_instruction IdentityInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return laplace_isa_instruction{
        LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH,
        input,
        output,
        LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction TrajectoryDecodeInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return laplace_isa_instruction{
        LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        input,
        output,
        LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction HighwayInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_HIGHWAY_COORDINATE_CALCULATE_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_COORDINATE_CALCULATE_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction HighwayRegistryInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_REGISTRY_MATERIALIZE_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction EvidenceInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_LINEAGE_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_LINEAGE_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction TestimonyInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_TESTIMONY_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_TESTIMONY_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction SourceProfileInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_SOURCE_PROFILE_VALIDATE_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_SOURCE_PROFILE_VALIDATE_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction WorldAdmissionInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_WORLD_ADMISSION_CLOSE_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_WORLD_ADMISSION_CLOSE_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction ReferenceTopologyInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_REFERENCE_TOPOLOGY_RESOLVE_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_REFERENCE_TOPOLOGY_RESOLVE_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_isa_instruction ReferenceMappingInstruction(
    std::uint32_t input,
    std::uint32_t output) {
    return {LAPLACE_ISA_OPCODE_REFERENCE_MAPPING_RESOLVE_BATCH,
            input, output,
            LAPLACE_ISA_INSTRUCTION_VERSION_REFERENCE_MAPPING_RESOLVE_BATCH,
            LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
}

laplace_id128 HighwayId(std::uint8_t seed) {
    laplace_id128 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_digest256 TestimonyDigest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_evidence_testimony_record TestimonyRecord(std::uint8_t seed) {
    laplace_evidence_testimony_record value{};
    value.evidence_node_id = TestimonyDigest(seed);
    value.source_profile_id = TestimonyDigest(0x70u);
    value.recipe_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 1u));
    value.trust_input_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 2u));
    value.outcome_detail_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 3u));
    value.uncertainty_numerator = 1u;
    value.uncertainty_denominator = 5u;
    value.sample_count = 2u;
    value.source_type = LAPLACE_EVIDENCE_SOURCE_STANDARD;
    value.outcome_type = LAPLACE_EVIDENCE_OUTCOME_MAPPING;
    value.disposition = LAPLACE_EVIDENCE_DISPOSITION_PERSISTED;
    value.flags = LAPLACE_EVIDENCE_TESTIMONY_FLAGS_NONE;
    EXPECT_EQ(laplace_evidence_testimony_identify(&value, &value.testimony_id),
              LAPLACE_EVIDENCE_TESTIMONY_OK);
    return value;
}

laplace_highway_key HighwayKey(std::uint32_t kind, std::uint8_t seed);

laplace_source_profile_manifest SourceProfile(std::uint8_t seed) {
    laplace_source_profile_manifest value{};
    value.coordinate = HighwayKey(LAPLACE_HIGHWAY_KIND_SOURCE_PROFILE, seed);
    value.authority_release_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 1u));
    value.license_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 2u));
    value.artifact_graph_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 3u));
    value.syntax_authority_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 4u));
    value.recipe_program_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 5u));
    value.universal_ast_mapping_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 6u));
    value.highway_references_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 7u));
    value.epistemic_witnessing_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 8u));
    value.denominator_declaration_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 9u));
    value.conformance_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 10u));
    value.completion_law_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 11u));
    value.selected_boundary_fingerprint = TestimonyDigest(0xe0u);
    value.byte_count = 32u;
    value.container_count = 1u;
    value.member_count = 1u;
    value.file_count = 1u;
    value.record_count = 1u;
    value.field_count = 2u;
    value.syntax_node_count = 3u;
    value.span_count = 2u;
    value.occurrence_count = 2u;
    value.output_count = 1u;
    value.closure_subject_count = 1u;
    value.persisted_count = 1u;
    value.not_applicable_mask =
        (UINT64_C(1) << 8u) | (UINT64_C(1) << 9u) |
        (UINT64_C(1) << 11u) | (UINT64_C(1) << 12u) |
        (UINT64_C(1) << 13u) | (UINT64_C(1) << 14u) |
        (UINT64_C(1) << 15u);
    value.reconstruction_class = LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT;
    EXPECT_EQ(laplace_source_profile_identify(&value, &value.profile_id),
              LAPLACE_SOURCE_PROFILE_OK);
    return value;
}

laplace_world_admission_record WorldAdmission(std::uint8_t seed) {
    laplace_world_admission_record value{};
    value.source_profile_id = TestimonyDigest(seed);
    value.selected_boundary_fingerprint = TestimonyDigest(0xe0u);
    value.source_profile_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 1u));
    value.recipe_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 2u));
    value.composition_working_set_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 3u));
    value.composition_presence_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 4u));
    value.composition_producer_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 5u));
    value.composition_stream_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 6u));
    value.evidence_lineage_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 7u));
    value.evidence_testimony_receipt_id = TestimonyDigest(static_cast<std::uint8_t>(seed + 8u));
    value.readback_fingerprint = TestimonyDigest(static_cast<std::uint8_t>(seed + 9u));
    value.profile_occurrence_count = 2u;
    value.composition_occurrence_count = 2u;
    value.profile_claim_count = 1u;
    value.evidence_node_count = 1u;
    value.testimony_count = 1u;
    value.profile_bound_testimony_count = 1u;
    value.recipe_bound_testimony_count = 1u;
    value.lineage_bound_testimony_count = 1u;
    value.closure_subject_count = 3u;
    value.closed_subject_count = 3u;
    value.reconstruction_class = 1u;
    EXPECT_EQ(laplace_world_admission_identify(&value, &value.admission_id),
              LAPLACE_WORLD_ADMISSION_OK);
    return value;
}

laplace_highway_key HighwayKey(std::uint32_t kind, std::uint8_t seed) {
    return {kind, 0u, HighwayId(seed), HighwayId(static_cast<std::uint8_t>(seed + 0x10u)),
            HighwayId(static_cast<std::uint8_t>(seed + 0x20u)),
            HighwayId(static_cast<std::uint8_t>(seed + 0x30u)), 1u};
}

laplace_reference_candidate ReferenceCandidate(
    std::uint8_t seed,
    std::uint8_t local,
    std::uint32_t flags) {
    laplace_reference_candidate value{};
    value.source_profile_id = TestimonyDigest(0xd0u);
    value.key = HighwayKey(LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE, seed);
    value.key.local_identifier = HighwayId(local);
    value.row_entity_id = HighwayId(static_cast<std::uint8_t>(seed + 0x40u));
    value.field_entity_id = HighwayId(static_cast<std::uint8_t>(seed + 0x50u));
    value.value_entity_id = value.key.local_identifier;
    value.source_ordinal = static_cast<std::uint64_t>(seed) + 1u;
    value.artifact_ordinal = 1u;
    value.row_ordinal = static_cast<std::uint64_t>(seed) + 1u;
    value.column_ordinal = 1u;
    value.rule_flags = flags;
    return value;
}

laplace_reference_mapping_candidate ReferenceMappingCandidate(
    std::uint8_t witness,
    std::uint8_t left,
    std::uint8_t right) {
    laplace_reference_mapping_candidate value{};
    value.boundary_id = TestimonyDigest(0xe0u);
    value.source_profile_id = TestimonyDigest(
        static_cast<std::uint8_t>(0xa0u + witness));
    value.left_reference_id = TestimonyDigest(
        static_cast<std::uint8_t>(0x20u + left));
    value.right_reference_id = TestimonyDigest(
        static_cast<std::uint8_t>(0x40u + right));
    auto left_key = HighwayKey(LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE, left);
    auto right_key = HighwayKey(LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE, right);
    EXPECT_EQ(laplace_highway_coordinate_calculate(
                  &left_key, &value.left_coordinate),
              LAPLACE_HIGHWAY_OK);
    EXPECT_EQ(laplace_highway_coordinate_calculate(
                  &right_key, &value.right_coordinate),
              LAPLACE_HIGHWAY_OK);
    value.relation_id = HighwayId(0x70u);
    value.row_entity_id = HighwayId(
        static_cast<std::uint8_t>(0x80u + witness));
    value.left_field_entity_id = HighwayId(0x90u);
    value.left_value_entity_id = HighwayId(
        static_cast<std::uint8_t>(0xa0u + left));
    value.right_field_entity_id = HighwayId(0xb0u);
    value.right_value_entity_id = HighwayId(
        static_cast<std::uint8_t>(0xc0u + right));
    value.source_ordinal = static_cast<std::uint64_t>(witness) + 1u;
    value.artifact_ordinal = 1u;
    value.row_ordinal = static_cast<std::uint64_t>(witness) + 1u;
    value.relation_version = 1u;
    value.relation_kind = LAPLACE_HIGHWAY_KIND_EXTERNAL_REFERENCE;
    value.flags = LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED;
    value.left_disposition = LAPLACE_REFERENCE_DISPOSITION_PRESENT;
    value.right_disposition = LAPLACE_REFERENCE_DISPOSITION_PRESENT;
    return value;
}

laplace_isa_program Program(
    laplace_isa_instruction* instructions,
    std::size_t instruction_count,
    laplace_isa_value_view* values,
    std::size_t value_count) {
    static const laplace_framework_context context = laplace_test_context(0u);
    return laplace_isa_program{
        instructions,
        values,
        &context,
        static_cast<std::uint64_t>(instruction_count),
        static_cast<std::uint64_t>(value_count),
        LAPLACE_ISA_MAJOR,
        LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL,
        0u};
}

TEST(IsaAbi, ContractAssignmentsAreStable) {
    static_assert(LAPLACE_ISA_MAJOR == 1u);
    static_assert(LAPLACE_ISA_MINOR == 10u);
    static_assert(LAPLACE_ISA_VALUE_U32_VECTOR != LAPLACE_ISA_VALUE_ID128_VECTOR);
    static_assert(sizeof(laplace_isa_digest256) == 32u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH, 0x00020001u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH, 0x00030001u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_HIGHWAY_COORDINATE_CALCULATE_BATCH, 0x00040001u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH, 0x00040002u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_LINEAGE_BATCH, 0x00050001u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_TESTIMONY_BATCH, 0x00050002u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_SOURCE_PROFILE_VALIDATE_BATCH, 0x00060001u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_WORLD_ADMISSION_CLOSE_BATCH, 0x00060002u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_REFERENCE_TOPOLOGY_RESOLVE_BATCH, 0x00060003u);
    EXPECT_EQ(LAPLACE_ISA_OPCODE_REFERENCE_MAPPING_RESOLVE_BATCH, 0x00060004u);
}

TEST(IsaExecution, EvidenceLineageMatchesCanonicalNativeOperationAndReceipt) {
    laplace_evidence_lineage_record root{};
    root.proposition_id = HighwayId(0x21u);
    std::memset(root.occurrence_id.bytes, 0x31, sizeof(root.occurrence_id.bytes));
    std::memset(root.source_id.bytes, 0x41, sizeof(root.source_id.bytes));
    std::memset(root.context_id.bytes, 0x51, sizeof(root.context_id.bytes));
    root.source_ordinal = 1u;
    root.record_kind = LAPLACE_EVIDENCE_RECORD_NODE;
    root.epistemic_kind = LAPLACE_EVIDENCE_KIND_OBSERVED;
    ASSERT_EQ(laplace_evidence_node_identify(&root, &root.node_id),
              LAPLACE_EVIDENCE_LINEAGE_OK);
    auto child = root;
    child.source_id.bytes[0] ^= 0x80u;
    child.source_ordinal = 2u;
    child.epistemic_kind = LAPLACE_EVIDENCE_KIND_TESTIMONY;
    ASSERT_EQ(laplace_evidence_node_identify(&child, &child.node_id),
              LAPLACE_EVIDENCE_LINEAGE_OK);
    std::array<laplace_evidence_lineage_record, 2> nodes{{root, child}};
    std::sort(nodes.begin(), nodes.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.node_id.bytes, right.node_id.bytes,
                           sizeof(left.node_id.bytes)) < 0;
    });
    laplace_evidence_lineage_record edge{};
    edge.node_id = child.node_id;
    edge.parent_node_id = root.node_id;
    edge.record_kind = LAPLACE_EVIDENCE_RECORD_DEPENDENCE_EDGE;
    std::array<laplace_evidence_lineage_record, 3> records{{
        nodes[0], nodes[1], edge}};
    std::array<laplace_evidence_root_record, 2> outputs{};
    std::array<laplace_isa_value_view, 2> values{{
        EvidenceInputView(records.data(), records.size()),
        EvidenceOutputView(outputs.data(), outputs.size())}};
    auto instruction = EvidenceInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, 2u);
    for (const auto& output : outputs) {
        EXPECT_EQ(std::memcmp(output.root_node_id.bytes, root.node_id.bytes,
                              sizeof(root.node_id.bytes)), 0);
    }
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
}

TEST(IsaExecution, EvidenceTestimonyMatchesCanonicalNativeOperationAndReceipt) {
    std::array<laplace_evidence_testimony_record, 2> records{{
        TestimonyRecord(0x10u), TestimonyRecord(0x30u)}};
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.testimony_id.bytes, right.testimony_id.bytes, 32u) < 0;
    });
    laplace_evidence_testimony_receipt native_receipt{};
    laplace_evidence_testimony_error native_error{};
    ASSERT_EQ(laplace_evidence_record_testimony_batch(
                  records.data(), records.size(), &native_receipt, &native_error),
              LAPLACE_EVIDENCE_TESTIMONY_OK);
    laplace_evidence_testimony_receipt output{};
    std::array<laplace_isa_value_view, 2> values{{
        TestimonyInputView(records.data(), records.size()),
        TestimonyOutputView(&output, 1u)}};
    auto instruction = TestimonyInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, 1u);
    EXPECT_EQ(std::memcmp(&output, &native_receipt, sizeof(output)), 0);
    EXPECT_EQ(receipt.executed_instruction_count, 1u);

    const auto original_isa_receipt = receipt;
    records[0].sample_count += 1u;
    ASSERT_EQ(laplace_evidence_testimony_identify(
                  &records[0], &records[0].testimony_id),
              LAPLACE_EVIDENCE_TESTIMONY_OK);
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.testimony_id.bytes, right.testimony_id.bytes, 32u) < 0;
    });
    values[1].count = 0u;
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    EXPECT_NE(std::memcmp(
        original_isa_receipt.input_fingerprint.bytes,
        receipt.input_fingerprint.bytes, 32u), 0);
    EXPECT_NE(std::memcmp(
        original_isa_receipt.receipt_id.bytes,
        receipt.receipt_id.bytes, 32u), 0);

    program.minor = 5u;
    values[1].count = 0u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

TEST(IsaExecution, SourceProfileMatchesCanonicalNativeOperationAndReceipt) {
    std::array<laplace_source_profile_manifest, 2> profiles{{
        SourceProfile(0x10u), SourceProfile(0x50u)}};
    std::sort(profiles.begin(), profiles.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.profile_id.bytes, right.profile_id.bytes, 32u) < 0;
    });
    laplace_source_profile_receipt native_receipt{};
    laplace_source_profile_error native_error{};
    ASSERT_EQ(laplace_source_profile_validate_batch(
                  profiles.data(), profiles.size(), &native_receipt, &native_error),
              LAPLACE_SOURCE_PROFILE_OK);
    laplace_source_profile_receipt output{};
    std::array<laplace_isa_value_view, 2> values{{
        SourceProfileInputView(profiles.data(), profiles.size()),
        SourceProfileOutputView(&output, 1u)}};
    auto instruction = SourceProfileInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, 1u);
    EXPECT_EQ(std::memcmp(&output, &native_receipt, sizeof(output)), 0);
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
    program.minor = 6u;
    values[1].count = 0u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

TEST(IsaExecution, WorldAdmissionMatchesCanonicalNativeOperationAndReceipt) {
    std::array<laplace_world_admission_record, 2> admissions{{
        WorldAdmission(0x10u), WorldAdmission(0x50u)}};
    std::sort(admissions.begin(), admissions.end(), [](const auto& left, const auto& right) {
        return std::memcmp(left.admission_id.bytes, right.admission_id.bytes, 32u) < 0;
    });
    laplace_world_admission_receipt native_receipt{};
    laplace_world_admission_error native_error{};
    ASSERT_EQ(laplace_world_admission_close_batch(
                  admissions.data(), admissions.size(), &native_receipt, &native_error),
              LAPLACE_WORLD_ADMISSION_OK);
    laplace_world_admission_receipt output{};
    std::array<laplace_isa_value_view, 2> values{{
        WorldAdmissionInputView(admissions.data(), admissions.size()),
        WorldAdmissionOutputView(&output, 1u)}};
    auto instruction = WorldAdmissionInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, 1u);
    EXPECT_EQ(std::memcmp(&output, &native_receipt, sizeof(output)), 0);
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
    program.minor = 7u;
    values[1].count = 0u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

TEST(IsaExecution, ReferenceTopologyMatchesCanonicalNativeOperationAndReceipt) {
    constexpr std::uint32_t endpoint = LAPLACE_REFERENCE_RULE_ENDPOINT;
    std::array<laplace_reference_candidate, 3> candidates{{
        ReferenceCandidate(
            0x10u, 0x91u,
            endpoint | LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION),
        ReferenceCandidate(0x20u, 0x91u, endpoint),
        ReferenceCandidate(0x30u, 0x92u, endpoint)}};
    candidates[1].key.authority = candidates[0].key.authority;
    candidates[1].key.release = candidates[0].key.release;
    candidates[1].key.name_space = candidates[0].key.name_space;
    candidates[2].key.authority = candidates[0].key.authority;
    candidates[2].key.release = candidates[0].key.release;
    candidates[2].key.name_space = candidates[0].key.name_space;
    std::array<laplace_reference_record, 3> expected{};
    std::array<laplace_reference_record, 3> outputs{};
    laplace_reference_topology_receipt native_receipt{};
    laplace_reference_topology_error native_error{};
    ASSERT_EQ(laplace_reference_topology_resolve_batch(
                  candidates.data(), candidates.size(), expected.data(),
                  &native_receipt, &native_error),
              LAPLACE_REFERENCE_TOPOLOGY_OK);
    std::array<laplace_isa_value_view, 2> values{{
        ReferenceCandidateInputView(candidates.data(), candidates.size()),
        ReferenceRecordOutputView(outputs.data(), outputs.size())}};
    auto instruction = ReferenceTopologyInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, candidates.size());
    EXPECT_EQ(std::memcmp(outputs.data(), expected.data(), sizeof(outputs)), 0);
    EXPECT_EQ(receipt.executed_instruction_count, 1u);

    program.minor = 8u;
    values[1].count = 0u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

TEST(IsaExecution, ReferenceMappingMatchesCanonicalNativeOperationAndReceipt) {
    std::array<laplace_reference_mapping_candidate, 3> candidates{{
        ReferenceMappingCandidate(0u, 0x01u, 0x02u),
        ReferenceMappingCandidate(1u, 0x01u, 0x02u),
        ReferenceMappingCandidate(2u, 0x03u, 0x04u)}};
    std::array<laplace_reference_mapping_record, 3> expected{};
    std::array<laplace_reference_mapping_record, 3> outputs{};
    laplace_reference_mapping_receipt native_receipt{};
    laplace_reference_mapping_error native_error{};
    ASSERT_EQ(laplace_reference_mapping_resolve_batch(
                  candidates.data(), candidates.size(), expected.data(),
                  &native_receipt, &native_error),
              LAPLACE_REFERENCE_MAPPING_OK);
    std::array<laplace_isa_value_view, 2> values{{
        ReferenceMappingCandidateInputView(candidates.data(), candidates.size()),
        ReferenceMappingRecordOutputView(outputs.data(), outputs.size())}};
    auto instruction = ReferenceMappingInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, candidates.size());
    EXPECT_EQ(std::memcmp(outputs.data(), expected.data(), sizeof(outputs)), 0);
    EXPECT_EQ(receipt.executed_instruction_count, 1u);

    const auto original_receipt = receipt;
    candidates[2].right_value_entity_id.bytes[0] ^= 0x80u;
    values[1].count = 0u;
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    EXPECT_NE(std::memcmp(original_receipt.input_fingerprint.bytes,
                          receipt.input_fingerprint.bytes, 32u), 0);
    EXPECT_NE(std::memcmp(original_receipt.receipt_id.bytes,
                          receipt.receipt_id.bytes, 32u), 0);

    program.minor = 9u;
    values[1].count = 0u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

TEST(IsaExecution, HighwayBatchMatchesCanonicalNativeOperationAndReceipt) {
    std::array<laplace_highway_key, 3> keys{{
        HighwayKey(LAPLACE_HIGHWAY_KIND_LANGUAGE, 0x10u),
        HighwayKey(LAPLACE_HIGHWAY_KIND_RECIPE, 0x20u),
        HighwayKey(LAPLACE_HIGHWAY_KIND_EFFECT, 0x30u)}};
    std::array<laplace_highway_coordinate, 3> outputs{};
    std::array<laplace_isa_value_view, 2> values{{
        HighwayInputView(keys.data(), keys.size()),
        HighwayOutputView(outputs.data(), outputs.size())}};
    auto instruction = HighwayInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, keys.size());
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
    for (std::size_t index = 0; index < keys.size(); ++index) {
        laplace_highway_coordinate expected{};
        ASSERT_EQ(laplace_highway_coordinate_calculate(&keys[index], &expected),
                  LAPLACE_HIGHWAY_OK);
        EXPECT_EQ(std::memcmp(&expected, &outputs[index], sizeof(expected)), 0);
    }

    const auto original_receipt = receipt;
    keys[1].release.bytes[0] ^= 0x80u;
    values[1].count = 0u;
    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    EXPECT_NE(std::memcmp(original_receipt.input_fingerprint.bytes,
                          receipt.input_fingerprint.bytes,
                          sizeof(receipt.input_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(original_receipt.receipt_id.bytes,
                          receipt.receipt_id.bytes,
                          sizeof(receipt.receipt_id.bytes)), 0);
}

TEST(IsaExecution, HighwayRegistryMaterializationUsesTheCanonicalLifecycleOperation) {
    std::array<std::uint32_t, 2> versions{{
        LAPLACE_HIGHWAY_REGISTRY_VERSION,
        LAPLACE_HIGHWAY_REGISTRY_VERSION}};
    std::array<laplace_highway_registry_receipt, 2> outputs{};
    std::array<laplace_isa_value_view, 2> values{{
        InputView(versions.data(), versions.size()),
        HighwayRegistryOutputView(outputs.data(), outputs.size())}};
    auto instruction = HighwayRegistryInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(values[1].count, versions.size());
    laplace_highway_registry_receipt expected{};
    ASSERT_EQ(laplace_highway_registry_materialize(program.context, &expected),
              LAPLACE_HIGHWAY_OK);
    EXPECT_EQ(std::memcmp(&outputs[0], &expected, sizeof(expected)), 0);
    EXPECT_EQ(std::memcmp(&outputs[1], &expected, sizeof(expected)), 0);

    versions[1] += 1u;
    values[1].count = 0u;
    std::memset(outputs.data(), 0xa5, sizeof(outputs));
    const auto before = outputs;
    EXPECT_EQ(laplace_isa_execute(&program, &receipt, &error),
              LAPLACE_ISA_INPUT_OUT_OF_RANGE);
    EXPECT_EQ(std::memcmp(outputs.data(), before.data(), sizeof(outputs)), 0);
}

TEST(IsaContext, IsMandatoryAndBoundToProgramAndReceiptIdentity) {
    std::uint32_t position = 0x41u;
    laplace_id128 output_a{};
    laplace_id128 output_b{};
    std::array<laplace_isa_value_view, 2> values_a{{
        InputView(&position, 1u), OutputView(&output_a, 1u)}};
    std::array<laplace_isa_value_view, 2> values_b{{
        InputView(&position, 1u), OutputView(&output_b, 1u)}};
    auto instruction_a = IdentityInstruction(0u, 1u);
    auto instruction_b = IdentityInstruction(0u, 1u);
    auto program_a = Program(&instruction_a, 1u, values_a.data(), values_a.size());
    auto program_b = Program(&instruction_b, 1u, values_b.data(), values_b.size());
    const auto context_a = laplace_test_context(0u);
    const auto context_b = laplace_test_context(1u);
    program_a.context = &context_a;
    program_b.context = &context_b;
    laplace_isa_receipt receipt_a{};
    laplace_isa_receipt receipt_b{};
    laplace_isa_error error{};

    program_a.context = nullptr;
    EXPECT_EQ(laplace_isa_validate(&program_a, &error), LAPLACE_ISA_CONTEXT_INVALID);
    program_a.context = &context_a;
    ASSERT_EQ(laplace_isa_execute(&program_a, &receipt_a, &error), LAPLACE_ISA_OK);
    ASSERT_EQ(laplace_isa_execute(&program_b, &receipt_b, &error), LAPLACE_ISA_OK);
    EXPECT_NE(std::memcmp(receipt_a.context_fingerprint.bytes,
                          receipt_b.context_fingerprint.bytes,
                          sizeof(receipt_a.context_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.program_fingerprint.bytes,
                          receipt_b.program_fingerprint.bytes,
                          sizeof(receipt_a.program_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_b.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);
}

TEST(IsaExecution, CodepointBatchMatchesCanonicalIdentity) {
    std::array<std::uint32_t, 5> positions{{0u, 0x41u, 0xd800u, 0x10000u, 0x10ffffu}};
    std::array<laplace_id128, 5> outputs{};
    std::array<laplace_isa_value_view, 2> values{{
        InputView(positions.data(), positions.size()),
        OutputView(outputs.data(), outputs.size())}};
    auto instruction = IdentityInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    EXPECT_EQ(values[1].count, positions.size());
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
    EXPECT_EQ(receipt.status, LAPLACE_ISA_OK);
    EXPECT_EQ(error.status, LAPLACE_ISA_OK);
    for (std::size_t index = 0; index < positions.size(); ++index) {
        laplace_id128 expected{};
        ASSERT_EQ(laplace_identity_codepoint(positions[index], &expected),
                  LAPLACE_IDENTITY_OK);
        EXPECT_TRUE(laplace_identity_equal(&outputs[index], &expected));
    }
}

TEST(IsaExecution, OneElementAndManyElementProgramsAgree) {
    std::array<std::uint32_t, 3> positions{{0x61u, 0x732bu, 0x1f680u}};
    std::array<laplace_id128, 3> batch_outputs{};
    std::array<laplace_isa_value_view, 2> batch_values{{
        InputView(positions.data(), positions.size()),
        OutputView(batch_outputs.data(), batch_outputs.size())}};
    auto batch_instruction = IdentityInstruction(0u, 1u);
    auto batch_program =
        Program(&batch_instruction, 1u, batch_values.data(), batch_values.size());
    laplace_isa_receipt batch_receipt{};
    laplace_isa_error batch_error{};
    ASSERT_EQ(laplace_isa_execute(&batch_program, &batch_receipt, &batch_error),
              LAPLACE_ISA_OK);

    for (std::size_t index = 0; index < positions.size(); ++index) {
        laplace_id128 single_output{};
        std::array<laplace_isa_value_view, 2> single_values{{
            InputView(&positions[index], 1u),
            OutputView(&single_output, 1u)}};
        auto single_instruction = IdentityInstruction(0u, 1u);
        auto single_program = Program(
            &single_instruction, 1u, single_values.data(), single_values.size());
        laplace_isa_receipt single_receipt{};
        laplace_isa_error single_error{};
        ASSERT_EQ(laplace_isa_execute(&single_program, &single_receipt, &single_error),
                  LAPLACE_ISA_OK);
        EXPECT_TRUE(laplace_identity_equal(&batch_outputs[index], &single_output));
    }
}

TEST(IsaExecution, TrajectoryDecodeMatchesCanonicalStructuralCalculation) {
    laplace_id128 a{};
    laplace_id128 b{};
    ASSERT_EQ(laplace_identity_codepoint(0x41u, &a), LAPLACE_IDENTITY_OK);
    ASSERT_EQ(laplace_identity_codepoint(0x42u, &b), LAPLACE_IDENTITY_OK);
    std::array<laplace_trajectory_carrier, 2> carriers{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &a, 1u, 1u, 0u, &carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &b, 2u, 3u, 0u, &carriers[1]),
              LAPLACE_TRAJECTORY_OK);
    std::array<laplace_composition_occurrence, 2> expected{};
    std::uint64_t logical_count = 0;
    ASSERT_EQ(laplace_trajectory_composition_decode(
                  carriers.data(), carriers.size(), expected.data(), expected.size(),
                  &logical_count),
              LAPLACE_TRAJECTORY_OK);

    std::array<laplace_composition_occurrence, 2> outputs{};
    std::array<laplace_isa_value_view, 2> values{{
        TrajectoryInputView(carriers.data(), carriers.size()),
        OccurrenceOutputView(outputs.data(), outputs.size())}};
    auto instruction = TrajectoryDecodeInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    ASSERT_EQ(laplace_isa_execute(&program, &receipt, &error), LAPLACE_ISA_OK);
    EXPECT_EQ(values[1].count, carriers.size());
    EXPECT_EQ(receipt.executed_instruction_count, 1u);
    EXPECT_EQ(std::memcmp(outputs.data(), expected.data(), sizeof(outputs)), 0);
}

TEST(IsaVersioning, MinorZeroAcceptsIdentityAndRejectsTrajectoryDecode) {
    std::uint32_t position = 0x41u;
    laplace_id128 identity{};
    std::array<laplace_isa_value_view, 2> identity_values{{
        InputView(&position, 1u),
        OutputView(&identity, 1u)}};
    auto identity_instruction = IdentityInstruction(0u, 1u);
    auto identity_program = Program(
        &identity_instruction, 1u, identity_values.data(), identity_values.size());
    identity_program.minor = 0u;
    laplace_isa_error error{};
    EXPECT_EQ(laplace_isa_validate(&identity_program, &error), LAPLACE_ISA_OK);

    laplace_trajectory_carrier carrier{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &identity, 1u, 1u, 0u, &carrier),
              LAPLACE_TRAJECTORY_OK);
    laplace_composition_occurrence occurrence{};
    std::array<laplace_isa_value_view, 2> trajectory_values{{
        TrajectoryInputView(&carrier, 1u),
        OccurrenceOutputView(&occurrence, 1u)}};
    auto trajectory_instruction = TrajectoryDecodeInstruction(0u, 1u);
    auto trajectory_program = Program(
        &trajectory_instruction, 1u, trajectory_values.data(),
        trajectory_values.size());
    trajectory_program.minor = 0u;
    EXPECT_EQ(laplace_isa_validate(&trajectory_program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
}

TEST(IsaValidation, InvalidLaterTrajectoryCannotPartiallyWriteResults) {
    laplace_id128 id{};
    ASSERT_EQ(laplace_identity_codepoint(0x41u, &id), LAPLACE_IDENTITY_OK);
    std::array<laplace_trajectory_carrier, 2> carriers{};
    ASSERT_EQ(laplace_trajectory_composition_encode(
                  &id, 1u, 1u, 0u, &carriers[0]),
              LAPLACE_TRAJECTORY_OK);
    carriers[1] = {};
    std::array<laplace_composition_occurrence, 2> outputs{};
    std::memset(outputs.data(), 0xa5, sizeof(outputs));
    const auto before = outputs;
    std::array<laplace_isa_value_view, 2> values{{
        TrajectoryInputView(carriers.data(), carriers.size()),
        OccurrenceOutputView(outputs.data(), outputs.size())}};
    auto instruction = TrajectoryDecodeInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    EXPECT_EQ(laplace_isa_execute(&program, &receipt, &error),
              LAPLACE_ISA_INPUT_OUT_OF_RANGE);
    EXPECT_EQ(receipt.executed_instruction_count, 0u);
    EXPECT_EQ(values[1].count, 0u);
    EXPECT_EQ(std::memcmp(outputs.data(), before.data(), sizeof(outputs)), 0);
}

TEST(IsaValidation, LaterInvalidInstructionCannotPartiallyExecuteEarlierWork) {
    std::uint32_t valid_position = 0x41u;
    std::uint32_t invalid_position = 0x110000u;
    laplace_id128 first_output{};
    laplace_id128 second_output{};
    std::memset(&first_output, 0xa5, sizeof(first_output));
    std::memset(&second_output, 0x5a, sizeof(second_output));
    const auto first_before = first_output;
    const auto second_before = second_output;
    std::array<laplace_isa_value_view, 4> values{{
        InputView(&valid_position, 1u),
        OutputView(&first_output, 1u),
        InputView(&invalid_position, 1u),
        OutputView(&second_output, 1u)}};
    std::array<laplace_isa_instruction, 2> instructions{{
        IdentityInstruction(0u, 1u),
        IdentityInstruction(2u, 3u)}};
    auto program = Program(
        instructions.data(), instructions.size(), values.data(), values.size());
    laplace_isa_receipt receipt{};
    laplace_isa_error error{};

    EXPECT_EQ(laplace_isa_execute(&program, &receipt, &error),
              LAPLACE_ISA_INPUT_OUT_OF_RANGE);
    EXPECT_EQ(error.instruction_index, 1u);
    EXPECT_EQ(receipt.executed_instruction_count, 0u);
    EXPECT_EQ(values[1].count, 0u);
    EXPECT_EQ(values[3].count, 0u);
    EXPECT_EQ(std::memcmp(&first_output, &first_before, sizeof(first_output)), 0);
    EXPECT_EQ(std::memcmp(&second_output, &second_before, sizeof(second_output)), 0);
}

TEST(IsaValidation, RejectsUnknownOpcodeVersionTypeCapacityAndOverlap) {
    std::uint32_t position = 0x41u;
    laplace_id128 wrong_input{};
    laplace_id128 output{};
    std::array<laplace_isa_value_view, 2> values{{
        InputView(&position, 1u),
        OutputView(&output, 1u)}};
    auto instruction = IdentityInstruction(0u, 1u);
    auto program = Program(&instruction, 1u, values.data(), values.size());
    laplace_isa_error error{};

    instruction.opcode += 1u;
    EXPECT_EQ(laplace_isa_validate(&program, &error), LAPLACE_ISA_UNKNOWN_OPCODE);
    instruction = IdentityInstruction(0u, 1u);
    instruction.version += 1u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_UNSUPPORTED_INSTRUCTION_VERSION);
    instruction = IdentityInstruction(0u, 1u);
    values[0] = OutputView(&wrong_input, 1u);
    values[0].count = 1u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_VALUE_TYPE_MISMATCH);
    values[0] = InputView(&position, 1u);
    values[1].capacity = 0u;
    EXPECT_EQ(laplace_isa_validate(&program, &error),
              LAPLACE_ISA_RESULT_CAPACITY_INSUFFICIENT);

    alignas(laplace_id128) std::array<std::uint8_t, sizeof(laplace_id128)> shared{};
    values[0] = InputView(reinterpret_cast<std::uint32_t*>(shared.data()), 1u);
    values[1] = OutputView(reinterpret_cast<laplace_id128*>(shared.data()), 1u);
    EXPECT_EQ(laplace_isa_validate(&program, &error), LAPLACE_ISA_VALUE_OVERLAP);
}

TEST(IsaReceipt, RepeatedExecutionIsDeterministicAndInputSensitive) {
    std::array<std::uint32_t, 2> positions_a{{0x41u, 0x42u}};
    std::array<std::uint32_t, 2> positions_b = positions_a;
    std::array<laplace_id128, 2> outputs_a{};
    std::array<laplace_id128, 2> outputs_b{};
    std::array<laplace_isa_value_view, 2> values_a{{
        InputView(positions_a.data(), positions_a.size()),
        OutputView(outputs_a.data(), outputs_a.size())}};
    std::array<laplace_isa_value_view, 2> values_b{{
        InputView(positions_b.data(), positions_b.size()),
        OutputView(outputs_b.data(), outputs_b.size())}};
    auto instruction_a = IdentityInstruction(0u, 1u);
    auto instruction_b = IdentityInstruction(0u, 1u);
    auto program_a = Program(&instruction_a, 1u, values_a.data(), values_a.size());
    auto program_b = Program(&instruction_b, 1u, values_b.data(), values_b.size());
    laplace_isa_receipt receipt_a{};
    laplace_isa_receipt receipt_b{};
    laplace_isa_error error_a{};
    laplace_isa_error error_b{};

    ASSERT_EQ(laplace_isa_execute(&program_a, &receipt_a, &error_a), LAPLACE_ISA_OK);
    ASSERT_EQ(laplace_isa_execute(&program_b, &receipt_b, &error_b), LAPLACE_ISA_OK);
    EXPECT_EQ(std::memcmp(&receipt_a, &receipt_b, sizeof(receipt_a)), 0);

    positions_b[1] = 0x43u;
    values_b[1].count = 0u;
    ASSERT_EQ(laplace_isa_execute(&program_b, &receipt_b, &error_b), LAPLACE_ISA_OK);
    EXPECT_NE(std::memcmp(receipt_a.input_fingerprint.bytes,
                          receipt_b.input_fingerprint.bytes,
                          sizeof(receipt_a.input_fingerprint.bytes)), 0);
    EXPECT_NE(std::memcmp(receipt_a.receipt_id.bytes,
                          receipt_b.receipt_id.bytes,
                          sizeof(receipt_a.receipt_id.bytes)), 0);
}

}  // namespace
