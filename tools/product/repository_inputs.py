"""Content identity for package inputs; workflow orchestration is recorded separately."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess


def repository_build_fingerprint(repository: Path) -> str:
    names = subprocess.run(
        ["git", "-c", f"safe.directory={repository}", "-C", str(repository),
         "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        check=True, stdout=subprocess.PIPE,
    ).stdout.split(b"\0")
    records = []
    for raw in sorted(set(names)):
        if not raw:
            continue
        name = os.fsdecode(raw)
        # All other repository inputs remain conservative build dependencies.
        if name.startswith(".github/workflows/"):
            continue
        path = repository / name
        if path.is_symlink():
            content = os.fsencode(os.readlink(path))
            kind = "symlink"
        elif path.is_file():
            content = path.read_bytes()
            kind = "executable" if path.stat().st_mode & 0o111 else "file"
        elif not path.exists():
            content, kind = b"", "deleted"
        else:
            raise ValueError(f"Unsupported repository build input: {name}")
        records.append([name, kind, hashlib.sha256(content).hexdigest()])
    encoded = json.dumps(["laplace.repository-build-inputs/v1", records],
                         ensure_ascii=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()
