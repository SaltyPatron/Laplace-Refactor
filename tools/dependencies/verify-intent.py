#!/usr/bin/env python3
"""Verify dependency capability intent against every committed source lock."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


INTENT_SCHEMA = "laplace.dependency-intent/v1"
GIT_SCHEMA = "laplace.dependency-lock/v1"
RELEASE_SCHEMA = "laplace.release-lock/v1"
GRAMMAR_SCHEMA = "laplace.tree-sitter-grammar-lock/v2"
ROOT_SCHEMA = "laplace.dependency-roots/v1"
ARTIFACT_SCHEMA = "laplace.artifact-lock/v1"
CRITICAL_COMPONENTS = {
    "blake3",
    "postgresql",
    "postgis",
    "gdal",
    "geos",
    "proj",
    "intel-llvm",
    "intel-oneapi-runtime",
    "onemkl",
    "onetbb",
    "eigen",
    "spectra",
    "tree-sitter",
    "tree-sitter-grammars",
    "dotnet",
    "stockfish",
    "stockfish-network-assets",
    "lichess",
    "syzygy-table-sets",
    "unicode-standard-data",
    "github-actions-runner",
}
CRITICAL_ROOTS = {
    "downloaded-source-data",
    "downloaded-model-data",
    "verified-source-cache",
    "installed-product-assets",
    "intel-oneapi-assets",
}


class IntentError(RuntimeError):
    """Raised when dependency intent and committed source identity diverge."""


def canonical_release_version(value: object, component: str, lock_kind: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise IntentError(f"{component} {lock_kind} version must be a non-empty string")
    normalized = value.strip()
    if normalized.startswith("REL_"):
        normalized = normalized[4:].replace("_", ".")
    elif normalized.startswith("v") and len(normalized) > 1 and normalized[1].isdigit():
        normalized = normalized[1:]
    return normalized


def license_digest_map(
    licenses: object, component: str, lock_kind: str
) -> dict[str, str]:
    if not isinstance(licenses, list) or not licenses:
        raise IntentError(f"{component} {lock_kind} licenses must be a non-empty array")
    result: dict[str, str] = {}
    for entry in licenses:
        if not isinstance(entry, dict):
            raise IntentError(f"{component} {lock_kind} license entry must be an object")
        path = entry.get("path")
        digest = entry.get("sha256")
        if not isinstance(path, str) or not path:
            raise IntentError(f"{component} {lock_kind} license path is invalid")
        if not isinstance(digest, str) or len(digest) != 64:
            raise IntentError(f"{component} {lock_kind} license digest is invalid")
        name = Path(path).name
        prior = result.get(name)
        if prior is not None and prior != digest:
            raise IntentError(
                f"{component} {lock_kind} has conflicting license identities for {name}"
            )
        result[name] = digest
    return result


def validate_release_and_git_coherence(
    repo_root: Path, components: list[dict[str, object]]
) -> None:
    git_lock = load_json(repo_root / "dependencies/lock.json")
    release_lock = load_json(repo_root / "dependencies/release-lock.json")
    git_dependencies = git_lock.get("dependencies")
    release_archives = release_lock.get("archives")
    if not isinstance(git_dependencies, dict) or not isinstance(release_archives, dict):
        raise IntentError("dependency locks do not contain their required maps")

    for component in components:
        if component.get("selection") != "locked-release-and-git":
            continue
        identifier = component["id"]
        assert isinstance(identifier, str)
        lock_entries = component.get("lock_entries")
        assert isinstance(lock_entries, list)
        git_names = [item[4:] for item in lock_entries if item.startswith("git:")]
        release_names = [
            item[8:] for item in lock_entries if item.startswith("release:")
        ]
        if len(git_names) != 1 or len(release_names) != 1:
            raise IntentError(
                f"{identifier} locked-release-and-git must select exactly one Git "
                "source and one release archive"
            )
        git_entry = git_dependencies.get(git_names[0])
        release_entry = release_archives.get(release_names[0])
        if not isinstance(git_entry, dict) or not isinstance(release_entry, dict):
            raise IntentError(f"{identifier} selected source is absent from its lock")
        git_version = canonical_release_version(
            git_entry.get("version"), identifier, "Git"
        )
        release_version = canonical_release_version(
            release_entry.get("version"), identifier, "release"
        )
        if git_version != release_version:
            raise IntentError(
                f"{identifier} Git/release version mismatch: "
                f"{git_entry.get('version')} != {release_entry.get('version')}"
            )
        git_licenses = license_digest_map(
            git_entry.get("licenses"), identifier, "Git"
        )
        release_licenses = license_digest_map(
            release_entry.get("licenses"), identifier, "release"
        )
        if git_licenses != release_licenses:
            raise IntentError(f"{identifier} Git/release license identity mismatch")


def load_json(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise IntentError(f"cannot read {path}: {error}") from error
    if not isinstance(document, dict):
        raise IntentError(f"{path} must contain a JSON object")
    return document


def string_list(value: object, field: str, component: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise IntentError(f"{component}.{field} must be a non-empty array")
    if any(not isinstance(item, str) or not item.strip() for item in value):
        raise IntentError(f"{component}.{field} must contain non-empty strings")
    if len(value) != len(set(value)):
        raise IntentError(f"{component}.{field} contains duplicate values")
    return value


def expected_lock_entries(repo_root: Path) -> set[str]:
    git_lock = load_json(repo_root / "dependencies/lock.json")
    release_lock = load_json(repo_root / "dependencies/release-lock.json")
    artifact_lock = load_json(repo_root / "dependencies/artifact-lock.json")
    grammar_lock = load_json(repo_root / "dependencies/tree-sitter-grammars.lock.json")
    if git_lock.get("schema") != GIT_SCHEMA:
        raise IntentError(f"dependencies/lock.json must use {GIT_SCHEMA}")
    if release_lock.get("schema") != RELEASE_SCHEMA:
        raise IntentError(f"dependencies/release-lock.json must use {RELEASE_SCHEMA}")
    if artifact_lock.get("schema") != ARTIFACT_SCHEMA:
        raise IntentError(f"dependencies/artifact-lock.json must use {ARTIFACT_SCHEMA}")
    if grammar_lock.get("schema") != GRAMMAR_SCHEMA:
        raise IntentError(
            f"dependencies/tree-sitter-grammars.lock.json must use {GRAMMAR_SCHEMA}"
        )
    dependencies = git_lock.get("dependencies")
    archives = release_lock.get("archives")
    artifacts = artifact_lock.get("artifacts")
    repositories = grammar_lock.get("repositories")
    if not isinstance(dependencies, dict) or not dependencies:
        raise IntentError("Git dependency lock has no dependencies")
    if not isinstance(archives, dict) or not archives:
        raise IntentError("release dependency lock has no archives")
    if not isinstance(artifacts, dict) or not artifacts:
        raise IntentError("artifact dependency lock has no artifacts")
    for name, artifact in artifacts.items():
        if not isinstance(name, str) or not name or not isinstance(artifact, dict):
            raise IntentError("artifact dependency lock contains an invalid entry")
        for field in ("version", "filename", "url", "sha256", "admission"):
            if not isinstance(artifact.get(field), str) or not artifact[field]:
                raise IntentError(f"artifact {name}.{field} must be a non-empty string")
        digest = artifact["sha256"]
        if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
            raise IntentError(f"artifact {name}.sha256 must be lowercase SHA-256")
        if not isinstance(artifact.get("size"), int) or artifact["size"] <= 0:
            raise IntentError(f"artifact {name}.size must be a positive integer")
    if not isinstance(repositories, list) or not repositories:
        raise IntentError("grammar dependency lock has no repositories")
    return {
        *(f"git:{name}" for name in dependencies),
        *(f"release:{name}" for name in archives),
        *(f"artifact:{name}" for name in artifacts),
        "grammars:tree-sitter-grammars",
    }


def validate_roots(repo_root: Path) -> int:
    document = load_json(repo_root / "dependencies/roots.json")
    if document.get("schema") != ROOT_SCHEMA:
        raise IntentError(f"dependencies/roots.json must use {ROOT_SCHEMA}")
    roots = document.get("roots")
    if not isinstance(roots, list) or not roots:
        raise IntentError("dependencies/roots.json must contain roots")
    identifiers: set[str] = set()
    environments: set[str] = set()
    for root in roots:
        if not isinstance(root, dict):
            raise IntentError("dependency root must be a JSON object")
        identifier = root.get("id")
        if not isinstance(identifier, str) or not identifier:
            raise IntentError("dependency root has no identifier")
        if identifier in identifiers:
            raise IntentError(f"duplicate dependency root: {identifier}")
        identifiers.add(identifier)
        environment = root.get("environment")
        if not isinstance(environment, str) or not environment:
            raise IntentError(f"{identifier}.environment must be a non-empty string")
        if environment in environments:
            raise IntentError(f"duplicate dependency root environment: {environment}")
        environments.add(environment)
        for field in ("class", "intent", "identity_requirement"):
            if not isinstance(root.get(field), str) or not root[field].strip():
                raise IntentError(f"{identifier}.{field} must be a non-empty string")
        path_fields = ("linux_discovery_path", "repository_discovery_path")
        paths = [root.get(field) for field in path_fields if root.get(field) is not None]
        if len(paths) != 1 or not isinstance(paths[0], str) or not paths[0]:
            raise IntentError(
                f"{identifier} must declare exactly one non-empty discovery path"
            )
    missing = sorted(CRITICAL_ROOTS - identifiers)
    if missing:
        raise IntentError("critical dependency root is missing: " + ", ".join(missing))
    return len(roots)


def validate(repo_root: Path) -> tuple[int, int, int, int]:
    intent = load_json(repo_root / "dependencies/intent.json")
    if intent.get("schema") != INTENT_SCHEMA:
        raise IntentError(f"dependencies/intent.json must use {INTENT_SCHEMA}")
    principles = intent.get("principles")
    string_list(principles, "principles", "document")
    components = intent.get("components")
    if not isinstance(components, list) or not components:
        raise IntentError("dependencies/intent.json must contain components")

    identifiers: set[str] = set()
    observed_lock_entries: dict[str, str] = {}
    unselected_count = 0
    for component in components:
        if not isinstance(component, dict):
            raise IntentError("dependency component must be a JSON object")
        identifier = component.get("id")
        if not isinstance(identifier, str) or not identifier:
            raise IntentError("dependency component has no identifier")
        if identifier in identifiers:
            raise IntentError(f"duplicate dependency intent: {identifier}")
        identifiers.add(identifier)
        for field in ("class", "boundary", "selection"):
            if not isinstance(component.get(field), str) or not component[field].strip():
                raise IntentError(f"{identifier}.{field} must be a non-empty string")
        string_list(component.get("profiles"), "profiles", identifier)
        string_list(component.get("intent"), "intent", identifier)
        lock_entries = component.get("lock_entries")
        if not isinstance(lock_entries, list):
            raise IntentError(f"{identifier}.lock_entries must be an array")
        if any(not isinstance(item, str) or not item for item in lock_entries):
            raise IntentError(f"{identifier}.lock_entries contains an invalid value")
        if len(lock_entries) != len(set(lock_entries)):
            raise IntentError(f"{identifier}.lock_entries contains duplicates")
        for lock_entry in lock_entries:
            prior = observed_lock_entries.get(lock_entry)
            if prior is not None:
                raise IntentError(
                    f"dependency lock entry {lock_entry} is claimed by {prior} and {identifier}"
                )
            observed_lock_entries[lock_entry] = identifier
        selection = component["selection"]
        if "locked" in selection and not lock_entries:
            raise IntentError(f"{identifier} is marked locked without a lock entry")
        if "selection-required" in selection:
            unselected_count += 1

    validate_release_and_git_coherence(repo_root, components)

    missing_critical = sorted(CRITICAL_COMPONENTS - identifiers)
    if missing_critical:
        raise IntentError(
            "critical dependency intent is missing: " + ", ".join(missing_critical)
        )

    expected = expected_lock_entries(repo_root)
    observed = set(observed_lock_entries)
    missing = sorted(expected - observed)
    unknown = sorted(observed - expected)
    if missing:
        raise IntentError(
            "locked sources have no dependency intent: " + ", ".join(missing)
        )
    if unknown:
        raise IntentError(
            "dependency intent references unknown source locks: " + ", ".join(unknown)
        )
    root_count = validate_roots(repo_root)
    return len(components), len(expected), unselected_count, root_count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "repo_root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    arguments = parser.parse_args()
    try:
        component_count, lock_count, unselected_count, root_count = validate(
            arguments.repo_root.resolve()
        )
    except IntentError as error:
        parser.error(str(error))
    print(
        f"verified {component_count} dependency intents against {lock_count} source "
        f"locks and {root_count} dependency roots; {unselected_count} component families "
        "still require a version selection"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
