#!/usr/bin/python3
"""Installed root entry point for exact whole-product activation.

The repository-side request compiler remains ``product_activation.py``. The installed
gateway loads that implementation from the immutable bundle and also accepts one
special authenticated gateway-upgrade request through the already-authorized
``execute-request`` verb. Upgrade content is data, never repository code executed as
root: the gateway validates the exact trusted file roster, hashes, modes, security
boundary and content-addressed bundle before atomically changing ``current``.
"""

from __future__ import annotations

import base64
import datetime as dt
import hashlib
import hmac
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tempfile
from typing import Any, Sequence


def implementation_path() -> Path:
    installed = Path(__file__).resolve().parent.parent / "controllers/product_activation_impl.py"
    if installed.is_file():
        return installed
    source = Path(__file__).with_name("product_activation.py")
    if source.is_file():
        return source
    raise RuntimeError("product activation implementation is absent")


def load_implementation() -> Any:
    path = implementation_path()
    specification = importlib.util.spec_from_file_location(
        "laplace_product_activation_gateway_impl", path
    )
    if specification is None or specification.loader is None:
        raise RuntimeError("cannot load product activation implementation")
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


activation = load_implementation()
KEY_FINGERPRINT_DOMAIN = b"laplace.product-activation-key-fingerprint/v1\0"
UPGRADE_SCHEMA = "laplace.product-activation-gateway-upgrade-request/v1"
UPGRADE_RESULT_SCHEMA = "laplace.product-activation-gateway-upgrade-result/v1"
UPGRADE_DOMAIN = b"laplace.product-activation-gateway-upgrade/v1\0"
UPGRADE_MAXIMUM_BYTES = 16 * 1024 * 1024


def activation_key_fingerprint(key: bytes) -> str:
    return activation.sha256_bytes(KEY_FINGERPRINT_DOMAIN + key)


def producer_document_identity(document: dict[str, Any], field: str) -> str:
    payload = {key: value for key, value in document.items() if key != field}
    return activation.sha256_bytes(activation.canonical_bytes(payload) + b"\n")


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
        != producer_document_identity(result, "installation_receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "product package installation result is not exact and complete"
        )


