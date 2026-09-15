#!/usr/bin/env python3
"""Retain bounded, read-only evidence of Highway admission receipt availability."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat

MAX_ENTRIES = 4096
MAX_DEPTH = 8
MAX_FILE_BYTES = 1024 * 1024
MAX_CAPTURE_BYTES = 16 * 1024 * 1024


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


def inspect(receipt_root: Path, output: Path, additional_roots: list[Path]) -> dict:
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema": "laplace.highway-receipt-diagnostic/v1", "scope": "filesystem evidence only; no database execution or reconstructed admission", "receipt_root": metadata(receipt_root), "retained_admission_directory": metadata(receipt_root / "highway"), "search_roots": [], "candidates": [], "symlinks_not_followed": [], "errors": [], "examined_entries": 0, "captured_bytes": 0, "truncated": False, "limits": {"entries": MAX_ENTRIES, "depth": MAX_DEPTH, "single_file_bytes": MAX_FILE_BYTES, "captured_bytes": MAX_CAPTURE_BYTES}}
    seen = set()

    def walk(directory: Path, depth: int) -> None:
        if depth > MAX_DEPTH or report["examined_entries"] >= MAX_ENTRIES:
            report["truncated"] = True
            return
        try:
            with os.scandir(directory) as entries:
                for entry in entries:
                    if report["examined_entries"] >= MAX_ENTRIES:
                        report["truncated"] = True
                        return
                    report["examined_entries"] += 1
                    path = Path(entry.path)
                    if entry.is_symlink():
                        report["symlinks_not_followed"].append(metadata(path))
                    elif entry.is_dir(follow_symlinks=False):
                        walk(path, depth + 1)
                    elif entry.is_file(follow_symlinks=False) and (entry.name in {"request.json", "receipt.json"} or "highway" in entry.name.lower() and entry.name.endswith(".json")):
                        capture(path)
        except OSError as error:
            report["errors"].append({"path": str(directory), "error": str(error)})

    def capture(path: Path) -> None:
        if str(path) in seen:
            return
        seen.add(str(path))
        if path.stat().st_size > MAX_FILE_BYTES:
            report["errors"].append({"path": str(path), "error": "candidate exceeds single-file evidence bound"})
            return
        try:
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            with os.fdopen(descriptor, "rb") as source:
                raw = source.read(MAX_FILE_BYTES + 1)
            if len(raw) > MAX_FILE_BYTES:
                raise ValueError("candidate grew beyond the file bound")
            document = json.loads(raw)
            schema = document.get("schema", "") if isinstance(document, dict) else ""
            if not isinstance(schema, str) or not schema.startswith("laplace.highway-"):
                return
            fingerprint = hashlib.sha256(raw).hexdigest()
            record = {**metadata(path), "sha256": fingerprint, "schema": schema, "package_id": document.get("package_id"), "system_identifier": document.get("system_identifier"), "request_sha256": document.get("request_sha256"), "capture": None}
            if report["captured_bytes"] + len(raw) <= MAX_CAPTURE_BYTES:
                target = output / f"{fingerprint}.json"
                if not target.exists():
                    target.write_bytes(raw)
                    report["captured_bytes"] += len(raw)
                record["capture"] = target.name
            else:
                report["truncated"] = True
            report["candidates"].append(record)
        except (OSError, ValueError) as error:
            report["errors"].append({"path": str(path), "error": str(error)})

    for root in [receipt_root, *additional_roots]:
        ancestry = [metadata(path) for path in reversed([root, *root.parents])]
        report["search_roots"].append({"path": str(root), "ancestry": ancestry})
        if any(item["kind"] != "directory" for item in ancestry):
            report["errors"].append({"path": str(root), "error": "search root or ancestor is absent, unreadable, non-directory or symlink; not traversed"})
            continue
        walk(root, 0)
    report["candidate_count"] = len(report["candidates"])
    report["recovery_status"] = "candidate-evidence-requires-native-identity-validation" if report["candidates"] else "no-retained-highway-evidence-found-within-declared-search-roots"
    (output / "diagnostic.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=Path("contracts/postgresql-cluster.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--search-root", type=Path, action="append", default=[], help="additional explicitly selected retained receipt archive; symlinks are never traversed")
    arguments = parser.parse_args()
    contract = json.loads(arguments.contract.read_text())
    result = inspect(Path(contract["instance"]["receipt_directory"]), arguments.output, arguments.search_root)
    print(json.dumps({key: result[key] for key in ("retained_admission_directory", "candidate_count", "recovery_status", "truncated")}))


if __name__ == "__main__":
    main()
