#!/usr/bin/env python3
"""Prove two consecutive installed product cognition turns share durable discourse state."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "product_cognition_live_proof", ROOT / "tools/delivery/product_cognition_live_proof.py"
)
assert SPEC is not None and SPEC.loader is not None
base = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = base
SPEC.loader.exec_module(base)


def prompt_scope_sql_turn(identities: dict[str, Any], ordinal: int, flags: int) -> str:
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
        if not isinstance(value, str) or base.HEX256.fullmatch(value) is None:
            raise RuntimeError("invalid prompt-scope fingerprint")
    if ordinal < 0 or flags < 0:
        raise RuntimeError("invalid discourse ordinal/flags")
    return (
        "ROW("
        + ",".join(base.bytea_literal(value) for value in values)
        + f",{ordinal}::numeric,{flags},1::numeric,1048576::numeric)"
        "::laplace.cognition_prompt_scope"
    )


def request_sql_turn(
    identities: dict[str, Any],
    program_id: str,
    expected_previous_checkpoint: str | None,
) -> str:
    evidence = identities["evidence_epoch"]
    result_contract = identities["request_fingerprint"]
    for value in (evidence, result_contract, program_id):
        if not isinstance(value, str) or base.HEX256.fullmatch(value) is None:
            raise RuntimeError("invalid firmware request fingerprint")
    previous_present = expected_previous_checkpoint is not None
    previous = expected_previous_checkpoint or ("00" * 32)
    if base.HEX256.fullmatch(previous) is None:
        raise RuntimeError("invalid predecessor checkpoint fingerprint")
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
        + base.bytea_literal(program_id)
        + ","
        + base.bytea_literal(evidence)
        + ","
        + base.bytea_literal(result_contract)
        + ","
        + base.bytea_literal(previous)
        + (",true," if previous_present else ",false,")
        + search
        + ","
        + forward
        + ","
        + materialization
        + ",4096::numeric,8192::numeric,0,1)::laplace.cognition_firmware_product_request"
    )


def execute_turn(
    plan: dict[str, Any],
    cluster: dict[str, Any],
    identities: dict[str, Any],
    runtime_epochs: dict[str, Any],
    firmware: dict[str, Any],
    prompt: str,
    ordinal: int,
    flags: int,
    previous_checkpoint_hex: str = "",
    expected_previous_checkpoint: str | None = None,
    label: str = "installed-product-cognition-turn",
) -> tuple[dict[str, Any], dict[str, Any]]:
    prompt_hex = prompt.encode("utf-8").hex()
    sql = f"""
