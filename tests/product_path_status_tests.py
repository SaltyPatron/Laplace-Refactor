#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_path.py"
WORKFLOW_PATH = REPOSITORY / ".github/workflows/product-path.yml"
CLEAN_ROOM_PATH = REPOSITORY / ".github/workflows/ci.yml"
CUSTOM_STACK_PATH = REPOSITORY / ".github/workflows/custom-stack.yml"
POSTGRESQL_PRODUCT_PATH = REPOSITORY / ".github/workflows/postgresql-product.yml"
PACKAGE_PRODUCT_PATH = REPOSITORY / ".github/workflows/package-product.yml"
PRODUCT_ACTIVATION_PATH = REPOSITORY / ".github/workflows/product-activation.yml"
ACTIVATION_CONTRACT_PATH = REPOSITORY / "contracts/product-activation-gateway.json"
SPEC = importlib.util.spec_from_file_location("laplace_product_path_status_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product-path module")
product_path = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = product_path
SPEC.loader.exec_module(product_path)


class ProductPathGitStatusTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = product_path.load_json(REPOSITORY / "contracts/product-path.json")

    def read_status(self, payload: bytes) -> list[str]:
        with tempfile.TemporaryDirectory() as temporary:
            status = Path(temporary) / "changed.status"
            status.write_bytes(payload)
            return product_path.read_git_name_status_z(status)

    def assert_physical_resource_serialization(self, workflows: dict[str, str]) -> None:
        for name, workflow in workflows.items():
            self.assertIn(
                "group: laplace-physical-product-proof",
                workflow,
                f"{name} lost shared physical-host resource ownership",
            )
            self.assertIn(
                "cancel-in-progress: false",
                workflow,
                f"{name} may cancel an in-flight physical proof",
            )

    def assert_legacy_branch_protection_bridge(self, workflow: str) -> None:
        aliases = {
            "legacy-requirements": "requirements",
            "legacy-native-dev": "native (linux-dev)",
            "legacy-native-sanitize": "native (linux-sanitize)",
        }
        for job_id, check_name in aliases.items():
            marker = f"  {job_id}:\n    name: {check_name}\n    needs: product-path\n"
            self.assertIn(
                marker,
                workflow,
                f"legacy protected context {check_name} can bypass product-path",
            )
            start = workflow.index(marker)
            end = workflow.find("\n  ", start + len(marker))
            if end < 0:
                end = len(workflow)
            block = workflow[start:end]
            self.assertIn(
                "    if: always() && needs.product-path.result == 'success'",
                block,
                f"legacy protected context {check_name} can be transitively skipped",
            )
        self.assertGreaterEqual(workflow.count("needs.product-path.result"), len(aliases))

    def assert_main_push_deployment_boundary(
        self, workflow: str, activation: str, contract: str
    ) -> None:
        self.assertIn(
            "  dev-bat-deployment:\n    needs: product-path\n",
            workflow,
        )
        deployment = workflow[workflow.index("  dev-bat-deployment:"):]
        self.assertIn("      always() &&\n      github.event_name == 'push'", deployment)
        self.assertIn("github.ref == 'refs/heads/main'", deployment)
        self.assertIn("needs.product-path.result == 'success'", deployment)
        self.assertIn("    permissions:\n      actions: write\n      contents: read\n", deployment)
        self.assertIn("    timeout-minutes: 360\n", deployment)
        top_level_permissions = workflow[
            workflow.index("permissions:\n"):workflow.index("\nconcurrency:")
        ]
        self.assertNotIn("actions: write", top_level_permissions)
        self.assertIn("actions/workflows/product-activation.yml/dispatches", deployment)
        self.assertIn("inputs[expected_sha]=$EXPECTED_SHA", deployment)
        self.assertIn("EXPECTED_SHA: ${{ github.sha }}", deployment)
        self.assertIn("LAPLACE_ACTIVATION_DISPATCHED_AT", deployment)
        self.assertIn("gh api --method GET", deployment)
        self.assertIn("actions/workflows/product-activation.yml/runs", deployment)
        self.assertIn('-f "head_sha=$EXPECTED_SHA"', deployment)
        self.assertIn("-f event=workflow_dispatch", deployment)
        self.assertIn('--arg sha "$EXPECTED_SHA"', deployment)
        self.assertIn('--arg since "$LAPLACE_ACTIVATION_DISPATCHED_AT"', deployment)
        self.assertIn(".head_sha == $sha", deployment)
        self.assertIn(".created_at >= $since", deployment)
        self.assertIn("gh run watch \"$run_id\"", deployment)
        self.assertIn("--exit-status", deployment)
        self.assertIn("gh run view \"$run_id\"", deployment)
        self.assertIn('test "$conclusion" = success', deployment)
        self.assertIn("Accepted-main CI is green only after", deployment)
        self.assertIn("  workflow_dispatch:\n", activation)
        self.assertIn("      expected_sha:\n", activation)
        self.assertIn("        required: true\n", activation)
        self.assertNotIn("  pull_request:\n", activation)
        self.assertNotIn("  push:\n", activation)
        self.assertGreaterEqual(
            activation.count("test \"$GITHUB_SHA\" = '${{ inputs.expected_sha }}'"),
            3,
        )
        self.assertIn("Verify the deployed DEV/BAT generation after activation", activation)
        self.assertIn('"deployment_event": "workflow_dispatch"', contract)

    def test_type_change_remains_semantic_and_requires_custom_stack(self) -> None:
        paths = self.read_status(b"T\0engine/src/composition.cpp\0")
        self.assertEqual(paths, ["engine/src/composition.cpp"])
        result = product_path.classify(self.contract, paths)
        self.assertIn("native", result["classes"])
        self.assertTrue(result["requires_custom_stack"])

    def test_copy_preserves_semantic_source_and_destination(self) -> None:
        paths = self.read_status(
            b"C100\0engine/src/composition.cpp\0docs/composition.md\0"
        )
        self.assertEqual(
            paths,
            ["engine/src/composition.cpp", "docs/composition.md"],
        )
        result = product_path.classify(self.contract, paths)
        self.assertIn("native", result["classes"])
        self.assertTrue(result["requires_custom_stack"])

    def test_unsupported_unmerged_status_fails_closed(self) -> None:
        with self.assertRaisesRegex(product_path.ProductPathError, "unsupported"):
            self.read_status(b"U\0engine/src/composition.cpp\0")

    def test_workflow_collects_type_changes(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("--diff-filter=ACMRTD", workflow)

    def test_product_path_is_only_verification_entrypoint(self) -> None:
        product_path_workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("  pull_request:\n", product_path_workflow)
        self.assertIn("  push:\n    branches:\n      - main\n", product_path_workflow)
        self.assertIn("github.event.before", product_path_workflow)
        self.assertIn("github.event.pull_request.base.sha", product_path_workflow)

        for path in (
            CLEAN_ROOM_PATH,
            CUSTOM_STACK_PATH,
            POSTGRESQL_PRODUCT_PATH,
            PACKAGE_PRODUCT_PATH,
        ):
            workflow = path.read_text(encoding="utf-8")
            self.assertIn("  workflow_call:\n", workflow)
            self.assertNotIn("  pull_request:\n", workflow)
            self.assertNotIn("  push:\n", workflow)

        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        self.assertIn("  workflow_dispatch:\n", activation)
        self.assertNotIn("  workflow_call:\n", activation)
        self.assertNotIn("  pull_request:\n", activation)
        self.assertNotIn("  push:\n", activation)

    def test_main_push_is_dev_bat_deployment_boundary(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        self.assert_main_push_deployment_boundary(workflow, activation, contract)

    def test_physical_product_proofs_are_serialized(self) -> None:
        workflows = {
            "custom-stack": CUSTOM_STACK_PATH.read_text(encoding="utf-8"),
            "postgresql-product": POSTGRESQL_PRODUCT_PATH.read_text(encoding="utf-8"),
            "package-product": PACKAGE_PRODUCT_PATH.read_text(encoding="utf-8"),
        }
        self.assert_physical_resource_serialization(workflows)

        orchestration = WORKFLOW_PATH.read_text(encoding="utf-8")
        custom = orchestration.index("  custom-stack-proof:")
        postgres = orchestration.index("  postgresql-product-proof:")
        package = orchestration.index("  package-product-proof:")
        self.assertLess(custom, postgres)
        self.assertLess(postgres, package)
        postgres_block = orchestration[postgres:package]
        package_block = orchestration[package:orchestration.index("  product-path:", package)]
        self.assertIn("      - custom-stack-proof", postgres_block)
        self.assertIn("      - custom-stack-proof", package_block)
        self.assertIn("      - postgresql-product-proof", package_block)

    def test_legacy_required_contexts_are_subordinate_to_product_path(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assert_legacy_branch_protection_bridge(workflow)

    def test_deliberate_defect_dropping_type_changes_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace("--diff-filter=ACMRTD", "--diff-filter=ACMRD")
        self.assertNotEqual(workflow, mutant)
        self.assertNotIn("--diff-filter=ACMRTD", mutant)

    def test_deliberate_parallel_physical_proof_defect_is_detected(self) -> None:
        workflows = {
            "custom-stack": CUSTOM_STACK_PATH.read_text(encoding="utf-8"),
            "postgresql-product": POSTGRESQL_PRODUCT_PATH.read_text(encoding="utf-8"),
            "package-product": PACKAGE_PRODUCT_PATH.read_text(encoding="utf-8"),
        }
        mutant = dict(workflows)
        mutant["postgresql-product"] = mutant["postgresql-product"].replace(
            "group: laplace-physical-product-proof",
            "group: postgresql-product-${{ github.ref }}",
            1,
        )
        with self.assertRaises(AssertionError):
            self.assert_physical_resource_serialization(mutant)

    def test_deliberate_duplicate_pr_trigger_defect_is_detected(self) -> None:
        clean_room = CLEAN_ROOM_PATH.read_text(encoding="utf-8")
        mutant = clean_room.replace(
            "on:\n  workflow_call:\n",
            "on:\n  workflow_call:\n  pull_request:\n",
            1,
        )
        self.assertIn("  pull_request:\n", mutant)
        self.assertNotIn("  pull_request:\n", clean_room)

    def test_deliberate_legacy_context_bypass_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace(
            "  legacy-requirements:\n    name: requirements\n    needs: product-path\n",
            "  legacy-requirements:\n    name: requirements\n    needs: hosted-proof\n",
            1,
        )
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_legacy_branch_protection_bridge(mutant)

    def test_deliberate_legacy_skip_cascade_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace(
            "    if: always() && needs.product-path.result == 'success'\n",
            "",
            1,
        )
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_legacy_branch_protection_bridge(mutant)

    def test_deliberate_pr_deployment_defect_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace(
            "github.event_name == 'push'",
            "github.event_name == 'pull_request'",
            1,
        )
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(mutant, activation, contract)

    def test_deliberate_deployment_skip_cascade_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace(
            "      always() &&\n      github.event_name == 'push'",
            "      github.event_name == 'push'",
            1,
        )
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(mutant, activation, contract)

    def test_deliberate_fire_and_forget_activation_defect_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        start = workflow.index("      - name: Require persistent product activation")
        end = workflow.index("\n  # Migration aliases", start)
        mutant = workflow[:start] + workflow[end:]
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(mutant, activation, contract)

    def test_deliberate_activation_exit_status_bypass_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace("            --exit-status \\\n", "", 1)
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(mutant, activation, contract)

    def test_deliberate_unbound_dispatch_sha_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        mutant = activation.replace(
            "          test \"$GITHUB_SHA\" = '${{ inputs.expected_sha }}'\n",
            "",
        )
        self.assertNotEqual(activation, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(workflow, mutant, contract)


if __name__ == "__main__":
    unittest.main()
