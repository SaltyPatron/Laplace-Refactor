#!/usr/bin/env python3
"""Build and materialize a source-checkout-free Laplace product installer bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import pwd
import grp
import shutil
import stat
import sys
import tempfile
from typing import Any, Iterable, Sequence


BUNDLE_SCHEMA = "laplace.product-installer-bundle/v1"
PACKAGE_SCHEMA = "laplace.package-manifest/v1"
RECEIPT_SCHEMA = "laplace.product-package-receipt/v1"
MATERIALIZATION_SCHEMA = "laplace.product-installer-materialization/v1"
HEX_256 = "0123456789abcdef"

CONTROL_SOURCES = {
    "contracts/highway-product-activation.json",
    "contracts/highway.json",
    "contracts/history/highway-v1.json",
    "contracts/instances/refactor.json",
    "contracts/postgresql-cluster.json",
    "contracts/postgresql-host.json",
    "contracts/product-activation-gateway.json",
    "contracts/product-host.json",
    "contracts/unicode-postgresql.json",
    "contracts/unicode-product-activation.json",
    "contracts/unicode-source.json",
    "tools/delivery/install_product_activation_gateway.py",
    "tools/delivery/product_activation.py",
    "tools/delivery/product_distribution.py",
    "tools/delivery/product_host.py",
    "tools/postgresql/clusterctl.py",
    "tools/postgresql/highwayctl.py",
    "tools/postgresql/hostctl.py",
    "tools/postgresql/unicodectl.py",
    "tools/product/build-package.py",
}


class DistributionError(RuntimeError):
    """The installer distribution is incomplete or differs from its manifest."""


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DistributionError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, json.JSONDecodeError) as error:
        raise DistributionError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise DistributionError(f"JSON root must be an object: {path}")
    return value


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def document_identity(value: dict[str, Any], field: str) -> str:
    payload = dict(value)
    payload.pop(field, None)
    return hashlib.sha256(canonical_bytes(payload)).hexdigest()


def require_hex(value: Any, field: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in HEX_256 for character in value)
    ):
        raise DistributionError(f"{field} must be lowercase SHA-256")
    return value


def require_relative(value: Any, field: str) -> str:
    if not isinstance(value, str):
        raise DistributionError(f"{field} must be a relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or str(path) != value:
        raise DistributionError(f"{field} must be a normalized relative path")
    return value


def require_absolute(value: Any, field: str) -> Path:
    if not isinstance(value, str):
        raise DistributionError(f"{field} must be an absolute path")
    path = Path(value)
    if not path.is_absolute() or ".." in PurePosixPath(value).parts:
        raise DistributionError(f"{field} must be a normalized absolute path")
    return path


def physical_file(path: Path, field: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise DistributionError(f"{field} must be a physical file: {path}")


def package_identity(manifest: dict[str, Any]) -> str:
    payload = dict(manifest)
    payload.pop("package_id", None)
    payload.pop("root", None)
    return hashlib.sha256(canonical_bytes(payload)).hexdigest()


def verify_package_manifest(manifest: dict[str, Any], source_root: Path) -> dict[str, Any]:
    if manifest.get("schema") != PACKAGE_SCHEMA:
        raise DistributionError("product package manifest schema differs")
    package_id = require_hex(manifest.get("package_id"), "package id")
    if package_identity(manifest) != package_id:
        raise DistributionError("product package identity differs from its manifest")
    root = require_absolute(manifest.get("root"), "package root")
    release = source_root.joinpath(*root.parts[1:])
    if not release.is_dir() or release.is_symlink():
        raise DistributionError("product package release root is absent")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise DistributionError("product package manifest omits files")
    observed: set[str] = set()
    file_count = 0
    symlink_count = 0
    total_file_bytes = 0
    for entry in files:
        if not isinstance(entry, dict):
            raise DistributionError("product package file entry is invalid")
        relative = require_relative(entry.get("path"), "package file path")
        if relative in observed:
            raise DistributionError(f"product package repeats a file: {relative}")
        observed.add(relative)
        path = release.joinpath(*PurePosixPath(relative).parts)
        kind = entry.get("kind", "file")
        if kind == "symlink":
            if not path.is_symlink() or os.readlink(path) != entry.get("target"):
                raise DistributionError(f"product package symlink differs: {relative}")
            identity = hashlib.sha256(os.readlink(path).encode("utf-8")).hexdigest()
            symlink_count += 1
        elif kind == "file":
            physical_file(path, f"product package file {relative}")
            identity = sha256_file(path)
            if stat.S_IMODE(path.stat().st_mode) != entry.get("mode"):
                raise DistributionError(f"product package mode differs: {relative}")
            file_count += 1
            total_file_bytes += path.stat().st_size
        else:
            raise DistributionError(f"product package kind is unsupported: {relative}")
        if identity != entry.get("sha256"):
            raise DistributionError(f"product package bytes differ: {relative}")
    if manifest.get("activation_eligible") is not True:
        raise DistributionError("product package is not activation eligible")
    return {
        "package_id": package_id,
        "release": release,
        "file_count": file_count,
        "symlink_count": symlink_count,
        "total_file_bytes": total_file_bytes,
    }


def derive_source_root(receipt: dict[str, Any], manifest: dict[str, Any]) -> Path:
    physical_release = require_absolute(receipt.get("physical_root"), "receipt physical root")
    logical = require_absolute(manifest.get("root"), "manifest root")
    source = physical_release
    for _part in logical.parts[1:]:
        source = source.parent
    if source.joinpath(*logical.parts[1:]) != physical_release:
        raise DistributionError("cannot derive product package source root")
    return source


def file_record(path: Path, relative: str) -> dict[str, Any]:
    physical_file(path, relative)
    return {
        "path": relative,
        "sha256": sha256_file(path),
        "mode": stat.S_IMODE(path.stat().st_mode),
    }


def copy_control(
    repository: Path,
    destination: Path,
    overrides: dict[str, Path] | None = None,
) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    selected_overrides = {} if overrides is None else overrides
    for relative in sorted(CONTROL_SOURCES):
        source = selected_overrides.get(
            relative, repository.joinpath(*PurePosixPath(relative).parts)
        )
        physical_file(source, f"installer control source {relative}")
        target = destination.joinpath(*PurePosixPath(relative).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        mode = 0o555 if relative.startswith("tools/") else 0o444
        target.chmod(mode)
        records.append(file_record(target, f"control/{relative}"))
    return records


def verify_source_contract(
    contract: dict[str, Any], source_root: Path
) -> dict[str, Any]:
    if contract.get("schema") != "laplace.unicode-source-contract/v1":
        raise DistributionError("Unicode source contract schema differs")
    version = contract.get("unicode_version")
    files = contract.get("files")
    if not isinstance(version, str) or not version or not isinstance(files, list):
        raise DistributionError("Unicode source contract is incomplete")
    if contract.get("file_count") != len(files) or not files:
        raise DistributionError("Unicode source file count differs")
    if not source_root.is_dir() or source_root.is_symlink():
        raise DistributionError("Unicode source root is absent or not physical")
    total = 0
    records: list[dict[str, Any]] = []
    observed: set[str] = set()
    for entry in files:
        if not isinstance(entry, dict):
            raise DistributionError("Unicode source file declaration is invalid")
        relative = require_relative(entry.get("path"), "Unicode source path")
        if relative in observed:
            raise DistributionError(f"Unicode source repeats a file: {relative}")
        observed.add(relative)
        path = source_root.joinpath(*PurePosixPath(relative).parts)
        physical_file(path, f"Unicode source {relative}")
        size = path.stat().st_size
        digest = sha256_file(path)
        if size != entry.get("bytes") or digest != entry.get("sha256"):
            raise DistributionError(f"Unicode source bytes differ: {relative}")
        total += size
        records.append({"path": relative, "bytes": size, "sha256": digest})
    source_id = hashlib.sha256(canonical_bytes(contract)).hexdigest()
    return {
        "id": source_id,
        "version": version,
        "file_count": len(records),
        "total_file_bytes": total,
        "files": records,
    }


def copy_declared_source(
    contract: dict[str, Any], source_root: Path, destination: Path
) -> None:
    if destination.exists() or destination.is_symlink():
        raise DistributionError(f"Unicode source destination already exists: {destination}")
    destination.mkdir(parents=True, mode=0o755)
    for entry in contract["files"]:
        relative = require_relative(entry["path"], "Unicode source path")
        source = source_root.joinpath(*PurePosixPath(relative).parts)
        target = destination.joinpath(*PurePosixPath(relative).parts)
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
        shutil.copyfile(source, target)
        target.chmod(0o444)
    verify_source_contract(contract, destination)
    for directory in sorted(
        (item for item in destination.rglob("*") if item.is_dir()),
        key=lambda item: len(item.parts),
        reverse=True,
    ):
        directory.chmod(0o555)
    destination.chmod(0o555)


def materialize_unicode_source(
    contract_path: Path,
    source_root: Path,
    logical_target: Path,
    root: Path,
) -> dict[str, Any]:
    if not logical_target.is_absolute():
        raise DistributionError("Unicode source target must be absolute")
    contract = load_json(contract_path)
    source = verify_source_contract(contract, source_root)
    target = prefixed(root, logical_target)
    installed_new = False
    if target.exists() or target.is_symlink():
        if not target.is_dir() or target.is_symlink():
            raise DistributionError("existing Unicode source target is not physical")
        installed = verify_source_contract(contract, target)
        if installed != source:
            raise DistributionError("existing Unicode source package differs")
    else:
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
        copy_declared_source(contract, source_root, target)
        installed_new = True
    if root == Path("/"):
        os.chown(target, 0, 0)
        for candidate in target.rglob("*"):
            os.chown(candidate, 0, 0, follow_symlinks=False)
    return {
        **source,
        "logical_root": str(logical_target),
        "installed_new": installed_new,
    }


def copy_package_tree(manifest: dict[str, Any], source_root: Path, destination: Path) -> None:
    logical = require_absolute(manifest["root"], "package root")
    source = source_root.joinpath(*logical.parts[1:])
    target = destination.joinpath(*logical.parts[1:])
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, target, symlinks=True)


def install_script() -> bytes:
    return b"""#!/bin/sh
