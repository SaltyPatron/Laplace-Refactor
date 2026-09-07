#!/usr/bin/env python3
"""Complete activation of the selected installed product without rebuilding it."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys


REPOSITORY = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "laplace_installed_product_runner",
    REPOSITORY / "tools/delivery/product_activation_runner.py",
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load the product activation owner")
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
SPEC.loader.exec_module(runner)


def resume(repository_commit: str, output: Path) -> dict:
    runner.require_runner()
    contract = runner.load_json(REPOSITORY / "contracts/postgresql-cluster.json")
    runner.clusterctl.validate_contract(contract)
    active = Path(contract["package"]["active_link"])
    if not active.is_symlink():
        raise runner.RunnerActivationError("no installed product is selected")
    target = os.readlink(active)
    package_id = target.removeprefix("releases/")
    if (target != f"releases/{package_id}" or len(package_id) != 64
            or any(c not in "0123456789abcdef" for c in package_id)):
        raise runner.RunnerActivationError("selected product identity is invalid")
    receipt_root = Path(contract["instance"]["receipt_directory"])
    cluster_root = receipt_root / "cluster-activation" / package_id
    installation = runner.load_json(cluster_root / "package-installation.json")
    if (installation.get("package_id") != package_id
            or installation.get("phase") != "installed"
            or installation.get("installed_package_verified") is not True
            or installation.get("installation_receipt_sha256") !=
                runner.document_identity(installation, "installation_receipt_sha256")):
        raise runner.RunnerActivationError("installed package receipt does not verify")

    source_root = Path(installation["source_physical_root"])
    if (source_root.name != "root"
            or source_root.parent.parent != Path("/build/laplace/runner/product/stage")):
        raise runner.RunnerActivationError("installed package source location differs")
    plan_id = source_root.parent.name
    if len(plan_id) != 64 or any(c not in "0123456789abcdef" for c in plan_id):
        raise runner.RunnerActivationError("installed package build identity is invalid")
    product_receipt_path = (
        Path("/build/laplace/runner/product/build") / plan_id / "package-receipt.json"
    )
    product = runner.load_json(product_receipt_path)
    manifest_path = Path(product["manifest"])
    if (product.get("package_id") != package_id
            or product.get("manifest_sha256") != installation["package_manifest_sha256"]
            or runner.clusterctl.sha256_file(manifest_path) != product["manifest_sha256"]):
        raise runner.RunnerActivationError("installed package manifest differs from its receipt")
    return runner.execute(
        product_receipt_path,
        receipt_root / "plan" / package_id / "resource-observation.json",
        repository_commit,
        output,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-commit", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(resume(args.repository_commit, args.output), sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"product_resume: {error}", file=sys.stderr)
        raise SystemExit(1) from error
