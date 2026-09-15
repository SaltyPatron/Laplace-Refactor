#!/usr/bin/env python3
"""Retain bounded, read-only evidence of Highway admission receipt availability."""
from __future__ import annotations

import argparse
from collections import deque
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import stat
import subprocess
import sys

MAX_ENTRIES = 4096
MAX_ROOTS = 8
MAX_DIRECTORY_INDEX = 256
MAX_DEPTH = 8
MAX_FILE_BYTES = 1024 * 1024
MAX_CAPTURE_BYTES = 16 * 1024 * 1024
HIGHWAY_SCHEMAS = {"laplace.highway-product-activation-request/v1", "laplace.highway-product-activation-receipt/v1"}
RELATED_SCHEMAS = {"laplace.postgresql-cluster-plan/v1", "laplace.postgresql-activation-receipt/v1", "laplace.unicode-product-activation-receipt/v1"}
DIAGNOSTIC_SCHEMAS = {"laplace.product-sql-failure/v1"}
MAX_DATABASE_OUTPUT_BYTES = 256 * 1024
DATABASE_TIMEOUT_SECONDS = 30


def load_runner():
    path = Path(__file__).with_name("product_activation_runner.py")
    specification = importlib.util.spec_from_file_location("laplace_highway_diagnostic_runner", path)
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


def render_committed_state_sql(highway_sql: str) -> str:
    """One metadata snapshot; never calls admission or revalidation operators."""
    physicality = """jsonb_build_object(
      'physicality_id', encode(p.physicality_id,'hex'),
      'entity_id', encode(p.entity_id,'hex'),
      'physicality_type', p.physicality_type, 'vertex_class', p.vertex_class,
      'recipe_version', p.recipe_version, 'structural_form', p.structural_form,
      'dimension_count', p.dimension_count, 'flags', p.flags,
      'recipe_fingerprint', encode(p.recipe_fingerprint,'hex'),
      'geometry_epoch', encode(p.geometry_epoch,'hex'),
      'trajectory_fingerprint', encode(p.trajectory_fingerprint,'hex'),
      'centroid_x_float8send_hex', encode(float8send(p.centroid_x),'hex'),
      'centroid_y_float8send_hex', encode(float8send(p.centroid_y),'hex'),
      'centroid_z_float8send_hex', encode(float8send(p.centroid_z),'hex'),
      'centroid_m_float8send_hex', encode(float8send(p.centroid_m),'hex'),
      'radius_float8send_hex', encode(float8send(p.radius),'hex'),
      'logical_count', p.logical_count::text, 'vertex_count', p.vertex_count::text,
      'trajectory_bytes', octet_length(p.trajectory))"""
    return """BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;
SET LOCAL statement_timeout = '20s';
SET LOCAL lock_timeout = '2s';
SET LOCAL bytea_output = 'hex';
WITH base(value) AS MATERIALIZED (
""" + highway_sql.strip().removesuffix(";") + """
), active_generation AS MATERIALIZED (
  SELECT g.* FROM laplace.highway_registry_active_control h
  JOIN laplace.highway_registry_generation g
    ON h.active_present AND g.activation_epoch_id=h.activation_epoch_id
   AND g.activation_epoch_fingerprint=h.activation_epoch_fingerprint
  WHERE h.singleton
), root_forms AS MATERIALIZED (
  SELECT p.* FROM active_generation g JOIN laplace.physicality p
    ON p.entity_id=g.root_entity_id
  ORDER BY p.physicality_id LIMIT 9
), unicode_roots AS MATERIALIZED (
  SELECT root_receipt, geometry_epoch, recipe_fingerprint,
    physicality_recipe_fingerprint, physicality_recipe_version
  FROM laplace.unicode_root_generation ORDER BY root_receipt LIMIT 3
)
SELECT jsonb_build_object(
  'schema', 'laplace.highway-committed-state-observation/v1',
  'scope', 'committed metadata snapshot; no native revalidation or repair',
  'database', current_database(), 'database_role', current_user,
  'server_version', current_setting('server_version'),
  'transaction_read_only', current_setting('transaction_read_only'),
  'transaction_isolation', current_setting('transaction_isolation'),
  'float_encoding', 'PostgreSQL float8send big-endian binary64',
  'control', (SELECT value::jsonb FROM base),
  'active_highway_generation', (SELECT to_jsonb(g) FROM active_generation g),
  'root_entity', (SELECT jsonb_build_object('entity_id', encode(e.entity_id,'hex'),
    'identity_witness', encode(e.identity_witness,'hex'))
    FROM active_generation g JOIN laplace.entity e ON e.entity_id=g.root_entity_id),
  'selected_root_physicality', (SELECT """ + physicality + """
    FROM active_generation g JOIN laplace.physicality p
      ON p.physicality_id=g.root_physicality_id),
  'root_entity_physicalities', COALESCE((SELECT jsonb_agg(value ORDER BY physicality_id)
    FROM (SELECT p.physicality_id, """ + physicality + """ AS value
      FROM root_forms p ORDER BY p.physicality_id LIMIT 8) sample), '[]'::jsonb),
  'root_entity_physicalities_truncated', (SELECT count(*) > 8 FROM root_forms),
  'unicode_root_generations', COALESCE((SELECT jsonb_agg(to_jsonb(r) ORDER BY root_receipt)
    FROM (SELECT * FROM unicode_roots ORDER BY root_receipt LIMIT 2) r), '[]'::jsonb),
  'unicode_root_generations_truncated', (SELECT count(*) > 2 FROM unicode_roots)
)::text;
ROLLBACK;
"""


