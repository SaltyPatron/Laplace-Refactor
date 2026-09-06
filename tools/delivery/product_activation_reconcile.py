#!/usr/bin/env python3
"""Run the product activation orchestrator with active-generation reconciliation."""

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

fresh_activate_product = runner.clusterctl.activate_product


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


runner.clusterctl.activate_product = reconcile_cluster_activation


def main(argv: list[str] | None = None) -> int:
    return runner.main(sys.argv[1:] if argv is None else argv)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"product_activation_reconcile: {error}", file=sys.stderr)
        raise SystemExit(1) from error
