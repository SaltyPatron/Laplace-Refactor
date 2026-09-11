#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one repair site, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


# A calculated relation is provenance-bearing through its calculation receipt; it
# is not testimony and must not manufacture an independent evidence root merely
# to participate in the native operator. Testimony/standing keeps its root law.
replace_once(
    "engine/src/cognition_observation_candidate_provider.inc",
    """    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION) {\n        return !DigestZero(transition.evidence_root_fingerprint)\n            ? LAPLACE_COGNITION_OPERATOR_SOURCE_DERIVED : 0U;\n    }\n""",
    """    if (transition.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION) {\n        return LAPLACE_COGNITION_OPERATOR_SOURCE_DERIVED;\n    }\n""",
)
replace_once(
    "engine/src/cognition_operator_part00.inc",
    """    if (constraint.source_class != LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY &&\n        Zero(constraint.evidence_root_id)) {\n        return false;\n    }\n    if (constraint.source_class == LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY &&\n        !Zero(constraint.evidence_root_id)) {\n        return false;\n    }\n""",
    """    if (constraint.source_class == LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY &&\n        Zero(constraint.evidence_root_id)) {\n        return false;\n    }\n    if (constraint.source_class == LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY &&\n        !Zero(constraint.evidence_root_id)) {\n        return false;\n    }\n""",
)

# Pin the distinction directly in operator acceptance: rootless derived
# calculation is valid; rootless testimony remains invalid.
replace_once(
    "tests/cognition_operator_tests.cpp",
    """TEST(CognitionOperator, RejectsNegativePrecisionRatherThanEncodingContradiction) {\n""",
    """TEST(CognitionOperator, DerivedCalculationUsesReceiptWithoutManufacturingEvidenceRoot) {\n    ProgramFixture program;\n    const std::vector fields{Field(20U, 0U), Field(21U, 1U)};\n    auto derived = Constraint(\n        30U, 11U, 0U, 1U, LAPLACE_COGNITION_OPERATOR_SOURCE_DERIVED, 80U);\n    derived.evidence_root_id = {};\n    laplace_cognition_operator_receipt receipt{};\n    auto value = Build(&program, fields, {derived}, &receipt);\n    EXPECT_NE(value.value, nullptr);\n    EXPECT_EQ(receipt.selected_constraint_count, 1U);\n    EXPECT_EQ(receipt.derived_constraint_count, 1U);\n    EXPECT_EQ(receipt.testimony_constraint_count, 0U);\n}\n\nTEST(CognitionOperator, TestimonyStillRequiresIndependentEvidenceRoot) {\n    ProgramFixture program;\n    const std::vector fields{Field(20U, 0U), Field(21U, 1U)};\n    auto testimony = Constraint(\n        30U, 11U, 0U, 1U, LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY, 80U);\n    testimony.evidence_root_id = {};\n    laplace_cognition_operator* value = nullptr;\n    laplace_cognition_operator_receipt receipt{};\n    EXPECT_EQ(laplace_cognition_operator_create(\n                  &program.value, fields.data(), fields.size(), &testimony, 1U,\n                  &value, &receipt),\n              LAPLACE_COGNITION_OPERATOR_CONSTRAINT_INVALID);\n    EXPECT_EQ(value, nullptr);\n}\n\nTEST(CognitionOperator, RejectsNegativePrecisionRatherThanEncodingContradiction) {\n""",
)

