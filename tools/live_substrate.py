#!/usr/bin/env python3
"""Read persisted Laplace Unicode identities and S3 coordinates from the active product.

This command is intentionally operational, not a fixture harness. It connects as the
ordinary ``laplace_app`` role to the selected persistent PostgreSQL instance and calls
the installed SECURITY DEFINER readback boundary. It performs no writes.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


HEX_128 = re.compile(r"^[0-9a-f]{32}$")
HEX_256 = re.compile(r"^[0-9a-f]{64}$")
DEFAULT_ACTIVE = Path("/opt/laplace/current")
DEFAULT_RECEIPT = Path("/opt/laplace/receipts/postgresql/refactor/unicode-product-activation.json")
DEFAULT_SOCKET = Path("/opt/laplace/runtime/postgresql/refactor")
DEFAULT_PORT = 55433
DEFAULT_DATABASE = "laplace_refactor"
DEFAULT_ROLE = "laplace_app"


class LiveSubstrateError(RuntimeError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LiveSubstrateError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise LiveSubstrateError(f"{path} is not a JSON object")
    return value


def selected_package(active: Path) -> tuple[str, Path]:
    try:
        release = active.resolve(strict=True)
    except OSError as error:
        raise LiveSubstrateError(f"active product is unavailable: {active}: {error}") from error
    package_id = release.name
    if HEX_256.fullmatch(package_id) is None:
        raise LiveSubstrateError(f"active product target is not content-addressed: {release}")
    return package_id, release


def activation_identity(receipt_path: Path, package_id: str) -> tuple[str, str]:
    receipt = load_json(receipt_path)
    if receipt.get("phase") != "product-activated":
        raise LiveSubstrateError("Unicode activation receipt is not product-activated")
    if receipt.get("package_id") != package_id:
        raise LiveSubstrateError("Unicode activation receipt belongs to a different package")
    epoch_id = str(receipt.get("activation_epoch_id", ""))
    epoch_fingerprint = str(receipt.get("activation_epoch_fingerprint", ""))
    if HEX_128.fullmatch(epoch_id) is None or HEX_256.fullmatch(epoch_fingerprint) is None:
        raise LiveSubstrateError("Unicode activation receipt has invalid epoch identity")
    return epoch_id, epoch_fingerprint


def parse_codepoints(text: str, explicit: list[str]) -> list[int]:
    values = [ord(character) for character in text]
    for raw in explicit:
        for token in raw.split(","):
            token = token.strip()
            if not token:
                continue
            base = 16 if token.lower().startswith("0x") else 10
            try:
                value = int(token, base)
            except ValueError as error:
                raise LiveSubstrateError(f"invalid codepoint: {token}") from error
            if value < 0 or value > 0x10FFFF:
                raise LiveSubstrateError(f"codepoint outside Unicode range: {token}")
            values.append(value)
    if not values:
        raise LiveSubstrateError("provide text or at least one --codepoint")
    return values


def render_sql(epoch_id: str, epoch_fingerprint: str, positions: list[int]) -> str:
    requested = ",".join(str(value) for value in positions)
    return f"""WITH direct AS MATERIALIZED (
  SELECT (laplace.unicode_tier0_resolve_batch(
    decode('{epoch_id}','hex'),
    decode('{epoch_fingerprint}','hex'),
    ARRAY[{requested}]::integer[])).*
), rows AS MATERIALIZED (
  SELECT
    ordinal,
    direct.codepoint_positions[ordinal] AS codepoint,
    direct.found[ordinal] AS found,
    direct.placement_ranks[ordinal] AS placement_rank,
    direct.position_classes[ordinal] AS position_class,
    encode(direct.entity_ids[ordinal],'hex') AS entity_id,
    encode(direct.identity_preimage_fingerprints[ordinal],'hex') AS identity_preimage_fingerprint,
    encode(direct.physicality_ids[ordinal],'hex') AS physicality_id,
    direct.coordinate_x[ordinal] AS x,
    direct.coordinate_y[ordinal] AS y,
    direct.coordinate_z[ordinal] AS z,
    direct.coordinate_m[ordinal] AS m,
    encode(direct.hilbert_keys[ordinal],'hex') AS hilbert_key,
    encode(direct.geometry_epochs[ordinal],'hex') AS geometry_epoch
  FROM direct
  CROSS JOIN LATERAL generate_subscripts(direct.codepoint_positions,1) AS ordinal
)
SELECT pg_catalog.json_build_object(
  'schema','laplace.live-substrate-unicode/v1',
  'activation_epoch_id',encode(direct.activation_epoch_id,'hex'),
  'activation_epoch_fingerprint',encode(direct.activation_epoch_fingerprint,'hex'),
  'rows',COALESCE((
    SELECT pg_catalog.json_agg(pg_catalog.json_build_object(
      'ordinal',ordinal,
      'codepoint',codepoint,
      'found',found,
      'placement_rank',placement_rank,
      'position_class',position_class,
      'entity_id',entity_id,
      'identity_preimage_fingerprint',identity_preimage_fingerprint,
      'physicality_id',physicality_id,
      's3',pg_catalog.json_build_array(x,y,z,m),
      'hilbert_key',hilbert_key,
      'geometry_epoch',geometry_epoch
    ) ORDER BY ordinal)
    FROM rows
  ),'[]'::json)
)::text
FROM direct;
"""


def execute(
    psql: Path,
    socket: Path,
    port: int,
    database: str,
    role: str,
    sql: str,
) -> dict[str, Any]:
    if not psql.is_file():
        raise LiveSubstrateError(f"packaged psql is unavailable: {psql}")
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
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        input=sql,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise LiveSubstrateError(f"active substrate read failed: {detail[-2000:]}")
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise LiveSubstrateError(f"active substrate read returned {len(lines)} result lines")
    try:
        value = json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise LiveSubstrateError("active substrate read did not return JSON") from error
    if not isinstance(value, dict) or value.get("schema") != "laplace.live-substrate-unicode/v1":
        raise LiveSubstrateError("active substrate read returned the wrong schema")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("text", nargs="?", default="", help="Unicode text to inspect in exact occurrence order")
    parser.add_argument("--codepoint", action="append", default=[], help="decimal or 0x-prefixed codepoint; repeat or comma-separate")
    parser.add_argument("--active", type=Path, default=DEFAULT_ACTIVE)
    parser.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--database", default=DEFAULT_DATABASE)
    parser.add_argument("--role", default=DEFAULT_ROLE)
    args = parser.parse_args()

    try:
        positions = parse_codepoints(args.text, args.codepoint)
        package_id, release = selected_package(args.active)
        epoch_id, epoch_fingerprint = activation_identity(args.receipt, package_id)
        result = execute(
            release / "pgsql-18/bin/psql",
            args.socket,
            args.port,
            args.database,
            args.role,
            render_sql(epoch_id, epoch_fingerprint, positions),
        )
        rows = result.get("rows")
        if not isinstance(rows, list) or len(rows) != len(positions):
            raise LiveSubstrateError("active substrate did not return one row per requested occurrence")
        for index, row in enumerate(rows):
            if not isinstance(row, dict) or row.get("codepoint") != positions[index]:
                raise LiveSubstrateError("active substrate changed requested occurrence order")
            row["character"] = chr(positions[index])
        result["package_id"] = package_id
        result["database"] = args.database
        result["role"] = args.role
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        return 0
    except LiveSubstrateError as error:
        print(f"laplace-live-substrate: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
