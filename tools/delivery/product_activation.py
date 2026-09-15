#!/usr/bin/python3
"""Create and execute authenticated whole-product activation requests.

The unprivileged side is a CI/CD request compiler.  The privileged side is installed
as immutable root-owned code and accepts only a bounded authenticated request on
stdin.  It never executes code or paths supplied by a repository checkout.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import hmac
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
from typing import Any, Mapping, Sequence


CONTRACT_SCHEMA = "laplace.product-activation-gateway-contract/v1"
REQUEST_SCHEMA = "laplace.product-activation-request/v1"
RESULT_SCHEMA = "laplace.product-activation-gateway-result/v1"
HEX_64 = re.compile(r"[0-9a-f]{64}\Z")
HEX_40 = re.compile(r"[0-9a-f]{40}\Z")


class ActivationGatewayError(RuntimeError):
    """The request or its execution violated the delivery contract."""


def reject_duplicate_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ActivationGatewayError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def parse_json(content: bytes) -> dict[str, Any]:
    try:
        value = json.loads(content, object_pairs_hook=reject_duplicate_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ActivationGatewayError(f"invalid JSON: {error}") from error
    if not isinstance(value, dict):
        raise ActivationGatewayError("JSON document must be an object")
    return value


def load_json(path: Path) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise ActivationGatewayError(f"required JSON file is absent or not physical: {path}")
    return parse_json(path.read_bytes())


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def document_identity(document: Mapping[str, Any], field: str) -> str:
    return sha256_bytes(canonical_bytes({key: value for key, value in document.items() if key != field}))


def resource_observation_identity(document: Mapping[str, Any]) -> str:
    payload = {
        key: value for key, value in document.items() if key != "observation_sha256"
    }
    return sha256_bytes(canonical_bytes(payload) + b"\n")


def atomic_write(path: Path, content: bytes, mode: int = 0o640) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def require_hex(value: Any, pattern: re.Pattern[str], label: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise ActivationGatewayError(f"{label} is not an exact lowercase hexadecimal identity")
    return value


def require_absolute(value: Any, label: str) -> Path:
    if not isinstance(value, str):
        raise ActivationGatewayError(f"{label} must be an absolute path")
    path = Path(value)
    if not path.is_absolute() or ".." in PurePosixPath(value).parts:
        raise ActivationGatewayError(f"{label} must be an absolute non-traversing path")
    return path


def require_below(path: Path, root: Path, label: str) -> None:
    try:
        path.relative_to(root)
    except ValueError as error:
        raise ActivationGatewayError(f"{label} escapes {root}") from error


def validate_contract(contract: dict[str, Any]) -> None:
    if contract.get("schema") != CONTRACT_SCHEMA or contract.get("version") != "1.0.0":
        raise ActivationGatewayError("activation gateway contract schema or version differs")
    repository = contract.get("repository")
    request = contract.get("request")
    gateway = contract.get("gateway")
    product = contract.get("product")
    operation = contract.get("operation")
    trusted = contract.get("trusted_bundle")
    local_administrator = contract.get("local_administrator")
    if not all(isinstance(value, dict) for value in (
        repository, request, gateway, product, operation, trusted,
        local_administrator,
    )):
        raise ActivationGatewayError("activation gateway contract sections are incomplete")
    if repository.get("runner_user") != repository.get("runner_group"):
        raise ActivationGatewayError("runner user and group must share one service identity")
    if request.get("schema") != REQUEST_SCHEMA or request.get("result_schema") != RESULT_SCHEMA:
        raise ActivationGatewayError("request/result schema declaration differs")
    if request.get("hmac_algorithm") != "HMAC-SHA-256" or request.get("hmac_domain") != REQUEST_SCHEMA:
        raise ActivationGatewayError("activation request authentication law differs")
    if local_administrator != {
        "enabled": True,
        "actor": "local-system-administrator",
        "workflow_run_id": 0,
        "workflow_run_attempt": 0,
    }:
        raise ActivationGatewayError("local administrator activation route differs")
    maximum = request.get("maximum_bytes")
    age = request.get("maximum_age_seconds")
    if not isinstance(maximum, int) or maximum < 4096 or maximum > 1024 * 1024:
        raise ActivationGatewayError("request maximum byte count is invalid")
    if not isinstance(age, int) or age < 60 or age > 86400:
        raise ActivationGatewayError("request maximum age is invalid")
    for section, names in (
        (gateway, ("release_root", "active_link", "executable", "sudoers_path", "receipt_root", "python")),
        (product, ("package_manifest_root", "package_stage_root", "package_release_root", "plan_receipt_root", "cluster_activation_root", "unicode_source_root", "unicode_result", "highway_result")),
    ):
        for name in names:
            require_absolute(section.get(name), f"{name}")
    if (
        product.get("package_receipt_schema")
        != "laplace.product-package-receipt/v1"
        or product.get("package_installation_schema")
        != "laplace.product-package-installation-receipt/v1"
    ):
        raise ActivationGatewayError("product package receipt schemas differ")
    files = trusted.get("files")
    if not isinstance(files, list) or not files or files != sorted(set(files)):
        raise ActivationGatewayError("trusted bundle file list must be sorted and unique")
    for relative in files:
        if not isinstance(relative, str) or PurePosixPath(relative).is_absolute() or ".." in PurePosixPath(relative).parts:
            raise ActivationGatewayError("trusted bundle contains an unsafe path")
    if operation.get("name") != "activate-product-unicode-and-highway" or operation.get("system_root_authorization") is not True:
        raise ActivationGatewayError("whole-product activation operation differs")


def require_exact_keys(value: Any, expected: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise ActivationGatewayError(f"{label} fields differ from the contract")
    return value


def decode_key(value: str) -> bytes:
    try:
        key = base64.b64decode(value, validate=True)
    except (ValueError, base64.binascii.Error) as error:
        raise ActivationGatewayError("activation HMAC key is not canonical base64") from error
    if len(key) != 32:
        raise ActivationGatewayError("activation HMAC key must contain exactly 32 bytes")
    return key


def request_mac(contract: dict[str, Any], payload: dict[str, Any], key: bytes) -> str:
    domain = contract["request"]["hmac_domain"].encode("utf-8")
    return hmac.new(key, domain + b"\0" + canonical_bytes(payload), hashlib.sha256).hexdigest()


def parse_utc(value: Any) -> dt.datetime:
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ActivationGatewayError("request creation time must be canonical UTC")
    try:
        parsed = dt.datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as error:
        raise ActivationGatewayError("request creation time is invalid") from error
    if parsed.microsecond != 0:
        raise ActivationGatewayError("request creation time must have whole-second precision")
    return parsed


def validate_payload_paths(
    contract: dict[str, Any], payload: dict[str, Any]
) -> tuple[Path, Path, Path, Path, Path]:
    package = require_exact_keys(
        payload.get("package"),
        {
            "id",
            "manifest",
            "manifest_sha256",
            "receipt",
            "receipt_sha256",
            "source_root",
        },
        "activation request package",
    )
    resource = payload.get("resource_observation")
    unicode = payload.get("unicode")
    if not all(isinstance(value, dict) for value in (resource, unicode)):
        raise ActivationGatewayError("activation request input sections are incomplete")
    package_id = require_hex(package.get("id"), HEX_64, "package id")
    manifest = require_absolute(package.get("manifest"), "package manifest")
    receipt = require_absolute(package.get("receipt"), "package receipt")
    package_source_root = require_absolute(package.get("source_root"), "package source root")
    resource_path = require_absolute(resource.get("path"), "resource observation")
    unicode_source_root = require_absolute(unicode.get("source_root"), "Unicode source root")
    manifest_root = Path(contract["product"]["package_manifest_root"])
    require_below(manifest, manifest_root, "package manifest")
    if manifest.name != "package-manifest.json" or len(manifest.relative_to(manifest_root).parts) != 2:
        raise ActivationGatewayError("package manifest is not in one content-addressed build directory")
    build_plan_id = require_hex(manifest.parent.name, HEX_64, "product build plan id")
    if receipt != manifest.parent / "package-receipt.json":
        raise ActivationGatewayError("package receipt is not paired with its manifest")
    expected_source_root = Path(contract["product"]["package_stage_root"]) / build_plan_id / "root"
    if package_source_root != expected_source_root:
        raise ActivationGatewayError("package source root differs from its addressed build plan")
    expected_resource = Path(contract["product"]["plan_receipt_root"]) / package_id / "resource-observation.json"
    if resource_path != expected_resource:
        raise ActivationGatewayError("resource observation is not package-addressed")
    if unicode_source_root != Path(contract["product"]["unicode_source_root"]):
        raise ActivationGatewayError("Unicode source root differs from the product contract")
    return manifest, receipt, expected_source_root, resource_path, unicode_source_root


def validate_physical_inputs(contract: dict[str, Any], payload: dict[str, Any]) -> None:
    manifest_path, receipt_path, package_source_root, resource_path, _unicode_source_root = validate_payload_paths(contract, payload)
    package = payload["package"]
    resource = payload["resource_observation"]
    manifest_digest = require_hex(package.get("manifest_sha256"), HEX_64, "package manifest digest")
    receipt_digest = require_hex(package.get("receipt_sha256"), HEX_64, "package receipt digest")
    resource_digest = require_hex(resource.get("sha256"), HEX_64, "resource observation digest")
    if sha256_file(manifest_path) != manifest_digest:
        raise ActivationGatewayError("package manifest bytes differ from the signed request")
    if sha256_file(receipt_path) != receipt_digest:
        raise ActivationGatewayError("package receipt bytes differ from the signed request")
    if sha256_file(resource_path) != resource_digest:
        raise ActivationGatewayError("resource observation bytes differ from the signed request")
    manifest = load_json(manifest_path)
    if manifest.get("package_id") != package["id"]:
        raise ActivationGatewayError("package manifest identity differs from the signed request")
    expected_root = f"{contract['product']['package_release_root']}/{package['id']}"
    if manifest.get("root") != expected_root:
        raise ActivationGatewayError("package manifest root differs from the product release root")
    receipt = load_json(receipt_path)
    expected_physical_root = package_source_root.joinpath(*PurePosixPath(expected_root).parts[1:])
    if (
        receipt.get("schema") != contract["product"]["package_receipt_schema"]
        or receipt.get("package_id") != package["id"]
        or receipt.get("manifest") != str(manifest_path)
        or receipt.get("manifest_sha256") != manifest_digest
        or receipt.get("physical_root") != str(expected_physical_root)
        or receipt.get("activation_eligible") is not True
        or receipt.get("build_input_closure_complete") is not True
        or receipt.get("product_activated") is not False
    ):
        raise ActivationGatewayError("product package receipt is not exact and activation eligible")
    resource_document = load_json(resource_path)
    if (
        resource_document.get("schema") != "laplace.execution-resource-observation/v2"
        or resource_document.get("package_id") != package["id"]
        or resource_document.get("package_manifest_sha256") != manifest_digest
        or resource_document.get("observation_sha256")
        != resource_observation_identity(resource_document)
        or resource_document.get("observation_sha256")
        != resource.get("observation_sha256")
    ):
        raise ActivationGatewayError(
            "resource observation is not bound to the signed product package"
        )


def delivery_selection_from_receipts(
    contract: dict[str, Any], package_receipt_path: Path, resource_path: Path
) -> tuple[dict[str, Any], dict[str, Any]]:
    receipt = load_json(package_receipt_path)
    manifest_path = require_absolute(receipt.get("manifest"), "product receipt manifest")
    manifest = load_json(manifest_path)
    build_plan_id = require_hex(manifest_path.parent.name, HEX_64, "product build plan id")
    package_id = require_hex(receipt.get("package_id"), HEX_64, "product receipt package id")
    package_source_root = Path(contract["product"]["package_stage_root"]) / build_plan_id / "root"
    resource = load_json(resource_path)
    selection = {
        "id": package_id,
        "manifest": str(manifest_path),
        "manifest_sha256": sha256_file(manifest_path),
        "receipt": str(package_receipt_path),
        "receipt_sha256": sha256_file(package_receipt_path),
        "source_root": str(package_source_root),
    }
    observation = {
        "path": str(resource_path),
        "sha256": sha256_file(resource_path),
        "observation_sha256": resource.get("observation_sha256"),
    }
    probe = {
        "package": selection,
        "resource_observation": observation,
        "unicode": {"source_root": contract["product"]["unicode_source_root"]},
    }
    validate_physical_inputs(contract, probe)
    return selection, observation


def build_request(
    contract: dict[str, Any],
    continuation: dict[str, Any],
    environment: Mapping[str, str],
    key: bytes,
    now: dt.datetime,
    package_receipt_path: Path | None = None,
    resource_observation_path: Path | None = None,
) -> dict[str, Any]:
    validate_contract(contract)
    repository = contract["repository"]
    if environment.get("GITHUB_REPOSITORY") != repository["slug"]:
        raise ActivationGatewayError("workflow repository differs from deployment authority")
    if environment.get("GITHUB_REF") != repository["deployment_ref"]:
        raise ActivationGatewayError("product activation may execute only from the deployment ref")
    if environment.get("GITHUB_EVENT_NAME") != repository["deployment_event"]:
        raise ActivationGatewayError("product activation requires the declared deployment event")
    commit = require_hex(environment.get("GITHUB_SHA"), HEX_40, "workflow commit")
    run_id = environment.get("GITHUB_RUN_ID", "")
    run_attempt = environment.get("GITHUB_RUN_ATTEMPT", "")
    if not run_id.isdecimal() or int(run_id) <= 0 or not run_attempt.isdecimal() or int(run_attempt) <= 0:
        raise ActivationGatewayError("workflow run identity is invalid")
    if (package_receipt_path is None) != (resource_observation_path is None):
        raise ActivationGatewayError("product receipt and resource observation must be selected together")
    if package_receipt_path is not None and resource_observation_path is not None:
        selected_package, selected_resource = delivery_selection_from_receipts(
            contract, package_receipt_path, resource_observation_path
        )
    else:
        successor = continuation.get("product_activation_projection", {}).get(
            "successor_product_package"
        )
        if not isinstance(successor, dict) or successor.get("activation_eligible") is not True:
            raise ActivationGatewayError("continuation does not select an activation-eligible successor package")
        if successor.get("immutable_release_installed") is not True:
            raise ActivationGatewayError("successor package is not recorded as immutably installed")
        installation = successor.get("installation", {})
        if installation.get("installed_package_verified") is not True:
            raise ActivationGatewayError("successor installed package is not verified")
        physical_release = require_absolute(successor.get("physical_root"), "successor package physical root")
        manifest_path = require_absolute(successor.get("package_manifest"), "successor package manifest")
        build_plan_id = require_hex(manifest_path.parent.name, HEX_64, "successor product build plan id")
        source_root = Path(contract["product"]["package_stage_root"]) / build_plan_id / "root"
        expected_release = source_root.joinpath(*PurePosixPath(f"{contract['product']['package_release_root']}/{successor.get('package_id')}").parts[1:])
        if physical_release != expected_release:
            raise ActivationGatewayError("successor package physical root differs from its build plan")
        resource = successor.get("resource_observation", {})
        selected_package = {
            "id": successor.get("package_id"),
            "manifest": str(manifest_path),
            "manifest_sha256": successor.get("package_manifest_sha256"),
            "receipt": successor.get("package_receipt"),
            "receipt_sha256": successor.get("package_receipt_sha256"),
            "source_root": str(source_root),
        }
        selected_resource = {
            "path": resource.get("receipt"),
            "sha256": resource.get("receipt_sha256"),
            "observation_sha256": resource.get("observation_sha256"),
        }
    payload = {
        "schema": REQUEST_SCHEMA,
        "operation": contract["operation"]["name"],
        "created_utc": now.astimezone(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "repository": {
            "slug": repository["slug"],
            "ref": repository["deployment_ref"],
            "commit": commit,
            "workflow_run_id": int(run_id),
            "workflow_run_attempt": int(run_attempt),
            "actor": environment.get("GITHUB_ACTOR", ""),
        },
        "contract_sha256": sha256_bytes(canonical_bytes(contract)),
        "package": selected_package,
        "resource_observation": selected_resource,
        "unicode": {"source_root": contract["product"]["unicode_source_root"]},
    }
    if not payload["repository"]["actor"]:
        raise ActivationGatewayError("workflow actor is absent")
    validate_physical_inputs(contract, payload)
    request = {"schema": REQUEST_SCHEMA, "payload": payload}
    request["request_id"] = sha256_bytes(canonical_bytes(payload))
    request["hmac_sha256"] = request_mac(contract, payload, key)
    return request


def build_local_administrator_request(
    contract: dict[str, Any],
    package_receipt_path: Path,
    resource_observation_path: Path,
    key: bytes,
    now: dt.datetime,
) -> dict[str, Any]:
    """Bind a root-local activation to the same exact package inputs as CI."""
    validate_contract(contract)
    selected_package, selected_resource = delivery_selection_from_receipts(
        contract, package_receipt_path, resource_observation_path
    )
    manifest = load_json(Path(selected_package["manifest"]))
    provenance = manifest.get("provenance")
    commit = provenance.get("repository_commit") if isinstance(provenance, dict) else None
    require_hex(commit, HEX_40, "package repository commit")
    local = contract["local_administrator"]
    payload = {
        "schema": REQUEST_SCHEMA,
        "operation": contract["operation"]["name"],
        "created_utc": now.astimezone(dt.timezone.utc).replace(
            microsecond=0
        ).isoformat().replace("+00:00", "Z"),
        "repository": {
            "slug": contract["repository"]["slug"],
            "ref": contract["repository"]["deployment_ref"],
            "commit": commit,
            "workflow_run_id": local["workflow_run_id"],
            "workflow_run_attempt": local["workflow_run_attempt"],
            "actor": local["actor"],
        },
        "contract_sha256": sha256_bytes(canonical_bytes(contract)),
        "package": selected_package,
        "resource_observation": selected_resource,
        "unicode": {"source_root": contract["product"]["unicode_source_root"]},
    }
    validate_physical_inputs(contract, payload)
    request = {"schema": REQUEST_SCHEMA, "payload": payload}
    request["request_id"] = sha256_bytes(canonical_bytes(payload))
    request["hmac_sha256"] = request_mac(contract, payload, key)
    return request


def validate_request(
    contract: dict[str, Any], request: dict[str, Any], key: bytes, now: dt.datetime
) -> dict[str, Any]:
    validate_contract(contract)
    if set(request) != {"schema", "payload", "request_id", "hmac_sha256"} or request.get("schema") != REQUEST_SCHEMA:
        raise ActivationGatewayError("activation request envelope differs")
    payload = require_exact_keys(
        request.get("payload"),
        {
            "schema", "operation", "created_utc", "repository",
            "contract_sha256", "package", "resource_observation", "unicode",
        },
        "activation request payload",
    )
    if payload.get("schema") != REQUEST_SCHEMA:
        raise ActivationGatewayError("activation request payload differs")
    expected_id = sha256_bytes(canonical_bytes(payload))
    require_hex(request.get("request_id"), HEX_64, "request id")
    require_hex(request.get("hmac_sha256"), HEX_64, "request HMAC")
    if request["request_id"] != expected_id:
        raise ActivationGatewayError("activation request identity differs")
    expected_mac = request_mac(contract, payload, key)
    if not hmac.compare_digest(request["hmac_sha256"], expected_mac):
        raise ActivationGatewayError("activation request authentication failed")
    if payload.get("contract_sha256") != sha256_bytes(canonical_bytes(contract)):
        raise ActivationGatewayError("activation request uses another gateway contract")
    if payload.get("operation") != contract["operation"]["name"]:
        raise ActivationGatewayError("activation request operation differs")
    repository = require_exact_keys(
        payload.get("repository"),
        {"slug", "ref", "commit", "workflow_run_id", "workflow_run_attempt", "actor"},
        "activation request repository authority",
    )
    require_exact_keys(
        payload.get("package"),
        {
            "id",
            "manifest",
            "manifest_sha256",
            "receipt",
            "receipt_sha256",
            "source_root",
        },
        "activation request package",
    )
    require_exact_keys(
        payload.get("resource_observation"),
        {"path", "sha256", "observation_sha256"},
        "activation request resource observation",
    )
    require_exact_keys(
        payload.get("unicode"), {"source_root"}, "activation request Unicode input"
    )
    authority = contract["repository"]
    if repository.get("slug") != authority["slug"] or repository.get("ref") != authority["deployment_ref"]:
        raise ActivationGatewayError("activation request repository authority differs")
    require_hex(repository.get("commit"), HEX_40, "request commit")
    local = contract["local_administrator"]
    if repository.get("actor") == local["actor"]:
        if (
            repository.get("workflow_run_id") != local["workflow_run_id"]
            or repository.get("workflow_run_attempt")
            != local["workflow_run_attempt"]
        ):
            raise ActivationGatewayError("local administrator request identity differs")
        manifest = load_json(Path(payload["package"]["manifest"]))
        provenance = manifest.get("provenance")
        if (
            not isinstance(provenance, dict)
            or provenance.get("repository_commit") != repository.get("commit")
        ):
            raise ActivationGatewayError(
                "local administrator request commit differs from the package"
            )
    elif (
        not isinstance(repository.get("workflow_run_id"), int)
        or repository["workflow_run_id"] <= 0
        or not isinstance(repository.get("workflow_run_attempt"), int)
        or repository["workflow_run_attempt"] <= 0
    ):
        raise ActivationGatewayError("workflow request identity is invalid")
    created = parse_utc(payload.get("created_utc"))
    current = now.astimezone(dt.timezone.utc)
    age = (current - created).total_seconds()
    if age < -60 or age > contract["request"]["maximum_age_seconds"]:
        raise ActivationGatewayError("activation request is outside its accepted time window")
    validate_physical_inputs(contract, payload)
    return payload


def command_receipt(label: str, command: Sequence[str], completed: subprocess.CompletedProcess[str]) -> dict[str, Any]:
    return {
        "label": label,
        "argv": list(command),
        "exit_code": completed.returncode,
        "stdout_sha256": sha256_bytes(completed.stdout.encode("utf-8")),
        "stderr_sha256": sha256_bytes(completed.stderr.encode("utf-8")),
    }


def run_fixed(label: str, command: Sequence[str], timeout: int) -> dict[str, Any]:
    completed = subprocess.run(
        list(command), check=False, cwd="/", env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C", "LC_ALL": "C"},
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout,
    )
    receipt = command_receipt(label, command, completed)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or f"exit {completed.returncode}"
        raise ActivationGatewayError(f"{label} failed: {detail}")
    return receipt


def validate_cluster_success(contract: dict[str, Any], result: dict[str, Any], package_id: str) -> Path:
    operation = contract["operation"]
    if (
        result.get("schema") != operation["cluster_success_schema"]
        or result.get("phase") != operation["cluster_success_phase"]
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("boot_enabled") is not True
        or result.get("active_target") != f"releases/{package_id}"
        or result.get("activation_receipt_sha256") != document_identity(result, "activation_receipt_sha256")
    ):
        raise ActivationGatewayError("cluster activation result is not exact and complete")
    plan = require_absolute(result.get("cluster_plan_path"), "cluster plan path")
    evidence_root = Path(contract["product"]["cluster_activation_root"]) / package_id
    require_below(plan, evidence_root, "cluster plan")
    if not plan.is_file() or plan.is_symlink():
        raise ActivationGatewayError("cluster plan evidence is absent")
    return plan


def validate_unicode_success(contract: dict[str, Any], result: dict[str, Any], package_id: str) -> None:
    if (
        result.get("schema") != contract["operation"]["unicode_success_schema"]
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_public_readback_proven") is not True
        or result.get("reverse_inversion_proven") is not True
        or result.get("receipt_sha256") != document_identity(result, "receipt_sha256")
    ):
        raise ActivationGatewayError("Unicode activation result is not exact and complete")


HIGHWAY_REVALIDATION_SCHEMA = "laplace.highway-committed-revalidation-receipt/v1"
HIGHWAY_REVALIDATION_CONTRACT_SHA256 = "566926c4945029186a04bb2c5bd5cc375520cfa5f86118893d62c9ece3b8f794"
HIGHWAY_REVALIDATION_PHASE = "product-unicode-activated-and-highway-revalidated"


def delivery_contract_path(relative: str) -> Path:
    """Resolve the same contract in a source checkout or immutable gateway bundle."""
    here = Path(__file__).resolve()
    root = here.parents[1] if here.name == "product_activation_impl.py" else here.parents[2]
    return root / relative


def producer_identity(document: Mapping[str, Any], field: str) -> str:
    return sha256_bytes(canonical_bytes({k: v for k, v in document.items() if k != field}) + b"\n")


def highway_revalidation_contract(gateway: dict[str, Any]) -> dict[str, Any]:
    policy = gateway.get("operation", {}).get("highway_revalidation")
    expected = {
        "contract_path": "contracts/highway-committed-revalidation.json",
        "contract_sha256": HIGHWAY_REVALIDATION_CONTRACT_SHA256,
        "receipt_schema": HIGHWAY_REVALIDATION_SCHEMA,
        "phase": "committed-state-revalidated",
    }
    if policy != expected:
        raise ActivationGatewayError("committed Highway revalidation was not explicitly selected")
    contract = load_json(delivery_contract_path(policy["contract_path"]))
    if sha256_bytes(canonical_bytes(contract) + b"\n") != policy["contract_sha256"]:
        raise ActivationGatewayError("committed Highway revalidation contract identity differs")
    return contract


def validate_highway_revalidation(gateway: dict[str, Any], result: dict[str, Any], package_id: str) -> None:
    """Validate a new native proof without promoting it to a historical activation."""
    contract = highway_revalidation_contract(gateway)
    activation_law = load_json(delivery_contract_path("contracts/highway-product-activation.json"))
    selected = contract["selection"]
    if (result.get("schema") != HIGHWAY_REVALIDATION_SCHEMA
            or result.get("phase") != contract["receipt"]["phase"]
            or result.get("mode") != selected["mode"]
            or result.get("activation_performed") is not False
            or result.get("historical_request_present") is not False
            or "activation" in result
            or result.get("restart_proven") is not True
            or result.get("cold_application_readback_proven") is not True
            or result.get("package_id") != package_id
            or result.get("system_identifier") != selected["system_identifier"]
            or result.get("committed_revalidation_contract_sha256") != HIGHWAY_REVALIDATION_CONTRACT_SHA256
            or result.get("historical_reference") != contract["historical_reference"]
            or result.get("receipt_sha256") != producer_identity(result, "receipt_sha256")):
        raise ActivationGatewayError("committed Highway revalidation receipt is incomplete or differs")
    request = result.get("revalidation_request")
    if not isinstance(request, dict) or sha256_bytes(canonical_bytes(request) + b"\n") != result.get("request_sha256"):
        raise ActivationGatewayError("committed Highway revalidation request identity differs")
    for field in ("package_id", "cluster_plan_sha256", "cluster_activation_receipt_sha256", "unicode_activation_receipt_sha256"):
        require_hex(result.get(field), HEX_64, field)
    for field in ("package_id", "cluster_plan_sha256", "cluster_activation_receipt_sha256", "unicode_activation_receipt_sha256", "system_identifier", "mode", "committed_revalidation_contract_sha256", "historical_reference"):
        if request.get(field) != result[field]:
            raise ActivationGatewayError(f"committed Highway request differs: {field}")
    if (request.get("schema") != contract["receipt"]["request_schema"]
            or request.get("activation_performed") is not False
            or request.get("historical_request_present") is not False
            or request.get("native_function") != contract["native"]["function"]
            or request.get("native_result_type") != contract["native"]["result_type"]
            or request.get("loaded_before_observation_sha256") != result.get("loaded_before_observation_sha256")
            or type(request.get("observed_postmaster_pid")) is not int
            or request["observed_postmaster_pid"] <= 0
            or request.get("activation_contract_sha256") != sha256_bytes(canonical_bytes(activation_law) + b"\n")
            or any(request.get(k) != activation_law[k] for k in ("operation", "execution_context", "expected_result"))):
        raise ActivationGatewayError("committed Highway native input contract differs")
    for field in ("orchestrator_sha256", "activation_orchestrator_sha256", "registry_contract_sha256", "predecessor_registry_contract_sha256", "unicode_request_fingerprint", "authority_fingerprint", "native_sql_sha256"):
        require_hex(request.get(field), HEX_64, field)
    if (request["registry_contract_sha256"] != activation_law["authority"]["highway_registry_contract_sha256"]
            or request["predecessor_registry_contract_sha256"] != activation_law["authority"]["predecessor_contract_sha256"]):
        raise ActivationGatewayError("committed Highway registry input contract differs")
    epochs = request.get("context_epochs")
    epoch_names = {"source_epoch", "identity_epoch", "geometry_epoch", "evidence_epoch", "firmware_epoch", "dependency_epoch", "database_epoch", "package_epoch", "perfcache_epoch", "numeric_epoch"}
    require_exact_keys(epochs, epoch_names, "committed Highway context epochs")
    for name, value in epochs.items():
        require_hex(value, HEX_64, name)
    inspection = request.get("inspected_state")
    retention = request.get("retention_observation")
    if (not isinstance(inspection, dict)
            or inspection.get("highway_epoch_id") != selected["registry_epoch_id"]
            or inspection.get("highway_epoch_fingerprint") != selected["registry_epoch_fingerprint"]
            or inspection.get("highway_sequence") != selected["activation_sequence"]
            or not isinstance(retention, dict)
            or retention.get("historical_request_present") is not False
            or retention.get("observed_entries") not in ([], ["failures"])):
        raise ActivationGatewayError("committed Highway prior state or absent retention differs")
    proof = result.get("revalidation")
    readback = result.get("readback")
    if not isinstance(proof, dict) or not isinstance(readback, dict):
        raise ActivationGatewayError("committed Highway native proof or cold readback is absent")
    if (not isinstance(result.get("cold_revalidation"), dict)
            or canonical_bytes(result["cold_revalidation"]) != canonical_bytes(proof)):
        raise ActivationGatewayError("committed Highway native proof changed or is absent after restart")
    fields128 = ("root_entity_id", "registry_epoch_id", "unicode_activation_epoch_id")
    fields256 = ("verification_receipt", "root_physicality_id", "registry_fingerprint", "registry_epoch_fingerprint", "stored_isa_receipt", "current_isa_receipt", "current_context_fingerprint", "stored_admission_receipt", "stored_activation_receipt", "stored_activation_fingerprint", "stored_generation_fingerprint", "event_chain_fingerprint", "current_working_set_receipt", "current_presence_semantic_receipt", "current_presence_execution_receipt", "current_producer_receipt", "current_staged_stream_receipt", "current_sink_artifacts_fingerprint", "unicode_root_receipt", "unicode_activation_epoch_fingerprint", "stored_working_set_receipt", "stored_producer_receipt")
    require_exact_keys(proof, set(fields128 + fields256) | {"registry_version", "activation_sequence", "kind_count", "alias_count", "disposition_count", "canonical_entity_count", "canonical_physicality_count", "transient_occurrence_count", "historical_composition_receipt_present", "historical_intermediate_receipts_verified", "activation_performed", "status", "retained_expected_epoch_count", "recovered_expected_epoch_count"}, "committed Highway native result")
    if (type(proof["historical_composition_receipt_present"]) is not bool
            or proof["historical_intermediate_receipts_verified"] is not False):
        raise ActivationGatewayError("committed Highway historical coverage differs")
    validate_highway_expected_epoch_coverage(proof)
    for fields, length in ((fields128, 32), (fields256, 64)):
        for name in fields:
            value = require_hex(proof.get(name), re.compile(r"[0-9a-f]{" + str(length) + r"}\Z"), name)
            if set(value) == {"0"}:
                raise ActivationGatewayError(f"committed Highway native proof is empty: {name}")
    expected = activation_law["expected_result"]
    for name in ("registry_version", "registry_fingerprint", "kind_count", "alias_count", "disposition_count", "status"):
        if proof.get(name) != expected[name] or type(proof[name]) is not type(expected[name]):
            raise ActivationGatewayError(f"committed Highway native registry differs: {name}")
    if (proof.get("activation_performed") is not False
            or proof["registry_epoch_id"] != selected["registry_epoch_id"]
            or proof["registry_epoch_fingerprint"] != selected["registry_epoch_fingerprint"]
            or type(proof.get("activation_sequence")) is not int
            or proof["activation_sequence"] != selected["activation_sequence"]
            or epochs["numeric_epoch"] != proof["registry_epoch_fingerprint"]
            or epochs["perfcache_epoch"] != proof["unicode_activation_epoch_fingerprint"]):
        raise ActivationGatewayError("committed Highway native identity differs from selected input")
    if (inspection.get("unicode_present") is not True
            or inspection.get("highway_present") is not True
            or inspection.get("unicode_epoch_id") != proof["unicode_activation_epoch_id"]
            or inspection.get("unicode_epoch_fingerprint") != proof["unicode_activation_epoch_fingerprint"]):
        raise ActivationGatewayError("committed Highway inspected Unicode identity differs")
    for name in ("registry_version", "registry_fingerprint", "kind_count", "alias_count", "disposition_count"):
        if inspection.get("active_" + name) != expected[name]:
            raise ActivationGatewayError("committed Highway inspected registry differs")
    for prefix in ("kind", "alias", "disposition"):
        if inspection.get("active_" + prefix + "_projection_count") != expected[prefix + "_count"]:
            raise ActivationGatewayError("committed Highway inspected projections differ")
    if (type(inspection.get("generation_count")) is not int or inspection["generation_count"] < 1
            or type(inspection.get("event_count")) is not int or inspection["event_count"] != proof["activation_sequence"]):
        raise ActivationGatewayError("committed Highway inspected event history differs")
    for name in ("canonical_entity_count", "canonical_physicality_count", "transient_occurrence_count"):
        if type(proof.get(name)) is not int or proof[name] < (0 if name == "transient_occurrence_count" else 1):
            raise ActivationGatewayError("committed Highway native reconstruction counts are invalid")
    for name in ("registry_version", "registry_fingerprint", "registry_epoch_id", "registry_epoch_fingerprint", "root_entity_id", "root_physicality_id", "activation_sequence", "unicode_activation_epoch_id", "unicode_activation_epoch_fingerprint", "status"):
        if readback.get(name) != proof[name] or type(readback[name]) is not type(proof[name]):
            raise ActivationGatewayError(f"committed Highway cold readback differs: {name}")
    for read_name, proof_name in (("activation_receipt", "stored_activation_receipt"), ("activation_fingerprint", "stored_activation_fingerprint")):
        if readback.get(read_name) != proof[proof_name]:
            raise ActivationGatewayError(f"committed Highway stored cold readback differs: {read_name}")
    read_isa = require_hex(readback.get("isa_receipt"), HEX_64, "cold read ISA receipt")
    if set(read_isa) == {"0"}:
        raise ActivationGatewayError("committed Highway cold read ISA receipt is empty")
    for prefix, count in (("kind", expected["kind_count"]), ("alias_kind", expected["alias_count"]), ("disposition", expected["disposition_count"])):
        ids = readback.get(prefix + "_ids")
        names = readback.get(("alias" if prefix == "alias_kind" else prefix) + "_name_entity_ids")
        if (not isinstance(ids, list) or len(ids) != count or any(type(v) is not int or v < 0 for v in ids)
                or len(set(ids)) != count or not isinstance(names, list) or len(names) != count):
            raise ActivationGatewayError("committed Highway cold registry projection differs")
        for value in names:
            require_hex(value, re.compile(r"[0-9a-f]{32}\Z"), "registry name entity")
    commands = result.get("command_receipts")
    labels = ("revalidate-committed-highway-registry", "restart-after-highway-revalidation", "highway-revalidation-restart-readiness", "cold-application-highway-revalidation-readback", "cold-revalidate-committed-highway-registry", "inspect-highway-after-committed-revalidation")
    if not isinstance(commands, list):
        raise ActivationGatewayError("committed Highway execution evidence is absent")
    for label in labels:
        matches = [entry for entry in commands if isinstance(entry, dict) and entry.get("label") == label]
        if len(matches) != 1 or type(matches[0].get("exit_code")) is not int or matches[0]["exit_code"] != 0:
            raise ActivationGatewayError(f"committed Highway execution evidence differs: {label}")
        if label in (labels[0], "cold-revalidate-committed-highway-registry") and matches[0].get("stdin_sha256") != request["native_sql_sha256"]:
            raise ActivationGatewayError("committed Highway native SQL input identity differs")
    for name in ("loaded_before_observation_sha256", "loaded_after_observation_sha256"):
        require_hex(result.get(name), HEX_64, name)
    if result["loaded_before_observation_sha256"] == result["loaded_after_observation_sha256"]:
        raise ActivationGatewayError("committed Highway restart observation did not change")


def validate_highway_expected_epoch_coverage(result: dict[str, Any], prefix: str = "") -> None:
    sequence = result.get(prefix + "activation_sequence")
    counts = [result.get(prefix + name) for name in (
        "retained_expected_epoch_count", "recovered_expected_epoch_count")]
    if (type(sequence) is not int or not 1 <= sequence <= 1024
            or any(type(value) is not int or not 0 <= value <= 1024 for value in counts)
            or sum(counts) != sequence):
        raise ActivationGatewayError("committed Highway expected epoch coverage differs")


def highway_aggregate_fields(receipt: dict[str, Any]) -> dict[str, Any]:
    if receipt.get("schema") == HIGHWAY_REVALIDATION_SCHEMA:
        return {"phase": HIGHWAY_REVALIDATION_PHASE,
                "highway_revalidation_receipt_sha256": receipt["receipt_sha256"],
                "highway_activation_performed": False, "highway_historical_request_present": False,
                **{"highway_" + name: receipt["revalidation"][name] for name in (
                    "stored_working_set_receipt", "stored_producer_receipt",
                    "historical_composition_receipt_present", "historical_intermediate_receipts_verified",
                    "activation_sequence", "retained_expected_epoch_count", "recovered_expected_epoch_count")}}
    return {"phase": "product-unicode-and-highway-activated",
            "highway_activation_receipt_sha256": receipt["receipt_sha256"]}


def validate_product_terminal_result(result: dict[str, Any]) -> None:
    if result.get("phase") == HIGHWAY_REVALIDATION_PHASE:
        if (result.get("highway_activation_performed") is not False
                or result.get("highway_historical_request_present") is not False
                or type(result.get("highway_historical_composition_receipt_present")) is not bool
                or result.get("highway_historical_intermediate_receipts_verified") is not False
                or "highway_activation_receipt_sha256" in result):
            raise ActivationGatewayError("product revalidation was mislabeled as activation")
        require_hex(result.get("highway_revalidation_receipt_sha256"), HEX_64, "Highway revalidation receipt")
        validate_highway_expected_epoch_coverage(result, "highway_")
        for name in ("highway_stored_working_set_receipt", "highway_stored_producer_receipt"):
            if set(require_hex(result.get(name), HEX_64, name)) == {"0"}:
                raise ActivationGatewayError(f"product revalidation stored reference is empty: {name}")
    elif result.get("phase") != "product-unicode-and-highway-activated" or "highway_revalidation_receipt_sha256" in result:
        raise ActivationGatewayError("product did not reach an exact terminal phase")


def validate_highway_success(contract: dict[str, Any], result: dict[str, Any], package_id: str) -> None:
    if result.get("schema") == HIGHWAY_REVALIDATION_SCHEMA:
        validate_highway_revalidation(contract, result, package_id)
        return
    if (
        result.get("schema") != contract["operation"]["highway_success_schema"]
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_application_readback_proven") is not True
        or result.get("receipt_sha256") != document_identity(result, "receipt_sha256")
    ):
        raise ActivationGatewayError("Highway activation result is not exact and complete")


def validate_package_installation(
    contract: dict[str, Any], result: dict[str, Any], payload: dict[str, Any]
) -> None:
    package = payload["package"]
    expected_release = f"{contract['product']['package_release_root']}/{package['id']}"
    if (
        result.get("schema") != contract["product"]["package_installation_schema"]
        or result.get("phase") != "installed"
        or result.get("package_id") != package["id"]
        or result.get("package_manifest_sha256") != package["manifest_sha256"]
        or result.get("package_root") != expected_release
        or result.get("installation_root") != "/"
        or result.get("installed_release") != expected_release
        or result.get("source_physical_root") != str(Path(package["source_root"]).resolve())
        or result.get("source_package_verified") is not True
        or result.get("installed_package_verified") is not True
        or result.get("overwrite_performed") is not False
        or result.get("installation_receipt_sha256")
        != document_identity(result, "installation_receipt_sha256")
    ):
        raise ActivationGatewayError("product package installation result is not exact and complete")


def verify_installed_bundle(
    executable: Path, require_root_ownership: bool = False
) -> tuple[Path, dict[str, Any]]:
    executable = executable.resolve()
    bundle = executable.parent.parent
    manifest_path = bundle / "bundle-manifest.json"
    manifest = load_json(manifest_path)
    if manifest.get("schema") != "laplace.product-activation-gateway-bundle/v1":
        raise ActivationGatewayError("installed gateway bundle schema differs")
    bundle_id = require_hex(manifest.get("bundle_id"), HEX_64, "gateway bundle id")
    if bundle_id != document_identity(manifest, "bundle_id"):
        raise ActivationGatewayError("gateway bundle identity differs")
    if bundle.name != bundle_id:
        raise ActivationGatewayError("gateway executable is not running from its addressed release")
    if require_root_ownership:
        for directory in (
            bundle,
            bundle / "bin",
            bundle / "contracts",
            bundle / "controllers",
        ):
            metadata = directory.stat()
            if metadata.st_uid != 0 or metadata.st_gid != 0 or metadata.st_mode & 0o022:
                raise ActivationGatewayError(
                    f"gateway bundle directory ownership is unsafe: {directory}"
                )
    files = manifest.get("files")
    if not isinstance(files, list):
        raise ActivationGatewayError("gateway bundle manifest omits files")
    observed: set[str] = set()
    for entry in files:
        if not isinstance(entry, dict) or set(entry) != {"path", "sha256"}:
            raise ActivationGatewayError("gateway bundle manifest entry differs")
        relative = entry["path"]
        if not isinstance(relative, str) or relative in observed:
            raise ActivationGatewayError("gateway bundle paths are invalid or repeated")
        observed.add(relative)
        path = bundle.joinpath(*PurePosixPath(relative).parts)
        if not path.is_file() or path.is_symlink() or sha256_file(path) != entry["sha256"]:
            raise ActivationGatewayError(f"gateway bundle file differs: {relative}")
        if require_root_ownership:
            metadata = path.stat()
            if metadata.st_uid != 0 or metadata.st_gid != 0 or metadata.st_mode & 0o022:
                raise ActivationGatewayError(
                    f"gateway bundle file ownership is unsafe: {relative}"
                )
    return bundle, manifest


def execute_request(
    contract: dict[str, Any], request: dict[str, Any], key: bytes, bundle: Path, now: dt.datetime
) -> dict[str, Any]:
    if os.geteuid() != 0:
        raise ActivationGatewayError("activation request execution requires the installed root gateway")
    payload = validate_request(contract, request, key, now)
    package_id = payload["package"]["id"]
    evidence = Path(contract["product"]["cluster_activation_root"]) / package_id
    installation_result_path = evidence / "package-installation.json"
    cluster_result_path = evidence / "activation-result.json"
    unicode_result_path = Path(contract["product"]["unicode_result"])
    highway_result_path = Path(contract["product"]["highway_result"])
    receipt_path = Path(contract["gateway"]["receipt_root"]) / f"{request['request_id']}.json"
    existing_result = load_json(receipt_path) if receipt_path.exists() else None
    python = contract["gateway"]["python"]
    controllers = bundle / "controllers"
    contracts = bundle / "contracts"
    command_receipts: list[dict[str, Any]] = []
    install_command = [
        python,
        str(controllers / "clusterctl.py"),
        "install-package",
        "--authorize-system-root",
        "--contract",
        str(contracts / "postgresql-cluster.json"),
        "--package-manifest",
        payload["package"]["manifest"],
        "--package-physical-root",
        payload["package"]["source_root"],
        "--root",
        "/",
        "--receipt",
        str(installation_result_path),
    ]
    command_receipts.append(
        run_fixed("install-product-package", install_command, 3600)
    )
    installation_result = load_json(installation_result_path)
    validate_package_installation(contract, installation_result, payload)
    if not cluster_result_path.exists():
        cluster_command = [
            python, str(controllers / "clusterctl.py"), "activate-product", "--authorize-system-root",
            "--contract", str(contracts / "postgresql-cluster.json"),
            "--package-manifest", payload["package"]["manifest"],
            "--resource-observation", payload["resource_observation"]["path"],
            "--evidence-directory", str(evidence), "--output", str(cluster_result_path),
        ]
        command_receipts.append(run_fixed("activate-product-cluster", cluster_command, 7200))
    cluster_result = load_json(cluster_result_path)
    plan_path = validate_cluster_success(contract, cluster_result, package_id)
    unicode_command = [
        python, str(controllers / "unicodectl.py"), "--authorize-system-root",
        "--contract", str(contracts / "unicode-product-activation.json"),
        "--cluster-contract", str(contracts / "postgresql-cluster.json"),
        "--source-contract", str(contracts / "unicode-source.json"),
        "--postgresql-contract", str(contracts / "unicode-postgresql.json"),
        "--package-manifest", payload["package"]["manifest"],
        "--cluster-plan", str(plan_path), "--cluster-activation-receipt", str(cluster_result_path),
        "--source-root", payload["unicode"]["source_root"], "--output", str(unicode_result_path),
    ]
    command_receipts.append(run_fixed("activate-product-unicode", unicode_command, 14400))
    unicode_result = load_json(unicode_result_path)
    validate_unicode_success(contract, unicode_result, package_id)
    highway_command = [
        python, str(controllers / "highwayctl.py"), "--authorize-system-root",
        "--contract", str(contracts / "highway-product-activation.json"),
        "--cluster-contract", str(contracts / "postgresql-cluster.json"),
        "--unicode-contract", str(contracts / "unicode-product-activation.json"),
        "--registry-contract", str(contracts / "highway.json"),
        "--previous-registry-contract", str(contracts / "history/highway-v1.json"),
        "--package-manifest", payload["package"]["manifest"],
        "--cluster-plan", str(plan_path),
        "--cluster-activation-receipt", str(cluster_result_path),
        "--unicode-activation-receipt", str(unicode_result_path),
        "--output", str(highway_result_path),
    ]
    if "highway_revalidation" in contract["operation"]:
        highway_revalidation_contract(contract)
        highway_command.extend(["--committed-revalidation-contract", str(contracts / "highway-committed-revalidation.json")])
    command_receipts.append(run_fixed("activate-product-highway", highway_command, 3600))
    highway_result = load_json(highway_result_path)
    validate_highway_success(contract, highway_result, package_id)
    result = {
        "schema": RESULT_SCHEMA,
        **highway_aggregate_fields(highway_result),
        "request_id": request["request_id"],
        "package_id": package_id,
        "repository_commit": payload["repository"]["commit"],
        "package_installation_receipt_sha256": installation_result[
            "installation_receipt_sha256"
        ],
        "cluster_activation_receipt_sha256": cluster_result["activation_receipt_sha256"],
        "unicode_activation_receipt_sha256": unicode_result["receipt_sha256"],
        "cluster_result": str(cluster_result_path),
        "unicode_result": str(unicode_result_path),
        "highway_result": str(highway_result_path),
        "command_receipts": command_receipts,
        "exact_replay": existing_result is not None,
    }
    result["result_sha256"] = document_identity(result, "result_sha256")
    if existing_result is not None:
        required = {
            "schema", "phase", "request_id", "package_id", "repository_commit",
            "package_installation_receipt_sha256",
            "cluster_activation_receipt_sha256", "unicode_activation_receipt_sha256",
            "cluster_result", "unicode_result",
            "highway_result", "command_receipts", "exact_replay",
            "result_sha256",
        }
        required.update(highway_aggregate_fields(highway_result))
        if (
            set(existing_result) != required
            or existing_result.get("result_sha256")
            != document_identity(existing_result, "result_sha256")
            or existing_result.get("request_id") != request["request_id"]
            or existing_result.get("package_id") != package_id
            or existing_result.get("repository_commit")
            != payload["repository"]["commit"]
            or existing_result.get("package_installation_receipt_sha256")
            != installation_result["installation_receipt_sha256"]
            or existing_result.get("cluster_activation_receipt_sha256")
            != cluster_result["activation_receipt_sha256"]
            or existing_result.get("unicode_activation_receipt_sha256")
            != unicode_result["receipt_sha256"]
            or any(existing_result.get(k) != v for k, v in highway_aggregate_fields(highway_result).items())
        ):
            raise ActivationGatewayError(
                "existing gateway result collides with revalidated product state"
            )
        return existing_result
    atomic_write(receipt_path, canonical_bytes(result))
    return result


def read_stdin_bounded(maximum: int) -> bytes:
    content = sys.stdin.buffer.read(maximum + 1)
    if len(content) > maximum:
        raise ActivationGatewayError("activation request exceeds its maximum byte count")
    if not content:
        raise ActivationGatewayError("activation request is empty")
    return content


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create-request")
    create.add_argument("--contract", default="contracts/product-activation-gateway.json")
    create.add_argument("--continuation", default="state/continuation.json")
    create.add_argument("--product-receipt")
    create.add_argument("--resource-observation")
    create.add_argument("--output", required=True)
    execute = subparsers.add_parser("execute-request")
    execute.add_argument("--contract")
    local = subparsers.add_parser("execute-local-selection")
    local.add_argument("--product-receipt", required=True)
    local.add_argument("--resource-observation", required=True)
    subparsers.add_parser("probe")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    if arguments.command == "create-request":
        contract_path = Path(arguments.contract)
        contract = load_json(contract_path)
        key_name = contract["request"]["secret_environment"]
        if key_name not in os.environ:
            raise ActivationGatewayError(f"required deployment secret is absent: {key_name}")
        request = build_request(
            contract, load_json(Path(arguments.continuation)), os.environ,
            decode_key(os.environ[key_name]), dt.datetime.now(dt.timezone.utc),
            Path(arguments.product_receipt) if arguments.product_receipt else None,
            Path(arguments.resource_observation) if arguments.resource_observation else None,
        )
        atomic_write(Path(arguments.output), canonical_bytes(request), 0o600)
        return 0

    executable = Path(__file__).resolve()
    bundle, _manifest = verify_installed_bundle(
        executable, require_root_ownership=True
    )
    contract_path = Path(arguments.contract) if getattr(arguments, "contract", None) else bundle / "contracts/product-activation-gateway.json"
    if contract_path != bundle / "contracts/product-activation-gateway.json":
        raise ActivationGatewayError("installed gateway accepts only its bundled contract")
    contract = load_json(contract_path)
    if arguments.command == "probe":
        print(json.dumps({"schema": "laplace.product-activation-gateway-probe/v1", "bundle_id": bundle.name}, sort_keys=True))
        return 0
    if os.geteuid() != 0:
        raise ActivationGatewayError("installed activation gateway requires root")
    key_path = Path(contract["request"]["secret_path"])
    if not key_path.is_file() or key_path.is_symlink() or (key_path.stat().st_mode & 0o077) != 0:
        raise ActivationGatewayError("root deployment secret is absent or has unsafe permissions")
    key = decode_key(key_path.read_text(encoding="ascii").strip())
    if arguments.command == "execute-local-selection":
        request = build_local_administrator_request(
            contract,
            Path(arguments.product_receipt),
            Path(arguments.resource_observation),
            key,
            dt.datetime.now(dt.timezone.utc),
        )
    else:
        request = parse_json(read_stdin_bounded(contract["request"]["maximum_bytes"]))
    result = execute_request(contract, request, key, bundle, dt.datetime.now(dt.timezone.utc))
    sys.stdout.write(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ActivationGatewayError as error:
        print(f"product activation gateway: {error}", file=sys.stderr)
        raise SystemExit(1)
