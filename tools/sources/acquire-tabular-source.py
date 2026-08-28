#!/usr/bin/env python3
"""Acquire one locked tabular source profile into an exact local fixture tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile
import time
from typing import Any, Callable
import urllib.request
import zipfile


PROFILE_SCHEMA = "laplace.tabular-source-profile/v1"
RECEIPT_SCHEMA = "laplace.tabular-source-acquisition-receipt/v1"


class AcquisitionError(RuntimeError):
    pass


def canonical(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AcquisitionError(message)


def safe_relative(value: str, label: str) -> Path:
    path = PurePosixPath(value)
    require(value != "" and not path.is_absolute(), f"unsafe {label}: {value}")
    require(all(part not in ("", ".", "..") for part in path.parts),
            f"unsafe {label}: {value}")
    return Path(*path.parts)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_file(root: Path, artifact: dict[str, Any]) -> Path:
    relative = safe_relative(artifact["local_discovery_path"], "discovery path")
    path = root / relative
    require(path.is_file(), f"acquired artifact is absent: {relative.as_posix()}")
    require(path.stat().st_size == artifact["byte_count"],
            f"acquired byte count differs: {relative.as_posix()}")
    require(sha256_file(path) == artifact["sha256"],
            f"acquired digest differs: {relative.as_posix()}")
    return path


def validate_profile(document: dict[str, Any]) -> None:
    require(document.get("schema") == PROFILE_SCHEMA,
            "unknown tabular source profile schema")
    artifacts = document.get("artifacts")
    require(isinstance(artifacts, list) and artifacts,
            "tabular source profile has no artifacts")
    names: set[str] = set()
    roots = 0
    for artifact in artifacts:
        name = artifact.get("name")
        require(isinstance(name, str) and name not in names,
                "artifact names must be unique strings")
        names.add(name)
        safe_relative(artifact["local_discovery_path"], "discovery path")
        require(isinstance(artifact.get("byte_count"), int)
                and artifact["byte_count"] > 0,
                f"invalid byte count: {name}")
        digest = artifact.get("sha256", "")
        require(isinstance(digest, str) and len(digest) == 64,
                f"invalid SHA-256: {name}")
        try:
            bytes.fromhex(digest)
        except ValueError as error:
            raise AcquisitionError(f"invalid SHA-256: {name}") from error
        parent = artifact.get("parent")
        if parent is None:
            roots += 1
            acquisition = artifact.get("acquisition", {})
            url = acquisition.get("url", "")
            require(acquisition.get("transport") == "https"
                    and isinstance(url, str) and url.startswith("https://"),
                    f"root artifact lacks locked HTTPS acquisition: {name}")
            require(acquisition.get("retry_attempts") in range(1, 6),
                    f"invalid acquisition retry bound: {name}")
        else:
            require(parent in names, f"artifact parent must precede member: {name}")
            safe_relative(artifact.get("archive_member", ""), "archive member")
    require(roots > 0, "tabular source profile has no acquisition root")


def verify_tree(document: dict[str, Any], root: Path) -> list[dict[str, Any]]:
    by_name = {artifact["name"]: artifact for artifact in document["artifacts"]}
    verified: list[dict[str, Any]] = []
    for artifact in document["artifacts"]:
        path = verify_file(root, artifact)
        parent = artifact.get("parent")
        if parent is not None:
            container = verify_file(root, by_name[parent])
            member = artifact["archive_member"]
            try:
                with zipfile.ZipFile(container) as archive:
                    archived = archive.read(member)
            except (KeyError, zipfile.BadZipFile) as error:
                raise AcquisitionError(
                    f"archive member is unavailable: {member}") from error
            require(archived == path.read_bytes(),
                    f"selected member differs from container: {member}")
        verified.append({
            "name": artifact["name"],
            "local_discovery_path": artifact["local_discovery_path"],
            "byte_count": artifact["byte_count"],
            "sha256": artifact["sha256"],
        })
    return verified


def download_https(url: str, destination: Path, attempts: int) -> None:
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            request = urllib.request.Request(
                url, headers={"User-Agent": "Laplace-locked-source-acquisition/1"})
            with urllib.request.urlopen(request, timeout=60) as response, \
                    destination.open("wb") as output:
                require(response.geturl().startswith("https://"),
                        "source acquisition redirected outside HTTPS")
                shutil.copyfileobj(response, output, 1024 * 1024)
            return
        except Exception as error:  # network/provider failures are reported exactly
            last_error = error
            destination.unlink(missing_ok=True)
            if attempt < attempts:
                time.sleep(attempt)
    raise AcquisitionError(f"source acquisition failed after {attempts} attempts: "
                           f"{last_error}")


def acquire(
    document: dict[str, Any],
    destination: Path,
    fetch: Callable[[str, Path, int], None] = download_https,
) -> dict[str, Any]:
    validate_profile(document)
    profile_sha256 = hashlib.sha256(canonical(document)).hexdigest()
    if destination.exists():
        verified = verify_tree(document, destination)
        receipt_path = destination / "acquisition-receipt.json"
        require(receipt_path.is_file(), "existing source tree lacks acquisition receipt")
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        require(receipt.get("schema") == RECEIPT_SCHEMA
                and receipt.get("profile_sha256") == profile_sha256
                and receipt.get("artifacts") == verified,
                "existing source acquisition receipt differs")
        return receipt

    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(
        prefix=".laplace-tabular-source.", dir=destination.parent))
    try:
        by_name = {artifact["name"]: artifact for artifact in document["artifacts"]}
        for artifact in document["artifacts"]:
            if artifact.get("parent") is not None:
                continue
            target = stage / safe_relative(
                artifact["local_discovery_path"], "discovery path")
            target.parent.mkdir(parents=True, exist_ok=True)
            acquisition = artifact["acquisition"]
            fetch(acquisition["url"], target, acquisition["retry_attempts"])
            verify_file(stage, artifact)
        for artifact in document["artifacts"]:
            parent = artifact.get("parent")
            if parent is None:
                continue
            container = verify_file(stage, by_name[parent])
            member = artifact["archive_member"]
            try:
                with zipfile.ZipFile(container) as archive:
                    selected = archive.read(member)
            except (KeyError, zipfile.BadZipFile) as error:
                raise AcquisitionError(
                    f"archive member is unavailable: {member}") from error
            target = stage / safe_relative(
                artifact["local_discovery_path"], "discovery path")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(selected)
            verify_file(stage, artifact)

        verified = verify_tree(document, stage)
        receipt_body = {
            "schema": RECEIPT_SCHEMA,
            "profile_sha256": profile_sha256,
            "artifacts": verified,
        }
        receipt_body["receipt_id"] = hashlib.sha256(
            canonical(receipt_body)).hexdigest()
        (stage / "acquisition-receipt.json").write_text(
            json.dumps(receipt_body, ensure_ascii=False, indent=2,
                       sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(stage, destination)
        return receipt_body
    except Exception:
        shutil.rmtree(stage, ignore_errors=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        document = json.loads(arguments.profile.read_text(encoding="utf-8"))
        receipt = acquire(document, arguments.destination)
    except (AcquisitionError, OSError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(receipt, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
