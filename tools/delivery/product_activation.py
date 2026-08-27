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
    if not all(isinstance(value, dict) for value in (repository, request, gateway, product, operation, trusted)):
        raise ActivationGatewayError("activation gateway contract sections are incomplete")
    if repository.get("runner_user") != repository.get("runner_group"):
        raise ActivationGatewayError("runner user and group must share one service identity")
    if request.get("schema") != REQUEST_SCHEMA or request.get("result_schema") != RESULT_SCHEMA:
        raise ActivationGatewayError("request/result schema declaration differs")
    if request.get("hmac_algorithm") != "HMAC-SHA-256" or request.get("hmac_domain") != REQUEST_SCHEMA:
        raise ActivationGatewayError("activation request authentication law differs")
    maximum = request.get("maximum_bytes")
    age = request.get("maximum_age_seconds")
    if not isinstance(maximum, int) or maximum < 4096 or maximum > 1024 * 1024:
        raise ActivationGatewayError("request maximum byte count is invalid")
    if not isinstance(age, int) or age < 60 or age > 86400:
        raise ActivationGatewayError("request maximum age is invalid")
    for section, names in (
        (gateway, ("release_root", "active_link", "executable", "sudoers_path", "receipt_root", "python")),
        (product, ("package_manifest_root", "package_release_root", "plan_receipt_root", "cluster_activation_root", "unicode_source_root", "unicode_result", "highway_result")),
    ):
        for name in names:
            require_absolute(section.get(name), f"{name}")
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


def validate_payload_paths(contract: dict[str, Any], payload: dict[str, Any]) -> tuple[Path, Path, Path]:
    package = payload.get("package")
    resource = payload.get("resource_observation")
    unicode = payload.get("unicode")
    if not all(isinstance(value, dict) for value in (package, resource, unicode)):
        raise ActivationGatewayError("activation request input sections are incomplete")
    package_id = require_hex(package.get("id"), HEX_64, "package id")
    manifest = require_absolute(package.get("manifest"), "package manifest")
    resource_path = require_absolute(resource.get("path"), "resource observation")
    source_root = require_absolute(unicode.get("source_root"), "Unicode source root")
    manifest_root = Path(contract["product"]["package_manifest_root"])
    require_below(manifest, manifest_root, "package manifest")
    if manifest.name != "package-manifest.json" or len(manifest.relative_to(manifest_root).parts) != 2:
        raise ActivationGatewayError("package manifest is not in one content-addressed build directory")
    require_hex(manifest.parent.name, HEX_64, "product build plan id")
    expected_resource = Path(contract["product"]["plan_receipt_root"]) / package_id / "resource-observation.json"
    if resource_path != expected_resource:
        raise ActivationGatewayError("resource observation is not package-addressed")
    if source_root != Path(contract["product"]["unicode_source_root"]):
        raise ActivationGatewayError("Unicode source root differs from the product contract")
    return manifest, resource_path, source_root


def validate_physical_inputs(contract: dict[str, Any], payload: dict[str, Any]) -> None:
    manifest_path, resource_path, _source_root = validate_payload_paths(contract, payload)
    package = payload["package"]
    resource = payload["resource_observation"]
    manifest_digest = require_hex(package.get("manifest_sha256"), HEX_64, "package manifest digest")
    resource_digest = require_hex(resource.get("sha256"), HEX_64, "resource observation digest")
    if sha256_file(manifest_path) != manifest_digest:
        raise ActivationGatewayError("package manifest bytes differ from the signed request")
    if sha256_file(resource_path) != resource_digest:
        raise ActivationGatewayError("resource observation bytes differ from the signed request")
    manifest = load_json(manifest_path)
    if manifest.get("package_id") != package["id"]:
        raise ActivationGatewayError("package manifest identity differs from the signed request")
    expected_root = f"{contract['product']['package_release_root']}/{package['id']}"
    if manifest.get("root") != expected_root:
        raise ActivationGatewayError("package manifest root differs from the product release root")


