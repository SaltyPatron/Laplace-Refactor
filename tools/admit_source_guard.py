#!/usr/bin/env python3
"""Constrain installed source admission to the configured local source estate."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Sequence

DEFAULT_SOURCE_ESTATE_ROOT = Path("/vault/Data")
SOURCE_ESTATE_ENV = "LAPLACE_SOURCE_ESTATE_ROOT"
DEFAULT_UNICODE_RELATIVE = Path("UCD/Public/UCD/latest")
DEFAULT_ACTIVE = Path("/opt/laplace/current")
DEFAULT_RECEIPT_ROOT = Path("/opt/laplace/receipts/postgresql/refactor")
DEFAULT_SOCKET = Path("/opt/laplace/runtime/postgresql/refactor")
DEFAULT_PORT = 55433
DEFAULT_DATABASE = "laplace_refactor"
DEFAULT_ROLE = "laplace_admin"
HEX128 = re.compile(r"^[0-9a-f]{32}$")
HEX256 = re.compile(r"^[0-9a-f]{64}$")


class AdmissionGuardError(RuntimeError):
    pass


def configured_estate_root() -> Path:
    raw = os.environ.get(SOURCE_ESTATE_ENV)
    root = Path(raw) if raw else DEFAULT_SOURCE_ESTATE_ROOT
    if not root.is_absolute():
        raise AdmissionGuardError(
            f"{SOURCE_ESTATE_ENV} must be an absolute directory path"
        )
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise AdmissionGuardError(
            f"configured source estate is unavailable: {root}: {error}"
        ) from error
    if not resolved.is_dir():
        raise AdmissionGuardError(
            f"configured source estate is not a directory: {resolved}"
        )
    return resolved


def resolve_estate_directory(root: Path, value: str, label: str) -> Path:
    candidate = Path(value)
    if not candidate.is_absolute():
        raise AdmissionGuardError(f"{label} must be an absolute server path")
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as error:
        raise AdmissionGuardError(f"{label} is unavailable: {candidate}: {error}") from error
    if not resolved.is_dir():
        raise AdmissionGuardError(f"{label} is not a directory: {resolved}")
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise AdmissionGuardError(
            f"{label} is outside the configured source estate {root}: {resolved}"
        ) from error
    return resolved


def option_index(argv: Sequence[str], name: str) -> int | None:
    for index, value in enumerate(argv):
        if value == name:
            return index
        if value.startswith(name + "="):
            return index
    return None


def option_value(argv: Sequence[str], name: str, default: str | None = None) -> str | None:
    index = option_index(argv, name)
    if index is None:
        return default
    value = argv[index]
    if value == name:
        if index + 1 >= len(argv):
            raise AdmissionGuardError(f"{name} requires a value")
        return argv[index + 1]
    return value.split("=", 1)[1]


def guarded_arguments(argv: Sequence[str], estate_root: Path | None = None) -> list[str]:
    values = list(argv)
    if any(value in {"-h", "--help"} for value in values[1:]) or len(values) < 3:
        return values

    root = configured_estate_root() if estate_root is None else estate_root.resolve(strict=True)
    if not root.is_dir():
        raise AdmissionGuardError(f"configured source estate is not a directory: {root}")

    values[2] = str(resolve_estate_directory(root, values[2], "source_root"))

    unicode_index = option_index(values, "--unicode-root")
    if unicode_index is None:
        unicode_root = resolve_estate_directory(
            root, str(root / DEFAULT_UNICODE_RELATIVE), "unicode_root"
        )
        values.extend(["--unicode-root", str(unicode_root)])
    else:
        option = values[unicode_index]
        if option == "--unicode-root":
            if unicode_index + 1 >= len(values):
                raise AdmissionGuardError("--unicode-root requires a path")
            values[unicode_index + 1] = str(
                resolve_estate_directory(
                    root, values[unicode_index + 1], "unicode_root"
                )
            )
        else:
            _, raw = option.split("=", 1)
            values[unicode_index] = "--unicode-root=" + str(
                resolve_estate_directory(root, raw, "unicode_root")
            )
    return values


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AdmissionGuardError(f"cannot read activation evidence {path}: {error}") from error
    if not isinstance(value, dict):
        raise AdmissionGuardError(f"activation evidence is not a JSON object: {path}")
    return value


def _live_runtime_state(
    tool_root: Path,
    socket: Path,
    port: int,
    database: str,
    role: str,
) -> dict[str, str]:
    try:
        tool_root = tool_root.resolve(strict=True)
    except OSError as error:
        raise AdmissionGuardError(f"tool root is unavailable: {tool_root}: {error}") from error
    if not tool_root.is_dir():
        raise AdmissionGuardError(f"tool root is not a directory: {tool_root}")
    psql = tool_root / "pgsql-18/bin/psql"
    if not psql.is_file() or psql.is_symlink():
        raise AdmissionGuardError(f"branch-built PostgreSQL client is unavailable: {psql}")
    sql = """
