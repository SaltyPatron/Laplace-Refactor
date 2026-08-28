#!/usr/bin/env python3
"""Activate and prove the complete Unicode product root in the selected cluster.

This file orchestrates one native semantic operation. It does not decode Unicode,
construct Tier-0 records, reproduce the perfcache access law, or mint substrate
identity. Those responsibilities remain in the packaged native engine and extension.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import pwd
import re
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Callable, Sequence


MODULE_PATH = Path(__file__).with_name("clusterctl.py")
SPEC = importlib.util.spec_from_file_location("laplace_unicode_clusterctl", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load the canonical PostgreSQL cluster controller")
clusterctl = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = clusterctl
SPEC.loader.exec_module(clusterctl)

CONTRACT_SCHEMA = "laplace.unicode-product-activation-contract/v1"
REQUEST_SCHEMA = "laplace.unicode-product-activation-request/v1"
IDENTITY_SCHEMA = "laplace.unicode-activation-identities/v1"
RECEIPT_SCHEMA = "laplace.unicode-product-activation-receipt/v1"
FAILURE_SCHEMA = "laplace.unicode-product-activation-failure/v1"
HEX_128 = re.compile(r"^[0-9a-f]{32}$")
HEX_256 = re.compile(r"^[0-9a-f]{64}$")


class UnicodeActivationError(RuntimeError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    output: dict[str, Any] = {}
    for key, value in pairs:
        if key in output:
            raise UnicodeActivationError(f"duplicate JSON object key: {key}")
        output[key] = value
    return output


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise UnicodeActivationError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise UnicodeActivationError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
    except OSError as error:
        raise UnicodeActivationError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest()


def document_identity(value: dict[str, Any], field: str) -> str:
    payload = dict(value)
    payload.pop(field, None)
    return sha256_bytes(canonical_bytes(payload))


def require_absolute(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.startswith("/"):
        raise UnicodeActivationError(f"{field} must be an absolute path")
    path = PurePosixPath(value)
    if ".." in path.parts or str(path) != value or any(ord(c) < 32 for c in value):
        raise UnicodeActivationError(f"{field} must be a normalized safe absolute path")
    return value


def require_relative(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value or value.startswith("/"):
        raise UnicodeActivationError(f"{field} must be a relative path")
    path = PurePosixPath(value)
    if ".." in path.parts or str(path) != value or any(ord(c) < 32 for c in value):
        raise UnicodeActivationError(f"{field} must be a normalized safe relative path")
    return value


def prefixed(root: Path, logical: str) -> Path:
    return clusterctl.prefixed(root, require_absolute(logical, "logical path"))


def sql_literal(value: str) -> str:
    if "\x00" in value or any(ord(character) < 32 for character in value):
        raise UnicodeActivationError("SQL text input contains a control character")
    return "'" + value.replace("'", "''") + "'"


def validate_activation_contract(
    contract: dict[str, Any],
    cluster_contract: dict[str, Any],
    source_contract: dict[str, Any],
    postgresql_contract: dict[str, Any],
) -> None:
    if contract.get("schema") != CONTRACT_SCHEMA or contract.get("version") != "1.0.0":
        raise UnicodeActivationError("Unicode product activation contract is invalid")
    authority = contract.get("authority")
    if not isinstance(authority, dict):
        raise UnicodeActivationError("activation authority boundary is absent")
    expected_authority = {
        "cluster_contract_schema": clusterctl.CONTRACT_SCHEMA,
        "cluster_activation_receipt_schema": clusterctl.ACTIVATION_SCHEMA,
        "cluster_plan_schema": clusterctl.PLAN_SCHEMA,
        "package_manifest_schema": clusterctl.PACKAGE_SCHEMA,
        "unicode_source_contract_schema": "laplace.unicode-source-contract/v1",
        "unicode_postgresql_contract_schema": "laplace.unicode-postgresql-contract/v1",
        "unicode_postgresql_contract_fingerprint": "a820581b0e1c36e16a7394ae4fd3bdce1d1910faccc97cbb69838e0876106dc4",
        "unicode_version": "17.0.0",
        "source_file_count": 33,
    }
    if authority != expected_authority:
        raise UnicodeActivationError("activation authority boundary drifted")
    if contract.get("orchestrator") != {
        "repository_path": "tools/postgresql/unicodectl.py",
        "classification": "typed-product-lifecycle-orchestrator-not-semantic-engine",
        "bind_exact_bytes_into_request": True,
    }:
        raise UnicodeActivationError("Unicode product orchestrator boundary differs")
    clusterctl.validate_contract(cluster_contract)
    if cluster_contract.get("schema") != authority["cluster_contract_schema"]:
        raise UnicodeActivationError("cluster contract schema differs")
    if source_contract.get("schema") != authority["unicode_source_contract_schema"]:
        raise UnicodeActivationError("Unicode source contract schema differs")
    if source_contract.get("unicode_version") != authority["unicode_version"]:
        raise UnicodeActivationError("Unicode source version differs")
    files = source_contract.get("files")
    if (
        not isinstance(files, list)
        or len(files) != authority["source_file_count"]
        or source_contract.get("file_count") != len(files)
    ):
        raise UnicodeActivationError("Unicode source file boundary differs")
    if postgresql_contract.get("schema") != authority["unicode_postgresql_contract_schema"]:
        raise UnicodeActivationError("Unicode PostgreSQL contract schema differs")
    fingerprint = postgresql_contract.get("contract_fingerprint", {})
    if fingerprint.get("value") != authority["unicode_postgresql_contract_fingerprint"]:
        raise UnicodeActivationError("Unicode PostgreSQL contract fingerprint differs")

    identity = contract.get("identity_provider")
    if not isinstance(identity, dict):
        raise UnicodeActivationError("activation identity provider is absent")
    if (
        identity.get("package_path") != "bin/laplace_unicode_activation_identify"
        or identity.get("output_schema") != IDENTITY_SCHEMA
        or identity.get("algorithm") != "BLAKE3-256"
        or identity.get("maximum_request_bytes") != 4 * 1024 * 1024
    ):
        raise UnicodeActivationError("activation identity provider contract differs")
    expected_fields = {
        "request_fingerprint": 32,
        "activation_epoch_id": 16,
        "activation_epoch_fingerprint": 32,
        "authority_fingerprint": 32,
        "source_epoch": 32,
        "identity_epoch": 32,
        "geometry_epoch": 32,
        "evidence_epoch": 32,
        "firmware_epoch": 32,
        "dependency_epoch": 32,
        "database_epoch": 32,
        "perfcache_epoch": 32,
        "numeric_epoch": 32,
        "package_epoch": 32,
    }
    fields = identity.get("fields")
    if not isinstance(fields, dict) or set(fields) != set(expected_fields):
        raise UnicodeActivationError("activation identity field set differs")
    domains: set[str] = set()
    for name, byte_count in expected_fields.items():
        item = fields[name]
        if not isinstance(item, dict) or item.get("bytes") != byte_count:
            raise UnicodeActivationError(f"activation identity width differs: {name}")
        domain = item.get("domain")
        if not isinstance(domain, str) or not domain.startswith(
            "laplace.unicode-product-activation."
        ):
            raise UnicodeActivationError(f"activation identity domain differs: {name}")
        domains.add(domain)
    if len(domains) != len(expected_fields):
        raise UnicodeActivationError("activation identity domains are not independent")

    operation = contract.get("operation", {})
    if (
        operation.get("initial_activation_only") is not True
        or operation.get("expected_old_sequence") != 0
        or operation.get("expected_old_present") is not False
        or operation.get("expected_old_epoch_id") != "0" * 32
        or operation.get("expected_old_epoch_fingerprint") != "0" * 64
        or operation.get("readback_positions") != [0, 65, 1114111]
    ):
        raise UnicodeActivationError("initial Unicode activation law differs")
    for key in ("maximum_batch_bytes",):
        if not isinstance(operation.get(key), int) or operation[key] <= 0:
            raise UnicodeActivationError(f"operation.{key} is invalid")
    context = contract.get("execution_context", {})
    if context != {
        "memory_bytes": 1073741824,
        "cpu_slots": 6,
        "io_slots": 2,
        "epoch_mask": 1023,
        "framework_major": 1,
        "framework_minor": 6,
        "flags": 1,
    }:
        raise UnicodeActivationError("Unicode activation execution context differs")
    expected = contract.get("expected_result", {})
    exact = {
        "total_frame_count": 2230150,
        "entity_count": 1114112,
        "physicality_count": 1114112,
        "atom_count": 1114112,
        "ducet_position_count": 1114112,
        "ducet_contraction_count": 964,
        "normalization_composition_count": 961,
        "tier0_artifact_bytes": 762586574,
        "reverse_artifact_bytes": 117440896,
        "tier0_artifact_digest": "8950d9867428fd660f8a49377b0c4a693b57ef0a9807ea189e425d0bb847c291",
        "reverse_artifact_digest": "6f33df84440a8f4bb19afa608befac11f17ad68123d06e043c2be7451f6ab7b1",
        "plan_manifest_fingerprint": "3a546d581afc1c4caf78c1c235aafd41b283984325d5717b188211d1091cb9e5",
        "perfcache_artifact_count": 2,
        "perfcache_dependency_count": 1,
        "reverse_dependency_module_id": "cb4d73fe1c7ad3784bdd69f9e22f5b3f",
    }
    if expected != exact:
        raise UnicodeActivationError("Unicode product result boundary differs")
    receipt = contract.get("receipt", {})
    if (
        receipt.get("schema") != RECEIPT_SCHEMA
        or receipt.get("failure_schema") != FAILURE_SCHEMA
        or receipt.get("directory_name") != "unicode"
    ):
        raise UnicodeActivationError("Unicode activation receipt contract differs")


def verify_source_bundle(
    source_contract: dict[str, Any], source_root: Path
) -> dict[str, Any]:
    if not source_root.is_absolute() or not source_root.is_dir() or source_root.is_symlink():
        raise UnicodeActivationError("verified Unicode source root is not a directory")
    evidence: list[dict[str, Any]] = []
    observed_paths: set[str] = set()
    for index, entry in enumerate(source_contract.get("files", [])):
        if not isinstance(entry, dict):
            raise UnicodeActivationError(f"source file entry {index} is invalid")
        relative = require_relative(entry.get("path"), f"source.files[{index}].path")
        if relative in observed_paths:
            raise UnicodeActivationError(f"Unicode source path is duplicated: {relative}")
        observed_paths.add(relative)
        expected_bytes = entry.get("bytes")
        expected_digest = entry.get("sha256")
        if (
            not isinstance(expected_bytes, int)
            or expected_bytes < 0
            or HEX_256.fullmatch(str(expected_digest)) is None
        ):
            raise UnicodeActivationError(f"Unicode source authority is invalid: {relative}")
        path = source_root / relative
        if not path.is_file() or path.is_symlink():
            raise UnicodeActivationError(f"Unicode source file is absent: {relative}")
        actual_bytes = path.stat().st_size
        actual_digest = sha256_file(path)
        if actual_bytes != expected_bytes or actual_digest != expected_digest:
            raise UnicodeActivationError(f"Unicode source bytes differ: {relative}")
        evidence.append(
            {"path": relative, "bytes": actual_bytes, "sha256": actual_digest}
        )
    for marker in source_contract.get("version_markers", []):
        if not isinstance(marker, dict):
            raise UnicodeActivationError("Unicode version marker is invalid")
        relative = require_relative(marker.get("path"), "version_markers.path")
        if "contains" not in marker:
            continue
        expected = marker["contains"]
        if not isinstance(expected, str) or not expected:
            raise UnicodeActivationError(f"Unicode version marker is invalid: {relative}")
        try:
            content = (source_root / relative).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise UnicodeActivationError(
                f"cannot inspect Unicode version marker {relative}: {error}"
            ) from error
        if expected not in content:
            raise UnicodeActivationError(f"Unicode version marker differs: {relative}")
    result = {
        "unicode_version": source_contract["unicode_version"],
        "source_root": str(source_root),
        "files": evidence,
    }
    result["source_evidence_sha256"] = sha256_bytes(canonical_bytes(result))
    return result


def activation_receipt_identity(receipt: dict[str, Any]) -> str:
    return document_identity(receipt, "activation_receipt_sha256")


def validate_product_boundary(
    cluster_contract: dict[str, Any],
    package: dict[str, Any],
    plan: dict[str, Any],
    activation_receipt: dict[str, Any],
    root: Path,
) -> None:
    clusterctl.validate_contract(cluster_contract)
    clusterctl.validate_plan(plan, cluster_contract)
    verification = clusterctl.verify_package(package, cluster_contract, root)
    if not verification.verified:
        raise UnicodeActivationError(f"product package does not verify: {verification.reason}")
    if plan.get("package_id") != package.get("package_id"):
        raise UnicodeActivationError("cluster plan and package identity differ")
    if plan.get("package_manifest_sha256") != verification.manifest_sha256:
        raise UnicodeActivationError("cluster plan and package manifest differ")
    if (
        activation_receipt.get("schema") != clusterctl.ACTIVATION_SCHEMA
        or activation_receipt.get("phase") != "activated"
        or activation_receipt.get("package_id") != package.get("package_id")
        or activation_receipt.get("plan_sha256") != plan.get("plan_sha256")
        or activation_receipt.get("restart_proven") is not True
        or activation_receipt.get("active_target")
        != f"releases/{package.get('package_id')}"
        or activation_receipt.get("activation_receipt_sha256")
        != activation_receipt_identity(activation_receipt)
    ):
        raise UnicodeActivationError("cluster activation receipt is not exact and complete")
    system_identifier = str(activation_receipt.get("system_identifier", ""))
    if not system_identifier.isdecimal() or int(system_identifier) <= 0:
        raise UnicodeActivationError("cluster activation omits system identity")
    active = prefixed(root, cluster_contract["package"]["active_link"])
    if not active.is_symlink() or os.readlink(active) != activation_receipt["active_target"]:
        raise UnicodeActivationError("active product pointer differs from cluster activation")
    if active.resolve() != prefixed(root, package["root"]).resolve():
        raise UnicodeActivationError("active product pointer resolves outside the package")


def validate_identities(value: dict[str, Any], contract: dict[str, Any]) -> None:
    fields = contract["identity_provider"]["fields"]
    if value.get("schema") != IDENTITY_SCHEMA or set(value) != {"schema", *fields}:
        raise UnicodeActivationError("native activation identity output differs")
    for name, specification in fields.items():
        text = value.get(name)
        pattern = HEX_128 if specification["bytes"] == 16 else HEX_256
        if not isinstance(text, str) or pattern.fullmatch(text) is None:
            raise UnicodeActivationError(f"native activation identity is invalid: {name}")
        if set(text) == {"0"}:
            raise UnicodeActivationError(f"native activation identity is zero: {name}")


def run_identity_provider(
    executable: Path, request_path: Path, contract: dict[str, Any]
) -> tuple[dict[str, Any], dict[str, Any]]:
    if not executable.is_file() or executable.is_symlink() or not os.access(executable, os.X_OK):
        raise UnicodeActivationError("packaged activation identity provider is unavailable")
    command = [str(executable), "--request", str(request_path)]
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        env=clusterctl.activation_environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    evidence = clusterctl.command_execution_receipt(
        "identify-unicode-activation", command, completed
    )
    if completed.returncode != 0:
        raise UnicodeActivationError(
            f"native activation identity provider failed: {completed.stderr.strip()}"
        )
    try:
        value = json.loads(completed.stdout, object_pairs_hook=reject_duplicate_keys)
    except json.JSONDecodeError as error:
        raise UnicodeActivationError(
            f"native activation identity provider returned invalid JSON: {error}"
        ) from error
    if not isinstance(value, dict):
        raise UnicodeActivationError("native activation identity output is not an object")
    validate_identities(value, contract)
    return value, evidence


def atomic_write(path: Path, content: bytes, mode: int = 0o640) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def write_immutable(path: Path, value: dict[str, Any]) -> None:
    content = canonical_bytes(value)
    if path.exists():
        if not path.is_file() or path.is_symlink() or path.read_bytes() != content:
            raise UnicodeActivationError(f"existing evidence differs: {path}")
        return
    atomic_write(path, content)


def parse_single_json(text: str, label: str) -> dict[str, Any]:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if len(lines) != 1:
        raise UnicodeActivationError(f"{label} did not return exactly one result")
    try:
        value = json.loads(lines[0], object_pairs_hook=reject_duplicate_keys)
    except json.JSONDecodeError as error:
        raise UnicodeActivationError(f"{label} returned invalid JSON: {error}") from error
    if not isinstance(value, dict):
        raise UnicodeActivationError(f"{label} result is not an object")
    return value


def run_psql(
    plan: dict[str, Any],
    cluster_contract: dict[str, Any],
    sql: str,
    label: str,
    os_user: str,
    database_role: str,
    timeout: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    instance = plan["instance"]
    command = [
        "/usr/sbin/runuser",
        "--user",
        os_user,
        "--",
        f"{plan['package_root']}/pgsql-18/bin/psql",
        "--host",
        instance["socket_directory"],
        "--port",
        str(instance["port"]),
        "--username",
        database_role,
        "--dbname",
        instance["database"],
        "--no-psqlrc",
        "--set",
        "ON_ERROR_STOP=1",
        "--quiet",
        "--tuples-only",
        "--no-align",
    ]
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        env=clusterctl.activation_environment(),
        input=sql,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    receipt = clusterctl.command_execution_receipt(label, command, completed)
    receipt["stdin_sha256"] = sha256_bytes(sql.encode("utf-8"))
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise UnicodeActivationError(
            f"{label} failed with exit {completed.returncode}: {detail[-1000:]}"
        )
    return parse_single_json(completed.stdout, label), receipt


def render_context(identities: dict[str, Any], contract: dict[str, Any]) -> str:
    epoch_names = [
        "source_epoch",
        "identity_epoch",
        "geometry_epoch",
        "evidence_epoch",
        "firmware_epoch",
        "dependency_epoch",
        "database_epoch",
        "perfcache_epoch",
        "numeric_epoch",
        "package_epoch",
    ]
    epochs = ",".join(f"decode('{identities[name]}','hex')" for name in epoch_names)
    context = contract["execution_context"]
    return (
        "ROW(ARRAY["
        + epochs
        + f"],decode('{identities['authority_fingerprint']}','hex'),"
        + f"{context['memory_bytes']}::bigint,{context['cpu_slots']},"
        + f"{context['io_slots']},{context['epoch_mask']}::bigint,"
        + f"{context['framework_major']}::smallint,"
        + f"{context['framework_minor']}::smallint,{context['flags']})"
        + "::laplace.execution_context"
    )


def render_inspection_sql() -> str:
    return """SELECT json_build_object(
  'sequence', active.sequence,
  'active_present', active.active_present,
  'activation_epoch_id', encode(active.activation_epoch_id, 'hex'),
  'activation_epoch_fingerprint', encode(active.epoch_fingerprint, 'hex'),
  'generation_count', (SELECT count(*) FROM laplace.unicode_root_generation),
  'deposit_count', (SELECT count(*) FROM laplace.unicode_root_deposit_receipt),
  'entity_count', (SELECT count(*) FROM laplace.entity),
  'physicality_count', (SELECT count(*) FROM laplace.physicality),
  'atom_count', (SELECT count(*) FROM laplace.attestation
                 WHERE source_fingerprint =
                       (SELECT root_receipt FROM laplace.unicode_root_generation)
                   AND attestation_kind = 3),
  'ducet_position_count', (SELECT count(*) FROM laplace.unicode_ducet_position),
  'ducet_contraction_count', (SELECT count(*) FROM laplace.unicode_ducet_contraction),
  'normalization_composition_count', (SELECT count(*) FROM laplace.unicode_normalization_composition),
  'perfcache_generation_count', (SELECT count(*) FROM laplace.perfcache_generation),
  'activation_event_count', (SELECT count(*) FROM laplace.perfcache_activation_event),
  'root_receipt', (SELECT encode(root_receipt,'hex') FROM laplace.unicode_root_generation),
  'producer_receipt', (SELECT encode(producer_receipt,'hex') FROM laplace.unicode_root_deposit_receipt),
  'staged_stream_receipt', (SELECT encode(staged_stream_receipt,'hex') FROM laplace.unicode_root_deposit_receipt),
  'sink_artifacts_fingerprint', (SELECT encode(sink_artifacts_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),
  'postgresql_artifact_fingerprint', (SELECT encode(postgresql_artifact_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),
  'perfcache_artifact_set_fingerprint', (SELECT encode(perfcache_artifact_set_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),
  'perfcache_manifest_fingerprint', (SELECT encode(perfcache_manifest_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),
  'perfcache_encoded_manifest_fingerprint', (SELECT encode(perfcache_encoded_manifest_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),
  'admission_receipt', (SELECT encode(admission_receipt,'hex') FROM laplace.unicode_root_deposit_receipt),
  'total_frame_count', (SELECT total_frame_count FROM laplace.unicode_root_deposit_receipt),
  'total_encoded_bytes', (SELECT total_encoded_bytes FROM laplace.unicode_root_deposit_receipt),
  'batch_count', (SELECT batch_count FROM laplace.unicode_root_deposit_receipt),
  'plan_manifest_fingerprint', (SELECT encode(plan_manifest_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),
  'plan_sequence_fingerprint', (SELECT encode(plan_sequence_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),
  'plan_count', (SELECT plan_count FROM laplace.unicode_root_deposit_receipt)
)::text
FROM laplace.perfcache_active_control AS active
WHERE active.singleton;
"""


def render_activation_sql(
    contract: dict[str, Any],
    identities: dict[str, Any],
    source_root: str,
    spool_directory: str,
    tier0_path: str,
    reverse_path: str,
) -> str:
    operation = contract["operation"]
    expected = contract["expected_result"]
    context = render_context(identities, contract)
    return f"""BEGIN;
SET LOCAL statement_timeout = '{operation['statement_timeout_seconds']}s';
SET LOCAL lock_timeout = '30s';
DO $preflight$
DECLARE active laplace.perfcache_active_control%ROWTYPE;
BEGIN
  SELECT * INTO STRICT active FROM laplace.perfcache_active_control WHERE singleton FOR UPDATE;
  IF active.sequence <> 0 OR active.active_present
     OR active.activation_epoch_id <> decode(repeat('00',16),'hex')
     OR active.epoch_fingerprint <> decode(repeat('00',32),'hex')
     OR EXISTS (SELECT 1 FROM laplace.unicode_root_generation)
     OR EXISTS (SELECT 1 FROM laplace.unicode_root_deposit_receipt)
     OR EXISTS (SELECT 1 FROM laplace.entity)
     OR EXISTS (SELECT 1 FROM laplace.physicality)
     OR EXISTS (SELECT 1 FROM laplace.attestation)
     OR EXISTS (SELECT 1 FROM laplace.consensus)
     OR EXISTS (SELECT 1 FROM laplace.perfcache_generation)
     OR EXISTS (SELECT 1 FROM laplace.perfcache_activation_event) THEN
    RAISE EXCEPTION 'initial Unicode product activation requires an empty exact product state';
  END IF;
END
$preflight$;
CREATE TEMP TABLE unicode_product_build AS
SELECT result.* FROM laplace.unicode_root_build_and_activate(
  {context},
  {sql_literal(source_root)},
  {sql_literal(spool_directory)},
  {sql_literal(tier0_path)},
  {sql_literal(reverse_path)},
  decode('{identities['activation_epoch_id']}','hex'),
  decode('{identities['activation_epoch_fingerprint']}','hex'),
  0,false,decode(repeat('00',16),'hex'),decode(repeat('00',32),'hex'),
  {operation['maximum_batch_bytes']}::bigint
) AS result;
DO $verify$
DECLARE build unicode_product_build%ROWTYPE;
DECLARE active laplace.perfcache_active_control%ROWTYPE;
BEGIN
  SELECT * INTO STRICT build FROM unicode_product_build;
  SELECT * INTO STRICT active FROM laplace.perfcache_active_control WHERE singleton;
  IF build.activation_epoch_id <> decode('{identities['activation_epoch_id']}','hex')
     OR build.activation_epoch_fingerprint <> decode('{identities['activation_epoch_fingerprint']}','hex')
     OR build.total_frame_count <> {expected['total_frame_count']}
     OR build.entity_count <> {expected['entity_count']}
     OR build.physicality_count <> {expected['physicality_count']}
     OR build.atom_count <> {expected['atom_count']}
     OR build.ducet_position_count <> {expected['ducet_position_count']}
     OR build.ducet_contraction_count <> {expected['ducet_contraction_count']}
     OR build.normalization_composition_count <> {expected['normalization_composition_count']}
     OR build.tier0_artifact_bytes <> {expected['tier0_artifact_bytes']}
     OR build.reverse_artifact_bytes <> {expected['reverse_artifact_bytes']}
     OR build.tier0_artifact_digest <> decode('{expected['tier0_artifact_digest']}','hex')
     OR build.reverse_artifact_digest <> decode('{expected['reverse_artifact_digest']}','hex')
     OR build.plan_manifest_fingerprint <> decode('{expected['plan_manifest_fingerprint']}','hex')
     OR build.perfcache_artifact_count <> {expected['perfcache_artifact_count']}
     OR build.perfcache_dependency_count <> {expected['perfcache_dependency_count']}
     OR build.reverse_dependency_module_id <> decode('{expected['reverse_dependency_module_id']}','hex')
     OR build.reverse_dependency_artifact_digest <> build.tier0_artifact_digest
     OR active.sequence <> 1 OR NOT active.active_present
     OR active.activation_epoch_id <> build.activation_epoch_id
     OR active.epoch_fingerprint <> build.activation_epoch_fingerprint
     OR (SELECT count(*) FROM laplace.entity) <> {expected['entity_count']}
     OR (SELECT count(*) FROM laplace.physicality) <> {expected['physicality_count']}
     OR (SELECT count(*) FROM laplace.attestation
         WHERE source_fingerprint = build.root_receipt
           AND attestation_kind = 3) <> {expected['atom_count']}
     OR (SELECT count(*) FROM laplace.unicode_ducet_position) <> {expected['ducet_position_count']}
     OR (SELECT count(*) FROM laplace.unicode_ducet_contraction) <> {expected['ducet_contraction_count']}
     OR (SELECT count(*) FROM laplace.unicode_normalization_composition) <> {expected['normalization_composition_count']}
     OR NOT EXISTS (
       SELECT 1 FROM laplace.unicode_root_generation AS generation
       WHERE generation.root_receipt = build.root_receipt
         AND generation.plan_manifest_fingerprint = build.plan_manifest_fingerprint
         AND generation.postgresql_artifact_fingerprint = build.postgresql_artifact_fingerprint)
     OR NOT EXISTS (
       SELECT 1 FROM laplace.unicode_root_deposit_receipt AS deposit
       WHERE deposit.root_receipt = build.root_receipt
         AND deposit.producer_receipt = build.producer_receipt
         AND deposit.staged_stream_receipt = build.staged_stream_receipt
         AND deposit.admission_receipt = build.admission_receipt) THEN
    RAISE EXCEPTION 'Unicode product activation result violates its exact contract';
  END IF;
END
$verify$;
SELECT json_build_object(
  'root_receipt', encode(root_receipt,'hex'),
  'producer_receipt', encode(producer_receipt,'hex'),
  'staged_stream_receipt', encode(staged_stream_receipt,'hex'),
  'sink_artifacts_fingerprint', encode(sink_artifacts_fingerprint,'hex'),
  'postgresql_artifact_fingerprint', encode(postgresql_artifact_fingerprint,'hex'),
  'perfcache_artifact_set_fingerprint', encode(perfcache_artifact_set_fingerprint,'hex'),
  'perfcache_manifest_fingerprint', encode(perfcache_manifest_fingerprint,'hex'),
  'perfcache_encoded_manifest_fingerprint', encode(perfcache_encoded_manifest_fingerprint,'hex'),
  'admission_receipt', encode(admission_receipt,'hex'),
  'activation_epoch_id', encode(activation_epoch_id,'hex'),
  'activation_epoch_fingerprint', encode(activation_epoch_fingerprint,'hex'),
  'total_frame_count', total_frame_count,
  'total_encoded_bytes', total_encoded_bytes,
  'batch_count', batch_count,
  'plan_manifest_fingerprint', encode(plan_manifest_fingerprint,'hex'),
  'plan_sequence_fingerprint', encode(plan_sequence_fingerprint,'hex'),
  'plan_count', plan_count,
  'entity_count', entity_count,
  'physicality_count', physicality_count,
  'atom_count', atom_count,
  'ducet_position_count', ducet_position_count,
  'ducet_contraction_count', ducet_contraction_count,
  'normalization_composition_count', normalization_composition_count,
  'tier0_artifact_digest', encode(tier0_artifact_digest,'hex'),
  'reverse_artifact_digest', encode(reverse_artifact_digest,'hex'),
  'tier0_artifact_bytes', tier0_artifact_bytes,
  'reverse_artifact_bytes', reverse_artifact_bytes
)::text FROM unicode_product_build;
COMMIT;
"""


def render_readback_sql(contract: dict[str, Any], identities: dict[str, Any]) -> str:
    positions = ",".join(str(value) for value in contract["operation"]["readback_positions"])
    return f"""WITH direct AS MATERIALIZED (
  SELECT (laplace.unicode_tier0_resolve_batch(
    decode('{identities['activation_epoch_id']}','hex'),
    decode('{identities['activation_epoch_fingerprint']}','hex'),
    ARRAY[{positions}])).*
), reverse AS MATERIALIZED (
  SELECT (laplace.unicode_identity_reverse_resolve_batch(
    direct.activation_epoch_id,direct.activation_epoch_fingerprint,
    direct.entity_ids,direct.identity_preimage_fingerprints)).*
  FROM direct
)
SELECT json_build_object(
  'direct_positions', direct.codepoint_positions,
  'direct_found', direct.found,
  'direct_entity_ids', ARRAY(SELECT encode(value,'hex') FROM unnest(direct.entity_ids) AS value),
  'direct_identity_preimage_fingerprints', ARRAY(SELECT encode(value,'hex') FROM unnest(direct.identity_preimage_fingerprints) AS value),
  'direct_physicality_ids', ARRAY(SELECT encode(value,'hex') FROM unnest(direct.physicality_ids) AS value),
  'direct_epoch_id', encode(direct.activation_epoch_id,'hex'),
  'direct_epoch_fingerprint', encode(direct.activation_epoch_fingerprint,'hex'),
  'reverse_positions', reverse.codepoint_positions,
  'reverse_found', reverse.found,
  'reverse_epoch_id', encode(reverse.activation_epoch_id,'hex'),
  'reverse_epoch_fingerprint', encode(reverse.activation_epoch_fingerprint,'hex')
)::text FROM direct CROSS JOIN reverse;
"""


def validate_inspection(
    inspection: dict[str, Any], contract: dict[str, Any], identities: dict[str, Any]
) -> str:
    expected = contract["expected_result"]
    empty_counts = (
        inspection.get("generation_count") == 0
        and inspection.get("deposit_count") == 0
        and inspection.get("entity_count") == 0
        and inspection.get("physicality_count") == 0
        and inspection.get("atom_count") == 0
        and inspection.get("ducet_position_count") == 0
        and inspection.get("ducet_contraction_count") == 0
        and inspection.get("normalization_composition_count") == 0
        and inspection.get("perfcache_generation_count") == 0
        and inspection.get("activation_event_count") == 0
    )
    if (
        inspection.get("sequence") == 0
        and inspection.get("active_present") is False
        and inspection.get("activation_epoch_id") == "0" * 32
        and inspection.get("activation_epoch_fingerprint") == "0" * 64
        and empty_counts
    ):
        return "fresh"
    recovered = (
        inspection.get("sequence") == 1
        and inspection.get("active_present") is True
        and inspection.get("activation_epoch_id") == identities["activation_epoch_id"]
        and inspection.get("activation_epoch_fingerprint")
        == identities["activation_epoch_fingerprint"]
        and inspection.get("generation_count") == 1
        and inspection.get("deposit_count") == 1
        and inspection.get("entity_count") == expected["entity_count"]
        and inspection.get("physicality_count") == expected["physicality_count"]
        and inspection.get("atom_count") == expected["atom_count"]
        and inspection.get("ducet_position_count") == expected["ducet_position_count"]
        and inspection.get("ducet_contraction_count")
        == expected["ducet_contraction_count"]
        and inspection.get("normalization_composition_count")
        == expected["normalization_composition_count"]
        and inspection.get("perfcache_generation_count") == 1
        and inspection.get("activation_event_count") == 1
    )
    if recovered:
        return "recover-post-commit"
    raise UnicodeActivationError("existing Unicode product state is neither empty nor exact recovery")


def recover_build_result(
    inspection: dict[str, Any], contract: dict[str, Any], identities: dict[str, Any]
) -> dict[str, Any]:
    """Reconstitute the committed operation receipt after a client-side interruption."""
    expected = contract["expected_result"]
    result = {
        "activation_epoch_id": identities["activation_epoch_id"],
        "activation_epoch_fingerprint": identities["activation_epoch_fingerprint"],
        "tier0_artifact_digest": expected["tier0_artifact_digest"],
        "reverse_artifact_digest": expected["reverse_artifact_digest"],
        "tier0_artifact_bytes": expected["tier0_artifact_bytes"],
        "reverse_artifact_bytes": expected["reverse_artifact_bytes"],
        "recovered_from_exact_committed_state": True,
    }
    for field in (
        "root_receipt",
        "producer_receipt",
        "staged_stream_receipt",
        "sink_artifacts_fingerprint",
        "postgresql_artifact_fingerprint",
        "perfcache_artifact_set_fingerprint",
        "perfcache_manifest_fingerprint",
        "perfcache_encoded_manifest_fingerprint",
        "admission_receipt",
        "total_frame_count",
        "total_encoded_bytes",
        "batch_count",
        "plan_manifest_fingerprint",
        "plan_sequence_fingerprint",
        "plan_count",
        "entity_count",
        "physicality_count",
        "atom_count",
        "ducet_position_count",
        "ducet_contraction_count",
        "normalization_composition_count",
    ):
        result[field] = inspection.get(field)
    validate_build_result(result, contract, identities)
    return result


def validate_build_result(
    result: dict[str, Any], contract: dict[str, Any], identities: dict[str, Any]
) -> None:
    expected = contract["expected_result"]
    if (
        result.get("activation_epoch_id") != identities["activation_epoch_id"]
        or result.get("activation_epoch_fingerprint")
        != identities["activation_epoch_fingerprint"]
    ):
        raise UnicodeActivationError("committed Unicode activation epoch differs")
    for field in (
        "total_frame_count",
        "entity_count",
        "physicality_count",
        "atom_count",
        "ducet_position_count",
        "ducet_contraction_count",
        "normalization_composition_count",
        "tier0_artifact_bytes",
        "reverse_artifact_bytes",
    ):
        if result.get(field) != expected[field]:
            raise UnicodeActivationError(f"committed Unicode result differs: {field}")
    total_encoded_bytes = result.get("total_encoded_bytes")
    batch_count = result.get("batch_count")
    plan_count = result.get("plan_count")
    if not isinstance(total_encoded_bytes, int) or total_encoded_bytes <= 0:
        raise UnicodeActivationError("committed Unicode result differs: total_encoded_bytes")
    if not isinstance(batch_count, int) or batch_count <= 0:
        raise UnicodeActivationError("committed Unicode result differs: batch_count")
    if (
        not isinstance(plan_count, int)
        or plan_count <= 0
        or plan_count > batch_count * 12 + 5
        or plan_count >= expected["total_frame_count"]
    ):
        raise UnicodeActivationError("committed Unicode result differs: plan_count")
    for field in (
        "tier0_artifact_digest",
        "reverse_artifact_digest",
        "plan_manifest_fingerprint",
    ):
        if result.get(field) != expected[field]:
            raise UnicodeActivationError(f"committed Unicode digest differs: {field}")
    for field in (
        "root_receipt",
        "producer_receipt",
        "staged_stream_receipt",
        "sink_artifacts_fingerprint",
        "postgresql_artifact_fingerprint",
        "perfcache_artifact_set_fingerprint",
        "perfcache_manifest_fingerprint",
        "perfcache_encoded_manifest_fingerprint",
        "admission_receipt",
        "plan_sequence_fingerprint",
    ):
        if HEX_256.fullmatch(str(result.get(field, ""))) is None:
            raise UnicodeActivationError(f"committed Unicode receipt is invalid: {field}")


def validate_readback(
    value: dict[str, Any], contract: dict[str, Any], identities: dict[str, Any]
) -> None:
    positions = contract["operation"]["readback_positions"]
    if (
        value.get("direct_positions") != positions
        or value.get("reverse_positions") != positions
        or value.get("direct_found") != [True] * len(positions)
        or value.get("reverse_found") != [True] * len(positions)
        or value.get("direct_epoch_id") != identities["activation_epoch_id"]
        or value.get("reverse_epoch_id") != identities["activation_epoch_id"]
        or value.get("direct_epoch_fingerprint")
        != identities["activation_epoch_fingerprint"]
        or value.get("reverse_epoch_fingerprint")
        != identities["activation_epoch_fingerprint"]
    ):
        raise UnicodeActivationError("cold public Unicode readback or reverse inversion differs")
    for field, pattern in (
        ("direct_entity_ids", HEX_128),
        ("direct_identity_preimage_fingerprints", HEX_256),
        ("direct_physicality_ids", HEX_256),
    ):
        values = value.get(field)
        if (
            not isinstance(values, list)
            or len(values) != len(positions)
            or any(pattern.fullmatch(str(item)) is None for item in values)
        ):
            raise UnicodeActivationError(f"cold public Unicode readback differs: {field}")


def artifact_observation(path: Path, expected_bytes: int) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink() or path.stat().st_size != expected_bytes:
        raise UnicodeActivationError(f"Unicode artifact is absent or has wrong size: {path}")
    return {"path": str(path), "bytes": expected_bytes, "sha256": sha256_file(path)}


def create_work_directories(
    paths: Sequence[Path], cluster_contract: dict[str, Any], root: Path
) -> None:
    for path in paths:
        if path.exists() or path.is_symlink():
            raise UnicodeActivationError(f"Unicode activation path already exists: {path}")
    for path in paths:
        path.mkdir(mode=0o700)
        if root == Path("/"):
            user = pwd.getpwnam(cluster_contract["instance"]["os_user"])
            os.chown(path, user.pw_uid, user.pw_gid)


def execute_unicode_activation(
    activation_contract: dict[str, Any],
    cluster_contract: dict[str, Any],
    source_contract: dict[str, Any],
    postgresql_contract: dict[str, Any],
    package: dict[str, Any],
    plan: dict[str, Any],
    cluster_receipt: dict[str, Any],
    source_root: Path,
    root: Path,
    authorize_system_root: bool,
    *,
    source_verifier: Callable[[dict[str, Any], Path], dict[str, Any]] = verify_source_bundle,
    identity_runner: Callable[[Path, Path, dict[str, Any]], tuple[dict[str, Any], dict[str, Any]]] = run_identity_provider,
    sql_runner: Callable[..., tuple[dict[str, Any], dict[str, Any]]] = run_psql,
    loaded_observer: Callable[[dict[str, Any], dict[str, Any], Path], dict[str, Any]] = clusterctl.observe_loaded_live,
    command_runner: Callable[[str, Sequence[str], int], dict[str, Any]] = clusterctl.execute_activation_command,
    readiness_runner: Callable[[str, Sequence[str], int], dict[str, Any]] = clusterctl.await_postgresql_ready,
) -> dict[str, Any]:
    clusterctl.require_fixture_or_root(root, authorize_system_root)
    validate_activation_contract(
        activation_contract, cluster_contract, source_contract, postgresql_contract
    )
    validate_product_boundary(cluster_contract, package, plan, cluster_receipt, root)
    source_evidence = source_verifier(source_contract, source_root)
    loaded_before = loaded_observer(plan, cluster_contract, root)
    clusterctl.verify_loaded(plan, cluster_contract, loaded_before)
    if loaded_before["system_identifier"] != cluster_receipt["system_identifier"]:
        raise UnicodeActivationError("live cluster identity differs from activation receipt")

    request = {
        "schema": REQUEST_SCHEMA,
        "orchestrator_path": activation_contract["orchestrator"][
            "repository_path"
        ],
        "orchestrator_sha256": sha256_file(Path(__file__).resolve()),
        "activation_contract_sha256": sha256_bytes(canonical_bytes(activation_contract)),
        "cluster_contract_sha256": sha256_bytes(canonical_bytes(cluster_contract)),
        "cluster_plan_sha256": plan["plan_sha256"],
        "cluster_activation_receipt_sha256": cluster_receipt[
            "activation_receipt_sha256"
        ],
        "cluster_system_identifier": loaded_before["system_identifier"],
        "package_id": package["package_id"],
        "package_manifest_sha256": plan["package_manifest_sha256"],
        "source_contract_sha256": sha256_bytes(canonical_bytes(source_contract)),
        "source_evidence_sha256": source_evidence["source_evidence_sha256"],
        "source_root": str(source_root),
        "unicode_postgresql_contract_sha256": sha256_bytes(
            canonical_bytes(postgresql_contract)
        ),
        "operation": activation_contract["operation"],
        "execution_context": activation_contract["execution_context"],
        "expected_result": activation_contract["expected_result"],
    }
    request_bytes = canonical_bytes(request)
    if len(request_bytes) > activation_contract["identity_provider"]["maximum_request_bytes"]:
        raise UnicodeActivationError("canonical activation request is too large")

    receipt_root = prefixed(root, cluster_contract["instance"]["receipt_directory"])
    staging = receipt_root / ".unicode-activation"
    staging.mkdir(parents=True, exist_ok=True, mode=0o750)
    request_sha = sha256_bytes(request_bytes)
    request_path = staging / f"request-{request_sha}.json"
    if request_path.exists() and request_path.read_bytes() != request_bytes:
        raise UnicodeActivationError("existing canonical activation request differs")
    if not request_path.exists():
        atomic_write(request_path, request_bytes)

    identity_relative = activation_contract["identity_provider"]["package_path"]
    identity_executable = prefixed(root, f"{package['root']}/{identity_relative}")
    identity_entry = next(
        (item for item in package["files"] if item.get("path") == identity_relative), None
    )
    if (
        not isinstance(identity_entry, dict)
        or identity_entry.get("kind") != "file"
        or identity_entry.get("sha256") != sha256_file(identity_executable)
    ):
        raise UnicodeActivationError("packaged activation identity bytes differ")
    identities, identity_command = identity_runner(
        identity_executable, request_path, activation_contract
    )
    validate_identities(identities, activation_contract)
    evidence_directory = (
        receipt_root
        / activation_contract["receipt"]["directory_name"]
        / identities["request_fingerprint"]
    )
    evidence_directory.mkdir(parents=True, exist_ok=True, mode=0o750)
    write_immutable(evidence_directory / "request.json", request)
    write_immutable(evidence_directory / "identities.json", identities)
    write_immutable(evidence_directory / "source-evidence.json", source_evidence)

    instance = cluster_contract["instance"]
    epoch = identities["activation_epoch_id"]
    spool_logical = f"{instance['temp_directory']}/{activation_contract['operation']['spool_directory_prefix']}{epoch}"
    generation_logical = f"{instance['perfcache_directory']}/{activation_contract['operation']['generation_directory_prefix']}{epoch}"
    tier0_logical = f"{generation_logical}/{activation_contract['operation']['tier0_filename']}"
    reverse_logical = f"{generation_logical}/{activation_contract['operation']['reverse_filename']}"
    spool_path = prefixed(root, spool_logical)
    generation_path = prefixed(root, generation_logical)
    tier0_path = prefixed(root, tier0_logical)
    reverse_path = prefixed(root, reverse_logical)

    command_receipts: list[dict[str, Any]] = [identity_command]
    inspection, inspection_receipt = sql_runner(
        plan,
        cluster_contract,
        render_inspection_sql(),
        "inspect-unicode-product-state",
        cluster_contract["security"]["admin_os_user"],
        instance["admin_role"],
        120,
    )
    command_receipts.append(inspection_receipt)
    mode = validate_inspection(inspection, activation_contract, identities)
    build_result: dict[str, Any]
    if mode == "fresh":
        create_work_directories((spool_path, generation_path), cluster_contract, root)
        sql = render_activation_sql(
            activation_contract,
            identities,
            str(source_root),
            spool_logical if root == Path("/") else str(spool_path),
            tier0_logical if root == Path("/") else str(tier0_path),
            reverse_logical if root == Path("/") else str(reverse_path),
        )
        build_result, build_receipt = sql_runner(
            plan,
            cluster_contract,
            sql,
            "activate-unicode-product-root",
            cluster_contract["security"]["admin_os_user"],
            instance["admin_role"],
            activation_contract["operation"]["statement_timeout_seconds"] + 120,
        )
        command_receipts.append(build_receipt)
        validate_build_result(build_result, activation_contract, identities)
    else:
        if not generation_path.is_dir() or generation_path.is_symlink():
            raise UnicodeActivationError("committed Unicode recovery lacks its generation directory")
        build_result = recover_build_result(
            inspection, activation_contract, identities
        )

    expected = activation_contract["expected_result"]
    artifacts_before = {
        "tier0": artifact_observation(tier0_path, expected["tier0_artifact_bytes"]),
        "reverse": artifact_observation(
            reverse_path, expected["reverse_artifact_bytes"]
        ),
    }
    restart = command_runner(
        "restart-after-unicode-activation",
        ["/usr/bin/systemctl", "restart", instance["service"]],
        activation_contract["operation"]["restart_timeout_seconds"],
    )
    command_receipts.append(restart)
    readiness = readiness_runner(
        "unicode-restart-readiness",
        plan["commands"]["probe_readiness"],
        120,
    )
    command_receipts.append(readiness)
    readback, readback_receipt = sql_runner(
        plan,
        cluster_contract,
        render_readback_sql(activation_contract, identities),
        "cold-public-unicode-readback",
        cluster_contract["security"]["app_os_user"],
        instance["app_role"],
        300,
    )
    command_receipts.append(readback_receipt)
    validate_readback(readback, activation_contract, identities)
    loaded_after = loaded_observer(plan, cluster_contract, root)
    clusterctl.verify_loaded(plan, cluster_contract, loaded_after)
    if (
        loaded_after["system_identifier"] != loaded_before["system_identifier"]
        or loaded_after.get("postmaster_pid") == loaded_before.get("postmaster_pid")
        or loaded_after["loaded_objects"] != loaded_before["loaded_objects"]
        or loaded_after["config_files"] != loaded_before["config_files"]
    ):
        raise UnicodeActivationError("product restart changed identity or retained the old postmaster")
    artifacts_after = {
        "tier0": artifact_observation(tier0_path, expected["tier0_artifact_bytes"]),
        "reverse": artifact_observation(
            reverse_path, expected["reverse_artifact_bytes"]
        ),
    }
    if artifacts_after != artifacts_before:
        raise UnicodeActivationError("Unicode artifacts changed across restart and readback")

    receipt: dict[str, Any] = {
        "schema": RECEIPT_SCHEMA,
        "phase": "product-activated",
        "request_sha256": request_sha,
        "request_fingerprint": identities["request_fingerprint"],
        "activation_epoch_id": identities["activation_epoch_id"],
        "activation_epoch_fingerprint": identities[
            "activation_epoch_fingerprint"
        ],
        "package_id": package["package_id"],
        "cluster_plan_sha256": plan["plan_sha256"],
        "cluster_activation_receipt_sha256": cluster_receipt[
            "activation_receipt_sha256"
        ],
        "system_identifier": loaded_after["system_identifier"],
        "source_evidence_sha256": source_evidence["source_evidence_sha256"],
        "mode": mode,
        "build_result": build_result,
        "artifacts": artifacts_after,
        "readback": readback,
        "loaded_before_observation_sha256": loaded_before["observation_sha256"],
        "loaded_after_observation_sha256": loaded_after["observation_sha256"],
        "restart_proven": True,
        "cold_public_readback_proven": True,
        "reverse_inversion_proven": True,
        "command_receipts": command_receipts,
    }
    receipt["receipt_sha256"] = document_identity(receipt, "receipt_sha256")
    write_immutable(
        evidence_directory / f"activation-{receipt['receipt_sha256']}.json", receipt
    )
    return receipt


def execute_unicode_activation_receipted(
    activation_contract: dict[str, Any],
    cluster_contract: dict[str, Any],
    source_contract: dict[str, Any],
    postgresql_contract: dict[str, Any],
    package: dict[str, Any],
    plan: dict[str, Any],
    cluster_receipt: dict[str, Any],
    source_root: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    """Execute the product boundary and durably receipt every failed disposition."""
    try:
        return execute_unicode_activation(
            activation_contract,
            cluster_contract,
            source_contract,
            postgresql_contract,
            package,
            plan,
            cluster_receipt,
            source_root,
            root,
            authorize_system_root,
        )
    except Exception as error:
        instance = cluster_contract.get("instance", {})
        receipt_directory = instance.get("receipt_directory")
        if isinstance(receipt_directory, str) and receipt_directory.startswith("/"):
            failure_directory = (
                prefixed(root, receipt_directory)
                / "unicode"
                / "failures"
            )
            context = {
                "activation_contract_sha256": sha256_bytes(
                    canonical_bytes(activation_contract)
                ),
                "cluster_contract_sha256": sha256_bytes(
                    canonical_bytes(cluster_contract)
                ),
                "source_contract_sha256": sha256_bytes(
                    canonical_bytes(source_contract)
                ),
                "unicode_postgresql_contract_sha256": sha256_bytes(
                    canonical_bytes(postgresql_contract)
                ),
                "package_id": package.get("package_id"),
                "cluster_plan_sha256": plan.get("plan_sha256"),
                "cluster_activation_receipt_sha256": cluster_receipt.get(
                    "activation_receipt_sha256"
                ),
                "source_root": str(source_root),
                "semantic_activation_state": "must-be-reinspected-before-retry",
            }
            write_failure(failure_directory, error, context)
        raise


def write_failure(
    evidence_directory: Path, error: BaseException, context: dict[str, Any]
) -> None:
    failure = {
        "schema": FAILURE_SCHEMA,
        "phase": "failed",
        "error_type": type(error).__name__,
        "error": str(error),
        "context": context,
        "success_receipt_issued": False,
    }
    failure["receipt_sha256"] = document_identity(failure, "receipt_sha256")
    write_immutable(
        evidence_directory / f"failure-{failure['receipt_sha256']}.json", failure
    )


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", default="contracts/unicode-product-activation.json")
    parser.add_argument("--cluster-contract", default="contracts/postgresql-cluster.json")
    parser.add_argument("--source-contract", default="contracts/unicode-source.json")
    parser.add_argument("--postgresql-contract", default="contracts/unicode-postgresql.json")
    parser.add_argument("--package-manifest", required=True)
    parser.add_argument("--cluster-plan", required=True)
    parser.add_argument("--cluster-activation-receipt", required=True)
    parser.add_argument("--source-root", default="/vault/Data/UCD/Public/UCD/latest")
    parser.add_argument("--output", default="-")
    parser.add_argument("--authorize-system-root", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    repository = Path(__file__).resolve().parents[2]

    def resolve(value: str) -> Path:
        path = Path(value)
        return path if path.is_absolute() else repository / path

    activation_contract = load_json(resolve(arguments.contract))
    cluster_contract = load_json(resolve(arguments.cluster_contract))
    source_contract = load_json(resolve(arguments.source_contract))
    postgresql_contract = load_json(resolve(arguments.postgresql_contract))
    package = load_json(Path(arguments.package_manifest))
    plan = load_json(Path(arguments.cluster_plan))
    cluster_receipt = load_json(Path(arguments.cluster_activation_receipt))
    result = execute_unicode_activation_receipted(
        activation_contract,
        cluster_contract,
        source_contract,
        postgresql_contract,
        package,
        plan,
        cluster_receipt,
        Path(arguments.source_root),
        Path("/"),
        arguments.authorize_system_root,
    )
    content = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(content)
    else:
        atomic_write(Path(arguments.output), content.encode("utf-8"))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except UnicodeActivationError as error:
        print(f"Unicode product activation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
