#!/usr/bin/env python3
"""Execute one real installed Laplace cognition program through its packaged surface.

This is not a fixture path. It verifies the active package and persistent product,
then invokes the installed ``laplace-cognition`` command. That command binds the
active product epochs, persists the exact prompt, executes production indexed native
cognition, performs exact witnessed realization and materializes the result. The
proof intentionally uses ``AA`` with a CONSTITUENT program: two prompt occurrences
converge to one canonical ``A`` entity, so the machine must resolve structural
evidence without an expected-answer field or harness-supplied semantic goal.
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


def execute_installed_surface(
    executable: Path, prompt: str
) -> tuple[dict[str, Any], dict[str, Any]]:
    if not executable.is_file() or executable.is_symlink():
        raise RuntimeError("installed laplace-cognition command is absent")
    command = [str(executable), prompt, "--relation", "constituent", "--json"]
    completed = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=300,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "installed laplace-cognition command failed: "
            f"exit={completed.returncode} stderr={completed.stderr.strip()}"
        )
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise RuntimeError(
            f"installed laplace-cognition returned {len(lines)} non-empty output lines"
        )
    try:
        document = json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise RuntimeError("installed laplace-cognition returned invalid JSON") from error
    if not isinstance(document, dict):
        raise RuntimeError("installed laplace-cognition returned a non-object")
    return document, {
        "argv": command,
        "exit_code": completed.returncode,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
    }


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

    firmware, compiler_receipt = compile_firmware(
        active / "bin/laplace_cognition_firmware_compile"
    )
    program_id = firmware["program_id"]
    prompt = "AA"
    surface, command_receipt = execute_installed_surface(
        active / "bin/laplace-cognition", prompt
    )
    if surface.get("schema") != "laplace.installed-cognition-command/v1":
        raise RuntimeError("installed cognition command returned the wrong receipt schema")
    if surface.get("package_id") != package_id:
        raise RuntimeError("installed cognition command executed a different package")
    if surface.get("relation") != "constituent":
        raise RuntimeError("installed cognition command executed a different relation program")
    if surface.get("program_id") != program_id:
        raise RuntimeError("installed cognition command used different native firmware")
    if surface.get("output_utf8") != "A" or surface.get("output_hex") != "41":
        raise RuntimeError("installed cognition command did not materialize canonical A")
    result = surface.get("execution")
    if not isinstance(result, dict):
        raise RuntimeError("installed cognition command omitted its native execution receipt")

    print(
        json.dumps({"installed_cognition_surface_result": surface}, sort_keys=True),
        flush=True,
    )
    status = result.get("status")
    if status != 0:
        raise RuntimeError(f"installed cognition returned status {status}: {result}")

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
        "runtime_epochs": runtime_epochs,
        "runtime_epoch_command_receipt": epoch_command_receipt,
        "program": firmware,
        "compiler_receipt": compiler_receipt,
        "prompt_utf8": prompt,
        "expected_structural_result_utf8": "A",
        "observed_output_utf8": observed_output.decode("utf-8"),
        "execution": result,
        "surface_receipt": surface,
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
