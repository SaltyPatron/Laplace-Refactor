#!/usr/bin/env python3
"""Catalog the observed source estate against exact repository source profiles.

This is a development discovery/planning tool. It never promotes local paths,
file extensions, parser success, or observed inventory labels into source
authority. A processing route is selected only when one repository source
profile matches a physical artifact graph byte-for-byte. Otherwise the result
stays explicit as nonconforming, ambiguous, excluded, or profile-required.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INVENTORY = ROOT / "state" / "vault-inventory.json"
DEFAULT_PROFILE_MODEL = ROOT / "contracts" / "source-profile-model.json"
DEFAULT_PROFILE_ROOT = ROOT / "contracts" / "sources"

CATALOG_SCHEMA = "laplace.source-estate-catalog/v1"
CATALOG_CLASSIFICATION = "derived-development-planning-not-source-authority"

EXCLUDED_STATUSES = {
    "duplicate",
    "quarantined",
    "fixture-only",
    "operational-residue",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    require(isinstance(value, dict), f"{path} is not a JSON object")
    return value


def sha256_file(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def relative_display(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def load_profiles(profile_root: Path) -> list[dict[str, Any]]:
    profiles: list[dict[str, Any]] = []
    for path in sorted(profile_root.glob("*.json")):
        document = load_json(path)
        artifacts = document.get("artifacts")
        coordinate = document.get("coordinate")
        syntax = document.get("syntax_authority")
        recipe = document.get("recipe_program")
        if not (
            isinstance(artifacts, list)
            and isinstance(coordinate, dict)
            and isinstance(syntax, dict)
            and isinstance(recipe, dict)
        ):
            continue

        normalized_artifacts: list[dict[str, Any]] = []
        for artifact in artifacts:
            require(isinstance(artifact, dict), f"{path}: artifact is not an object")
            local_path = artifact.get("local_discovery_path")
            byte_count = artifact.get("byte_count")
            digest = artifact.get("sha256")
            require(
                isinstance(local_path, str) and local_path,
                f"{path}: artifact lacks local_discovery_path",
            )
            require(
                isinstance(byte_count, int) and byte_count >= 0,
                f"{path}: artifact lacks byte_count",
            )
            require(
                isinstance(digest, str) and len(digest) == 64,
                f"{path}: artifact lacks SHA-256",
            )
            normalized_artifacts.append(
                {
                    "local_discovery_path": local_path,
                    "byte_count": byte_count,
                    "sha256": digest.lower(),
                }
            )

        provider = syntax.get("provider")
        recipe_coordinate = recipe.get("coordinate")
        require(
            isinstance(provider, str) and provider,
            f"{path}: syntax authority lacks provider",
        )
        require(
            isinstance(recipe_coordinate, str) and recipe_coordinate,
            f"{path}: recipe program lacks coordinate",
        )
        profiles.append(
            {
                "path": relative_display(path, ROOT),
                "schema": document.get("schema"),
                "coordinate": coordinate,
                "syntax_provider": provider,
                "recipe_coordinate": recipe_coordinate,
                "artifacts": normalized_artifacts,
            }
        )
    return profiles


def inspect_profile_candidate(
    profile: dict[str, Any],
    candidate_root: Path,
) -> dict[str, Any] | None:
    observations: list[dict[str, Any]] = []
    existing = 0
    exact = 0

    for artifact in profile["artifacts"]:
        path = candidate_root / artifact["local_discovery_path"]
        observation: dict[str, Any] = {
            "relative_path": artifact["local_discovery_path"],
            "expected_byte_count": artifact["byte_count"],
            "expected_sha256": artifact["sha256"],
            "exists": path.is_file(),
        }
        if path.is_file():
            existing += 1
            observed_bytes = path.stat().st_size
            observation["observed_byte_count"] = observed_bytes
            if observed_bytes == artifact["byte_count"]:
                observed_digest = sha256_file(path)
                observation["observed_sha256"] = observed_digest
                observation["exact"] = observed_digest == artifact["sha256"]
            else:
                observation["exact"] = False
            if observation["exact"]:
                exact += 1
        else:
            observation["exact"] = False
        observations.append(observation)

    if existing == 0:
        return None

    return {
        "entry": candidate_root.name,
        "artifact_count": len(profile["artifacts"]),
        "existing_artifacts": existing,
        "exact_artifacts": exact,
        "exact": exact == len(profile["artifacts"]),
        "artifacts": observations,
    }


def resolve_physical_profiles(
    source_root: Path,
    profiles: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    require(source_root.is_dir(), f"source root is unavailable: {source_root}")
    entries = sorted(path for path in source_root.iterdir() if path.is_dir())
    resolved: list[dict[str, Any]] = []

    for profile in profiles:
        candidates = [
            candidate
            for entry in entries
            if (candidate := inspect_profile_candidate(profile, entry)) is not None
        ]
        exact_entries = [
            candidate["entry"] for candidate in candidates if candidate["exact"]
        ]
        if len(exact_entries) == 1:
            disposition = "resolved-exact-profile"
        elif len(exact_entries) > 1:
            disposition = "ambiguous-exact-profile"
        elif candidates:
            disposition = "profile-nonconforming"
        else:
            disposition = "profile-not-present"

        resolved.append(
            {
                "profile_path": profile["path"],
                "profile_coordinate": profile["coordinate"],
                "syntax_provider": profile["syntax_provider"],
                "recipe_coordinate": profile["recipe_coordinate"],
                "disposition": disposition,
                "exact_entries": exact_entries,
                "candidates": candidates,
            }
        )
    return resolved


def profile_resolutions_by_entry(
    physical_profiles: Iterable[dict[str, Any]],
) -> dict[str, list[dict[str, Any]]]:
    by_entry: dict[str, list[dict[str, Any]]] = {}
    for resolution in physical_profiles:
        for candidate in resolution["candidates"]:
            by_entry.setdefault(candidate["entry"], []).append(
                {
                    "profile_path": resolution["profile_path"],
                    "profile_coordinate": resolution["profile_coordinate"],
                    "syntax_provider": resolution["syntax_provider"],
                    "recipe_coordinate": resolution["recipe_coordinate"],
                    "profile_disposition": resolution["disposition"],
                    "candidate_exact": candidate["exact"],
                    "candidate_artifact_count": candidate["artifact_count"],
                    "candidate_existing_artifacts": candidate["existing_artifacts"],
                    "candidate_exact_artifacts": candidate["exact_artifacts"],
                }
            )
    return by_entry


def unresolved_proposal(
    required_sections: list[str],
    gaps: list[str],
) -> dict[str, Any]:
    return {
        "classification": "candidate-profile-work-not-authority",
        "required_sections": required_sections,
        "observed_gaps": gaps,
        "syntax_selection": "unresolved-until-versioned-profile-binds-provider",
        "recipe_selection": "unresolved-until-versioned-profile-binds-recipe",
    }


def catalog_entry(
    root_kind: str,
    item: dict[str, Any],
    required_sections: list[str],
    matches: list[dict[str, Any]],
    physical_verified: bool,
) -> dict[str, Any]:
    name = item.get("name")
    status = item.get("status")
    gaps = item.get("missing")
    require(isinstance(name, str) and name, f"{root_kind} inventory entry lacks name")
    require(isinstance(status, str) and status, f"{root_kind}/{name} lacks status")
    require(isinstance(gaps, list), f"{root_kind}/{name} lacks missing-gap list")

    entry: dict[str, Any] = {
        "root_kind": root_kind,
        "observed_name": name,
        "observed_status": status,
        "observed_roles": item.get("roles", []),
        "observed_modalities": item.get("modalities", []),
        "source_identity_from_path": False,
    }

    if status in EXCLUDED_STATUSES:
        entry["processing"] = {
            "disposition": "excluded-from-profile-selection",
            "reason": status,
        }
        return entry

    exact_matches = [
        match
        for match in matches
        if match["candidate_exact"]
        and match["profile_disposition"] == "resolved-exact-profile"
    ]
    ambiguous_exact_matches = [
        match
        for match in matches
        if match["candidate_exact"]
        and match["profile_disposition"] == "ambiguous-exact-profile"
    ]
    nonexact_matches = [match for match in matches if not match["candidate_exact"]]

    if physical_verified and len(exact_matches) == 1:
        match = exact_matches[0]
        entry["processing"] = {
            "disposition": "resolved-exact-profile",
            "profile_path": match["profile_path"],
            "profile_coordinate": match["profile_coordinate"],
            "syntax_provider": match["syntax_provider"],
            "recipe_coordinate": match["recipe_coordinate"],
            "basis": [
                "repository-profile-coordinate",
                "physical-artifact-byte-count",
                "physical-artifact-sha256",
                "profile-declared-syntax-authority",
                "profile-declared-recipe-program",
            ],
        }
    elif physical_verified and (len(exact_matches) > 1 or ambiguous_exact_matches):
        entry["processing"] = {
            "disposition": "ambiguous-exact-profile",
            "candidate_profiles": sorted(
                {
                    match["profile_path"]
                    for match in exact_matches + ambiguous_exact_matches
                }
            ),
            "proposal": unresolved_proposal(required_sections, gaps),
        }
    elif physical_verified and nonexact_matches:
        entry["processing"] = {
            "disposition": "profile-nonconforming",
            "candidate_profiles": [
                {
                    "profile_path": match["profile_path"],
                    "existing_artifacts": match["candidate_existing_artifacts"],
                    "exact_artifacts": match["candidate_exact_artifacts"],
                    "artifact_count": match["candidate_artifact_count"],
                }
                for match in nonexact_matches
            ],
            "proposal": unresolved_proposal(required_sections, gaps),
        }
    else:
        entry["processing"] = {
            "disposition": (
                "profile-required"
                if physical_verified
                else "physical-profile-resolution-required"
            ),
            "proposal": unresolved_proposal(required_sections, gaps),
        }

    return entry


def build_catalog(
    inventory: dict[str, Any],
    profile_model: dict[str, Any],
    profiles: list[dict[str, Any]],
    source_root: Path | None = None,
    model_root: Path | None = None,
) -> dict[str, Any]:
    require(
        inventory.get("schema") == "laplace.vault-source-model-inventory/v1",
        "unexpected vault inventory schema",
    )
    require(
        inventory.get("classification")
        == "observed-development-state-not-source-authority",
        "vault inventory was promoted to authority",
    )
    require(
        profile_model.get("schema") == "laplace.source-profile-model/v1",
        "unexpected source profile model schema",
    )
    required_section_map = profile_model.get("required_sections")
    require(
        isinstance(required_section_map, dict) and required_section_map,
        "profile model lacks required sections",
    )
    required_sections = sorted(required_section_map)

    physical_profiles: list[dict[str, Any]] = []
    if source_root is not None:
        physical_profiles = resolve_physical_profiles(source_root, profiles)
    if model_root is not None:
        require(model_root.is_dir(), f"model root is unavailable: {model_root}")
    by_entry = profile_resolutions_by_entry(physical_profiles)

    data_entries = inventory.get("data_root", {}).get("entries")
    model_entries = inventory.get("model_root", {}).get("entries")
    require(isinstance(data_entries, list), "inventory data_root entries are missing")
    require(isinstance(model_entries, list), "inventory model_root entries are missing")

    inventory_data_names = {
        item.get("name") for item in data_entries if isinstance(item, dict)
    }
    inventory_model_names = {
        item.get("name") for item in model_entries if isinstance(item, dict)
    }
    physical_data_names = (
        {path.name for path in source_root.iterdir() if path.is_dir()}
        if source_root is not None
        else set()
    )
    physical_model_names = (
        {path.name for path in model_root.iterdir() if path.is_dir()}
        if model_root is not None
        else set()
    )

    entries = [
        catalog_entry(
            "data",
            item,
            required_sections,
            by_entry.get(item.get("name"), []),
            source_root is not None,
        )
        for item in data_entries
    ]
    entries.extend(
        catalog_entry(
            "model",
            item,
            required_sections,
            [],
            model_root is not None,
        )
        for item in model_entries
    )

    catalog: dict[str, Any] = {
        "schema": CATALOG_SCHEMA,
        "classification": CATALOG_CLASSIFICATION,
        "laws": {
            "path_is_authority": False,
            "parser_success_is_semantic_authority": False,
            "unprofiled_processing_is_guessed": False,
            "exact_profile_resolution": (
                "one repository profile whose declared artifact graph matches one "
                "physical top-level estate entry by byte count and SHA-256"
            ),
            "ambiguous_or_missing_behavior": "retain explicit unresolved disposition",
        },
        "inputs": {
            "inventory_schema": inventory["schema"],
            "inventory_observed_at_utc": inventory.get("observed_at_utc"),
            "profile_model_schema": profile_model["schema"],
            "repository_profile_count": len(profiles),
            "physical_source_root_verified": source_root is not None,
            "physical_model_root_observed": model_root is not None,
        },
        "estate_drift": {
            "data_missing_from_physical_root": (
                sorted(inventory_data_names - physical_data_names)
                if source_root is not None
                else []
            ),
            "data_uninventoried_physical_entries": (
                sorted(physical_data_names - inventory_data_names)
                if source_root is not None
                else []
            ),
            "model_missing_from_physical_root": (
                sorted(inventory_model_names - physical_model_names)
                if model_root is not None
                else []
            ),
            "model_uninventoried_physical_entries": (
                sorted(physical_model_names - inventory_model_names)
                if model_root is not None
                else []
            ),
        },
        "profiles": (
            physical_profiles
            if source_root is not None
            else [
                {
                    "profile_path": profile["path"],
                    "profile_coordinate": profile["coordinate"],
                    "syntax_provider": profile["syntax_provider"],
                    "recipe_coordinate": profile["recipe_coordinate"],
                    "disposition": "physical-resolution-required",
                }
                for profile in profiles
            ]
        ),
        "entries": entries,
    }
    validate_catalog(catalog, required_sections)
    return catalog


def validate_catalog(catalog: dict[str, Any], required_sections: list[str]) -> None:
    require(catalog.get("schema") == CATALOG_SCHEMA, "catalog schema drift")
    require(
        catalog.get("classification") == CATALOG_CLASSIFICATION,
        "catalog classification drift",
    )
    laws = catalog.get("laws")
    require(isinstance(laws, dict), "catalog laws missing")
    require(
        laws.get("path_is_authority") is False,
        "local path was promoted to source authority",
    )
    require(
        laws.get("parser_success_is_semantic_authority") is False,
        "parser success was promoted to semantic authority",
    )
    require(
        laws.get("unprofiled_processing_is_guessed") is False,
        "unprofiled processing was guessed",
    )

    entries = catalog.get("entries")
    require(isinstance(entries, list), "catalog entries missing")
    for entry in entries:
        require(isinstance(entry, dict), "catalog entry is not an object")
        name = entry.get("observed_name", "<unknown>")
        require(
            entry.get("source_identity_from_path") is False,
            f"{name}: local path entered source identity",
        )
        processing = entry.get("processing")
        require(isinstance(processing, dict), f"{name}: processing disposition missing")
        disposition = processing.get("disposition")
        if disposition == "resolved-exact-profile":
            for field in (
                "profile_path",
                "profile_coordinate",
                "syntax_provider",
                "recipe_coordinate",
                "basis",
            ):
                require(field in processing, f"{name}: resolved processing lacks {field}")
            basis = processing["basis"]
            require(
                "physical-artifact-sha256" in basis,
                f"{name}: resolved processing lacks exact digest evidence",
            )
            require(
                "profile-declared-syntax-authority" in basis,
                f"{name}: parser was not selected by profile",
            )
            require(
                "profile-declared-recipe-program" in basis,
                f"{name}: recipe was not selected by profile",
            )
        elif disposition in {
            "ambiguous-exact-profile",
            "profile-nonconforming",
            "profile-required",
            "physical-profile-resolution-required",
        }:
            require(
                "syntax_provider" not in processing,
                f"{name}: unresolved source selected a parser",
            )
            require(
                "recipe_coordinate" not in processing,
                f"{name}: unresolved source selected a recipe",
            )
            proposal = processing.get("proposal")
            require(
                isinstance(proposal, dict),
                f"{name}: unresolved source lacks profile work proposal",
            )
            require(
                proposal.get("required_sections") == required_sections,
                f"{name}: profile proposal does not carry the complete required section set",
            )
        elif disposition == "excluded-from-profile-selection":
            require("reason" in processing, f"{name}: exclusion lacks reason")
        else:
            raise ValueError(f"{name}: unknown processing disposition {disposition!r}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
    parser.add_argument("--profile-model", type=Path, default=DEFAULT_PROFILE_MODEL)
    parser.add_argument("--profile-root", type=Path, default=DEFAULT_PROFILE_ROOT)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--model-root", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    inventory = load_json(arguments.inventory)
    profile_model = load_json(arguments.profile_model)
    profiles = load_profiles(arguments.profile_root)
    catalog = build_catalog(
        inventory,
        profile_model,
        profiles,
        source_root=arguments.source_root,
        model_root=arguments.model_root,
    )
    encoded = json.dumps(catalog, indent=2, sort_keys=True) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
