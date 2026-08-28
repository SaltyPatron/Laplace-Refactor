#!/usr/bin/env python3
"""Admit, activate, restart, and read back the canonical Highway registry.

This product-lifecycle controller composes public PostgreSQL operations. Registry
materialization, Unicode resolution, universal-AST construction, canonical
deposition, activation, and readback validation remain in the native engine and
PostgreSQL extension.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import sys
from pathlib import Path
from typing import Any, Callable, Sequence


def load_sibling(module_name: str, filename: str) -> Any:
    path = Path(__file__).with_name(filename)
    specification = importlib.util.spec_from_file_location(module_name, path)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"cannot load controller {filename}")
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


clusterctl = load_sibling("laplace_highway_clusterctl", "clusterctl.py")
unicodectl = load_sibling("laplace_highway_unicodectl", "unicodectl.py")

CONTRACT_SCHEMA = "laplace.highway-product-activation-contract/v1"
RECEIPT_SCHEMA = "laplace.highway-product-activation-receipt/v1"
FAILURE_SCHEMA = "laplace.highway-product-activation-failure/v1"
REGISTRY_SCHEMA = "laplace.highway-registry-contract/v1"
ZERO_256 = "0" * 64


class HighwayActivationError(RuntimeError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        return unicodectl.load_json(path)
    except Exception as error:
        raise HighwayActivationError(str(error)) from error


def validate_contract(
    contract: dict[str, Any],
    cluster_contract: dict[str, Any],
    unicode_contract: dict[str, Any],
    registry_contract: dict[str, Any],
    previous_registry_contract: dict[str, Any],
) -> None:
    if contract.get("schema") != CONTRACT_SCHEMA or contract.get("version") != "1.0.0":
        raise HighwayActivationError("Highway product activation contract is invalid")
    authority = contract.get("authority")
    expected_authority = {
        "cluster_contract_schema": clusterctl.CONTRACT_SCHEMA,
        "cluster_activation_receipt_schema": clusterctl.ACTIVATION_SCHEMA,
        "cluster_plan_schema": clusterctl.PLAN_SCHEMA,
        "package_manifest_schema": clusterctl.PACKAGE_SCHEMA,
        "unicode_activation_receipt_schema": unicodectl.RECEIPT_SCHEMA,
        "unicode_activation_contract_schema": unicodectl.CONTRACT_SCHEMA,
        "highway_registry_contract_schema": REGISTRY_SCHEMA,
        "highway_registry_contract_sha256": "c71eecdb1982f3ce5b3a85cef13ad143523f20d14f8b30bfc97a92fd99ba49ed",
        "highway_registry_fingerprint": "1b5a1f5a2177c18256ec55bdc403da557ab3dc0003fcdee21784c21ea85f56b5",
        "highway_registry_version": 2,
        "predecessor_contract_path": "contracts/history/highway-v1.json",
        "predecessor_contract_sha256": "da4930ad76eca61fabfbd880f129a2223f5478744c056a9c4d312ea93ddf4bf7",
        "predecessor_registry_fingerprint": "14c9b853a6beade10617cf702a7cf080a23d5e059090e5e95d682738c1fb6843",
        "predecessor_registry_version": 1,
        "predecessor_kind_count": 16,
        "predecessor_alias_count": 0,
        "predecessor_disposition_count": 8,
    }
    if authority != expected_authority:
        raise HighwayActivationError("Highway activation authority boundary drifted")
    if contract.get("orchestrator") != {
        "repository_path": "tools/postgresql/highwayctl.py",
        "classification": "typed-product-lifecycle-orchestrator-not-semantic-engine",
        "bind_exact_bytes_into_request": True,
    }:
        raise HighwayActivationError("Highway product orchestrator boundary differs")
    clusterctl.validate_contract(cluster_contract)
    if unicode_contract.get("schema") != authority["unicode_activation_contract_schema"]:
        raise HighwayActivationError("Unicode activation contract schema differs")
    if registry_contract.get("schema") != REGISTRY_SCHEMA:
        raise HighwayActivationError("Highway registry contract schema differs")
    if unicodectl.sha256_bytes(unicodectl.canonical_bytes(registry_contract)) != authority[
        "highway_registry_contract_sha256"
    ]:
        raise HighwayActivationError("Highway registry contract bytes differ")
    if registry_contract.get("version") != authority["highway_registry_version"]:
        raise HighwayActivationError("Highway registry version differs")
    if previous_registry_contract.get("schema") != REGISTRY_SCHEMA:
        raise HighwayActivationError("Highway predecessor registry contract schema differs")
    if unicodectl.sha256_bytes(
        unicodectl.canonical_bytes(previous_registry_contract)
    ) != authority["predecessor_contract_sha256"]:
        raise HighwayActivationError("Highway predecessor registry contract bytes differ")
    if previous_registry_contract.get("version") != authority["predecessor_registry_version"]:
        raise HighwayActivationError("Highway predecessor registry version differs")
    predecessor_kinds = previous_registry_contract.get("kinds")
    predecessor_dispositions = previous_registry_contract.get("dispositions")
    if (
        not isinstance(predecessor_kinds, list)
        or len(predecessor_kinds) != authority["predecessor_kind_count"]
        or sum(len(kind.get("aliases", [])) for kind in predecessor_kinds)
        != authority["predecessor_alias_count"]
        or not isinstance(predecessor_dispositions, list)
        or len(predecessor_dispositions) != authority["predecessor_disposition_count"]
    ):
        raise HighwayActivationError("Highway predecessor registry cardinality differs")
    operation = contract.get("operation")
    if operation != {
        "initial_activation_only": False,
        "fresh_expected_old_sequence": 0,
        "fresh_activation_sequence": 1,
        "successor_expected_old_sequence": 1,
        "successor_activation_sequence": 2,
        "preferred_batch_bytes": 1048576,
        "statement_timeout_seconds": 900,
        "restart_timeout_seconds": 240,
    }:
        raise HighwayActivationError("Highway activation operation differs")
    if contract.get("execution_context") != {
        "memory_bytes": 1073741824,
        "cpu_slots": 6,
        "io_slots": 2,
        "epoch_mask": 1023,
        "framework_major": 1,
        "framework_minor": 6,
        "activation_flags": 1,
        "read_flags": 2,
    }:
        raise HighwayActivationError("Highway activation execution context differs")
    expected = contract.get("expected_result")
    if expected != {
        "registry_version": 2,
        "registry_fingerprint": authority["highway_registry_fingerprint"],
        "kind_count": 17,
        "alias_count": 0,
        "disposition_count": 8,
        "fresh_activation_sequence": 1,
        "successor_activation_sequence": 2,
        "effect_disposition": 3,
        "status": 0,
    }:
        raise HighwayActivationError("Highway expected product result differs")
    receipt = contract.get("receipt", {})
    if (
        receipt.get("schema") != RECEIPT_SCHEMA
        or receipt.get("failure_schema") != FAILURE_SCHEMA
        or receipt.get("directory_name") != "highway"
    ):
        raise HighwayActivationError("Highway activation receipt boundary differs")


def validate_unicode_receipt(
    receipt: dict[str, Any], package: dict[str, Any], identities: dict[str, Any]
) -> None:
    if (
        receipt.get("schema") != unicodectl.RECEIPT_SCHEMA
        or receipt.get("phase") != "product-activated"
        or receipt.get("package_id") != package.get("package_id")
        or receipt.get("restart_proven") is not True
        or receipt.get("cold_public_readback_proven") is not True
        or receipt.get("reverse_inversion_proven") is not True
        or receipt.get("receipt_sha256")
        != unicodectl.document_identity(receipt, "receipt_sha256")
        or receipt.get("activation_epoch_id") != identities.get("activation_epoch_id")
        or receipt.get("activation_epoch_fingerprint")
        != identities.get("activation_epoch_fingerprint")
    ):
        raise HighwayActivationError("Unicode product activation receipt is not exact and complete")


def load_unicode_identities(
    cluster_contract: dict[str, Any], unicode_contract: dict[str, Any],
    unicode_receipt: dict[str, Any], root: Path,
) -> dict[str, Any]:
    fingerprint = unicode_receipt.get("request_fingerprint")
    if not isinstance(fingerprint, str) or unicodectl.HEX_256.fullmatch(fingerprint) is None:
        raise HighwayActivationError("Unicode activation request fingerprint is invalid")
    receipt_root = unicodectl.prefixed(root, cluster_contract["instance"]["receipt_directory"])
    path = receipt_root / unicode_contract["receipt"]["directory_name"] / fingerprint / "identities.json"
    identities = load_json(path)
    try:
        unicodectl.validate_identities(identities, unicode_contract)
    except Exception as error:
        raise HighwayActivationError(str(error)) from error
    return identities


def context_sql(
    identities: dict[str, Any], contract: dict[str, Any],
    perfcache_epoch: str, numeric_epoch: str, read_only: bool,
) -> str:
    epoch_names = [
        "source_epoch", "identity_epoch", "geometry_epoch", "evidence_epoch",
        "firmware_epoch", "dependency_epoch", "database_epoch",
    ]
    epochs = [identities[name] for name in epoch_names]
    epochs.extend([perfcache_epoch, numeric_epoch, identities["package_epoch"]])
    if any(unicodectl.HEX_256.fullmatch(str(value)) is None for value in epochs):
        raise HighwayActivationError("Highway execution context epoch is invalid")
    settings = contract["execution_context"]
    flags = settings["read_flags" if read_only else "activation_flags"]
    encoded = ",".join(f"decode('{value}','hex')" for value in epochs)
    return (
        "ROW(ARRAY[" + encoded + "],"
        + f"decode('{identities['authority_fingerprint']}','hex'),"
        + f"{settings['memory_bytes']}::bigint,{settings['cpu_slots']},"
        + f"{settings['io_slots']},{settings['epoch_mask']}::bigint,"
        + f"{settings['framework_major']}::smallint,"
        + f"{settings['framework_minor']}::smallint,{flags}::integer)"
        + "::laplace.execution_context"
    )


def render_inspection_sql() -> str:
    return """SELECT json_build_object(
  'unicode_present', u.active_present,
  'unicode_epoch_id', encode(u.activation_epoch_id,'hex'),
  'unicode_epoch_fingerprint', encode(u.epoch_fingerprint,'hex'),
  'highway_sequence', h.sequence,
  'highway_present', h.active_present,
  'highway_epoch_id', encode(h.activation_epoch_id,'hex'),
  'highway_epoch_fingerprint', encode(h.activation_epoch_fingerprint,'hex'),
  'active_registry_version', g.registry_version,
  'active_registry_fingerprint', encode(g.registry_fingerprint,'hex'),
  'active_kind_count', g.kind_count,
  'active_alias_count', g.alias_count,
  'active_disposition_count', g.disposition_count,
  'active_kind_projection_count', (SELECT count(*) FROM laplace.highway_registry_kind_projection p WHERE p.activation_epoch_id=h.activation_epoch_id AND p.activation_epoch_fingerprint=h.activation_epoch_fingerprint),
  'active_alias_projection_count', (SELECT count(*) FROM laplace.highway_registry_alias_projection p WHERE p.activation_epoch_id=h.activation_epoch_id AND p.activation_epoch_fingerprint=h.activation_epoch_fingerprint),
  'active_disposition_projection_count', (SELECT count(*) FROM laplace.highway_registry_disposition_projection p WHERE p.activation_epoch_id=h.activation_epoch_id AND p.activation_epoch_fingerprint=h.activation_epoch_fingerprint),
  'generation_count', (SELECT count(*) FROM laplace.highway_registry_generation),
  'event_count', (SELECT count(*) FROM laplace.highway_registry_activation_event)
)::text
FROM laplace.perfcache_active_control u
CROSS JOIN laplace.highway_registry_active_control h
LEFT JOIN laplace.highway_registry_generation g
  ON h.active_present
 AND g.activation_epoch_id=h.activation_epoch_id
 AND g.activation_epoch_fingerprint=h.activation_epoch_fingerprint