set -eu
LAPLACE_INSTALLER_ROOT=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)
exec /usr/bin/python3 \"$LAPLACE_INSTALLER_ROOT/control/tools/delivery/product_host.py\" install \\
  --repository \"$LAPLACE_INSTALLER_ROOT/control\" \\
  --authorize-system-root \\
  --generate-key \\
  --distribution \"$LAPLACE_INSTALLER_ROOT/installer-manifest.json\" \\
  --output /opt/laplace/receipts/deployments/product-host-install.json \\
  \"$@\"
"""


def build_bundle(
    repository: Path,
    receipt_path: Path,
    output_root: Path,
    unicode_contract_path: Path | None = None,
    unicode_source_root: Path | None = None,
) -> dict[str, Any]:
    repository = repository.resolve()
    receipt = load_json(receipt_path)
    if receipt.get("schema") != RECEIPT_SCHEMA:
        raise DistributionError("source product receipt schema differs")
    manifest_path = require_absolute(receipt.get("manifest"), "receipt manifest")
    physical_file(manifest_path, "product package manifest")
    if sha256_file(manifest_path) != receipt.get("manifest_sha256"):
        raise DistributionError("product package manifest bytes differ from its receipt")
    manifest = load_json(manifest_path)
    source_root = derive_source_root(receipt, manifest)
    package = verify_package_manifest(manifest, source_root)
    if receipt.get("package_id") != package["package_id"]:
        raise DistributionError("product package receipt identity differs")
    output_root.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".laplace-installer.", dir=output_root))
    try:
        control = temporary / "control"
        payload = temporary / "payload"
        selected_unicode_contract = (
            repository / "contracts/unicode-source.json"
            if unicode_contract_path is None
            else unicode_contract_path
        )
        unicode_contract = load_json(selected_unicode_contract)
        configured_reference = unicode_contract.get("authority", {}).get(
            "local_verified_reference_root"
        )
        selected_unicode_source = (
            Path(configured_reference)
            if unicode_source_root is None and isinstance(configured_reference, str)
            else unicode_source_root
        )
        if selected_unicode_source is None:
            raise DistributionError("Unicode source root is not configured")
        unicode_status = verify_source_contract(
            unicode_contract, selected_unicode_source
        )
        control_records = copy_control(
            repository,
            control,
            {"contracts/unicode-source.json": selected_unicode_contract},
        )
        payload.mkdir()
        package_manifest = payload / "package-manifest.json"
        package_manifest.write_bytes(canonical_bytes(manifest))
        package_manifest.chmod(0o444)
        copy_package_tree(manifest, source_root, payload / "root")
        payload_status = verify_package_manifest(manifest, payload / "root")
        unicode_payload = payload / "sources/unicode"
        copy_declared_source(
            unicode_contract, selected_unicode_source, unicode_payload
        )
        executable = temporary / "install"
        executable.write_bytes(install_script())
        executable.chmod(0o555)
        core = {
            "schema": BUNDLE_SCHEMA,
            "package": {
                "id": package["package_id"],
                "manifest": "payload/package-manifest.json",
                "manifest_sha256": sha256_file(package_manifest),
                "root": "payload/root",
                "file_count": payload_status["file_count"],
                "symlink_count": payload_status["symlink_count"],
                "total_file_bytes": payload_status["total_file_bytes"],
            },
            "control_files": control_records,
            "sources": {
                "unicode": {
                    **unicode_status,
                    "contract": "control/contracts/unicode-source.json",
                    "contract_sha256": sha256_file(
                        control / "contracts/unicode-source.json"
                    ),
                    "root": "payload/sources/unicode",
                    "install_root": f"/opt/laplace/sources/unicode/{unicode_status['version']}",
                }
            },
            "entrypoint": file_record(executable, "install"),
        }
        core["bundle_id"] = document_identity(core, "bundle_id")
        manifest_output = temporary / "installer-manifest.json"
        manifest_output.write_bytes(canonical_bytes(core))
        manifest_output.chmod(0o444)
        destination = output_root / f"laplace-installer-{core['bundle_id']}"
        if destination.exists() or destination.is_symlink():
            verified = verify_bundle(destination / "installer-manifest.json")
            if verified["bundle_id"] != core["bundle_id"]:
                raise DistributionError("existing installer bundle differs")
            shutil.rmtree(temporary)
            return verified
        os.replace(temporary, destination)
        return verify_bundle(destination / "installer-manifest.json")
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def verify_record(bundle: Path, record: dict[str, Any]) -> None:
    if not isinstance(record, dict) or set(record) != {"path", "sha256", "mode"}:
        raise DistributionError("installer file record fields differ")
    relative = require_relative(record["path"], "installer file path")
    path = bundle.joinpath(*PurePosixPath(relative).parts)
    physical_file(path, relative)
    if sha256_file(path) != require_hex(record["sha256"], f"{relative} digest"):
        raise DistributionError(f"installer file bytes differ: {relative}")
    if stat.S_IMODE(path.stat().st_mode) != record["mode"]:
        raise DistributionError(f"installer file mode differs: {relative}")


def verify_bundle(manifest_path: Path) -> dict[str, Any]:
    physical_file(manifest_path, "installer manifest")
    bundle = manifest_path.parent
    manifest = load_json(manifest_path)
    if manifest.get("schema") != BUNDLE_SCHEMA:
        raise DistributionError("installer bundle schema differs")
    bundle_id = require_hex(manifest.get("bundle_id"), "installer bundle id")
    if document_identity(manifest, "bundle_id") != bundle_id:
        raise DistributionError("installer bundle identity differs")
    controls = manifest.get("control_files")
    if not isinstance(controls, list) or len(controls) != len(CONTROL_SOURCES):
        raise DistributionError("installer control file set differs")
    for record in controls:
        verify_record(bundle, record)
    verify_record(bundle, manifest.get("entrypoint"))
    package = manifest.get("package")
    if not isinstance(package, dict):
        raise DistributionError("installer package record is absent")
    package_manifest = bundle / require_relative(package.get("manifest"), "package manifest")
    if sha256_file(package_manifest) != require_hex(
        package.get("manifest_sha256"), "package manifest digest"
    ):
        raise DistributionError("installer package manifest bytes differ")
    package_document = load_json(package_manifest)
    payload_root = bundle / require_relative(package.get("root"), "package payload root")
    observed = verify_package_manifest(package_document, payload_root)
    for field in ("file_count", "symlink_count", "total_file_bytes"):
        if observed[field] != package.get(field):
            raise DistributionError(f"installer package {field} differs")
    if observed["package_id"] != package.get("id"):
        raise DistributionError("installer package identity differs")
    sources = manifest.get("sources")
    unicode = sources.get("unicode") if isinstance(sources, dict) else None
    if not isinstance(unicode, dict):
        raise DistributionError("installer Unicode source package is absent")
    source_contract_path = bundle / require_relative(
        unicode.get("contract"), "Unicode source contract"
    )
    if sha256_file(source_contract_path) != require_hex(
        unicode.get("contract_sha256"), "Unicode source contract digest"
    ):
        raise DistributionError("installer Unicode source contract differs")
    source_contract = load_json(source_contract_path)
    source_root = bundle / require_relative(
        unicode.get("root"), "Unicode source payload root"
    )
    source_observation = verify_source_contract(source_contract, source_root)
    for field in ("id", "version", "file_count", "total_file_bytes", "files"):
        if source_observation[field] != unicode.get(field):
            raise DistributionError(f"installer Unicode source {field} differs")
    require_absolute(unicode.get("install_root"), "Unicode source install root")
    return manifest


def prefixed(root: Path, logical: Path) -> Path:
    return logical if root == Path("/") else root.joinpath(*logical.parts[1:])


def copy_if_absent(source: Path, destination: Path) -> None:
    if destination.exists() or destination.is_symlink():
        raise DistributionError(f"installer materialization target exists: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, symlinks=True)


def chown_tree(path: Path, uid: int, gid: int) -> None:
    os.chown(path, uid, gid, follow_symlinks=False)
    for candidate in path.rglob("*"):
        os.chown(candidate, uid, gid, follow_symlinks=False)


def materialize(manifest_path: Path, root: Path) -> dict[str, Any]:
    manifest = verify_bundle(manifest_path)
    bundle = manifest_path.parent
    bundle_id = manifest["bundle_id"]
    package_record = manifest["package"]
    package_manifest_source = bundle / package_record["manifest"]
    package_manifest = load_json(package_manifest_source)
    unicode_record = manifest["sources"]["unicode"]
    unicode_contract = bundle / unicode_record["contract"]
    unicode_source = materialize_unicode_source(
        unicode_contract,
        bundle / unicode_record["root"],
        require_absolute(unicode_record["install_root"], "Unicode source install root"),
        root,
    )
    logical_build = Path("/build/laplace/runner/product/build") / bundle_id
    logical_stage = Path("/build/laplace/runner/product/stage") / bundle_id / "root"
    build = prefixed(root, logical_build)
    stage = prefixed(root, logical_stage)
    manifest_output = build / "package-manifest.json"
    receipt_output = build / "package-receipt.json"
    package_release = logical_stage / package_manifest["root"].lstrip("/")
    physical_release = prefixed(root, package_release)
    replay = build.exists() and stage.exists()
    if build.exists() or stage.exists():
        if not manifest_output.is_file() or not receipt_output.is_file() or not physical_release.is_dir():
            raise DistributionError("existing installer materialization is incomplete")
        if manifest_output.read_bytes() != canonical_bytes(package_manifest):
            raise DistributionError("existing installer package manifest differs")
        verify_package_manifest(package_manifest, prefixed(root, logical_stage))
        receipt = load_json(receipt_output)
    else:
        build.mkdir(parents=True, mode=0o750)
        manifest_output.write_bytes(canonical_bytes(package_manifest))
        manifest_output.chmod(0o640)
        copy_if_absent(bundle / package_record["root"], stage)
        status = verify_package_manifest(package_manifest, stage)
        receipt = {
            "schema": RECEIPT_SCHEMA,
            "package_id": status["package_id"],
            "manifest": str(logical_build / "package-manifest.json"),
            "manifest_sha256": sha256_file(manifest_output),
            "physical_root": str(package_release),
            "file_count": status["file_count"],
            "symlink_count": status["symlink_count"],
            "total_file_bytes": status["total_file_bytes"],
            "activation_eligible": True,
            "build_input_closure_complete": True,
            "product_activated": False,
            "distribution_bundle_id": bundle_id,
        }
        receipt_output.write_bytes(canonical_bytes(receipt))
        receipt_output.chmod(0o640)
    if root == Path("/"):
        user = pwd.getpwnam("laplace-runner")
        group = grp.getgrnam("laplace-runner")
        chown_tree(build, user.pw_uid, group.gr_gid)
        chown_tree(prefixed(root, logical_stage.parent), user.pw_uid, group.gr_gid)
    result = {
        "schema": MATERIALIZATION_SCHEMA,
        "bundle_id": bundle_id,
        "package_id": package_record["id"],
        "product_receipt": str(logical_build / "package-receipt.json"),
        "package_manifest": str(logical_build / "package-manifest.json"),
        "package_source_root": str(logical_stage),
        "unicode_source": unicode_source,
        "replay": replay,
    }
    result["receipt_sha256"] = document_identity(result, "receipt_sha256")
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build")
    build.add_argument("--repository", required=True)
    build.add_argument("--product-receipt", required=True)
    build.add_argument("--output-directory", required=True)
    build.add_argument("--unicode-contract")
    build.add_argument("--unicode-source-root")
    build.add_argument("--result")
    verify = subparsers.add_parser("verify")
    verify.add_argument("--manifest", required=True)
    install = subparsers.add_parser("materialize")
    install.add_argument("--manifest", required=True)
    install.add_argument("--root", default="/")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    if arguments.command == "build":
        result = build_bundle(
            Path(arguments.repository),
            Path(arguments.product_receipt),
            Path(arguments.output_directory),
            Path(arguments.unicode_contract) if arguments.unicode_contract else None,
            Path(arguments.unicode_source_root) if arguments.unicode_source_root else None,
        )
    elif arguments.command == "verify":
        result = verify_bundle(Path(arguments.manifest))
    else:
        result = materialize(Path(arguments.manifest), Path(arguments.root))
    content = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if getattr(arguments, "result", None):
        output = Path(arguments.result)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(content, encoding="utf-8")
    print(content, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except DistributionError as error:
        print(f"product distribution: {error}", file=sys.stderr)
        raise SystemExit(1)
