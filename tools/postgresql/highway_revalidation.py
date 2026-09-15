#!/usr/bin/env python3
"""Orchestrate an explicitly selected new proof of committed Highway state.

The native operation owns canonical reconstruction and stored-state validation.
No missing historical input is reconstructed or replaced by this controller.
"""
from __future__ import annotations

import os
from pathlib import Path
from typing import Any

CONTRACT_SCHEMA = "laplace.highway-committed-revalidation-contract/v1"
CONTRACT_SHA256 = "566926c4945029186a04bb2c5bd5cc375520cfa5f86118893d62c9ece3b8f794"
REQUEST_SCHEMA = "laplace.highway-committed-revalidation-request/v1"
RECEIPT_SCHEMA = "laplace.highway-committed-revalidation-receipt/v1"
HEX128_FIELDS = ("root_entity_id", "registry_epoch_id", "unicode_activation_epoch_id")
HEX256_FIELDS = (
    "verification_receipt", "root_physicality_id", "registry_fingerprint",
    "registry_epoch_fingerprint", "stored_isa_receipt", "current_isa_receipt",
    "current_context_fingerprint", "stored_admission_receipt",
    "stored_activation_receipt", "stored_activation_fingerprint",
    "stored_generation_fingerprint", "event_chain_fingerprint",
    "current_working_set_receipt", "current_presence_semantic_receipt",
    "current_presence_execution_receipt", "current_producer_receipt",
    "current_staged_stream_receipt", "current_sink_artifacts_fingerprint",
    "unicode_root_receipt", "unicode_activation_epoch_fingerprint",
    "stored_working_set_receipt", "stored_producer_receipt",
)
COUNT_FIELDS = ("canonical_entity_count", "canonical_physicality_count", "transient_occurrence_count")
HISTORY_FIELDS = ("historical_composition_receipt_present", "historical_intermediate_receipts_verified")
EXPECTED_EPOCH_FIELDS = ("retained_expected_epoch_count", "recovered_expected_epoch_count")
VALUE_FIELDS = ("registry_version", "activation_sequence", "kind_count", "alias_count", "disposition_count",
                *COUNT_FIELDS, *HISTORY_FIELDS, "activation_performed", "status", *EXPECTED_EPOCH_FIELDS)


def write_immutable(h: Any, path: Path, value: dict) -> None:
    """Use the shared immutable evidence publisher with this controller's error type."""
    try:
        h.unicodectl.write_immutable(path, value)
    except h.unicodectl.UnicodeActivationError as error:
        raise h.HighwayActivationError("committed Highway " + str(error)) from error


def validate_contract(h: Any, contract: dict) -> None:
    if (contract.get("schema") != CONTRACT_SCHEMA
            or h.unicodectl.sha256_bytes(h.unicodectl.canonical_bytes(contract)) != CONTRACT_SHA256):
        raise h.HighwayActivationError("committed Highway revalidation contract differs")


def missing_retention(h: Any, root: Path, cluster_contract: dict) -> dict | None:
    directory = h.unicodectl.prefixed(root, cluster_contract["instance"]["receipt_directory"]) / "highway"
    for parent in (directory, *directory.parents):
        if parent.is_symlink():
            raise h.HighwayActivationError("Highway retention boundary contains a symlink")
    if directory.exists() and not directory.is_dir():
        raise h.HighwayActivationError("Highway retention boundary is not a directory")
    entries = []
    if directory.exists():
        with os.scandir(directory) as candidates:
            for entry in candidates:
                if h.unicodectl.HEX_256.fullmatch(entry.name):
                    # Present evidence is always handled by the original strict
                    # replay validator, even when those bytes are invalid.
                    return None
                if entry.name != "failures" or entry.is_symlink() or not entry.is_dir(follow_symlinks=False):
                    raise h.HighwayActivationError("unknown evidence prevents missing-retention selection")
                entries.append(entry.name)
    return {"directory": str(directory), "historical_request_present": False,
            "observed_entries": sorted(entries)}


def render_sql(h: Any, contract: dict, identities: dict, unicode_receipt: dict, inspection: dict) -> str:
    context = h.context_sql(identities, contract,
        unicode_receipt["activation_epoch_fingerprint"], inspection["highway_epoch_fingerprint"], False)
    fields = [f"'{name}',encode({name},'hex')" for name in HEX128_FIELDS + HEX256_FIELDS]
    fields.extend(f"'{name}',{name}" for name in VALUE_FIELDS)
    return ("BEGIN;\nSET LOCAL statement_timeout = '" + str(contract["operation"]["statement_timeout_seconds"]) + "s';\n"
        "WITH proof AS MATERIALIZED (SELECT * FROM laplace.highway_registry_revalidate_committed("
        + context + "," + str(contract["operation"]["preferred_batch_bytes"]) + "::numeric))\n"
        + "SELECT json_build_object(\n  " + ",\n  ".join(fields) + ")::text FROM proof;\nCOMMIT;\n")