def observe_committed_state(contract: dict, output: Path) -> dict:
    report = {"schema": "laplace.highway-database-diagnostic/v1", "status": "unavailable",
              "scope": "optional read-only committed metadata; no activation or recovery claim",
              "limits": {"process_seconds": DATABASE_TIMEOUT_SECONDS, "statement_seconds": 20,
                         "lock_seconds": 2, "retained_bytes_per_output": MAX_DATABASE_OUTPUT_BYTES,
                         "root_physicalities": 8, "unicode_root_generations": 2},
              "process": None, "outputs": {}}

    def retain(name, value):
        raw = value if isinstance(value, bytes) else (value or "").encode("utf-8")
        retained = raw[:MAX_DATABASE_OUTPUT_BYTES]
        filename = "committed-state." + name
        (output / filename).write_bytes(retained)
        report["outputs"][name] = {"file": filename, "observed_bytes": len(raw),
            "observed_sha256": hashlib.sha256(raw).hexdigest(),
            "retained_bytes": len(retained), "retained_sha256": hashlib.sha256(retained).hexdigest(),
            "truncated": len(raw) != len(retained)}

    def observed(completed, receipt):
        report["process"] = receipt
        retain("stdout", completed.stdout)
        retain("stderr", completed.stderr)

    try:
        runner = load_runner()
        runner.clusterctl.validate_contract(contract)
        runner.require_runner()
        active = Path(contract["package"]["active_link"])
        root = active.resolve(strict=True)
        if root.parent != Path(contract["package"]["release_root"]).resolve(strict=True) or re.fullmatch(r"[0-9a-f]{64}", root.name) is None:
            raise ValueError("active package is outside the declared immutable release directory")
        # This is the transport observed at the active link, not a reconstructed
        # activation plan or an assertion about the loaded server's package.
        transport = {"package_root": str(root), "postgresql_major": contract["package"]["postgresql_major"],
                     "instance": contract["instance"]}
        report["transport"] = {**transport, "active_link": str(active), "selected_release_directory_name": root.name,
                               "loaded_server_package_verified": False}
        sql = render_committed_state_sql(runner.highwayctl.render_inspection_sql())
        (output / "committed-state.sql").write_text(sql)
        report["sql_sha256"] = hashlib.sha256(sql.encode()).hexdigest()
        result, _ = runner.runner_sql(transport, contract, sql, "inspect-committed-highway-state",
            contract["security"]["admin_os_user"], contract["instance"]["admin_role"], DATABASE_TIMEOUT_SECONDS,
            completed_observer=observed)
        if result.get("schema") != "laplace.highway-committed-state-observation/v1" or result.get("transaction_read_only") != "on" or result.get("transaction_isolation") != "repeatable read":
            raise ValueError("committed-state observation lacks its read-only snapshot boundary")
        if any(value["truncated"] for value in report["outputs"].values()):
            raise ValueError("database output exceeds the declared retained evidence bound")
        report["status"] = "observed"
        report["observation"] = result
    except subprocess.TimeoutExpired as error:
        report["status"] = "timed-out"
        report["timeout_seconds"] = error.timeout
        report["timeout_command"] = error.cmd
        report["partial_output"] = True
        retain("stdout", error.stdout)
        retain("stderr", error.stderr)
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        report["error_type"] = type(error).__name__
        retain("error", str(error))
        if report["process"] is not None:
            report["status"] = "failed"
    (output / "committed-state.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    return report


def metadata(path: Path) -> dict:
    result = {"path": str(path)}
    try:
        observed = path.lstat()
        kind = "symlink" if stat.S_ISLNK(observed.st_mode) else "directory" if stat.S_ISDIR(observed.st_mode) else "file" if stat.S_ISREG(observed.st_mode) else "special"
        result.update({"kind": kind, "mode": oct(stat.S_IMODE(observed.st_mode)), "uid": observed.st_uid, "gid": observed.st_gid, "bytes": observed.st_size})
        if kind == "symlink":
            result["target"] = os.readlink(path)
    except FileNotFoundError:
        result["kind"] = "absent"
    except OSError as error:
        result.update({"kind": "unreadable", "error": str(error)})
    return result


def inspect(receipt_root: Path, output: Path, additional_roots: list[Path], request_sha: str | None = None, package_id: str | None = None) -> dict:
    if request_sha is not None and re.fullmatch(r"[0-9a-f]{64}", request_sha) is None:
        raise ValueError("Expected request identity must be a lowercase SHA-256")
    if package_id is not None and re.fullmatch(r"[0-9a-f]{64}", package_id) is None:
        raise ValueError("Expected package identity must be a lowercase SHA-256")
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema": "laplace.highway-receipt-diagnostic/v1", "scope": "filesystem evidence only; no database execution or reconstructed admission", "receipt_root": metadata(receipt_root), "retained_admission_directory": metadata(receipt_root / "highway"), "search_roots": [], "candidates": [], "symlinks_not_followed": [], "errors": [], "examined_entries": 0, "captured_bytes": 0, "truncated": False, "limits": {"entries": MAX_ENTRIES, "depth": MAX_DEPTH, "single_file_bytes": MAX_FILE_BYTES, "captured_bytes": MAX_CAPTURE_BYTES}}
    report.update({"expected_request_sha256": request_sha, "related_package_id": package_id, "directory_index": [], "directory_index_truncated": False})
    report["failure_evidence"] = []
    report["limits"]["directory_index_per_root"] = MAX_DIRECTORY_INDEX
    seen = set()

    def walk(root: Path) -> None:
        # Give every explicitly selected archive a bounded fair search. Breadth
        # first traversal reaches sibling receipt trees before large build leaves.
        pending = deque([(root, 0)])
        examined = 0
        indexed = 0
        while pending and examined < MAX_ENTRIES:
            directory, depth = pending.popleft()
            if depth > MAX_DEPTH:
                report["truncated"] = True
                continue
            try:
                with os.scandir(directory) as entries:
                    for entry in entries:
                        if examined >= MAX_ENTRIES:
                            report["truncated"] = True
                            break
                        examined += 1
                        report["examined_entries"] += 1
                        path = Path(entry.path)
                        if entry.is_symlink():
                            report["symlinks_not_followed"].append(metadata(path))
                        elif entry.is_dir(follow_symlinks=False):
                            if indexed < MAX_DIRECTORY_INDEX:
                                report["directory_index"].append({**metadata(path), "search_root": str(root), "depth": depth + 1})
                                indexed += 1
                            else:
                                report["directory_index_truncated"] = True
                            # Known admission identity and named receipt archives
                            # take precedence over unrelated build output trees.
                            if entry.name in {request_sha, package_id} or any(word in entry.name.lower() for word in ("highway", "receipt")):
                                pending.appendleft((path, depth + 1))
                            else:
                                pending.append((path, depth + 1))
                        elif entry.is_file(follow_symlinks=False) and entry.name.endswith(".json"):
                            capture(path)
            except OSError as error:
                report["errors"].append({"path": str(directory), "error": str(error)})
        if pending:
            report["truncated"] = True

    def capture(path: Path) -> None:
        if str(path) in seen:
            return
        seen.add(str(path))
        try:
            if path.stat().st_size > MAX_FILE_BYTES:
                report["errors"].append({"path": str(path), "error": "candidate exceeds single-file evidence bound"})
                return
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            with os.fdopen(descriptor, "rb") as source:
                raw = source.read(MAX_FILE_BYTES + 1)
            if len(raw) > MAX_FILE_BYTES:
                raise ValueError("candidate grew beyond the file bound")
            document = json.loads(raw)
            schema = document.get("schema", "") if isinstance(document, dict) else ""
            if not isinstance(schema, str):
                return
            related = schema in RELATED_SCHEMAS and package_id is not None and document.get("package_id") == package_id
            diagnostic = schema in DIAGNOSTIC_SCHEMAS
            if schema not in HIGHWAY_SCHEMAS and not related and not diagnostic:
                return
            fingerprint = hashlib.sha256(raw).hexdigest()
            record = {**metadata(path), "sha256": fingerprint, "schema": schema, "package_id": document.get("package_id"), "system_identifier": document.get("system_identifier"), "request_sha256": document.get("request_sha256"), "capture": None}
            if related:
                record.update({"capture_reason": "exact related package identity", **{key: document.get(key) for key in ("plan_sha256", "activation_receipt_sha256", "receipt_sha256", "request_fingerprint")}})
            if report["captured_bytes"] + len(raw) <= MAX_CAPTURE_BYTES:
                target = output / f"{fingerprint}.json"
                if not target.exists():
                    target.write_bytes(raw)
                    report["captured_bytes"] += len(raw)
                record["capture"] = target.name
            else:
                report["truncated"] = True
            if diagnostic:
                record["capture_reason"] = "failed SQL process evidence; not an admission receipt"
                report["failure_evidence"].append(record)
            else:
                report["candidates"].append(record)
        except (OSError, ValueError) as error:
            report["errors"].append({"path": str(path), "error": str(error)})

    roots = list(dict.fromkeys([receipt_root, *additional_roots]))
    if len(roots) > MAX_ROOTS:
        report["truncated"] = True
    report["limits"].update({"entries_per_root": MAX_ENTRIES, "roots": MAX_ROOTS, "entries": MAX_ENTRIES * min(len(roots), MAX_ROOTS)})
    for root in roots[:MAX_ROOTS]:
        ancestry = [metadata(path) for path in reversed([root, *root.parents])]
        report["search_roots"].append({"path": str(root), "ancestry": ancestry})
        if any(item["kind"] != "directory" for item in ancestry):
            report["errors"].append({"path": str(root), "error": "search root or ancestor is absent, unreadable, non-directory or symlink; not traversed"})
            continue
        walk(root)
    report["candidate_count"] = len(report["candidates"])
    report["recovery_status"] = "candidate-evidence-requires-native-identity-validation" if report["candidates"] else "no-retained-highway-evidence-found-within-declared-search-roots"
    (output / "diagnostic.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=Path("contracts/postgresql-cluster.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--search-root", type=Path, action="append", default=[], help="additional explicitly selected retained receipt archive; symlinks are never traversed")
    parser.add_argument("--request-sha256", help="prioritize an independently observed admission request identity without reconstructing it")
    parser.add_argument("--package-id", help="capture related plan, cluster and Unicode receipts only for this independently observed package")
    parser.add_argument("--inspect-committed-state", action="store_true", help="also retain one bounded read-only snapshot using the active package's runner-owned PostgreSQL transport")
    arguments = parser.parse_args()
    contract = json.loads(arguments.contract.read_text())
    result = inspect(Path(contract["instance"]["receipt_directory"]), arguments.output, arguments.search_root, arguments.request_sha256, arguments.package_id)
    if arguments.inspect_committed_state:
        database = observe_committed_state(contract, arguments.output)
        result["scope"] = "filesystem evidence and separately scoped read-only database observation; no reconstructed admission"
        result["database_diagnostic"] = {"file": "committed-state.json", "status": database["status"]}
        (arguments.output / "diagnostic.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({key: result[key] for key in ("retained_admission_directory", "candidate_count", "recovery_status", "truncated", "database_diagnostic") if key in result}))


if __name__ == "__main__":
    main()
