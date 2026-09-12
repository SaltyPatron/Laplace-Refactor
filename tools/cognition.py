#!/usr/bin/env python3
"""Execute the installed Laplace product cognition route for an exact UTF-8 prompt.

This is a product operation, not a SQL or ISA shell.  It selects one of the bounded
native firmware programs emitted by the installed Laplace compiler, binds the active
product epochs and authority recorded by product activation, admits the exact prompt,
executes the production indexed cognition providers, materializes the native result,
and returns the execution receipts.  It never injects an expected answer.

The current PostgreSQL host contract uses peer authentication for the product runner.
Consequently this command is executable by the enrolled product service/operator
identity; broader end-user authentication remains a separate product-surface concern.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Sequence


HEX_128 = re.compile(r"^[0-9a-f]{32}$")
HEX_256 = re.compile(r"^[0-9a-f]{64}$")
HEX_BYTES = re.compile(r"^[0-9a-f]*$")
RELATIONS = ("container", "constituent", "predecessor", "successor", "cooccur", "semantic")

DEFAULT_ACTIVE = Path("/opt/laplace/current")
DEFAULT_RECEIPT_ROOT = Path("/opt/laplace/receipts/postgresql/refactor")
DEFAULT_SOCKET = Path("/opt/laplace/runtime/postgresql/refactor")
DEFAULT_PORT = 55433
DEFAULT_DATABASE = "laplace_refactor"
DEFAULT_ROLE = "laplace_admin"

RECEIPT_SCHEMA = "laplace.installed-cognition-command/v1"
UNICODE_RECEIPT_SCHEMA = "laplace.unicode-product-activation-receipt/v1"
IDENTITIES_SCHEMA = "laplace.unicode-activation-identities/v1"
FIRMWARE_SCHEMA = "laplace.cognition-firmware-image/v1"


class CognitionCommandError(RuntimeError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CognitionCommandError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise CognitionCommandError(f"JSON root is not an object: {path}")
    return value


def require_hex(value: Any, field: str, pattern: re.Pattern[str] = HEX_256) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        width = 128 if pattern is HEX_128 else 256
        raise CognitionCommandError(f"{field} must be lowercase {width}-bit hexadecimal")
    return value


def bytea_literal(value: str) -> str:
    if HEX_BYTES.fullmatch(value) is None or len(value) % 2:
        raise CognitionCommandError("invalid hexadecimal byte string")
    return f"decode('{value}','hex')"


def bytea_hex(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.startswith("\\x"):
        raise CognitionCommandError(f"{field} is not PostgreSQL bytea JSON")
    encoded = value[2:].lower()
    if HEX_BYTES.fullmatch(encoded) is None or len(encoded) % 2:
        raise CognitionCommandError(f"{field} contains invalid PostgreSQL bytea")
    return encoded


def selected_package(active: Path) -> tuple[str, Path]:
    try:
        release = active.resolve(strict=True)
    except OSError as error:
        raise CognitionCommandError(f"active product is unavailable: {active}: {error}") from error
    package_id = require_hex(release.name, "active package id")
    return package_id, release


def load_activation_state(receipt_root: Path, package_id: str) -> dict[str, Any]:
    receipt = load_json(receipt_root / "unicode-product-activation.json")
    if receipt.get("schema") != UNICODE_RECEIPT_SCHEMA or receipt.get("phase") != "product-activated":
        raise CognitionCommandError("Unicode activation receipt is not product-activated")
    if receipt.get("package_id") != package_id:
        raise CognitionCommandError("Unicode activation receipt belongs to a different package")
    request_fingerprint = require_hex(receipt.get("request_fingerprint"), "activation request fingerprint")
    identities = load_json(receipt_root / "unicode" / request_fingerprint / "identities.json")
    if identities.get("schema") != IDENTITIES_SCHEMA:
        raise CognitionCommandError("Unicode activation identities use the wrong schema")
    for name in (
        "request_fingerprint",
        "activation_epoch_fingerprint",
        "authority_fingerprint",
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
    ):
        require_hex(identities.get(name), f"activation identities.{name}")
    require_hex(identities.get("activation_epoch_id"), "activation identities.activation_epoch_id", HEX_128)
    if identities["request_fingerprint"] != request_fingerprint:
        raise CognitionCommandError("activation identities belong to a different request")
    if receipt.get("activation_epoch_id") != identities["activation_epoch_id"]:
        raise CognitionCommandError("activation epoch id differs from the product receipt")
    if receipt.get("activation_epoch_fingerprint") != identities["activation_epoch_fingerprint"]:
        raise CognitionCommandError("activation epoch fingerprint differs from the product receipt")
    return identities


def compile_firmware(executable: Path, relation: str) -> dict[str, Any]:
    if relation not in RELATIONS:
        raise CognitionCommandError(f"unsupported relation: {relation}")
    if not executable.is_file() or executable.is_symlink():
        raise CognitionCommandError(f"installed cognition firmware compiler is unavailable: {executable}")
    try:
        completed = subprocess.run(
            [str(executable), "--relation", relation],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CognitionCommandError(f"cannot execute installed firmware compiler: {error}") from error
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise CognitionCommandError(
            f"installed firmware compiler failed with exit {completed.returncode}: {detail[-2000:]}"
        )
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise CognitionCommandError("installed firmware compiler returned invalid JSON") from error
    if not isinstance(document, dict):
        raise CognitionCommandError("installed firmware compiler returned a non-object")
    program_id = require_hex(document.get("program_id"), "firmware program_id")
    image_hex = document.get("image_hex")
    if (
        document.get("schema") != FIRMWARE_SCHEMA
        or document.get("relation") != relation
        or not isinstance(image_hex, str)
        or HEX_BYTES.fullmatch(image_hex) is None
        or len(image_hex) % 2
        or document.get("image_bytes") != len(image_hex) // 2
        or not image_hex
    ):
        raise CognitionCommandError("installed firmware compiler result violates its contract")
    return document


def psql_command(
    psql: Path,
    socket: Path,
    port: int,
    database: str,
    role: str,
) -> list[str]:
    if not psql.is_file():
        raise CognitionCommandError(f"packaged PostgreSQL client is unavailable: {psql}")
    return [
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


def run_json_sql(
    command: list[str], sql: str, *, timeout: int, operation: str
) -> dict[str, Any]:
    try:
        completed = subprocess.run(
            command,
            cwd="/",
            input=sql,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CognitionCommandError(f"{operation} could not execute: {error}") from error
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no PostgreSQL diagnostic"
        raise CognitionCommandError(f"{operation} failed: {detail[-4000:]}")
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise CognitionCommandError(f"{operation} returned {len(lines)} non-empty rows; expected one")
    try:
        value = json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise CognitionCommandError(f"{operation} returned invalid JSON") from error
    if not isinstance(value, dict):
        raise CognitionCommandError(f"{operation} returned a non-object JSON value")
    return value


def active_epoch_sql() -> str:
    return """SELECT pg_catalog.json_build_object(
  'unicode_present',u.active_present,
  'perfcache_epoch',pg_catalog.encode(u.epoch_fingerprint,'hex'),
  'highway_present',h.active_present,
  'numeric_epoch',pg_catalog.encode(h.activation_epoch_fingerprint,'hex')
)::text
FROM laplace.perfcache_active_control AS u
CROSS JOIN laplace.highway_registry_active_control AS h
WHERE u.singleton AND h.singleton;
"""


def validate_active_epochs(value: dict[str, Any], identities: dict[str, Any]) -> tuple[str, str]:
    if value.get("unicode_present") is not True or value.get("highway_present") is not True:
        raise CognitionCommandError("installed cognition requires active Unicode and Highway generations")
    perfcache = require_hex(value.get("perfcache_epoch"), "active Unicode perfcache epoch")
    numeric = require_hex(value.get("numeric_epoch"), "active Highway numeric epoch")
    if perfcache != identities["activation_epoch_fingerprint"]:
        raise CognitionCommandError("active Unicode generation differs from activation identities")
    return perfcache, numeric


def context_sql(
    identities: dict[str, Any], program_id: str, perfcache_epoch: str, numeric_epoch: str
) -> str:
    epochs = [
        identities["source_epoch"],
        identities["identity_epoch"],
        identities["geometry_epoch"],
        identities["evidence_epoch"],
        program_id,
        identities["dependency_epoch"],
        identities["database_epoch"],
        perfcache_epoch,
        numeric_epoch,
        identities["package_epoch"],
    ]
    for index, value in enumerate(epochs):
        require_hex(value, f"execution epoch[{index}]")
    authority = require_hex(identities["authority_fingerprint"], "execution authority")
    encoded = ",".join(bytea_literal(value) for value in epochs)
    return (
        "ROW(ARRAY[" + encoded + "]::bytea[]," + bytea_literal(authority)
        + ",1073741824::bigint,6,2,1023::bigint,1::smallint,6::smallint,1)"
        "::laplace.execution_context"
    )


def prompt_scope_sql(identities: dict[str, Any], turn_ordinal: int) -> str:
    if turn_ordinal < 0 or turn_ordinal > (1 << 63) - 1:
        raise CognitionCommandError("turn ordinal is outside the supported range")
    values = [
        identities["source_epoch"],
        identities["identity_epoch"],
        identities["geometry_epoch"],
        identities["geometry_epoch"],
        identities["request_fingerprint"],
        identities["package_epoch"],
        identities["database_epoch"],
        identities["dependency_epoch"],
        identities["source_epoch"],
        identities["numeric_epoch"],
    ]
    encoded = ",".join(bytea_literal(require_hex(value, "prompt scope fingerprint")) for value in values)
    return (
        "ROW(" + encoded + f",{turn_ordinal}::numeric,0,1::numeric,1048576::numeric)"
        "::laplace.cognition_prompt_scope"
    )


def request_sql(
    identities: dict[str, Any],
    program_id: str,
    previous_checkpoint_fingerprint: str | None,
) -> str:
    evidence = require_hex(identities["evidence_epoch"], "evidence epoch")
    result_contract = require_hex(identities["request_fingerprint"], "result contract fingerprint")
    program_id = require_hex(program_id, "selected firmware program")
    if previous_checkpoint_fingerprint is None:
        previous = "0" * 64
        present = "false"
    else:
        previous = require_hex(previous_checkpoint_fingerprint, "previous checkpoint fingerprint")
        present = "true"
    search = (
        "ROW(64::numeric,256::numeric,128::numeric,64::numeric,1048576::numeric,"
        "64::numeric,64::numeric,64::numeric,8,1,8,64)"
        "::laplace.cognition_observation_search_budget"
    )
    forward = (
        "ROW(16::numeric,32::numeric,32::numeric,32::numeric,16::numeric,8192::numeric,"
        "128::numeric,128::numeric,4,4)::laplace.cognition_observation_forward_limits"
    )
    materialization = (
        "ROW(64::numeric,64::numeric,4096::numeric,16,1)"
        "::laplace.cognition_firmware_materialization_limits"
    )
    return (
        "ROW(" + bytea_literal(program_id) + "," + bytea_literal(evidence) + ","
        + bytea_literal(result_contract) + "," + bytea_literal(previous) + "," + present + ","
        + search + "," + forward + "," + materialization
        + ",4096::numeric,8192::numeric,0,1)::laplace.cognition_firmware_product_request"
    )


def render_cognition_sql(
    prompt: str,
    identities: dict[str, Any],
    firmware: dict[str, Any],
    perfcache_epoch: str,
    numeric_epoch: str,
    *,
    turn_ordinal: int = 0,
    previous_checkpoint: bytes = b"",
    previous_checkpoint_fingerprint: str | None = None,
) -> str:
    if previous_checkpoint and previous_checkpoint_fingerprint is None:
        raise CognitionCommandError("checkpoint bytes require --checkpoint-fingerprint")
    if previous_checkpoint_fingerprint is not None and not previous_checkpoint:
        raise CognitionCommandError("checkpoint fingerprint requires --checkpoint-in")
    prompt_hex = prompt.encode("utf-8").hex()
    checkpoint_hex = previous_checkpoint.hex()
    program_id = require_hex(firmware.get("program_id"), "firmware program_id")
    image_hex = firmware.get("image_hex")
    if not isinstance(image_hex, str) or HEX_BYTES.fullmatch(image_hex) is None or not image_hex:
        raise CognitionCommandError("firmware image bytes are invalid")
    return f"""SELECT pg_catalog.row_to_json(result)::text