WHERE u.singleton AND h.singleton;
"""


def validate_inspection(
    value: dict[str, Any], unicode_receipt: dict[str, Any], contract: dict[str, Any]
) -> dict[str, Any]:
    if (
        value.get("unicode_present") is not True
        or value.get("unicode_epoch_id") != unicode_receipt["activation_epoch_id"]
        or value.get("unicode_epoch_fingerprint")
        != unicode_receipt["activation_epoch_fingerprint"]
    ):
        raise HighwayActivationError("active Unicode state differs from its product receipt")
    zero_epoch = "00" * 16
    if (
        value.get("highway_sequence") == 0
        and value.get("highway_present") is False
        and value.get("highway_epoch_id") == zero_epoch
        and value.get("highway_epoch_fingerprint") == ZERO_256
        and value.get("active_registry_version") is None
        and value.get("active_registry_fingerprint") is None
        and value.get("active_kind_count") is None
        and value.get("active_alias_count") is None
        and value.get("active_disposition_count") is None
        and value.get("active_kind_projection_count") == 0
        and value.get("active_alias_projection_count") == 0
        and value.get("active_disposition_projection_count") == 0
        and value.get("generation_count") == 0
        and value.get("event_count") == 0
    ):
        return {
            "mode": "fresh",
            "numeric_epoch": ZERO_256,
            "expected_activation_sequence": contract["operation"]["fresh_activation_sequence"],
            "expected_generation_count": 1,
            "expected_event_count": 1,
        }
    if (
        value.get("highway_present") is not True
        or unicodectl.HEX_128.fullmatch(str(value.get("highway_epoch_id", ""))) is None
        or unicodectl.HEX_256.fullmatch(
            str(value.get("highway_epoch_fingerprint", ""))
        ) is None
    ):
        raise HighwayActivationError("Highway product state is partial, conflicting, or not replayable")
    authority = contract["authority"]
    operation = contract["operation"]
    expected = contract["expected_result"]
    predecessor_exact = (
        value.get("highway_sequence") == operation["successor_expected_old_sequence"]
        and value.get("generation_count") == 1
        and value.get("event_count") == 1
        and value.get("active_registry_version") == authority["predecessor_registry_version"]
        and value.get("active_registry_fingerprint") == authority["predecessor_registry_fingerprint"]
        and value.get("active_kind_count") == authority["predecessor_kind_count"]
        and value.get("active_alias_count") == authority["predecessor_alias_count"]
        and value.get("active_disposition_count") == authority["predecessor_disposition_count"]
        and value.get("active_kind_projection_count") == authority["predecessor_kind_count"]
        and value.get("active_alias_projection_count") == authority["predecessor_alias_count"]
        and value.get("active_disposition_projection_count") == authority["predecessor_disposition_count"]
    )
    if predecessor_exact:
        return {
            "mode": "successor",
            "numeric_epoch": value["highway_epoch_fingerprint"],
            "expected_activation_sequence": operation["successor_activation_sequence"],
            "expected_generation_count": 2,
            "expected_event_count": 2,
        }
    active_sequence = value.get("highway_sequence")
    target_exact = (
        active_sequence in {
            expected["fresh_activation_sequence"],
            expected["successor_activation_sequence"],
        }
        and value.get("generation_count") == active_sequence
        and value.get("event_count") == active_sequence
        and value.get("active_registry_version") == expected["registry_version"]
        and value.get("active_registry_fingerprint") == expected["registry_fingerprint"]
        and value.get("active_kind_count") == expected["kind_count"]
        and value.get("active_alias_count") == expected["alias_count"]
        and value.get("active_disposition_count") == expected["disposition_count"]
        and value.get("active_kind_projection_count") == expected["kind_count"]
        and value.get("active_alias_projection_count") == expected["alias_count"]
        and value.get("active_disposition_projection_count") == expected["disposition_count"]
    )
    if target_exact:
        return {
            "mode": "replay",
            "numeric_epoch": value["highway_epoch_fingerprint"],
            "expected_activation_sequence": active_sequence,
            "expected_generation_count": active_sequence,
            "expected_event_count": active_sequence,
        }
    raise HighwayActivationError("Highway product state is partial, conflicting, or not replayable")


def render_activation_sql(
    contract: dict[str, Any], identities: dict[str, Any],
    unicode_receipt: dict[str, Any], state: dict[str, Any],
) -> str:
    operation = contract["operation"]
    expected = contract["expected_result"]
    context = context_sql(
        identities, contract, unicode_receipt["activation_epoch_fingerprint"],
        state["numeric_epoch"], False,
    )
    activation_sequence = state["expected_activation_sequence"]
    return f"""BEGIN;
