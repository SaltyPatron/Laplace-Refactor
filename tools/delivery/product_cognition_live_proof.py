#!/usr/bin/env python3
"""Execute one real installed Laplace cognition program against the persistent product.

This is not a fixture path. It uses the active package's firmware compiler, active
Unicode/context identities, installed PostgreSQL extension, persistent prompt
admission, production indexed cognition providers, exact witnessed realization and
production materialization. The proof intentionally uses `AA` with a CONSTITUENT
program: two distinct prompt occurrences converge to one canonical `A` entity, so
firmware must resolve the repeated structural evidence without an expected-answer
field or goal injected by this harness.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

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


def compile_firmware(executable: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    if not executable.is_file() or executable.is_symlink():
        raise RuntimeError("installed cognition firmware compiler is absent")
    command = [str(executable), "--relation", "constituent"]
    completed = subprocess.run(command, text=True, capture_output=True, check=False, timeout=30)
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
        or document.get("relation") != "constituent"
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
        "exit_code": completed.returncode,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
    }


def context_sql(identities: dict[str, Any], program_id: str) -> str:
    epoch_names = [
        "source_epoch",
        "identity_epoch",
        "geometry_epoch",
        "evidence_epoch",
        None,
        "dependency_epoch",
        "database_epoch",
        "perfcache_epoch",
        "numeric_epoch",
        "package_epoch",
    ]
    epochs: list[str] = []
    for name in epoch_names:
        value = program_id if name is None else identities[name]
        if not isinstance(value, str) or HEX256.fullmatch(value) is None:
            raise RuntimeError(f"invalid execution epoch: {name or 'firmware_epoch'}")
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
        + ",4096::numeric,8192::numeric,0,1)::laplace.cognition_firmware_product_request"
    )


def prove(output: Path) -> None:
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

    firmware, compiler_receipt = compile_firmware(
        active / "bin/laplace_cognition_firmware_compile"
    )
    program_id = firmware["program_id"]
    prompt = "AA"
    prompt_hex = prompt.encode("utf-8").hex()
    sql = f"""
SELECT pg_catalog.row_to_json(result)::text
FROM laplace.cognition_firmware_execute_product(
    {context_sql(identities, program_id)},
    {bytea_literal(firmware['image_hex'])},
    pg_catalog.convert_from({bytea_literal(prompt_hex)}, 'UTF8'),
    {prompt_scope_sql(identities)},
    {request_sql(identities, program_id)},
    decode('','hex'),
    8388608::bigint
) AS result;
"""
    result, command_receipt = r.runner_sql(
        plan,
        cluster,
        sql,
        "installed-product-cognition-falsification",
        "laplace-runner",
        cluster["instance"]["admin_role"],
        300,
    )
    if not isinstance(result, dict):
        raise RuntimeError("installed cognition route did not return one JSON result")

    status = result.get("status")
    output_hex = bytea_hex(result.get("output"), "output")
    returned_program = bytea_hex(result.get("program_id"), "program_id")
    receipt_hex = bytea_hex(result.get("execution_receipt_id"), "execution_receipt_id")
    output_fingerprint = bytea_hex(result.get("output_fingerprint"), "output_fingerprint")
    prompt_receipt = bytea_hex(
        result.get("prompt_admission_receipt_id"), "prompt_admission_receipt_id"
    )
    prompt_persistence = bytea_hex(
        result.get("prompt_persistence_receipt_id"), "prompt_persistence_receipt_id"
    )
    trunk = bytea_hex(result.get("trunk_entity_id"), "trunk_entity_id")

    observed_output = bytes.fromhex(output_hex)
    if status != 0:
        raise RuntimeError(f"installed cognition returned status {status}: {result}")
    if returned_program != program_id:
        raise RuntimeError("executed program does not match installed compiled firmware")
    if observed_output != b"A":
        raise RuntimeError(
            f"repeated-constituent proof returned {observed_output!r}, expected canonical A"
        )
    if result.get("completed_steps") != 2 or result.get("emitted_parts") != 1:
        raise RuntimeError("firmware did not execute the complete interpret/emit program")
    if int(result.get("provider_call_count", 0)) <= 0:
        raise RuntimeError("firmware reported no provider execution")
    if int(result.get("materialization_resolved_nodes", 0)) <= 0:
        raise RuntimeError("product materialization resolved no canonical nodes")
    if int(result.get("materialization_database_operations", 0)) <= 0:
        raise RuntimeError("product materialization did not execute PostgreSQL readback")
    for name, value, length in (
        ("program_id", returned_program, 64),
        ("execution_receipt_id", receipt_hex, 64),
        ("output_fingerprint", output_fingerprint, 64),
        ("prompt_admission_receipt_id", prompt_receipt, 64),
        ("prompt_persistence_receipt_id", prompt_persistence, 64),
        ("trunk_entity_id", trunk, 32),
    ):
        if len(value) != length:
            raise RuntimeError(f"{name} has the wrong identity width")

    proof = {
        "schema": "laplace.installed-product-cognition-proof/v1",
        "package_id": package_id,
        "system_identifier": loaded["system_identifier"],
        "postmaster_pid": loaded["postmaster_pid"],
        "program": firmware,
        "compiler_receipt": compiler_receipt,
        "prompt_utf8": prompt,
        "expected_structural_result_utf8": "A",
        "observed_output_utf8": observed_output.decode("utf-8"),
        "execution": result,
        "command_receipt": command_receipt,
    }
    proof["proof_sha256"] = u.sha256_bytes(u.canonical_bytes(proof))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(proof, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(proof, sort_keys=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    prove(parser.parse_args().output)
