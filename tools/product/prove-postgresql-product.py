#!/usr/bin/env python3
"""Bind current source/package identity to the root-authoritative live PostgreSQL product."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Mapping, Sequence

PROOF_SCHEMA = "laplace.postgresql-product-proof/v1"
BINDING_SCHEMA = "laplace.postgresql-product-proof-binding/v1"
PACKAGE_PROOF_SCHEMA = "laplace.package-product-proof/v1"
HEX_64 = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT = re.compile(r"^[0-9a-f]{40}$")

class PostgreSQLProductProofError(RuntimeError):
    pass

def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PostgreSQLProductProofError(f"duplicate JSON key: {key}")
        result[key] = value
    return result

def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys)
    except (OSError, json.JSONDecodeError) as error:
        raise PostgreSQLProductProofError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise PostgreSQLProductProofError(f"JSON root must be an object: {path}")
    return value

def parse_json(text: str) -> dict[str, Any]:
    try:
        value = json.loads(text, object_pairs_hook=reject_duplicate_keys)
    except json.JSONDecodeError as error:
        raise PostgreSQLProductProofError(f"invalid gateway probe JSON: {error}") from error
    if not isinstance(value, dict):
        raise PostgreSQLProductProofError("gateway probe root must be an object")
    return value

def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")

def probe_identity(value: Mapping[str, Any]) -> str:
    payload = {key: item for key, item in value.items() if key != "probe_sha256"}
    return hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()

def require_hex(value: Any, label: str) -> str:
    if not isinstance(value, str) or HEX_64.fullmatch(value) is None:
        raise PostgreSQLProductProofError(f"{label} is not a lowercase 256-bit identity")
    return value

def require_git(value: Any, label: str) -> str:
    if not isinstance(value, str) or GIT_OBJECT.fullmatch(value) is None:
        raise PostgreSQLProductProofError(f"{label} is not an exact Git object identity")
    return value

def require_file(path: Path, label: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise PostgreSQLProductProofError(f"{label} is absent or not physical: {path}")

def run(command: Sequence[str], label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(list(command), check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or f"exit {completed.returncode}"
        raise PostgreSQLProductProofError(f"{label} failed: {detail}")
    return completed

def repository_identity(repository: Path) -> tuple[str, str]:
    commit = run(["git", "-C", str(repository), "rev-parse", "HEAD"], "repository commit").stdout.strip()
    tree = run(["git", "-C", str(repository), "rev-parse", "HEAD^{tree}"], "repository tree").stdout.strip()
    return require_git(commit, "repository commit"), require_git(tree, "repository tree")

def validate_probe(probe: Mapping[str, Any], package: Mapping[str, Any], commit: str, tree: str, schema: str) -> None:
    expected_keys = {
        "schema", "bundle_id", "package_id", "active_target", "postgresql_version",
        "cluster_activation_receipt_sha256", "cluster_plan_sha256",
        "live_loaded_observation_sha256", "probe_sql_sha256", "system_identifier",
        "service", "service_state", "loaded_objects", "config_files", "probe_sha256",
    }
    if set(probe) != expected_keys:
        raise PostgreSQLProductProofError("gateway product probe fields differ")
    if package.get("schema") != PACKAGE_PROOF_SCHEMA or package.get("phase") != "composed-installed-retained":
        raise PostgreSQLProductProofError("package proof is incomplete")
    if package.get("repository_commit") != require_git(commit, "expected source commit") or package.get("repository_tree") != require_git(tree, "expected source tree"):
        raise PostgreSQLProductProofError("package proof belongs to different source")
    package_id = require_hex(package.get("package_id"), "package proof package id")
    for label, field in (
        ("gateway bundle id", "bundle_id"),
        ("cluster activation receipt", "cluster_activation_receipt_sha256"),
        ("cluster plan", "cluster_plan_sha256"),
        ("live loaded observation", "live_loaded_observation_sha256"),
        ("live probe program", "probe_sql_sha256"),
        ("gateway probe", "probe_sha256"),
    ):
        require_hex(probe.get(field), label)
    if (
        probe.get("schema") != schema
        or probe.get("package_id") != package_id
        or probe.get("active_target") != f"releases/{package_id}"
        or probe.get("postgresql_version") != "18.6"
        or probe.get("service_state") != "active"
        or not str(probe.get("system_identifier", "")).isdecimal()
        or probe.get("probe_sha256") != probe_identity(probe)
        or not isinstance(probe.get("loaded_objects"), list)
        or not probe["loaded_objects"]
        or not isinstance(probe.get("config_files"), list)
        or not probe["config_files"]
    ):
        raise PostgreSQLProductProofError("gateway product probe does not match current package/product state")

def store_receipt(executable: Path, receipt: Path, root: Path) -> str:
    require_file(executable, "receipt-store executable")
    require_file(receipt, "PostgreSQL product receipt")
    root.mkdir(parents=True, exist_ok=True, mode=0o2750)
    completed = run([str(executable), "put", "--receipt", str(receipt), "--root", str(root)], "durable product receipt publication")
    digest = require_hex(completed.stdout.strip(), "durable product receipt BLAKE3")
    run([str(executable), "verify", "--digest", digest, "--root", str(root)], "durable product receipt replay")
    return digest

def prove(repository: Path, package_proof_path: Path, output: Path) -> dict[str, Any]:
    repository = repository.resolve()
    package = load_json(package_proof_path)
    commit, tree = repository_identity(repository)
    gateway_contract = load_json(repository / "contracts/product-activation-gateway.json")
    schema = gateway_contract.get("operation", {}).get("product_probe_schema")
    if schema != "laplace.product-activation-gateway-probe/v2":
        raise PostgreSQLProductProofError("gateway product probe contract differs")
    gateway = Path(str(gateway_contract.get("gateway", {}).get("executable", "")))
    if not gateway.is_absolute():
        raise PostgreSQLProductProofError("installed gateway path is not absolute")
    completed = run(["/usr/bin/sudo", "-n", str(gateway), "probe"], "root-authoritative PostgreSQL product probe")
    probe = parse_json(completed.stdout)
    validate_probe(probe, package, commit, tree, schema)

    installed_release = Path(str(package.get("installed_release", "")))
    store = installed_release / "bin/laplace_receipt_store"
    durable_package_root = Path(str(package.get("durable_receipt_root", "")))
    if not durable_package_root.is_absolute() or durable_package_root.name != "package-product":
        raise PostgreSQLProductProofError("package proof durable receipt authority differs")
    durable_root = durable_package_root.parent / "postgresql-product"
    binding = {
        "schema": BINDING_SCHEMA,
        "repository_commit": commit,
        "repository_tree": tree,
        "package_id": package["package_id"],
        "package_binding_receipt_blake3": require_hex(package.get("binding_receipt_blake3"), "package binding BLAKE3"),
        "gateway_bundle_id": probe["bundle_id"],
        "gateway_probe_sha256": probe["probe_sha256"],
        "cluster_activation_receipt_sha256": probe["cluster_activation_receipt_sha256"],
        "cluster_plan_sha256": probe["cluster_plan_sha256"],
        "live_loaded_observation_sha256": probe["live_loaded_observation_sha256"],
        "system_identifier": probe["system_identifier"],
    }
    binding_path = output.parent / "postgresql-product-binding.json"
    if binding_path.exists() or output.exists():
        raise PostgreSQLProductProofError("PostgreSQL product proof output already exists")
    binding_path.write_bytes(canonical_bytes(binding))
    binding_blake3 = store_receipt(store, binding_path, durable_root)
    result = {
        "schema": PROOF_SCHEMA,
        "phase": "current-source-package-live-product-retained",
        "repository_commit": commit,
        "repository_tree": tree,
        "package_id": package["package_id"],
        "postgresql_version": probe["postgresql_version"],
        "gateway_bundle_id": probe["bundle_id"],
        "gateway_probe_sha256": probe["probe_sha256"],
        "cluster_activation_receipt_sha256": probe["cluster_activation_receipt_sha256"],
        "cluster_plan_sha256": probe["cluster_plan_sha256"],
        "live_loaded_observation_sha256": probe["live_loaded_observation_sha256"],
        "system_identifier": probe["system_identifier"],
        "binding_receipt_blake3": binding_blake3,
        "durable_receipt_root": str(durable_root),
    }
    output.write_bytes(canonical_bytes(result))
    return result

def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=".")
    parser.add_argument("--package-proof", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)

def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    prove(Path(args.repository), Path(args.package_proof), Path(args.output))
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except PostgreSQLProductProofError as error:
        print(f"postgresql-product-proof: {error}", file=sys.stderr)
        raise SystemExit(1) from error
