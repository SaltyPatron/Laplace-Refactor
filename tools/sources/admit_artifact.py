#!/usr/bin/env python3
"""Admit one exact source artifact/member under a declarative Laplace source recipe.

This is a bootstrap/acquisition boundary only.  It verifies bytes, extracts an
exact selected member when declared, and emits an atomic receipt.  Parsing,
identity, AST lowering, evidence, and persistence remain native Laplace work.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import urllib.request
import zipfile

SCHEMA = "laplace.source-artifact/v1"
SOURCE_CLASSES = {"foundational_seed", "observation", "derived", "model"}
EVIDENCE_TYPES = {
    "standard", "curated_dataset", "corpus", "direct_observation",
    "calculation", "model", "self_assertion", "external_provider",
}
RECONSTRUCTION = {"exact", "semantic", "none"}


class AdmissionError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AdmissionError(message)


def canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    count = 0
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
            count += len(block)
    return digest.hexdigest(), count


def validate_recipe(document: dict[str, object]) -> None:
    require(document.get("schema") == SCHEMA, "unknown source artifact schema")
    profile = document.get("profile")
    authority = document.get("authority")
    artifact = document.get("artifact")
    selected = document.get("selected_member")
    evidence = document.get("evidence")
    require(isinstance(profile, dict), "source profile is missing")
    require(isinstance(authority, dict), "source authority is missing")
    require(isinstance(artifact, dict), "source artifact is missing")
    require(isinstance(evidence, dict), "source evidence declaration is missing")
    require(profile.get("source_class") in SOURCE_CLASSES,
            "source profile has unknown epistemic class")
    require(profile.get("evidence_source_type") in EVIDENCE_TYPES,
            "source profile has unknown evidence source type")
    require(profile.get("reconstruction_class") in RECONSTRUCTION,
            "source profile has unknown reconstruction class")
    require(isinstance(profile.get("version"), int) and profile["version"] > 0,
            "source profile version must be positive")
    require(isinstance(authority.get("publisher"), str) and authority["publisher"],
            "publisher is missing")
    require(isinstance(authority.get("release"), str) and authority["release"],
            "release is missing")
    require(isinstance(artifact.get("name"), str) and artifact["name"],
            "artifact name is missing")
    require(isinstance(artifact.get("local_discovery_path"), str)
            and artifact["local_discovery_path"],
            "artifact discovery path is missing")
    require(isinstance(artifact.get("byte_count"), int)
            and artifact["byte_count"] > 0,
            "artifact byte count is invalid")
    require(isinstance(artifact.get("sha256"), str)
            and len(artifact["sha256"]) == 64,
            "artifact SHA-256 is invalid")
    bytes.fromhex(artifact["sha256"])
    acquisition = artifact.get("acquisition")
    require(isinstance(acquisition, dict), "artifact acquisition is missing")
    require(acquisition.get("transport") == "https",
            "only exact HTTPS bootstrap acquisition is supported")
    require(isinstance(acquisition.get("url"), str)
            and acquisition["url"].startswith("https://"),
            "artifact acquisition URL is invalid")
    require(isinstance(acquisition.get("retry_attempts"), int)
            and 1 <= acquisition["retry_attempts"] <= 5,
            "artifact retry count is invalid")
    if selected is not None:
        require(isinstance(selected, dict), "selected_member must be an object")
        require(isinstance(selected.get("name"), str) and selected["name"],
                "selected member name is missing")
        require(isinstance(selected.get("sha256"), str)
                and len(selected["sha256"]) == 64,
                "selected member SHA-256 is invalid")
        bytes.fromhex(selected["sha256"])
    require(evidence.get("epistemic_kind") in {
        "observed", "testimony", "calculation", "derivation",
        "model_behavior", "unknown", "contradiction",
    }, "source evidence epistemic kind is invalid")
    require(isinstance(evidence.get("dependence_root"), str)
            and evidence["dependence_root"],
            "source evidence dependence root is missing")


def acquire(url: str, target: Path, attempts: int) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    error: Exception | None = None
    for attempt in range(attempts):
        temporary = target.with_name(
            f".{target.name}.acquire-{os.getpid()}-{attempt}.tmp"
        )
        try:
            request = urllib.request.Request(
                url,
                headers={"User-Agent": "Laplace source-artifact admission/1"},
            )
            with urllib.request.urlopen(request, timeout=60) as response, \
                    temporary.open("wb") as output:
                require(response.status == 200, f"HTTP status {response.status}")
                while True:
                    block = response.read(1024 * 1024)
                    if not block:
                        break
                    output.write(block)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, target)
            return
        except Exception as caught:  # network/filesystem boundary; preserve cause
            error = caught
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
            if attempt + 1 < attempts:
                time.sleep(min(2 ** attempt, 4))
    raise AdmissionError(f"artifact acquisition failed: {error}")


def extract_exact_member(archive: Path, name: str) -> bytes:
    try:
        with zipfile.ZipFile(archive, "r") as package:
            matches = [item for item in package.infolist() if item.filename == name]
            require(len(matches) == 1,
                    f"selected member must occur exactly once: {name}")
            info = matches[0]
            require(not info.is_dir(), "selected member cannot be a directory")
            require((info.flag_bits & 0x1) == 0,
                    "encrypted source members are not supported")
            return package.read(info)
    except (zipfile.BadZipFile, OSError, RuntimeError) as error:
        raise AdmissionError(f"source archive is invalid: {error}") from error


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--recipe", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    parser.add_argument("--acquire", action="store_true")
    arguments = parser.parse_args()

    recipe_bytes = arguments.recipe.read_bytes()
    document = json.loads(recipe_bytes.decode("utf-8"))
    validate_recipe(document)
    artifact = document["artifact"]
    selected = document.get("selected_member")
    source_path = arguments.source_root / artifact["local_discovery_path"]

    if not source_path.exists():
        require(arguments.acquire,
                f"source artifact is absent: {source_path}")
        acquisition = artifact["acquisition"]
        acquire(acquisition["url"], source_path, acquisition["retry_attempts"])

    archive_sha, archive_bytes = sha256_file(source_path)
    require(archive_bytes == artifact["byte_count"],
            f"artifact byte count mismatch: expected={artifact['byte_count']} "
            f"actual={archive_bytes}")
    require(archive_sha == artifact["sha256"],
            f"artifact SHA-256 mismatch: expected={artifact['sha256']} "
            f"actual={archive_sha}")

    if selected is None:
        selected_bytes = source_path.read_bytes()
        selected_name = artifact["name"]
        selected_sha = archive_sha
    else:
        selected_name = selected["name"]
        selected_bytes = extract_exact_member(source_path, selected_name)
        selected_sha = sha256_bytes(selected_bytes)
        require(selected_sha == selected["sha256"],
                f"selected member SHA-256 mismatch: expected={selected['sha256']} "
                f"actual={selected_sha}")

    atomic_write(arguments.output, selected_bytes)
    recipe_sha = sha256_bytes(canonical_json(document))
    receipt = {
        "schema": "laplace.source-artifact-admission-receipt/v1",
        "recipe_sha256": recipe_sha,
        "recipe_file_sha256": sha256_bytes(recipe_bytes),
        "profile_name": document["profile"]["name"],
        "source_class": document["profile"]["source_class"],
        "evidence_source_type": document["profile"]["evidence_source_type"],
        "authority": document["authority"],
        "artifact": {
            "path": str(source_path),
            "byte_count": archive_bytes,
            "sha256": archive_sha,
        },
        "selected_member": {
            "name": selected_name,
            "byte_count": len(selected_bytes),
            "sha256": selected_sha,
        },
        "output": str(arguments.output),
        "epistemic_kind": document["evidence"]["epistemic_kind"],
        "dependence_root": document["evidence"]["dependence_root"],
    }
    atomic_write(
        arguments.receipt,
        json.dumps(receipt, ensure_ascii=False, sort_keys=True, indent=2).encode("utf-8")
        + b"\n",
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AdmissionError, OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"source artifact admission failed: {error}") from error