def validate_result(h: Any, result: dict, contract: dict, unicode_receipt: dict, inspection: dict) -> None:
    if not isinstance(result, dict) or set(result) != set(HEX128_FIELDS + HEX256_FIELDS + VALUE_FIELDS):
        raise h.HighwayActivationError("native committed Highway result fields differ")
    expected = contract["expected_result"]
    values = {name: expected[name] for name in ("registry_version", "registry_fingerprint", "kind_count", "alias_count", "disposition_count", "status")}
    values.update({"registry_epoch_id": inspection["highway_epoch_id"],
        "registry_epoch_fingerprint": inspection["highway_epoch_fingerprint"],
        "activation_sequence": inspection["highway_sequence"],
        "unicode_activation_epoch_id": unicode_receipt["activation_epoch_id"],
        "unicode_activation_epoch_fingerprint": unicode_receipt["activation_epoch_fingerprint"]})
    if result.get("activation_performed") is not False or any(
            result.get(k) != v or type(result[k]) is not type(v) for k, v in values.items()):
        raise h.HighwayActivationError("native committed Highway revalidation result differs")
    if (type(result["historical_composition_receipt_present"]) is not bool
            or result["historical_intermediate_receipts_verified"] is not False):
        raise h.HighwayActivationError("native committed Highway historical coverage differs")
    if (type(result["activation_sequence"]) is not int
            or not 1 <= result["activation_sequence"] <= 1024
            or any(type(result[name]) is not int or not 0 <= result[name] <= 1024
                   for name in EXPECTED_EPOCH_FIELDS)
            or sum(result[name] for name in EXPECTED_EPOCH_FIELDS) != result["activation_sequence"]):
        raise h.HighwayActivationError("native committed Highway expected epoch coverage differs")
    for names, pattern in ((HEX128_FIELDS, h.unicodectl.HEX_128), (HEX256_FIELDS, h.unicodectl.HEX_256)):
        for name in names:
            if not isinstance(result.get(name), str) or pattern.fullmatch(result[name]) is None or set(result[name]) == {"0"}:
                raise h.HighwayActivationError(f"native committed Highway proof has invalid {name}")
    if any(type(result.get(name)) is not int or result[name] < 0 for name in COUNT_FIELDS):
        raise h.HighwayActivationError("native committed Highway proof has invalid canonical counts")
    if result["canonical_entity_count"] == 0 or result["canonical_physicality_count"] == 0:
        raise h.HighwayActivationError("native committed Highway proof did not reconstruct canonical state")