SELECT pg_catalog.row_to_json(result)::text
FROM laplace.cognition_firmware_execute_product(
    {base.context_sql(identities, firmware['program_id'], runtime_epochs['perfcache_epoch'], runtime_epochs['numeric_epoch'])},
    {base.bytea_literal(firmware['image_hex'])},
    pg_catalog.convert_from({base.bytea_literal(prompt_hex)}, 'UTF8'),
    {prompt_scope_sql_turn(identities, ordinal, flags)},
    {request_sql_turn(identities, firmware['program_id'], expected_previous_checkpoint)},
    {base.bytea_literal(previous_checkpoint_hex)},
    8388608::bigint
) AS result;
"""
    result, command = base.r.runner_sql(
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
    return result, command


def require_success(result: dict[str, Any], expected_output: bytes, label: str) -> None:
    if result.get("status") != 0:
        raise RuntimeError(f"{label} returned status {result.get('status')}: {result}")
    observed = bytes.fromhex(base.bytea_hex(result.get("output"), f"{label}.output"))
    if observed != expected_output:
        raise RuntimeError(f"{label} returned {observed!r}, expected {expected_output!r}")
    if int(result.get("provider_call_count", 0)) <= 0:
        raise RuntimeError(f"{label} reported no provider execution")
    if int(result.get("materialization_resolved_nodes", 0)) <= 0:
        raise RuntimeError(f"{label} resolved no canonical materialization nodes")
    if int(result.get("materialization_database_operations", 0)) <= 0:
        raise RuntimeError(f"{label} did not execute PostgreSQL materialization readback")


def corrupt_digest(value: str) -> str:
    if base.HEX256.fullmatch(value) is None:
        raise RuntimeError("cannot corrupt invalid digest")
    replacement = "1" if value[0] != "1" else "2"
    return replacement + value[1:]


def prove(output: Path) -> None:
    base.r.require_runner()
    cluster = base.u.load_json(ROOT / "contracts/postgresql-cluster.json")
    unicode_contract = base.u.load_json(ROOT / "contracts/unicode-product-activation.json")
    active = Path(cluster["package"]["active_link"]).resolve(strict=True)
    package_id = active.name
    if base.u.HEX_256.fullmatch(package_id) is None:
        raise RuntimeError("active package identity is invalid")

    receipt_root = Path(cluster["instance"]["receipt_directory"])
    completed = base.u.load_json(
        receipt_root / "cluster-activation" / package_id / "activation-complete.json"
    )
    plan = base.u.load_json(Path(completed["cluster_plan_path"]))
    base.r.clusterctl.validate_plan(plan, cluster)
    loaded = base.r.clusterctl.observe_loaded_live(plan, cluster, Path("/"))
    base.r.clusterctl.verify_loaded(plan, cluster, loaded)

    unicode_receipt = base.u.load_json(receipt_root / "unicode-product-activation.json")
    identities = base.h.load_unicode_identities(
        cluster, unicode_contract, unicode_receipt, Path("/")
    )
    base.h.validate_unicode_receipt(unicode_receipt, {"package_id": package_id}, identities)
    runtime_epochs, epoch_command = base.active_epoch_state(plan, cluster, unicode_receipt)
    firmware, compiler_receipt = base.compile_firmware(
        active / "bin/laplace_cognition_firmware_compile"
    )

    first, first_command = execute_turn(
        plan, cluster, identities, runtime_epochs, firmware,
        "AA", 0, 0, label="installed-product-cognition-turn-0",
    )
    require_success(first, b"A", "turn-0")
    first_checkpoint = base.bytea_hex(first.get("checkpoint"), "turn-0.checkpoint")
    first_checkpoint_id = base.bytea_hex(
        first.get("next_checkpoint_fingerprint"), "turn-0.next_checkpoint_fingerprint"
    )
    if not first_checkpoint:
        raise RuntimeError("turn-0 published an empty checkpoint")

    corrupted, corrupted_command = execute_turn(
        plan, cluster, identities, runtime_epochs, firmware,
        "AAA", 1, 1,
        previous_checkpoint_hex=first_checkpoint,
        expected_previous_checkpoint=corrupt_digest(first_checkpoint_id),
        label="installed-product-cognition-corrupt-predecessor",
    )
    if corrupted.get("status") == 0:
        raise RuntimeError("corrupted predecessor checkpoint fingerprint was accepted")

    second, second_command = execute_turn(
        plan, cluster, identities, runtime_epochs, firmware,
        "AAA", 1, 1,
        previous_checkpoint_hex=first_checkpoint,
        expected_previous_checkpoint=first_checkpoint_id,
        label="installed-product-cognition-turn-1",
    )
    require_success(second, b"A", "turn-1")
    second_checkpoint = base.bytea_hex(second.get("checkpoint"), "turn-1.checkpoint")
    second_checkpoint_id = base.bytea_hex(
        second.get("next_checkpoint_fingerprint"), "turn-1.next_checkpoint_fingerprint"
    )
    if not second_checkpoint:
        raise RuntimeError("turn-1 published an empty checkpoint")
    if second_checkpoint_id == first_checkpoint_id:
        raise RuntimeError("turn-1 did not advance the durable checkpoint identity")

    proof = {
        "schema": "laplace.installed-product-cognition-multiturn-proof/v1",
        "package_id": package_id,
        "system_identifier": loaded["system_identifier"],
        "postmaster_pid": loaded["postmaster_pid"],
        "runtime_epochs": runtime_epochs,
        "runtime_epoch_command_receipt": epoch_command,
        "program": firmware,
        "compiler_receipt": compiler_receipt,
        "turn_0": {"prompt_utf8": "AA", "execution": first, "command_receipt": first_command},
        "corrupted_predecessor": {
            "expected_status_nonzero": True,
            "execution": corrupted,
            "command_receipt": corrupted_command,
        },
        "turn_1": {"prompt_utf8": "AAA", "execution": second, "command_receipt": second_command},
    }
    proof["proof_sha256"] = base.u.sha256_bytes(base.u.canonical_bytes(proof))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(proof, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(proof, sort_keys=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    prove(parser.parse_args().output)
