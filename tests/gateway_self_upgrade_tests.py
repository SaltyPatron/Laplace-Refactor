#!/usr/bin/env python3

from __future__ import annotations

import copy
import datetime as dt
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[1]
NOW = dt.datetime(2026, 9, 4, 17, 0, 0, tzinfo=dt.timezone.utc)
KEY = bytes(range(32))


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


activation = load_module(
    "laplace_gateway_upgrade_test_activation",
    REPOSITORY / "tools/delivery/product_activation.py",
)
installer = load_module(
    "laplace_gateway_upgrade_test_installer",
    REPOSITORY / "tools/delivery/install_product_activation_gateway.py",
)
compiler = load_module(
    "laplace_gateway_upgrade_test_compiler",
    REPOSITORY / "tools/delivery/gateway_upgrade.py",
)
gateway = load_module(
    "laplace_gateway_upgrade_test_gateway",
    REPOSITORY / "tools/delivery/product_activation_gateway.py",
)


class GatewaySelfUpgradeTests(unittest.TestCase):
    def fixture(self, root: Path):
        source = root / "source"
        for relative, repository_relative in installer.SOURCE_MAP.items():
            target = source / repository_relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPOSITORY / repository_relative, target)

        contract = copy.deepcopy(
            activation.load_json(REPOSITORY / "contracts/product-activation-gateway.json")
        )
        deployment = root / "deployment"
        contract["gateway"]["release_root"] = str(deployment / "releases")
        contract["gateway"]["active_link"] = str(deployment / "current")
        contract["gateway"]["executable"] = str(
            deployment / "current/bin/laplace-product-activate"
        )
        contract["gateway"]["sudoers_path"] = str(root / "sudoers")
        contract["gateway"]["receipt_root"] = str(root / "receipts")
        contract["request"]["secret_path"] = str(root / "activation.key")
        contract_path = source / installer.SOURCE_MAP[
            "contracts/product-activation-gateway.json"
        ]
        contract_path.write_bytes(activation.canonical_bytes(contract))
        return source, contract

    def request(self, source: Path, contract: dict):
        return compiler.build_request(
            source,
            contract,
            installer.SOURCE_MAP,
            KEY,
            NOW,
            "github-actions",
            1234,
        )

    def fake_verify(self, executable: Path, require_root_ownership: bool = True):
        bundle = executable.resolve().parent.parent
        manifest = json.loads((bundle / "bundle-manifest.json").read_text(encoding="utf-8"))
        return bundle, manifest

    def test_signed_bundle_is_installed_and_atomically_selected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-self-upgrade-") as temporary:
            root = Path(temporary)
            source, contract = self.fixture(root)
            request = self.request(source, contract)
            payload = activation.canonical_bytes(request)
            with mock.patch.object(gateway.os, "chown", return_value=None), mock.patch.object(
                gateway.activation, "verify_installed_bundle", side_effect=self.fake_verify
            ):
                result = gateway.execute_gateway_upgrade(payload, contract, KEY)
                replay = gateway.execute_gateway_upgrade(payload, contract, KEY)

            self.assertEqual(result["schema"], gateway.UPGRADE_RESULT_SCHEMA)
            self.assertEqual(result["phase"], "gateway-upgraded")
            self.assertTrue(result["installed_new"])
            self.assertTrue(result["active_pointer_changed"])
            self.assertFalse(replay["installed_new"])
            self.assertFalse(replay["active_pointer_changed"])
            active = Path(contract["gateway"]["active_link"])
            self.assertTrue(active.is_symlink())
            self.assertEqual(active.resolve().name, result["bundle_id"])
            self.assertTrue(
                (active.resolve() / "bin/laplace-product-activate").is_file()
            )

    def test_bad_hmac_is_rejected_before_installation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-bad-mac-") as temporary:
            root = Path(temporary)
            source, contract = self.fixture(root)
            request = self.request(source, contract)
            request["mac"] = "0" * 64
            with self.assertRaisesRegex(
                activation.ActivationGatewayError, "HMAC authentication failed"
            ):
                gateway.execute_gateway_upgrade(
                    activation.canonical_bytes(request), contract, KEY
                )
            self.assertFalse(Path(contract["gateway"]["release_root"]).exists())

    def test_missing_or_reordered_trusted_file_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-roster-") as temporary:
            root = Path(temporary)
            source, contract = self.fixture(root)
            request = self.request(source, contract)
            request["files"] = request["files"][1:]
            payload = {key: value for key, value in request.items() if key != "mac"}
            request["mac"] = compiler.request_mac(payload, KEY)
            with self.assertRaisesRegex(
                activation.ActivationGatewayError, "file roster differs"
            ):
                gateway.execute_gateway_upgrade(
                    activation.canonical_bytes(request), contract, KEY
                )

    def test_security_boundary_move_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-boundary-") as temporary:
            root = Path(temporary)
            source, contract = self.fixture(root)
            changed = copy.deepcopy(contract)
            changed["gateway"]["active_link"] = str(root / "other/current")
            contract_file = source / installer.SOURCE_MAP[
                "contracts/product-activation-gateway.json"
            ]
            contract_file.write_bytes(activation.canonical_bytes(changed))
            request = compiler.build_request(
                source,
                changed,
                installer.SOURCE_MAP,
                KEY,
                NOW,
                "github-actions",
                1234,
            )
            with self.assertRaisesRegex(
                activation.ActivationGatewayError, "cannot move gateway.active_link"
            ):
                gateway.execute_gateway_upgrade(
                    activation.canonical_bytes(request), contract, KEY
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