SET LOCAL statement_timeout = '{operation['statement_timeout_seconds']}s';
SET LOCAL lock_timeout = '30s';
CREATE TEMP TABLE highway_product_activation AS
SELECT activated.* FROM laplace.highway_registry_admit_and_activate(
  {context}, {operation['preferred_batch_bytes']}::numeric) AS activated;
DO $verify$
DECLARE activated highway_product_activation%ROWTYPE;
BEGIN
  SELECT * INTO STRICT activated FROM highway_product_activation;
  IF activated.status <> {expected['status']}
     OR activated.registry_version <> {expected['registry_version']}
     OR activated.registry_fingerprint <> decode('{expected['registry_fingerprint']}','hex')
     OR activated.activation_sequence <> {activation_sequence}
     OR activated.effect_disposition <> {expected['effect_disposition']}
     OR activated.unicode_activation_epoch_id <> decode('{unicode_receipt['activation_epoch_id']}','hex')
     OR activated.unicode_activation_epoch_fingerprint <> decode('{unicode_receipt['activation_epoch_fingerprint']}','hex')
     OR cardinality(activated.kind_name_entity_ids) <> {expected['kind_count']}
     OR cardinality(activated.alias_name_entity_ids) <> {expected['alias_count']}
     OR cardinality(activated.disposition_name_entity_ids) <> {expected['disposition_count']}
     OR NOT EXISTS (SELECT 1 FROM laplace.canonical_entity WHERE entity_id=activated.root_entity_id)
     OR NOT EXISTS (SELECT 1 FROM laplace.physicality WHERE physicality_id=activated.root_physicality_id AND entity_id=activated.root_entity_id)
     OR NOT EXISTS (SELECT 1 FROM laplace.highway_registry_active_control WHERE singleton AND active_present AND sequence=activated.activation_sequence AND activation_receipt=activated.activation_receipt)
     OR (SELECT count(*) FROM laplace.highway_registry_kind_projection) <> {expected['kind_count']}
     OR (SELECT count(*) FROM laplace.highway_registry_alias_projection) <> {expected['alias_count']}
     OR (SELECT count(*) FROM laplace.highway_registry_disposition_projection) <> {expected['disposition_count']} THEN
    RAISE EXCEPTION 'Highway product activation violates its exact contract';
  END IF;
