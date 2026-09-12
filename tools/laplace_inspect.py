#!/usr/bin/env python3
"""Inspect canonical persisted Laplace substrate state through fixed read operations."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Sequence

HEX128 = re.compile(r"^[0-9a-fA-F]{32}$")
DEFAULT_SOCKET = Path("/opt/laplace/runtime/postgresql/refactor")
DEFAULT_PORT = 55433
DEFAULT_DATABASE = "laplace_refactor"
DEFAULT_ROLE = "laplace_admin"


class InspectError(RuntimeError):
    pass


def default_psql() -> Path:
    invoked = Path(sys.argv[0]).resolve()
    if len(invoked.parents) >= 2:
        packaged = invoked.parents[1] / "pgsql-18/bin/psql"
        if packaged.is_file():
            return packaged
    return Path("/opt/laplace/current/pgsql-18/bin/psql")


def psql_command(args: argparse.Namespace) -> list[str]:
    psql = args.psql or default_psql()
    if not psql.is_file():
        raise InspectError(f"PostgreSQL client is unavailable: {psql}")
    return [
        str(psql),
        "--host", str(args.socket),
        "--port", str(args.port),
        "--username", args.role,
        "--dbname", args.database,
        "--no-psqlrc",
        "--set", "ON_ERROR_STOP=1",
        "--quiet",
        "--tuples-only",
        "--no-align",
    ]


def run_json(command: list[str], sql: str) -> Any:
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
        raise InspectError(f"database query could not execute: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no PostgreSQL diagnostic"
        raise InspectError(f"database query failed: {detail[-4000:]}")
    rows = [line for line in result.stdout.splitlines() if line.strip()]
    if len(rows) != 1:
        raise InspectError(
            f"database query returned {len(rows)} rows; expected one JSON document"
        )
    try:
        return json.loads(rows[0])
    except json.JSONDecodeError as error:
        raise InspectError("database query returned invalid JSON") from error


def bounded_limit(value: int) -> int:
    if value < 1 or value > 1000:
        raise InspectError("--limit must be between 1 and 1000")
    return value


def summary_sql() -> str:
    return """SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.summary/v1',
  'database',current_database(),
  'role',current_user,
  'server_version',current_setting('server_version'),
  'extension_version',(SELECT extversion FROM pg_catalog.pg_extension WHERE extname='laplace'),
  'counts',pg_catalog.json_build_object(
    'entities',(SELECT count(*) FROM laplace.entity),
    'physicalities',(SELECT count(*) FROM laplace.physicality),
    'attestations',(SELECT count(*) FROM laplace.attestation),
    'consensus',(SELECT count(*) FROM laplace.consensus),
    'evidence_nodes',(SELECT count(*) FROM laplace.evidence_node),
    'standing_states',(SELECT count(*) FROM laplace.standing_state_history),
    'standing_arenas',(SELECT count(DISTINCT arena_scope_id) FROM laplace.standing_state_history),
    'source_profiles',(SELECT count(*) FROM laplace.source_profile),
    'execution_receipts',(SELECT count(*) FROM laplace.execution_receipt)
  ),
  'unicode',(SELECT pg_catalog.json_build_object(
      'active_present',active_present,
      'sequence',sequence,
      'activation_epoch_id',pg_catalog.encode(activation_epoch_id,'hex'),
      'epoch_fingerprint',pg_catalog.encode(epoch_fingerprint,'hex'))
    FROM laplace.perfcache_active_control WHERE singleton),
  'highway',(SELECT pg_catalog.json_build_object(
      'active_present',active_present,
      'sequence',sequence,
      'activation_epoch_id',pg_catalog.encode(activation_epoch_id,'hex'),
      'activation_epoch_fingerprint',pg_catalog.encode(activation_epoch_fingerprint,'hex'))
    FROM laplace.highway_registry_active_control WHERE singleton)
)::text;
"""


def entities_sql(limit: int) -> str:
    return f"""SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.entities/v1',
  'rows',COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'entity_id',pg_catalog.encode(entity_id,'hex'),
      'identity_witness',pg_catalog.encode(identity_witness,'hex')) ORDER BY entity_id),'[]'::json)
)::text
FROM (SELECT entity_id,identity_witness FROM laplace.entity ORDER BY entity_id LIMIT {limit}) AS selected;
"""


def physicalities_sql(limit: int) -> str:
    return f"""SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.physicalities/v1',
  'rows',COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'physicality_id',pg_catalog.encode(physicality_id,'hex'),
      'entity_id',pg_catalog.encode(entity_id,'hex'),
      'physicality_type',physicality_type,
      'vertex_class',vertex_class,
      'structural_form',structural_form,
      'dimension_count',dimension_count,
      'logical_count',logical_count,
      'vertex_count',vertex_count,
      'trajectory_fingerprint',pg_catalog.encode(trajectory_fingerprint,'hex')) ORDER BY physicality_id),'[]'::json)
)::text
FROM (SELECT physicality_id,entity_id,physicality_type,vertex_class,structural_form,
             dimension_count,logical_count,vertex_count,trajectory_fingerprint
      FROM laplace.physicality ORDER BY physicality_id LIMIT {limit}) AS selected;
