#!/usr/bin/env python3
"""Unicode product controller with committed-root runtime-context recovery.

The implementation body remains in ``unicodectl_core.py`` so its existing product
contract stays reviewable. This entry point adds one recovery law for a persistent
Unicode root whose historical admission files were lost: the committed database,
artifact, contract, restart, and public-readback evidence may preserve the root while
a fresh native deployment identity supplies the non-root execution-context channels.
The projection is explicit and separately receipted; native identity output is also
retained byte-for-byte.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from typing import Any, Callable


_CORE_PATH = Path(__file__).with_name("unicodectl_core.py")
_SPEC = importlib.util.spec_from_file_location("laplace_unicode_controller_core", _CORE_PATH)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot load the Unicode product controller core")
_core = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _core
_SPEC.loader.exec_module(_core)
# Product request provenance names the public controller path, not this storage split.
_core.__file__ = str(Path(__file__).resolve())

for _name in dir(_core):
    if not _name.startswith("__"):
        globals()[_name] = getattr(_core, _name)

RUNTIME_CONTEXT_PROJECTION_SCHEMA = "laplace.unicode-runtime-context-projection/v1"
_original_render_inspection_sql = _core.render_inspection_sql
_original_retained_root_identities = _core.retained_root_identities


def render_inspection_sql() -> str:
    """Expose the root-owned fields needed to prove a detached committed root."""
    sql = _original_render_inspection_sql()
    needle = (
        "  'activation_epoch_fingerprint', encode(active.epoch_fingerprint, 'hex'),\n"
    )
    if needle not in sql:
        raise _core.UnicodeActivationError("Unicode inspection contract drifted")
    addition = (
        "  'root_geometry_epoch', (SELECT encode(geometry_epoch,'hex') FROM laplace.unicode_root_generation),\n"
        "  'root_postgresql_contract_fingerprint', (SELECT encode(postgresql_contract_fingerprint,'hex') FROM laplace.unicode_root_generation),\n"
        "  'deposit_activation_epoch_id', (SELECT encode(activation_epoch_id,'hex') FROM laplace.unicode_root_deposit_receipt),\n"
        "  'deposit_activation_epoch_fingerprint', (SELECT encode(activation_epoch_fingerprint,'hex') FROM laplace.unicode_root_deposit_receipt),\n"
    )
    return sql.replace(needle, needle + addition, 1)


def _has_committed_root_projection_boundary(inspection: dict[str, Any]) -> bool:
    required = {
        "active_present",
        "activation_epoch_id",
        "activation_epoch_fingerprint",
        "generation_count",
        "deposit_count",
        "root_receipt",
        "root_geometry_epoch",
        "root_postgresql_contract_fingerprint",
        "deposit_activation_epoch_id",
        "deposit_activation_epoch_fingerprint",
    }
    return required.issubset(inspection)


def _committed_root_runtime_context(
    receipt_root: Path,
    inspection: dict[str, Any],
    request: dict[str, Any],
    contract: dict[str, Any],
    identity_executable: Path,
    identity_runner: Callable[..., Any],
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Compose an auditable runtime context without inventing historical evidence."""
    epoch_id = inspection.get("activation_epoch_id")
    epoch_fingerprint = inspection.get("activation_epoch_fingerprint")
    geometry_epoch = inspection.get("root_geometry_epoch")
    postgresql_fingerprint = inspection.get("root_postgresql_contract_fingerprint")
    expected_postgresql = contract["authority"]["unicode_postgresql_contract_fingerprint"]
    expected_root = contract["expected_result"]["root_receipt"]

    if inspection.get("active_present") is not True:
        raise _core.UnicodeActivationError("committed Unicode recovery requires an active root")
    if inspection.get("generation_count") != 1 or inspection.get("deposit_count") != 1:
        raise _core.UnicodeActivationError("committed Unicode recovery requires one exact root and deposit")
    if not isinstance(epoch_id, str) or _core.HEX_128.fullmatch(epoch_id) is None:
        raise _core.UnicodeActivationError("committed Unicode recovery epoch id is invalid")
    if not isinstance(epoch_fingerprint, str) or _core.HEX_256.fullmatch(epoch_fingerprint) is None:
        raise _core.UnicodeActivationError("committed Unicode recovery epoch fingerprint is invalid")
    if set(epoch_id) == {"0"} or set(epoch_fingerprint) == {"0"}:
        raise _core.UnicodeActivationError("committed Unicode recovery epoch is zero")
    if inspection.get("deposit_activation_epoch_id") != epoch_id:
        raise _core.UnicodeActivationError("committed Unicode deposit epoch id differs from active control")
    if inspection.get("deposit_activation_epoch_fingerprint") != epoch_fingerprint:
        raise _core.UnicodeActivationError(
            "committed Unicode deposit epoch fingerprint differs from active control"
        )
    if not isinstance(geometry_epoch, str) or _core.HEX_256.fullmatch(geometry_epoch) is None:
        raise _core.UnicodeActivationError("committed Unicode geometry epoch is invalid")
    if set(geometry_epoch) == {"0"}:
        raise _core.UnicodeActivationError("committed Unicode geometry epoch is zero")
    if postgresql_fingerprint != expected_postgresql:
        raise _core.UnicodeActivationError(
            "committed Unicode PostgreSQL contract fingerprint differs"
        )
    if inspection.get("root_receipt") != expected_root:
        raise _core.UnicodeActivationError("committed Unicode root receipt differs")

    request_bytes = _core.canonical_bytes(request)
    request_sha = _core.sha256_bytes(request_bytes)
    request_path = receipt_root / ".unicode-activation" / f"request-{request_sha}.json"
    if (
        request_path.is_symlink()
        or not request_path.is_file()
        or request_path.read_bytes() != request_bytes
    ):
        raise _core.UnicodeActivationError(
            "current canonical Unicode deployment request is not retained"
        )

    native, command = identity_runner(identity_executable, request_path, contract)
    _core.validate_identities(native, contract)
    effective = dict(native)
    effective["activation_epoch_id"] = epoch_id
    effective["activation_epoch_fingerprint"] = epoch_fingerprint
    effective["geometry_epoch"] = geometry_epoch
    _core.validate_identities(effective, contract)

    projection = {
        "schema": RUNTIME_CONTEXT_PROJECTION_SCHEMA,
        "disposition": "committed-root-current-runtime",
        "reason": "historical-admission-evidence-unavailable",
        "request_sha256": request_sha,
        "native_request_fingerprint": native["request_fingerprint"],
        "native_activation_epoch_id": native["activation_epoch_id"],
        "native_activation_epoch_fingerprint": native["activation_epoch_fingerprint"],
        "native_geometry_epoch": native["geometry_epoch"],
        "committed_activation_epoch_id": epoch_id,
        "committed_activation_epoch_fingerprint": epoch_fingerprint,
        "committed_geometry_epoch": geometry_epoch,
        "committed_root_receipt": expected_root,
        "committed_postgresql_contract_fingerprint": postgresql_fingerprint,
    }
    projection["projection_sha256"] = _core.document_identity(
        projection, "projection_sha256"
    )
    evidence = (
        receipt_root
        / contract["receipt"]["directory_name"]
        / native["request_fingerprint"]
    )
    evidence.mkdir(parents=True, exist_ok=True, mode=0o750)
    _core.write_immutable(evidence / "native-identities.json", native)
    _core.write_immutable(evidence / "runtime-context-projection.json", projection)
    command = dict(command)
    command["runtime_context_projection"] = projection
    return effective, request, command