def validate_cluster_success(
    contract: dict[str, Any], result: dict[str, Any], package_id: str
) -> Path:
    operation = contract["operation"]
    if (
        result.get("schema") != operation["cluster_success_schema"]
        or result.get("phase") != operation["cluster_success_phase"]
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("active_target") != f"releases/{package_id}"
        or result.get("activation_receipt_sha256")
        != producer_document_identity(result, "activation_receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "cluster activation result is not exact and complete"
        )
    plan = activation.require_absolute(result.get("cluster_plan_path"), "cluster plan path")
    evidence_root = Path(contract["product"]["cluster_activation_root"]) / package_id
    activation.require_below(plan, evidence_root, "cluster plan")
    if not plan.is_file() or plan.is_symlink():
        raise activation.ActivationGatewayError("cluster plan evidence is absent")
    return plan


def validate_unicode_success(
    contract: dict[str, Any], result: dict[str, Any], package_id: str
) -> None:
    if (
        result.get("schema") != contract["operation"]["unicode_success_schema"]
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_public_readback_proven") is not True
        or result.get("reverse_inversion_proven") is not True
        or result.get("receipt_sha256")
        != producer_document_identity(result, "receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "Unicode activation result is not exact and complete"
        )


def validate_highway_success(
    contract: dict[str, Any], result: dict[str, Any], package_id: str
) -> None:
    if (
        result.get("schema") != contract["operation"]["highway_success_schema"]
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_application_readback_proven") is not True
        or result.get("receipt_sha256")
        != producer_document_identity(result, "receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "Highway activation result is not exact and complete"
        )


def patch_implementation() -> None:
    activation.validate_package_installation = validate_package_installation
    activation.validate_cluster_success = validate_cluster_success
    activation.validate_unicode_success = validate_unicode_success
    activation.validate_highway_success = validate_highway_success


patch_implementation()


def installed_context() -> tuple[Path, dict[str, Any], bytes]:
    if os.geteuid() != 0:
        raise activation.ActivationGatewayError("installed activation gateway requires root")
    executable = Path(__file__).resolve()
    bundle, _manifest = activation.verify_installed_bundle(
        executable, require_root_ownership=True
    )
    contract = activation.load_json(bundle / "contracts/product-activation-gateway.json")
    activation.validate_contract(contract)
    key_path = Path(contract["request"]["secret_path"])
    if (
        not key_path.is_file()
        or key_path.is_symlink()
        or (key_path.stat().st_mode & 0o077) != 0
    ):
        raise activation.ActivationGatewayError(
            "root deployment secret is absent or has unsafe permissions"
        )
    key = activation.decode_key(key_path.read_text(encoding="ascii").strip())
    return bundle, contract, key


def probe_installed_gateway() -> dict[str, str]:
    bundle, contract, key = installed_context()
    return {
        "schema": "laplace.product-activation-gateway-probe/v2",
        "bundle_id": bundle.name,
        "contract_sha256": activation.sha256_bytes(activation.canonical_bytes(contract)),
        "key_fingerprint_sha256": activation_key_fingerprint(key),
        "self_upgrade": "authenticated-execute-request/v1",
    }


def _upgrade_mac(payload: dict[str, Any], key: bytes) -> str:
    return hmac.new(
        key, UPGRADE_DOMAIN + activation.canonical_bytes(payload), hashlib.sha256
    ).hexdigest()


def _expected_mode(relative: str) -> int:
    return 0o555 if relative.startswith(("bin/", "controllers/")) else 0o444


def _decode_upgrade(
    content: bytes, contract: dict[str, Any], key: bytes
) -> tuple[dict[str, Any], list[tuple[str, int, str, bytes]]]:
    if len(content) > UPGRADE_MAXIMUM_BYTES:
        raise activation.ActivationGatewayError("gateway upgrade request exceeds byte ceiling")
    request = activation.parse_json(content)
    if set(request) != {"schema", "created_at", "actor", "workflow_run_id", "files", "mac"}:
        raise activation.ActivationGatewayError("gateway upgrade request fields differ")
    if request.get("schema") != UPGRADE_SCHEMA:
        raise activation.ActivationGatewayError("gateway upgrade request schema differs")
    payload = {name: value for name, value in request.items() if name != "mac"}
    supplied = request.get("mac")
    if not isinstance(supplied, str) or not hmac.compare_digest(supplied, _upgrade_mac(payload, key)):
        raise activation.ActivationGatewayError("gateway upgrade HMAC authentication failed")
    created = activation.parse_utc(request.get("created_at"))
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    age = (now - created).total_seconds()
    if age < -30 or age > int(contract["request"]["maximum_age_seconds"]):
        raise activation.ActivationGatewayError("gateway upgrade request is outside its time window")
    if not isinstance(request.get("actor"), str) or not request["actor"]:
        raise activation.ActivationGatewayError("gateway upgrade actor is invalid")
    if not isinstance(request.get("workflow_run_id"), int) or request["workflow_run_id"] < 0:
        raise activation.ActivationGatewayError("gateway upgrade workflow identity is invalid")
    if not isinstance(request.get("files"), list):
        raise activation.ActivationGatewayError("gateway upgrade file set is invalid")

    decoded: list[tuple[str, int, str, bytes]] = []
    seen: set[str] = set()
    for entry in request["files"]:
        if not isinstance(entry, dict) or set(entry) != {"path", "mode", "sha256", "content_b64"}:
            raise activation.ActivationGatewayError("gateway upgrade file fields differ")
        relative = entry["path"]
        if (
            not isinstance(relative, str)
            or relative in seen
            or PurePosixPath(relative).is_absolute()
            or ".." in PurePosixPath(relative).parts
        ):
            raise activation.ActivationGatewayError("gateway upgrade path is unsafe or duplicate")
        seen.add(relative)
        mode = _expected_mode(relative)
        if entry["mode"] != format(mode, "04o"):
            raise activation.ActivationGatewayError("gateway upgrade file mode differs")
        try:
            body = base64.b64decode(entry["content_b64"], validate=True)
        except Exception as error:
            raise activation.ActivationGatewayError("gateway upgrade file content is invalid") from error
        digest = activation.sha256_bytes(body)
        if entry["sha256"] != digest:
            raise activation.ActivationGatewayError("gateway upgrade file digest differs")
        decoded.append((relative, mode, digest, body))

    expected = contract["trusted_bundle"]["files"]
    if [relative for relative, _mode, _digest, _body in decoded] != expected:
        raise activation.ActivationGatewayError("gateway upgrade file roster differs")
    return request, decoded


def execute_gateway_upgrade(
    content: bytes, current_contract: dict[str, Any], key: bytes
) -> dict[str, Any]:
    request, decoded = _decode_upgrade(content, current_contract, key)
    contract_bytes = next(
        (body for relative, _mode, _digest, body in decoded
         if relative == "contracts/product-activation-gateway.json"),
        None,
    )
    if contract_bytes is None:
        raise activation.ActivationGatewayError("gateway upgrade contract is absent")
    new_contract = activation.parse_json(contract_bytes)
    activation.validate_contract(new_contract)
    for section, fields in (
        ("gateway", ("release_root", "active_link", "executable", "sudoers_path", "receipt_root", "python")),
        ("request", ("secret_path", "hmac_algorithm", "maximum_age_seconds")),
        ("repository", ("runner_user", "runner_group", "slug")),
    ):
        for field in fields:
            if new_contract[section][field] != current_contract[section][field]:
                raise activation.ActivationGatewayError(
                    f"gateway upgrade cannot move {section}.{field}"
                )
    if new_contract["trusted_bundle"]["files"] != current_contract["trusted_bundle"]["files"]:
        raise activation.ActivationGatewayError("gateway upgrade cannot change trusted file roster")

    core = {
        "schema": "laplace.product-activation-gateway-bundle/v1",
        "files": [
            {"path": relative, "sha256": digest}
            for relative, _mode, digest, _body in decoded
        ],
    }
    manifest = dict(core)
    manifest["bundle_id"] = activation.sha256_bytes(activation.canonical_bytes(core))
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
                activation.atomic_write(destination, body, mode)
            activation.atomic_write(
                temporary / "bundle-manifest.json",
                activation.canonical_bytes(manifest),
                0o444,
            )
            for candidate in [temporary, *temporary.rglob("*")]:
                os.chown(candidate, 0, 0, follow_symlinks=False)
            os.replace(temporary, release)
            installed_new = True
        except BaseException:
            shutil.rmtree(temporary, ignore_errors=True)
            raise

    executable = release / "bin/laplace-product-activate"
    verified_bundle, verified_manifest = activation.verify_installed_bundle(
        executable, require_root_ownership=True
    )
    if verified_bundle != release or verified_manifest != manifest:
        raise activation.ActivationGatewayError("self-upgraded gateway bundle did not verify")
    if active.exists() and not active.is_symlink():
        raise activation.ActivationGatewayError("gateway active pointer is not a symlink")
    previous = os.readlink(active) if active.is_symlink() else None
    target = os.path.relpath(release, active.parent)
    changed = previous != target
    if changed:
        temporary_link = active.parent / f".{active.name}.{bundle_id[:16]}"
        temporary_link.unlink(missing_ok=True)
        os.symlink(target, temporary_link)
        os.replace(temporary_link, active)

    result = {
        "schema": UPGRADE_RESULT_SCHEMA,
        "phase": "gateway-upgraded",
        "bundle_id": bundle_id,
        "installed_new": installed_new,
        "active_pointer_changed": changed,
        "previous_active_target": previous,
        "actor": request["actor"],
        "workflow_run_id": request["workflow_run_id"],
    }
    result["receipt_sha256"] = activation.document_identity(result, "receipt_sha256")
    return result


def execute_stdin_request(content: bytes) -> dict[str, Any]:
    bundle, contract, key = installed_context()
    document = activation.parse_json(content)
    if document.get("schema") == UPGRADE_SCHEMA:
        return execute_gateway_upgrade(content, contract, key)
    if len(content) > int(contract["request"]["maximum_bytes"]):
        raise activation.ActivationGatewayError("activation request exceeds byte ceiling")
    return activation.execute_request(
        contract,
        document,
        key,
        bundle,
        dt.datetime.now(dt.timezone.utc).replace(microsecond=0),
    )


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments == ["probe"]:
        print(json.dumps(probe_installed_gateway(), sort_keys=True))
        return 0
    if arguments == ["execute-request"]:
        content = sys.stdin.buffer.read(UPGRADE_MAXIMUM_BYTES + 1)
        if len(content) > UPGRADE_MAXIMUM_BYTES:
            raise activation.ActivationGatewayError("gateway request exceeds absolute byte ceiling")
        print(json.dumps(execute_stdin_request(content), sort_keys=True))
        return 0
    return activation.main(arguments)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except activation.ActivationGatewayError as error:
        print(f"product activation gateway: {error}", file=sys.stderr)
        raise SystemExit(1)