FROM laplace.cognition_firmware_execute_product(
  {context_sql(identities, program_id, perfcache_epoch, numeric_epoch)},
  {bytea_literal(image_hex)},
  pg_catalog.convert_from({bytea_literal(prompt_hex)},'UTF8'),
  {prompt_scope_sql(identities, turn_ordinal)},
  {request_sql(identities, program_id, previous_checkpoint_fingerprint)},
  {bytea_literal(checkpoint_hex)},
  8388608::bigint
) AS result;
"""


def validate_result(result: dict[str, Any], firmware: dict[str, Any]) -> tuple[bytes, bytes, str]:
    status = result.get("status")
    if status != 0:
        failed_step = result.get("failed_step")
        native_status = result.get("native_status")
        raise CognitionCommandError(
            f"installed cognition returned status={status} failed_step={failed_step} native_status={native_status}"
        )
    returned_program = bytea_hex(result.get("program_id"), "result.program_id")
    expected_program = require_hex(firmware.get("program_id"), "firmware program_id")
    if returned_program != expected_program:
        raise CognitionCommandError("executed firmware program differs from the selected installed program")
    output = bytes.fromhex(bytea_hex(result.get("output"), "result.output"))
    checkpoint = bytes.fromhex(bytea_hex(result.get("checkpoint"), "result.checkpoint"))
    next_checkpoint = bytea_hex(
        result.get("next_checkpoint_fingerprint"), "result.next_checkpoint_fingerprint"
    )
    require_hex(next_checkpoint, "result.next_checkpoint_fingerprint")
    for field in (
        "execution_receipt_id",
        "trace_fingerprint",
        "output_fingerprint",
        "prompt_admission_receipt_id",
        "prompt_persistence_receipt_id",
    ):
        require_hex(bytea_hex(result.get(field), f"result.{field}"), f"result.{field}")
    bytea_hex(result.get("trunk_entity_id"), "result.trunk_entity_id")
    return output, checkpoint, next_checkpoint


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def execute(arguments: argparse.Namespace) -> dict[str, Any]:
    package_id, release = selected_package(arguments.active)
    identities = load_activation_state(arguments.receipt_root, package_id)
    firmware = compile_firmware(release / "bin/laplace_cognition_firmware_compile", arguments.relation)
    command = psql_command(
        release / "pgsql-18/bin/psql",
        arguments.socket,
        arguments.port,
        arguments.database,
        arguments.role,
    )
    active_epochs = run_json_sql(
        command, active_epoch_sql(), timeout=60, operation="active cognition epoch read"
    )
    perfcache_epoch, numeric_epoch = validate_active_epochs(active_epochs, identities)

    if arguments.prompt_file is not None:
        try:
            prompt = arguments.prompt_file.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise CognitionCommandError(f"cannot read UTF-8 prompt file: {error}") from error
    else:
        prompt = arguments.prompt
    if prompt is None:
        raise CognitionCommandError("provide PROMPT or --prompt-file")

    checkpoint = b""
    if arguments.checkpoint_in is not None:
        try:
            checkpoint = arguments.checkpoint_in.read_bytes()
        except OSError as error:
            raise CognitionCommandError(f"cannot read checkpoint: {error}") from error
        if not checkpoint:
            raise CognitionCommandError("checkpoint input is empty")

    sql = render_cognition_sql(
        prompt,
        identities,
        firmware,
        perfcache_epoch,
        numeric_epoch,
        turn_ordinal=arguments.turn_ordinal,
        previous_checkpoint=checkpoint,
        previous_checkpoint_fingerprint=arguments.checkpoint_fingerprint,
    )
    if arguments.render_sql:
        return {"rendered_sql": sql, "program": firmware, "package_id": package_id}

    result = run_json_sql(command, sql, timeout=300, operation="installed product cognition")
    output, next_checkpoint_bytes, next_checkpoint_fingerprint = validate_result(result, firmware)
    if arguments.checkpoint_out is not None:
        write_atomic(arguments.checkpoint_out, next_checkpoint_bytes)

    receipt: dict[str, Any] = {
        "schema": RECEIPT_SCHEMA,
        "package_id": package_id,
        "relation": arguments.relation,
        "program_id": firmware["program_id"],
        "prompt_transport_sha256": hashlib.sha256(prompt.encode("utf-8")).hexdigest(),
        "turn_ordinal": arguments.turn_ordinal,
        "output_hex": output.hex(),
        "next_checkpoint_fingerprint": next_checkpoint_fingerprint,
        "execution": result,
    }
    try:
        receipt["output_utf8"] = output.decode("utf-8")
    except UnicodeDecodeError:
        receipt["output_utf8"] = None
    if arguments.receipt is not None:
        write_atomic(
            arguments.receipt,
            (json.dumps(receipt, sort_keys=True, indent=2) + "\n").encode("utf-8"),
        )
    return receipt


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser(description=__doc__)
    value.add_argument("prompt", nargs="?", help="exact UTF-8 prompt")
    value.add_argument("--prompt-file", type=Path, help="read the exact UTF-8 prompt from a file")
    value.add_argument("--relation", choices=RELATIONS, default="semantic")
    value.add_argument("--turn-ordinal", type=int, default=0)
    value.add_argument("--checkpoint-in", type=Path)
    value.add_argument("--checkpoint-fingerprint")
    value.add_argument("--checkpoint-out", type=Path)
    value.add_argument("--receipt", type=Path)
    value.add_argument("--json", action="store_true", help="print the complete command receipt")
    value.add_argument("--render-sql", action="store_true", help="validate and print the exact product SQL without executing cognition")
    value.add_argument("--active", type=Path, default=DEFAULT_ACTIVE)
    value.add_argument("--receipt-root", type=Path, default=DEFAULT_RECEIPT_ROOT)
    value.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    value.add_argument("--port", type=int, default=DEFAULT_PORT)
    value.add_argument("--database", default=DEFAULT_DATABASE)
    value.add_argument("--role", default=DEFAULT_ROLE)
    return value


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    if arguments.prompt is not None and arguments.prompt_file is not None:
        print("laplace-cognition: PROMPT and --prompt-file are mutually exclusive", file=sys.stderr)
        return 64
    try:
        receipt = execute(arguments)
        if arguments.render_sql:
            sys.stdout.write(receipt["rendered_sql"])
        elif arguments.json or receipt.get("output_utf8") is None:
            sys.stdout.write(json.dumps(receipt, sort_keys=True, separators=(",", ":")) + "\n")
        else:
            sys.stdout.write(str(receipt["output_utf8"]))
            if not str(receipt["output_utf8"]).endswith("\n"):
                sys.stdout.write("\n")
        return 0
    except CognitionCommandError as error:
        print(f"laplace-cognition: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
