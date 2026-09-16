#!/usr/bin/env python3

from __future__ import annotations

import base64
import copy
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
from unittest import mock
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY / "tools/delivery/product_host.py"
SPEC = importlib.util.spec_from_file_location("laplace_product_host_tests", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load product host module")
host = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = host
SPEC.loader.exec_module(host)


class ProductHostTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract_path = REPOSITORY / "contracts/product-host.json"
        self.contract = host.load_json(self.contract_path)

    def key(self, root: Path) -> Path:
        path = root / "activation.key"
        path.write_text(
            base64.b64encode(bytes(range(32))).decode("ascii") + "\n",
            encoding="ascii",
        )
        path.chmod(0o600)
        return path

    def test_contract_binds_identity_gateway_cluster_and_lifecycle(self) -> None:
        host.validate_contract(self.contract, REPOSITORY)
        self.assertFalse(self.contract["lifecycle"]["manual_hotfixes"])
        self.assertEqual(
            self.contract["lifecycle"]["instance_state_owner"],
            "cluster activation module",
        )
        self.assertEqual(
            self.contract["modules"]["host_controller"],
            "tools/postgresql/hostctl.py",
        )

    def test_host_convergence_leaves_candidate_state_for_activation_but_owns_receipts(self) -> None:
        cluster = host.load_json(REPOSITORY / self.contract["modules"]["cluster_contract"])
        instance = cluster["instance"]
        declared = {item["path"]: item for item in self.contract["directories"]}
        cluster_owned = {
            instance["data_directory"],
            instance["wal_directory"],
            instance["temp_directory"],
            instance["perfcache_directory"],
            instance["config_directory"],
            instance["log_directory"],
        }
        for path in cluster_owned:
            with self.subTest(path=path):
                self.assertNotIn(path, declared)

        # product_host owns only bounded parents in its legacy/customer path. The
        # top-level /pgtemp prerequisite belongs to scripts/setup-host.sh and is not
        # duplicated into this contract merely to satisfy the DEV/BAT host layout.
        expected_parents = {
            instance["data_directory"]: "/opt/laplace/pgdata/refactor",
            instance["perfcache_directory"]: "/opt/laplace/pgdata/refactor",
            instance["wal_directory"]: "/var/lib/pgwal",
            instance["config_directory"]: "/etc/laplace/instances",
            instance["log_directory"]: "/var/log/laplace/postgresql",
        }
        for leaf, parent in expected_parents.items():
            with self.subTest(leaf=leaf, parent=parent):
                self.assertIn(parent, declared)
        self.assertNotIn("/pgtemp", declared)

        self.assertIn(instance["receipt_directory"], declared)
        receipt = declared[instance["receipt_directory"]]
        self.assertEqual(receipt["owner"], "laplace-runner")
        self.assertEqual(receipt["group"], "laplace-runner")
        self.assertEqual(receipt["mode"], "2770")

    def test_fixture_convergence_is_persistent_exact_and_repairable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-product-host-") as temporary:
            root = Path(temporary)
            key = self.key(root)
            first = host.converge_host(
                REPOSITORY, self.contract_path, key, root, False
            )
            second = host.converge_host(
                REPOSITORY, self.contract_path, key, root, False
            )
            self.assertEqual(first["phase"], "host-ready")
            self.assertFalse(first["product_activated"])
            self.assertTrue(first["gateway"]["installed_new"])
            self.assertFalse(second["gateway"]["installed_new"])
            for item in self.contract["directories"]:
                target = root.joinpath(*Path(item["path"]).parts[1:])
                self.assertTrue(target.is_dir())
                self.assertFalse(target.is_symlink())
                self.assertEqual(
                    stat.S_IMODE(target.stat().st_mode), int(item["mode"], 8)
                )

            cluster = host.load_json(
                REPOSITORY / self.contract["modules"]["cluster_contract"]
            )
            instance = cluster["instance"]
            for path in (
                instance["data_directory"],
                instance["wal_directory"],
                instance["temp_directory"],
                instance["perfcache_directory"],
                instance["config_directory"],
                instance["log_directory"],
            ):
                target = root.joinpath(*Path(path).parts[1:])
                self.assertFalse(
                    target.exists() or target.is_symlink(),
                    f"host bootstrap stole cluster-owned candidate state: {path}",
                )
            receipt_root = root.joinpath(*Path(instance["receipt_directory"]).parts[1:])
            self.assertTrue(receipt_root.is_dir())
            self.assertFalse(receipt_root.is_symlink())

    def test_fixture_repair_restores_mode_without_touching_contents(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="laplace-product-host-repair-"
        ) as temporary:
            root = Path(temporary)
            key = self.key(root)
            host.converge_host(REPOSITORY, self.contract_path, key, root, False)
            build = root / "build/laplace/runner"
            evidence = build / "retained-receipt.json"
            evidence.write_text("retained\n", encoding="utf-8")
            build.chmod(0o777)
            repaired = host.converge_host(
                REPOSITORY, self.contract_path, key, root, False
            )
            self.assertEqual(stat.S_IMODE(build.stat().st_mode), 0o2770)
            self.assertEqual(evidence.read_text(encoding="utf-8"), "retained\n")
            observed = next(
                item
                for item in repaired["directories"]
                if item["path"] == "/build/laplace/runner"
            )
            self.assertTrue(observed["changed"])

    def test_shared_parent_repair_preserves_creator_and_group_write(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-host-group-") as temporary:
            root = Path(temporary)
            target = root / "shared"
            target.mkdir(mode=0o700)
            creator = target.stat().st_uid
            with mock.patch.object(host.os, "chown") as chown:
                host.ensure_directory(root, Path("/shared"), 0o2770,
                                      creator + 1, target.stat().st_gid, True)
            chown.assert_called_once_with(target, creator, target.stat().st_gid)
            self.assertEqual(target.stat().st_uid, creator)
            self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o2770)
            child = target / "child"
            child.mkdir()
            self.assertEqual(child.stat().st_gid, target.stat().st_gid)
            self.assertTrue(child.stat().st_mode & stat.S_ISGID)

    def test_symlinked_host_boundary_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="laplace-product-host-link-"
        ) as temporary:
            root = Path(temporary)
            (root / "build").symlink_to(root / "outside")
            with self.assertRaisesRegex(host.HostError, "physical directory"):
                host.converge_host(
                    REPOSITORY, self.contract_path, self.key(root), root, False
                )

    def test_system_root_requires_explicit_root_authority(self) -> None:
        with self.assertRaisesRegex(host.HostError, "requires root"):
            host.converge_host(
                REPOSITORY, self.contract_path, Path("/missing"), Path("/"), False
            )

    def test_first_install_key_generation_is_exact_and_replayable(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="laplace-product-host-key-"
        ) as temporary:
            key = Path(temporary) / "etc/laplace/product-activation.key"
            self.assertTrue(host.ensure_activation_key(key, True))
            first = key.read_bytes()
            self.assertEqual(stat.S_IMODE(key.stat().st_mode), 0o400)
            self.assertFalse(host.ensure_activation_key(key, True))
            self.assertEqual(key.read_bytes(), first)
            key.chmod(0o600)
            key.write_text("invalid\n", encoding="ascii")
            with self.assertRaises(host.gateway.activation.ActivationGatewayError):
                host.ensure_activation_key(key, False)

    def test_contract_mutants_cannot_redirect_host_authority(self) -> None:
        for label, mutate in (
            (
                "identity",
                lambda value: value["service_identity"].update(user="root"),
            ),
            (
                "gateway",
                lambda value: value["modules"].update(
                    gateway_executable="/tmp/gateway"
                ),
            ),
            (
                "hotfix",
                lambda value: value["lifecycle"].update(manual_hotfixes=True),
            ),
        ):
            with self.subTest(label=label):
                mutant = copy.deepcopy(self.contract)
                mutate(mutant)
                with self.assertRaises(host.HostError):
                    host.validate_contract(mutant, REPOSITORY)

    def test_one_command_entrypoint_resolves_exact_accepted_publication(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-accepted-product-") as temporary:
            root = Path(temporary)
            publication = root / "publication.json"
            publication.write_bytes(
                host.gateway.activation.canonical_bytes(
                    {
                        "schema": "laplace.postgresql-package-publication-receipt/v1",
                        "publication_complete": True,
                    }
                )
            )
            state = root / "state.json"
            state.write_bytes(
                host.gateway.activation.canonical_bytes(
                    {
                        "schema": "laplace.product-publication-selection/v1",
                        "classification": "authority-selected-development-publication",
                        "postgresql_publication": {
                            "receipt": str(publication),
                            "receipt_sha256": host.gateway.activation.sha256_file(
                                publication
                            ),
                        },
                    }
                )
            )
            self.assertEqual(host.resolve_accepted_publication(state), publication)
            publication.write_bytes(publication.read_bytes() + b"changed\n")
            with self.assertRaisesRegex(host.HostError, "bytes differ"):
                host.resolve_accepted_publication(state)

    def test_repository_entrypoint_is_one_command_without_internal_paths(self) -> None:
        entrypoint = REPOSITORY / "install"
        self.assertTrue(entrypoint.is_file())
        source = entrypoint.read_text(encoding="utf-8")
        self.assertIn("product_host.py\" install", source)
        self.assertIn("--accepted-state", source)
        self.assertIn("state/product-publication-selection.json", source)
        self.assertNotIn("/opt/laplace/receipts/postgresql/", source)

    def test_setup_host_defaults_to_product_activation_with_explicit_prerequisites_mode(self) -> None:
        entrypoint = REPOSITORY / "scripts/setup-host.sh"
        unit = REPOSITORY / "packaging/systemd/laplace-refactor-postgresql.service"
        self.assertTrue(entrypoint.is_file())
        self.assertTrue(unit.is_file())
        source = entrypoint.read_text(encoding="utf-8")
        self.assertIn('MODE="${1:-setup}"', source)
        self.assertIn('"$MODE" == prerequisites', source)
        self.assertIn('"$SCRIPT_DIR/setup-product.sh"', source)
        self.assertIn('"$REPOSITORY/tools/delivery/product_cognition_service.py" ensure', source)
        self.assertIn('"$REPOSITORY/tools/delivery/product_cognition_service.py" restart', source)
        self.assertNotIn('"$SYSTEMCTL_BIN" restart "$COGNITION_SERVICE"', source)
        self.assertNotIn('stopped here by design', source)
        service = unit.read_text(encoding="utf-8")

        # The default completes product setup; prerequisites-only is explicit.
        self.assertIn("laplace-runner", source)
        self.assertIn("resolve_command", source)
        self.assertIn('SERVICE_SOURCE="$REPOSITORY/packaging/systemd/$SERVICE"', source)
        self.assertIn('SERVICE_TARGET="/etc/systemd/system/$SERVICE"', source)
        self.assertIn('"$SYSTEMCTL_BIN" daemon-reload', source)
        self.assertIn('"$SYSTEMCTL_BIN" enable "$SERVICE"', source)
        self.assertIn('"$SYSTEMCTL_BIN" is-enabled --quiet "$SERVICE"', source)
        self.assertIn('"started_by_bootstrap": false', source)
        self.assertIn("/opt/laplace/runtime", source)
        self.assertIn("/pgtemp", source)
        self.assertIn("/etc/sudoers.d/laplace-refactor-postgresql-service", source)
        for action in ("start", "stop", "restart"):
            self.assertIn(
                f"$RUNNER_USER ALL=(root) NOPASSWD: $SYSTEMCTL_BIN {action} $SERVICE",
                source,
            )
        self.assertIn('"product_activated": false', source)
        self.assertIn('"postgresql_initialized": false', source)
        self.assertIn('"activation_gateway_installed": false', source)
        self.assertNotIn("runuser", source)

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

        # Bootstrap must never become package/database/semantic execution again.
        for forbidden in (
            "product_host.py",
            "--generate-key",
            "laplace-product-activate",
            "execute-request",
            "LAPLACE_ACTIVATION_HMAC_KEY_B64",
            "build-package.py",
            "clusterctl.py activate-product",
            "unicodectl.py",
            "highwayctl.py",
            "--accepted-state",
            " initdb",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def static_service_fixture(self, root: Path) -> tuple[list[str], dict[str, str]]:
        """Run only the real static-envelope function, with protocol-only OS peers."""
        source = (REPOSITORY / "scripts/setup-host.sh").read_text(encoding="utf-8")
        start = source.index("install_static_service_envelopes() {\n")
        end = source.index("\n}\n", start) + len("\n}\n")
        command = [
            "bash", "-c",
            "set -euo pipefail\n" + source[start:end] + "\ninstall_static_service_envelopes\n",
        ]
        source_root = root / "unit sources"
        target_root = root / "unit targets"
        source_root.mkdir()
        target_root.mkdir()
        names = (
            "laplace-refactor-postgresql.service",
            "laplace-refactor-cognition.service",
        )
        for name in names:
            (source_root / name).write_bytes(
                (REPOSITORY / "packaging/systemd" / name).read_bytes()
            )
        # These peers check the install/systemctl protocol and copy real unit bytes
        # only inside the fixture. They do not claim root ownership or a real manager.
        peer = """\
import json
import os
from pathlib import Path
import sys

arguments = sys.argv[1:]
name = Path(sys.argv[0]).name
with Path(os.environ["SERVICE_TEST_LOG"]).open("a", encoding="utf-8") as stream:
    stream.write(json.dumps([name, *arguments]) + "\\n")
if name == "install":
    if len(arguments) != 8 or arguments[:6] != ["-o", "root", "-g", "root", "-m", "0644"]:
        raise SystemExit("unexpected static envelope ownership/mode")
    source, target = map(Path, arguments[6:])
    target.write_bytes(source.read_bytes())
    target.chmod(0o644)
elif name == "systemctl":
    state_path = Path(os.environ["SERVICE_TEST_ENABLED"])
    enabled = set(json.loads(state_path.read_text()) if state_path.exists() else [])
    allowed = {os.environ["SERVICE"], os.environ["COGNITION_SERVICE"]}
    if arguments == ["daemon-reload"]:
        pass
    elif len(arguments) == 2 and arguments[0] == "enable" and arguments[1] in allowed:
        target = Path(os.environ["SERVICE_TEST_TARGETS"]) / arguments[1]
        if not target.is_file():
            raise SystemExit("cannot enable an absent unit")
        enabled.add(arguments[1])
        state_path.write_text(json.dumps(sorted(enabled)), encoding="utf-8")
    elif len(arguments) == 3 and arguments[:2] == ["is-enabled", "--quiet"]:
        if arguments[2] not in enabled:
            raise SystemExit("unit is not enabled")
    else:
        raise SystemExit("unexpected lifecycle action")
else:
    raise SystemExit("unexpected protocol peer")
"""
        environment = os.environ.copy()
        for name in ("install", "systemctl"):
            path = root / name
            path.write_text("#!" + sys.executable + "\n" + peer, encoding="utf-8")
            path.chmod(0o755)
            environment[name.upper() + "_BIN"] = str(path)
        environment.update({
            "SERVICE": names[0],
            "SERVICE_SOURCE": str(source_root / names[0]),
            "SERVICE_TARGET": str(target_root / names[0]),
            "COGNITION_SERVICE": names[1],
            "COGNITION_SERVICE_SOURCE": str(source_root / names[1]),
            "COGNITION_SERVICE_TARGET": str(target_root / names[1]),
            "SERVICE_TEST_LOG": str(root / "calls.jsonl"),
            "SERVICE_TEST_ENABLED": str(root / "enabled.json"),
            "SERVICE_TEST_TARGETS": str(target_root),
        })
        return command, environment

    def test_prerequisites_install_both_exact_envelopes_without_starting_them(self) -> None:
        source = (REPOSITORY / "scripts/setup-host.sh").read_text(encoding="utf-8")
        call = source.index("\ninstall_static_service_envelopes\n")
        self.assertGreater(call, source.index('if [[ "$MODE" == storage ]]; then'))
        self.assertLess(call, source.index('if [[ "$MODE" == prerequisites ]]; then'))
        self.assertLess(
            source.index('if [[ "$MODE" == prerequisites ]]; then'),
            source.index('"$SCRIPT_DIR/setup-product.sh"'),
        )
        self.assertGreater(
            source.index('"$REPOSITORY/tools/delivery/product_cognition_service.py" restart'),
            source.index('"$SCRIPT_DIR/setup-product.sh"'),
        )
        self.assertLess(
            source.index('"$REPOSITORY/tools/delivery/product_cognition_service.py" ensure'),
            source.index('"$REPOSITORY/tools/delivery/product_cognition_service.py" restart'),
        )
        self.assertIn('"$SUDO_BIN" -u "$RUNNER_USER" -H -- "$PYTHON_BIN"', source)
        self.assertIn('"cognition_service_envelope": {', source)
        self.assertIn('"sha256": "$COGNITION_SERVICE_SHA"', source)
        with tempfile.TemporaryDirectory(prefix="laplace-static-services-") as temporary:
            root = Path(temporary)
            command, environment = self.static_service_fixture(root)
            for _ in range(2):
                result = subprocess.run(
                    command, env=environment, text=True, capture_output=True, timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                for prefix in ("SERVICE", "COGNITION_SERVICE"):
                    expected = Path(environment[prefix + "_SOURCE"]).read_bytes()
                    target = Path(environment[prefix + "_TARGET"])
                    self.assertEqual(target.read_bytes(), expected)
                    self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o644)
            calls = [
                json.loads(line)
                for line in Path(environment["SERVICE_TEST_LOG"]).read_text().splitlines()
            ]
            one_pass = [
                ["install", "-o", "root", "-g", "root", "-m", "0644",
                 environment["SERVICE_SOURCE"], environment["SERVICE_TARGET"]],
                ["install", "-o", "root", "-g", "root", "-m", "0644",
                 environment["COGNITION_SERVICE_SOURCE"], environment["COGNITION_SERVICE_TARGET"]],
                ["systemctl", "daemon-reload"],
                ["systemctl", "enable", environment["SERVICE"]],
                ["systemctl", "enable", environment["COGNITION_SERVICE"]],
                ["systemctl", "is-enabled", "--quiet", environment["SERVICE"]],
                ["systemctl", "is-enabled", "--quiet", environment["COGNITION_SERVICE"]],
            ]
            self.assertEqual(calls, one_pass * 2)
            self.assertEqual(
                json.loads(Path(environment["SERVICE_TEST_ENABLED"]).read_text()),
                sorted((environment["SERVICE"], environment["COGNITION_SERVICE"])),
            )

    def test_unsafe_static_unit_pair_refuses_before_install_or_manager_calls(self) -> None:
        for prefix in ("SERVICE", "COGNITION_SERVICE"):
            for kind in ("missing", "symlink"):
                with self.subTest(unit=prefix, kind=kind):
                    with tempfile.TemporaryDirectory(prefix="laplace-static-refusal-") as temporary:
                        root = Path(temporary)
                        command, environment = self.static_service_fixture(root)
                        source = Path(environment[prefix + "_SOURCE"])
                        content = source.read_bytes()
                        source.unlink()
                        if kind == "symlink":
                            outside = root / "outside.service"
                            outside.write_bytes(content)
                            source.symlink_to(outside)
                        result = subprocess.run(
                            command, env=environment, text=True, capture_output=True, timeout=10,
                        )
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("static service definition is absent or unsafe", result.stderr)
                        self.assertFalse(Path(environment["SERVICE_TEST_LOG"]).exists())
                        self.assertFalse(Path(environment["SERVICE_TEST_ENABLED"]).exists())
                        self.assertEqual(list(Path(environment["SERVICE_TEST_TARGETS"]).iterdir()), [])


OWNER_SPEC = importlib.util.spec_from_file_location(
    "laplace_cognition_service_owner_tests",
    REPOSITORY / "tools/delivery/product_cognition_service.py",
)
if OWNER_SPEC is None or OWNER_SPEC.loader is None:
    raise RuntimeError("cannot load cognition service owner")
cognition_owner = importlib.util.module_from_spec(OWNER_SPEC)
sys.modules[OWNER_SPEC.name] = cognition_owner
OWNER_SPEC.loader.exec_module(cognition_owner)


class CognitionOwnerTests(unittest.TestCase):
    def setUp(self) -> None:
        import socket
        self.temporary = tempfile.TemporaryDirectory(prefix="laplace-cognition-owner-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.home = self.root / "home"
        self.home.mkdir()
        self.runtime = self.root / "runtime"
        self.runtime.mkdir(mode=0o700)
        self.bus = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.bus.bind(str(self.runtime / "bus"))
        self.addCleanup(self.bus.close)
        self.marker = self.root / "receipts/cognition-user-service.json"
        self.marker.parent.mkdir()
        self.repository = self.root / "repository"
        for scope in ("system", "user"):
            relative = "packaging/systemd/" + ("user/" if scope == "user" else "") + cognition_owner.UNIT
            target = self.repository / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((REPOSITORY / relative).read_bytes())
        self.enabled = False
        self.system_enabled = True
        self.active_user = False
        self.active_system = False
        self.fail_enable = False
        self.dropins = ""
        self.calls = []
        self.loaded_user_bytes = None
        self.service = cognition_owner.Owner(
            uid=os.geteuid(), home=self.home, repository=self.repository,
            marker=self.marker, runtime=self.runtime,
            system_unit=self.root / "system.service", execute=self.execute,
        )
        self.real_file_bytes = cognition_owner.file_bytes
        # A non-root hosted test cannot chown a fixture to root. All actual file,
        # byte, symlink and mode checks remain in use; only this fixture's system
        # file expected uid is the executing test uid.
        def fixture_file_bytes(path, uid=None):
            return self.real_file_bytes(
                path, os.geteuid() if path == self.service.system_unit and uid == 0 else uid)
        self.patch = mock.patch.object(cognition_owner, "file_bytes", fixture_file_bytes)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def execute(self, arguments, environment, timeout):
        self.calls.append((list(arguments), dict(environment), timeout))
        self.assertEqual(environment["HOME"], str(self.home))
        self.assertEqual(environment["XDG_RUNTIME_DIR"], str(self.runtime))
        self.assertEqual(environment["DBUS_SESSION_BUS_ADDRESS"], "unix:path=" + str(self.runtime / "bus"))
        if arguments[:2] == ["/usr/bin/loginctl", "enable-linger"]:
            self.assertEqual(arguments[2:], [cognition_owner.USER])
            return subprocess.CompletedProcess(arguments, 0, "", "")
        if arguments[:2] == ["/usr/bin/loginctl", "show-user"]:
            return subprocess.CompletedProcess(arguments, 0, "yes\n", "")
        if arguments[:2] == ["/usr/bin/sudo", "-n"]:
            self.assertEqual(arguments[2:], ["/usr/bin/systemctl", "restart", cognition_owner.UNIT])
            self.active_system = True
            return subprocess.CompletedProcess(arguments, 0, "", "")
        self.assertEqual(arguments[0], "/usr/bin/systemctl")
        user = arguments[1] == "--user"
        operation = arguments[2:] if user else arguments[1:]
        if operation[0] == "show":
            path = self.service.user_unit if user else self.service.system_unit
            present = path.exists()
            active = self.active_user if user else self.active_system
            values = {
                "LoadState": "loaded" if present else "not-found",
                "ActiveState": "active" if active else "inactive",
                "SubState": "running" if active else "dead",
                "UnitFileState": "enabled" if (self.enabled if user else self.system_enabled) else "disabled",
                "FragmentPath": str(path) if present else "",
                "NeedDaemonReload": "yes" if user and present and path.read_bytes() != self.loaded_user_bytes else "no",
                "DropInPaths": self.dropins,
                "ControlGroup": "/fixture.service",
                "User": "" if user else cognition_owner.USER,
                "MainPID": "2468" if active else "0",
            }
            return subprocess.CompletedProcess(arguments, 0, "".join(k + "=" + v + "\n" for k, v in values.items()), "")
        self.assertTrue(user)
        if operation == ["daemon-reload"]:
            self.loaded_user_bytes = self.service.user_unit.read_bytes()
        elif operation == ["enable", cognition_owner.UNIT]:
            if self.fail_enable:
                return subprocess.CompletedProcess(arguments, 1, "", "deliberate enablement failure")
            self.enabled = True
        elif operation == ["restart", cognition_owner.UNIT]:
            self.active_user = True
        else:
            self.fail("unexpected manager operation: " + repr(operation))
        return subprocess.CompletedProcess(arguments, 0, "", "")

    def install_system(self):
        self.service.system_unit.write_bytes(self.service.desired("system"))

    def test_user_owner_is_enabled_before_atomic_selection_and_replays_without_start(self):
        first = self.service.ensure()
        raw = self.marker.read_bytes()
        second = self.service.ensure()
        self.assertEqual(first["scope"], "user")
        self.assertEqual(second["scope"], "user")
        self.assertEqual(self.marker.read_bytes(), raw)
        self.assertEqual(self.service.user_unit.read_bytes(), self.service.desired("user"))
        self.assertTrue(self.enabled)
        self.assertFalse(self.active_user)
        self.assertFalse(json.loads(raw)["cold_boot_proven"])
        self.assertFalse(any("restart" in call[0] or "--now" in call[0] for call in self.calls))
        # A manager which reports enablement failure cannot publish selection.
        self.marker.unlink()
        self.enabled = False
        self.fail_enable = True
        with self.assertRaisesRegex(cognition_owner.ServiceError, "command failed"):
            self.service.ensure()
        self.assertFalse(self.marker.exists())

    def test_selected_scope_survives_later_system_envelope_installation(self):
        self.service.ensure()
        self.install_system()
        self.assertEqual(self.service.verify()["scope"], "user")
        self.active_system = True
        with self.assertRaisesRegex(cognition_owner.ServiceError, "already running"):
            self.service.verify()

    def test_existing_verified_system_owner_is_selected_without_creating_user_marker(self):
        self.install_system()
        result = self.service.ensure()
        self.assertEqual(result["scope"], "system")
        self.assertFalse(self.marker.exists())
        self.assertFalse(self.service.user_unit.exists())
        self.assertFalse(any(call[0][0] == "/usr/bin/loginctl" for call in self.calls))
        self.service.system_unit.write_bytes(self.service.system_unit.read_bytes().replace(
            b"ConditionPathExists=!", b"# ConditionPathExists=!"))
        with self.assertRaisesRegex(cognition_owner.ServiceError, "exact mutually exclusive"):
            self.service.verify()

    def test_recorded_unit_update_preserves_prior_owner_on_manager_failure(self):
        self.service.ensure()
        previous_unit = self.service.user_unit.read_bytes()
        previous_marker = self.marker.read_bytes()
        source = self.repository / "packaging/systemd/user" / cognition_owner.UNIT
        source.write_bytes(source.read_bytes() + b"# revised envelope\n")
        self.fail_enable = True
        with self.assertRaisesRegex(cognition_owner.ServiceError, "command failed"):
            self.service.ensure()
        self.assertEqual(self.service.user_unit.read_bytes(), previous_unit)
        self.assertEqual(self.marker.read_bytes(), previous_marker)
        self.fail_enable = False
        self.service.ensure()
        self.assertEqual(self.service.user_unit.read_bytes(), source.read_bytes())
        self.assertNotEqual(self.marker.read_bytes(), previous_marker)

    def test_changed_receipt_or_changed_selected_unit_is_rejected_before_manager_mutation(self):
        self.service.ensure()
        original = self.marker.read_bytes()
        value = json.loads(original)
        value["uid"] += 1
        self.marker.write_text(json.dumps(value), encoding="utf-8")
        before = len(self.calls)
        with self.assertRaisesRegex(cognition_owner.ServiceError, "another owner"):
            self.service.ensure()
        self.assertEqual(len(self.calls), before)
        self.marker.write_bytes(original)
        self.service.user_unit.write_bytes(self.service.user_unit.read_bytes() + b"# drift\n")
        with self.assertRaisesRegex(cognition_owner.ServiceError, "retained receipt"):
            self.service.ensure()
        self.assertEqual(len(self.calls), before)

    def test_unrecorded_unit_and_linked_destination_are_not_overwritten(self):
        self.service.user_unit.parent.mkdir(parents=True)
        self.service.user_unit.write_text("unrelated unit\n", encoding="utf-8")
        with self.assertRaisesRegex(cognition_owner.ServiceError, "unrecorded user unit"):
            self.service.ensure()
        self.assertEqual(self.service.user_unit.read_text(), "unrelated unit\n")
        self.service.user_unit.unlink()
        outside = self.root / "outside"
        outside.write_text("retained", encoding="utf-8")
        self.service.user_unit.symlink_to(outside)
        with self.assertRaisesRegex(cognition_owner.ServiceError, "unsafe service file"):
            self.service.ensure()
        self.assertEqual(outside.read_text(), "retained")
        self.assertFalse(self.marker.exists())

    def test_linger_bus_and_runtime_ownership_are_actual_persistence_prerequisites(self):
        self.runtime.chmod(0o755)
        with self.assertRaisesRegex(cognition_owner.ServiceError, "0700"):
            self.service.ensure()
        self.runtime.chmod(0o700)
        self.bus.close()
        (self.runtime / "bus").unlink()
        (self.runtime / "bus").write_text("not a bus", encoding="utf-8")
        with self.assertRaisesRegex(cognition_owner.ServiceError, "owned socket"):
            self.service.ensure()
        self.assertFalse(self.marker.exists())

    def test_restart_uses_selected_scope_and_rejects_activation_drift(self):
        self.service.ensure()
        output = self.root / "evidence"
        output.mkdir()
        snapshots = [{"package_id": "a" * 64}, {"package_id": "a" * 64}]
        observations = []
        def observe(commit, destination):
            self.assertEqual(commit, "b" * 40)
            observations.append(destination.name)
            return snapshots[len(observations) - 1]
        def healthy(pid, uid, active):
            self.assertEqual((pid, uid, active["package_id"]), (2468, os.geteuid(), "a" * 64))
            return {"peer_verified": True}
        result = self.service.restart("b" * 40, output, observe_activation=observe,
                                      stable_activation=lambda value: value, health=healthy)
        self.assertTrue(result["activated_package_verified"])
        self.assertEqual(observations, ["activation-before.json", "activation-after.json"])
        self.assertIn(["/usr/bin/systemctl", "--user", "restart", cognition_owner.UNIT],
                      [call[0] for call in self.calls])
        self.assertFalse(any(call[0][0] == "/usr/bin/sudo" for call in self.calls))
        observations.clear()
        snapshots[1] = {"package_id": "c" * 64}
        with self.assertRaisesRegex(cognition_owner.ServiceError, "changed during cognition restart"):
            self.service.restart("b" * 40, output, observe_activation=observe,
                                 stable_activation=lambda value: value, health=healthy)

    def test_system_restart_uses_only_the_fixed_existing_privileged_command(self):
        self.install_system()
        output = self.root / "system-evidence"
        output.mkdir()
        result = self.service.restart(
            "b" * 40, output, observe_activation=lambda *_: {"package_id": "a" * 64},
            stable_activation=lambda value: value, health=lambda *_: {"peer_verified": True})
        self.assertEqual(result["scope"], "system")
        self.assertIn(["/usr/bin/sudo", "-n", "/usr/bin/systemctl", "restart", cognition_owner.UNIT],
                      [call[0] for call in self.calls])

    def test_inherited_manager_redirects_do_not_select_another_account(self):
        with mock.patch.dict(os.environ, {
            "HOME": "/wrong", "XDG_CONFIG_HOME": "/wrong", "XDG_RUNTIME_DIR": "/wrong",
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=/wrong",
            "SYSTEMD_UNIT_PATH": "/wrong", "SYSTEMD_BUS_ADDRESS": "unix:path=/wrong",
        }):
            result = self.service.ensure()
        self.assertEqual(result["scope"], "user")
        self.assertTrue(all("SYSTEMD_UNIT_PATH" not in call[1] and
                            "SYSTEMD_BUS_ADDRESS" not in call[1] for call in self.calls))


    def test_effective_unit_dropin_is_not_accepted_as_the_fixed_envelope(self):
        self.dropins = "/unrecorded/override.conf"
        with self.assertRaisesRegex(cognition_owner.ServiceError, "enablement did not persist"):
            self.service.ensure()
        self.assertFalse(self.marker.exists())

    def health_fixture(self, *, related, trickle=False):
        import time
        release = self.root / "package"
        binary = release / "bin"
        binary.mkdir(parents=True)
        core = binary / "laplace-cognition-core-service"
        wrapper = binary / "laplace-cognition-service"
        core.write_text(
            "#!" + sys.executable + "\n"
            "import json, socket, sys, time\n"
            "s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
            "s.bind(sys.argv[1]); s.listen(4)\n"
            "body = json.dumps(dict(status='ready', package_id='a'*64, "
            "unicode_present=True, highway_present=True)).encode()\n"
            "while True:\n"
            "    c, _ = s.accept()\n"
            "    try:\n"
            "        c.recv(8192)\n"
            "        wire = (b'HTTP/1.1 200 OK\\r\\nContent-Length: ' + "
            "str(len(body)).encode() + b'\\r\\nConnection: close\\r\\n\\r\\n' + body)\n"
            "        if " + repr(trickle) + ":\n"
            "            for byte in wire:\n"
            "                c.sendall(bytes([byte])); time.sleep(0.03)\n"
            "        else: c.sendall(wire)\n"
            "    except OSError: pass\n"
            "    finally: c.close()\n",
            encoding="utf-8",
        )
        wrapper.write_text(
            "#!" + sys.executable + "\n"
            "import signal, subprocess, sys, time\n"
            "child = None\n"
            "def stop(*unused): raise SystemExit(0)\n"
            "signal.signal(signal.SIGTERM, stop)\n"
            "try:\n"
            "    if sys.argv[3] == 'related': child = subprocess.Popen([sys.executable, sys.argv[1], sys.argv[2]])\n"
            "    while True: time.sleep(0.1)\n"
            "finally:\n"
            "    if child is not None:\n"
            "        child.terminate()\n"
            "        child.wait(timeout=5)\n",
            encoding="utf-8",
        )
        socket_path = self.root / "health.sock"
        process = subprocess.Popen(
            [sys.executable, str(wrapper), str(core), str(socket_path),
             "related" if related else "unrelated"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        children = [process]
        if not related:
            children.append(subprocess.Popen(
                [sys.executable, str(core), str(socket_path)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        def cleanup():
            for child in children:
                if child.poll() is None:
                    child.terminate()
            for child in children:
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)
        self.addCleanup(cleanup)
        deadline = time.monotonic() + 5
        while not socket_path.exists():
            self.assertIsNone(process.poll(), "wrapper exited before fixture readiness")
            self.assertLess(time.monotonic(), deadline, "health fixture did not start")
            time.sleep(0.01)
        return process, socket_path, {"release": str(release), "package_id": "a" * 64}

    def test_actual_core_child_peer_is_accepted_and_stale_package_is_rejected(self):
        process, path, activation = self.health_fixture(related=True)
        result = cognition_owner.observe_health(
            process.pid, os.geteuid(), activation, socket_path=path)
        self.assertEqual(result["wrapper"]["pid"], process.pid)
        self.assertNotEqual(result["core"]["pid"], process.pid)
        self.assertEqual(result["core"]["parent_pid"], process.pid)
        self.assertEqual(result["socket_peer_pid"], result["core"]["pid"])
        self.assertEqual(result["core"]["cgroup"], result["wrapper"]["cgroup"])
        self.assertGreaterEqual(result["core"]["start_ticks"], result["wrapper"]["start_ticks"])
        with self.assertRaisesRegex(cognition_owner.ServiceError, "activated native floor"):
            cognition_owner.observe_health(
                process.pid, os.geteuid(), {**activation, "package_id": "b" * 64},
                socket_path=path)

    def test_same_uid_and_core_bytes_do_not_admit_an_unrelated_socket_peer(self):
        process, path, activation = self.health_fixture(related=False)
        with self.assertRaisesRegex(cognition_owner.ServiceError, "current core child"):
            cognition_owner.observe_health(
                process.pid, os.geteuid(), activation, socket_path=path)

    def test_actual_trickling_core_response_cannot_extend_absolute_health_deadline(self):
        import time
        process, path, activation = self.health_fixture(related=True, trickle=True)
        started = time.monotonic()
        with self.assertRaisesRegex(cognition_owner.ServiceError, "health deadline expired"):
            cognition_owner.observe_health(
                process.pid, os.geteuid(), activation, socket_path=path,
                deadline=started + 0.2)
        self.assertLess(time.monotonic() - started, 1.5)


    def test_expected_script_named_only_as_an_argument_is_not_process_identity(self):
        script = self.root / "claimed-service"
        script.write_text("#!" + sys.executable + "\npass\n", encoding="utf-8")
        child = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(30)", str(script)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            with self.assertRaisesRegex(cognition_owner.ServiceError, "script position"):
                cognition_owner.process_identity(child.pid, os.geteuid(), script)
        finally:
            child.terminate()
            child.wait(timeout=5)



if __name__ == "__main__":
    unittest.main(verbosity=2)
