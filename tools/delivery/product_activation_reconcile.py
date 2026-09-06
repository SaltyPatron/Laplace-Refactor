#!/usr/bin/env python3
"""Run product activation with release-capacity and active-generation reconciliation."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from typing import Any


REPOSITORY = Path(__file__).resolve().parents[2]


def _load(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


runner = _load(
    "laplace_reconciled_product_activation_runner",
    REPOSITORY / "tools/delivery/product_activation_runner.py",
)
upgrade = _load(
    "laplace_product_cluster_generation_upgrade",
    REPOSITORY / "tools/delivery/product_cluster_upgrade.py",
)
release_capacity = _load(
    "laplace_product_release_capacity",
    REPOSITORY / "tools/delivery/product_release_capacity.py",
)

fresh_activate_product = runner.clusterctl.activate_product
fresh_install_package = runner.clusterctl.install_package


def install_package_with_capacity(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    source_physical_root: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    """Prove release-volume headroom before the immutable copy begins."""

    physical_release = runner.clusterctl.prefixed(source_physical_root, manifest["root"])
    capacity_receipt = release_capacity.reconcile_capacity(
        contract,
        {
            "package_id": manifest["package_id"],
            "physical_root": str(physical_release),
        },
        manifest,
    )
    receipt_root = Path(contract["instance"]["receipt_directory"])
    runner.write_json(
        receipt_root
        / "release-capacity"
        / manifest["package_id"]
        / "capacity.json",
        capacity_receipt,
    )
    return fresh_install_package(
        manifest,
        contract,
        source_physical_root,
        root,
        authorize_system_root,
    )


def reconcile_cluster_activation(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    evidence_directory: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    contract = runner.clusterctl.load_json(contract_path)
    active = Path(contract["package"]["active_link"])
    if active.exists() or active.is_symlink():
        return upgrade.upgrade_product(
            contract_path,
            package_path,
            resource_path,
            evidence_directory,
            authorize_system_root,
        )
    return fresh_activate_product(
        contract_path,
        package_path,
        resource_path,
        evidence_directory,
        authorize_system_root,
    )


runner.clusterctl.install_package = install_package_with_capacity
runner.clusterctl.activate_product = reconcile_cluster_activation


def main(argv: list[str] | None = None) -> int:
    return runner.main(sys.argv[1:] if argv is None else argv)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"product_activation_reconcile: {error}", file=sys.stderr)
        raise SystemExit(1) from error