# Finite output capacity is a typed resource boundary. It must publish no prefix
# and report EXHAUSTED while retaining OK transport/execution status.
replace_once(
    "tests/observation_query_tests.cpp",
    """TEST(ObservationCandidateBatch, CapacityFailureDoesNotPublishAPrefix) {\n    const auto fixture = BuildFixture();\n    const auto binding = Binding(fixture.root, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);\n    auto index = CreateIndex(fixture, {binding});\n    laplace_cognition_observation_candidate sentinel;\n    std::memset(&sentinel, 0xa5, sizeof(sentinel));\n    auto output = sentinel;\n    std::size_t count = 99;\n    laplace_cognition_observation_candidate_usage usage{};\n    ASSERT_EQ(laplace_observation_query_index_candidates_batch(index.value, &binding,\n        &fixture.root, 1U, &output, 1U, &count, &usage), LAPLACE_OBSERVATION_QUERY_OVERFLOW);\n    EXPECT_EQ(count, 0U);\n    EXPECT_EQ(std::memcmp(&output, &sentinel, sizeof(output)), 0);\n    EXPECT_EQ(usage.crossing_count, 0U);\n    std::array<laplace_cognition_observation_candidate, 16> complete{};\n    ASSERT_EQ(laplace_observation_query_index_candidates_batch(index.value, &binding,\n        &fixture.root, 1U, complete.data(), complete.size(), &count, &usage),\n        LAPLACE_OBSERVATION_QUERY_OK);\n    EXPECT_GT(count, 1U);\n    EXPECT_EQ(usage.crossing_count, count);\n    EXPECT_EQ(laplace_observation_query_index_candidates_batch(index.value, &binding,\n        &fixture.root, 1U, nullptr, 0U, &count, &usage), LAPLACE_OBSERVATION_QUERY_OVERFLOW);\n    EXPECT_EQ(count, 0U);\n}\n""",
    """TEST(ObservationCandidateBatch, CapacityFailureDoesNotPublishAPrefix) {\n    const auto fixture = BuildFixture();\n    const auto binding = Binding(fixture.root, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);\n    auto index = CreateIndex(fixture, {binding});\n    laplace_cognition_observation_candidate sentinel;\n    std::memset(&sentinel, 0xa5, sizeof(sentinel));\n    auto output = sentinel;\n    std::size_t count = 99;\n    laplace_cognition_observation_candidate_usage usage{};\n    ASSERT_EQ(laplace_observation_query_index_candidates_batch(index.value, &binding,\n        &fixture.root, 1U, &output, 1U, &count, &usage), LAPLACE_OBSERVATION_QUERY_OK);\n    EXPECT_EQ(count, 0U);\n    EXPECT_EQ(std::memcmp(&output, &sentinel, sizeof(output)), 0);\n    EXPECT_EQ(usage.crossing_count, 0U);\n    EXPECT_EQ(usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);\n    std::array<laplace_cognition_observation_candidate, 16> complete{};\n    ASSERT_EQ(laplace_observation_query_index_candidates_batch(index.value, &binding,\n        &fixture.root, 1U, complete.data(), complete.size(), &count, &usage),\n        LAPLACE_OBSERVATION_QUERY_OK);\n    EXPECT_GT(count, 1U);\n    EXPECT_EQ(usage.crossing_count, count);\n    EXPECT_EQ(laplace_observation_query_index_candidates_batch(index.value, &binding,\n        &fixture.root, 1U, nullptr, 0U, &count, &usage), LAPLACE_OBSERVATION_QUERY_OK);\n    EXPECT_EQ(count, 0U);\n    EXPECT_EQ(usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);\n}\n""",
)
replace_once(
    "tests/cognition_prompt_structural_provider_tests.cpp",
    """TEST(CognitionPromptStructuralProvider, CapacityFailurePublishesNoPrefixOrFalseAbsence) {\n    PromptIndexFixture fixture(\"ababa\");\n    ASSERT_NE(fixture.provider.state, nullptr);\n    const auto binding = Binding(fixture.view.trunk_entity_id, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);\n    std::array<laplace_cognition_observation_candidate, 1> buffer{};\n    buffer[0].multiplicity = 9876U;\n    buffer[0].target_entity_id = Codepoint('z');\n    const auto before = CandidateFields(buffer[0]);\n    laplace_query_search_state state{};\n    std::uint64_t cost = 0U;\n    std::size_t count = 999U;\n    laplace_cognition_observation_candidate_usage usage{};\n    EXPECT_EQ(fixture.provider.enumerate_candidates(\n        fixture.provider.state, &binding, &fixture.view.trunk_entity_id, &state, &cost,\n        1U, buffer.data(), buffer.size(), &count, &usage), 0);\n    EXPECT_EQ(count, 0U);\n    EXPECT_EQ(usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN);\n    EXPECT_EQ(CandidateFields(buffer[0]), before);\n    EXPECT_GT(usage.rows_examined, UINT64_C(0));\n    EXPECT_GT(usage.index_plan_count, UINT64_C(0));\n    EXPECT_EQ(usage.crossing_count, UINT64_C(0));\n    const auto empty = Query(fixture.provider, binding, {fixture.view.trunk_entity_id}, 0U, 0U);\n    EXPECT_EQ(empty.status, 0);\n    EXPECT_TRUE(empty.values.empty());\n    EXPECT_EQ(empty.usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN);\n}\n""",
    """TEST(CognitionPromptStructuralProvider, CapacityFailurePublishesNoPrefixOrFalseAbsence) {\n    PromptIndexFixture fixture(\"ababa\");\n    ASSERT_NE(fixture.provider.state, nullptr);\n    const auto binding = Binding(fixture.view.trunk_entity_id, LAPLACE_OBSERVATION_QUERY_CONSTITUENT);\n    std::array<laplace_cognition_observation_candidate, 1> buffer{};\n    buffer[0].multiplicity = 9876U;\n    buffer[0].target_entity_id = Codepoint('z');\n    const auto before = CandidateFields(buffer[0]);\n    laplace_query_search_state state{};\n    std::uint64_t cost = 0U;\n    std::size_t count = 999U;\n    laplace_cognition_observation_candidate_usage usage{};\n    EXPECT_EQ(fixture.provider.enumerate_candidates(\n        fixture.provider.state, &binding, &fixture.view.trunk_entity_id, &state, &cost,\n        1U, buffer.data(), buffer.size(), &count, &usage), 0);\n    EXPECT_EQ(count, 0U);\n    EXPECT_EQ(usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);\n    EXPECT_EQ(CandidateFields(buffer[0]), before);\n    EXPECT_GT(usage.rows_examined, UINT64_C(0));\n    EXPECT_GT(usage.index_plan_count, UINT64_C(0));\n    EXPECT_EQ(usage.crossing_count, UINT64_C(0));\n    const auto empty = Query(fixture.provider, binding, {fixture.view.trunk_entity_id}, 0U, 0U);\n    EXPECT_EQ(empty.status, 0);\n    EXPECT_TRUE(empty.values.empty());\n    EXPECT_EQ(empty.usage.limiting_disposition, LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED);\n}\n""",
)