def retained_root_identities(
    receipt_root: Path,
    inspection: dict[str, Any],
    request: dict[str, Any],
    contract: dict[str, Any],
    identity_executable: Path,
    identity_runner: Callable[..., Any],
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Prefer exact history; recover from committed root only when history is absent."""
    try:
        return _original_retained_root_identities(
            receipt_root,
            inspection,
            request,
            contract,
            identity_executable,
            identity_runner,
        )
    except _core.UnicodeActivationError as error:
        if str(error) != (
            "committed Unicode root requires one exact retained admission; "
            "canonical_matches=0 staged_matches=0"
        ):
            raise
        if not _has_committed_root_projection_boundary(inspection):
            raise
        return _committed_root_runtime_context(
            receipt_root,
            inspection,
            request,
            contract,
            identity_executable,
            identity_runner,
        )


# Existing execution functions resolve these globals in the core module. Patch the
# two recovery boundaries there so every existing caller gets the same law.
_core.render_inspection_sql = render_inspection_sql
_core.retained_root_identities = retained_root_identities
globals()["render_inspection_sql"] = render_inspection_sql
globals()["retained_root_identities"] = retained_root_identities


def main(argv=None) -> int:
    return _core.main(argv)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except _core.UnicodeActivationError as error:
        print(f"Unicode product activation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
