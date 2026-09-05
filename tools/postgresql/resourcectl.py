#!/usr/bin/env python3
"""Observe physical PostgreSQL product resources through the packaged native authority.

Resource observation is a pre-activation execution-authority operation, not a
PostgreSQL lifecycle transition.  This controller therefore validates the selected
runner-owned cluster contract directly and invokes the exact packaged
``laplace_resource_observe`` binary without entering the legacy cluster-core CLI.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from typing import Any, Sequence


_CLUSTERCTL_PATH = Path(__file__).with_name("clusterctl.py")
_SPEC = importlib.util.spec_from_file_location(
    "laplace_postgresql_runner_clusterctl", _CLUSTERCTL_PATH
)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot load runner-owned PostgreSQL cluster controller")
clusterctl = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = clusterctl
_SPEC.loader.exec_module(clusterctl)


def observe_resources(
    contract_path: Path,
    package_path: Path,
    package_physical_root: Path,
) -> dict[str, Any]:
    contract = clusterctl.load_json(contract_path)
    package = clusterctl.load_json(package_path)
    clusterctl.validate_contract(contract)

    package_status = clusterctl.verify_package(
        package, contract, package_physical_root
    )
    if not package_status.verified:
        raise clusterctl.ClusterError(
            "native resource observation requires exact package bytes: "
            f"{package_status.reason}"
        )

    observer_entry = package_status.files.get(clusterctl.RESOURCE_OBSERVER_PATH)
    if not isinstance(observer_entry, dict) or observer_entry.get("kind") != "file":
        raise clusterctl.ClusterError("package omits the native resource observer")

    observer = (
        clusterctl.prefixed(package_physical_root, package["root"])
        / clusterctl.RESOURCE_OBSERVER_PATH
    )
    instance = contract["instance"]
    policy = contract["resource_policy"]
    command = [
        str(observer),
        "--data",
        instance["data_directory"],
        "--wal",
        instance["wal_directory"],
        "--temporary",
        instance["temp_directory"],
        "--maximum-cpu-slots",
        str(policy["maximum_cpu_slots"]),
        "--maximum-memory-bytes",
        str(policy["maximum_memory_bytes"]),
        "--maximum-io-slots",
        str(policy["maximum_io_slots"]),
    ]
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        env={"LANG": "C", "LC_ALL": "C", "PATH": "/usr/bin:/bin"},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or f"exit {completed.returncode}"
        raise clusterctl.ClusterError(f"native resource observer failed: {detail}")

    try:
        native = json.loads(
            completed.stdout,
            object_pairs_hook=clusterctl.reject_duplicate_keys,
        )
    except json.JSONDecodeError as error:
        raise clusterctl.ClusterError(
            f"native resource observer returned invalid JSON: {error}"
        ) from error
    if not isinstance(native, dict):
        raise clusterctl.ClusterError("native resource observer output must be an object")

    result = clusterctl.finalize_native_resource_observation(
        native,
        observer_entry,
        contract,
        package["package_id"],
        package_status.manifest_sha256,
    )
    clusterctl.validate_resource_observation(result, contract)
    clusterctl.validate_resource_package_binding(result, package)
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    observe = subparsers.add_parser("observe-resources")
    observe.add_argument("--contract", required=True)
    observe.add_argument("--package-manifest", required=True)
    observe.add_argument("--package-physical-root", required=True)
    observe.add_argument("--output", default="-")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    if arguments.command != "observe-resources":
        raise clusterctl.ClusterError(
            f"unsupported resource operation: {arguments.command}"
        )
    result = observe_resources(
        Path(arguments.contract),
        Path(arguments.package_manifest),
        Path(arguments.package_physical_root),
    )
    clusterctl.write_json(Path(arguments.output), result)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"resourcectl: {error}", file=sys.stderr)
        raise SystemExit(1) from error
