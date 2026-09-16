#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import stat
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools/tests/custom_stack_qa.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_custom_stack_qa_tests", MODULE_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load custom-stack QA module")
qa = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = qa
SPEC.loader.exec_module(qa)


class CustomStackQaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = qa.read_json(ROOT / "contracts/custom-stack-qa.json")
        qa.validate_contract(self.contract, ROOT)

    def plan(self, *paths: str) -> dict:
        return qa.build_plan(
            self.contract, ROOT, list(paths), "candidate-sha"
        )

    def test_unrelated_contract_change_keeps_expensive_boundaries_out_of_core(
        self,
    ) -> None:
        plan = self.plan("contracts/trust-matchup-realization.json")
        self.assertEqual(plan["selected_profiles"], [])
        self.assertEqual(plan["selected_physical_tests"], [])
        isolated = {
            row["ctest_name"] for row in self.contract["isolated_tests"]
        }
        self.assertTrue(isolated.isdisjoint(plan["core_tests"]))
        self.assertGreater(plan["core_test_count"], 0)
        self.assertTrue(plan["hosted_antecedent_required"])
        profileless = {
            row["ctest_name"]
            for row in qa.registry_entries(ROOT)
            if row.get("profiles") is None
        }
        self.assertTrue(profileless.isdisjoint(plan["core_tests"]))
        self.assertTrue(
            all(
                "custom-stack" in row.get("profiles", [])
                for row in qa.registry_entries(ROOT)
                if row["ctest_name"] in plan["core_tests"]
            )
        )

    def test_source_runtime_schema_is_required_in_custom_stack_core(self) -> None:
        name = "postgres.source-runtime-state-schema"
        rows = [row for row in qa.registry_entries(ROOT) if row["ctest_name"] == name]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["profiles"], ["postgres", "custom-stack"])
        for path in ("tools/admit_source.py", "tools/admit_source_guard.py",
                     "tools/sources/runtime_state.py",
                     "integrations/postgresql/extension/laplace--version.sql.in"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertIn(name, plan["core_tests"])
                self.assertNotIn(name, plan["manual_source_acceptance_tests"])
                self.assertNotIn(name, plan["selected_physical_tests"])
        # Keep the registry connected to the actual PostgreSQL-enabled CTest
        # declaration and its selected client/server binaries.
        registration = (ROOT / "tests/CMakeLists.txt").read_text()
        self.assertIn("NAME " + name, registration)
        self.assertIn('source_runtime_state_postgres_tests.py"', registration)
        self.assertIn('--pg-bindir "${LAPLACE_POSTGRES_BINDIR}"', registration)

    def test_composition_change_selects_only_composition_boundary(self) -> None:
        plan = self.plan("engine/src/composition.cpp")
        self.assertEqual(
            plan["selected_profiles"], ["composition-boundary"]
        )
        self.assertEqual(
            plan["selected_physical_tests"],
            [
                "postgres.composition-whole-boundary-measurement",
                "postgres.composition-whole-boundary-receipt-contract",
            ],
        )

    def test_highway_revalidation_runs_without_manual_source_acceptance(self) -> None:
        name = "postgres.highway-committed-revalidation-contract"
        for path in (
            "integrations/postgresql/extension/src/highway_registry_revalidate_pg.inc",
            "tests/postgres/unicode_root_contract.sql",
            "engine/src/persistence.c",
        ):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(plan["selected_physical_tests"], [name])
                self.assertNotIn(name, plan["core_tests"])
                self.assertFalse(plan["source_acceptance_authorized"])
                self.assertNotIn(name, plan["manual_source_acceptance_tests"])
                self.assertEqual(plan["required_physical_receipts"],
                                 ["highway_committed_revalidation", "product_cognition_retained_prompt"])
                self.assertEqual(plan["required_physical_receipts_by_test"],
                                 {name: ["highway_committed_revalidation", "product_cognition_retained_prompt"]})

    def test_product_candidate_changes_require_actual_persisted_prompt_replay(self) -> None:
        owner = "postgres.highway-committed-revalidation-contract"
        name = "product_cognition_retained_prompt"
        expected = {
            "schema": "laplace.product-cognition-retained-prompt-test/v1",
            "product_calls": 3, "durable_replay_calls": 2, "output_hex": "41",
            "canonical_root_unchanged": True, "exact_warm_replay": True,
            "warm_publication_reused": True,
            "evidence_lineage_counts_unchanged": True, "completed_steps": 2,
            "replay_physical_provider_batches": 1, "replay_materialization_nodes": 1,
        }
        for path in (
            "tests/postgres/product_cognition_retained_prompt_contract.sql",
            "engine/src/cognition_observation_provider_set.inc",
            "engine/src/cognition_observation_candidate_provider.inc",
            "engine/src/cognition_prompt_structural_provider.inc",
            "engine/src/cognition_firmware.cpp",
            "integrations/postgresql/extension/src/cognition_product_pg.c",
            "integrations/postgresql/extension/src/cognition_firmware_pg.c",
            "integrations/postgresql/extension/src/observation_cognition_persisted_pg.c",
            "integrations/postgresql/extension/src/prompt_admission_pg.c",
            "tools/cognition_firmware_compile.cpp",
        ):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertIn(owner, plan["selected_physical_tests"])
                self.assertNotIn(owner, plan["core_tests"])
                self.assertNotIn(owner, plan["manual_source_acceptance_tests"])
                self.assertFalse(plan["source_acceptance_authorized"])
                self.assertIn(name, plan["required_physical_receipts_by_test"][owner])
                self.assertEqual(
                    plan["required_physical_receipt_fields_by_test"][owner][name], expected)

    def test_perfcache_change_selects_hot_lookup_boundary_once(self) -> None:
        plan = self.plan("engine/src/perfcache.cpp")
        self.assertEqual(
            plan["selected_profiles"], ["unicode-hot-performance"]
        )
        self.assertEqual(
            plan["selected_physical_tests"],
            [],
        )

    def test_unicode_access_change_selects_epoch_and_public_source_boundaries(self) -> None:
        plan = self.plan("integrations/postgresql/extension/src/unicode_access_pg.c")
        self.assertEqual(
            plan["selected_profiles"],
            ["source-admission-suite", "unicode-access-epoch"],
        )
        self.assertEqual(
            plan["selected_physical_tests"],
            [],
        )

    def test_generic_source_admission_change_selects_real_source_regressions(
        self,
    ) -> None:
        plan = self.plan("integrations/postgresql/extension/src/source_admission_pg.c")
        self.assertEqual(
            plan["selected_profiles"], ["source-admission-suite", "verified-cpp-source"],
        )
        self.assertEqual(
            plan["selected_physical_tests"],
            ["postgres.verified-cpp-source-contract"],
        )

    def test_verified_cpp_sources_require_automatic_native_receipt(self):
        name = "postgres.verified-cpp-source-contract"
        for path in (
            "tools/sources/verified_git.py", "tools/sources/qualify_grammar.py",
            "engine/src/tree_sitter_grammar.cpp", "engine/src/decomposition_composition.cpp",
            "integrations/postgresql/extension/src/source_admission_pg.c",
            "integrations/postgresql/extension/src/materialization_pg.c",
            "integrations/postgresql/extension/src/materialization_pg.h",
            "integrations/postgresql/extension/src/spi_context_pg.h",
            "integrations/postgresql/extension/src/spi_budget_pg.h",
            "integrations/postgresql/extension/tests/materialization_provider_pg.c",
            "integrations/postgresql/extension/source_observation_profile.sql.in",
            "tests/postgres/verified_cpp_source_contract.sql",
            ".github/workflows/ci.yml", ".github/workflows/custom-stack.yml",
        ):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertIn(name, plan["selected_physical_tests"])
                self.assertNotIn(name, plan["core_tests"])
                self.assertNotIn(name, plan["manual_source_acceptance_tests"])
                self.assertFalse(plan["source_acceptance_authorized"])
                self.assertIn("verified_cpp_source_admission", plan["required_physical_receipts"])

    def test_iso_profile_change_does_not_select_cili(self) -> None:
        plan = self.plan("contracts/sources/iso-639-3-20260415.json")
        self.assertEqual(plan["selected_profiles"], ["source-admission-suite"])
        self.assertEqual(
            plan["selected_physical_tests"],
            [],
        )

    def test_full_source_acceptance_requires_explicit_selection(self):
        plan = qa.build_plan(self.contract, ROOT, ["contracts/sources/cili-pwn-mappings-20240611.json"],
                             "candidate-sha", include_source_acceptance=True)
        self.assertIn("postgres.source-admission-suite-whole-route-contract", plan["selected_physical_tests"])
        automatic = self.plan("contracts/sources/cili-pwn-mappings-20240611.json")
        self.assertTrue(set(automatic["manual_source_acceptance_tests"]).isdisjoint(
            automatic["core_tests"] + automatic["selected_physical_tests"]))

    def test_manual_workflow_requires_receipts_only_for_its_actual_selection(self):
        workflow = (ROOT / '.github/workflows/source-corpus-acceptance.yml').read_text()
        block = workflow.split("<<'PY'\n", 1)[1].split('\n          PY', 1)[0]
        selection = textwrap.dedent(block).split('work = pathlib.Path(sys.argv[2])', 1)[0]
        namespace = {}
        with patch.object(Path, 'cwd', return_value=ROOT):
            exec(selection, namespace)
        plan = namespace['plan']
        self.assertEqual(plan['selected_physical_tests'], plan['manual_source_acceptance_tests'])
        required = sorted({name for row in self.contract['isolated_tests']
                           if row['ctest_name'] in plan['selected_physical_tests']
                           for name in row.get('required_receipts', [])})
        self.assertEqual(plan['required_physical_receipts'], required)
        self.assertNotIn('verified_cpp_source_admission', required)

    def test_missing_isolated_registry_test_fails_closed(self) -> None:
        broken = copy.deepcopy(self.contract)
        broken["isolated_tests"][0][
            "ctest_name"
        ] = "invented.expensive.test"
        with self.assertRaisesRegex(
            qa.QaError, "absent from custom-stack registry"
        ):
            qa.validate_contract(broken, ROOT)

    def test_git_rename_retains_both_semantic_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "changed.status"
            path.write_bytes(
                b"R100\0engine/src/composition.cpp\0docs/composition.md\0"
            )
            self.assertEqual(
                qa.read_git_name_status_z(path),
                ["engine/src/composition.cpp", "docs/composition.md"],
            )

    def test_ctest_selector_uses_ctest_322_compatible_posix_ere(self) -> None:
        names = [
            "alpha.one",
            "beta-two",
            "gamma[3]",
            "delta+four",
        ]
        selector = qa.test_regex(names)
        self.assertNotIn("(?:", selector)
        self.assertTrue(selector.startswith("^("))
        self.assertTrue(selector.endswith(")$"))
        import re

        compiled = re.compile(selector)
        for name in names:
            self.assertIsNotNone(compiled.fullmatch(name))
        self.assertIsNone(compiled.fullmatch("alphaXone"))

    def _fake_ctest(
        self,
        root: Path,
        *,
        inventory: list[str],
        junit: str,
        exit_code: int = 0,
        output: str = "",
    ) -> Path:
        fake = root / "fake-ctest.py"
        fake.write_text(
            """#!/usr/bin/env python3
import json
from pathlib import Path
import sys

inventory = %r
junit = %r
exit_code = %d
output = %r
if '--show-only=json-v1' in sys.argv:
    print(json.dumps({'tests': [{'name': name} for name in inventory]}))
    raise SystemExit(0)
report = Path(sys.argv[sys.argv.index('--output-junit') + 1])
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(junit, encoding='utf-8')
if output:
    print(output)
raise SystemExit(exit_code)
"""
            % (inventory, junit, exit_code, output),
            encoding="utf-8",
        )
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        return fake

    def test_zero_exit_without_executed_tests_is_not_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake = self._fake_ctest(
                root,
                inventory=["core.required"],
                junit="<testsuite tests=\"0\"></testsuite>",
                output="No tests were found!!!",
            )
            plan = {
                "schema": qa.PLAN_SCHEMA,
                "head_sha": "candidate",
                "selected_profiles": [],
                "core_tests": ["core.required"],
                "selected_physical_tests": [],
            }
            result = root / "result.json"
            status = qa.execute_plan(
                plan, root / "build", root / "qa", result, str(fake)
            )
            self.assertNotEqual(status, 0)
            recorded = json.loads(result.read_text(encoding="utf-8"))
            lane = recorded["lanes"][0]
            self.assertEqual(
                recorded["aggregate_required_qa"], "failed"
            )
            self.assertEqual(lane["ctest_exit_code"], 0)
            self.assertEqual(lane["executed_test_count"], 0)
            self.assertFalse(lane["selection_verified"])
            self.assertEqual(
                lane["primary_failure"]["class"],
                "selection-proof-failure",
            )

    def test_zero_exit_missing_one_selected_test_is_not_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake = self._fake_ctest(
                root,
                inventory=["core.one", "core.two"],
                junit=(
                    "<testsuite>"
                    "<testcase name=\"core.one\" time=\"0.1\"/>"
                    "</testsuite>"
                ),
            )
            plan = {
                "schema": qa.PLAN_SCHEMA,
                "head_sha": "candidate",
                "selected_profiles": [],
                "core_tests": ["core.one", "core.two"],
                "selected_physical_tests": [],
            }
            result = root / "result.json"
            status = qa.execute_plan(
                plan, root / "build", root / "qa", result, str(fake)
            )
            self.assertNotEqual(status, 0)
            recorded = json.loads(result.read_text(encoding="utf-8"))
            lane = recorded["lanes"][0]
            self.assertEqual(lane["executed_test_count"], 1)
            self.assertIn(
                "missing=['core.two']",
                lane["primary_failure"]["detail"],
            )

    def test_exact_selected_execution_is_proven_before_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake = self._fake_ctest(
                root,
                inventory=["core.one", "core.two"],
                junit=(
                    "<testsuite>"
                    "<testcase name=\"core.one\" time=\"0.1\"/>"
                    "<testcase name=\"core.two\" time=\"0.2\"/>"
                    "</testsuite>"
                ),
            )
            plan = {
                "schema": qa.PLAN_SCHEMA,
                "head_sha": "candidate",
                "selected_profiles": [],
                "core_tests": ["core.one", "core.two"],
                "selected_physical_tests": [],
            }
            result = root / "result.json"
            status = qa.execute_plan(
                plan, root / "build", root / "qa", result, str(fake)
            )
            self.assertEqual(status, 0)
            recorded = json.loads(result.read_text(encoding="utf-8"))
            lane = recorded["lanes"][0]
            self.assertEqual(
                recorded["aggregate_required_qa"], "passed"
            )
            self.assertEqual(lane["executed_test_count"], 2)
            self.assertEqual(
                lane["executed_tests"], ["core.one", "core.two"]
            )
            self.assertTrue(lane["selection_verified"])
            self.assertTrue(lane["evidence_receipts_verified"])
            self.assertEqual(lane["embedded_receipts"], [])

    def test_embedded_physical_receipts_are_retained_in_terminal_result(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            embedded = {
                "schema": "laplace.postgresql-test-resource-guard/v1",
                "result": "completed",
                "maxima": {"rss_bytes": 123456},
            }
            fake = self._fake_ctest(
                root,
                inventory=["selected.physical"],
                junit=(
                    "<testsuite>"
                    "<testcase name=\"selected.physical\" time=\"0.2\"/>"
                    "</testsuite>"
                ),
                output=(
                    "LAPLACE_QA_RECEIPT postgres_source_resource_guard "
                    + json.dumps(embedded, separators=(",", ":"))
                ),
            )
            plan = {
                "schema": qa.PLAN_SCHEMA,
                "head_sha": "candidate",
                "selected_profiles": ["fixture"],
                "core_tests": [],
                "selected_physical_tests": ["selected.physical"],
                "selected_physical_parallel_jobs": 1,
            }
            result = root / "result.json"
            status = qa.execute_plan(
                plan, root / "build", root / "qa", result, str(fake)
            )
            self.assertEqual(status, 0)
            recorded = json.loads(result.read_text(encoding="utf-8"))
            lane = recorded["lanes"][0]
            self.assertTrue(lane["evidence_receipts_verified"])
            self.assertEqual(
                lane["embedded_receipts"],
                [{"test": None, "name": "postgres_source_resource_guard", "receipt": embedded}],
            )

    def test_successful_junit_output_retains_required_physical_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            receipt = {"schema": "laplace.physical-test/v1", "checks": 18}
            marker = "LAPLACE_QA_RECEIPT physical_proof " + json.dumps(receipt)
            fake = self._fake_ctest(
                root, inventory=["selected.physical"], output="",
                junit=("<testsuite><testcase name=\"selected.physical\" time=\"0.2\">"
                       "<system-out><![CDATA[" + marker + "]]></system-out>"
                       "</testcase></testsuite>"),
            )
            plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                    "selected_physical_tests": ["selected.physical"],
                    "required_physical_receipts": ["physical_proof"],
                    "required_physical_receipts_by_test": {"selected.physical": ["physical_proof"]}}
            result = root / "result.json"
            self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", result, str(fake)), 0)
            lane = json.loads(result.read_text())["lanes"][0]
            self.assertEqual(lane["embedded_receipts"], [{"test": "selected.physical", "name": "physical_proof", "receipt": receipt}])
            self.assertTrue(lane["evidence_receipts_verified"])

    def test_generated_plan_preserves_each_required_receipt_owner(self) -> None:
        contract = copy.deepcopy(self.contract)
        names = ("postgres.highway-committed-revalidation-contract",
                 "postgres.verified-cpp-source-contract")
        for row in contract["isolated_tests"]:
            if row["ctest_name"] in names:
                row["required_receipts"] = ["shared_proof"]
                row.pop("required_receipt_fields", None)
        plan = qa.build_plan(contract, ROOT, [
            "integrations/postgresql/extension/src/highway_registry_revalidate_pg.inc",
            "tests/postgres/verified_cpp_source_contract.sql"], "candidate")
        self.assertEqual(plan["required_physical_receipts"], ["shared_proof"])
        self.assertEqual(plan["required_physical_receipts_by_test"],
                         {name: ["shared_proof"] for name in names})

    def test_cpp_gate_requires_all_current_control_counts_from_its_own_receipt(self) -> None:
        owner = "postgres.verified-cpp-source-contract"
        name = "verified_cpp_source_admission"
        generated = self.plan("tests/postgres/verified_cpp_source_contract.sql")
        expected = {"schema": "laplace.verified-cpp-source-acceptance/v1",
                    "negative_controls": 13, "reconciliation_controls": 2,
                    "profile_schema_controls": 6, "reference_rule_array_controls": 2,
                    "physicality_binding_controls": 4, "prefetch_ambiguity_controls": 1,
                    "same_content_physicality_selection": True}
        self.assertEqual(generated["required_physical_receipt_fields_by_test"][owner][name], expected)
        changes = [(None, None), ("profile_schema_controls", None),
                   ("reference_rule_array_controls", None), ("reference_rule_array_controls", 1),
                   ("reference_rule_array_controls", 2.0), ("reference_rule_array_controls", True),
                   ("physicality_binding_controls", None), ("physicality_binding_controls", 3),
                   ("physicality_binding_controls", 4.0), ("physicality_binding_controls", True),
                   ("prefetch_ambiguity_controls", None), ("prefetch_ambiguity_controls", 0),
                   ("prefetch_ambiguity_controls", 1.0), ("prefetch_ambiguity_controls", True),
                   ("same_content_physicality_selection", None), ("same_content_physicality_selection", False),
                   ("same_content_physicality_selection", 1), ("same_content_physicality_selection", "true"),
                   ("negative_controls", 12), ("reconciliation_controls", 1),
                   ("profile_schema_controls", 5), ("profile_schema_controls", 6.0),
                   ("profile_schema_controls", True), ("schema", "laplace.obsolete-proof/v1")]
        for field, value in changes:
            with self.subTest(field=field, value=value), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                receipt = dict(expected)
                if field and value is None:
                    receipt.pop(field)
                elif field:
                    receipt[field] = value
                marker = "LAPLACE_QA_RECEIPT " + name + " " + json.dumps(receipt)
                fake = self._fake_ctest(root, inventory=[owner], output="",
                    junit='<testsuite><testcase name="' + owner + '"><system-out><![CDATA[' +
                          marker + ']]></system-out></testcase></testsuite>')
                plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                        "selected_physical_tests": [owner], "required_physical_receipts": [name],
                        "required_physical_receipts_by_test": {owner: [name]},
                        "required_physical_receipt_fields_by_test": {owner: {name: expected}}}
                path = root / "result.json"
                self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", path, str(fake)),
                                 qa.EVIDENCE_RECEIPT_EXIT if field else 0)
                lane = json.loads(path.read_text())["lanes"][0]
                self.assertEqual(lane["ctest_exit_code"], 0)
                self.assertEqual(lane["required_receipt_fields_by_test"], {owner: {name: expected}})
                if field:
                    self.assertIn("receipt field differs", lane["primary_failure"]["detail"])

    def test_highway_novelty_diagnostics_are_required_from_the_native_test_owner(self) -> None:
        owner = "postgres.highway-committed-revalidation-contract"
        name = "highway_committed_revalidation"
        expected = {"schema": "laplace.highway-committed-revalidation-test/v1",
                    "novelty_diagnostic_controls": 3, "historical_geometry_controls": 1}
        generated = self.plan("integrations/postgresql/extension/src/highway_registry_revalidate_pg.inc")
        self.assertEqual(generated["required_physical_receipt_fields_by_test"][owner][name], expected)
        cases = [("novelty_diagnostic_controls", value) for value in (3, None, 0, 2, 3.0, True)]
        cases += [("historical_geometry_controls", value) for value in (1, None, 0, 2, 1.0, True)]
        for field, count in cases:
            with self.subTest(field=field, count=count), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                receipt = dict(expected)
                if count is None:
                    receipt.pop(field)
                else:
                    receipt[field] = count
                marker = "LAPLACE_QA_RECEIPT " + name + " " + json.dumps(receipt)
                fake = self._fake_ctest(root, inventory=[owner], output="",
                    junit='<testsuite><testcase name="' + owner + '"><system-out><![CDATA[' +
                          marker + ']]></system-out></testcase></testsuite>')
                plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                        "selected_physical_tests": [owner], "required_physical_receipts": [name],
                        "required_physical_receipts_by_test": {owner: [name]},
                        "required_physical_receipt_fields_by_test": {owner: {name: expected}}}
                path = root / "result.json"
                self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", path, str(fake)),
                                 0 if type(count) is int and count == expected[field] else qa.EVIDENCE_RECEIPT_EXIT)

    def test_receipt_field_requirements_cannot_name_an_unrequired_marker(self) -> None:
        contract = copy.deepcopy(self.contract)
        row = next(item for item in contract["isolated_tests"]
                   if item["ctest_name"] == "postgres.verified-cpp-source-contract")
        row["required_receipt_fields"] = {"unrequired-proof": {"count": 6}}
        with self.assertRaisesRegex(qa.QaError, "required receipt fields"):
            qa.validate_contract(contract, ROOT)

    def test_reflection_changes_require_their_own_completed_native_receipt(self) -> None:
        owner = "postgres.verified-cpp-source-contract"
        name = "physicality_entity_reflection"
        expected = {"schema": "laplace.physicality-entity-contract/v1",
            "capacity_refusals": 5, "corruption_refusals": 10, "content_binding_refusals": 7,
            "native_rle_interval_split_replay_verified": True,
            "complete_interval_cold_backend_verified": True,
            "interval_corruption_refusals": 4,
            "same_entity_distinct_native_forms": 3,
            "transparent_singleton_public_sink_verified": True,
            "distinct_physicality_rle_intervals_verified": True,
            "same_entity_batch_single_replay_verified": True,
            "same_entity_cold_receipt_and_content_parity": True,
            "singleton_constructor_refusals": 3,
            "original_physicality_selected_reads": 4,
            "original_physicality_owner_refusals": 1,
            "canonical_and_original_atom_cold_reads": 2,
            "canonical_atom_owner_read_verified": True,
            "original_atom_owner_read_verified": True,
            "cross_epoch_source_view_verified": True,
            "structural_frontier_exhaustion_verified": True,
            "original_same_entity_rle_nodes": 5,
            "original_same_entity_rle_carriers": 4,
            "same_physicality_distinct_observation_replay": True,
            "singleton_mixed_batch_scope_identity_verified": True,
            "unrelated_batch_neighbor_physicality_change_verified": True,
            "read_only_context_refusal_verified": True,
            "mixed_batch_replay_new_rollback_verified": True,
            "shared_owner_candidate_selection_verified": True,
            "cold_backend_verified": True, "canonical_physicalities_unchanged": True}
        for path in ("contracts/physicality-entity-reflection.json",
            "engine/include/laplace/physicality_entity.h", "engine/src/physicality_entity.cpp",
            "engine/include/laplace/physicality_occurrence_binding.h", "engine/src/physicality_occurrence_binding.cpp",
            "engine/include/laplace/content_reference_view.h", "engine/src/content_reference_view.cpp",
            "engine/src/canonical_composition_plan.hpp",
            "integrations/postgresql/extension/src/physicality_entity_pg.c",
            "integrations/postgresql/extension/src/content_materialization_pg.c",
            "integrations/postgresql/extension/physicality_entity.sql.in",
            "tests/postgres/physicality_entity_contract.sql"):
            with self.subTest(path=path):
                generated = self.plan(path)
                self.assertIn(owner, generated["selected_physical_tests"])
                self.assertIn(name, generated["required_physical_receipts_by_test"][owner])
                self.assertEqual(generated["required_physical_receipt_fields_by_test"][owner][name], expected)
        cases = [(None, None)] + [(key, None) for key in expected]
        cases += [("capacity_refusals", 4), ("corruption_refusals", 9),
                  ("content_binding_refusals", 5), ("corruption_refusals", 10.0),
                  ("cold_backend_verified", 1), ("canonical_physicalities_unchanged", False),
                  ("cross_epoch_source_view_verified", False),
                  ("cross_epoch_source_view_verified", 1),
                  ("cross_epoch_source_view_verified", "true"),
                  ("structural_frontier_exhaustion_verified", False),
                  ("structural_frontier_exhaustion_verified", 1),
                  ("structural_frontier_exhaustion_verified", "true")]
        for field, value in cases:
            with self.subTest(field=field, value=value), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                receipt = dict(expected)
                if field and value is None:
                    receipt.pop(field)
                elif field:
                    receipt[field] = value
                marker = "LAPLACE_QA_RECEIPT " + name + " " + json.dumps(receipt)
                fake = self._fake_ctest(root, inventory=[owner], output="",
                    junit='<testsuite><testcase name="' + owner + '"><system-out><![CDATA[' +
                          marker + ']]></system-out></testcase></testsuite>')
                plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                    "selected_physical_tests": [owner], "required_physical_receipts": [name],
                    "required_physical_receipts_by_test": {owner: [name]},
                    "required_physical_receipt_fields_by_test": {owner: {name: expected}}}
                path = root / "result.json"
                self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", path, str(fake)),
                    qa.EVIDENCE_RECEIPT_EXIT if field else 0)

    def test_same_named_receipts_keep_distinct_testcase_observations(self) -> None:
        for second_count in (18, 19):
            for log_copies in (False, True):
                with self.subTest(second_count=second_count, log_copies=log_copies), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    owners = ("selected.highway", "selected.cpp")
                    receipts = [{"schema": "laplace.physical-proof/v1", "count": count}
                                for count in (18, second_count)]
                    markers = ["LAPLACE_QA_RECEIPT shared_proof " + json.dumps(value)
                               for value in receipts]
                    junit = "<testsuite>" + "".join(
                        '<testcase name="' + owner + '"><system-out><![CDATA[' + marker +
                        ']]></system-out></testcase>' for owner, marker in zip(owners, markers)
                    ) + "</testsuite>"
                    fake = self._fake_ctest(root, inventory=list(owners), junit=junit,
                                            output="\n".join(markers) if log_copies else "")
                    plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                            "selected_physical_tests": list(owners),
                            "required_physical_receipts": ["shared_proof"],
                            "required_physical_receipts_by_test": {owner: ["shared_proof"] for owner in owners}}
                    result = root / "result.json"
                    self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", result, str(fake)), 0)
                    lane = json.loads(result.read_text())["lanes"][0]
                    self.assertEqual(lane["embedded_receipts"], [
                        {"test": owner, "name": "shared_proof", "receipt": value}
                        for owner, value in zip(owners, receipts)])
                    self.assertEqual(lane["required_receipts_by_test"], plan["required_physical_receipts_by_test"])

    def test_required_receipt_cannot_be_borrowed_from_another_test_or_lane_log(self) -> None:
        marker = 'LAPLACE_QA_RECEIPT highway_proof {"schema":"laplace.proof/v1"}'
        for transport in ("other-testcase", "unowned-log"):
            with self.subTest(transport=transport), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                output = '<system-out>' + marker + '</system-out>' if transport == "other-testcase" else ""
                fake = self._fake_ctest(root, inventory=["selected.highway", "selected.cpp"],
                    junit='<testsuite><testcase name="selected.highway"/>' +
                          '<testcase name="selected.cpp">' + output + '</testcase></testsuite>',
                    output=marker if transport == "unowned-log" else "")
                plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                        "selected_physical_tests": ["selected.highway", "selected.cpp"],
                        "required_physical_receipts": ["highway_proof"],
                        "required_physical_receipts_by_test": {"selected.highway": ["highway_proof"]}}
                result = root / "result.json"
                self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", result, str(fake)), qa.EVIDENCE_RECEIPT_EXIT)
                lane = json.loads(result.read_text())["lanes"][0]
                self.assertEqual(lane["ctest_exit_code"], 0)
                self.assertIn("owning tests", lane["primary_failure"]["detail"])

    def test_duplicate_or_conflicting_receipts_within_one_testcase_fail(self) -> None:
        first = 'LAPLACE_QA_RECEIPT shared_proof {"schema":"laplace.proof/v1","count":18}'
        for second in (first, first.replace('"count":18', '"count":19')):
            for split_outputs in (False, True):
                with self.subTest(second=second, split_outputs=split_outputs), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    separator = '</system-out><system-out>' if split_outputs else '\n'
                    fake = self._fake_ctest(root, inventory=["selected.physical"], output="",
                        junit='<testsuite><testcase name="selected.physical"><system-out>' +
                              first + separator + second + '</system-out></testcase></testsuite>')
                    plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                            "selected_physical_tests": ["selected.physical"]}
                    result = root / "result.json"
                    self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", result, str(fake)), qa.EVIDENCE_RECEIPT_EXIT)
                    detail = json.loads(result.read_text())["lanes"][0]["primary_failure"]["detail"]
                    self.assertIn("duplicated within testcase", detail)

    def test_missing_required_receipt_turns_successful_test_red(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake = self._fake_ctest(root, inventory=["selected.physical"], output="",
                junit='<testsuite><testcase name="selected.physical"/></testsuite>')
            plan = {"schema": qa.PLAN_SCHEMA, "core_tests": [],
                    "selected_physical_tests": ["selected.physical"],
                    "required_physical_receipts": ["physical_proof"]}
            self.assertEqual(qa.execute_plan(plan, root / "build", root / "qa", root / "result.json", str(fake)), qa.EVIDENCE_RECEIPT_EXIT)

    def test_receipt_transport_duplicates_require_exact_equal_content(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            log, report = root / "lane.log", root / "lane.xml"
            marker = 'LAPLACE_QA_RECEIPT proof {"schema":"laplace.proof/v1","count":18}'
            log.write_text(marker + "\n")
            report.write_text('<testsuite><testcase name="physical"><system-out>' + marker + '</system-out></testcase></testsuite>')
            self.assertEqual(len(qa.embedded_receipts(log, report)), 1)
            report.write_text(report.read_text().replace('"count":18', '"count":17'))
            with self.assertRaisesRegex(qa.QaError, "differs between retained outputs"):
                qa.embedded_receipts(log, report)
            for left, right in (("true", "1"), ("false", "0")):
                with self.subTest(left=left, right=right):
                    log.write_text(marker.replace('"count":18', '"count":' + left) + "\n")
                    report.write_text('<testsuite><testcase name="physical"><system-out>' +
                                      marker.replace('"count":18', '"count":' + right) +
                                      '</system-out></testcase></testsuite>')
                    with self.assertRaisesRegex(qa.QaError, "differs between retained outputs"):
                        qa.embedded_receipts(log, report)
            log.write_text(marker + "\n" + marker + "\n")
            with self.assertRaisesRegex(qa.QaError, "duplicated"):
                qa.embedded_receipts(log)

    def test_malformed_embedded_receipt_turns_zero_exit_red(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake = self._fake_ctest(
                root,
                inventory=["selected.physical"],
                junit=(
                    "<testsuite>"
                    "<testcase name=\"selected.physical\" time=\"0.2\"/>"
                    "</testsuite>"
                ),
                output="LAPLACE_QA_RECEIPT broken {not-json}",
            )
            plan = {
                "schema": qa.PLAN_SCHEMA,
                "head_sha": "candidate",
                "selected_profiles": ["fixture"],
                "core_tests": [],
                "selected_physical_tests": ["selected.physical"],
                "selected_physical_parallel_jobs": 1,
            }
            result = root / "result.json"
            status = qa.execute_plan(
                plan, root / "build", root / "qa", result, str(fake)
            )
            self.assertEqual(status, qa.EVIDENCE_RECEIPT_EXIT)
            recorded = json.loads(result.read_text(encoding="utf-8"))
            lane = recorded["lanes"][0]
            self.assertFalse(lane["evidence_receipts_verified"])
            self.assertEqual(
                lane["primary_failure"]["class"], "evidence-receipt-failure"
            )

    def test_empty_execution_plan_fails_closed_with_terminal_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake = self._fake_ctest(
                root,
                inventory=["available.unselected"],
                junit="<testsuite/>",
            )
            plan = {
                "schema": qa.PLAN_SCHEMA,
                "head_sha": "candidate",
                "selected_profiles": [],
                "core_tests": [],
                "selected_physical_tests": [],
            }
            result = root / "result.json"
            status = qa.execute_plan(
                plan, root / "build", root / "qa", result, str(fake)
            )
            self.assertNotEqual(status, 0)
            recorded = json.loads(result.read_text(encoding="utf-8"))
            self.assertEqual(
                recorded["primary_terminal_result"]["failure"]["class"],
                "selection-plan-empty",
            )

    def test_first_terminal_lane_failure_stops_before_selected_physical_tests(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake = root / "fake-ctest.py"
            invocations = root / "invocations.jsonl"
            fake.write_text(
                """#!/usr/bin/env python3
import json
from pathlib import Path
import sys

log = Path(%r)
with log.open('a', encoding='utf-8') as stream:
    stream.write(json.dumps(sys.argv[1:]) + '\\n')
if '--show-only=json-v1' in sys.argv:
    print(json.dumps({'tests': [{'name': 'core.fail'}, {'name': 'selected.never'}]}))
    raise SystemExit(0)
report = Path(sys.argv[sys.argv.index('--output-junit') + 1])
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(
    '<testsuite><testcase name="core.fail" time="0.1"><failure message="deliberate failure">boom</failure></testcase></testsuite>',
    encoding='utf-8',
)
print('core.fail failed deliberately')
raise SystemExit(8)
"""
                % str(invocations),
                encoding="utf-8",
            )
            fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
            plan = {
                "schema": qa.PLAN_SCHEMA,
                "head_sha": "candidate",
                "selected_profiles": ["fixture"],
                "core_tests": ["core.fail"],
                "selected_physical_tests": ["selected.never"],
            }
            result = root / "result.json"
            status = qa.execute_plan(
                plan,
                root / "build",
                root / "qa",
                result,
                str(fake),
            )
            self.assertNotEqual(status, 0)
            recorded = json.loads(result.read_text(encoding="utf-8"))
            self.assertEqual(
                recorded["aggregate_required_qa"], "failed"
            )
            self.assertEqual(
                recorded["primary_terminal_result"]["lane"], "core"
            )
            self.assertEqual(
                recorded["primary_terminal_result"]["failure"]["test"],
                "core.fail",
            )
            calls = invocations.read_text(
                encoding="utf-8"
            ).splitlines()
            self.assertEqual(len(calls), 2)
            self.assertIn("--show-only=json-v1", json.loads(calls[0]))
            self.assertIn("--stop-on-failure", json.loads(calls[1]))
            self.assertNotIn("selected.never", calls[1])


if __name__ == "__main__":
    unittest.main()
