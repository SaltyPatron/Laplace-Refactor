#!/usr/bin/env python3
"""Admit a verified local source profile into the persistent Laplace substrate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Sequence

HEX = re.compile(r"^[0-9a-f]+$")
HEX256 = re.compile(r"^[0-9a-f]{64}$")
HEX128 = re.compile(r"^[0-9a-f]{32}$")
INTEGER = re.compile(r"^[0-9]+$")
PROFILE_NAMES = {
    "iso-639-3-20260415",
    "cili-pwn-mappings-20240611",
    "cili-pwn-mappings-20260903",
}
DEFAULT_ACTIVE = Path("/opt/laplace/current")
DEFAULT_RECEIPT_ROOT = Path("/opt/laplace/receipts/postgresql/refactor")
DEFAULT_UNICODE_ROOT = Path("/vault/Data/UCD/Public/UCD/latest")
DEFAULT_SOCKET = Path("/opt/laplace/runtime/postgresql/refactor")
DEFAULT_PORT = 55433
DEFAULT_DATABASE = "laplace_refactor"
DEFAULT_ROLE = "laplace_admin"


class AdmissionError(RuntimeError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AdmissionError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise AdmissionError(f"JSON root is not an object: {path}")
    return value


def require_hex(value: Any, field: str, width: int = 64) -> str:
    pattern = HEX128 if width == 32 else HEX256
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise AdmissionError(
            f"{field} must be exactly {width} lowercase hexadecimal characters"
        )
    return value


def bytea(value: str) -> str:
    if not value or HEX.fullmatch(value) is None or len(value) % 2:
        raise AdmissionError("invalid hexadecimal byte string")
    return f"decode('{value}','hex')"


def zero_bytea() -> str:
    return "decode(repeat('00',32),'hex')"


def selected_package(active: Path) -> tuple[str, Path]:
    try:
        release = active.resolve(strict=True)
    except OSError as error:
        raise AdmissionError(f"active product is unavailable: {active}: {error}") from error
    package_id = require_hex(release.name, "active package id")
    return package_id, release


def selected_tool_release(explicit: Path | None, active_release: Path) -> Path:
    if explicit is not None:
        try:
            release = explicit.resolve(strict=True)
        except OSError as error:
            raise AdmissionError(f"tool release is unavailable: {explicit}: {error}") from error
        if not release.is_dir():
            raise AdmissionError("tool release is not a directory")
        return release
    try:
        invoked = Path(sys.argv[0]).resolve(strict=True)
    except OSError:
        return active_release
    if invoked.parent.name == "bin" and invoked.parent.parent.is_dir():
        return invoked.parent.parent
    return active_release


def load_activation_state(receipt_root: Path, package_id: str) -> dict[str, Any]:
    receipt = load_json(receipt_root / "unicode-product-activation.json")
    if receipt.get("schema") != "laplace.unicode-product-activation-receipt/v1":
        raise AdmissionError("Unicode activation receipt has the wrong schema")
    if receipt.get("phase") != "product-activated" or receipt.get("package_id") != package_id:
        raise AdmissionError("Unicode activation receipt does not belong to the active product")
    request = require_hex(receipt.get("request_fingerprint"), "activation request fingerprint")
    identities = load_json(receipt_root / "unicode" / request / "identities.json")
    if identities.get("schema") != "laplace.unicode-activation-identities/v1":
        raise AdmissionError("Unicode activation identities have the wrong schema")
    for field in (
        "request_fingerprint", "authority_fingerprint", "source_epoch", "identity_epoch",
        "geometry_epoch", "evidence_epoch", "firmware_epoch", "dependency_epoch",
        "database_epoch", "perfcache_epoch", "numeric_epoch", "package_epoch",
    ):
        require_hex(identities.get(field), f"activation identities.{field}")
    if identities["request_fingerprint"] != request:
        raise AdmissionError("activation identity request fingerprint differs")
    return identities


def compile_profile(
    executable: Path,
    profile: str,
    source_root: Path,
    unicode_root: Path,
    geometry_epoch: str,
) -> dict[str, str]:
    if profile not in PROFILE_NAMES:
        raise AdmissionError(f"unsupported source profile: {profile}")
    geometry_epoch = require_hex(geometry_epoch, "live Unicode geometry epoch")
    try:
        source_root = source_root.resolve(strict=True)
        unicode_root = unicode_root.resolve(strict=True)
    except OSError as error:
        raise AdmissionError(f"source root is unavailable: {error}") from error
    if not source_root.is_dir() or not unicode_root.is_dir():
        raise AdmissionError("source and Unicode roots must be directories")
    if not executable.is_file() or executable.is_symlink():
        raise AdmissionError(f"installed source-profile compiler is unavailable: {executable}")
    try:
        result = subprocess.run(
            [
                str(executable),
                profile,
                str(source_root),
                str(unicode_root),
                geometry_epoch,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=120,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AdmissionError(f"native source-profile compilation could not execute: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AdmissionError(
            f"native source-profile compilation failed with exit {result.returncode}: {detail[-4000:]}"
        )
    values: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if not line:
            continue
        if "=" not in line:
            raise AdmissionError("native source-profile compiler emitted malformed output")
        key, value = line.split("=", 1)
        if not key or key in values:
            raise AdmissionError("native source-profile compiler emitted duplicate/empty key")
        values[key] = value
    if (
        values.get("SCHEMA") != "laplace.source-profile-compile/v1"
        or values.get("PROFILE") != profile
        or values.get("GEOMETRY_EPOCH") != geometry_epoch
    ):
        raise AdmissionError("native source-profile compiler output has the wrong identity")
    return values


def number(values: dict[str, str], name: str) -> int:
    value = values.get(name)
    if value is None or INTEGER.fullmatch(value) is None:
        raise AdmissionError(f"native source-profile field {name} is not an unsigned integer")
    return int(value)


def hex_value(values: dict[str, str], name: str, width: int | None = None) -> str:
    value = values.get(name)
    if (
        not isinstance(value, str)
        or not value
        or HEX.fullmatch(value) is None
        or len(value) % 2
    ):
        raise AdmissionError(f"native source-profile field {name} is not hexadecimal")
    if width is not None and len(value) != width:
        raise AdmissionError(f"native source-profile field {name} has the wrong width")
    return value


def context_sql(identities: dict[str, Any]) -> str:
    epochs = [
        identities["source_epoch"], identities["identity_epoch"], identities["geometry_epoch"],
        identities["evidence_epoch"], identities["firmware_epoch"],
        identities["dependency_epoch"], identities["database_epoch"],
        identities["perfcache_epoch"], identities["numeric_epoch"], identities["package_epoch"],
    ]
    return (
        "ROW(ARRAY["
        + ",".join(bytea(require_hex(v, "execution epoch")) for v in epochs)
        + "]::bytea[],"
        + bytea(require_hex(identities["authority_fingerprint"], "authority"))
        + ",4294967296::bigint,6,2,1023::bigint,1::smallint,6::smallint,1)"
        "::laplace.execution_context"
    )


def profile_sql(values: dict[str, str]) -> str:
    identity_fields = [
        "AUTHORITY_ID", "RELEASE_ID", "NAMESPACE_ID", "LOCAL_IDENTIFIER_ID",
    ]
    digest_fields = [
        "AUTHORITY_RELEASE_FINGERPRINT", "LICENSE_FINGERPRINT", "ARTIFACT_GRAPH_FINGERPRINT",
        "SYNTAX_AUTHORITY_FINGERPRINT", "RECIPE_PROGRAM_FINGERPRINT",
        "UNIVERSAL_AST_MAPPING_FINGERPRINT", "HIGHWAY_REFERENCES_FINGERPRINT",
        "EPISTEMIC_WITNESSING_FINGERPRINT", "DENOMINATOR_DECLARATION_FINGERPRINT",
        "CONFORMANCE_FINGERPRINT", "COMPLETION_LAW_FINGERPRINT", "SELECTED_BOUNDARY_FINGERPRINT",
    ]
    for name in identity_fields:
        hex_value(values, name, 32)
    for name in digest_fields:
        hex_value(values, name, 64)
    zero_counts = ",".join("0::numeric" for _ in range(30))
    return (
        "ROW(" + zero_bytea() + ","
        + f"{number(values,'KIND')},"
        + ",".join(bytea(values[name]) for name in identity_fields) + ","
        + f"{number(values,'VERSION')}::numeric,"
        + ",".join(bytea(values[name]) for name in digest_fields) + ","
        + zero_counts + ","
        + f"{number(values,'RECONSTRUCTION_CLASS')},{number(values,'SOURCE_FLAGS')})"
        "::laplace.source_profile_manifest"
    )


def artifact_sql(values: dict[str, str], index: int, source_root: Path) -> str:
    prefix = f"ARTIFACT_{index}_"
    artifact_id = hex_value(values, prefix + "ID", 64)
    parent_id = hex_value(values, prefix + "PARENT_ID", 64)
    expected_sha = hex_value(values, prefix + "EXPECTED_SHA256", 64)
    name = hex_value(values, prefix + "NAME")
    media = hex_value(values, prefix + "MEDIA_TYPE")
    local_path = hex_value(values, prefix + "LOCAL_PATH")
    columns = number(values, prefix + "COLUMNS")
    column_sql = [
        bytea(hex_value(values, prefix + f"COLUMN_{column}"))
        for column in range(columns)
    ]
    source_root_hex = str(source_root.resolve(strict=True)).encode("utf-8").hex()
    content = (
        "pg_read_binary_file(pg_catalog.convert_from(" + bytea(source_root_hex)
        + ",'UTF8') || '/' || pg_catalog.convert_from(" + bytea(local_path) + ",'UTF8'))"
    )
    return (
        "ROW(" + bytea(artifact_id) + "," + bytea(parent_id) + "," + bytea(expected_sha)
        + "," + content + "," + bytea(name) + "," + bytea(media)
        + f",{number(values,prefix+'RECORDS')}::numeric"
        + f",{number(values,prefix+'FIELDS')}::numeric"
        + f",{number(values,prefix+'REFERENCE_MASK')}::numeric"
        + f",{number(values,prefix+'MODE')},{number(values,prefix+'DELIMITER')}"
        + f",{number(values,prefix+'TERMINATOR')},{columns},{number(values,prefix+'OUTCOME')}"
        + f",{number(values,prefix+'FLAGS')},ARRAY[" + ",".join(column_sql)
        + "]::bytea[]," + str(number(values,prefix+'HEADER_RECORDS')) + ")"
        "::laplace.tabular_source_artifact"
    )


def reference_rules_sql(values: dict[str, str]) -> str:
    count = number(values, "REFERENCE_RULE_COUNT")
    if count == 0:
        return "ARRAY[]::laplace.tabular_reference_rule[]"
    rows = []
    for index in range(count):
        prefix = f"REFERENCE_RULE_{index}_"
        namespace = hex_value(values, prefix + "NAMESPACE", 32)
        rows.append(
            "ROW("
            + f"{number(values,prefix+'ARTIFACT')}::numeric,{number(values,prefix+'COLUMN')}::numeric,"
            + bytea(namespace)
            + f",{number(values,prefix+'KIND')},{number(values,prefix+'FLAGS')})"
            "::laplace.tabular_reference_rule"
        )
    return "ARRAY[" + ",".join(rows) + "]::laplace.tabular_reference_rule[]"


def mapping_rules_sql(values: dict[str, str]) -> str:
    count = number(values, "MAPPING_RULE_COUNT")
    if count == 0:
        return "ARRAY[]::laplace.tabular_mapping_rule[]"
    rows = []
    for index in range(count):
        prefix = f"MAPPING_RULE_{index}_"
        relation = hex_value(values, prefix + "RELATION")
        rows.append(
            "ROW("
            + f"{number(values,prefix+'ARTIFACT')}::numeric,{number(values,prefix+'LEFT_COLUMN')}::numeric,"
            + f"{number(values,prefix+'RIGHT_COLUMN')}::numeric,{bytea(relation)},"
            + f"{number(values,prefix+'VERSION')}::numeric,{number(values,prefix+'KIND')},"
            + f"{number(values,prefix+'FLAGS')})::laplace.tabular_mapping_rule"
        )
    return "ARRAY[" + ",".join(rows) + "]::laplace.tabular_mapping_rule[]"


def render_sql(values: dict[str, str], identities: dict[str, Any], source_root: Path) -> str:
    artifacts = number(values, "ARTIFACT_COUNT")
    if artifacts < 1 or artifacts > 1024:
        raise AdmissionError("native source profile returned an invalid artifact count")
    artifact_array = "ARRAY[" + ",".join(
        artifact_sql(values, index, source_root) for index in range(artifacts)
    ) + "]::laplace.tabular_source_artifact[]"
    occurrence = hex_value(values, "OCCURRENCE_CONTEXT_FINGERPRINT", 64)
    geometry_epoch = hex_value(values, "GEOMETRY_EPOCH", 64)
    batch = number(values, "PREFERRED_BATCH_BYTES")
    return f"""BEGIN;
