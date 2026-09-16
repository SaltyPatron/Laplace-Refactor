#!/usr/bin/env python3
"""Execute real installed Laplace cognition programs against the persistent product.

This is not a fixture path. It uses the active package's firmware compiler, active
Unicode/context identities, installed PostgreSQL extension, persistent prompt
admission, production indexed cognition providers, exact witnessed realization and
production materialization.

Two independent physical obligations are proven. The repeated-CONSTITUENT program
uses ``AA`` to converge on canonical ``A`` without an expected-answer field. The
CONSTITUENT->CONTAINER program binds its later goal to the admitted observation,
returns that exact ``AA`` composition despite other containers of ``A``, and proves that
materialization resolves one provider node for the root and validates the embedded
Unicode atom run directly. Its ordinary stored-root path uses four metered database
operations, one cached 32-byte trajectory read, and no per-child provider resolution.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "product_cognition_live_runner", ROOT / "tools/delivery/product_activation_runner.py"
)
assert SPEC is not None and SPEC.loader is not None
r = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = r
SPEC.loader.exec_module(r)
u = r.unicodectl
h = r.highwayctl

HEX256 = re.compile(r"^[0-9a-f]{64}$")
HEXBYTES = re.compile(r"^[0-9a-f]*$")

# LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE. These explicit
# relation programs range over the admitted prompt and every matching
# persisted composition in the statement snapshot. The indexed provider reads
# one overflow sentinel and rejects incomplete metadata/payload/candidate sets;
# native resource exhaustion and goalless distinct-target ambiguity still refuse output.
# The root goal is a typed program binding, not a restriction of the provider boundary.
# This declares that finite structural boundary, not complete world knowledge.
OBSERVATION_BOUNDARY_COMPLETE = 8


def bytea_literal(hex_value: str) -> str:
    if not HEXBYTES.fullmatch(hex_value) or len(hex_value) % 2:
        raise RuntimeError("invalid hex byte string")
    return f"decode('{hex_value}','hex')"


def bytea_hex(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.startswith("\\x"):
        raise RuntimeError(f"{field} is not PostgreSQL bytea JSON")
    encoded = value[2:].lower()
    if not HEXBYTES.fullmatch(encoded) or len(encoded) % 2:
        raise RuntimeError(f"{field} contains invalid bytea hex")
    return encoded


def compiler_identity(executable: Path) -> dict[str, Any]:
    if not executable.is_absolute() or executable.is_symlink() or not executable.is_file():
        raise RuntimeError("firmware compiler must be an absolute regular file")
    before = executable.stat()
    digest = u.sha256_file(executable)
    after = executable.stat()
    identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns)
    if identity(before) != identity(after):
        raise RuntimeError("firmware compiler changed while hashing")
    return {"path": str(executable.resolve(strict=True)), "sha256": digest,
            "bytes": after.st_size, "device": after.st_dev, "inode": after.st_ino}


def compile_firmware(
    executable: Path,
    relations: tuple[str, ...] = ("constituent",),
    goal_bindings: tuple[tuple[int, str, int], ...] = (),
) -> tuple[dict[str, Any], dict[str, Any]]:
    if not executable.is_file() or executable.is_symlink():
        raise RuntimeError("installed cognition firmware compiler is absent")
    if not relations:
        raise RuntimeError("installed cognition proof requires at least one relation")
    compiler_before = compiler_identity(executable)
    goals = []
    selected_steps: set[int] = set()
    for step, source, source_step in goal_bindings:
        if (type(step) is not int or not 0 < step < len(relations)
                or step in selected_steps or type(source_step) is not int
                or source not in ("observation", "answer")
                or (source == "observation" and source_step != 0)
                or (source == "answer" and not 0 <= source_step < step)):
            raise RuntimeError("invalid explicit firmware goal binding")
        selected_steps.add(step)
        goals.append({"step": step, "source": source, "source_step": source_step})
    goals.sort(key=lambda item: item["step"])
    command = [str(executable)]
    if len(relations) == 1:
        command.extend(("--relation", relations[0]))
    else:
        command.extend(("--relations", ",".join(relations)))
    for goal in goals:
        operand = f'{goal["step"]}:{goal["source"]}'
        if goal["source"] == "answer":
            operand += f':{goal["source_step"]}'
        command.extend(("--goal", operand))
    completed = subprocess.run(command, text=True, capture_output=True, check=False, timeout=30)
    if compiler_identity(executable) != compiler_before:
        raise RuntimeError("firmware compiler changed during execution")
    if completed.returncode != 0:
        raise RuntimeError(
            f"installed firmware compiler failed: exit={completed.returncode} stderr={completed.stderr.strip()}"
        )
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError("installed firmware compiler returned invalid JSON") from error
    program_id = document.get("program_id")
    image_hex = document.get("image_hex")
    image_bytes = document.get("image_bytes")
    if (
        document.get("schema") != "laplace.cognition-firmware-image/v1"
        or document.get("program") != "relation-chain-exact-witnessed-output"
        or document.get("mode") != "explicit"
        or document.get("relations") != list(relations)
        or u.canonical_bytes(document.get("goal_bindings", [])) != u.canonical_bytes(goals)
        or document.get("step_count") != len(relations) + 1
        or (len(relations) == 1 and document.get("relation") != relations[0])
        or not isinstance(program_id, str)
        or HEX256.fullmatch(program_id) is None
        or not isinstance(image_hex, str)
        or HEXBYTES.fullmatch(image_hex) is None
        or len(image_hex) % 2
        or image_bytes != len(image_hex) // 2
        or image_bytes <= 0
    ):
        raise RuntimeError("installed firmware compiler result is invalid")
    return document, {
        "argv": command,
        "compiler": compiler_before,
        "exit_code": completed.returncode,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
    }


def active_epoch_state(
    plan: dict[str, Any],
    cluster: dict[str, Any],
    unicode_receipt: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any]]:
    sql = """