# Native path mathematics is now part of the observation answer contract.
replace_once(
    "tests/cognition_observation_external_provider_tests.cpp",
    """    EXPECT_EQ(answer.flags, 0U);\n""",
    """    EXPECT_EQ(\n        answer.flags,\n        LAPLACE_COGNITION_OBSERVATION_ANSWER_OPERATOR_EXECUTED);\n""",
)
replace_once(
    "tests/cognition_observation_external_provider_tests.cpp",
    """    EXPECT_EQ(\n        first.flags,\n        LAPLACE_COGNITION_OBSERVATION_ANSWER_RELATION_ID_PRESENT);\n""",
    """    EXPECT_EQ(\n        first.flags,\n        LAPLACE_COGNITION_OBSERVATION_ANSWER_RELATION_ID_PRESENT |\n            LAPLACE_COGNITION_OBSERVATION_ANSWER_OPERATOR_EXECUTED);\n""",
)

# Evidence-precision identity now binds both selected constraint precision and the
# exact boundary set. Solver regularization is intentionally excluded.
replace_once(
    "tests/cognition_solver_tests.cpp",
    """bool Same(const laplace_digest256& left, const laplace_digest256& right) {\n    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;\n}\n""",
    """bool Same(const laplace_digest256& left, const laplace_digest256& right) {\n    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;\n}\n\nbool Zero(const laplace_digest256& value) {\n    for (const auto byte : value.bytes) {\n        if (byte != 0U) return false;\n    }\n    return true;\n}\n""",
)
replace_once(
    "tests/cognition_solver_tests.cpp",
    """    EXPECT_TRUE(Same(\n        receipt.evidence_precision_fingerprint,\n        operator_value.receipt.constraint_set_fingerprint));\n    EXPECT_TRUE(std::isfinite(receipt.final_energy));\n""",
    """    EXPECT_FALSE(Zero(receipt.evidence_precision_fingerprint));\n    EXPECT_FALSE(Same(\n        receipt.evidence_precision_fingerprint,\n        operator_value.receipt.constraint_set_fingerprint));\n    EXPECT_TRUE(std::isfinite(receipt.final_energy));\n""",
)
replace_once(
    "tests/cognition_solver_tests.cpp",
    """    EXPECT_TRUE(Same(\n        receipt.evidence_precision_fingerprint,\n        operator_value.receipt.constraint_set_fingerprint));\n    EXPECT_DOUBLE_EQ(receipt.regularization, 0.5);\n""",
    """    EXPECT_FALSE(Zero(receipt.evidence_precision_fingerprint));\n    EXPECT_FALSE(Same(\n        receipt.evidence_precision_fingerprint,\n        operator_value.receipt.constraint_set_fingerprint));\n    EXPECT_DOUBLE_EQ(receipt.regularization, 0.5);\n\n    auto alternate_program = SolverProgram(operator_value.receipt, 0.25);\n    std::array<double, 2> alternate_solution{};\n    laplace_cognition_solver_receipt alternate_receipt{};\n    ASSERT_EQ(laplace_cognition_solver_execute(\n                  operator_value.value, &alternate_program, initial.data(), initial.size(),\n                  alternate_solution.data(), alternate_solution.size(), &alternate_receipt),\n              LAPLACE_COGNITION_SOLVER_OK);\n    EXPECT_TRUE(Same(\n        receipt.evidence_precision_fingerprint,\n        alternate_receipt.evidence_precision_fingerprint));\n""",
)

