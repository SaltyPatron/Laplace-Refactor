#!/usr/bin/env python3
"""Regression tests for the persistent laplace-runner activation boundary."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/product-activation.yml"
SETUP = ROOT / "scripts/setup-host.sh"
SERVICE = ROOT / "packaging/systemd/laplace-refactor-postgresql.service"
CLUSTER = ROOT / "contracts/postgresql-cluster.json"
RUNNER = ROOT / "tools/delivery/product_activation_runner.py"
RECONCILER = ROOT / "tools/delivery/product_activation_reconcile.py"
CLUSTERCTL = ROOT / "tools/postgresql/clusterctl.py"
RESOURCECTL = ROOT / "tools/postgresql/resourcectl.py"
UNICODECTL = ROOT / "tools/postgresql/unicodectl_core.py"
HIGHWAYCTL = ROOT / "tools/postgresql/highwayctl.py"


def load_module(relative_path: str):
    path = ROOT / relative_path
    spec = importlib.util.spec_from_file_location("tested_" + path.stem, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class ProductActivationRunnerTests(unittest.TestCase):
    def cognition_failure_inputs(self):
        identities = {name: "41" * 32 for name in (
            "source_epoch", "identity_epoch", "geometry_epoch", "evidence_epoch",
            "dependency_epoch", "database_epoch", "package_epoch", "numeric_epoch",
            "authority_fingerprint", "request_fingerprint",
        )}
        runtime = {"perfcache_epoch": "42" * 32, "numeric_epoch": "43" * 32}
        firmware = {"program_id": "44" * 32, "image_hex": "abcd"}
        result = {"status": 9, "native_status": 2, "failed_step": 0,
                  "physical_provider_rows": 2, "physical_provider_batches": 1,
                  "output": "\\x", "program_id": "\\x" + firmware["program_id"]}
        diagnostic = {
            "schema": "laplace.cognition-failure-diagnostic/v1",
            "status": 9, "failed_step": 0, "native_status": 2,
            "native_disposition": 4, "physical_provider_rows": 2,
            "physical_provider_batches": 1, "semantic_provider_rows": 0,
            "semantic_database_operations": 0, "materialization_nodes": 0,
            "materialization_trajectory_reads": 0, "materialization_trajectory_bytes": 0,
            "materialization_database_operations": 0,
        }
        return identities, runtime, firmware, result, diagnostic

    def test_installed_cognition_retains_actual_failure_before_raising(self) -> None:
        proof = load_module("tools/delivery/product_cognition_live_proof.py")
        identities, runtime, firmware, result, diagnostic = self.cognition_failure_inputs()
        stdout = json.dumps(result) + "\n"
        stderr = "NOTICE:  LAPLACE_COGNITION_FAILURE " + json.dumps(diagnostic) + "\n"
        provenance = {"package_id": "45" * 32, "postmaster_pid": 123}
        captured_sql = []

        def execute(*args, completed_observer):
            sql = args[2]
            captured_sql.append(sql)
            self.assertTrue(sql.lstrip().startswith("SET client_min_messages = notice;"))
            receipt = {"label": args[3], "exit_code": 0,
                       "stdin_sha256": proof.u.sha256_bytes(sql.encode("utf-8"))}
            completed_observer(subprocess.CompletedProcess(["psql"], 0, stdout, stderr), receipt)
            return result, receipt

        for label in ("installed-product-cognition-falsification",
                      "installed-product-materialization-frontier-falsification"):
            with self.subTest(label=label), tempfile.TemporaryDirectory(dir=ROOT) as directory:
                output = Path(directory) / "installed-cognition-proof.json"
                artifact = Path(directory) / "current-attempt" / "failure.json"
                output.write_text('{"schema":"stale-success"}')
                with mock.patch.object(proof.r, "runner_sql", side_effect=execute), \
                     mock.patch("builtins.print"), \
                     self.assertRaisesRegex(RuntimeError, "returned status 9"):
                    proof.execute_product(
                        {}, {"instance": {"admin_role": "laplace_admin"}}, identities,
                        runtime, firmware, "AA", label,
                        failure_output=output, failure_artifact=artifact,
                        failure_provenance=provenance,
                    )
                self.assertEqual(artifact.read_bytes(), output.read_bytes())
                document = json.loads(output.read_text())
                self.assertEqual(document["schema"], "laplace.installed-product-cognition-failure/v1")
                self.assertIs(document["success_receipt_issued"], False)
                self.assertEqual(document["execution"], result)
                self.assertEqual(document["native_failure"], {"state": "observed", "diagnostic": diagnostic})
                self.assertEqual(document["provenance"], provenance)
                self.assertEqual(document["program"], firmware)
                self.assertEqual(document["prompt_utf8"], "AA")
                self.assertEqual(document["label"], label)
                self.assertEqual(document["request_sql_utf8"], captured_sql[-1])
                self.assertEqual(document["request_sql_sha256"], document["command_receipt"]["stdin_sha256"])
                self.assertNotIn("proof_sha256", document)
                for name, raw in (("stdout", stdout), ("stderr", stderr)):
                    self.assertEqual(bytes.fromhex(document["outputs"][name]["retained_hex"]).decode(), raw)
                    self.assertIs(document["outputs"][name]["truncated"], False)
                identity = document.pop("failure_sha256")
                self.assertEqual(identity, proof.u.sha256_bytes(proof.u.canonical_bytes(document)))
                retained = Path(directory) / f"installed-cognition-failure-{identity}.json"
                self.assertEqual(json.loads(retained.read_text()), {**document, "failure_sha256": identity})

    def test_installed_cognition_failure_diagnostics_are_bounded_and_not_inferred(self) -> None:
        proof = load_module("tools/delivery/product_cognition_live_proof.py")
        _, _, _, result, diagnostic = self.cognition_failure_inputs()
        marker = proof.NATIVE_FAILURE_MARKER
        self.assertEqual(proof.native_failure_diagnostic("", result)["state"], "unavailable")
        self.assertEqual(proof.native_failure_diagnostic(marker + "{", result)["state"], "invalid")
        sentinel = {**diagnostic, "failed_step": (1 << 32) - 1}
        observed = proof.native_failure_diagnostic(marker + json.dumps(sentinel),
                                                  {**result, "failed_step": -1})
        self.assertEqual(observed, {"state": "observed", "diagnostic": sentinel})
        self.assertEqual(proof.native_failure_diagnostic(
            marker + json.dumps(sentinel), {**result, "failed_step": -2})["state"], "mismatched")
        for field in ("status", "failed_step", "native_status", "native_disposition"):
            for value in (-1, True, 1 << 32):
                with self.subTest(field=field, invalid=value):
                    invalid = {**diagnostic, field: value}
                    self.assertEqual(proof.native_failure_diagnostic(
                        marker + json.dumps(invalid), result)["state"], "invalid")
        for field in ("physical_provider_rows", "physical_provider_batches", "semantic_provider_rows",
                      "semantic_database_operations", "materialization_nodes",
                      "materialization_trajectory_reads", "materialization_trajectory_bytes",
                      "materialization_database_operations"):
            for value in (-1, True, 1 << 64):
                with self.subTest(field=field, invalid=value):
                    invalid = {**diagnostic, field: value}
                    self.assertEqual(proof.native_failure_diagnostic(
                        marker + json.dumps(invalid), result)["state"], "invalid")
        for field, value in (("status", 10), ("failed_step", 1), ("native_status", 3)):
            changed = {**diagnostic, field: value}
            self.assertEqual(proof.native_failure_diagnostic(marker + json.dumps(changed), result)["state"],
                             "mismatched")
        for invalid in ({**diagnostic, "native_disposition": True},
                        {**diagnostic, "native_disposition": -1},
                        {key: value for key, value in diagnostic.items() if key != "native_disposition"}):
            self.assertEqual(proof.native_failure_diagnostic(marker + json.dumps(invalid), result)["state"],
                             "invalid")
        line = marker + json.dumps(diagnostic)
        self.assertEqual(proof.native_failure_diagnostic(line + "\n" + line, result)["state"], "invalid")
        raw = "é" * (proof.MAX_FAILURE_OUTPUT_BYTES + 1)
        captured = proof.bounded_output(raw)
        self.assertIs(captured["truncated"], True)
        self.assertEqual(captured["observed_bytes"], len(raw.encode("utf-8")))
        self.assertEqual(captured["observed_sha256"], proof.u.sha256_bytes(raw.encode("utf-8")))
        self.assertEqual(bytes.fromhex(captured["retained_hex"]),
                         raw.encode("utf-8")[:proof.MAX_FAILURE_OUTPUT_BYTES])

    def test_installed_cognition_retains_transport_failure_and_does_not_relabel_success(self) -> None:
        proof = load_module("tools/delivery/product_cognition_live_proof.py")
        identities, runtime, firmware, result, _ = self.cognition_failure_inputs()
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / "proof.json"

            def failed(*args, completed_observer):
                completed_observer(subprocess.CompletedProcess(["psql"], 1, "", "ERROR: native failure"),
                                   {"exit_code": 1})
                raise RuntimeError("SQL transport failed")

            with mock.patch.object(proof.r, "runner_sql", side_effect=failed), \
                 self.assertRaisesRegex(RuntimeError, "SQL transport failed"):
                proof.execute_product({}, {"instance": {"admin_role": "laplace_admin"}},
                                      identities, runtime, firmware, "AA", "transport",
                                      failure_output=output)
            failure = json.loads(output.read_text())
            self.assertIsNone(failure["execution"])
            self.assertEqual(failure["native_failure"]["state"], "unavailable")
            self.assertEqual(failure["command_receipt"]["exit_code"], 1)
            self.assertEqual(bytes.fromhex(failure["outputs"]["stderr"]["retained_hex"]).decode(),
                             "ERROR: native failure")
            success = {**result, "status": 0, "native_status": 0}
            success_output = Path(directory) / "success-proof.json"
            success_artifact = Path(directory) / "success-attempt" / "failure.json"
            with mock.patch.object(proof.r, "runner_sql", return_value=(success, {"exit_code": 0})), \
                 mock.patch("builtins.print"):
                observed, _ = proof.execute_product(
                    {}, {"instance": {"admin_role": "laplace_admin"}}, identities,
                    runtime, firmware, "AA", "success", failure_output=success_output,
                    failure_artifact=success_artifact,
                )
            self.assertEqual(observed, success)
            self.assertFalse(success_output.exists())
            self.assertFalse(success_artifact.exists())
            self.assertEqual(json.loads(output.read_text()), failure)

    def test_installed_cognition_failure_log_matches_retained_evidence_without_raw_output(self) -> None:
        proof = load_module("tools/delivery/product_cognition_live_proof.py")
        _, _, firmware, result, diagnostic = self.cognition_failure_inputs()
        private = "private-prompt-sql-stderr-and-extension-field"
        expanded = {**diagnostic, "future_private_field": private}
        cases = (
            ("observed", proof.NATIVE_FAILURE_MARKER + json.dumps(expanded), result),
            ("mismatched", proof.NATIVE_FAILURE_MARKER + json.dumps(expanded), {**result, "status": 10}),
            ("invalid", proof.NATIVE_FAILURE_MARKER + private, result),
            ("unavailable", private, result),
        )
        for state, stderr, execution in cases:
            with self.subTest(state=state), tempfile.TemporaryDirectory(dir=ROOT) as directory:
                output = Path(directory) / "proof.json"
                artifact = Path(directory) / "attempt.json"
                emitted = []
                def observe_print(encoded, *, flush):
                    self.assertTrue(flush)
                    self.assertEqual(output.read_bytes(), artifact.read_bytes())
                    retained = json.loads(output.read_text())
                    immutable = Path(directory) / (
                        "installed-cognition-failure-" + retained["failure_sha256"] + ".json"
                    )
                    self.assertEqual(json.loads(immutable.read_text()), retained)
                    emitted.append(json.loads(encoded))
                with mock.patch("builtins.print", side_effect=observe_print):
                    proof.retain_execution_failure(
                        output, failure_artifact=artifact, label="installed-failure-control",
                        result=execution, command_receipt={"exit_code": 0},
                        captured={"stderr": stderr, "outputs": {"stderr": proof.bounded_output(stderr)}},
                        sql=private, firmware=firmware, prompt=private,
                        provenance={"package_id": "45" * 32}, error=private,
                    )
                self.assertEqual(len(emitted), 1)
                summary = emitted[0]
                document = json.loads(output.read_text())
                self.assertEqual(set(summary), {"schema", "label", "failure_sha256", "native_failure"})
                self.assertEqual(summary["schema"], "laplace.installed-product-cognition-failure-log/v1")
                self.assertEqual(summary["label"], document["label"])
                self.assertEqual(summary["failure_sha256"], document["failure_sha256"])
                self.assertEqual(summary["native_failure"]["state"], state)
                expected_native = {key: value for key, value in document["native_failure"].items()
                                   if key in ("state", "reason")}
                if "diagnostic" in document["native_failure"]:
                    expected_native["diagnostic"] = {
                        key: document["native_failure"]["diagnostic"][key]
                        for key in ("schema", *proof.NATIVE_FAILURE_COUNTER_FIELDS)
                    }
                    self.assertEqual(document["native_failure"]["diagnostic"]["future_private_field"], private)
                self.assertEqual(summary["native_failure"], expected_native)
                self.assertNotIn(private, json.dumps(summary))
                self.assertLess(len(json.dumps(summary)), 2048)
                identity = document.pop("failure_sha256")
                self.assertEqual(identity, proof.u.sha256_bytes(proof.u.canonical_bytes(document)))

    def test_installed_cognition_failure_log_is_not_issued_before_retention(self) -> None:
        proof = load_module("tools/delivery/product_cognition_live_proof.py")
        _, _, firmware, result, diagnostic = self.cognition_failure_inputs()
        values = dict(
            label="retention-refusal", result=result, command_receipt={"exit_code": 0},
            captured={"stderr": proof.NATIVE_FAILURE_MARKER + json.dumps(diagnostic)},
            sql="SELECT private_input", firmware=firmware, prompt="private_input",
            provenance=None, error="native refusal",
        )
        with mock.patch("builtins.print") as output:
            proof.retain_execution_failure(None, failure_artifact=None, **values)
        output.assert_not_called()
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            with mock.patch.object(proof.u, "write_immutable", side_effect=RuntimeError("retention refused")), \
                 mock.patch("builtins.print") as output, \
                 self.assertRaisesRegex(RuntimeError, "retention refused"):
                proof.retain_execution_failure(
                    Path(directory) / "proof.json", failure_artifact=Path(directory) / "attempt.json",
                    **values,
                )
            output.assert_not_called()
            self.assertFalse((Path(directory) / "attempt.json").exists())

    def test_installed_cognition_request_declares_its_complete_structural_boundary(self) -> None:
        proof = load_module("tools/delivery/product_cognition_live_proof.py")
        multiturn = load_module("tools/delivery/product_cognition_multiturn_live_proof.py")
        identities = {"evidence_epoch": "41" * 32, "request_fingerprint": "42" * 32}
        requests = {
            "single": proof.request_sql(identities, "43" * 32),
            "initial": multiturn.request_sql_turn(identities, "43" * 32, None),
            "continued": multiturn.request_sql_turn(identities, "43" * 32, "44" * 32),
        }
        self.assertIn("decode('" + "00" * 32 + "','hex'),false,", requests["initial"])
        self.assertIn("decode('" + "44" * 32 + "','hex'),true,", requests["continued"])
        for label, request in requests.items():
            with self.subTest(route=label):
                self.assert_cognition_boundary(request)

    def assert_cognition_boundary(self, request: str) -> None:
        fields = re.search(
            r",(\d+),(\d+)\)::laplace\.cognition_firmware_product_request", request,
        )
        self.assertIsNotNone(fields)
        assert fields is not None
        header = (ROOT / "engine/include/laplace/cognition_observation_request.h").read_text()
        boundary = re.search(
            r"LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE = UINT32_C\((\d+)\)",
            header,
        )
        self.assertIsNotNone(boundary)
        assert boundary is not None
        self.assertEqual(int(fields.group(1)), int(boundary.group(1)))
        self.assertEqual(int(fields.group(2)), 1)

    def test_service_executes_same_native_route_with_separate_provider_grant(self) -> None:
        service = load_module("tools/cognition_service.py")
        package = "45" * 32
        program = "46" * 32
        identities = {key: "47" * 32 for key in (
            "source_epoch", "identity_epoch", "geometry_epoch", "evidence_epoch",
            "dependency_epoch", "database_epoch", "request_fingerprint", "package_epoch",
            "numeric_epoch", "authority_fingerprint",
        )}
        runtime = {"unicode_present": True, "highway_present": True,
                   "perfcache_epoch": "48" * 32, "numeric_epoch": "49" * 32}
        initial = {"session_fingerprint": "50" * 32, "discourse_id": "51" * 32,
                   "turn_ordinal": 0}
        continued = {**initial, "turn_ordinal": 1, "previous_package_id": package,
                     "previous_program_id": program, "previous_checkpoint_hex": "abcd",
                     "previous_checkpoint_fingerprint": "52" * 32}
        for relations in (["constituent"], None):
            firmware = {"program_id": program, "image_hex": "abcd",
                        "mode": "auto" if relations is None else "explicit"}
            for session in (initial, continued):
                with self.subTest(relations=relations, turn=session["turn_ordinal"]):
                    # Control only external state and the SQL transport. The real
                    # service executes every request/SQL/response builder here.
                    with mock.patch.object(service, "selected_product", return_value=(package, Path("/selected"))), \
                         mock.patch.object(service, "compile_firmware", return_value=firmware), \
                         mock.patch.object(service, "load_identities", return_value=identities), \
                         mock.patch.object(service, "active_runtime_state", return_value=runtime), \
                         mock.patch.object(service, "run_psql", return_value={"status": 9, "native_status": 2, "output": "\\x"}) as execute:
                        result = service.execute_cognition("AA", relations, session)
                    sql = execute.call_args.args[1]
                    self.assertIn("FROM laplace.cognition_firmware_execute_product(", sql)
                    self.assert_cognition_boundary(sql)
                    grant = re.search(r",(\d+)::bigint,6,2,1023::bigint", sql)
                    workspace = re.search(r"\n  (\d+)::bigint\n\) AS result;", sql)
                    self.assertIsNotNone(grant)
                    self.assertIsNotNone(workspace)
                    assert grant is not None and workspace is not None
                    self.assertEqual(int(grant.group(1)), 1073741824)
                    self.assertEqual(int(workspace.group(1)), 268435456)
                    self.assertLess(int(workspace.group(1)) + 268435456, int(grant.group(1)))
                    self.assertIn(",1048576::numeric,8388608::numeric,", sql)
                    self.assertIn("decode('" + ("52" * 32 if session["turn_ordinal"] else "00" * 32)
                                  + "','hex')," + ("true," if session["turn_ordinal"] else "false,"), sql)
                    self.assertEqual(result["status"], 9)
                    self.assertEqual(result["execution"]["native_status"], 2)
                    self.assertEqual(result["checkpoint_hex"], "")

    @unittest.skipUnless(shutil.which("jq"), "jq is required to execute delivery result predicates")
    def test_workflow_and_setup_accept_only_distinct_complete_revalidation_variant(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")
        workflow_match = re.search(r"--arg repository_commit \"\$GITHUB_SHA\" '\n(.*?)\n          ' \"\$result\"", workflow, re.S)
        setup = (ROOT / "scripts/setup-product.sh").read_text(encoding="utf-8")
        setup_match = re.search(r"jq -e --arg package \"\$package\" '\n(.*?)\n' \"\$work/activation.json\"", setup, re.S)
        self.assertIsNotNone(workflow_match)
        self.assertIsNotNone(setup_match)
        base = {"schema": "laplace.product-activation-result/v1",
                "phase": "product-unicode-activated-and-highway-revalidated",
                "execution_owner": "laplace-runner", "root_product_executor": False,
                "package_id": "42" * 32, "repository_commit": "43" * 20,
                "package_installation_receipt_sha256": "44" * 32,
                "cluster_activation_receipt_sha256": "45" * 32,
                "unicode_activation_receipt_sha256": "46" * 32,
                "highway_revalidation_receipt_sha256": "47" * 32,
                "highway_activation_performed": False,
                "highway_historical_request_present": False,
                "highway_stored_working_set_receipt": "51" * 32,
                "highway_stored_producer_receipt": "52" * 32,
                "highway_historical_composition_receipt_present": False,
                "highway_historical_intermediate_receipts_verified": False,
                "highway_activation_sequence": 1,
                "highway_retained_expected_epoch_count": 1,
                "highway_recovered_expected_epoch_count": 0,
                "result_sha256": "48" * 32}
        normal = copy.deepcopy(base)
        normal["phase"] = "product-unicode-and-highway-activated"
        normal["highway_activation_receipt_sha256"] = normal.pop("highway_revalidation_receipt_sha256")
        normal.pop("highway_activation_performed")
        normal.pop("highway_historical_request_present")
        for field in ("highway_stored_working_set_receipt", "highway_stored_producer_receipt",
                      "highway_historical_composition_receipt_present", "highway_historical_intermediate_receipts_verified",
                      "highway_activation_sequence", "highway_retained_expected_epoch_count", "highway_recovered_expected_epoch_count"):
            normal.pop(field)
        present_composition = {**base, "highway_historical_composition_receipt_present": True}
        mutants = []
        for field, value in (("highway_activation_performed", True),
                ("highway_historical_request_present", True),
                ("highway_historical_composition_receipt_present", 0),
                ("highway_historical_intermediate_receipts_verified", True),
                ("highway_historical_intermediate_receipts_verified", 0),
                ("highway_activation_sequence", True),
                ("highway_activation_sequence", 0),
                ("highway_activation_sequence", 1025),
                ("highway_retained_expected_epoch_count", True),
                ("highway_recovered_expected_epoch_count", "0"),
                ("highway_retained_expected_epoch_count", -1),
                ("highway_recovered_expected_epoch_count", 0.5),
                ("highway_recovered_expected_epoch_count", 1),
                ("highway_stored_working_set_receipt", "00" * 32),
                ("highway_stored_producer_receipt", "00" * 32),
                ("highway_stored_working_set_receipt", "51" * 16),
                ("highway_stored_producer_receipt", "52" * 16),
                ("highway_activation_receipt_sha256", "49" * 32),
                ("highway_revalidation_receipt_sha256", ""),
                ("phase", "product-unicode-and-highway-activated"),
                ("root_product_executor", True), ("package_id", "50" * 32)):
            invalid = copy.deepcopy(base)
            invalid[field] = value
            mutants.append(invalid)
        for field in ("highway_activation_performed", "highway_stored_working_set_receipt",
                      "highway_stored_producer_receipt", "highway_historical_composition_receipt_present",
                      "highway_historical_intermediate_receipts_verified",
                      "highway_activation_sequence", "highway_retained_expected_epoch_count", "highway_recovered_expected_epoch_count"):
            missing = copy.deepcopy(base)
            missing.pop(field)
            mutants.append(missing)
        for source, predicate in (("workflow", workflow_match[1]), ("setup", setup_match[1])):
            mixed = {**base, "highway_activation_sequence": 5,
                     "highway_retained_expected_epoch_count": 2, "highway_recovered_expected_epoch_count": 3}
            recovered = {**base, "highway_retained_expected_epoch_count": 0, "highway_recovered_expected_epoch_count": 1}
            for expected, document in [(True, base), (True, present_composition), (True, normal),
                                       (True, recovered), (True, mixed)] + [(False, mutant) for mutant in mutants]:
                with self.subTest(source=source, phase=document["phase"], expected=expected, document=document):
                    completed = subprocess.run(["jq", "-e", "--arg", "package_id", base["package_id"],
                        "--arg", "package", base["package_id"], "--arg", "repository_commit", base["repository_commit"], predicate],
                        input=json.dumps(document), text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
                    self.assertEqual(completed.returncode == 0, expected, completed.stderr)

    def test_workflow_selects_runner_provider_not_root_or_systemd(self) -> None:
        source = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("product_activation_reconcile.py", source)
        reconciler = RECONCILER.read_text(encoding="utf-8")
        self.assertIn("product_activation_runner.py", reconciler)
        self.assertIn("product_cluster_upgrade.py", reconciler)
        self.assertIn("tools/postgresql/resourcectl.py observe-resources", source)
        self.assertNotIn("tools/postgresql/clusterctl.py observe-resources", source)
        self.assertIn('execution_owner == "laplace-runner"', source)
        self.assertIn("root_product_executor == false", source)
        self.assertIn("pg_ctl", source)
        self.assertIn("sudo -n /usr/bin/systemctl restart laplace-refactor-cognition.service", source)
        self.assertIn("lifecycle_provider", source)
        for forbidden in (
            "laplace-product-activate",
            "LAPLACE_ACTIVATION_HMAC_KEY_B64",
            "compile_gateway_upgrade.py",
            "product_activation.py create-request",
            "gateway-upgrade-request",
            "root gateway",
            "systemctl is-active",
            "systemctl is-enabled",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_bootstrap_service_is_optional_integration_not_product_executor(self) -> None:
        source = SETUP.read_text(encoding="utf-8")
        self.assertIn('SERVICE_SOURCE="$REPOSITORY/packaging/systemd/$SERVICE"', source)
        self.assertIn('"$SYSTEMCTL_BIN" daemon-reload', source)
        self.assertIn('"$SYSTEMCTL_BIN" enable "$SERVICE"', source)
        self.assertIn('"started_by_bootstrap": false', source)
        for forbidden in (
            "build-package.py",
            "clusterctl.py activate-product",
            "unicodectl.py",
            "highwayctl.py",
            "laplace-product-activate",
            "execute-request",
            "initdb",
            "LAPLACE_ACTIVATION_HMAC_KEY_B64",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_static_service_is_optional_boot_integration(self) -> None:
        service = SERVICE.read_text(encoding="utf-8")
        self.assertIn("User=laplace-runner", service)
        self.assertIn("Group=laplace-runner", service)
        self.assertIn(
            "ExecStart=/opt/laplace/runtime/refactor/pgsql-18/bin/postgres",
            service,
        )
        self.assertNotIn("ExecStart=/opt/laplace/current/", service)
        self.assertNotIn("/opt/laplace/releases/", service)
        self.assertNotIn("AllowedCPUs=", service)
        self.assertNotIn("MemoryHigh=", service)
        self.assertNotIn("MemoryMax=", service)

    def test_cluster_contract_has_one_recurring_os_owner_and_runner_socket(self) -> None:
        contract = json.loads(CLUSTER.read_text(encoding="utf-8"))
        instance = contract["instance"]
        security = contract["security"]
        self.assertEqual(instance["os_user"], "laplace-runner")
        self.assertEqual(instance["os_group"], "laplace-runner")
        self.assertEqual(security["admin_os_user"], "laplace-runner")
        self.assertEqual(security["app_os_user"], "laplace-runner")
        self.assertEqual(instance["admin_role"], "laplace_admin")
        self.assertEqual(instance["app_role"], "laplace_app")
        self.assertTrue(instance["socket_directory"].startswith("/opt/laplace/runtime/"))

    def test_runner_provider_declares_pg_ctl_and_no_root_product_executor(self) -> None:
        provider = RUNNER.read_text(encoding="utf-8")
        controller = CLUSTERCTL.read_text(encoding="utf-8")
        self.assertIn('"execution_owner": RUNNER_USER', provider)
        self.assertIn('"root_product_executor": False', provider)
        self.assertIn('"postgresql_lifecycle_provider": clusterctl.LIFECYCLE_PROVIDER', provider)
        self.assertIn('LIFECYCLE_PROVIDER = "pg_ctl"', controller)
        self.assertIn("RUNNER_USER = \"laplace-runner\"", controller)
        self.assertIn("RUNTIME_LINK = \"/opt/laplace/runtime/refactor\"", controller)
        self.assertIn("postmaster.pid", controller)
        self.assertNotIn("laplace-product-activate", provider)
        self.assertNotIn("execute-request", provider)
        self.assertNotIn("runuser", provider)
        self.assertNotIn("sudo", controller)
        self.assertNotIn("systemctl", controller)
        self.assertNotIn('["/usr/sbin/runuser"', controller)
        self.assertNotIn('["runuser"', controller)

    def test_runner_loaded_object_probe_ignores_deleted_pseudo_maps_fail_closed(self) -> None:
        controller = CLUSTERCTL.read_text(encoding="utf-8")
        self.assertIn('if executable.endswith(" (deleted)"):', controller)
        self.assertIn(
            'raise _core.ClusterError(f"process {pid} executable was deleted after start")',
            controller,
        )
        self.assertIn('if path.endswith(" (deleted)"):', controller)
        self.assertIn("required package objects remain fail-closed", controller)
        self.assertIn("missing = sorted(expected_paths - process_paths)", controller)
        self.assertIn("live PostgreSQL backend omits required package objects", controller)

    def test_resource_observer_uses_real_runner_contract_without_legacy_cli(self) -> None:
        source = RESOURCECTL.read_text(encoding="utf-8")
        self.assertIn("clusterctl.validate_contract(contract)", source)
        self.assertIn("clusterctl.verify_package(", source)
        self.assertIn("clusterctl.finalize_native_resource_observation(", source)
        self.assertIn("clusterctl.validate_resource_observation(result, contract)", source)
        self.assertIn("clusterctl.validate_resource_package_binding(result, package)", source)
        self.assertIn("RESOURCE_OBSERVER_PATH", source)
        self.assertNotIn("_validation_contract", source)
        self.assertNotIn("cluster_core", source)
        self.assertNotIn("runuser", source)
        self.assertNotIn("sudo", source)
        self.assertNotIn("systemctl", source)

    def test_unicode_and_highway_restart_requests_are_intercepted_by_provider(self) -> None:
        provider = RUNNER.read_text(encoding="utf-8")
        unicode = UNICODECTL.read_text(encoding="utf-8")
        highway = HIGHWAYCTL.read_text(encoding="utf-8")
        self.assertIn("runner_command", provider)
        self.assertIn("unsupported systemd operation", provider)
        self.assertIn('Path(values[0]).name == "systemctl"', provider)
        self.assertIn("restart-after-unicode-activation", unicode)
        self.assertIn("restart-after-highway-activation", highway)
        self.assertIn('plan["commands"]["stop_candidate"]', provider)
        self.assertIn('plan["commands"]["start_candidate"]', provider)


class IndexedCognitionSuccessorReconciliationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location(
            "laplace_indexed_successor_reconcile_tests", RECONCILER)
        if spec is None or spec.loader is None:
            raise RuntimeError("cannot load actual product reconciliation owner")
        cls.owner = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = cls.owner
        spec.loader.exec_module(cls.owner)

    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="laplace-indexed-successor-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        relative = "pgsql-18/share/extension/laplace--1.0.0--1.0.1.sql"
        self.migration = self.root / relative
        self.migration.parent.mkdir(parents=True)
        self.migration.write_bytes((ROOT / "integrations/postgresql/extension/observation_cognition_persisted.sql.in").read_bytes())
        self.package = {"package_id": "42" * 32, "files": [{
            "path": relative, "kind": "file",
            "sha256": hashlib.sha256(self.migration.read_bytes()).hexdigest()}]}
        self.plan = {"postgresql_major": 18, "package_root": str(self.root)}
        self.contract = {"instance": {"admin_role": "laplace_admin"}}

    def observations(self, version: str) -> list:
        return [({"version": version, "owner": "laplace_admin"}, {}), ({
            "schema": "laplace.indexed-cognition-upgrade/v1", "version": version,
            "owner": "laplace_admin", "native_bindings": 2, "ready_indexes": 2},
            {"fixture": "acknowledged server verification"})]

    def test_supported_successors_verify_inherited_native_bindings_without_downgrade(self) -> None:
        for version in ("1.0.2", "1.0.3", "1.0.4"):
            with self.subTest(version=version), mock.patch.object(
                self.owner.runner, "runner_sql", side_effect=self.observations(version)
            ) as sql, mock.patch.object(self.owner, "fresh_reconcile_indexed_cognition") as predecessor:
                receipt = self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                    self.plan, self.contract, self.package)
                predecessor.assert_not_called()
                self.assertEqual(sql.call_count, 2)
                self.assertEqual(receipt["version"], version)
                self.assertEqual(receipt["script_sha256"], self.package["files"][0]["sha256"])
                self.assertEqual(receipt["package_id"], self.package["package_id"])
                self.assertEqual(receipt["receipt_sha256"],
                    self.owner.runner.document_identity(receipt, "receipt_sha256"))

    def test_new_successor_refuses_changed_package_migration_before_database_reconciliation(self) -> None:
        self.migration.write_bytes(self.migration.read_bytes() + b"\n-- changed package bytes\n")
        with mock.patch.object(self.owner.runner, "runner_sql",
                side_effect=self.observations("1.0.4")) as sql:
            with self.assertRaisesRegex(self.owner.runner.RunnerActivationError, "migration bytes differ"):
                self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                    self.plan, self.contract, self.package)
            self.assertEqual(sql.call_count, 1)

    def test_new_successor_refuses_version_or_native_proof_drift(self) -> None:
        for field, changed in (("version", "1.0.5"), ("owner", "other_role"),
                ("native_bindings", 1), ("ready_indexes", 1)):
            observed = self.observations("1.0.4")
            observed[1][0][field] = changed
            with self.subTest(field=field), mock.patch.object(
                self.owner.runner, "runner_sql", side_effect=observed):
                with self.assertRaisesRegex(self.owner.runner.RunnerActivationError, "result differs"):
                    self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                        self.plan, self.contract, self.package)

    def test_unknown_version_uses_existing_version_admission_owner(self) -> None:
        with mock.patch.object(self.owner.runner, "runner_sql",
                return_value=({"version": "1.0.5"}, {})) as sql, mock.patch.object(
                self.owner, "fresh_reconcile_indexed_cognition", return_value={"deferred": True}) as predecessor:
            result = self.owner.reconcile_indexed_cognition_after_generation_upgrade(
                self.plan, self.contract, self.package)
            predecessor.assert_called_once_with(self.plan, self.contract, self.package)
            self.assertEqual(sql.call_count, 1)
            self.assertEqual(result, {"deferred": True})


if __name__ == "__main__":
    unittest.main(verbosity=2)
