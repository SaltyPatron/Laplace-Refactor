#!/usr/bin/env python3
"""Validate the dated /vault source and model inventory.

The default check is repository-local and suitable for CI.  --verify-physical
also compares the manifest with the machine-local vault without reading model
weight payloads into memory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INVENTORY_PATH = ROOT / "state" / "vault-inventory.json"
CONTINUATION_PATH = ROOT / "state" / "continuation.json"
UNICODE_CONTRACT_PATH = ROOT / "contracts" / "unicode-source.json"
ISO_CONTRACT_PATH = ROOT / "contracts" / "sources" / "iso-639-3-20260415.json"
CILI_CONTRACT_PATH = ROOT / "contracts" / "sources" / "cili-pwn-mappings-20240611.json"
TREE_SITTER_LOCK_PATH = ROOT / "dependencies" / "tree-sitter-grammars.lock.json"

KNOWN_TREE_SITTER_REVISION_MISMATCHES = {
    "tree-sitter-fsharp",
    "tree-sitter-m68k",
    "tree-sitter-r",
    "tree-sitter-yaml",
}
KNOWN_TREE_SITTER_EXTRAS = {"ebnf", "superhtml", "v-analyzer", "ziggy"}


def load(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    require(isinstance(value, dict), f"{path} is not a JSON object")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def entry_map(document: dict, root_key: str) -> dict[str, dict]:
    entries = document[root_key]["entries"]
    require(isinstance(entries, list), f"{root_key} entries are not a list")
    result = {entry.get("name"): entry for entry in entries}
    require(len(result) == len(entries), f"{root_key} contains duplicate or missing names")
    return result


def validate_structure(document: dict, continuation: dict) -> None:
    require(
        document.get("schema") == "laplace.vault-source-model-inventory/v1",
        "vault inventory schema drift",
    )
    require(
        document.get("classification") == "observed-development-state-not-source-authority",
        "observed vault state was promoted to source authority",
    )
    require(document.get("roots") == {"data": "/vault/Data", "models": "/vault/models"}, "vault roots drift")

    data = entry_map(document, "data_root")
    models = entry_map(document, "model_root")
    require(len(data) == document["data_root"].get("observed_top_level_directories") == 25, "data inventory is not the complete observed 25-directory boundary")
    require(len(models) == document["model_root"].get("observed_top_level_directories") == 38, "model inventory is not the complete observed 38-directory boundary")
    require(sum(item["allocated_bytes"] for item in data.values()) == document["data_root"].get("observed_allocated_bytes"), "data allocated-byte denominator drift")
    require(sum(item["allocated_bytes"] for item in models.values()) == document["model_root"].get("observed_allocated_bytes"), "model allocated-byte denominator drift")

    continuation_inputs = continuation["development_inputs"]
    require(
        set(continuation_inputs["data_root"]["observed_top_level_entries"]) == set(data),
        "continuation and data inventory disagree",
    )
    require(
        set(continuation_inputs["model_root"]["observed_top_level_entries"]) == set(models),
        "continuation and model inventory disagree",
    )

    allowed_statuses = set(document["status_legend"])
    for root_name, entries in (("data", data), ("model", models)):
        for name, item in entries.items():
            for field in ("allocated_bytes", "content", "version", "roles", "modalities", "status", "missing"):
                require(field in item, f"{root_name}/{name} lacks {field}")
            require(item["status"] in allowed_statuses, f"{root_name}/{name} has unknown status")
            require(isinstance(item["missing"], list) and item["missing"], f"{root_name}/{name} has no explicit gap boundary")

    covered_data: set[str] = set()
    for family in document.get("source_family_coverage", []):
        for name in family.get("entries", []):
            if name in data:
                covered_data.add(name)
    require(covered_data == set(data), "source-family classification omits a data directory")

    law = document["interpretation_law"]
    precision = law.get("model_precision_precedence", "").lower()
    for term in ("full-precision safetensors", "primary", "awq", "gguf", "subordinate", "never substitutes"):
        require(term in precision, f"model precision precedence lost {term}")
    require(models["models--Qwen--Qwen2.5-Coder-7B-Instruct-AWQ"]["status"] == "incomplete-download", "incomplete AWQ derivative was promoted")
    require("incomplete-derived-model-export" in models["models--Qwen--Qwen2.5-Coder-7B-Instruct-AWQ"]["roles"], "AWQ derivative role drift")
    require(models["gguf"]["roles"] == ["derived-model-export-witness"], "GGUF derivative was promoted")
    require("derived-model-export-witness" in models["yolo11x"]["roles"], "TorchScript derivative relation was erased")
    require(data["omw"]["status"] == "duplicate", "duplicate OMW estate was promoted")
    require(data["Unicode.BAD-DONOTUSE"]["status"] == "quarantined", "quarantined Unicode mirror was promoted")
    require(data["test-data"]["status"] == "fixture-only", "fixtures were promoted to source completion")
    require(data["CILI"]["status"] == "nonconforming-to-declared-contract", "nonconforming CILI checkout was promoted")
    require(data["ISO639"]["status"] == "locked-profile-conformant", "verified ISO profile state drift")
    require(data["UCD"]["status"] == "locked-profile-conformant", "verified Unicode source state drift")


def validate_exact_artifacts(contract_path: Path, source_root: Path) -> int:
    contract = load(contract_path)
    checked = 0
    for artifact in contract["artifacts"]:
        local_path = source_root / artifact["local_discovery_path"]
        require(local_path.is_file(), f"missing exact artifact: {local_path}")
        require(local_path.stat().st_size == artifact["byte_count"], f"artifact byte count drift: {local_path}")
        require(digest(local_path) == artifact["sha256"], f"artifact digest drift: {local_path}")
        checked += 1
    return checked


def validate_unicode(source_root: Path) -> int:
    contract = load(UNICODE_CONTRACT_PATH)
    checked = 0
    for artifact in contract["files"]:
        path = source_root / artifact["path"]
        require(path.is_file(), f"missing Unicode artifact: {path}")
        require(path.stat().st_size == artifact["bytes"], f"Unicode byte count drift: {path}")
        require(digest(path) == artifact["sha256"], f"Unicode digest drift: {path}")
        checked += 1
    require(checked == contract["file_count"] == 33, "Unicode selected-file denominator drift")
    return checked


def validate_cili_nonconformance(source_root: Path) -> int:
    contract = load(CILI_CONTRACT_PATH)
    mismatches = 0
    for artifact in contract["artifacts"]:
        path = source_root / artifact["local_discovery_path"]
        if artifact["parent"] is None:
            require(not path.exists(), "the inventory says the pinned CILI archive is absent")
            continue
        require(path.is_file(), f"CILI selected member is absent: {path}")
        exact = path.stat().st_size == artifact["byte_count"] and digest(path) == artifact["sha256"]
        require(not exact, f"CILI inventory is stale; selected member now conforms: {path}")
        mismatches += 1
    require(mismatches == 2, "CILI mismatch denominator drift")
    return mismatches


def git_head(path: Path) -> str:
    return subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()


def validate_tree_sitter(source_root: Path) -> tuple[int, int]:
    lock = load(TREE_SITTER_LOCK_PATH)
    repositories = {entry["name"]: entry for entry in lock["repositories"]}
    physical = {path.name for path in source_root.iterdir() if path.is_dir()}
    require(set(repositories).issubset(physical), "one or more locked Tree-sitter repositories are absent")
    extras = physical - set(repositories)
    require(extras == KNOWN_TREE_SITTER_EXTRAS, f"Tree-sitter extra-directory disposition drift: {sorted(extras)}")
    for name in KNOWN_TREE_SITTER_REVISION_MISMATCHES:
        require(git_head(source_root / name) != repositories[name]["revision"], f"Tree-sitter mismatch is now stale: {name}")
    return len(repositories), len(extras)


def validate_model_weight_closure(model_root: Path, inventory: dict) -> tuple[int, int, int, int]:
    model_entries = entry_map(inventory, "model_root")
    excluded = {".locks", "code-corpus", "stack-v2", "tiny-codes"}
    index_count = 0
    reference_count = 0
    safetensors_packages = 0
    symlinks_checked = 0
    incomplete_name = "models--Qwen--Qwen2.5-Coder-7B-Instruct-AWQ"

    for name, entry in model_entries.items():
        if name in excluded:
            continue
        root = model_root / name
        for path in root.rglob("*"):
            if path.is_symlink():
                require(path.exists(), f"broken model-package symlink: {path}")
                symlinks_checked += 1
        incomplete_files = [path for path in root.rglob("*.incomplete") if path.is_file()]
        if name == incomplete_name:
            require(len(incomplete_files) == 2, "AWQ partial-download evidence drift")
        else:
            require(not incomplete_files, f"unexpected incomplete model download: {name}")

        safetensors = [path for path in root.rglob("*.safetensors") if path.is_file()]
        if "safetensors" in entry["content"].lower() and name != incomplete_name:
            require(safetensors, f"declared safetensors package has no weights: {name}")
        if safetensors and name != incomplete_name:
            safetensors_packages += 1

        for index_path in root.rglob("*index*.json"):
            if ".cache" in index_path.parts:
                continue
            index = load(index_path)
            weight_map = index.get("weight_map")
            if not isinstance(weight_map, dict):
                continue
            referenced = set(weight_map.values())
            missing = [filename for filename in referenced if not (index_path.parent / filename).is_file()]
            if name == incomplete_name:
                require(missing, "AWQ index unexpectedly has full closure")
            else:
                require(not missing, f"weight-index closure failure in {name}: {missing}")
            index_count += 1
            reference_count += len(referenced)

    return index_count, reference_count, safetensors_packages, symlinks_checked


def validate_physical(document: dict) -> dict:
    data_root = Path(document["roots"]["data"])
    model_root = Path(document["roots"]["models"])
    require(data_root.is_dir(), f"data root is unavailable: {data_root}")
    require(model_root.is_dir(), f"model root is unavailable: {model_root}")

    data_names = {path.name for path in data_root.iterdir() if path.is_dir()}
    model_names = {path.name for path in model_root.iterdir() if path.is_dir()}
    require(data_names == set(entry_map(document, "data_root")), "physical data root differs from inventory")
    require(model_names == set(entry_map(document, "model_root")), "physical model root differs from inventory")

    iso_checked = validate_exact_artifacts(ISO_CONTRACT_PATH, data_root / "ISO639")
    unicode_checked = validate_unicode(data_root / "UCD" / "Public" / "UCD" / "latest")
    cili_mismatches = validate_cili_nonconformance(data_root / "CILI")
    locked_grammars, extra_grammars = validate_tree_sitter(data_root / "TreeSitter")
    (
        model_indexes,
        model_weight_references,
        safetensors_packages,
        model_symlinks,
    ) = validate_model_weight_closure(model_root, document)
    return {
        "physical_data_directories": len(data_names),
        "physical_model_directories": len(model_names),
        "iso_artifacts_verified": iso_checked,
        "unicode_artifacts_verified": unicode_checked,
        "cili_selected_mismatches_verified": cili_mismatches,
        "tree_sitter_locked_directories": locked_grammars,
        "tree_sitter_extra_directories": extra_grammars,
        "model_weight_indexes_checked": model_indexes,
        "model_weight_file_references_checked": model_weight_references,
        "top_level_model_packages_with_safetensors_observed": safetensors_packages,
        "model_package_symlinks_checked": model_symlinks,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify-physical", action="store_true")
    arguments = parser.parse_args()
    inventory = load(INVENTORY_PATH)
    validate_structure(inventory, load(CONTINUATION_PATH))
    result: dict[str, object] = {
        "schema": inventory["schema"],
        "data_entries": len(inventory["data_root"]["entries"]),
        "model_entries": len(inventory["model_root"]["entries"]),
        "structural_status": "valid",
    }
    if arguments.verify_physical:
        result["physical"] = validate_physical(inventory)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