# Each UCDXML claim node depends on the same source root; the claim node is not
# itself the source root. Preserve that distinction in the hosted execution proof.
replace_once(
    ".github/workflows/ci.yml",
    """          jq -s -e '\n            length == 3 and\n            any(.[]; .property == \"na\" and .value == \"LATIN CAPITAL LETTER A\") and\n            any(.[]; .property == \"sc\" and .value == \"Latn\") and\n            any(.[]; .property == \"gc\" and .value == \"Lu\") and\n            all(.[];\n              (.proposition_entity_id | length) == 32 and\n              (.occurrence_id | length) == 64 and\n              (.evidence_node_id | length) == 64 and\n              (.evidence_root_id | length) == 64 and\n              (.testimony_id | length) == 64 and\n              .evidence_node_id == .evidence_root_id and\n              .evidence_source_type == 1)\n          ' \"$latin\"\n""",
    """          jq -s -e '\n            length == 3 and\n            any(.[]; .property == \"na\" and .value == \"LATIN CAPITAL LETTER A\") and\n            any(.[]; .property == \"sc\" and .value == \"Latn\") and\n            any(.[]; .property == \"gc\" and .value == \"Lu\") and\n            all(.[];\n              (.proposition_entity_id | length) == 32 and\n              (.occurrence_id | length) == 64 and\n              (.evidence_node_id | length) == 64 and\n              (.evidence_root_id | length) == 64 and\n              (.testimony_id | length) == 64 and\n              .evidence_node_id != .evidence_root_id and\n              .evidence_source_type == 1) and\n            (map(.evidence_root_id) | unique | length) == 1\n          ' \"$latin\"\n""",
)
replace_once(
    ".github/workflows/ci.yml",
    """          jq -s -e '\n            length == 2 and\n            any(.[]; .property == \"sc\" and .value == \"Hani\") and\n            any(.[]; .property == \"kTotalStrokes\" and .value == \"1\") and\n            all(.[];\n              (.proposition_entity_id | length) == 32 and\n              (.occurrence_id | length) == 64 and\n              (.evidence_root_id | length) == 64 and\n              (.testimony_id | length) == 64 and\n              .evidence_source_type == 1)\n          ' \"$han\"\n""",
    """          jq -s -e '\n            length == 2 and\n            any(.[]; .property == \"sc\" and .value == \"Hani\") and\n            any(.[]; .property == \"kTotalStrokes\" and .value == \"1\") and\n            all(.[];\n              (.proposition_entity_id | length) == 32 and\n              (.occurrence_id | length) == 64 and\n              (.evidence_node_id | length) == 64 and\n              (.evidence_root_id | length) == 64 and\n              (.testimony_id | length) == 64 and\n              .evidence_node_id != .evidence_root_id and\n              .evidence_source_type == 1) and\n            (map(.evidence_root_id) | unique | length) == 1\n          ' \"$han\"\n""",
)

print("final convergence CI semantic repair applied")
