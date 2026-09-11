#!/usr/bin/env python3
"""Converge runner-owned package evidence after recurring product activation.

Root bootstrap owns migration of historical root-level `packages`, `plans`, and the
obsolete root-owned `deployments` directory. Recurring activation owns only the
runner-writable PostgreSQL instance receipt tree. This controller collapses the
remaining package-specific runner outputs into the existing
`cluster-activation/<package-id>` generation after activation finishes.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import pwd
import sys
from typing import Any, Sequence


REPOSITORY = Path(__file__).resolve().parents[2]
RUNNER_USER = "laplace-runner"


def load_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


estate = load_module(
    "laplace_receipt_estate_shared",
    REPOSITORY / "tools/delivery/receipt_estate.py",
)


class RunnerReceiptEstateError(RuntimeError):
    pass


def converge(cluster_contract: dict[str, Any]) -> dict[str, Any]:
    expected = pwd.getpwnam(RUNNER_USER)
    if os.geteuid() != expected.pw_uid:
        actual = pwd.getpwuid(os.geteuid()).pw_name
        raise RunnerReceiptEstateError(
            f"runner receipt convergence requires {RUNNER_USER}, not {actual}"
        )
    instance = cluster_contract.get("instance")
    if not isinstance(instance, dict):
        raise RunnerReceiptEstateError("PostgreSQL cluster contract instance is absent")
    instance_root = Path(str(instance.get("receipt_directory", "")))
    if not instance_root.is_absolute():
        raise RunnerReceiptEstateError("PostgreSQL receipt directory is not absolute")
    canonical_root = instance_root / "cluster-activation"
    estate.ensure_directory(
        instance_root, expected.pw_uid, expected.pw_gid, False
    )
    estate.ensure_directory(
        canonical_root, expected.pw_uid, expected.pw_gid, False
    )

    migrated: list[dict[str, str]] = []
    preserved: list[str] = []
    removed: list[str] = []
    estate.migrate_package_directory_tree(
        instance_root / "plan",
        canonical_root,
        {"host-inventory.json", "host-selection.json", "resource-observation.json"},
        expected.pw_uid,
        expected.pw_gid,
        False,
        migrated,
        preserved,
        removed,
    )
    estate.migrate_named_package_leaf(
        instance_root / "release-capacity",
        canonical_root,
        "capacity.json",
        "release-capacity.json",
        expected.pw_uid,
        expected.pw_gid,
        False,
        migrated,
        preserved,
        removed,
    )
    estate.migrate_named_package_leaf(
        instance_root / "product-cognition",
        canonical_root,
        "installed-proof.json",
        "installed-cognition-proof.json",
        expected.pw_uid,
        expected.pw_gid,
        False,
        migrated,
        preserved,
        removed,
    )
    estate.migrate_singleton_product_receipt(
        instance_root / "unicode-product-activation.json",
        canonical_root,
        "unicode-product-activation.json",
        expected.pw_uid,
        expected.pw_gid,
        False,
        migrated,
    )
    estate.migrate_singleton_product_receipt(
        instance_root / "highway-product-activation.json",
        canonical_root,
        "highway-product-activation.json",
        expected.pw_uid,
        expected.pw_gid,
        False,
        migrated,
    )
    result: dict[str, Any] = {
        "schema": "laplace.runner-receipt-estate-convergence/v1",
        "canonical_package_evidence_root": str(canonical_root),
        "migrated": migrated,
        "removed_empty_legacy_roots": removed,
        "preserved_unknown_or_conflicting": sorted(set(preserved)),
        "migration_count": len(migrated),
    }
    result["receipt_sha256"] = estate.sha256_bytes(estate.canonical_bytes(result))
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cluster-contract", default="contracts/postgresql-cluster.json"
    )
    parser.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    result = converge(estate.load_json(Path(arguments.cluster_contract)))
    content = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(content)
    else:
        output = Path(arguments.output)
        if not output.parent.is_dir():
            raise RunnerReceiptEstateError("output parent is absent")
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (RunnerReceiptEstateError, estate.ReceiptEstateError) as error:
        print(f"runner-receipt-estate: {error}", file=sys.stderr)
        raise SystemExit(1) from error
