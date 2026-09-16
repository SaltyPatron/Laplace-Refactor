#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import re
import subprocess
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
BENCHMARK_COMPOSITION_PATH = REPOSITORY / ".github/workflows/benchmark-composition-frontier.yml"
SPEC = importlib.util.spec_from_file_location("laplace_product_path_status_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product-path module")
product_path = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = product_path
SPEC.loader.exec_module(product_path)


class ProductPathGitStatusTests(unittest.TestCase):
    @staticmethod
    def job_boundary(workflow: str, job: str) -> tuple[int, int]:
        marker = f"  {job}:\n"
        start = workflow.index(marker)
        following = re.search(r"(?m)^  [A-Za-z0-9_-]+:\s*$", workflow[start + len(marker):])
        end = start + len(marker) + following.start() if following else len(workflow)
        return start, end

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

    def assert_physical_orchestration_serialization(self, orchestration: str) -> None:
        custom = orchestration.index("  custom-stack-proof:")
        postgres = orchestration.index("  postgresql-product-proof:")
        package = orchestration.index("  package-product-proof:")
        product_path_job = orchestration.index("  product-path:", package)
        blocks = {
            "custom": orchestration[custom:postgres],
            "postgres": orchestration[postgres:package],
            "package": orchestration[package:product_path_job],
        }
        for name, block in blocks.items():
            self.assertIn("      - classify", block, f"{name} lost classification dependency")
            self.assertIn("      - hosted-proof", block, f"{name} lost hosted proof dependency")
            self.assertIn(
                "needs.hosted-proof.result == 'success'",
                block,
                f"{name} can run before hosted proof succeeds",
            )
        self.assertIn(
            "      - custom-stack-proof",
            blocks["postgres"],
            "postgresql proof can become the replaceable pending member of the shared concurrency group",
        )
        self.assertIn(
            "      - custom-stack-proof",
            blocks["package"],
            "package proof does not wait for custom-stack host ownership",
        )
        self.assertIn(
            "      - postgresql-product-proof",
            blocks["package"],
            "package proof can become the third concurrent member and cancel a pending proof",
        )
        self.assertIn("      (!cancelled()) &&", blocks["postgres"])
        self.assertIn("      (!cancelled()) &&", blocks["package"])

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
            start, end = self.job_boundary(workflow, job_id)
            block = workflow[start:end]
            self.assertIn(
                "    if: always()\n",
                block,
                f"legacy protected context {check_name} must run and fail after an unsuccessful aggregate",
            )
            self.assertIn('      - run: test "${{ needs.product-path.result }}" = success', block)
        self.assertGreaterEqual(workflow.count("needs.product-path.result"), len(aliases))

    def assert_main_push_deployment_boundary(
        self, workflow: str, activation: str, contract: str
    ) -> None:
        self.assertIn(
            "  dev-bat-deployment:\n    needs: [classify, product-path]\n",
            workflow,
        )
        start, end = self.job_boundary(workflow, "dev-bat-deployment")
        deployment = workflow[start:end]
        self.assertIn("      (!cancelled()) &&\n      github.event_name == 'push'", deployment)
        self.assertIn("github.ref == 'refs/heads/main'", deployment)
        self.assertIn("needs.product-path.result == 'success'", deployment)
        self.assertIn("needs.classify.result == 'success'", deployment)
        self.assertIn("needs.classify.outputs.audit_only != 'true'", deployment)
        self.assertIn("    uses: ./.github/workflows/product-activation.yml\n", deployment)
        self.assertIn("    with:\n      expected_sha: ${{ github.sha }}\n", deployment)
        self.assertNotIn("runs-on:", deployment)
        self.assertNotIn("actions/workflows/product-activation.yml/dispatches", deployment)
        self.assertNotIn("GH_TOKEN", deployment)
        self.assertNotIn("gh run watch", deployment)
        self.assertNotIn("ref=main", deployment)
        top_level_permissions = workflow[
            workflow.index("permissions:\n"):workflow.index("\nconcurrency:")
        ]
        self.assertNotIn("actions: write", top_level_permissions)

        self.assertIn("  workflow_call:\n", activation)
        self.assertIn("  workflow_dispatch:\n", activation)
        self.assertGreaterEqual(activation.count("      expected_sha:\n"), 2)
        self.assertGreaterEqual(activation.count("        required: true\n"), 2)
        self.assertNotIn("  pull_request:\n", activation)
        self.assertNotIn("  push:\n", activation)
        literal_sha_gate = "test \"$GITHUB_SHA\" = '${{ inputs.expected_sha }}'"
        diagnostic_sha_gate = (
            "require_equal \"$GITHUB_SHA\" '${{ inputs.expected_sha }}' repository-sha"
        )
        self.assertGreaterEqual(
            activation.count(literal_sha_gate) + activation.count(diagnostic_sha_gate),
            3,
        )
        self.assertGreaterEqual(
            activation.count("push|workflow_dispatch) ;;"),
            3,
            "activation must accept only authoritative main-push calls or explicit manual dispatch",
        )
        self.assertIn("Verify the persistent DEV/BAT product directly", activation)
        self.assertIn("product_activation_runner.py", activation)
        self.assertIn(".execution_owner == \"laplace-runner\"", activation)
        self.assertIn(".root_product_executor == false", activation)
        self.assertNotIn("laplace-product-activate", activation)
        self.assertNotIn("LAPLACE_ACTIVATION_HMAC_KEY_B64", activation)
        self.assertNotIn("compile_gateway_upgrade.py", activation)
        self.assertNotIn("product_activation.py create-request", activation)
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
        self.assertIn("BASE_REF: ${{ github.base_ref }}", product_path_workflow)
        self.assertIn(
            'BASE_SHA=$(git merge-base "$base_remote" "$HEAD_SHA")',
            product_path_workflow,
        )
        self.assertNotIn("github.event.pull_request.base.sha", product_path_workflow)

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
        self.assertIn("  workflow_call:\n", activation)
        self.assertIn("  workflow_dispatch:\n", activation)
        self.assertNotIn("  pull_request:\n", activation)
        self.assertNotIn("  push:\n", activation)

    def test_pull_request_classification_rejects_stale_event_base(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("BASE_REF: ${{ github.base_ref }}", workflow)
        self.assertIn('git show-ref --verify --quiet "$base_remote"', workflow)
        self.assertIn(
            'BASE_SHA=$(git merge-base "$base_remote" "$HEAD_SHA")',
            workflow,
        )
        self.assertNotIn("github.event.pull_request.base.sha", workflow)

    def test_main_push_is_dev_bat_deployment_boundary(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        self.assert_main_push_deployment_boundary(workflow, activation, contract)

    def test_physical_product_proofs_share_host_ownership_and_serialize_execution(self) -> None:
        workflows = {
            "custom-stack": CUSTOM_STACK_PATH.read_text(encoding="utf-8"),
            "postgresql-product": POSTGRESQL_PRODUCT_PATH.read_text(encoding="utf-8"),
            "package-product": PACKAGE_PRODUCT_PATH.read_text(encoding="utf-8"),
        }
        self.assert_physical_resource_serialization(workflows)
        self.assert_physical_orchestration_serialization(
            WORKFLOW_PATH.read_text(encoding="utf-8")
        )

    def test_legacy_required_contexts_are_subordinate_to_product_path(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assert_legacy_branch_protection_bridge(workflow)

    def test_composition_benchmark_is_dispatch_only_exact_sha_and_shared_host(self) -> None:
        workflow = BENCHMARK_COMPOSITION_PATH.read_text(encoding="utf-8")
        self.assertIn("  workflow_dispatch:\n", workflow)
        self.assertNotIn("  pull_request:\n", workflow)
        self.assertNotIn("  push:\n", workflow)
        self.assertIn("      expected_sha:\n", workflow)
        self.assertIn("        required: true\n", workflow)
        self.assertIn("inputs.expected_sha", workflow)
        self.assertIn("test \"$GITHUB_SHA\" =", workflow)
        self.assertIn("test \"$(git rev-parse HEAD)\" =", workflow)
        self.assertIn("group: laplace-physical-product-proof", workflow)
        self.assertIn("queue: max", workflow)
        self.assertIn("cancel-in-progress: false", workflow)
        self.assertIn("taskset -c", workflow)
        self.assertIn("physical_cores", workflow)
        self.assertIn("logical_threads", workflow)
        self.assertIn("candidates = [1, 2, 3, 4, physical, selected_smt, logical]", workflow)
        self.assertIn("semantic receipt changed across worker grants", workflow)
        self.assertIn("stream fingerprint changed across worker grants", workflow)
        self.assertIn("laplace.benchmark-suite-receipt/v1", workflow)
        self.assertIn("legacy_reference", workflow)
        self.assertIn("directly_comparable", workflow)
        self.assertIn("actions/upload-artifact", workflow)

    def test_deliberate_defect_dropping_type_changes_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace("--diff-filter=ACMRTD", "--diff-filter=ACMRD")
        self.assertNotEqual(workflow, mutant)
        self.assertNotIn("--diff-filter=ACMRTD", mutant)

    def test_deliberate_shared_host_ownership_defect_is_detected(self) -> None:
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

    def test_deliberate_parallel_physical_orchestration_defect_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        mutant = workflow.replace(
            "      - custom-stack-proof\n    if: >-\n      needs.classify.outputs.audit_only != 'true' &&\n      (!cancelled()) &&\n      needs.classify.outputs.requires_postgresql_product",
            "    if: >-\n      needs.classify.outputs.audit_only != 'true' &&\n      (!cancelled()) &&\n      needs.classify.outputs.requires_postgresql_product",
            1,
        )
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_physical_orchestration_serialization(mutant)

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
        start, end = self.job_boundary(workflow, "legacy-requirements")
        block = workflow[start:end]
        mutant = workflow.replace(
            block,
            block.replace("    if: always()\n", ""),
            1,
        )
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_legacy_branch_protection_bridge(mutant)

    def test_legacy_alias_commands_fail_for_unsuccessful_aggregate(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for job_id in ("legacy-requirements", "legacy-native-dev", "legacy-native-sanitize"):
            start, end = self.job_boundary(workflow, job_id)
            block = workflow[start:end]
            self.assertIn("    if: always()\n", block)
            command = block.split("      - run: ", 1)[1].strip()
            for result in ("success", "failure", "cancelled", "skipped", ""):
                with self.subTest(job=job_id, result=result):
                    execution = subprocess.run(
                        ["bash", "-c", command.replace("${{ needs.product-path.result }}", result)],
                        capture_output=True,
                    )
                    self.assertEqual(execution.returncode == 0, result == "success")

    def test_success_only_legacy_condition_is_rejected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        start, end = self.job_boundary(workflow, "legacy-requirements")
        block = workflow[start:end]
        mutant = workflow.replace(block, block.replace(
            "    if: always()\n",
            "    if: always() && needs.product-path.result == 'success'\n",
        ), 1)
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
            "      (!cancelled()) &&\n      github.event_name == 'push'",
            "      github.event_name == 'push'",
            1,
        )
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(mutant, activation, contract)

    def test_deliberate_moving_main_activation_defect_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        start, end = self.job_boundary(workflow, "dev-bat-deployment")
        deployment = workflow[start:end]
        mutant_deployment = deployment.replace(
            "    uses: ./.github/workflows/product-activation.yml\n    with:\n      expected_sha: ${{ github.sha }}\n",
            "    runs-on: ubuntu-24.04\n    steps:\n      - run: echo ref=main\n",
            1,
        )
        mutant = workflow[:start] + mutant_deployment + workflow[end:]
        self.assertNotEqual(workflow, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(mutant, activation, contract)

    def test_deliberate_missing_workflow_call_activation_defect_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        mutant = activation.replace("  workflow_call:\n", "", 1)
        self.assertNotEqual(activation, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(workflow, mutant, contract)

    def test_deliberate_unbound_activation_sha_is_detected(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        activation = PRODUCT_ACTIVATION_PATH.read_text(encoding="utf-8")
        contract = ACTIVATION_CONTRACT_PATH.read_text(encoding="utf-8")
        mutant = activation.replace(
            "          test \"$GITHUB_SHA\" = '${{ inputs.expected_sha }}'\n",
            "",
        ).replace(
            "          require_equal \"$GITHUB_SHA\" '${{ inputs.expected_sha }}' repository-sha\n",
            "",
        )
        self.assertNotEqual(activation, mutant)
        with self.assertRaises(AssertionError):
            self.assert_main_push_deployment_boundary(workflow, mutant, contract)


    def assert_audit_only_boundaries(self, workflow: str, clean_room: str) -> None:
        inputs = clean_room[:clean_room.index("\njobs:")]
        self.assertIn("      run_native:\n", inputs)
        self.assertIn("        type: boolean\n", inputs)
        self.assertIn("        default: true\n", inputs)
        self.assertNotIn("        default: false\n", inputs)
        start, end = self.job_boundary(workflow, "hosted-proof")
        caller = workflow[start:end]
        self.assertIn("    needs: classify\n", caller)
        self.assertIn("run_native: ${{ needs.classify.outputs.audit_only != 'true' }}", caller)
        self.assertIn("audit_base_sha: ${{ needs.classify.outputs.base_sha }}", caller)
        self.assertIn("audit_head_sha: ${{ needs.classify.outputs.head_sha }}", caller)
        self.assertNotIn("    if:", caller)
        start, end = self.job_boundary(clean_room, "native")
        native = clean_room[start:end]
        header = native[:native.index("    steps:")]
        self.assertNotIn("    if:", header, "job-level skipping drops protected matrix contexts")
        self.assertNotIn("    name:", header, "protected native context names must remain stable")
        self.assertIn("          - linux-dev\n          - linux-sanitize\n", header)
        steps = re.split(r"(?m)^      - ", native[native.index("    steps:"):])[1:]
        self.assertGreaterEqual(len(steps), 15)
        self.assertTrue(steps[0].startswith("uses: actions/checkout@"))
        self.assertIn("          fetch-depth: 0\n", steps[0])
        policy = [step for step in steps if step.startswith(
            "name: Verify current-source audit-only classification\n")]
        self.assertEqual(len(policy), 1)
        self.assertIn("        if: ${{ !inputs.run_native }}\n", policy[0])
        self.assertIn('test "$AUDIT_HEAD_SHA" = "$EVENT_HEAD_SHA"', policy[0])
        self.assertIn("github.event.pull_request.head.sha || github.sha", policy[0])
        self.assertIn("product_path.py verify-audit-only", policy[0])
        self.assertIn('--base "$AUDIT_BASE_SHA" --head "$AUDIT_HEAD_SHA"', policy[0])
        self.assertIn('--expected-checkout "$GITHUB_SHA"', policy[0])
        self.assertIn("set -euo pipefail", policy[0])
        self.assertIn("Native builds and tests did not execute.", policy[0])
        for step in steps[1:]:
            if step == policy[0]:
                continue
            always = (step.startswith("name: Report compiler object cache\n")
                      or step.startswith("name: Retain exact "))
            expected = "        if: always() && inputs.run_native\n" if always else "        if: inputs.run_native\n"
            self.assertIn(expected, step, f"unguarded native step: {step.splitlines()[0]}")
            self.assertEqual(len(re.findall(r"(?m)^        if:", step)), 1)
        for command in ("cmake --build", "ctest --test-dir", "verify-registry.sh"):
            self.assertIn(command, native)
        for job in ("publication-recovery", "custom-stack-proof", "candidate-chess-calibration",
                    "postgresql-product-proof", "package-product-proof", "dev-bat-deployment",
                    "deployed-chess-calibration", "deployed-stockfish-corpus", "dev-bat-live-substrate"):
            start, end = self.job_boundary(workflow, job)
            block = workflow[start:end]
            self.assertIn("needs.classify.outputs.audit_only != 'true'", block, job)
            self.assertIn("classify", block[:block.index("    if:")], job)
        self.assertNotIn("api.github.com/repos", native)
        self.assertNotIn("actions: write", clean_room)
        self.assertNotIn("checks: write", clean_room)
        self.assertNotIn("/statuses/", clean_room)

    def test_audit_only_keeps_both_real_protected_matrix_contexts_and_all_heavy_guards(self) -> None:
        self.assert_audit_only_boundaries(WORKFLOW_PATH.read_text(), CLEAN_ROOM_PATH.read_text())

    def test_audit_only_default_or_caller_bypass_is_detected(self) -> None:
        workflow, clean_room = WORKFLOW_PATH.read_text(), CLEAN_ROOM_PATH.read_text()
        mutants = [
            (workflow, clean_room.replace("        default: true\n", "        default: false\n", 1)),
            (workflow.replace("needs.classify.outputs.audit_only != 'true' }}", "false }}", 1), clean_room),
            (workflow.replace("audit_head_sha: ${{ needs.classify.outputs.head_sha }}",
                              "audit_head_sha: unrelated", 1), clean_room),
            (workflow, clean_room.replace("  native:\n", "  native:\n    if: inputs.run_native\n", 1)),
            (workflow, clean_room.replace("          - linux-sanitize\n", "", 1)),
            (workflow, clean_room.replace("product_path.py verify-audit-only", "product_path.py classify", 1)),
            (workflow, clean_room.replace('test "$AUDIT_HEAD_SHA" = "$EVENT_HEAD_SHA"', "true", 1)),
        ]
        for index, (mutant_workflow, mutant_ci) in enumerate(mutants):
            with self.subTest(index=index), self.assertRaises(AssertionError):
                self.assert_audit_only_boundaries(mutant_workflow, mutant_ci)

    def test_every_heavy_native_step_requires_the_run_native_input(self) -> None:
        workflow, clean_room = WORKFLOW_PATH.read_text(), CLEAN_ROOM_PATH.read_text()
        for match in re.finditer(r"(?m)^        if: (?:always\(\) && )?inputs.run_native\n", clean_room):
            mutant = clean_room[:match.start()] + clean_room[match.end():]
            with self.subTest(offset=match.start()), self.assertRaises(AssertionError):
                self.assert_audit_only_boundaries(workflow, mutant)

    def test_every_physical_and_deployment_audit_guard_is_enforced(self) -> None:
        workflow, clean_room = WORKFLOW_PATH.read_text(), CLEAN_ROOM_PATH.read_text()
        for job in ("publication-recovery", "custom-stack-proof", "candidate-chess-calibration",
                    "postgresql-product-proof", "package-product-proof", "dev-bat-deployment",
                    "deployed-chess-calibration", "deployed-stockfish-corpus", "dev-bat-live-substrate"):
            start, end = self.job_boundary(workflow, job)
            block = workflow[start:end].replace("needs.classify.outputs.audit_only != 'true' &&", "")
            mutant = workflow[:start] + block + workflow[end:]
            with self.subTest(job=job), self.assertRaises(AssertionError):
                self.assert_audit_only_boundaries(mutant, clean_room)

    def test_aggregate_still_rejects_failed_or_cancelled_current_hosted_proof(self) -> None:
        workflow = WORKFLOW_PATH.read_text()
        start, end = self.job_boundary(workflow, "product-path")
        block = workflow[start:end]
        self.assertIn("    if: always()\n", block)
        command = block.split("        run: |\n", 1)[1]
        command = "\n".join(line[10:] for line in command.splitlines())
        with tempfile.TemporaryDirectory() as temporary:
            environment = dict(os.environ, CLASSIFY_RESULT="success", HOSTED_RESULT="success",
                PUBLICATION_RECOVERY_RESULT="skipped", CUSTOM_RESULT="skipped",
                CHESS_RESULT="skipped", POSTGRESQL_RESULT="skipped", PACKAGE_RESULT="skipped",
                REQUIRES_CUSTOM="false", REQUIRES_POSTGRESQL="false", REQUIRES_PACKAGE="false",
                REQUIRES_CANDIDATE_CHESS="false", BLOCKED="false", REQUIRED_EVIDENCE='["hosted"]',
                UNIMPLEMENTED_EVIDENCE="[]", GITHUB_STEP_SUMMARY=str(Path(temporary) / "summary"))
            for status in ("success", "failure", "cancelled", "skipped", ""):
                with self.subTest(status=status):
                    execution = subprocess.run(["bash", "-c", command],
                        env=dict(environment, HOSTED_RESULT=status), capture_output=True, timeout=10)
                    self.assertEqual(execution.returncode == 0, status == "success")


if __name__ == "__main__":
    unittest.main()