SET LOCAL statement_timeout = '15min';
WITH admitted AS MATERIALIZED (
  SELECT (laplace.source_admit_tabular(
    {context_sql(identities)},
    {profile_sql(values)},
    {bytea(geometry_epoch)},
    {bytea(occurrence)},
    {artifact_array},
    {reference_rules_sql(values)},
    {mapping_rules_sql(values)},
    {batch}::numeric
  )).*
)
SELECT pg_catalog.json_build_object(
  'schema','laplace.admit-source/v1',
  'admission',pg_catalog.row_to_json(admitted),
  'persisted_profile',(SELECT pg_catalog.row_to_json(p) FROM laplace.source_profile AS p
                       WHERE p.profile_id=admitted.profile_id),
  'entity_count',(SELECT count(*) FROM laplace.entity),
  'physicality_count',(SELECT count(*) FROM laplace.physicality),
  'attestation_count',(SELECT count(*) FROM laplace.attestation)
)::text
FROM admitted;
COMMIT;
"""


def psql_command(tool_release: Path, args: argparse.Namespace) -> list[str]:
    psql = tool_release / "pgsql-18/bin/psql"
    if not psql.is_file():
        raise AdmissionError(f"packaged PostgreSQL client is unavailable: {psql}")
    return [
        str(psql), "--host", str(args.socket), "--port", str(args.port),
        "--username", args.role, "--dbname", args.database, "--no-psqlrc",
        "--set", "ON_ERROR_STOP=1", "--quiet", "--tuples-only", "--no-align",
    ]


def run_scalar(command: list[str], sql: str, operation: str) -> str:
    try:
        result = subprocess.run(
            command,
            input=sql,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AdmissionError(f"{operation} could not execute: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no PostgreSQL diagnostic"
        raise AdmissionError(f"{operation} failed: {detail[-4000:]}")
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise AdmissionError(f"{operation} returned {len(lines)} rows; expected one")
    return lines[0]


def run_sql(command: list[str], sql: str) -> dict[str, Any]:
    try:
        result = subprocess.run(
            command,
            input=sql,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=1000,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AdmissionError(f"source admission could not execute: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no PostgreSQL diagnostic"
        raise AdmissionError(f"source admission failed: {detail[-6000:]}")
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise AdmissionError(f"source admission returned {len(lines)} JSON rows; expected one")
    try:
        value = json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise AdmissionError("source admission returned invalid JSON") from error
    if not isinstance(value, dict) or value.get("schema") != "laplace.admit-source/v1":
        raise AdmissionError("source admission returned the wrong result schema")
    admission = value.get("admission")
    if not isinstance(admission, dict) or value.get("persisted_profile") is None:
        raise AdmissionError("source admission did not persist its source profile")
    profile_id = admission.get("profile_id")
    receipt_id = admission.get("source_profile_receipt_id")
    if not isinstance(profile_id, str) or not profile_id.startswith("\\x") or len(profile_id) != 66:
        raise AdmissionError("source admission returned an invalid profile id")
    if not isinstance(receipt_id, str) or not receipt_id.startswith("\\x") or len(receipt_id) != 66:
        raise AdmissionError("source admission returned an invalid source-profile receipt")
    return value


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser(description=__doc__)
    value.add_argument("profile", choices=sorted(PROFILE_NAMES))
    value.add_argument("source_root", type=Path)
    value.add_argument("--unicode-root", type=Path, default=DEFAULT_UNICODE_ROOT)
    value.add_argument("--active", type=Path, default=DEFAULT_ACTIVE)
    value.add_argument("--tool-root", type=Path)
    value.add_argument("--receipt-root", type=Path, default=DEFAULT_RECEIPT_ROOT)
    value.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    value.add_argument("--port", type=int, default=DEFAULT_PORT)
    value.add_argument("--database", default=DEFAULT_DATABASE)
    value.add_argument("--role", default=DEFAULT_ROLE)
    value.add_argument("--render-sql", action="store_true")
    value.add_argument("--pretty", action="store_true")
    return value


def main(argv: Sequence[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        active_package_id, active_release = selected_package(args.active)
        tool_release = selected_tool_release(args.tool_root, active_release)
        identities = load_activation_state(args.receipt_root, active_package_id)
        command = psql_command(tool_release, args)
        geometry_epoch = require_hex(
            run_scalar(
                command,
                "SELECT pg_catalog.encode(geometry_epoch,'hex') "
                "FROM laplace.unicode_root_generation "
                "ORDER BY recorded_at DESC LIMIT 1;\n",
                "live Unicode geometry read",
            ),
            "live Unicode geometry epoch",
        )
        compiled = compile_profile(
            tool_release / "bin/laplace_source_profile_compile",
            args.profile,
            args.source_root,
            args.unicode_root,
            geometry_epoch,
        )
        sql = render_sql(compiled, identities, args.source_root)
        if args.render_sql:
            sys.stdout.write(sql)
            return 0
        result = run_sql(command, sql)
        result["active_package_id"] = active_package_id
        result["tool_release"] = str(tool_release)
        result["profile"] = args.profile
        result["geometry_epoch"] = geometry_epoch
        result["native_source_fingerprint"] = hex_value(compiled, "SOURCE_FINGERPRINT", 64)
        result["native_reconstruction_fingerprint"] = hex_value(
            compiled, "RECONSTRUCTION_FINGERPRINT", 64
        )
        if args.pretty:
            sys.stdout.write(json.dumps(result, indent=2, sort_keys=True) + "\n")
        else:
            sys.stdout.write(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
        return 0
    except AdmissionError as error:
        print(f"laplace-admit-source: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
