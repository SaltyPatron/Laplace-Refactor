#!/usr/bin/env python3
"""Run product activation with release-capacity and active-generation reconciliation."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tempfile
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


def _active_release(
    contract: dict[str, Any], root: Path, release_root: Path
) -> Path | None:
    active = runner.clusterctl.prefixed(root, contract["package"]["active_link"])
    if not active.is_symlink():
        return None
    raw = os.readlink(active)
    target = (active.parent / raw).resolve(strict=False)
    try:
        relative = target.relative_to(release_root.resolve())
    except ValueError as error:
        raise runner.RunnerActivationError(
            f"active product pointer escapes release root: {active} -> {raw}"
        ) from error
    if len(relative.parts) != 1 or release_capacity.PACKAGE_ID.fullmatch(relative.name) is None:
        raise runner.RunnerActivationError(
            f"active product pointer is not one canonical release: {active} -> {raw}"
        )
    if target.is_symlink() or not target.is_dir():
        raise runner.RunnerActivationError(
            f"active product release is absent or unsafe: {target}"
        )
    return target


def _round_allocation(size: int, unit: int) -> int:
    return ((size + unit - 1) // unit) * unit if size else 0


def _reuse_install_plan(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    source_physical_root: Path,
    root: Path,
) -> dict[str, Any]:
    """Plan an immutable install that reuses already verified filesystem bytes.

    A content-addressed package generation frequently differs in only a few files while
    carrying the same PostgreSQL/runtime tree as the active generation.  Copying every
    unchanged byte into another release is unnecessary physical work.  This provider
    hard-links only an exact file whose SHA-256 and mode already satisfy the successor
    manifest.  The target tree is still separately verified before and after the
    atomic release rename.  Any file that cannot be proved reusable is copied normally.
    """

    runner.clusterctl.require_fixture_or_root(root, root == Path("/"))
    runner.clusterctl.validate_contract(contract)
    source_status = runner.clusterctl.verify_package(
        manifest, contract, source_physical_root
    )
    if not source_status.verified:
        raise runner.RunnerActivationError(
            f"source package cannot be reused: {source_status.reason}"
        )

    installed_release = runner.clusterctl.prefixed(root, manifest["root"])
    release_root = runner.clusterctl.prefixed(
        root, contract["package"]["release_root"]
    )
    if installed_release.parent != release_root:
        raise runner.RunnerActivationError(
            "package installation target is outside the release root"
        )
    release_root.mkdir(parents=True, exist_ok=True, mode=0o2755)
    if installed_release.exists() or installed_release.is_symlink():
        raise runner.RunnerActivationError(
            "reuse install planning requires an absent successor release"
        )

    active_release = _active_release(contract, root, release_root)
    source_release = runner.clusterctl.prefixed(source_physical_root, manifest["root"])
    release_device = release_root.stat().st_dev
    allocation = os.statvfs(release_root)
    unit = int(allocation.f_frsize or allocation.f_bsize)
    if unit <= 0:
        raise runner.RunnerActivationError(
            "release filesystem reports no allocation unit"
        )

    entries: list[dict[str, Any]] = []
    relative_directories: set[str] = set()
    copied_file_bytes = 0
    copied_allocation_bytes = 0
    hardlink_source_bytes = 0
    hardlink_active_bytes = 0
    copied_files = 0
    hardlinked_files = 0
    symlinks = 0

    for relative, entry in sorted(source_status.files.items()):
        path = PurePosixPath(relative)
        for parent in path.parents:
            if str(parent) == ".":
                break
            relative_directories.add(str(parent))
        source = source_release.joinpath(*path.parts)
        if entry.get("kind", "file") == "symlink":
            entries.append(
                {
                    "path": relative,
                    "method": "symlink",
                    "source": None,
                    "entry": entry,
                    "bytes": 0,
                }
            )
            symlinks += 1
            continue

        source_metadata = source.stat()
        method = "copy"
        reuse_source: Path | None = None
        if source_metadata.st_dev == release_device:
            method = "hardlink-source"
            reuse_source = source
        elif active_release is not None:
            candidate = active_release.joinpath(*path.parts)
            try:
                candidate_metadata = candidate.stat()
            except (FileNotFoundError, OSError):
                candidate_metadata = None
            if (
                candidate_metadata is not None
                and candidate_metadata.st_dev == release_device
                and stat.S_ISREG(candidate_metadata.st_mode)
                and not candidate.is_symlink()
                and stat.S_IMODE(candidate_metadata.st_mode) == entry["mode"]
                and candidate_metadata.st_size == source_metadata.st_size
                and runner.clusterctl.sha256_file(candidate) == entry["sha256"]
            ):
                method = "hardlink-active"
                reuse_source = candidate

        size = int(source_metadata.st_size)
        if method == "copy":
            copied_files += 1
            copied_file_bytes += size
            copied_allocation_bytes += _round_allocation(size, unit)
        else:
            hardlinked_files += 1
            if method == "hardlink-source":
                hardlink_source_bytes += size
            else:
                hardlink_active_bytes += size
        entries.append(
            {
                "path": relative,
                "method": method,
                "source": str(reuse_source or source),
                "entry": entry,
                "bytes": size,
            }
        )

    logical_components = sum(
        1 for part in PurePosixPath(manifest["root"]).parts if part != "/"
    )
    # One mkdtemp directory, the logical package-root hierarchy, and every relative
    # manifest directory need new directory inodes.  Directory entries consume
    # filesystem metadata even for hard links, so reserve one allocation unit for
    # every created directory and every manifest leaf as a conservative bound.
    directory_count = 1 + logical_components + len(relative_directories)
    metadata_allocation_bytes = unit * (directory_count + len(entries))
    required_available_bytes = copied_allocation_bytes + metadata_allocation_bytes
    required_available_inodes = directory_count + copied_files + symlinks
    free_bytes = int(allocation.f_frsize * allocation.f_bavail)
    free_inodes = int(allocation.f_favail)
    if free_bytes < required_available_bytes or free_inodes < required_available_inodes:
        raise release_capacity.CapacityError(
            "link-aware product release capacity remains insufficient: "
            f"required_bytes={required_available_bytes} available_bytes={free_bytes} "
            f"required_inodes={required_available_inodes} available_inodes={free_inodes}"
        )

    source_package_bytes = sum(
        int(item["bytes"])
        for item in entries
        if item["method"] != "symlink"
    )
    receipt: dict[str, Any] = {
        "schema": release_capacity.SCHEMA,
        "successor_package_id": manifest["package_id"],
        "release_root": str(release_root),
        "source_package_bytes": source_package_bytes,
        "copy_required": True,
        "install_strategy": "verified-hardlink-or-copy",
        "allocation_unit_bytes": unit,
        "copied_file_count": copied_files,
        "copied_file_bytes": copied_file_bytes,
        "copied_file_allocation_bytes": copied_allocation_bytes,
        "hardlinked_file_count": hardlinked_files,
        "hardlinked_source_bytes": hardlink_source_bytes,
        "hardlinked_active_bytes": hardlink_active_bytes,
        "symlink_count": symlinks,
        "metadata_allocation_bytes": metadata_allocation_bytes,
        "required_available_bytes": required_available_bytes,
        "required_available_inodes": required_available_inodes,
        "free_bytes_before": free_bytes,
        "free_inodes_before": free_inodes,
        "capacity_satisfied": True,
    }
    receipt["receipt_sha256"] = release_capacity.document_identity(
        receipt, "receipt_sha256"
    )
    return {
        "source_status": source_status,
        "source_release": source_release,
        "installed_release": installed_release,
        "release_root": release_root,
        "entries": entries,
        "capacity_receipt": receipt,
    }


def _execute_reuse_install(
    plan: dict[str, Any],
    manifest: dict[str, Any],
    contract: dict[str, Any],
    source_physical_root: Path,
    root: Path,
) -> dict[str, Any]:
    release_root = Path(plan["release_root"])
    installed_release = Path(plan["installed_release"])
    temporary_root = Path(
        tempfile.mkdtemp(
            prefix=f".{manifest['package_id']}.install.", dir=release_root
        )
    )
    temporary_release = runner.clusterctl.prefixed(temporary_root, manifest["root"])
    try:
        for planned in plan["entries"]:
            relative = PurePosixPath(planned["path"])
            entry = planned["entry"]
            destination = temporary_release.joinpath(*relative.parts)
            destination.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
            method = planned["method"]
            if method == "symlink":
                destination.symlink_to(entry["target"])
            elif method in {"hardlink-source", "hardlink-active"}:
                source = Path(planned["source"])
                os.link(source, destination, follow_symlinks=False)
            elif method == "copy":
                source = Path(planned["source"])
                with source.open("rb") as input_stream, destination.open("xb") as output_stream:
                    shutil.copyfileobj(
                        input_stream, output_stream, length=1024 * 1024
                    )
                    output_stream.flush()
                    os.fsync(output_stream.fileno())
                destination.chmod(entry["mode"])
            else:
                raise runner.RunnerActivationError(
                    f"unknown release installation method: {method}"
                )

        staged_status = runner.clusterctl.verify_package(
            manifest, contract, temporary_root
        )
        if not staged_status.verified:
            raise runner.RunnerActivationError(
                "reused package failed pre-install verification: "
                f"{staged_status.reason}"
            )
        os.rename(temporary_release, installed_release)
        descriptor = os.open(release_root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    finally:
        shutil.rmtree(temporary_root, ignore_errors=True)

    installed_status = runner.clusterctl.verify_package(manifest, contract, root)
    if not installed_status.verified:
        raise runner.RunnerActivationError(
            f"installed reused package failed verification: {installed_status.reason}"
        )
    return runner.clusterctl.package_installation_receipt(
        manifest,
        installed_status,
        source_physical_root,
        root,
        installed_release,
    )


def install_package_with_capacity(
    manifest: dict[str, Any],
    contract: dict[str, Any],
    source_physical_root: Path,
    root: Path,
    authorize_system_root: bool,
) -> dict[str, Any]:
    """Prove release-volume capacity and select one exact immutable install plan."""

    physical_release = runner.clusterctl.prefixed(
        source_physical_root, manifest["root"]
    )
    capacity_receipt: dict[str, Any]
    try:
        capacity_receipt = release_capacity.reconcile_capacity(
            contract,
            {
                "package_id": manifest["package_id"],
                "physical_root": str(physical_release),
            },
            manifest,
        )
    except release_capacity.CapacityError as full_copy_error:
        # Full byte-for-byte duplication is only one physical installation plan. If
        # immutable bytes already exist on the release filesystem, prove and reuse
        # them rather than failing a product activation for redundant storage work.
        try:
            reuse_plan = _reuse_install_plan(
                manifest, contract, source_physical_root, root
            )
        except Exception:
            raise full_copy_error
        capacity_receipt = reuse_plan["capacity_receipt"]
        receipt_root = Path(contract["instance"]["receipt_directory"])
        runner.write_json(
            receipt_root
            / "release-capacity"
            / manifest["package_id"]
            / "capacity.json",
            capacity_receipt,
        )
        return _execute_reuse_install(
            reuse_plan, manifest, contract, source_physical_root, root
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
    if current.get("version") != "1.0.3":
        return fresh_reconcile_indexed_cognition(plan, cluster_contract, package)

    relative = (
        f"pgsql-{plan['postgresql_major']}/share/extension/"
        "laplace--1.0.2--1.0.3.sql"
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
    IF version <> '1.0.3' THEN
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
        "version": "1.0.3",
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
        # assumption and is not deleted as disposable state. Re-prove its plan,
        # loaded bytes, PostgreSQL identity, and a real restart first. Current-format
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
