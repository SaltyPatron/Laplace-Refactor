#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest


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

    def test_perfcache_change_selects_hot_lookup_boundary_once(self) -> None:
        plan = self.plan("engine/src/perfcache.cpp")
        self.assertEqual(
            plan["selected_profiles"], ["unicode-hot-performance"]
        )
        self.assertEqual(
            plan["selected_physical_tests"],
            ["perfcache.hot-lookup-receipt-contract-and-mutation"],
        )

    def test_unicode_access_change_selects_epoch_and_public_source_boundaries(self) -> None:
        plan = self.plan("postgres/extension/src/unicode_access_pg.c")
        self.assertEqual(
            plan["selected_profiles"],
            ["source-admission-suite", "unicode-access-epoch"],
        )
        self.assertEqual(
            plan["selected_physical_tests"],
            [
                "postgres.mutation-unicode-access-expected-epoch-detected",
                "postgres.source-admission-suite-whole-route-contract",
            ],
        )

    def test_generic_source_admission_change_selects_real_source_regressions(
        self,
    ) -> None:
        plan = self.plan("postgres/extension/src/source_admission_pg.c")
        self.assertEqual(
            plan["selected_profiles"], ["source-admission-suite"],
        )
        self.assertEqual(
            plan["selected_physical_tests"],
            ["postgres.source-admission-suite-whole-route-contract"],
        )

    def test_iso_profile_change_does_not_select_cili(self) -> None:
        plan = self.plan("contracts/sources/iso-639-3-20260415.json")
        self.assertEqual(plan["selected_profiles"], ["source-admission-suite"])
        self.assertEqual(
            plan["selected_physical_tests"],
            ["postgres.source-admission-suite-whole-route-contract"],
        )

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