def build_request(
    contract: dict[str, Any],
    continuation: dict[str, Any],
    environment: Mapping[str, str],
    key: bytes,
    now: dt.datetime,
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
    successor = (
        continuation.get("active_work", {})
        .get("implementation_progress", {})
        .get("successor_product_package")
    )
    if not isinstance(successor, dict) or successor.get("activation_eligible") is not True:
        raise ActivationGatewayError("continuation does not select an activation-eligible successor package")
    if successor.get("immutable_release_installed") is not True:
        raise ActivationGatewayError("successor package is not recorded as immutably installed")
    installation = successor.get("installation", {})
    if installation.get("installed_package_verified") is not True:
        raise ActivationGatewayError("successor installed package is not verified")
    resource = successor.get("resource_observation", {})
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
        "package": {
            "id": successor.get("package_id"),
            "manifest": successor.get("package_manifest"),
            "manifest_sha256": successor.get("package_manifest_sha256"),
        },
        "resource_observation": {
            "path": resource.get("receipt"),
            "sha256": resource.get("receipt_sha256"),
            "observation_sha256": resource.get("observation_sha256"),
        },
        "unicode": {"source_root": contract["product"]["unicode_source_root"]},
    }
    if not payload["repository"]["actor"]:
        raise ActivationGatewayError("workflow actor is absent")
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
        payload.get("package"), {"id", "manifest", "manifest_sha256"},
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


def validate_highway_success(contract: dict[str, Any], result: dict[str, Any], package_id: str) -> None:
    if (
        result.get("schema") != contract["operation"]["highway_success_schema"]
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_application_readback_proven") is not True
        or result.get("receipt_sha256") != document_identity(result, "receipt_sha256")
    ):
        raise ActivationGatewayError("Highway activation result is not exact and complete")


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
    cluster_result_path = evidence / "activation-result.json"
    unicode_result_path = Path(contract["product"]["unicode_result"])
    highway_result_path = Path(contract["product"]["highway_result"])
    receipt_path = Path(contract["gateway"]["receipt_root"]) / f"{request['request_id']}.json"
    existing_result = load_json(receipt_path) if receipt_path.exists() else None
    python = contract["gateway"]["python"]
    controllers = bundle / "controllers"
    contracts = bundle / "contracts"
    command_receipts: list[dict[str, Any]] = []
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
        "--package-manifest", payload["package"]["manifest"],
        "--cluster-plan", str(plan_path),
        "--cluster-activation-receipt", str(cluster_result_path),
        "--unicode-activation-receipt", str(unicode_result_path),
        "--output", str(highway_result_path),
    ]
    command_receipts.append(run_fixed("activate-product-highway", highway_command, 3600))
    highway_result = load_json(highway_result_path)
    validate_highway_success(contract, highway_result, package_id)
    result = {
        "schema": RESULT_SCHEMA,
        "phase": "product-unicode-and-highway-activated",
        "request_id": request["request_id"],
        "package_id": package_id,
        "repository_commit": payload["repository"]["commit"],
        "cluster_activation_receipt_sha256": cluster_result["activation_receipt_sha256"],
        "unicode_activation_receipt_sha256": unicode_result["receipt_sha256"],
        "highway_activation_receipt_sha256": highway_result["receipt_sha256"],
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
            "cluster_activation_receipt_sha256", "unicode_activation_receipt_sha256",
            "highway_activation_receipt_sha256", "cluster_result", "unicode_result",
            "highway_result", "command_receipts", "exact_replay",
            "result_sha256",
        }
        if (
            set(existing_result) != required
            or existing_result.get("result_sha256")
            != document_identity(existing_result, "result_sha256")
            or existing_result.get("request_id") != request["request_id"]
            or existing_result.get("package_id") != package_id
            or existing_result.get("repository_commit")
            != payload["repository"]["commit"]
            or existing_result.get("cluster_activation_receipt_sha256")
            != cluster_result["activation_receipt_sha256"]
            or existing_result.get("unicode_activation_receipt_sha256")
            != unicode_result["receipt_sha256"]
            or existing_result.get("highway_activation_receipt_sha256")
            != highway_result["receipt_sha256"]
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
    create.add_argument("--output", required=True)
    execute = subparsers.add_parser("execute-request")
    execute.add_argument("--contract")
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
