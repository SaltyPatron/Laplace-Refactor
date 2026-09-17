#!/usr/bin/env python3
from __future__ import annotations

from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "tools/openai_api_compat.py"
INSPECT = ROOT / "tools/laplace_inspect.py"
SOURCE = ROOT / "tools/source_product.py"
INDEX = ROOT / "product/web/index.html"
WEB = ROOT / "product/web/app.js"
LIVE = ROOT / "tools/delivery/product_gateway_live_proof.py"
WORKFLOW = ROOT / ".github/workflows/product-cognition-multiturn.yml"
PACKAGE = ROOT / "contracts/product-package.json"


class ProductGatewayContractTests(unittest.TestCase):
    def test_chat_profile_rejects_flattened_history_and_live_proves_declared_boundary(self) -> None:
        wrapper = WRAPPER.read_text(encoding="utf-8")
        live = LIVE.read_text(encoding="utf-8")
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("unsupported_conversation_history", wrapper)
        self.assertIn("unsupported_message_role", wrapper)
        self.assertNotIn("OpenAI-compatible conversation transcript:", wrapper)
        self.assertIn('"declared_boundary": "single-user-message"', live)
        self.assertIn('"client_supplied_role_history": False', live)
        self.assertIn('"stream": True', live)
        self.assertIn('.openai_chat.declared_boundary == "single-user-message"', workflow)
        self.assertIn('.openai_chat.client_supplied_role_history == false', workflow)
        self.assertIn('.openai_chat.roles == ["user"]', workflow)
        self.assertIn('.openai_chat.streaming.done == true', workflow)

    def test_product_navigation_is_world_first_not_admin_first(self) -> None:
        index = INDEX.read_text(encoding="utf-8")
        self.assertIn('data-workspace="explore"', index)
        self.assertIn('data-workspace="chat"', index)
        self.assertIn('data-workspace="operator"', index)
        self.assertNotIn('class="nav-item" data-workspace="graph"', index)
        self.assertNotIn('class="nav-item" data-workspace="sources"', index)
        self.assertNotIn('class="nav-item" data-workspace="sql"', index)
        for label in ("Entities", "Consensus", "Rankings", "Evidence", "Sources", "Physicality", "Occurrences"):
            self.assertIn(f">{label}</button>", index)

    def test_explore_uses_real_persisted_facets_and_entity_context(self) -> None:
        inspect = INSPECT.read_text(encoding="utf-8")
        web = WEB.read_text(encoding="utf-8")
        for route in ("/api/v1/consensus", "/api/v1/evidence", "/api/v1/standings"):
            self.assertIn(route, web)
        self.assertIn("def consensus_sql", inspect)
        self.assertIn("def evidence_sql", inspect)
        self.assertIn("def standings_sql", inspect)
        self.assertIn("'consensus',(SELECT", inspect)
        self.assertIn("'evidence',(SELECT", inspect)
        self.assertIn('/api/v1/entities/${encodeURIComponent(entityId)}', web)
        self.assertIn('No identity was invented.', web)

    def test_source_ui_cannot_submit_caller_server_paths(self) -> None:
        web = WEB.read_text(encoding="utf-8")
        self.assertIn('/api/v1/source-catalog', web)
        self.assertIn('/api/v1/sources/preflight', web)
        self.assertIn('/api/v1/source-jobs/', web)
        self.assertNotIn('source_root:', web)
        self.assertNotIn('unicode_root:', web)

    def test_source_job_is_persisted_before_worker_launch(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        write = source.index("atomic_write_json(job_path, job)")
        launch = source.index("subprocess.Popen(", write)
        self.assertLess(write, launch)
        self.assertIn('job_id = sha256_bytes((expected_plan_id + "\\0" + key).encode("utf-8"))', source)
        self.assertIn('plan = preflight(source_id, metadata, estate)', source)
        self.assertIn('if plan["plan_id"] != expected_plan_id:', source)

    def test_package_requires_new_product_owners(self) -> None:
        package = PACKAGE.read_text(encoding="utf-8")
        for required in (
            '"bin/laplace-openai-api"',
            '"bin/laplace-openai-api-core"',
            '"bin/laplace-openai-api-transport"',
            '"bin/laplace-source-product"',
            '"bin/laplace-source-job-worker"',
            '"share/laplace/source-contracts/selected-source-boundary.json"',
        ):
            self.assertIn(required, package)

    def test_activated_proof_requires_durable_source_identity(self) -> None:
        live = LIVE.read_text(encoding="utf-8")
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn('idempotent_retry_same_job', live)
        self.assertIn('"result_sha256": result_sha', live)
        self.assertIn('.source_ingestion.idempotent_retry_same_job == true', workflow)
        self.assertIn('.source_ingestion.state == "succeeded"', workflow)
        self.assertIn('.source_ingestion.result_sha256 | test("^[0-9a-f]{64}$")', workflow)


class InstalledHttpReadinessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location("gateway_readiness_test", LIVE)
        assert spec is not None and spec.loader is not None
        cls.proof = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.proof)
        cls.owner = cls.proof.package_owner()

    def fixture(self, root: Path) -> dict:
        owner = self.owner
        contract = owner.load_json(ROOT / "contracts/postgresql-cluster.json")
        assets = {
            "/": ("text/html", b"<!doctype html><title>Laplace</title>"),
            "/app.js": ("text/javascript", b"window.laplace = 'selected';\n"),
            "/styles.css": ("text/css", b"body { color: black; }\n"),
        }
        files = {
            name: {"path": name, "kind": "file", "mode": 0o644,
                   "sha256": owner.sha256_bytes(b"fixture"), "runpath": []}
            for name in contract["package"]["required_files"]
        }
        for route, name, _ in self.proof.WEB_ASSETS:
            relative = "share/laplace/web/" + name
            files[relative] = {"path": relative, "kind": "file", "mode": 0o644,
                               "sha256": owner.sha256_bytes(assets[route][1]), "runpath": []}
        manifest = {
            "schema": owner.PACKAGE_SCHEMA,
            "postgresql": {"version": contract["package"]["postgresql_version"],
                           "pg_config": "pgsql-18/bin/pg_config"},
            "capabilities": contract["package"]["required_capabilities"],
            "activation_eligible": True, "activation_gates": {"fixture": True},
            "loader_environment": {}, "files": list(files.values()),
            "loaded_objects": contract["package"]["required_loaded_objects"],
        }
        package_id = owner.package_identity(manifest)
        manifest["package_id"] = package_id
        manifest["root"] = contract["package"]["release_root"] + "/" + package_id
        release = owner.prefixed(root, manifest["root"])
        web = release / "share/laplace/web"
        web.mkdir(parents=True)
        for route, name, _ in self.proof.WEB_ASSETS:
            (web / name).write_bytes(assets[route][1])
            (web / name).chmod(0o644)
        active = owner.prefixed(root, contract["package"]["active_link"])
        runtime = owner.prefixed(root, "/opt/laplace/runtime/refactor")
        active.parent.mkdir(parents=True, exist_ok=True)
        runtime.parent.mkdir(parents=True, exist_ok=True)
        active.symlink_to("releases/" + package_id)
        runtime.symlink_to("../releases/" + package_id)
        product = owner.load_json(ROOT / "contracts/product-package.json")["build"]
        build_root = owner.prefixed(root, product["root"])
        stage_root = owner.prefixed(root, product["stage_root"])
        build = build_root / ("1" * 64)
        build.mkdir(parents=True)
        physical = stage_root / ("1" * 64) / "root" / manifest["root"].lstrip("/")
        physical.mkdir(parents=True)
        manifest_path = build / "package-manifest.json"
        manifest_path.write_bytes(owner.canonical_bytes(manifest))
        product_receipt = build / "package-receipt.json"
        product_receipt.write_text(json.dumps({
            "schema": "laplace.product-package-receipt/v1", "package_id": package_id,
            "plan_sha256": "2" * 64, "physical_root": str(physical),
            "manifest": str(manifest_path), "manifest_sha256": owner.sha256_file(manifest_path),
            "activation_eligible": True, "build_input_closure_complete": True,
            "product_activated": False,
        }))
        health = {"schema": "laplace.product.health/v1", "status": "ready",
                  "web_root": str(web),
                  "cognition": {"status": "ready", "package_id": package_id,
                                "unicode_present": True, "highway_present": True}}
        responses = {path: (200, media, body) for path, (media, body) in assets.items()}
        responses["/health"] = (200, "application/json", json.dumps(health).encode())
        return {"root": root, "package_id": package_id, "release": release,
                "active": active, "runtime": runtime, "manifest": manifest_path,
                "product_receipt": product_receipt, "health": health,
                "responses": responses, "calls": [], "after_get": None}

    @contextmanager
    def server(self, fixture: dict):
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                fixture["calls"].append(self.path)
                fixture.setdefault("authorization", []).append(self.headers.get("Authorization"))
                if "wire_prefix" in fixture:
                    try:
                        self.connection.sendall(fixture["wire_prefix"])
                        if fixture.get("wire_silent"):
                            fixture["wire_stop"].wait(2)
                        for byte in fixture["wire_trickle"]:
                            if fixture["wire_stop"].is_set():
                                break
                            self.connection.sendall(bytes([byte]))
                            fixture["sent"] += 1
                            if fixture["wire_stop"].wait(0.005):
                                break
                    except OSError:
                        pass
                    finally:
                        fixture["wire_stopped"].set()
                    return
                status, media, body = fixture["responses"].get(
                    self.path, (404, "text/plain", b"not found"))
                if fixture.get("required_token") and self.path not in {"/", "/app.js", "/styles.css"}:
                    if self.headers.get("Authorization") != "Bearer " + fixture["required_token"]:
                        status, media, body = 401, "application/json", b'{"error":"unauthorized"}'
                self.send_response(status)
                self.send_header("Content-Type", media)
                self.send_header("Content-Length", str(len(body)))
                if status == 302:
                    self.send_header("Location", "/app.js")
                self.end_headers()
                if fixture["after_get"] is not None:
                    fixture["after_get"](self.path)
                if fixture.get("trickle") and self.path == "/health":
                    for byte in body:
                        try:
                            self.wfile.write(bytes([byte]))
                            self.wfile.flush()
                            fixture["sent"] += 1
                            time.sleep(0.005)
                        except OSError:
                            break
                else:
                    self.wfile.write(body)

            def log_message(self, *_args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            with mock.patch.object(self.proof, "BASE", f"http://127.0.0.1:{server.server_port}"):
                yield
        finally:
            if "wire_stop" in fixture:
                fixture["wire_stop"].set()
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)

    def test_selected_authority_rejects_anonymous_health_and_stream_then_reads_bound_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            fixture["required_token"] = "proof-fixture-only"
            output = fixture["root"] / "authenticated-readiness.json"
            with self.server(fixture), \
                    mock.patch.object(self.proof, "AUTH_TOKEN", fixture["required_token"]), \
                    mock.patch.object(self.proof, "AUTH_SELECTION", {"mode": "managed-operator"}):
                self.proof.prove_readiness(output, fixture["package_id"],
                    fixture["product_receipt"], root=fixture["root"])
            report = json.loads(output.read_text())
            self.assertEqual(report["status"], "passed")
            self.assertEqual(report["anonymous_protected_status"], 401)
            self.assertEqual(report["http_auth"], {"mode": "managed-operator"})
            self.assertEqual(fixture["calls"][:3], ["/health", "/api/v1/stream", "/health"])
            self.assertEqual(fixture["authorization"], [None, None, "Bearer proof-fixture-only", None, None, None])
            self.assertNotIn("proof-fixture-only", output.read_text())

    def test_gets_exact_selected_assets_and_retains_bound_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            output = fixture["root"] / "readiness.json"
            with self.server(fixture), mock.patch.dict("os.environ", {
                    "http_proxy": "http://127.0.0.1:1", "HTTP_PROXY": "http://127.0.0.1:1",
                    "all_proxy": "http://127.0.0.1:1", "ALL_PROXY": "http://127.0.0.1:1",
                    "no_proxy": "", "NO_PROXY": ""}):
                self.proof.prove_readiness(output, fixture["package_id"],
                                          fixture["product_receipt"], root=fixture["root"])
            receipt = json.loads(output.read_text())
            self.assertEqual(receipt["status"], "passed")
            self.assertEqual(fixture["calls"], ["/health", "/", "/app.js", "/styles.css"])
            self.assertEqual(receipt["health"]["cognition_package_id"], fixture["package_id"])
            self.assertEqual(receipt["package_manifest_sha256"], self.owner.sha256_file(fixture["manifest"]))
            self.assertEqual(len(receipt["assets"]), 3)
            for asset in receipt["assets"]:
                self.assertEqual(asset["sha256"], self.owner.sha256_bytes(fixture["responses"][asset["path"]][2]))


    def test_http_readiness_uses_exact_retained_metadata_after_actual_reclamation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            metadata = self.proof.metadata_owner()
            original = fixture["product_receipt"].read_bytes()
            original_manifest = fixture["manifest"]
            build_root = original_manifest.parent.parent
            stage_root = build_root.parent / "stage"
            result = metadata.reconcile(
                {"build": {"root": str(build_root), "stage_root": str(stage_root)}},
                receipt_root=build_root.parent / "retention", preserve=set(),
                minimum_age_seconds=0,
            )
            retained = Path(result["removed"][0]["retained_metadata"])
            self.assertFalse(original_manifest.exists())
            # A new incomplete execution of the same plan is disposable state.
            original_manifest.parent.mkdir()
            original_manifest.write_bytes(b"partial new build")
            selected = retained / "package-receipt.json"
            output = fixture["root"] / "readiness-retained.json"
            with self.server(fixture):
                self.proof.prove_readiness(output, fixture["package_id"], selected,
                                          root=fixture["root"])
            report = json.loads(output.read_text())
            self.assertEqual(report["status"], "passed")
            self.assertEqual(report["package_metadata"]["selection"], "retained")
            self.assertEqual(report["package_metadata"]["manifest_path"],
                             str(retained / "package-manifest.json"))
            self.assertEqual(report["package_metadata"]["original_manifest_path"],
                             str(original_manifest))
            self.assertEqual(selected.read_bytes(), original)
            self.assertEqual(fixture["calls"], ["/health", "/", "/app.js", "/styles.css"])
            (retained / "package-manifest.json").write_bytes(b'{"changed":true}')
            fixture["calls"].clear()
            with self.server(fixture), self.assertRaises(RuntimeError):
                self.proof.prove_readiness(fixture["root"] / "refused-retained.json",
                                          fixture["package_id"], selected, root=fixture["root"])
            self.assertEqual(fixture["calls"], [])

    def test_actual_http_failures_cannot_certify_installed_readiness(self) -> None:
        cases = ("wrong-package", "wrong-web-root", "inactive-native", "malformed-health",
                 "non-200", "redirect", "wrong-asset", "wrong-content-type",
                 "oversized-response", "slow-response", "stale-selection", "asset-drift")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                fixture = self.fixture(Path(directory))
                output = fixture["root"] / "readiness.json"
                if case == "wrong-package":
                    fixture["health"]["cognition"]["package_id"] = "ff" * 32
                elif case == "wrong-web-root":
                    fixture["health"]["web_root"] = str(fixture["root"] / "other-web")
                elif case == "inactive-native":
                    fixture["health"]["cognition"]["highway_present"] = False
                fixture["responses"]["/health"] = (200, "application/json", json.dumps(fixture["health"]).encode())
                if case == "malformed-health":
                    fixture["responses"]["/health"] = (200, "application/json", b"{")
                elif case == "non-200":
                    fixture["responses"]["/health"] = (503, "application/json", b"{}")
                elif case == "redirect":
                    fixture["responses"]["/health"] = (302, "text/plain", b"")
                elif case == "wrong-asset":
                    fixture["responses"]["/app.js"] = (200, "text/javascript", b"stale bytes")
                elif case == "wrong-content-type":
                    body = fixture["responses"]["/app.js"][2]
                    fixture["responses"]["/app.js"] = (200, "text/html", body)
                elif case == "oversized-response":
                    fixture["responses"]["/health"] = (200, "application/json",
                        b" " * (self.proof.READINESS_RESPONSE_BYTES + 1))
                elif case == "slow-response":
                    fixture["trickle"], fixture["sent"] = True, 0
                elif case in ("stale-selection", "asset-drift"):
                    def drift(path, fixture=fixture, case=case):
                        if path == "/":
                            if case == "stale-selection":
                                fixture["runtime"].unlink()
                                fixture["runtime"].symlink_to("../releases/" + "ff" * 32)
                            else:
                                (fixture["release"] / "share/laplace/web/styles.css").write_bytes(b"changed")
                    fixture["after_get"] = drift
                with self.server(fixture), \
                        mock.patch.object(self.proof, "READINESS_TIMEOUT_SECONDS",
                                          0.05 if case == "slow-response" else 30.0), \
                        self.assertRaises((RuntimeError, OSError, ValueError)):
                    self.proof.prove_readiness(output, fixture["package_id"],
                                              fixture["product_receipt"], root=fixture["root"])
                receipt = json.loads(output.read_text())
                self.assertEqual(receipt["status"], "failed")
                self.assertTrue(receipt["failure"])
                self.assertTrue(all(path in {"/health", "/", "/app.js", "/styles.css"} for path in fixture["calls"]))
                if case == "redirect":
                    self.assertEqual(fixture["calls"], ["/health"])
                if case == "slow-response":
                    self.assertLess(fixture["sent"], len(fixture["responses"]["/health"][2]))

    def test_shared_wall_deadline_bounds_status_headers_body_and_chunk_framing(self) -> None:
        ordinary = (b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    b"Content-Length: 512\r\nConnection: close\r\n\r\n")
        chunked = (b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                   b"Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n")
        cases = {
            "silent-status": (b"", b""),
            "status": (b"", b"HTTP/1.1 200 " + b"O" * 512 + b"\r\n"),
            "headers": (b"HTTP/1.1 200 OK\r\n", b"X-Probe: " + b"a" * 512 + b"\r\n"),
            "body": (ordinary, b"x" * 512),
            "chunk-size": (chunked, b"0" * 512 + b"1\r\nx\r\n0\r\n\r\n"),
            "chunk-trailer": (chunked + b"1\r\nx\r\n0\r\n", b"X-Probe: " + b"a" * 512 + b"\r\n\r\n"),
        }
        for phase, (prefix, trickle) in cases.items():
            with self.subTest(phase=phase), tempfile.TemporaryDirectory() as directory:
                fixture = self.fixture(Path(directory))
                fixture.update(wire_prefix=prefix, wire_trickle=trickle,
                               wire_silent=phase == "silent-status", sent=0,
                               wire_stop=threading.Event(), wire_stopped=threading.Event())
                output = fixture["root"] / "readiness.json"
                with self.server(fixture), \
                        mock.patch.object(self.proof, "READINESS_TIMEOUT_SECONDS", 0.075):
                    started = time.monotonic()
                    with self.assertRaisesRegex(RuntimeError, "deadline expired"):
                        self.proof.prove_readiness(output, fixture["package_id"],
                                                  fixture["product_receipt"], root=fixture["root"])
                    elapsed = time.monotonic() - started
                    # The server keeps making progress every 5 ms for more than
                    # 2 seconds. An inactivity timeout alone must fail this bound.
                    self.assertLess(elapsed, 0.75, phase)
                    receipt = json.loads(output.read_text())
                    self.assertEqual(receipt["status"], "failed")
                    self.assertIn("deadline expired", receipt["failure"])
                    self.assertEqual(fixture["calls"], ["/health"])
                    if trickle:
                        self.assertLess(fixture["sent"], len(trickle))
                self.assertTrue(fixture["wire_stopped"].wait(1), phase)

    def test_http_readiness_refuses_non_loopback_endpoints_before_connect(self) -> None:
        for endpoint in ("https://127.0.0.1:55434", "http://localhost:55434",
                         "http://127.0.0.1:55434/other", "http://user@127.0.0.1:55434"):
            with self.subTest(endpoint=endpoint), mock.patch.object(self.proof, "BASE", endpoint), \
                    mock.patch.object(self.proof, "HTTPConnection") as connect, \
                    self.assertRaisesRegex(RuntimeError, "direct IPv4 loopback"):
                self.proof.readiness_get("/health", time.monotonic() + 1)
            connect.assert_not_called()

    def test_manifest_and_installed_asset_tampering_fail_before_http(self) -> None:
        for case in ("manifest", "canonical-manifest", "installed-asset", "asset-symlink"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                fixture = self.fixture(Path(directory))
                asset = fixture["release"] / "share/laplace/web/app.js"
                if case == "manifest":
                    fixture["manifest"].write_bytes(fixture["manifest"].read_bytes() + b" ")
                elif case == "canonical-manifest":
                    manifest = json.loads(fixture["manifest"].read_text())
                    manifest["activation_gates"]["fixture"] = False
                    fixture["manifest"].write_bytes(self.owner.canonical_bytes(manifest))
                    selected = json.loads(fixture["product_receipt"].read_text())
                    selected["manifest_sha256"] = self.owner.sha256_file(fixture["manifest"])
                    fixture["product_receipt"].write_text(json.dumps(selected))
                elif case == "installed-asset":
                    asset.write_bytes(b"modified")
                else:
                    elsewhere = fixture["root"] / "copied.js"
                    elsewhere.write_bytes(asset.read_bytes())
                    asset.unlink()
                    asset.symlink_to(elsewhere)
                with self.server(fixture), self.assertRaises(RuntimeError):
                    self.proof.prove_readiness(fixture["root"] / "readiness.json", fixture["package_id"],
                                              fixture["product_receipt"], root=fixture["root"])
                self.assertEqual(fixture["calls"], [])

    def test_existing_receipt_is_not_overwritten_or_reused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            output = fixture["root"] / "readiness.json"
            original = b'{"status":"passed","old":true}\n'
            output.write_bytes(original)
            with self.server(fixture), self.assertRaises(FileExistsError):
                self.proof.prove_readiness(output, fixture["package_id"],
                                          fixture["product_receipt"], root=fixture["root"])
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(fixture["calls"], [])

    def test_expired_deadline_does_not_issue_a_success_or_http_request(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(Path(directory))
            output = fixture["root"] / "readiness.json"
            with self.server(fixture), mock.patch.object(self.proof, "READINESS_TIMEOUT_SECONDS", 0), \
                    self.assertRaisesRegex(RuntimeError, "deadline"):
                self.proof.prove_readiness(output, fixture["package_id"],
                                          fixture["product_receipt"], root=fixture["root"])
            self.assertEqual(json.loads(output.read_text())["status"], "failed")
            self.assertEqual(fixture["calls"], [])

    def test_default_cli_keeps_existing_full_gateway_proof(self) -> None:
        with mock.patch.object(self.proof, "prove") as full, \
                mock.patch.object(self.proof, "selected_http_authority", return_value=({"mode": "none"}, None)), \
                mock.patch.object(self.proof, "AUTH_TOKEN", None), \
                mock.patch.object(self.proof, "AUTH_SELECTION", {"mode": "none"}), \
                mock.patch.object(self.proof, "prove_readiness") as readiness, \
                mock.patch("sys.argv", ["proof", "--output", "/tmp/proof.json", "--package-id", "ab" * 32]):
            self.assertEqual(self.proof.main(), 0)
        full.assert_called_once_with(Path("/tmp/proof.json"), "ab" * 32, include_source_ingestion=False)
        readiness.assert_not_called()

    def test_activation_uses_selected_receipt_and_retains_get_only_proof(self) -> None:
        workflow = (ROOT / ".github/workflows/product-activation.yml").read_text()
        step = workflow.split("      - name: Start and execute the installed Laplace cognition surface", 1)[1]
        step = step.split("      - name: Observe retained Highway", 1)[0]
        self.assertIn("--readiness-only", step)
        self.assertIn("--product-receipt '${{ needs.compose-product.outputs.product_receipt }}'", step)
        self.assertNotIn("--include-source-ingestion", step)
        self.assertIn("always() && env.LAPLACE_INSTALLED_HTTP_READINESS != ''", step)
        self.assertIn("installed-http-readiness-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}.json", step)
        self.assertIn("http://127.0.0.1:55434", step)
        self.assertNotIn("no TCP listener", step)


class FullGatewayProofLifecycleTests(unittest.TestCase):
    def setUp(self) -> None:
        specification = importlib.util.spec_from_file_location("full_gateway_lifecycle", LIVE)
        assert specification is not None and specification.loader is not None
        self.proof = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(self.proof)

    def test_repeated_attempts_have_distinct_sessions_and_retain_completed_native_turns(self) -> None:
        sessions = {}
        def http(method, path, payload=None, timeout=320.0):
            if path == "/":
                return 200, "text/html", INDEX.read_bytes()
            if path == "/app.js":
                return 200, "application/javascript", WEB.read_bytes()
            raise AssertionError(path)
        def json_http(method, path, payload=None, timeout=320.0):
            if path == "/api/v1/health":
                return {"schema": "laplace.product.health/v1", "status": "ready"}
            if path == "/api/v1/descriptors":
                return {"schema": "laplace.product.descriptors/v1",
                        "transports": {"mcp": "/mcp", "openai": "/v1"},
                        "explore": {"schema": "laplace.product.explore/v1"},
                        "source_ingestion": {"caller_supplied_server_path": False,
                                             "idempotent_durable_jobs": True}}
            if path == "/api/v1/cognition":
                session = payload["session"]
                ordinal = sessions.get(session, 0)
                sessions[session] = ordinal + 1
                return {"status": 0, "output_utf8": "A", "turn_ordinal": ordinal,
                        "continued": ordinal > 0,
                        "next_checkpoint_fingerprint": ("%02x" % (ordinal + 1)) * 32}
            if path == "/v1/chat/completions":
                raise RuntimeError("controlled OpenAI transport interruption")
            raise AssertionError(path)
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(self.proof, "http", side_effect=http), \
                mock.patch.object(self.proof, "json_http", side_effect=json_http), \
                mock.patch.object(self.proof, "prove_explore", return_value={"controlled_transport": True}):
            for index in range(2):
                output = Path(directory) / f"attempt-{index}.json"
                with self.assertRaisesRegex(RuntimeError, "controlled OpenAI"):
                    self.proof.prove(output, "ab" * 32)
                report = json.loads(output.read_text())
                self.assertEqual(report["status"], "failed")
                self.assertEqual(report["completed_phases"],
                    ["browser", "health", "descriptors", "explore",
                     "cognition_turn_0", "cognition_turn_1"])
                self.assertEqual(report["partial"]["cognition_turn_0"]["ordinal"], 0)
                self.assertEqual(report["partial"]["cognition_turn_1"]["ordinal"], 1)
                self.assertNotIn("openai_nonstream", report["partial"])
                self.assertEqual(report["failure"]["type"], "RuntimeError")
                self.assertGreaterEqual(report["elapsed_seconds"], 0)
        self.assertEqual(len(sessions), 2)
        self.assertEqual(set(sessions.values()), {2})
        for session in sessions:
            self.assertTrue(session.startswith("proof-" + ("ab" * 12) + "-"))
            self.assertRegex(session.rsplit("-", 1)[1], r"^[0-9a-f]{32}$")

    def test_initial_failure_is_retained_and_prior_proof_is_never_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "proof.json"
            with mock.patch.object(self.proof, "http", side_effect=RuntimeError("actual transport boundary failed")):
                with self.assertRaisesRegex(RuntimeError, "transport boundary"):
                    self.proof.prove(output, "ab" * 32)
            before = output.read_bytes()
            report = json.loads(before)
            self.assertEqual(report["status"], "failed")
            self.assertEqual(report["completed_phases"], [])
            self.assertEqual(report["partial"], {})
            with mock.patch.object(self.proof, "http") as request, self.assertRaises(FileExistsError):
                self.proof.prove(output, "ab" * 32)
            request.assert_not_called()
            self.assertEqual(output.read_bytes(), before)

    def test_activation_runs_existing_full_owner_after_readiness_with_bounded_retention(self) -> None:
        workflow = (ROOT / ".github/workflows/product-activation.yml").read_text()
        step = workflow.split("      - name: Start and execute the installed Laplace cognition surface", 1)[1]
        step = step.split("      - name: Observe retained Highway", 1)[0]
        self.assertEqual(step.count("python3 tools/delivery/product_gateway_live_proof.py"), 2)
        self.assertLess(step.index("--readiness-only"), step.index("for attempt in"))
        self.assertLess(step.index("--readiness-only"), step.index('gateway_root='))
        self.assertIn("timeout --signal=TERM --kill-after=10s 300s", step)
        self.assertIn('--package-id "$package_id" --output "$gateway_root/proof.json"', step)
        self.assertIn('>"$gateway_root/stdout.log" 2>"$gateway_root/stderr.log"', step)
        self.assertIn("always() && env.LAPLACE_INSTALLED_GATEWAY_PROOF != ''", step)
        self.assertIn('.status == "passed"', step)
        self.assertNotIn("--include-source-ingestion", step)


class ManagedOperatorTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import sys
        spec = importlib.util.spec_from_file_location("managed_operator_api_tests",
            ROOT / "tools/openai_api_service.py")
        cls.api = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = cls.api
        spec.loader.exec_module(cls.api)
        spec = importlib.util.spec_from_file_location("managed_operator_gateway_proof_tests", LIVE)
        cls.proof = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.proof)

    def test_existing_assignment_is_parsed_without_shell_and_plain_tokens_are_preserved(self):
        token = "fixture-only-AZ09_=/+-" * 3
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "operator.env"
            for newline in ("", "\n"):
                path.write_text("LAPLACE_OPERATOR_TOKEN=" + token + newline)
                self.assertEqual(self.api.load_operator_token(path), token)
            for invalid in ('export LAPLACE_OPERATOR_TOKEN=' + token,
                            'LAPLACE_OPERATOR_TOKEN="' + token + '"',
                            "LAPLACE_OPERATOR_TOKEN=$(do-not-evaluate)",
                            "LAPLACE_OPERATOR_TOKEN=" + token + "\nOTHER=value\n",
                            "LAPLACE_OPERATOR_TOKEN=short",
                            "LAPLACE_OPERATOR_TOKEN=" + "a" * 65536):
                path.write_text(invalid)
                with self.assertRaises(ValueError) as raised:
                    self.api.load_operator_token(path)
                self.assertNotIn(invalid, str(raised.exception))
            path.write_text(token + "\n")
            self.assertEqual(self.api.load_bearer_token(path), token)
            self.assertIsNone(self.api.load_bearer_token(None))

    def test_actual_argument_parser_keeps_standalone_and_explicit_authorities_distinct(self):
        import sys
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "operator.env"
            token = "fixture-only-" * 4
            path.write_text("LAPLACE_OPERATOR_TOKEN=" + token + "\n")
            server = mock.Mock()
            with mock.patch.object(self.api, "ThreadingHTTPServer", return_value=server), \
                    mock.patch.object(self.api.Handler, "bearer_token", None):
                for arguments, expected in (([], None), (["--operator-token-file", str(path)], token)):
                    with mock.patch.object(sys, "argv", ["api", *arguments]):
                        self.assertEqual(self.api.main(), 0)
                    self.assertEqual(self.api.Handler.bearer_token, expected)
                with mock.patch.object(sys, "argv", ["api", "--operator-token-file", str(path),
                                                   "--bearer-token-file", str(path)]):
                    with self.assertRaises(SystemExit) as error:
                        self.api.main()
                    self.assertEqual(error.exception.code, 2)

    @contextmanager
    def actual_gateway(self):
        import http.client
        token = "fixture-only-" * 4
        package = "ab" * 32
        api = self.api
        class Handler(api.Handler):
            bearer_token = token
            web_root = ROOT / "product/web"
            def log_message(self, *_args):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        with mock.patch.object(api, "health_payload", return_value={
                "status": "ready", "package_id": package,
                "unicode_present": True, "highway_present": True}), \
                mock.patch.object(api, "inspect_value", return_value={
                    "schema": "laplace.inspect.summary/v1", "counts": {}}), \
                mock.patch.object(self.proof, "BASE", f"http://127.0.0.1:{server.server_port}"), \
                mock.patch.object(self.proof, "AUTH_TOKEN", token):
            worker.start()
            try:
                yield server, token, package
            finally:
                server.shutdown()
                server.server_close()
                worker.join(timeout=2)

    def test_full_gateway_owner_accepts_expected_401_and_continues_through_real_event_stream(self):
        with self.actual_gateway() as (_, token, package):
            retained = {}
            with mock.patch.object(self.proof, "json_http",
                    side_effect=RuntimeError("controlled boundary after actual authority proof")), \
                    mock.patch.object(self.proof, "AUTH_SELECTION", {"mode": "managed-operator"}):
                with self.assertRaisesRegex(RuntimeError, "controlled boundary after actual"):
                    self.proof.execute_gateway_proof(package, include_source_ingestion=False,
                        retain=lambda phase, value: retained.update({phase: value}))
            self.assertEqual(list(retained), ["browser", "http_authority", "product_events"])
            self.assertEqual(retained["http_authority"]["anonymous_mcp_status"], 401)
            self.assertTrue(retained["product_events"]["complete_frame"])
            with self.assertRaisesRegex(RuntimeError, "HTTP 401"):
                self.proof.http("POST", "/mcp", {}, timeout=2, authorized=False)

    def test_real_handler_enforces_bearer_on_api_mcp_and_event_stream(self):
        import http.client
        with self.actual_gateway() as (server, token, package):
            for method, path, body in (("GET", "/health", None),
                    ("GET", "/api/v1/stream", None), ("POST", "/mcp", b"{}")):
                for header in ({}, {"Authorization": "Bearer wrong"}):
                    connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
                    connection.request(method, path, body=body, headers=header)
                    response = connection.getresponse()
                    self.assertEqual(response.status, 401)
                    response.read()
                    connection.close()
            connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
            connection.request("GET", "/health", headers={"Authorization": "Bearer " + token})
            response = connection.getresponse()
            self.assertEqual(response.status, 200)
            self.assertEqual(json.loads(response.read())["cognition"]["package_id"], package)
            connection.close()
            observed = self.proof.prove_product_event(package)
            self.assertEqual(observed["event"], "product-snapshot")
            self.assertTrue(observed["complete_frame"])
            self.assertEqual(observed["package_id"], package)
            self.assertNotIn(token, json.dumps(observed))
            with mock.patch.object(self.proof, "AUTH_TOKEN", "wrong"):
                with self.assertRaisesRegex(RuntimeError, "stream is unavailable"):
                    self.proof.prove_product_event(package)

if __name__ == "__main__":
    unittest.main()
