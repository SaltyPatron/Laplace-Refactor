#!/usr/bin/python3
"""Compile and execute authenticated immutable activation-gateway upgrades.

The unprivileged compiler serializes the fixed trusted bundle as data. The installed
root gateway verifies the HMAC, age, exact file roster, hashes, modes, contract
boundary, and content-addressed bundle identity before atomically selecting it.
Repository checkout code is never executed as root.
"""

from __future__ import annotations

import base64
import datetime as dt
import hmac
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
from typing import Any, Mapping


SCHEMA = "laplace.product-activation-gateway-upgrade-request/v1"
RESULT_SCHEMA = "laplace.product-activation-gateway-upgrade-result/v1"
DOMAIN = b"laplace.product-activation-gateway-upgrade/v1\0"
MAXIMUM_BYTES = 16 * 1024 * 1024


class GatewayUpgradeError(RuntimeError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def sha256_bytes(content: bytes) -> str:
    import hashlib
    return hashlib.sha256(content).hexdigest()


def expected_mode(relative: str) -> int:
    return 0o555 if relative.startswith(("bin/", "controllers/")) else 0o444


def request_mac(payload: dict[str, Any], key: bytes) -> str:
    import hashlib
    return hmac.new(key, DOMAIN + canonical_bytes(payload), hashlib.sha256).hexdigest()


def build_request(
    repository: Path,
    contract: dict[str, Any],
    source_map: Mapping[str, str],
    key: bytes,
    now: dt.datetime,
    actor: str,
    workflow_run_id: int,
) -> dict[str, Any]:
    trusted = contract["trusted_bundle"]["files"]
    if set(trusted) != set(source_map):
        raise GatewayUpgradeError("trusted bundle and source map differ")
    files: list[dict[str, Any]] = []
    for relative in trusted:
        if PurePosixPath(relative).is_absolute() or ".." in PurePosixPath(relative).parts:
            raise GatewayUpgradeError("unsafe trusted bundle path")
        source = repository / source_map[relative]
        if not source.is_file() or source.is_symlink():
            raise GatewayUpgradeError(f"gateway source is absent or not physical: {source}")
        content = source.read_bytes()
        files.append(
            {
                "path": relative,
                "mode": format(expected_mode(relative), "04o"),
                "sha256": sha256_bytes(content),
                "content_b64": base64.b64encode(content).decode("ascii"),
            }
        )
    payload = {
        "schema": SCHEMA,
        "created_at": now.astimezone(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "actor": actor,
        "workflow_run_id": workflow_run_id,
        "files": files,
    }
    request = dict(payload)
    request["mac"] = request_mac(payload, key)
    encoded = canonical_bytes(request)
    if len(encoded) > MAXIMUM_BYTES:
        raise GatewayUpgradeError("gateway upgrade request exceeds the fixed byte ceiling")
    return request


def _decode_request(content: bytes, key: bytes, maximum_age_seconds: int, now: dt.datetime) -> tuple[dict[str, Any], list[tuple[str, int, str, bytes]]]:
    if len(content) > MAXIMUM_BYTES:
        raise GatewayUpgradeError("gateway upgrade request exceeds the fixed byte ceiling")
    try:
        request = json.loads(content)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise GatewayUpgradeError(f"invalid gateway upgrade JSON: {error}") from error
    if not isinstance(request, dict) or set(request) != {"schema", "created_at", "actor", "workflow_run_id", "files", "mac"}:
        raise GatewayUpgradeError("gateway upgrade request fields differ")
    if request["schema"] != SCHEMA:
        raise GatewayUpgradeError("gateway upgrade request schema differs")
    payload = {k: v for k, v in request.items() if k != "mac"}
    supplied = request["mac"]
    if not isinstance(supplied, str) or not hmac.compare_digest(supplied, request_mac(payload, key)):
        raise GatewayUpgradeError("gateway upgrade HMAC authentication failed")
    try:
        created = dt.datetime.fromisoformat(request["created_at"].replace("Z", "+00:00"))
    except (AttributeError, ValueError) as error:
        raise GatewayUpgradeError("gateway upgrade creation time is invalid") from error
    now = now.astimezone(dt.timezone.utc)
    age = (now - created).total_seconds()
    if age < -30 or age > maximum_age_seconds:
        raise GatewayUpgradeError("gateway upgrade request is outside its accepted time window")
    if not isinstance(request["actor"], str) or not request["actor"]:
        raise GatewayUpgradeError("gateway upgrade actor is invalid")
    if not isinstance(request["workflow_run_id"], int) or request["workflow_run_id"] < 0:
        raise GatewayUpgradeError("gateway upgrade workflow identity is invalid")
    if not isinstance(request["files"], list):
        raise GatewayUpgradeError("gateway upgrade files are invalid")
    decoded: list[tuple[str, int, str, bytes]] = []
    seen: set[str] = set()
    for entry in request["files"]:
        if not isinstance(entry, dict) or set(entry) != {"path", "mode", "sha256", "content_b64"}:
            raise GatewayUpgradeError("gateway upgrade file fields differ")
        relative = entry["path"]
        if not isinstance(relative, str) or relative in seen or PurePosixPath(relative).is_absolute() or ".." in PurePosixPath(relative).parts:
            raise GatewayUpgradeError("gateway upgrade contains an unsafe or duplicate path")
        seen.add(relative)
        mode = expected_mode(relative)
        if entry["mode"] != format(mode, "04o"):
            raise GatewayUpgradeError("gateway upgrade file mode differs")
        try:
            body = base64.b64decode(entry["content_b64"], validate=True)
        except Exception as error:
            raise GatewayUpgradeError("gateway upgrade file content is not canonical base64") from error
        digest = sha256_bytes(body)
        if entry["sha256"] != digest:
            raise GatewayUpgradeError("gateway upgrade file digest differs")
        decoded.append((relative, mode, digest, body))
    return request, decoded


def execute_request(
    content: bytes,
    current_contract: dict[str, Any],
    key: bytes,
    now: dt.datetime,
    activation_module: Any,
    require_root: bool = True,
) -> dict[str, Any]:
    if require_root and os.geteuid() != 0:
        raise GatewayUpgradeError("gateway self-upgrade requires root")
    request, decoded = _decode_request(
        content, key, int(current_contract["request"]["maximum_age_seconds"]), now
    )
    expected = current_contract["trusted_bundle"]["files"]
    actual = [relative for relative, _mode, _digest, _body in decoded]
    if actual != expected:
        raise GatewayUpgradeError("gateway upgrade file roster differs from the active contract")
    contract_entry = next((body for relative, _mode, _digest, body in decoded if relative == "contracts/product-activation-gateway.json"), None)
    if contract_entry is None:
        raise GatewayUpgradeError("gateway upgrade contract is absent")
    new_contract = json.loads(contract_entry)
    activation_module.validate_contract(new_contract)
    for section, fields in (
        ("gateway", ("release_root", "active_link", "executable", "sudoers_path", "receipt_root", "python")),
        ("request", ("secret_path", "hmac_algorithm", "maximum_age_seconds")),
        ("repository", ("runner_user", "runner_group", "slug")),
    ):
        for field in fields:
            if new_contract[section][field] != current_contract[section][field]:
                raise GatewayUpgradeError(f"gateway upgrade cannot move the established {section}.{field} boundary")
    if new_contract["trusted_bundle"]["files"] != expected:
        raise GatewayUpgradeError("gateway upgrade cannot change its trusted file roster through self-upgrade")

    manifest_core = {
        "schema": "laplace.product-activation-gateway-bundle/v1",
        "files": [{"path": relative, "sha256": digest} for relative, _mode, digest, _body in decoded],
    }
    manifest = dict(manifest_core)
    manifest["bundle_id"] = sha256_bytes(canonical_bytes(manifest_core))
    bundle_id = manifest["bundle_id"]
    release_root = Path(current_contract["gateway"]["release_root"])
    release = release_root / bundle_id
    active = Path(current_contract["gateway"]["active_link"])
    release_root.mkdir(parents=True, exist_ok=True, mode=0o755)
    installed_new = False
    if not release.exists():
        temporary = Path(tempfile.mkdtemp(prefix=f".{bundle_id}.", dir=release_root))
        try:
            temporary.chmod(0o755)
            for relative, mode, _digest, body in decoded:
                destination = temporary.joinpath(*PurePosixPath(relative).parts)
                destination.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
                activation_module.atomic_write(destination, body, mode)
            activation_module.atomic_write(temporary / "bundle-manifest.json", canonical_bytes(manifest), 0o444)
            if require_root:
                for candidate in [temporary, *temporary.rglob("*")]:
                    os.chown(candidate, 0, 0, follow_symlinks=False)
            os.replace(temporary, release)
            installed_new = True
        except BaseException:
            shutil.rmtree(temporary, ignore_errors=True)
            raise
    executable = release / "bin/laplace-product-activate"
    verified_bundle, verified_manifest = activation_module.verify_installed_bundle(
        executable, require_root_ownership=require_root
    )
    if verified_bundle != release or verified_manifest != manifest:
        raise GatewayUpgradeError("self-upgraded gateway bundle did not verify")
    previous = os.readlink(active) if active.is_symlink() else None
    if active.exists() and not active.is_symlink():
        raise GatewayUpgradeError("activation gateway pointer is not a symlink")
    target = os.path.relpath(release, active.parent)
    changed = previous != target
    if changed:
        temporary_link = active.parent / f".{active.name}.{bundle_id[:16]}"
        temporary_link.unlink(missing_ok=True)
        os.symlink(target, temporary_link)
        os.replace(temporary_link, active)
    result = {
        "schema": RESULT_SCHEMA,
        "phase": "gateway-upgraded",
        "bundle_id": bundle_id,
        "installed_new": installed_new,
        "active_pointer_changed": changed,
        "previous_active_target": previous,
        "actor": request["actor"],
        "workflow_run_id": request["workflow_run_id"],
    }
    result["receipt_sha256"] = sha256_bytes(canonical_bytes(result))
    return result
