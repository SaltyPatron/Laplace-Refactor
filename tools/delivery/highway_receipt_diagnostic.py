#!/usr/bin/env python3
"""Retain bounded, read-only evidence of Highway admission receipt availability."""
from __future__ import annotations

import argparse
from collections import deque
import hashlib
import json
import os
import re
from pathlib import Path
import stat

MAX_ENTRIES = 4096
MAX_ROOTS = 8
MAX_DIRECTORY_INDEX = 256
MAX_DEPTH = 8
MAX_FILE_BYTES = 1024 * 1024
MAX_CAPTURE_BYTES = 16 * 1024 * 1024
HIGHWAY_SCHEMAS = {"laplace.highway-product-activation-request/v1", "laplace.highway-product-activation-receipt/v1"}


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


def inspect(receipt_root: Path, output: Path, additional_roots: list[Path], request_sha: str | None = None) -> dict:
    if request_sha is not None and re.fullmatch(r"[0-9a-f]{64}", request_sha) is None:
        raise ValueError("Expected request identity must be a lowercase SHA-256")
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema": "laplace.highway-receipt-diagnostic/v1", "scope": "filesystem evidence only; no database execution or reconstructed admission", "receipt_root": metadata(receipt_root), "retained_admission_directory": metadata(receipt_root / "highway"), "search_roots": [], "candidates": [], "symlinks_not_followed": [], "errors": [], "examined_entries": 0, "captured_bytes": 0, "truncated": False, "limits": {"entries": MAX_ENTRIES, "depth": MAX_DEPTH, "single_file_bytes": MAX_FILE_BYTES, "captured_bytes": MAX_CAPTURE_BYTES}}
    report.update({"expected_request_sha256": request_sha, "directory_index": [], "directory_index_truncated": False})
    report["limits"]["directory_index_per_root"] = MAX_DIRECTORY_INDEX
    seen = set()

    def walk(root: Path) -> None:
        # Give every explicitly selected archive a bounded fair search. Breadth
        # first traversal reaches sibling receipt trees before large build leaves.
        pending = deque([(root, 0)])
        examined = 0
        indexed = 0
        while pending and examined < MAX_ENTRIES:
            directory, depth = pending.popleft()
            if depth > MAX_DEPTH:
                report["truncated"] = True
                continue
            try:
                with os.scandir(directory) as entries:
                    for entry in entries:
                        if examined >= MAX_ENTRIES:
                            report["truncated"] = True
                            break
                        examined += 1
                        report["examined_entries"] += 1
                        path = Path(entry.path)
                        if entry.is_symlink():
                            report["symlinks_not_followed"].append(metadata(path))
                        elif entry.is_dir(follow_symlinks=False):
                            if indexed < MAX_DIRECTORY_INDEX:
                                report["directory_index"].append({**metadata(path), "search_root": str(root), "depth": depth + 1})
                                indexed += 1
                            else:
                                report["directory_index_truncated"] = True
                            # Known admission identity and named receipt archives
                            # take precedence over unrelated build output trees.
                            if entry.name == request_sha or any(word in entry.name.lower() for word in ("highway", "receipt")):
                                pending.appendleft((path, depth + 1))
                            else:
                                pending.append((path, depth + 1))
                        elif entry.is_file(follow_symlinks=False) and entry.name.endswith(".json"):
                            capture(path)
            except OSError as error:
                report["errors"].append({"path": str(directory), "error": str(error)})
        if pending:
            report["truncated"] = True

    def capture(path: Path) -> None:
        if str(path) in seen:
            return
        seen.add(str(path))
        try:
            if path.stat().st_size > MAX_FILE_BYTES:
                report["errors"].append({"path": str(path), "error": "candidate exceeds single-file evidence bound"})
                return
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            with os.fdopen(descriptor, "rb") as source:
                raw = source.read(MAX_FILE_BYTES + 1)
            if len(raw) > MAX_FILE_BYTES:
                raise ValueError("candidate grew beyond the file bound")
            document = json.loads(raw)
            schema = document.get("schema", "") if isinstance(document, dict) else ""
            if not isinstance(schema, str) or schema not in HIGHWAY_SCHEMAS:
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

    roots = list(dict.fromkeys([receipt_root, *additional_roots]))
    if len(roots) > MAX_ROOTS:
        report["truncated"] = True
    report["limits"].update({"entries_per_root": MAX_ENTRIES, "roots": MAX_ROOTS, "entries": MAX_ENTRIES * min(len(roots), MAX_ROOTS)})
    for root in roots[:MAX_ROOTS]:
        ancestry = [metadata(path) for path in reversed([root, *root.parents])]
        report["search_roots"].append({"path": str(root), "ancestry": ancestry})
        if any(item["kind"] != "directory" for item in ancestry):
            report["errors"].append({"path": str(root), "error": "search root or ancestor is absent, unreadable, non-directory or symlink; not traversed"})
            continue
        walk(root)
    report["candidate_count"] = len(report["candidates"])
    report["recovery_status"] = "candidate-evidence-requires-native-identity-validation" if report["candidates"] else "no-retained-highway-evidence-found-within-declared-search-roots"
    (output / "diagnostic.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, default=Path("contracts/postgresql-cluster.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--search-root", type=Path, action="append", default=[], help="additional explicitly selected retained receipt archive; symlinks are never traversed")
    parser.add_argument("--request-sha256", help="prioritize an independently observed admission request identity without reconstructing it")
    arguments = parser.parse_args()
    contract = json.loads(arguments.contract.read_text())
    result = inspect(Path(contract["instance"]["receipt_directory"]), arguments.output, arguments.search_root, arguments.request_sha256)
    print(json.dumps({key: result[key] for key in ("retained_admission_directory", "candidate_count", "recovery_status", "truncated")}))


if __name__ == "__main__":
    main()