"""


def attestations_sql(limit: int) -> str:
    return f"""SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.attestations/v1',
  'rows',COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'attestation_id',pg_catalog.encode(attestation_id,'hex'),
      'entity_id',pg_catalog.encode(entity_id,'hex'),
      'physicality_id',pg_catalog.encode(physicality_id,'hex'),
      'source_fingerprint',pg_catalog.encode(source_fingerprint,'hex'),
      'context_fingerprint',pg_catalog.encode(context_fingerprint,'hex'),
      'source_ordinal',source_ordinal,
      'attestation_kind',attestation_kind,
      'flags',flags) ORDER BY attestation_id),'[]'::json)
)::text
FROM (SELECT attestation_id,entity_id,physicality_id,source_fingerprint,context_fingerprint,
             source_ordinal,attestation_kind,flags
      FROM laplace.attestation ORDER BY attestation_id LIMIT {limit}) AS selected;
"""


def consensus_sql(limit: int) -> str:
    return f"""SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.consensus/v1',
  'rows',COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'consensus_id',pg_catalog.encode(consensus_id,'hex'),
      'proposition_entity_id',pg_catalog.encode(proposition_entity_id,'hex'),
      'epoch_id',pg_catalog.encode(epoch_id,'hex'),
      'evidence_boundary',pg_catalog.encode(evidence_boundary,'hex'),
      'recipe_fingerprint',pg_catalog.encode(recipe_fingerprint,'hex'),
      'observation_count',observation_count,
      'independent_root_count',independent_root_count,
      'disposition',disposition,
      'standing',standing) ORDER BY standing DESC,consensus_id),'[]'::json)
)::text
FROM (
  SELECT consensus_id,proposition_entity_id,epoch_id,evidence_boundary,recipe_fingerprint,
         observation_count,independent_root_count,disposition,standing
  FROM laplace.consensus
  ORDER BY standing DESC,consensus_id
  LIMIT {limit}
) AS selected;
"""


def evidence_sql(limit: int) -> str:
    return f"""SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.evidence/v1',
  'rows',COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'node_id',pg_catalog.encode(node_id,'hex'),
      'proposition_id',pg_catalog.encode(proposition_id,'hex'),
      'occurrence_id',pg_catalog.encode(occurrence_id,'hex'),
      'source_id',pg_catalog.encode(source_id,'hex'),
      'context_id',pg_catalog.encode(context_id,'hex'),
      'source_ordinal',source_ordinal,
      'epistemic_kind',epistemic_kind,
      'flags',flags) ORDER BY source_ordinal,node_id),'[]'::json)
)::text
FROM (
  SELECT node_id,proposition_id,occurrence_id,source_id,context_id,
         source_ordinal,epistemic_kind,flags
  FROM laplace.evidence_node
  ORDER BY source_ordinal,node_id
  LIMIT {limit}
) AS selected;
"""


def standings_sql(limit: int) -> str:
    return f"""SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.standings/v1',
  'rows',COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'state_id',pg_catalog.encode(state_id,'hex'),
      'coordinate_id',pg_catalog.encode(coordinate_id,'hex'),
      'arena_scope_id',pg_catalog.encode(arena_scope_id,'hex'),
      'prior_state_id',pg_catalog.encode(prior_state_id,'hex'),
      'epoch_id',pg_catalog.encode(epoch_id,'hex'),
      'rating_recipe_id',pg_catalog.encode(rating_recipe_id,'hex'),
      'rating',rating,
      'rating_deviation',rating_deviation,
      'volatility',volatility,
      'eligible_match_count',eligible_match_count,
      'period_ordinal',period_ordinal,
      'rating_recipe_version',rating_recipe_version) ORDER BY rating DESC,coordinate_id),'[]'::json)
)::text
FROM (
  SELECT s.state_id,s.coordinate_id,s.arena_scope_id,s.prior_state_id,s.epoch_id,
         s.rating_recipe_id,s.rating,s.rating_deviation,s.volatility,
         s.eligible_match_count,s.period_ordinal,s.rating_recipe_version
  FROM laplace.standing_state_history AS s
  WHERE NOT EXISTS (
    SELECT 1 FROM laplace.standing_state_history AS successor
    WHERE successor.prior_state_id=s.state_id
  )
  ORDER BY s.rating DESC,s.coordinate_id
  LIMIT {limit}
) AS selected;
"""


def source_profiles_sql(limit: int) -> str:
    return f"""SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.source-profiles/v1',
  'rows',COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'profile_id',pg_catalog.encode(profile_id,'hex'),
      'coordinate_authority',pg_catalog.encode(coordinate_authority,'hex'),
      'coordinate_release',pg_catalog.encode(coordinate_release,'hex'),
      'coordinate_namespace',pg_catalog.encode(coordinate_namespace,'hex'),
      'coordinate_local_identifier',pg_catalog.encode(coordinate_local_identifier,'hex'),
      'coordinate_version',coordinate_version,
      'byte_count',byte_count,
      'record_count',record_count,
      'field_count',field_count,
      'reference_count',reference_count,
      'claim_count',claim_count,
      'unknown_count',unknown_count,
      'unresolved_count',unresolved_count,
      'persisted_count',persisted_count,
      'derived_count',derived_count) ORDER BY profile_id),'[]'::json)
)::text
FROM (SELECT profile_id,coordinate_authority,coordinate_release,coordinate_namespace,
             coordinate_local_identifier,coordinate_version,byte_count,record_count,field_count,
             reference_count,claim_count,unknown_count,unresolved_count,persisted_count,derived_count
      FROM laplace.source_profile ORDER BY profile_id LIMIT {limit}) AS selected;