END
$verify$;
SELECT json_build_object(
  'registry_version',registry_version,
  'registry_fingerprint',encode(registry_fingerprint,'hex'),
  'registry_epoch_id',encode(registry_epoch_id,'hex'),
  'registry_epoch_fingerprint',encode(registry_epoch_fingerprint,'hex'),
  'root_entity_id',encode(root_entity_id,'hex'),
  'root_physicality_id',encode(root_physicality_id,'hex'),
  'isa_receipt',encode(isa_receipt,'hex'),
  'unicode_root_receipt',encode(unicode_root_receipt,'hex'),
  'unicode_activation_epoch_id',encode(unicode_activation_epoch_id,'hex'),
  'unicode_activation_epoch_fingerprint',encode(unicode_activation_epoch_fingerprint,'hex'),
  'working_set_receipt',encode(working_set_receipt,'hex'),
  'presence_semantic_receipt',encode(presence_semantic_receipt,'hex'),
  'presence_execution_receipt',encode(presence_execution_receipt,'hex'),
  'producer_receipt',encode(producer_receipt,'hex'),
  'staged_stream_receipt',encode(staged_stream_receipt,'hex'),
  'sink_artifacts_fingerprint',encode(sink_artifacts_fingerprint,'hex'),
  'admission_receipt',encode(admission_receipt,'hex'),
  'activation_receipt',encode(activation_receipt,'hex'),
  'activation_fingerprint',encode(activation_fingerprint,'hex'),
  'activation_sequence',activation_sequence,
  'effect_disposition',effect_disposition,
  'kind_count',cardinality(kind_name_entity_ids),
  'alias_count',cardinality(alias_name_entity_ids),
  'disposition_count',cardinality(disposition_name_entity_ids),
  'entity_inserted',entity_inserted,
  'physicality_inserted',physicality_inserted,
  'trajectory_vertex_inserted',trajectory_vertex_inserted,
  'occurrence_inserted',occurrence_inserted,
  'plan_sequence_fingerprint',encode(plan_sequence_fingerprint,'hex'),
  'plan_count',plan_count,
  'status',status
)::text FROM highway_product_activation;
COMMIT;
"""


def validate_activation_result(
    result: dict[str, Any], contract: dict[str, Any],
    unicode_receipt: dict[str, Any], state: dict[str, Any],
) -> None:
    expected = contract["expected_result"]
    if any(
        result.get(name) != value
        for name, value in {
            "registry_version": expected["registry_version"],
            "registry_fingerprint": expected["registry_fingerprint"],
            "activation_sequence": state["expected_activation_sequence"],
            "effect_disposition": expected["effect_disposition"],
            "kind_count": expected["kind_count"],
            "alias_count": expected["alias_count"],
            "disposition_count": expected["disposition_count"],
            "status": expected["status"],
            "unicode_activation_epoch_id": unicode_receipt["activation_epoch_id"],
            "unicode_activation_epoch_fingerprint": unicode_receipt["activation_epoch_fingerprint"],
        }.items()
    ):
        raise HighwayActivationError("Highway activation result differs")
    for field, pattern in (
        ("registry_epoch_id", unicodectl.HEX_128),
        ("root_entity_id", unicodectl.HEX_128),
        ("registry_epoch_fingerprint", unicodectl.HEX_256),
        ("root_physicality_id", unicodectl.HEX_256),
        ("isa_receipt", unicodectl.HEX_256),
        ("unicode_root_receipt", unicodectl.HEX_256),
        ("working_set_receipt", unicodectl.HEX_256),
        ("presence_semantic_receipt", unicodectl.HEX_256),
        ("presence_execution_receipt", unicodectl.HEX_256),
        ("producer_receipt", unicodectl.HEX_256),
        ("staged_stream_receipt", unicodectl.HEX_256),
        ("sink_artifacts_fingerprint", unicodectl.HEX_256),
        ("admission_receipt", unicodectl.HEX_256),
        ("activation_receipt", unicodectl.HEX_256),
        ("activation_fingerprint", unicodectl.HEX_256),
        ("plan_sequence_fingerprint", unicodectl.HEX_256),
    ):
        if pattern.fullmatch(str(result.get(field, ""))) is None:
            raise HighwayActivationError(f"Highway activation result has invalid {field}")
    inserted = [
        result.get("entity_inserted"), result.get("physicality_inserted"),
        result.get("trajectory_vertex_inserted"), result.get("occurrence_inserted"),
    ]
    if any(not isinstance(value, int) or value < 0 for value in inserted):
        raise HighwayActivationError("Highway activation write counts are invalid")
    if state["mode"] in {"fresh", "successor"} and any(value <= 0 for value in inserted):
        raise HighwayActivationError(
            f"{state['mode']} Highway activation did not write its canonical state"
        )
    if state["mode"] == "replay" and any(value != 0 for value in inserted):
        raise HighwayActivationError("Highway activation replay wrote duplicate canonical state")


def render_readback_sql(
    contract: dict[str, Any], identities: dict[str, Any],
    unicode_receipt: dict[str, Any], activation: dict[str, Any],
) -> str:
    context = context_sql(
        identities, contract, unicode_receipt["activation_epoch_fingerprint"],
        activation["registry_epoch_fingerprint"], True,
    )
    return f"""SELECT json_build_object(
  'registry_version',r.registry_version,
  'registry_fingerprint',encode(r.registry_fingerprint,'hex'),
  'registry_epoch_id',encode(r.registry_epoch_id,'hex'),
  'registry_epoch_fingerprint',encode(r.registry_epoch_fingerprint,'hex'),
  'root_entity_id',encode(r.root_entity_id,'hex'),
  'root_physicality_id',encode(r.root_physicality_id,'hex'),
  'activation_sequence',r.activation_sequence,
  'activation_receipt',encode(r.activation_receipt,'hex'),
  'activation_fingerprint',encode(r.activation_fingerprint,'hex'),
  'isa_receipt',encode(r.isa_receipt,'hex'),
  'kind_ids',r.kind_ids,
  'kind_name_entity_ids',ARRAY(SELECT encode(value,'hex') FROM unnest(r.kind_name_entity_ids) value),
  'alias_kind_ids',r.alias_kind_ids,
  'alias_name_entity_ids',ARRAY(SELECT encode(value,'hex') FROM unnest(r.alias_name_entity_ids) value),
  'disposition_ids',r.disposition_ids,
  'disposition_name_entity_ids',ARRAY(SELECT encode(value,'hex') FROM unnest(r.disposition_name_entity_ids) value),
  'unicode_activation_epoch_id',encode(r.unicode_activation_epoch_id,'hex'),
  'unicode_activation_epoch_fingerprint',encode(r.unicode_activation_epoch_fingerprint,'hex'),
  'status',r.status
)::text FROM laplace.highway_registry_resolve_active({context}) r;
"""


def validate_readback(
    value: dict[str, Any], contract: dict[str, Any],
    unicode_receipt: dict[str, Any], activation: dict[str, Any],
) -> None:
    expected = contract["expected_result"]
    exact = {
        "registry_version": expected["registry_version"],
        "registry_fingerprint": expected["registry_fingerprint"],
        "registry_epoch_id": activation["registry_epoch_id"],
        "registry_epoch_fingerprint": activation["registry_epoch_fingerprint"],
        "root_entity_id": activation["root_entity_id"],
        "root_physicality_id": activation["root_physicality_id"],
        "activation_sequence": activation["activation_sequence"],
        "activation_receipt": activation["activation_receipt"],
        "activation_fingerprint": activation["activation_fingerprint"],
        "unicode_activation_epoch_id": unicode_receipt["activation_epoch_id"],
        "unicode_activation_epoch_fingerprint": unicode_receipt["activation_epoch_fingerprint"],
        "status": expected["status"],
    }
    if any(value.get(name) != expected_value for name, expected_value in exact.items()):
        raise HighwayActivationError("application-role Highway readback differs from activation")
    arrays = {
        "kind_ids": expected["kind_count"],
        "kind_name_entity_ids": expected["kind_count"],
        "alias_kind_ids": expected["alias_count"],
        "alias_name_entity_ids": expected["alias_count"],
        "disposition_ids": expected["disposition_count"],
        "disposition_name_entity_ids": expected["disposition_count"],
    }
    for field, count in arrays.items():
        if not isinstance(value.get(field), list) or len(value[field]) != count:
            raise HighwayActivationError(f"application-role Highway readback differs: {field}")


def execute_highway_activation(
    contract: dict[str, Any], cluster_contract: dict[str, Any],
    unicode_contract: dict[str, Any], registry_contract: dict[str, Any],
    previous_registry_contract: dict[str, Any],
    package: dict[str, Any], plan: dict[str, Any], cluster_receipt: dict[str, Any],
    unicode_receipt: dict[str, Any], root: Path, authorize_system_root: bool,
    *, sql_runner: Callable[..., tuple[dict[str, Any], dict[str, Any]]] = unicodectl.run_psql,
    loaded_observer: Callable[..., dict[str, Any]] = clusterctl.observe_loaded_live,
    command_runner: Callable[..., dict[str, Any]] = clusterctl.execute_activation_command,
    readiness_runner: Callable[..., dict[str, Any]] = clusterctl.await_postgresql_ready,
) -> dict[str, Any]:
    clusterctl.require_fixture_or_root(root, authorize_system_root)
    validate_contract(
        contract, cluster_contract, unicode_contract, registry_contract,
        previous_registry_contract,
    )
    try:
        unicodectl.validate_product_boundary(
            cluster_contract, package, plan, cluster_receipt, root
        )
    except Exception as error:
        raise HighwayActivationError(str(error)) from error
    identities = load_unicode_identities(
        cluster_contract, unicode_contract, unicode_receipt, root
    )
    validate_unicode_receipt(unicode_receipt, package, identities)
    loaded_before = loaded_observer(plan, cluster_contract, root)
    clusterctl.verify_loaded(plan, cluster_contract, loaded_before)
    if loaded_before["system_identifier"] != cluster_receipt["system_identifier"]:
        raise HighwayActivationError("live cluster identity differs from activation receipt")
    instance = cluster_contract["instance"]
    command_receipts: list[dict[str, Any]] = []
    inspection, inspection_receipt = sql_runner(
        plan, cluster_contract, render_inspection_sql(), "inspect-highway-product-state",
        cluster_contract["security"]["admin_os_user"], instance["admin_role"], 120,
    )
    command_receipts.append(inspection_receipt)
    state = validate_inspection(inspection, unicode_receipt, contract)
    activation, activation_command = sql_runner(
        plan, cluster_contract,
        render_activation_sql(contract, identities, unicode_receipt, state),
        "admit-and-activate-highway-product-registry",
        cluster_contract["security"]["admin_os_user"], instance["admin_role"],
        contract["operation"]["statement_timeout_seconds"] + 120,
    )
    command_receipts.append(activation_command)
    validate_activation_result(activation, contract, unicode_receipt, state)
    restart = command_runner(
        "restart-after-highway-activation",
        ["/usr/bin/systemctl", "restart", instance["service"]],
        contract["operation"]["restart_timeout_seconds"],
    )
    command_receipts.append(restart)
    readiness = readiness_runner(
        "highway-restart-readiness", plan["commands"]["probe_readiness"], 120
    )
    command_receipts.append(readiness)
    readback, readback_command = sql_runner(
        plan, cluster_contract,
        render_readback_sql(contract, identities, unicode_receipt, activation),
        "cold-application-highway-readback",
        cluster_contract["security"]["app_os_user"], instance["app_role"], 300,
    )
    command_receipts.append(readback_command)
    validate_readback(readback, contract, unicode_receipt, activation)
    loaded_after = loaded_observer(plan, cluster_contract, root)
    clusterctl.verify_loaded(plan, cluster_contract, loaded_after)
    if (
        loaded_after["system_identifier"] != loaded_before["system_identifier"]
        or loaded_after.get("postmaster_pid") == loaded_before.get("postmaster_pid")
        or loaded_after["loaded_objects"] != loaded_before["loaded_objects"]
        or loaded_after["config_files"] != loaded_before["config_files"]
    ):
        raise HighwayActivationError("product restart changed identity or retained the old postmaster")
    request = {
        "schema": "laplace.highway-product-activation-request/v1",
        "orchestrator_sha256": unicodectl.sha256_file(Path(__file__).resolve()),
        "activation_contract_sha256": unicodectl.sha256_bytes(unicodectl.canonical_bytes(contract)),
        "registry_contract_sha256": unicodectl.sha256_bytes(unicodectl.canonical_bytes(registry_contract)),
        "predecessor_registry_contract_sha256": unicodectl.sha256_bytes(
            unicodectl.canonical_bytes(previous_registry_contract)
        ),
        "package_id": package["package_id"],
        "cluster_plan_sha256": plan["plan_sha256"],
        "cluster_activation_receipt_sha256": cluster_receipt["activation_receipt_sha256"],
        "unicode_activation_receipt_sha256": unicode_receipt["receipt_sha256"],
        "unicode_request_fingerprint": unicode_receipt["request_fingerprint"],
        "operation": contract["operation"],
        "execution_context": contract["execution_context"],
        "expected_result": contract["expected_result"],
        "inspected_state": state,
    }
    request_sha = unicodectl.sha256_bytes(unicodectl.canonical_bytes(request))
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "phase": "product-activated",
        "request_sha256": request_sha,
        "package_id": package["package_id"],
        "cluster_plan_sha256": plan["plan_sha256"],
        "cluster_activation_receipt_sha256": cluster_receipt["activation_receipt_sha256"],
        "unicode_activation_receipt_sha256": unicode_receipt["receipt_sha256"],
        "system_identifier": loaded_after["system_identifier"],
        "mode": state["mode"],
        "activation": activation,
        "readback": readback,
        "restart_proven": True,
        "cold_application_readback_proven": True,
        "loaded_before_observation_sha256": loaded_before["observation_sha256"],
        "loaded_after_observation_sha256": loaded_after["observation_sha256"],
        "command_receipts": command_receipts,
    }
    receipt["receipt_sha256"] = unicodectl.document_identity(receipt, "receipt_sha256")
    evidence_root = unicodectl.prefixed(root, cluster_contract["instance"]["receipt_directory"])
    evidence = evidence_root / contract["receipt"]["directory_name"] / request_sha
    unicodectl.write_immutable(evidence / "request.json", request)
    unicodectl.write_immutable(evidence / "activation-result.json", activation)
    unicodectl.write_immutable(evidence / "readback.json", readback)
    unicodectl.write_immutable(evidence / "receipt.json", receipt)
    return receipt


def execute_highway_activation_receipted(*args: Any, **kwargs: Any) -> dict[str, Any]:
    try:
        return execute_highway_activation(*args, **kwargs)
    except Exception as error:
        contract = args[0]
        cluster_contract = args[1]
        package = args[5]
        plan = args[6]
        cluster_receipt = args[7]
        root = args[9]
        receipt_directory = cluster_contract.get("instance", {}).get("receipt_directory")
        if isinstance(receipt_directory, str) and receipt_directory.startswith("/"):
            failure = {
                "schema": FAILURE_SCHEMA,
                "phase": "failed",
                "error_type": type(error).__name__,
                "error": str(error),
                "context": {
                    "activation_contract_sha256": unicodectl.sha256_bytes(unicodectl.canonical_bytes(contract)),
                    "package_id": package.get("package_id"),
                    "cluster_plan_sha256": plan.get("plan_sha256"),
                    "cluster_activation_receipt_sha256": cluster_receipt.get("activation_receipt_sha256"),
                    "semantic_activation_state": "must-be-reinspected-before-retry",
                },
                "success_receipt_issued": False,
            }
            failure["receipt_sha256"] = unicodectl.document_identity(failure, "receipt_sha256")
            directory = unicodectl.prefixed(root, receipt_directory) / "highway" / "failures"
            unicodectl.write_immutable(
                directory / f"failure-{failure['receipt_sha256']}.json", failure
            )
        raise


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", default="contracts/highway-product-activation.json")
    parser.add_argument("--cluster-contract", default="contracts/postgresql-cluster.json")
    parser.add_argument("--unicode-contract", default="contracts/unicode-product-activation.json")
    parser.add_argument("--registry-contract", default="contracts/highway.json")
    parser.add_argument(
        "--previous-registry-contract", default="contracts/history/highway-v1.json"
    )
    parser.add_argument("--package-manifest", required=True)
    parser.add_argument("--cluster-plan", required=True)
    parser.add_argument("--cluster-activation-receipt", required=True)
    parser.add_argument("--unicode-activation-receipt", required=True)
    parser.add_argument("--output", default="-")
    parser.add_argument("--authorize-system-root", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    repository = Path(__file__).resolve().parents[2]

    def resolve(value: str) -> Path:
        path = Path(value)
        return path if path.is_absolute() else repository / path

    result = execute_highway_activation_receipted(
        load_json(resolve(arguments.contract)),
        load_json(resolve(arguments.cluster_contract)),
        load_json(resolve(arguments.unicode_contract)),
        load_json(resolve(arguments.registry_contract)),
        load_json(resolve(arguments.previous_registry_contract)),
        load_json(Path(arguments.package_manifest)),
        load_json(Path(arguments.cluster_plan)),
        load_json(Path(arguments.cluster_activation_receipt)),
        load_json(Path(arguments.unicode_activation_receipt)),
        Path("/"),
        arguments.authorize_system_root,
    )
    content = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(content)
    else:
        unicodectl.atomic_write(Path(arguments.output), content.encode("utf-8"))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HighwayActivationError as error:
        print(f"Highway product activation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
