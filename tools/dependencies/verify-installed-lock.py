#!/usr/bin/env python3
"""Validate selected installed providers without treating installation as authority."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any


SCHEMA = "laplace.installed-provider-lock/v1"
EXPECTED_PROVIDERS = {
    "intel-oneapi-runtime": {
        "version": "2026.1.1",
        "root": "/opt/intel/oneapi/compiler/2026.1",
        "roles": {
            "imf-runtime", "svml-runtime", "intlc-runtime", "irc-runtime",
            "irng-runtime", "openmp-runtime", "mathimf-header", "openmp-header",
            "intel-developer-tools-license", "compiler-third-party-notices",
            "openmp-third-party-notices",
        },
    },
    "onetbb": {
        "version": "2023.1.0",
        "root": "/opt/intel/oneapi/tbb/2023.1",
        "roles": {
            "tbb-runtime", "tbbmalloc-runtime", "tbbbind-2-5-runtime",
            "tbb-version-header", "tbb-task-arena-header",
            "tbb-global-control-header", "tbb-parallel-for-header",
            "tbb-blocked-range-header", "tbb-cmake-config", "tbb-cmake-version",
            "tbb-license", "tbb-third-party-notices",
        },
    },
    "onemkl": {
        "version": "2026.1.0",
        "root": "/opt/intel/oneapi/mkl/2026.1",
        "roles": {
            "mkl-runtime", "mkl-core-runtime", "mkl-lp64-runtime",
            "mkl-sequential-runtime", "mkl-tbb-thread-runtime",
            "mkl-vml-avx2-runtime", "mkl-root-header", "mkl-version-header",
            "mkl-service-header", "mkl-vml-header", "mkl-vml-defines-header",
            "mkl-vml-functions-header", "mkl-cblas-header", "mkl-lapacke-header",
            "mkl-sparse-blas-header", "mkl-cmake-config", "mkl-cmake-version",
            "mkl-license", "mkl-third-party-notices", "mkl-onetbb-notices",
        },
    },
}
EXPECTED_PROVIDER_SELECTION_SHA256 = {
    "intel-oneapi-runtime": "f14ba1d659dcc7c618386955da3daf025da48ebe5589cad3875028db6aeb12e6",
    "onetbb": "b57274d401cde4b11d7287d290ba86df86e95bbfb5e14984a5e0a4a942a2f00d",
    "onemkl": "01d6a49e0f32a724ddf7cd79166415d9b0acb81ee2bf344391f3981cd8f9fa46",
}
FILE_CLASSES = {"runtime-object", "header", "cmake-input", "license"}
HEX_SHA256 = re.compile(r"[0-9a-f]{64}")
SONAME_PATTERN = re.compile(r"\(SONAME\).*\[([^]]+)\]")


class InstalledLockError(RuntimeError):
    """Raised when an installed-provider selection contract is invalid."""


def reject_duplicate_object_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    document: dict[str, object] = {}
    for key, value in pairs:
        if key in document:
            raise InstalledLockError(f"duplicate JSON object key: {key}")
        document[key] = value
    return document


def load_lock(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_object_keys,
        )
    except (OSError, json.JSONDecodeError) as error:
        raise InstalledLockError(f"cannot read installed provider lock {path}: {error}") from error
    if not isinstance(document, dict):
        raise InstalledLockError("installed provider lock must be an object")
    return document


def require(condition: bool, message: str) -> None:
    if not condition:
        raise InstalledLockError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def selection_sha256(providers: dict[str, Any]) -> str:
    payload = json.dumps(
        providers, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def under_root(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def validate_lock(document: dict[str, Any]) -> dict[str, Any]:
    require(document.get("schema") == SCHEMA, f"installed provider lock must use {SCHEMA}")
    require(
        document.get("selection") == "installed-bytes-selected-not-product-runtime-activated",
        "installed provider selection crossed the activation boundary",
    )
    providers = document.get("providers")
    require(isinstance(providers, dict), "installed provider map is missing")
    require(set(providers) == set(EXPECTED_PROVIDERS), "installed provider set changed")
    computed_fingerprint = selection_sha256(providers)
    require(
        document.get("provider_selection_sha256") == computed_fingerprint,
        "installed provider selection fingerprint differs",
    )

    file_count = 0
    for provider_id, expected in EXPECTED_PROVIDERS.items():
        provider = providers[provider_id]
        require(isinstance(provider, dict), f"{provider_id} provider is not an object")
        require(provider.get("version") == expected["version"], f"{provider_id} version changed")
        root_text = provider.get("immutable_root")
        require(root_text == expected["root"], f"{provider_id} immutable root changed")
        provider_payload = json.dumps(
            provider, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        require(
            hashlib.sha256(provider_payload).hexdigest()
            == EXPECTED_PROVIDER_SELECTION_SHA256[provider_id],
            f"{provider_id} exact selected provider identity differs",
        )
        require("latest" not in Path(root_text).parts, f"{provider_id} uses an ambient latest path")
        root = Path(root_text)
        packages = provider.get("package_versions")
        require(isinstance(packages, dict) and packages, f"{provider_id} package versions are missing")
        require(
            all(isinstance(name, str) and name and isinstance(version, str) and version for name, version in packages.items()),
            f"{provider_id} package version entry is invalid",
        )
        license_id = provider.get("license_id")
        require(isinstance(license_id, str) and license_id, f"{provider_id} license identity is missing")

        files = provider.get("files")
        require(isinstance(files, list) and files, f"{provider_id} file selection is missing")
        roles: dict[str, dict[str, Any]] = {}
        classes: set[str] = set()
        paths: set[str] = set()
        for entry in files:
            require(isinstance(entry, dict), f"{provider_id} file entry is invalid")
            role = entry.get("role")
            file_class = entry.get("class")
            raw_path = entry.get("path")
            require(isinstance(role, str) and role and role not in roles, f"{provider_id} has an invalid or duplicate file role")
            require(file_class in FILE_CLASSES, f"{provider_id}.{role} has an invalid file class")
            require(isinstance(raw_path, str) and raw_path and raw_path not in paths, f"{provider_id}.{role} has an invalid or duplicate path")
            path = Path(raw_path)
            require(path.is_absolute() and under_root(path, root), f"{provider_id}.{role} escapes its immutable versioned root")
            require("latest" not in path.parts, f"{provider_id}.{role} uses an ambient latest path")
            require(isinstance(entry.get("bytes"), int) and entry["bytes"] > 0, f"{provider_id}.{role} byte size is invalid")
            require(isinstance(entry.get("sha256"), str) and HEX_SHA256.fullmatch(entry["sha256"]) is not None, f"{provider_id}.{role} digest is invalid")
            if file_class == "runtime-object":
                require(isinstance(entry.get("soname"), str) and entry["soname"], f"{provider_id}.{role} SONAME is missing")
            elif "soname" in entry:
                raise InstalledLockError(f"{provider_id}.{role} non-runtime file has a SONAME")
            roles[role] = entry
            classes.add(file_class)
            paths.add(raw_path)
        require(set(roles) == expected["roles"], f"{provider_id} required object/header/license role set changed")
        require({"runtime-object", "header", "license"}.issubset(classes), f"{provider_id} lacks runtime, header, or license identity")

        selection = provider.get("cmake_selection")
        require(isinstance(selection, dict), f"{provider_id} CMake selection is missing")
        for field in ("include_root", "library_root"):
            raw_path = selection.get(field)
            require(isinstance(raw_path, str) and raw_path and Path(raw_path).is_absolute(), f"{provider_id}.{field} is invalid")
            require(under_root(Path(raw_path), root) and "latest" not in Path(raw_path).parts, f"{provider_id}.{field} is not immutable")
        targets = selection.get("targets")
        require(isinstance(targets, dict) and targets, f"{provider_id} CMake target map is missing")
        selected_roles = set()
        for target, target_roles in targets.items():
            require(isinstance(target, str) and target, f"{provider_id} has an invalid CMake target")
            require(isinstance(target_roles, list) and target_roles, f"{provider_id}.{target} has no selected objects")
            require(len(target_roles) == len(set(target_roles)), f"{provider_id}.{target} repeats an object")
            require(all(role in roles and roles[role]["class"] == "runtime-object" for role in target_roles), f"{provider_id}.{target} selects an unknown or non-runtime role")
            selected_roles.update(target_roles)
        runtime_roles = {role for role, entry in roles.items() if entry["class"] == "runtime-object"}
        require(selected_roles == runtime_roles, f"{provider_id} runtime object is not bound to a CMake selection")
        file_count += len(files)

    boundaries = document.get("authority_boundaries", {})
    require(boundaries.get("ambient-latest-symlinks-are-authority") is False, "ambient latest path became authority")
    require(boundaries.get("installed-bytes-are-source-authority") is False, "installed bytes became source authority")
    require(boundaries.get("selection-activates-product-runtime") is False, "provider selection claims runtime activation")
    require(boundaries.get("selection-implements-unicode") is False, "provider selection claims Unicode implementation")
    require(boundaries.get("selection-implements-scheduler") is False, "provider selection claims scheduler implementation")
    require(boundaries.get("loaded-runtime-object-closure-requires-package-receipt") is True, "loaded object closure no longer requires a package receipt")
    return {"providers": providers, "file_count": file_count, "selection_sha256": computed_fingerprint}


def verify_files(report: dict[str, Any], readelf: Path, dpkg_query: Path) -> None:
    require(readelf.is_file(), f"ELF inspector is missing: {readelf}")
    require(dpkg_query.is_file(), f"package query tool is missing: {dpkg_query}")
    for provider_id, provider in report["providers"].items():
        for package, version in provider["package_versions"].items():
            result = subprocess.run(
                [str(dpkg_query), "-W", "-f=${Version}", package],
                check=False,
                capture_output=True,
                text=True,
            )
            require(result.returncode == 0, f"{provider_id} package is missing: {package}")
            require(result.stdout == version, f"{provider_id} package version differs: {package}")
        for entry in provider["files"]:
            path = Path(entry["path"])
            require(path.is_file() and not path.is_symlink(), f"selected installed file is missing, non-regular, or a symlink: {path}")
            require(path.stat().st_size == entry["bytes"], f"selected installed file size differs: {path}")
            require(sha256(path) == entry["sha256"], f"selected installed file digest differs: {path}")
            if entry["class"] == "runtime-object":
                result = subprocess.run(
                    [str(readelf), "-d", str(path)],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                require(result.returncode == 0, f"cannot inspect selected runtime object: {path}")
                sonames = SONAME_PATTERN.findall(result.stdout)
                require(sonames == [entry["soname"]], f"selected runtime SONAME differs: {path}")


def cmake_quote(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def write_cmake(report: dict[str, Any], output: Path) -> None:
    lines = [
        "# Generated from dependencies/installed-lock.json; do not edit.",
        f'set(LAPLACE_ONEAPI_SELECTION_SHA256 "{report["selection_sha256"]}")',
    ]
    for provider_id, provider in sorted(report["providers"].items()):
        prefix = "LAPLACE_ONEAPI_" + provider_id.upper().replace("-", "_")
        lines.append(f'set({prefix}_VERSION "{cmake_quote(provider["version"])}")')
        lines.append(f'set({prefix}_ROOT "{cmake_quote(provider["immutable_root"])}")')
        selection = provider["cmake_selection"]
        lines.append(f'set({prefix}_INCLUDE_ROOT "{cmake_quote(selection["include_root"])}")')
        lines.append(f'set({prefix}_LIBRARY_ROOT "{cmake_quote(selection["library_root"])}")')
        if "config_directory" in selection:
            lines.append(f'set({prefix}_CONFIG_DIRECTORY "{cmake_quote(selection["config_directory"])}")')
        for entry in provider["files"]:
            role = entry["role"].upper().replace("-", "_")
            lines.append(f'set({prefix}_{role} "{cmake_quote(entry["path"])}")')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lock", nargs="?", type=Path, default=Path(__file__).resolve().parents[2] / "dependencies/installed-lock.json")
    parser.add_argument("--verify-files", action="store_true")
    parser.add_argument("--readelf", type=Path, default=Path("/usr/bin/readelf"))
    parser.add_argument("--dpkg-query", type=Path, default=Path("/usr/bin/dpkg-query"))
    parser.add_argument("--cmake-output", type=Path)
    arguments = parser.parse_args()
    try:
        report = validate_lock(load_lock(arguments.lock))
        if arguments.verify_files:
            verify_files(report, arguments.readelf, arguments.dpkg_query)
        if arguments.cmake_output is not None:
            require(arguments.verify_files, "CMake selection emission requires exact installed-file verification")
            write_cmake(report, arguments.cmake_output)
    except InstalledLockError as error:
        parser.error(str(error))
    disposition = "verified-installed-bytes" if arguments.verify_files else "validated-contract-without-installed-byte-claim"
    print(json.dumps({"disposition": disposition, "file_count": report["file_count"], "provider_count": len(report["providers"]), "selection_sha256": report["selection_sha256"]}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
