#!/usr/bin/env python3
"""Execute real installed Laplace cognition programs against the persistent product.

This is not a fixture path. It uses the active package's firmware compiler, active
Unicode/context identities, installed PostgreSQL extension, persistent prompt
admission, production indexed cognition providers, exact witnessed realization and
production materialization.

Two independent physical obligations are proven. The repeated-CONSTITUENT program
uses ``AA`` to converge on canonical ``A`` without an expected-answer field. The
CONSTITUENT->CONTAINER program returns the exact ``AA`` composition and proves that
materialization resolves the root plus its repeated child through one frontier cache:
three set-wise PostgreSQL reads total, one cached trajectory read, and no per-child
SPI reread.
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

# LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE. These explicit
# terminal-relation programs range over the admitted prompt and every matching
# persisted composition in the statement snapshot. The indexed provider reads
# one overflow sentinel and rejects incomplete metadata/payload/candidate sets;
# native resource exhaustion and distinct-target ambiguity still refuse output.
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


def compile_firmware(
    executable: Path,
    relations: tuple[str, ...] = ("constituent",),
) -> tuple[dict[str, Any], dict[str, Any]]:
    if not executable.is_file() or executable.is_symlink():
        raise RuntimeError("installed cognition firmware compiler is absent")
    if not relations:
        raise RuntimeError("installed cognition proof requires at least one relation")
    command = [str(executable)]
    if len(relations) == 1:
        command.extend(("--relation", relations[0]))
    else:
        command.extend(("--relations", ",".join(relations)))
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
        or document.get("program") != "relation-chain-exact-witnessed-output"
        or document.get("mode") != "explicit"
        or document.get("relations") != list(relations)
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


def execute_product(
    plan: dict[str, Any],
    cluster: dict[str, Any],
    identities: dict[str, Any],
    runtime_epochs: dict[str, Any],
    firmware: dict[str, Any],
    prompt: str,
    label: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    program_id = firmware["program_id"]
    prompt_hex = prompt.encode("utf-8").hex()
    sql = f"""
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
    result, command_receipt = r.runner_sql(
        plan,
        cluster,
        sql,
        label,
        "laplace-runner",
        cluster["instance"]["admin_role"],
        300,
    )
    if not isinstance(result, dict):
        raise RuntimeError(f"{label} did not return one JSON result")
    print(json.dumps({label: result}, sort_keys=True), flush=True)
    if result.get("status") != 0:
        raise RuntimeError(f"{label} returned status {result.get('status')}: {result}")
    returned_program = bytea_hex(result.get("program_id"), "program_id")
    if returned_program != program_id:
        raise RuntimeError(f"{label} executed a different firmware program")
    return result, command_receipt


def require_identity_widths(result: dict[str, Any]) -> dict[str, str]:
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
        "prompt_persistence_receipt_id": bytea_hex(
            result.get("prompt_persistence_receipt_id"), "prompt_persistence_receipt_id"
        ),
        "trunk_entity_id": bytea_hex(result.get("trunk_entity_id"), "trunk_entity_id"),
    }
    for name, value in values.items():
        expected = 32 if name == "trunk_entity_id" else 64
        if len(value) != expected:
            raise RuntimeError(f"{name} has the wrong identity width")
    return values


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
    runtime_epochs, epoch_command_receipt = active_epoch_state(
        plan, cluster, unicode_receipt
    )
    compiler = active / "bin/laplace_cognition_firmware_compile"

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
    )
    constituent_output = bytes.fromhex(
        bytea_hex(constituent_result.get("output"), "output")
    )
    constituent_identities = require_identity_widths(constituent_result)
    if constituent_output != b"A":
        raise RuntimeError(
            f"repeated-constituent proof returned {constituent_output!r}, expected canonical A"
        )
    if (
        constituent_result.get("completed_steps") != 2
        or constituent_result.get("emitted_parts") != 1
    ):
        raise RuntimeError("firmware did not execute the complete interpret/emit program")
    if int(constituent_result.get("provider_call_count", 0)) <= 0:
        raise RuntimeError("firmware reported no provider execution")
    if int(constituent_result.get("materialization_resolved_nodes", 0)) <= 0:
        raise RuntimeError("product materialization resolved no canonical nodes")
    if int(constituent_result.get("materialization_database_operations", 0)) <= 0:
        raise RuntimeError("product materialization did not execute PostgreSQL readback")

    # Deliberate physical-plan proof. AA -> A -> AA forces one composition
    # materialization containing a repeated atom. The frontier-caching provider must
    # read the composition identity+physicality set-wise, read its cached trajectory
    # once, then resolve unique child A through one entity frontier plus Tier-0 batch.
    # Any return to scalar entity/physicality/trajectory SPI changes these counters.
    batch_firmware, batch_compiler_receipt = compile_firmware(
        compiler, ("constituent", "container")
    )
    batch_result, batch_command_receipt = execute_product(
        plan,
        cluster,
        identities,
        runtime_epochs,
        batch_firmware,
        prompt,
        "installed-product-materialization-frontier-falsification",
    )
    batch_output = bytes.fromhex(bytea_hex(batch_result.get("output"), "output"))
    batch_identities = require_identity_widths(batch_result)
    if batch_output != b"AA":
        raise RuntimeError(
            f"materialization frontier proof returned {batch_output!r}, expected exact AA"
        )
    if batch_result.get("completed_steps") != 3 or batch_result.get("emitted_parts") != 1:
        raise RuntimeError("materialization frontier firmware did not execute both relation steps and emit")
    resolved_nodes = int(batch_result.get("materialization_resolved_nodes", 0))
    trajectory_reads = int(batch_result.get("materialization_trajectory_reads", 0))
    database_operations = int(batch_result.get("materialization_database_operations", 0))
    trajectory_bytes = int(batch_result.get("materialization_trajectory_bytes", 0))
    if resolved_nodes != 2:
        raise RuntimeError(
            f"materialization frontier resolved {resolved_nodes} nodes, expected root plus one unique child"
        )
    if trajectory_reads != 1 or trajectory_bytes <= 0:
        raise RuntimeError(
            f"materialization frontier trajectory work drifted: reads={trajectory_reads} bytes={trajectory_bytes}"
        )
    if database_operations != 3:
        raise RuntimeError(
            "materialization frontier regressed from set-wise persistence: "
            f"database_operations={database_operations}, expected 3"
        )

    proof = {
        "schema": "laplace.installed-product-cognition-proof/v1",
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
        "command_receipt": constituent_command_receipt,
        "materialization_frontier": {
            "program": batch_firmware,
            "compiler_receipt": batch_compiler_receipt,
            "expected_output_utf8": "AA",
            "observed_output_utf8": batch_output.decode("utf-8"),
            "execution": batch_result,
            "execution_identities": batch_identities,
            "command_receipt": batch_command_receipt,
            "expected_resolved_nodes": 2,
            "expected_trajectory_reads": 1,
            "expected_database_operations": 3,
        },
    }
    proof["proof_sha256"] = u.sha256_bytes(u.canonical_bytes(proof))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(proof, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(proof, sort_keys=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    prove(parser.parse_args().output)