SELECT pg_catalog.json_build_object(
    'unicode_present', u.active_present,
    'perfcache_epoch', pg_catalog.encode(u.epoch_fingerprint,'hex'),
    'highway_present', h.active_present,
    'numeric_epoch', pg_catalog.encode(h.activation_epoch_fingerprint,'hex')
)::text
FROM laplace.perfcache_active_control AS u
CROSS JOIN laplace.highway_registry_active_control AS h
WHERE u.singleton AND h.singleton;
"""
    state, command = r.runner_sql(
        plan,
        cluster,
        sql,
        "observe-installed-cognition-active-epochs",
        "laplace-runner",
        cluster["instance"]["admin_role"],
        60,
    )
    if not isinstance(state, dict):
        raise RuntimeError("installed cognition active epoch state is not one JSON object")
    perfcache_epoch = state.get("perfcache_epoch")
    numeric_epoch = state.get("numeric_epoch")
    if (
        state.get("unicode_present") is not True
        or state.get("highway_present") is not True
        or not isinstance(perfcache_epoch, str)
        or HEX256.fullmatch(perfcache_epoch) is None
        or not isinstance(numeric_epoch, str)
        or HEX256.fullmatch(numeric_epoch) is None
    ):
        raise RuntimeError("installed cognition requires active Unicode and Highway epochs")
    if perfcache_epoch != unicode_receipt.get("activation_epoch_fingerprint"):
        raise RuntimeError("live Unicode perfcache epoch differs from its activation receipt")
    return state, command


def context_sql(
    identities: dict[str, Any],
    program_id: str,
    perfcache_epoch: str,
    numeric_epoch: str,
) -> str:
    epoch_values = [
        ("source_epoch", identities["source_epoch"]),
        ("identity_epoch", identities["identity_epoch"]),
        ("geometry_epoch", identities["geometry_epoch"]),
        ("evidence_epoch", identities["evidence_epoch"]),
        ("firmware_epoch", program_id),
        ("dependency_epoch", identities["dependency_epoch"]),
        ("database_epoch", identities["database_epoch"]),
        ("perfcache_epoch", perfcache_epoch),
        ("numeric_epoch", numeric_epoch),
        ("package_epoch", identities["package_epoch"]),
    ]
    epochs: list[str] = []
    for name, value in epoch_values:
        if not isinstance(value, str) or HEX256.fullmatch(value) is None:
            raise RuntimeError(f"invalid execution epoch: {name}")
        epochs.append(bytea_literal(value))
    authority = identities["authority_fingerprint"]
    if not isinstance(authority, str) or HEX256.fullmatch(authority) is None:
        raise RuntimeError("invalid authority fingerprint")
    return (
        "ROW(ARRAY[" + ",".join(epochs) + "]::bytea[],"
        + bytea_literal(authority)
        + ",1073741824::bigint,6,2,1023::bigint,1::smallint,6::smallint,1)"
        "::laplace.execution_context"
    )


def prompt_scope_sql(identities: dict[str, Any]) -> str:
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
    for value in values:
        if not isinstance(value, str) or HEX256.fullmatch(value) is None:
            raise RuntimeError("invalid prompt-scope fingerprint")
    return (
        "ROW("
        + ",".join(bytea_literal(value) for value in values)
        + ",0::numeric,0,1::numeric,1048576::numeric)::laplace.cognition_prompt_scope"
    )


def request_sql(identities: dict[str, Any], program_id: str) -> str:
    evidence = identities["evidence_epoch"]
    result_contract = identities["request_fingerprint"]
    for value in (evidence, result_contract, program_id):
        if not isinstance(value, str) or HEX256.fullmatch(value) is None:
            raise RuntimeError("invalid firmware request fingerprint")
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
        "ROW("
        + bytea_literal(program_id)
        + ","
        + bytea_literal(evidence)
        + ","
        + bytea_literal(result_contract)
        + ","
        + bytea_literal("00" * 32)
        + ",false,"
        + search
        + ","
        + forward
        + ","
        + materialization
        + f",4096::numeric,8192::numeric,{OBSERVATION_BOUNDARY_COMPLETE},1)"
        "::laplace.cognition_firmware_product_request"
    )



FAILURE_SCHEMA = "laplace.installed-product-cognition-failure/v1"
MAX_FAILURE_OUTPUT_BYTES = 65536
NATIVE_FAILURE_MARKER = "LAPLACE_COGNITION_FAILURE "
NATIVE_FAILURE_COUNTER_FIELDS = (
    "status", "failed_step", "native_status", "native_disposition",
    "physical_provider_rows", "physical_provider_batches", "semantic_provider_rows",
    "semantic_database_operations", "materialization_nodes",
    "materialization_trajectory_reads", "materialization_trajectory_bytes",
    "materialization_database_operations",
)


def bounded_output(content: str) -> dict[str, Any]:
    raw = content.encode("utf-8")
    retained = raw[:MAX_FAILURE_OUTPUT_BYTES]
    return {
        "observed_bytes": len(raw),
        "observed_sha256": u.sha256_bytes(raw),
        "retained_bytes": len(retained),
        "retained_hex": retained.hex(),
        "truncated": len(raw) != len(retained),
    }


def native_failure_diagnostic(stderr: str, result: Any) -> dict[str, Any]:
    lines = [line.split(NATIVE_FAILURE_MARKER, 1)[1] for line in stderr.splitlines()
             if NATIVE_FAILURE_MARKER in line]
    if not lines:
        return {"state": "unavailable", "reason": "native marker absent"}
    if len(lines) != 1:
        return {"state": "invalid", "reason": "multiple native failure markers"}
    if len(lines[0].encode("utf-8")) > MAX_FAILURE_OUTPUT_BYTES:
        return {"state": "invalid", "reason": "native marker exceeds retention bound"}
    try:
        diagnostic = json.loads(lines[0])
    except json.JSONDecodeError:
        return {"state": "invalid", "reason": "native marker is not JSON"}
    fields = NATIVE_FAILURE_COUNTER_FIELDS
    if (
        not isinstance(diagnostic, dict)
        or diagnostic.get("schema") != "laplace.cognition-failure-diagnostic/v1"
        or any(
            type(diagnostic.get(field)) is not int
            or not 0 <= diagnostic[field] <= ((1 << (32 if field in fields[:4] else 64)) - 1)
            for field in fields
        )
    ):
        return {"state": "invalid", "reason": "native marker schema or counters invalid"}
    if not isinstance(result, dict):
        return {"state": "mismatched", "diagnostic": diagnostic}
    reported = {field: result.get(field) for field in ("status", "failed_step", "native_status")}
    # The existing SQL integer ABI renders native UINT32_MAX (no step) as -1.
    # Preserve the native diagnostic and normalize only that exact comparison.
    if reported["failed_step"] == -1:
        reported["failed_step"] = (1 << 32) - 1
    if any(diagnostic[field] != reported[field] for field in reported):
        return {"state": "mismatched", "diagnostic": diagnostic}
    return {"state": "observed", "diagnostic": diagnostic}


def retain_execution_failure(
    output: Path | None,
    *,
    failure_artifact: Path | None,
    label: str,
    result: Any,
    command_receipt: dict[str, Any] | None,
    captured: dict[str, Any],
    sql: str,
    firmware: dict[str, Any],
    prompt: str,
    provenance: dict[str, Any] | None,
    error: str,
) -> None:
    if output is None and failure_artifact is None:
        return
    result_json = json.dumps(result, sort_keys=True, ensure_ascii=True)
    execution_capture = bounded_output(result_json)
    document = {
        "schema": FAILURE_SCHEMA,
        "success_receipt_issued": False,
        "label": label,
        "error": bounded_output(error),
        "provenance": provenance or {},
        "program": firmware,
        "prompt_utf8": prompt,
        "request_sql_utf8": sql,
        "request_sql_sha256": u.sha256_bytes(sql.encode("utf-8")),
        "execution": None if execution_capture["truncated"] else result,
        "execution_capture": execution_capture,
        "command_receipt": command_receipt,
        "outputs": captured.get("outputs", {}),
        "native_failure": native_failure_diagnostic(captured.get("stderr", ""), result),
        "retained_bytes_per_output": MAX_FAILURE_OUTPUT_BYTES,
        "output_encoding": "UTF-8 encoding of subprocess decoded text",
    }
    document["failure_sha256"] = u.sha256_bytes(u.canonical_bytes(document))
    encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        # Keep each failed attempt addressable even if a later retry succeeds.
        u.write_immutable(output.parent / f"installed-cognition-failure-{document['failure_sha256']}.json",
                          document)
        output.write_text(encoded, encoding="utf-8")
    if failure_artifact is not None:
        failure_artifact.parent.mkdir(parents=True, exist_ok=True)
        failure_artifact.write_text(encoded, encoding="utf-8")

    # Publish the exact retained identity and validated native counters to the
    # ordinary CI log. Raw SQL, prompt and process output stay in the artifact.
    native = document["native_failure"]
    visible_native = {key: native[key] for key in ("state", "reason") if key in native}
    if isinstance(native.get("diagnostic"), dict):
        visible_native["diagnostic"] = {
            key: native["diagnostic"][key]
            for key in ("schema", *NATIVE_FAILURE_COUNTER_FIELDS)
        }
    print(json.dumps({
        "schema": "laplace.installed-product-cognition-failure-log/v1",
        "label": label,
        "failure_sha256": document["failure_sha256"],
        "native_failure": visible_native,
    }, sort_keys=True), flush=True)


def execute_product(
    plan: dict[str, Any],
    cluster: dict[str, Any],
    identities: dict[str, Any],
    runtime_epochs: dict[str, Any],
    firmware: dict[str, Any],
    prompt: str,
    label: str,
    *,
    failure_output: Path | None = None,
    failure_artifact: Path | None = None,
    failure_provenance: dict[str, Any] | None = None,
    validate_result: Callable[[dict[str, Any]], None] | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    program_id = firmware["program_id"]
    prompt_hex = prompt.encode("utf-8").hex()
    sql = f"""
