#!/usr/bin/python3
"""Compile authenticated immutable activation-gateway upgrade requests.

This module is intentionally unprivileged. The only execution authority lives in the
installed root-owned ``product_activation_gateway.py`` bundle. Keeping request
construction here and execution there prevents a second gateway implementation from
becoming executable repository policy.
"""

from __future__ import annotations

import base64
import datetime as dt
import hashlib
import hmac
import json
from pathlib import Path, PurePosixPath
from typing import Any, Mapping


SCHEMA = "laplace.product-activation-gateway-upgrade-request/v1"
DOMAIN = b"laplace.product-activation-gateway-upgrade/v1\0"
MAXIMUM_BYTES = 16 * 1024 * 1024


class GatewayUpgradeError(RuntimeError):
    pass


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def expected_mode(relative: str) -> int:
    return 0o555 if relative.startswith(("bin/", "controllers/")) else 0o444


def request_mac(payload: dict[str, Any], key: bytes) -> str:
    return hmac.new(
        key, DOMAIN + canonical_bytes(payload), hashlib.sha256
    ).hexdigest()


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
        path = PurePosixPath(relative)
        if path.is_absolute() or ".." in path.parts:
            raise GatewayUpgradeError("unsafe trusted bundle path")
        source = repository / source_map[relative]
        if not source.is_file() or source.is_symlink():
            raise GatewayUpgradeError(
                f"gateway source is absent or not physical: {source}"
            )
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
        "created_at": now.astimezone(dt.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "actor": actor,
        "workflow_run_id": workflow_run_id,
        "files": files,
    }
    request = dict(payload)
    request["mac"] = request_mac(payload, key)
    if len(canonical_bytes(request)) > MAXIMUM_BYTES:
        raise GatewayUpgradeError(
            "gateway upgrade request exceeds the fixed byte ceiling"
        )
    return request
