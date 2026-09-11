#!/usr/bin/env python3
"""Compile selected Laplace substrate operators into a SafeTensors model artifact.

The normal product request supplies one shared typed field/constraint estate and a
small declarative target-slot plan.  PostgreSQL passes that set to the native
scope planner once; the client never invents operator/program identities or repeats
semantic state per target job.  The original explicit-job request remains accepted
as a compatibility surface.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Sequence


REQUEST_SCHEMA_V1 = "laplace.target-attention-export-request/v1"
REQUEST_SCHEMA_V2 = "laplace.target-attention-export-request/v2"
RECEIPT_SCHEMA_V1 = "laplace.target-attention-export-client-receipt/v1"
RECEIPT_SCHEMA_V2 = "laplace.target-attention-export-client-receipt/v2"
ARTIFACT_SCHEMA = "laplace.target-attention-safetensors/v1"
HEX_128 = re.compile(r"^[0-9a-f]{32}$")
HEX_256 = re.compile(r"^[0-9a-f]{64}$")
ROLE = {"qk": 1, "vo": 2}


class ModelExportError(RuntimeError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ModelExportError(f"duplicate JSON object key: {key}")
        value[key] = item
    return value


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ModelExportError(f"cannot read request {path}: {error}") from error
    if not isinstance(value, dict):
        raise ModelExportError("request root must be an object")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def require_object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ModelExportError(f"{field} must be an object")
    return value


def require_list(value: Any, field: str, *, nonempty: bool = True) -> list[Any]:
    if not isinstance(value, list) or (nonempty and not value):
        raise ModelExportError(f"{field} must be {'a non-empty' if nonempty else 'an'} array")
    return value


def require_int(value: Any, field: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ModelExportError(f"{field} must be an integer >= {minimum}")
    if value > (1 << 63) - 1:
        raise ModelExportError(f"{field} exceeds the supported signed 64-bit client range")
    return value


def require_float(value: Any, field: str, *, positive: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ModelExportError(f"{field} must be numeric")
    number = float(value)
    if not math.isfinite(number) or (positive and number <= 0.0):
        raise ModelExportError(f"{field} must be {'positive and ' if positive else ''}finite")
    return number


def require_bool(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise ModelExportError(f"{field} must be boolean")
    return value


def require_hex(value: Any, field: str, pattern: re.Pattern[str]) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        bits = 128 if pattern is HEX_128 else 256
        raise ModelExportError(f"{field} must be lowercase {bits}-bit hexadecimal")
    return value


def digest_sql(value: str) -> str:
    return f"decode('{value}','hex')"


def number_sql(value: float) -> str:
    rendered = format(value, ".17g")
    if rendered in {"inf", "-inf", "nan"}:
        raise ModelExportError("non-finite SQL numeric value")
    return f"{rendered}::double precision"


def context_sql(context: dict[str, Any]) -> str:
    epochs = require_list(context.get("epochs"), "execution_context.epochs")
    if len(epochs) != 10:
        raise ModelExportError("execution_context.epochs must contain exactly 10 digests")
    epoch_sql = ",".join(
        digest_sql(require_hex(value, f"execution_context.epochs[{index}]", HEX_256))
        for index, value in enumerate(epochs)
    )
    authority = require_hex(
        context.get("authority_fingerprint"),
        "execution_context.authority_fingerprint",
        HEX_256,
    )
    memory = require_int(context.get("memory_bytes"), "execution_context.memory_bytes", minimum=1)
    cpu = require_int(context.get("cpu_slots"), "execution_context.cpu_slots", minimum=1)
    io = require_int(context.get("io_slots"), "execution_context.io_slots", minimum=1)
    epoch_mask = require_int(context.get("epoch_mask"), "execution_context.epoch_mask", minimum=1)
    major = require_int(context.get("framework_major"), "execution_context.framework_major", minimum=1)
    minor = require_int(context.get("framework_minor"), "execution_context.framework_minor", minimum=0)
    flags = require_int(context.get("flags"), "execution_context.flags", minimum=0)
    if major > 32767 or minor > 32767 or cpu > 2147483647 or io > 2147483647 or flags > 2147483647:
        raise ModelExportError("execution context integer exceeds its PostgreSQL field width")
    return (
        "ROW(ARRAY[" + epoch_sql + "]," + digest_sql(authority) + ","
        + f"{memory}::bigint,{cpu}::integer,{io}::integer,{epoch_mask}::bigint,"
        + f"{major}::smallint,{minor}::smallint,{flags}::integer)::laplace.execution_context"
    )


def field_sql(value: Any, field_name: str) -> str:
    field = require_object(value, field_name)
    field_id = require_hex(field.get("field_id"), f"{field_name}.field_id", HEX_256)
    entity_id = require_hex(field.get("entity_id"), f"{field_name}.entity_id", HEX_128)
    physicality_id = require_hex(
        field.get("physicality_id", "0" * 64), f"{field_name}.physicality_id", HEX_256
    )
    role_id = require_hex(field.get("role_id", "0" * 64), f"{field_name}.role_id", HEX_256)
    recipe = require_hex(
        field.get("recipe_fingerprint"), f"{field_name}.recipe_fingerprint", HEX_256
    )
    ordinal = require_int(field.get("ordinal"), f"{field_name}.ordinal", minimum=0)
    dimension = require_int(
        field.get("value_dimension", 1), f"{field_name}.value_dimension", minimum=1
    )
    flags = require_int(field.get("flags", 0), f"{field_name}.flags", minimum=0)
    if dimension > 2147483647 or flags > 2147483647:
        raise ModelExportError(f"{field_name} integer exceeds PostgreSQL field width")
    return (
        "ROW(" + ",".join(
            (
                digest_sql(field_id),
                digest_sql(entity_id),
                digest_sql(physicality_id),
                digest_sql(role_id),
                digest_sql(recipe),
                f"{ordinal}::numeric",
                f"{dimension}::integer",
                f"{flags}::integer",
            )
        ) + ")::laplace.cognition_operator_field"
    )


def constraint_sql(value: Any, field_name: str) -> str:
    item = require_object(value, field_name)
    digests = []
    for key in (
        "constraint_id", "plane_id", "law_fingerprint", "units_fingerprint",
        "evidence_root_id", "calculation_receipt_id",
    ):
        digests.append(require_hex(item.get(key), f"{field_name}.{key}", HEX_256))
    source = require_int(item.get("source_field_index"), f"{field_name}.source_field_index")
    target = require_int(item.get("target_field_index"), f"{field_name}.target_field_index")
    scale = require_float(item.get("transport_scale", 1.0), f"{field_name}.transport_scale")
    offset = require_float(item.get("transport_offset", 0.0), f"{field_name}.transport_offset")
    target_value = require_float(item.get("target_value", 0.0), f"{field_name}.target_value")
    precision = require_float(item.get("precision"), f"{field_name}.precision", positive=True)
    family = require_int(item.get("relation_family"), f"{field_name}.relation_family", minimum=1)
    source_class = require_int(item.get("source_class"), f"{field_name}.source_class", minimum=0)
    direction = require_int(item.get("direction"), f"{field_name}.direction", minimum=0)
    transport = require_int(item.get("transport_kind"), f"{field_name}.transport_kind", minimum=0)
    flags = require_int(item.get("flags", 0), f"{field_name}.flags", minimum=0)
    if any(value > 2147483647 for value in (family, source_class, direction, transport, flags)):
        raise ModelExportError(f"{field_name} integer exceeds PostgreSQL field width")
    parts = [digest_sql(value) for value in digests]
    parts.extend(
        (
            f"{source}::numeric", f"{target}::numeric", number_sql(scale), number_sql(offset),
            number_sql(target_value), number_sql(precision), f"{family}::integer",
            f"{source_class}::integer", f"{direction}::integer", f"{transport}::integer",
            f"{flags}::integer",
        )
    )
    return "ROW(" + ",".join(parts) + ")::laplace.cognition_operator_constraint"


def fields_sql(values: list[Any], field_name: str) -> str:
    return "ARRAY[" + ",".join(
        field_sql(item, f"{field_name}[{index}]") for index, item in enumerate(values)
    ) + "]::laplace.cognition_operator_field[]"


def constraints_sql(values: list[Any], field_name: str) -> str:
    return "ARRAY[" + ",".join(
        constraint_sql(item, f"{field_name}[{index}]") for index, item in enumerate(values)
    ) + "]::laplace.cognition_operator_constraint[]"


def role_value(value: Any, field_name: str) -> int:
    if isinstance(value, str):
        if value not in ROLE:
            raise ModelExportError(f"{field_name} must be qk or vo")
        return ROLE[value]
    role = require_int(value, field_name, minimum=1)
    if role not in ROLE.values():
        raise ModelExportError(f"{field_name} must be 1/qk or 2/vo")
    return role


def program_sql(value: Any, field_name: str, context_fingerprint_sql: str) -> str:
    program = require_object(value, field_name)
    program_id = require_hex(program.get("program_id"), f"{field_name}.program_id", HEX_256)
    boundary = require_hex(program.get("boundary_id"), f"{field_name}.boundary_id", HEX_256)
    epoch = require_hex(program.get("evidence_epoch"), f"{field_name}.evidence_epoch", HEX_256)
    result = require_hex(
        program.get("result_contract_fingerprint"),
        f"{field_name}.result_contract_fingerprint", HEX_256,
    )
    families = require_list(
        program.get("eligible_relation_families"), f"{field_name}.eligible_relation_families"
    )
    family_values = [
        require_int(item, f"{field_name}.eligible_relation_families[{index}]", minimum=1)
        for index, item in enumerate(families)
    ]
    if any(item > 2147483647 for item in family_values):
        raise ModelExportError(f"{field_name} relation family exceeds PostgreSQL integer range")
    source_mask = require_int(program.get("eligible_source_mask"), f"{field_name}.eligible_source_mask")
    flags = require_int(program.get("flags"), f"{field_name}.flags")
    tolerance = require_float(
        program.get("numeric_tolerance"), f"{field_name}.numeric_tolerance", positive=True
    )
    version = require_int(program.get("version", 1), f"{field_name}.version", minimum=1)
    if any(item > 2147483647 for item in (source_mask, flags, version)):
        raise ModelExportError(f"{field_name} integer exceeds PostgreSQL field width")
    family_sql = "ARRAY[" + ",".join(str(item) for item in family_values) + "]::integer[]"
    return (
        "ROW(" + ",".join(
            (
                digest_sql(program_id), digest_sql(boundary), context_fingerprint_sql,
                digest_sql(epoch), digest_sql(result), family_sql,
                f"{source_mask}::integer", f"{flags}::integer", number_sql(tolerance),
                f"{version}::integer",
            )
        ) + ")::laplace.cognition_operator_program"
    )


def job_sql(value: Any, index: int, context_fingerprint_sql: str) -> tuple[str, tuple[int, int, int, int]]:
    name = f"jobs[{index}]"
    job = require_object(value, name)
    role = role_value(job.get("target_role"), f"{name}.target_role")
    layer = require_int(job.get("layer_index", 0), f"{name}.layer_index")
    head = require_int(job.get("head_index", 0), f"{name}.head_index")
    expert = require_int(job.get("expert_index", 0), f"{name}.expert_index")
    if any(value > 2147483647 for value in (role, layer, head, expert)):
        raise ModelExportError(f"{name} coordinate exceeds PostgreSQL integer range")
    role_fp = require_hex(job.get("role_fingerprint"), f"{name}.role_fingerprint", HEX_256)
    job_fields = require_list(job.get("fields"), f"{name}.fields")
    job_constraints = require_list(job.get("constraints"), f"{name}.constraints")
    rank = require_int(job.get("head_rank"), f"{name}.head_rank", minimum=1)
    program = program_sql(job.get("operator_program"), f"{name}.operator_program", context_fingerprint_sql)
    sql = (
        "ROW(" + ",".join(
            (
                f"{role}::integer", f"{layer}::integer", f"{head}::integer",
                f"{expert}::integer", digest_sql(role_fp), program,
                fields_sql(job_fields, f"{name}.fields"),
                constraints_sql(job_constraints, f"{name}.constraints"),
                f"{rank}::numeric",
            )
        ) + ")::laplace.target_operator_job"
    )
    return sql, (layer, head, expert, role)


def slot_sql(value: Any, index: int) -> tuple[str, tuple[int, int, int, int], int]:
    name = f"slots[{index}]"
    slot = require_object(value, name)
    role = role_value(slot.get("target_role"), f"{name}.target_role")
    layer = require_int(slot.get("layer_index", 0), f"{name}.layer_index")
    head = require_int(slot.get("head_index", 0), f"{name}.head_index")
    expert = require_int(slot.get("expert_index", 0), f"{name}.expert_index")
    families = require_list(slot.get("eligible_relation_families"), f"{name}.eligible_relation_families")
    family_values = [
        require_int(item, f"{name}.eligible_relation_families[{family_index}]", minimum=1)
        for family_index, item in enumerate(families)
    ]
    if len(set(family_values)) != len(family_values):
        raise ModelExportError(f"{name}.eligible_relation_families must not contain duplicates")
    source_mask = require_int(slot.get("eligible_source_mask"), f"{name}.eligible_source_mask", minimum=1)
    rank = require_int(slot.get("head_rank"), f"{name}.head_rank", minimum=1)
    flags = require_int(slot.get("flags", 0), f"{name}.flags", minimum=0)
    if any(item > 2147483647 for item in (role, layer, head, expert, source_mask, flags)):
        raise ModelExportError(f"{name} integer exceeds PostgreSQL field width")
    if source_mask > 7:
        raise ModelExportError(f"{name}.eligible_source_mask contains an unknown source class")
    if any(item > 2147483647 for item in family_values):
        raise ModelExportError(f"{name} relation family exceeds PostgreSQL integer range")
    family_expr = "ARRAY[" + ",".join(str(item) for item in family_values) + "]::integer[]"
    sql = (
        "ROW(" + ",".join(
            (
                f"{role}::integer", f"{layer}::integer", f"{head}::integer",
                f"{expert}::integer", family_expr, f"{source_mask}::integer",
                f"{rank}::numeric", f"{flags}::integer",
            )
        ) + ")::laplace.target_scope_slot"
    )
    return sql, (layer, head, expert, role), rank


def common_request(request: dict[str, Any]) -> tuple[str, str, str, str, str, int, float, bool]:
    context = require_object(request.get("execution_context"), "execution_context")
    context_expr = context_sql(context)
    evidence_boundary = require_hex(request.get("evidence_boundary"), "evidence_boundary", HEX_256)
    evidence_epoch = require_hex(request.get("evidence_epoch"), "evidence_epoch", HEX_256)
    recipe = require_hex(request.get("recipe_fingerprint"), "recipe_fingerprint", HEX_256)
    target = require_hex(
        request.get("target_contract_fingerprint"), "target_contract_fingerprint", HEX_256
    )
    hidden_width = require_int(request.get("hidden_width"), "hidden_width", minimum=1)
    tolerance = require_float(
        request.get("relative_tolerance"), "relative_tolerance", positive=True
    )
    exact = require_bool(request.get("require_exact", True), "require_exact")
    return context_expr, evidence_boundary, evidence_epoch, recipe, target, hidden_width, tolerance, exact


def validate_head_pairs(coordinate_roles: dict[tuple[int, int, int], dict[int, int]]) -> None:
    for coordinate, roles in coordinate_roles.items():
        if set(roles) != {1, 2} or roles[1] != roles[2]:
            raise ModelExportError(
                "each target head requires exactly one QK and one VO slot with equal rank: "
                f"layer={coordinate[0]} head={coordinate[1]} expert={coordinate[2]}"
            )


def render_legacy_sql(request: dict[str, Any]) -> str:
    context_expr, evidence_boundary, evidence_epoch, recipe, target, hidden_width, tolerance, exact = common_request(request)
    jobs = require_list(request.get("jobs"), "jobs")
    if len(jobs) % 2 != 0:
        raise ModelExportError("jobs must contain complete QK/VO pairs")

    rendered_jobs: list[str] = []
    coordinate_roles: dict[tuple[int, int, int], dict[int, int]] = {}
    context_fp = "bound.context_fingerprint"
    for index, item in enumerate(jobs):
        rendered, coordinate = job_sql(item, index, context_fp)
        rendered_jobs.append(rendered)
        layer, head, expert, role = coordinate
        rank = require_int(require_object(item, f"jobs[{index}]").get("head_rank"), f"jobs[{index}].head_rank", minimum=1)
        roles = coordinate_roles.setdefault((layer, head, expert), {})
        if role in roles:
            raise ModelExportError(
                f"duplicate target role at layer={layer} head={head} expert={expert}"
            )
        roles[role] = rank
    validate_head_pairs(coordinate_roles)

    jobs_expr = "ARRAY[" + ",".join(rendered_jobs) + "]::laplace.target_operator_job[]"
    call = (
        "laplace.target_attention_export(bound.context," + ",".join(
            (
                digest_sql(evidence_boundary), digest_sql(evidence_epoch),
                digest_sql(recipe), digest_sql(target), jobs_expr,
                f"{hidden_width}::numeric", number_sql(tolerance),
                "true" if exact else "false",
            )
        ) + ")"
    )
    return f"""WITH bound AS MATERIALIZED (
  SELECT {context_expr} AS context,
         laplace.execution_context_fingerprint({context_expr}) AS context_fingerprint
), exported AS MATERIALIZED (
  SELECT e.*
  FROM bound
  CROSS JOIN LATERAL {call} AS e
)
SELECT json_build_object(
  'artifact_base64', encode(artifact, 'base64'),
  'artifact_id', encode(artifact_id, 'hex'),
  'compile_receipt_id', encode(compile_receipt_id, 'hex'),
  'compile_request_fingerprint', encode(compile_request_fingerprint, 'hex'),
  'projection_id', encode(projection_id, 'hex'),
  'embedding_fingerprint', encode(embedding_fingerprint, 'hex'),
  'head_set_fingerprint', encode(head_set_fingerprint, 'hex'),
  'byte_count', byte_count,
  'header_byte_count', header_byte_count,
  'data_byte_count', data_byte_count,
  'tensor_count', tensor_count,
  'head_count', head_count,
  'field_count', field_count,
  'hidden_width', hidden_width,
  'semantic_basis_rank', semantic_basis_rank,
  'null_basis_rank', null_basis_rank,
  'max_relative_residual', max_relative_residual,
  'compile_status', compile_status,
  'projection_status', projection_status,
  'codec_status', codec_status
)::text
FROM exported;
"""


def render_scoped_sql(request: dict[str, Any]) -> str:
    context_expr, evidence_boundary, evidence_epoch, recipe, target, hidden_width, tolerance, exact = common_request(request)
    estate_fields = require_list(request.get("fields"), "fields")
    estate_constraints = require_list(request.get("constraints"), "constraints")
    slots = require_list(request.get("slots"), "slots")
    if len(slots) % 2 != 0:
        raise ModelExportError("slots must contain complete QK/VO pairs")
    numeric_tolerance = require_float(
        request.get("operator_numeric_tolerance", 1e-12),
        "operator_numeric_tolerance", positive=True,
    )
    operator_flags = require_int(
        request.get("operator_program_flags", 7), "operator_program_flags", minimum=0
    )
    if operator_flags > 2147483647:
        raise ModelExportError("operator_program_flags exceeds PostgreSQL integer range")

    rendered_slots: list[str] = []
    coordinate_roles: dict[tuple[int, int, int], dict[int, int]] = {}
    for index, item in enumerate(slots):
        rendered, coordinate, rank = slot_sql(item, index)
        rendered_slots.append(rendered)
        layer, head, expert, role = coordinate
        roles = coordinate_roles.setdefault((layer, head, expert), {})
        if role in roles:
            raise ModelExportError(
                f"duplicate target role at layer={layer} head={head} expert={expert}"
            )
        roles[role] = rank
    validate_head_pairs(coordinate_roles)

    slot_expr = "ARRAY[" + ",".join(rendered_slots) + "]::laplace.target_scope_slot[]"
    call = (
        "laplace.target_attention_export_scoped(bound.context," + ",".join(
            (
                digest_sql(evidence_boundary), digest_sql(evidence_epoch),
                digest_sql(recipe), digest_sql(target),
                fields_sql(estate_fields, "fields"),
                constraints_sql(estate_constraints, "constraints"),
                slot_expr, number_sql(numeric_tolerance), f"{operator_flags}::integer",
                f"{hidden_width}::numeric", number_sql(tolerance),
                "true" if exact else "false",
            )
        ) + ")"
    )
    return f"""WITH bound AS MATERIALIZED (
  SELECT {context_expr} AS context
), exported AS MATERIALIZED (
  SELECT e.*
  FROM bound
  CROSS JOIN LATERAL {call} AS e
)
SELECT json_build_object(
  'artifact_base64', encode(artifact, 'base64'),
  'artifact_id', encode(artifact_id, 'hex'),
  'scope_receipt_id', encode(scope_receipt_id, 'hex'),
  'scope_request_fingerprint', encode(scope_request_fingerprint, 'hex'),
  'scope_slot_set_fingerprint', encode(scope_slot_set_fingerprint, 'hex'),
  'compile_receipt_id', encode(compile_receipt_id, 'hex'),
  'compile_request_fingerprint', encode(compile_request_fingerprint, 'hex'),
  'projection_id', encode(projection_id, 'hex'),
  'embedding_fingerprint', encode(embedding_fingerprint, 'hex'),
  'head_set_fingerprint', encode(head_set_fingerprint, 'hex'),
  'byte_count', byte_count,
  'header_byte_count', header_byte_count,
  'data_byte_count', data_byte_count,
  'tensor_count', tensor_count,
  'head_count', head_count,
  'field_count', field_count,
  'hidden_width', hidden_width,
  'semantic_basis_rank', semantic_basis_rank,
  'null_basis_rank', null_basis_rank,
  'max_relative_residual', max_relative_residual,
  'selected_constraint_count', selected_constraint_count,
  'source_mask_union', source_mask_union,
  'scope_status', scope_status,
  'compile_status', compile_status,
  'projection_status', projection_status,
  'codec_status', codec_status
)::text
FROM exported;
"""


def render_sql(request: dict[str, Any]) -> str:
    schema = request.get("schema")
    if schema == REQUEST_SCHEMA_V2:
        return render_scoped_sql(request)
    if schema == REQUEST_SCHEMA_V1:
        return render_legacy_sql(request)
    raise ModelExportError(
        f"request.schema must be {REQUEST_SCHEMA_V2} (or compatibility {REQUEST_SCHEMA_V1})"
    )


def parse_server_output(stdout: str) -> dict[str, Any]:
    lines = [line for line in stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise ModelExportError(f"server returned {len(lines)} non-empty rows; expected exactly one")
    try:
        value = json.loads(lines[0], object_pairs_hook=reject_duplicate_keys)
    except (json.JSONDecodeError, ModelExportError) as error:
        raise ModelExportError(f"server returned invalid export JSON: {error}") from error
    if not isinstance(value, dict):
        raise ModelExportError("server export result is not an object")
    status_names = ["compile_status", "projection_status", "codec_status"]
    if "scope_status" in value:
        status_names.insert(0, "scope_status")
    for name in status_names:
        if value.get(name) != 0:
            raise ModelExportError(f"server export failed: {name}={value.get(name)!r}")
    return value


def validate_artifact(artifact: bytes, server: dict[str, Any]) -> dict[str, Any]:
    if len(artifact) < 9:
        raise ModelExportError("returned artifact is too short to be SafeTensors")
    byte_count = require_int(server.get("byte_count"), "server.byte_count", minimum=1)
    if byte_count != len(artifact):
        raise ModelExportError(
            f"returned artifact byte count differs: receipt={byte_count} actual={len(artifact)}"
        )
    header_length = int.from_bytes(artifact[:8], "little", signed=False)
    header_receipt = require_int(
        server.get("header_byte_count"), "server.header_byte_count", minimum=1
    )
    if header_length != header_receipt or header_length > len(artifact) - 8:
        raise ModelExportError("returned SafeTensors header length differs from server receipt")
    try:
        header = json.loads(artifact[8 : 8 + header_length].decode("utf-8").rstrip(" "))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ModelExportError(f"returned SafeTensors header is invalid: {error}") from error
    if not isinstance(header, dict) or not isinstance(header.get("__metadata__"), dict):
        raise ModelExportError("returned SafeTensors header has no Laplace metadata")
    metadata = header["__metadata__"]
    if metadata.get("laplace_schema") != ARTIFACT_SCHEMA:
        raise ModelExportError("returned artifact schema is not the projected Laplace model format")
    bindings = {
        "laplace_compile_receipt_id": "compile_receipt_id",
        "laplace_compile_request_fingerprint": "compile_request_fingerprint",
        "laplace_projection_id": "projection_id",
        "laplace_embedding_fingerprint": "embedding_fingerprint",
        "laplace_head_set_fingerprint": "head_set_fingerprint",
    }
    for metadata_name, server_name in bindings.items():
        expected = require_hex(server.get(server_name), f"server.{server_name}", HEX_256)
        if metadata.get(metadata_name) != expected:
            raise ModelExportError(f"artifact metadata {metadata_name} differs from server receipt")
    tensors = [name for name in header if name != "__metadata__"]
    tensor_count = require_int(server.get("tensor_count"), "server.tensor_count", minimum=1)
    if len(tensors) != tensor_count:
        raise ModelExportError(
            f"artifact tensor count differs: receipt={tensor_count} header={len(tensors)}"
        )
    if "laplace.embedding" not in header:
        raise ModelExportError("artifact omits the shared embedding tensor")
    head_count = require_int(server.get("head_count"), "server.head_count", minimum=1)
    for role in ("q", "k", "v", "o"):
        observed = sum(name.startswith(f"laplace.{role}.layer_") for name in tensors)
        if observed != head_count:
            raise ModelExportError(
                f"artifact contains {observed} {role.upper()} tensors for {head_count} heads"
            )
    data_count = require_int(server.get("data_byte_count"), "server.data_byte_count", minimum=1)
    if 8 + header_length + data_count != len(artifact):
        raise ModelExportError("SafeTensors header/data accounting does not close over artifact")
    return header


def write_atomic(path: Path, data: bytes, *, force: bool) -> None:
    if path.exists() and not force:
        raise ModelExportError(f"output already exists: {path}; use --force to replace it")
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def execute(
    request_path: Path,
    output_path: Path,
    receipt_path: Path,
    *,
    psql: str,
    host: str | None,
    port: int | None,
    database: str | None,
    user: str | None,
    force: bool,
) -> dict[str, Any]:
    request = load_json(request_path)
    sql = render_sql(request)
    command = [psql, "-X", "-A", "-t", "-v", "ON_ERROR_STOP=1", "-f", "-"]
    if host is not None:
        command.extend(("-h", host))
    if port is not None:
        command.extend(("-p", str(port)))
    if database is not None:
        command.extend(("-d", database))
    if user is not None:
        command.extend(("-U", user))
    try:
        completed = subprocess.run(
            command,
            input=sql,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as error:
        raise ModelExportError(f"cannot execute PostgreSQL client {psql}: {error}") from error
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "no PostgreSQL diagnostic"
        raise ModelExportError(f"model export PostgreSQL call failed: {detail}")
    server = parse_server_output(completed.stdout)
    encoded = server.pop("artifact_base64", None)
    if not isinstance(encoded, str) or not encoded:
        raise ModelExportError("server result omitted artifact_base64")
    try:
        artifact = base64.b64decode(encoded, validate=False)
    except (ValueError, TypeError) as error:
        raise ModelExportError(f"server artifact base64 is invalid: {error}") from error
    validate_artifact(artifact, server)

    artifact_id = require_hex(server.get("artifact_id"), "server.artifact_id", HEX_256)
    request_schema = request.get("schema")
    receipt: dict[str, Any] = {
        "schema": RECEIPT_SCHEMA_V2 if request_schema == REQUEST_SCHEMA_V2 else RECEIPT_SCHEMA_V1,
        "request_schema": request_schema,
        "request_sha256": hashlib.sha256(canonical_bytes(request)).hexdigest(),
        "artifact_sha256": hashlib.sha256(artifact).hexdigest(),
        "artifact_id": artifact_id,
        "artifact_path": str(output_path),
        "artifact_bytes": len(artifact),
        "compile_receipt_id": server["compile_receipt_id"],
        "compile_request_fingerprint": server["compile_request_fingerprint"],
        "projection_id": server["projection_id"],
        "embedding_fingerprint": server["embedding_fingerprint"],
        "head_set_fingerprint": server["head_set_fingerprint"],
        "tensor_count": server["tensor_count"],
        "head_count": server["head_count"],
        "field_count": server["field_count"],
        "hidden_width": server["hidden_width"],
        "semantic_basis_rank": server["semantic_basis_rank"],
        "null_basis_rank": server["null_basis_rank"],
        "max_relative_residual": server["max_relative_residual"],
        "codec": "safetensors-f64",
    }
    if request_schema == REQUEST_SCHEMA_V2:
        receipt.update(
            {
                "scope_receipt_id": require_hex(
                    server.get("scope_receipt_id"), "server.scope_receipt_id", HEX_256
                ),
                "scope_request_fingerprint": require_hex(
                    server.get("scope_request_fingerprint"),
                    "server.scope_request_fingerprint", HEX_256,
                ),
                "scope_slot_set_fingerprint": require_hex(
                    server.get("scope_slot_set_fingerprint"),
                    "server.scope_slot_set_fingerprint", HEX_256,
                ),
                "selected_constraint_count": require_int(
                    server.get("selected_constraint_count"),
                    "server.selected_constraint_count", minimum=1,
                ),
                "source_mask_union": require_int(
                    server.get("source_mask_union"), "server.source_mask_union", minimum=1
                ),
            }
        )
    receipt_bytes = canonical_bytes(receipt)
    write_atomic(output_path, artifact, force=force)
    try:
        write_atomic(receipt_path, receipt_bytes, force=force)
    except Exception:
        if not force:
            output_path.unlink(missing_ok=True)
        raise
    return receipt


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser(
        prog="laplace-model-export",
        description=(
            "Compile one typed Laplace operator estate and declared target slots "
            "into an actual SafeTensors artifact."
        ),
    )
    value.add_argument("request", type=Path, help="target export request JSON")
    value.add_argument("--output", "-o", type=Path, required=True, help="SafeTensors output path")
    value.add_argument(
        "--receipt", type=Path,
        help="receipt JSON path (default: OUTPUT.receipt.json)",
    )
    value.add_argument("--psql", default="psql", help="psql executable")
    value.add_argument("--host", help="PostgreSQL host or Unix socket directory")
    value.add_argument("--port", type=int, help="PostgreSQL port")
    value.add_argument("--database", "-d", help="PostgreSQL database")
    value.add_argument("--user", "-U", help="PostgreSQL role")
    value.add_argument("--force", action="store_true", help="replace existing output and receipt")
    value.add_argument(
        "--render-sql", action="store_true",
        help="validate the request and print the exact SQL without executing it",
    )
    return value


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        request = load_json(arguments.request)
        if arguments.render_sql:
            sys.stdout.write(render_sql(request))
            return 0
        receipt_path = arguments.receipt or Path(str(arguments.output) + ".receipt.json")
        receipt = execute(
            arguments.request,
            arguments.output,
            receipt_path,
            psql=arguments.psql,
            host=arguments.host,
            port=arguments.port,
            database=arguments.database,
            user=arguments.user,
            force=arguments.force,
        )
        sys.stdout.write(json.dumps(receipt, sort_keys=True, separators=(",", ":")) + "\n")
        return 0
    except ModelExportError as error:
        print(f"laplace-model-export: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