def execute(h: Any, *, recovery_contract: dict, contract: dict, cluster_contract: dict,
            package: dict, plan: dict, cluster_receipt: dict, unicode_receipt: dict,
            identities: dict, inspection: dict, activation_request: dict, root: Path,
            loaded_before: dict, retention: dict, command_receipts: list,
            sql_runner: Any, loaded_observer: Any, command_runner: Any, readiness_runner: Any) -> dict:
    validate_contract(h, recovery_contract)
    selected = recovery_contract["selection"]
    if (loaded_before["system_identifier"] != selected["system_identifier"]
            or inspection["highway_epoch_id"] != selected["registry_epoch_id"]
            or inspection["highway_epoch_fingerprint"] != selected["registry_epoch_fingerprint"]
            or inspection["highway_sequence"] != selected["activation_sequence"]):
        raise h.HighwayActivationError("committed Highway state is outside the selected recovery boundary")
    sql = render_sql(h, contract, identities, unicode_receipt, inspection)
    epochs = {name: identities[name] for name in ("source_epoch", "identity_epoch", "geometry_epoch", "evidence_epoch",
        "firmware_epoch", "dependency_epoch", "database_epoch", "package_epoch")}
    epochs.update({"perfcache_epoch": unicode_receipt["activation_epoch_fingerprint"],
                   "numeric_epoch": inspection["highway_epoch_fingerprint"]})
    request = {**activation_request, "schema": REQUEST_SCHEMA,
        "orchestrator_sha256": h.unicodectl.sha256_file(Path(__file__).resolve()),
        "activation_orchestrator_sha256": activation_request["orchestrator_sha256"],
        "committed_revalidation_contract_sha256": CONTRACT_SHA256,
        "mode": selected["mode"], "activation_performed": False, "historical_request_present": False,
        "historical_reference": recovery_contract["historical_reference"],
        "retention_observation": retention, "inspected_state": inspection,
        "loaded_before_observation_sha256": loaded_before["observation_sha256"],
        "observed_postmaster_pid": loaded_before["postmaster_pid"],
        "context_epochs": epochs, "authority_fingerprint": identities["authority_fingerprint"],
        "native_function": recovery_contract["native"]["function"],
        "native_result_type": recovery_contract["native"]["result_type"],
        "native_sql_sha256": h.unicodectl.sha256_bytes(sql.encode("utf-8"))}
    request_sha = h.unicodectl.sha256_bytes(h.unicodectl.canonical_bytes(request))
    instance = cluster_contract["instance"]
    evidence = h.unicodectl.prefixed(root, instance["receipt_directory"]) / recovery_contract["receipt"]["directory_name"] / request_sha
    write_immutable(h, evidence / "request.json", request)
    proof, execution = sql_runner(plan, cluster_contract, sql, "revalidate-committed-highway-registry",
        cluster_contract["security"]["admin_os_user"], instance["admin_role"], contract["operation"]["statement_timeout_seconds"] + 120)
    if execution.get("exit_code") != 0 or execution.get("stdin_sha256") != request["native_sql_sha256"]:
        raise h.HighwayActivationError("native committed Highway execution input identity differs")
    command_receipts.append(execution)
    validate_result(h, proof, contract, unicode_receipt, inspection)
    write_immutable(h, evidence / "revalidation-result.json", proof)
    command_receipts.append(command_runner("restart-after-highway-revalidation",
        ["/usr/bin/systemctl", "restart", instance["service"]], contract["operation"]["restart_timeout_seconds"]))
    command_receipts.append(readiness_runner("highway-revalidation-restart-readiness", plan["commands"]["probe_readiness"], 120))
    cold_proof, cold_execution = sql_runner(plan, cluster_contract, sql,
        "cold-revalidate-committed-highway-registry", cluster_contract["security"]["admin_os_user"],
        instance["admin_role"], contract["operation"]["statement_timeout_seconds"] + 120)
    if (cold_execution.get("exit_code") != 0
            or cold_execution.get("stdin_sha256") != request["native_sql_sha256"]
            or h.unicodectl.canonical_bytes(cold_proof) != h.unicodectl.canonical_bytes(proof)):
        raise h.HighwayActivationError("cold native committed Highway proof differs")
    command_receipts.append(cold_execution)
    write_immutable(h, evidence / "cold-revalidation-result.json", cold_proof)
    # The public readback reports stored original activation identity. Current
    # verification receipts are deliberately never substituted into that view.
    committed = {**proof, "activation_receipt": proof["stored_activation_receipt"],
                 "activation_fingerprint": proof["stored_activation_fingerprint"]}
    readback, read_command = sql_runner(plan, cluster_contract,
        h.render_readback_sql(contract, identities, unicode_receipt, committed),
        "cold-application-highway-revalidation-readback", cluster_contract["security"]["app_os_user"], instance["app_role"], 300)
    command_receipts.append(read_command)
    h.validate_readback(readback, contract, unicode_receipt, committed)
    read_isa = readback.get("isa_receipt")
    if not isinstance(read_isa, str) or h.unicodectl.HEX_256.fullmatch(read_isa) is None or set(read_isa) == {"0"}:
        raise h.HighwayActivationError("cold Highway read operation lacks its current ISA receipt")
    final_inspection, inspect_command = sql_runner(plan, cluster_contract, h.render_inspection_sql(),
        "inspect-highway-after-committed-revalidation", cluster_contract["security"]["admin_os_user"], instance["admin_role"], 120)
    command_receipts.append(inspect_command)
    if final_inspection != inspection:
        raise h.HighwayActivationError("committed Highway state changed across verification and restart")
    loaded_after = loaded_observer(plan, cluster_contract, root)
    h.clusterctl.verify_loaded(plan, cluster_contract, loaded_after)
    if (loaded_after["system_identifier"] != loaded_before["system_identifier"]
            or loaded_after.get("postmaster_pid") == loaded_before.get("postmaster_pid")
            or loaded_after["loaded_objects"] != loaded_before["loaded_objects"]
            or loaded_after["config_files"] != loaded_before["config_files"]):
        raise h.HighwayActivationError("revalidation restart changed identity or retained the old postmaster")
    if any(type(item.get("exit_code")) is not int or item["exit_code"] != 0 for item in command_receipts):
        raise h.HighwayActivationError("committed Highway verification command did not succeed")
    receipt = {"schema": RECEIPT_SCHEMA, "phase": "committed-state-revalidated",
        "request_sha256": request_sha, "revalidation_request": request,
        "committed_revalidation_contract_sha256": CONTRACT_SHA256,
        "mode": selected["mode"], "activation_performed": False, "historical_request_present": False,
        "historical_reference": recovery_contract["historical_reference"],
        **{name: request[name] for name in ("package_id", "cluster_plan_sha256", "cluster_activation_receipt_sha256", "unicode_activation_receipt_sha256", "system_identifier")},
        "revalidation": proof, "cold_revalidation": cold_proof, "readback": readback,
        "restart_proven": True, "cold_application_readback_proven": True,
        "loaded_before_observation_sha256": loaded_before["observation_sha256"],
        "loaded_after_observation_sha256": loaded_after["observation_sha256"],
        "command_receipts": command_receipts}
    receipt["receipt_sha256"] = h.unicodectl.document_identity(receipt, "receipt_sha256")
    write_immutable(h, evidence / "readback.json", readback)
    write_immutable(h, evidence / "receipt.json", receipt)
    return receipt
