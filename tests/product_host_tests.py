#!/usr/bin/env python3

from __future__ import annotations

import base64
import copy
import importlib.util
from pathlib import Path
import stat
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
        self.assertIn(receipt["mode"], {"0750", "2750"})

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
            self.assertEqual(stat.S_IMODE(build.stat().st_mode), 0o2750)
            self.assertEqual(evidence.read_text(encoding="utf-8"), "retained\n")
            observed = next(
                item
                for item in repaired["directories"]
                if item["path"] == "/build/laplace/runner"
            )
            self.assertTrue(observed["changed"])

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

    def test_setup_host_is_prerequisites_only_then_hands_control_to_cicd(self) -> None:
        entrypoint = REPOSITORY / "scripts/setup-host.sh"
        self.assertTrue(entrypoint.is_file())
        source = entrypoint.read_text(encoding="utf-8")

        # Positive bootstrap boundary: establish identity, parent roots and exact
        # narrow service-control capability, then stop before product deployment.
        self.assertIn("laplace-runner", source)
        self.assertIn("resolve_command", source)
        self.assertIn("/pgtemp", source)
        self.assertIn("/etc/sudoers.d/laplace-refactor-postgresql-service", source)
        self.assertIn("$SYSTEMCTL_BIN start $SERVICE", source)
        self.assertIn("$SYSTEMCTL_BIN stop $SERVICE", source)
        self.assertIn("$SYSTEMCTL_BIN restart $SERVICE", source)
        self.assertIn('"product_activated": false', source)
        self.assertIn('"postgresql_initialized": false', source)
        self.assertIn('"activation_gateway_installed": false', source)
        self.assertNotIn("runuser", source)

        # Bootstrap must never become deployment/product execution again.
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
            "packaging/systemd/$SERVICE",
            "systemctl daemon-reload",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
