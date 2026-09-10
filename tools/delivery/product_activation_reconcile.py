#!/usr/bin/env python3
"""Run product activation with release-capacity and active-generation reconciliation."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from typing import Any


REPOSITORY = Path(__file__).resolve().parents[2]


def _load(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


runner = _load(
    "laplace_reconciled_product_activation_runner",
    REPOSITORY / "tools/delivery/product_activation_runner.py",
)
upgrade = _load(
    "laplace_product_cluster_generation_upgrade",
    REPOSITORY / "tools/delivery/product_cluster_upgrade.py",
)
adoption = _load(
    "laplace_product_predecessor_adoption",
    REPOSITORY / "tools/delivery/product_predecessor_adoption.py",
)
release_capacity = _load(
    "laplace_product_release_capacity",
    REPOSITORY / "tools/delivery/product_release_capacity.py",
)

fresh_activate_product = runner.clusterctl.activate_product
fresh_install_package = runner.clusterctl.install_package
fresh_reconcile_indexed_cognition = runner.reconcile_indexed_cognition


def install_package_with_capacity(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    source_physical_root: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    """Prove release-volume headroom before the immutable copy begins."""

    physical_release = runner.clusterctl.prefixed(source_physical_root, manifest["root"])
    capacity_receipt = release_capacity.reconcile_capacity(
        contract,
        {
            "package_id": manifest["package_id"],
            "physical_root": str(physical_release),
        },
        manifest,
    )
    receipt_root = Path(contract["instance"]["receipt_directory"])
    runner.write_json(
        receipt_root
        / "release-capacity"
        / manifest["package_id"]
        / "capacity.json",
        capacity_receipt,
    )
    return fresh_install_package(
        manifest,
        contract,
        source_physical_root,
        root,
        authorize_system_root,
    )


def reconcile_indexed_cognition_after_generation_upgrade(
    plan: dict[str, Any], cluster_contract: dict[str, Any], package: dict[str, Any]
) -> dict[str, Any]:
    """Accept the indexed-cognition predecessor or its proved 1.0.2 successor.

    ``upgrade_product`` may advance an existing persistent extension through 1.0.1
    to 1.0.2 before the legacy runner reaches its indexed-cognition reconciliation
    step. Re-entering that step must verify the inherited native bindings/indexes,
    not reject the already-upgraded product or attempt a downgrade.
    """

    version_sql = """
SELECT pg_catalog.json_build_object('version', extversion, 'owner', current_user)::text
  FROM pg_catalog.pg_extension
 WHERE extname = 'laplace';
"""
    current, _ = runner.runner_sql(
        plan,
        cluster_contract,
        version_sql,
        "observe-indexed-cognition-version",
        runner.RUNNER_USER,
        cluster_contract["instance"]["admin_role"],
        60,
    )
    if current.get("version") != "1.0.2":
        return fresh_reconcile_indexed_cognition(plan, cluster_contract, package)

    relative = (
        f"pgsql-{plan['postgresql_major']}/share/extension/"
        "laplace--1.0.0--1.0.1.sql"
    )
    path = Path(plan["package_root"]) / relative
    entries = [item for item in package["files"] if item.get("path") == relative]
    if (
        len(entries) != 1
        or entries[0].get("kind") != "file"
        or path.is_symlink()
        or not path.is_file()
        or path.stat().st_size > 65536
        or entries[0].get("sha256") != runner.clusterctl.sha256_file(path)
    ):
        raise runner.RunnerActivationError(
            "packaged indexed cognition migration bytes differ"
        )

    sql = """BEGIN;