"""


def entity_sql(entity_hex: str) -> str:
    if HEX128.fullmatch(entity_hex) is None:
        raise InspectError("entity id must be exactly 32 hexadecimal characters")
    entity_hex = entity_hex.lower()
    return f"""WITH target AS (SELECT decode('{entity_hex}','hex') AS entity_id)
SELECT pg_catalog.json_build_object(
  'schema','laplace.inspect.entity/v2',
  'entity',(SELECT pg_catalog.json_build_object(
      'entity_id',pg_catalog.encode(e.entity_id,'hex'),
      'identity_witness',pg_catalog.encode(e.identity_witness,'hex'))
    FROM laplace.entity AS e,target AS t WHERE e.entity_id=t.entity_id),
  'physicalities',(SELECT COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'physicality_id',pg_catalog.encode(p.physicality_id,'hex'),
      'physicality_type',p.physicality_type,
      'structural_form',p.structural_form,
      'logical_count',p.logical_count,
      'vertex_count',p.vertex_count,
      'trajectory_fingerprint',pg_catalog.encode(p.trajectory_fingerprint,'hex')) ORDER BY p.physicality_id),'[]'::json)
    FROM laplace.physicality AS p,target AS t WHERE p.entity_id=t.entity_id),
  'attestations',(SELECT COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'attestation_id',pg_catalog.encode(a.attestation_id,'hex'),
      'physicality_id',pg_catalog.encode(a.physicality_id,'hex'),
      'source_fingerprint',pg_catalog.encode(a.source_fingerprint,'hex'),
      'context_fingerprint',pg_catalog.encode(a.context_fingerprint,'hex'),
      'source_ordinal',a.source_ordinal,
      'attestation_kind',a.attestation_kind,
      'flags',a.flags) ORDER BY a.attestation_id),'[]'::json)
    FROM laplace.attestation AS a,target AS t WHERE a.entity_id=t.entity_id),
  'consensus',(SELECT COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'consensus_id',pg_catalog.encode(c.consensus_id,'hex'),
      'epoch_id',pg_catalog.encode(c.epoch_id,'hex'),
      'evidence_boundary',pg_catalog.encode(c.evidence_boundary,'hex'),
      'observation_count',c.observation_count,
      'independent_root_count',c.independent_root_count,
      'disposition',c.disposition,
      'standing',c.standing) ORDER BY c.standing DESC,c.consensus_id),'[]'::json)
    FROM laplace.consensus AS c,target AS t WHERE c.proposition_entity_id=t.entity_id),
  'evidence',(SELECT COALESCE(pg_catalog.json_agg(pg_catalog.json_build_object(
      'node_id',pg_catalog.encode(n.node_id,'hex'),
      'occurrence_id',pg_catalog.encode(n.occurrence_id,'hex'),
      'source_id',pg_catalog.encode(n.source_id,'hex'),
      'context_id',pg_catalog.encode(n.context_id,'hex'),
      'source_ordinal',n.source_ordinal,
      'epistemic_kind',n.epistemic_kind) ORDER BY n.source_ordinal,n.node_id),'[]'::json)
    FROM laplace.evidence_node AS n,target AS t WHERE n.proposition_id=t.entity_id)
)::text;
"""


def build_sql(args: argparse.Namespace) -> str:
    if args.command == "summary":
        return summary_sql()
    if args.command == "entities":
        return entities_sql(bounded_limit(args.limit))
    if args.command == "physicalities":
        return physicalities_sql(bounded_limit(args.limit))
    if args.command == "attestations":
        return attestations_sql(bounded_limit(args.limit))
    if args.command == "consensus":
        return consensus_sql(bounded_limit(args.limit))
    if args.command == "evidence":
        return evidence_sql(bounded_limit(args.limit))
    if args.command == "standings":
        return standings_sql(bounded_limit(args.limit))
    if args.command == "source-profiles":
        return source_profiles_sql(bounded_limit(args.limit))
    if args.command == "entity":
        return entity_sql(args.entity_id)
    raise InspectError(f"unsupported command: {args.command}")


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser(description=__doc__)
    value.add_argument("--psql", type=Path)
    value.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    value.add_argument("--port", type=int, default=DEFAULT_PORT)
    value.add_argument("--database", default=DEFAULT_DATABASE)
    value.add_argument("--role", default=DEFAULT_ROLE)
    value.add_argument("--pretty", action="store_true")
    sub = value.add_subparsers(dest="command", required=True)
    sub.add_parser("summary")
    for name in (
        "entities",
        "physicalities",
        "attestations",
        "consensus",
        "evidence",
        "standings",
        "source-profiles",
    ):
        command = sub.add_parser(name)
        command.add_argument("--limit", type=int, default=25)
    entity = sub.add_parser("entity")
    entity.add_argument("entity_id")
    return value


def main(argv: Sequence[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        value = run_json(psql_command(args), build_sql(args))
        if args.pretty:
            sys.stdout.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
        else:
            sys.stdout.write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
        return 0
    except InspectError as error:
        print(f"laplace-inspect: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