SELECT pg_catalog.json_build_object(
  'activation_epoch_id',pg_catalog.encode(u.activation_epoch_id,'hex'),
  'activation_epoch_fingerprint',pg_catalog.encode(u.epoch_fingerprint,'hex'),
  'geometry_epoch',(
    SELECT pg_catalog.encode(g.geometry_epoch,'hex')
    FROM laplace.unicode_root_generation AS g
    ORDER BY g.recorded_at DESC
    LIMIT 1
  ),
  'perfcache_epoch',pg_catalog.encode(u.epoch_fingerprint,'hex'),
  'numeric_epoch',pg_catalog.encode(h.activation_epoch_fingerprint,'hex')
)::text
FROM laplace.perfcache_active_control AS u
CROSS JOIN laplace.highway_registry_active_control AS h
WHERE u.singleton AND u.active_present
  AND h.singleton AND h.active_present;
"""
    command = [
        str(psql),
        "--host", str(socket),
        "--port", str(port),
        "--username", role,
        "--dbname", database,
        "--no-psqlrc",
        "--set", "ON_ERROR_STOP=1",
        "--quiet",
        "--tuples-only",
        "--no-align",
    ]
    try:
        completed = subprocess.run(
            command,
            input=sql,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AdmissionGuardError(f"live activation-state query could not execute: {error}") from error
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no PostgreSQL diagnostic"
        raise AdmissionGuardError(f"live activation-state query failed: {detail[-4000:]}")
    rows = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    if len(rows) != 1:
        raise AdmissionGuardError(
            f"live activation-state query returned {len(rows)} rows; expected one"
        )
    try:
        value = json.loads(rows[0])
    except json.JSONDecodeError as error:
        raise AdmissionGuardError("live activation-state query returned invalid JSON") from error
    if not isinstance(value, dict):
        raise AdmissionGuardError("live activation-state query did not return an object")
    required = {
        "activation_epoch_id": HEX128,
        "activation_epoch_fingerprint": HEX256,
        "geometry_epoch": HEX256,
        "perfcache_epoch": HEX256,
        "numeric_epoch": HEX256,
    }
    result: dict[str, str] = {}
    for field, pattern in required.items():
        candidate = value.get(field)
        if not isinstance(candidate, str) or pattern.fullmatch(candidate) is None:
            raise AdmissionGuardError(f"live activation state has invalid {field}")
        result[field] = candidate
    return result


def _matching_activation_generations(
    receipt_root: Path,
    live: dict[str, str],
) -> list[Path]:
    try:
        receipt_root = receipt_root.resolve(strict=True)
    except OSError as error:
        raise AdmissionGuardError(
            f"source-admission receipt root is unavailable: {receipt_root}: {error}"
        ) from error
    generation_root = receipt_root / "cluster-activation"
    if not generation_root.is_dir() or generation_root.is_symlink():
        raise AdmissionGuardError(
            f"cluster activation receipt root is unavailable: {generation_root}"
        )

    matches: list[Path] = []
    for generation in sorted(generation_root.iterdir(), key=lambda path: path.name):
        if not generation.is_dir() or generation.is_symlink() or HEX256.fullmatch(generation.name) is None:
            continue
        receipt_path = generation / "unicode-product-activation.json"
        if not receipt_path.is_file() or receipt_path.is_symlink():
            continue
        receipt = _load_json(receipt_path)
        if (
            receipt.get("schema") != "laplace.unicode-product-activation-receipt/v1"
            or receipt.get("phase") != "product-activated"
            or receipt.get("package_id") != generation.name
            or receipt.get("activation_epoch_id") != live["activation_epoch_id"]
            or receipt.get("activation_epoch_fingerprint")
            != live["activation_epoch_fingerprint"]
        ):
            continue
        request = receipt.get("request_fingerprint")
        if not isinstance(request, str) or HEX256.fullmatch(request) is None:
            continue
        identities_path = receipt_root / "unicode" / request / "identities.json"
        if not identities_path.is_file() or identities_path.is_symlink():
            continue
        identities = _load_json(identities_path)
        if (
            identities.get("schema") == "laplace.unicode-activation-identities/v1"
            and identities.get("request_fingerprint") == request
            and identities.get("geometry_epoch") == live["geometry_epoch"]
            and identities.get("perfcache_epoch") == live["perfcache_epoch"]
            and identities.get("numeric_epoch") == live["numeric_epoch"]
        ):
            matches.append(generation)
    return matches


def bind_unpublished_runtime(
    argv: Sequence[str],
    *,
    active_path: Path = DEFAULT_ACTIVE,
    live_state: dict[str, str] | None = None,
) -> list[str]:
    """Bind package-proof execution to the uniquely live persisted generation.

    Ordinary installed use still requires /opt/laplace/current. This fallback exists
    only for an explicit --tool-root when a package is being proved before publication.
    It never publishes or mutates the activation pointer.
    """
    values = list(argv)
    if any(value in {"-h", "--help"} for value in values[1:]) or len(values) < 3:
        return values
    if option_index(values, "--active") is not None:
        return values

    # A present or broken activation selector remains authoritative/fail-closed.
    if active_path.exists() or active_path.is_symlink():
        return values

    tool_root_raw = option_value(values, "--tool-root")
    if tool_root_raw is None:
        return values

    receipt_root = Path(
        option_value(values, "--receipt-root", str(DEFAULT_RECEIPT_ROOT))
        or str(DEFAULT_RECEIPT_ROOT)
    )
    socket = Path(option_value(values, "--socket", str(DEFAULT_SOCKET)) or str(DEFAULT_SOCKET))
    port_raw = option_value(values, "--port", str(DEFAULT_PORT)) or str(DEFAULT_PORT)
    database = option_value(values, "--database", DEFAULT_DATABASE) or DEFAULT_DATABASE
    role = option_value(values, "--role", DEFAULT_ROLE) or DEFAULT_ROLE
    try:
        port = int(port_raw)
    except ValueError as error:
        raise AdmissionGuardError("--port must be an integer") from error
    if port < 1 or port > 65535:
        raise AdmissionGuardError("--port is outside the TCP port range")

    live = (
        dict(live_state)
        if live_state is not None
        else _live_runtime_state(Path(tool_root_raw), socket, port, database, role)
    )
    required = {
        "activation_epoch_id": HEX128,
        "activation_epoch_fingerprint": HEX256,
        "geometry_epoch": HEX256,
        "perfcache_epoch": HEX256,
        "numeric_epoch": HEX256,
    }
    for field, pattern in required.items():
        value = live.get(field)
        if not isinstance(value, str) or pattern.fullmatch(value) is None:
            raise AdmissionGuardError(f"provided live activation state has invalid {field}")

    matches = _matching_activation_generations(receipt_root, live)
    if len(matches) != 1:
        raise AdmissionGuardError(
            "package-proof source admission requires exactly one activation receipt "
            f"matching live PostgreSQL state; found {len(matches)}"
        )
    values.extend(["--active", str(matches[0])])
    return values


def main(argv: Sequence[str] | None = None) -> int:
    values = list(sys.argv if argv is None else argv)
    core = Path(__file__).resolve().parent / "laplace-admit-source-core"
    try:
        if not core.is_file() or core.is_symlink() or not os.access(core, os.X_OK):
            raise AdmissionGuardError(
                f"installed source-admission core is unavailable: {core}"
            )
        guarded = guarded_arguments(values)
        guarded = bind_unpublished_runtime(guarded)
    except AdmissionGuardError as error:
        print(f"laplace-admit-source: {error}", file=sys.stderr)
        return 2

    os.execv(str(core), [str(core), *guarded[1:]])
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