SET LOCAL lock_timeout = '30s';
SET LOCAL statement_timeout = '300s';
DO $reconcile$
DECLARE version text; owner name; target record;
BEGIN
    SELECT e.extversion, pg_catalog.pg_get_userbyid(e.extowner) INTO STRICT version, owner
      FROM pg_catalog.pg_extension e WHERE e.extname='laplace';
    IF owner <> current_user THEN RAISE EXCEPTION 'extension reconciliation requires its actual owner'; END IF;
    IF version <> '1.0.2' THEN
        RAISE EXCEPTION 'product cognition successor changed during reconciliation: %', version;
    END IF;
    FOR target IN SELECT * FROM (VALUES
        ('laplace.trajectory_entity_ids(bytea)', 'laplace_pg_trajectory_entity_ids'),
        ('laplace.cognition_observation_execute_persisted(laplace.execution_context,laplace.cognition_observation_request)',
         'laplace_pg_cognition_observation_execute_persisted')) AS t(signature,symbol)
    LOOP
        IF NOT EXISTS (
            SELECT 1 FROM pg_catalog.pg_proc p
            JOIN pg_catalog.pg_language l ON l.oid=p.prolang
            JOIN pg_catalog.pg_depend d ON d.classid='pg_catalog.pg_proc'::regclass AND d.objid=p.oid
              AND d.refclassid='pg_catalog.pg_extension'::regclass AND d.deptype='e'
            JOIN pg_catalog.pg_extension e ON e.oid=d.refobjid
            WHERE p.oid=pg_catalog.to_regprocedure(target.signature) AND e.extname='laplace'
              AND p.proowner=e.extowner AND l.lanname='c' AND p.probin='laplace_pg'
              AND p.prosrc=target.symbol AND NOT p.prosecdef AND p.proisstrict
        ) THEN RAISE EXCEPTION 'indexed cognition binding is not owned native extension code: %',target.signature;
        END IF;
    END LOOP;
    IF (SELECT count(*) FROM pg_catalog.pg_index i
        WHERE i.indexrelid IN ('laplace.physicality_constituent_lookup_idx'::regclass,
                              'laplace.physicality_entity_lookup_idx'::regclass)
          AND i.indisvalid AND i.indisready) <> 2 THEN
        RAISE EXCEPTION 'indexed cognition indexes are not ready';
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_catalog.pg_class c
                   JOIN pg_catalog.pg_index i ON i.indexrelid=c.oid
                   JOIN pg_catalog.pg_am a ON a.oid=c.relam
                   WHERE c.oid='laplace.physicality_constituent_lookup_idx'::regclass
                     AND i.indrelid='laplace.physicality'::regclass
                     AND a.amname='gin' AND 'fastupdate=off'=ANY(c.reloptions)) THEN
        RAISE EXCEPTION 'indexed cognition membership requires its maintained GIN posting tree';
    END IF;
END $reconcile$;
SELECT pg_catalog.json_build_object('schema','laplace.indexed-cognition-upgrade/v1',
    'version',extversion,'owner',current_user,'native_bindings',2,'ready_indexes',2)::text
  FROM pg_catalog.pg_extension WHERE extname='laplace';
COMMIT;
"""
    result, command = runner.runner_sql(
        plan,
        cluster_contract,
        sql,
        "reconcile-indexed-cognition-successor",
        runner.RUNNER_USER,
        cluster_contract["instance"]["admin_role"],
        360,
    )
    expected = {
        "schema": "laplace.indexed-cognition-upgrade/v1",
        "version": "1.0.2",
        "owner": cluster_contract["instance"]["admin_role"],
        "native_bindings": 2,
        "ready_indexes": 2,
    }
    if result != expected:
        raise runner.RunnerActivationError(
            "indexed cognition successor reconciliation result differs"
        )
    receipt = dict(
        result,
        package_id=package["package_id"],
        script_sha256=entries[0]["sha256"],
        command_receipt=command,
    )
    receipt["receipt_sha256"] = runner.document_identity(
        receipt, "receipt_sha256"
    )
    return receipt


def reconcile_cluster_activation(
    contract_path: Path,
    package_path: Path,
    resource_path: Path,
    evidence_directory: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    contract = runner.clusterctl.load_json(contract_path)
    active = Path(contract["package"]["active_link"])
    if active.exists() or active.is_symlink():
        # A predecessor produced before the current receipt schema is not promoted by
        # assumption and is not deleted as disposable state.  Re-prove its plan,
        # loaded bytes, PostgreSQL identity, and a real restart first.  Current-format
        # receipts are a no-op here.
        adoption.ensure_current_predecessor_receipt(contract_path)
        return upgrade.upgrade_product(
            contract_path,
            package_path,
            resource_path,
            evidence_directory,
            authorize_system_root,
        )
    return fresh_activate_product(
        contract_path,
        package_path,
        resource_path,
        evidence_directory,
        authorize_system_root,
    )


runner.clusterctl.install_package = install_package_with_capacity
runner.clusterctl.activate_product = reconcile_cluster_activation
runner.reconcile_indexed_cognition = reconcile_indexed_cognition_after_generation_upgrade


def main(argv: list[str] | None = None) -> int:
    return runner.main(sys.argv[1:] if argv is None else argv)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"product_activation_reconcile: {error}", file=sys.stderr)
        raise SystemExit(1) from error