SET client_min_messages = notice;
SELECT pg_catalog.row_to_json(result)::text
FROM laplace.cognition_firmware_execute_product(
    {context_sql(identities, program_id, runtime_epochs['perfcache_epoch'], runtime_epochs['numeric_epoch'])},
    {bytea_literal(firmware['image_hex'])},
    pg_catalog.convert_from({bytea_literal(prompt_hex)}, 'UTF8'),
    {prompt_scope_sql(identities)},
    {request_sql(identities, program_id)},
    decode('','hex'),
    8388608::bigint
) AS result;
"""
    captured: dict[str, Any] = {}
    result: Any = None
    command_receipt: dict[str, Any] | None = None

    def observe(completed: subprocess.CompletedProcess[str], receipt: dict[str, Any]) -> None:
        captured["command_receipt"] = receipt
        captured["stderr"] = completed.stderr
        captured["outputs"] = {
            name: bounded_output(content)
            for name, content in (("stdout", completed.stdout), ("stderr", completed.stderr))
        }

    try:
        result, command_receipt = r.runner_sql(
            plan,
            cluster,
            sql,
            label,
            "laplace-runner",
            cluster["instance"]["admin_role"],
            300,
            completed_observer=observe,
        )
        if not isinstance(result, dict):
            raise RuntimeError(f"{label} did not return one JSON result")
        print(json.dumps({label: result}, sort_keys=True), flush=True)
        if result.get("status") != 0:
            raise RuntimeError(f"{label} returned status {result.get('status')}: {result}")
        returned_program = bytea_hex(result.get("program_id"), "program_id")
        if returned_program != program_id:
            raise RuntimeError(f"{label} executed a different firmware program")
        if validate_result is not None:
            validate_result(result)
    except (RuntimeError, ValueError) as error:
        retain_execution_failure(
            failure_output,
            failure_artifact=failure_artifact,
            label=label,
            result=result,
            command_receipt=command_receipt or captured.get("command_receipt"),
            captured=captured,
            sql=sql,
            firmware=firmware,
            prompt=prompt,
            provenance=failure_provenance,
            error=str(error),
        )
        raise
    return result, command_receipt


def require_identity_widths(result: dict[str, Any]) -> dict[str, str | None]:
    if "prompt_persistence_receipt_id" not in result:
        raise RuntimeError("product result omitted the prompt publication disposition")
    publication = result["prompt_persistence_receipt_id"]
    values = {
        "program_id": bytea_hex(result.get("program_id"), "program_id"),
        "execution_receipt_id": bytea_hex(
            result.get("execution_receipt_id"), "execution_receipt_id"
        ),
        "output_fingerprint": bytea_hex(
            result.get("output_fingerprint"), "output_fingerprint"
        ),
        "prompt_admission_receipt_id": bytea_hex(
            result.get("prompt_admission_receipt_id"), "prompt_admission_receipt_id"
        ),
        # The native owner returns NULL when canonical prompt content already
        # exists and no producer stream needs publication. Do not invent one.
        "prompt_persistence_receipt_id": (
            None if publication is None else bytea_hex(publication, "prompt_persistence_receipt_id")
        ),
        "trunk_entity_id": bytea_hex(result.get("trunk_entity_id"), "trunk_entity_id"),
    }
    for name, value in values.items():
        if name == "prompt_persistence_receipt_id" and value is None:
            continue
        expected = 32 if name == "trunk_entity_id" else 64
        if len(value) != expected:
            raise RuntimeError(f"{name} has the wrong identity width")
    return values


def require_same_prompt_root(first: dict[str, str | None], replay: dict[str, str | None]) -> None:
    # Both identities have already passed require_identity_widths. The frontier
    # program independently materializes the persisted AA root through PostgreSQL.
    if first["trunk_entity_id"] != replay["trunk_entity_id"]:
        raise RuntimeError("installed prompt replay selected a different canonical root")


def execution_counter(result: dict[str, Any], field: str) -> int:
    value = result.get(field)
    if type(value) is not int or not 0 <= value < (1 << 64):
        raise RuntimeError(f"{field} is not a native unsigned counter")
    return value


def require_constituent_result(result: dict[str, Any]) -> None:
    observed_output = bytes.fromhex(
        bytea_hex(result.get("output"), "output")
    )
    require_identity_widths(result)
    if observed_output != b"A":
        raise RuntimeError(
            f"repeated-constituent proof returned {observed_output!r}, expected canonical A"
        )
    if (
        execution_counter(result, "completed_steps") != 2
        or execution_counter(result, "emitted_parts") != 1
    ):
        raise RuntimeError("firmware did not execute the complete interpret/emit program")
    if execution_counter(result, "provider_call_count") <= 0:
        raise RuntimeError("firmware reported no provider execution")
    if execution_counter(result, "materialization_resolved_nodes") <= 0:
        raise RuntimeError("product materialization resolved no canonical nodes")
    if execution_counter(result, "materialization_database_operations") <= 0:
        raise RuntimeError("product materialization did not execute PostgreSQL readback")

def require_frontier_result(
    result: dict[str, Any], first_identities: dict[str, str | None],
) -> None:
    observed_output = bytes.fromhex(bytea_hex(result.get("output"), "output"))
    identities = require_identity_widths(result)
    require_same_prompt_root(first_identities, identities)
    if observed_output != b"AA":
        raise RuntimeError(
            f"materialization frontier proof returned {observed_output!r}, expected exact AA"
        )
    if execution_counter(result, "completed_steps") != 3 or execution_counter(result, "emitted_parts") != 1:
        raise RuntimeError("materialization frontier firmware did not execute both relation steps and emit")
    resolved_nodes = execution_counter(result, "materialization_resolved_nodes")
    trajectory_reads = execution_counter(result, "materialization_trajectory_reads")
    database_operations = execution_counter(result, "materialization_database_operations")
    trajectory_bytes = execution_counter(result, "materialization_trajectory_bytes")
    # resolved_nodes counts provider resolve calls, not all content entities.
    # Native materialization authenticates embedded Unicode atom identities and
    # appends their packed run directly; A is not resolved a second time.
    if resolved_nodes != 1:
        raise RuntimeError(
            f"materialization frontier resolved {resolved_nodes} nodes, expected one provider-resolved root and inline atom run"
        )
    if trajectory_reads != 1 or trajectory_bytes != 32:
        raise RuntimeError(
            f"materialization frontier trajectory work drifted: reads={trajectory_reads} bytes={trajectory_bytes}"
        )
    # The existing product route pins Unicode before creating this provider:
    # no cold pin work belongs here. The ordinary stored root costs exactly
    # one empty derived-owner inventory, one entity frontier, one physicality
    # frontier and one empty derived-alternative inventory (materialization_pg.c
    # and physicality_entity_pg.c). Extra child work must still fail this proof.
    if database_operations != 4:
        raise RuntimeError(
            "materialization frontier regressed from set-wise persistence: "
            f"database_operations={database_operations}, expected 4"
        )

def prove(output: Path, failure_artifact: Path | None = None,
          compiler_override: Path | None = None) -> None:
    r.require_runner()
    cluster = u.load_json(ROOT / "contracts/postgresql-cluster.json")
    unicode_contract = u.load_json(ROOT / "contracts/unicode-product-activation.json")
    active = Path(cluster["package"]["active_link"]).resolve(strict=True)
    package_id = active.name
    if u.HEX_256.fullmatch(package_id) is None:
        raise RuntimeError("active package identity is invalid")

    receipt_root = Path(cluster["instance"]["receipt_directory"])
    completed = u.load_json(
        receipt_root / "cluster-activation" / package_id / "activation-complete.json"
    )
    plan = u.load_json(Path(completed["cluster_plan_path"]))
    r.clusterctl.validate_plan(plan, cluster)
    loaded = r.clusterctl.observe_loaded_live(plan, cluster, Path("/"))
    r.clusterctl.verify_loaded(plan, cluster, loaded)

    unicode_receipt = u.load_json(receipt_root / "unicode-product-activation.json")
    identities = h.load_unicode_identities(
        cluster, unicode_contract, unicode_receipt, Path("/")
    )
    h.validate_unicode_receipt(unicode_receipt, {"package_id": package_id}, identities)
    runtime_epochs, epoch_command_receipt = active_epoch_state(
        plan, cluster, unicode_receipt
    )
    compiler = (active / "bin/laplace_cognition_firmware_compile"
                if compiler_override is None else compiler_override)
    compiler_selection = {
        "scope": "active-package" if compiler_override is None else "explicit-candidate",
        "identity": compiler_identity(compiler),
        "installed_package_id": package_id,
    }
    failure_provenance = {
        "package_id": package_id,
        "compiler_selection": compiler_selection,
        "system_identifier": loaded["system_identifier"],
        "postmaster_pid": loaded["postmaster_pid"],
        "runtime_epochs": runtime_epochs,
        "runtime_epoch_command_receipt": epoch_command_receipt,
    }

    constituent_firmware, constituent_compiler_receipt = compile_firmware(
        compiler, ("constituent",)
    )
    prompt = "AA"
    constituent_result, constituent_command_receipt = execute_product(
        plan,
        cluster,
        identities,
        runtime_epochs,
        constituent_firmware,
        prompt,
        "installed-product-cognition-falsification",
        failure_output=output,
        failure_artifact=failure_artifact,
        failure_provenance=failure_provenance,
        validate_result=require_constituent_result,
    )
    constituent_output = bytes.fromhex(bytea_hex(constituent_result["output"], "output"))
    constituent_identities = require_identity_widths(constituent_result)

    # AA -> A -> AA proves the stored root and its embedded, identity-checked
    # atom run through the production materialization route. A is shared by other
    # compositions; the program explicitly asks to return to this admitted root.
    # Validation executes
    # inside execute_product so every failed obligation retains actual evidence.
    batch_firmware, batch_compiler_receipt = compile_firmware(
        compiler, ("constituent", "container"), ((1, "observation", 0),)
    )
    batch_result, batch_command_receipt = execute_product(
        plan,
        cluster,
        identities,
        runtime_epochs,
        batch_firmware,
        prompt,
        "installed-product-materialization-frontier-falsification",
        failure_output=output,
        failure_artifact=failure_artifact,
        failure_provenance=failure_provenance,
        validate_result=lambda result: require_frontier_result(result, constituent_identities),
    )
    batch_output = bytes.fromhex(bytea_hex(batch_result["output"], "output"))
    batch_identities = require_identity_widths(batch_result)

    proof = {
        "schema": "laplace.installed-product-cognition-proof/v1",
        "compiler_selection": compiler_selection,
        "package_id": package_id,
        "system_identifier": loaded["system_identifier"],
        "postmaster_pid": loaded["postmaster_pid"],
        "runtime_epochs": runtime_epochs,
        "runtime_epoch_command_receipt": epoch_command_receipt,
        "program": constituent_firmware,
        "compiler_receipt": constituent_compiler_receipt,
        "prompt_utf8": prompt,
        "expected_structural_result_utf8": "A",
        "observed_output_utf8": constituent_output.decode("utf-8"),
        "execution": constituent_result,
        "execution_identities": constituent_identities,
        "prompt_publication_required": constituent_result["prompt_persistence_receipt_id"] is not None,
        "prompt_replay_canonical_root_verified": True,
        "command_receipt": constituent_command_receipt,
        "materialization_frontier": {
            "program": batch_firmware,
            "compiler_receipt": batch_compiler_receipt,
            "expected_output_utf8": "AA",
            "observed_output_utf8": batch_output.decode("utf-8"),
            "execution": batch_result,
            "execution_identities": batch_identities,
            "command_receipt": batch_command_receipt,
            "expected_resolved_nodes": 1,
            "expected_trajectory_reads": 1,
            "expected_trajectory_bytes": 32,
            "resolved_nodes_scope": "provider resolution calls; embedded Unicode atom runs are identity-checked inline",
            "expected_database_operations": 4,
        },
    }
    if compiler_identity(compiler) != compiler_selection["identity"]:
        raise RuntimeError("selected firmware compiler changed during proof")
    proof["proof_sha256"] = u.sha256_bytes(u.canonical_bytes(proof))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(proof, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(proof, sort_keys=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--failure-artifact", type=Path)
    parser.add_argument("--compiler", type=Path,
                        help="explicit candidate compiler; receipt distinguishes it from the active package")
    arguments = parser.parse_args()
    prove(arguments.output, arguments.failure_artifact, arguments.compiler)
