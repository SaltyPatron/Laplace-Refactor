#!/usr/bin/env python3
"""Selected-source catalog, preflight, durable ingestion jobs, and worker execution."""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import stat
import subprocess
import sys
import time
from typing import Any

CATALOG_SCHEMA = "laplace.product.source-catalog/v1"
PLAN_SCHEMA = "laplace.product.source-ingestion-plan/v1"
JOB_SCHEMA = "laplace.product.source-ingestion-job/v1"
BOUNDARY_SCHEMA = "laplace.configured-source-boundary/v1"
DEFAULT_ESTATE_ROOT = Path("/vault/Data")
DEFAULT_JOB_ROOT = Path("/opt/laplace/receipts/source-jobs")
DEFAULT_UNICODE_RELATIVE = Path("UCD/Public/UCD/latest")
IDEMPOTENCY = re.compile(r"^[A-Za-z0-9._:-]{1,128}$")
JOB_ID = re.compile(r"^[0-9a-f]{64}$")


class SourceProductError(RuntimeError):
    def __init__(self, message: str, status: int = 400, code: str = "source_product_error") -> None:
        super().__init__(message)
        self.message = message
        self.status = status
        self.code = code


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SourceProductError(f"cannot read product source metadata {path}: {error}", 503, "source_metadata_unavailable") from error
    if not isinstance(value, dict):
        raise SourceProductError(f"product source metadata is not an object: {path}", 503, "source_metadata_invalid")
    return value


def metadata_root() -> Path:
    executable = Path(__file__).resolve()
    installed = executable.parent.parent / "share/laplace/source-contracts"
    if installed.is_dir():
        return installed
    repository = executable.parent.parent
    return repository / "contracts"


def boundary_path(root: Path | None = None) -> Path:
    selected = metadata_root() if root is None else root
    installed = selected / "selected-source-boundary.json"
    if installed.is_file():
        return installed
    development = selected / "selected-source-boundary-20260903.json"
    if development.is_file():
        return development
    raise SourceProductError("installed selected-source boundary is unavailable", 503, "source_boundary_unavailable")


def profiles_root(root: Path | None = None) -> Path:
    selected = metadata_root() if root is None else root
    installed = selected / "profiles"
    if installed.is_dir():
        return installed
    development = selected / "sources"
    if development.is_dir():
        return development
    raise SourceProductError("installed source profiles are unavailable", 503, "source_profiles_unavailable")


def configured_estate_root(override: Path | None = None) -> Path:
    raw = os.environ.get("LAPLACE_SOURCE_ESTATE_ROOT")
    candidate = override or (Path(raw) if raw else DEFAULT_ESTATE_ROOT)
    if not candidate.is_absolute():
        raise SourceProductError("configured source estate root must be absolute", 503, "source_estate_invalid")
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as error:
        raise SourceProductError(f"configured source estate is unavailable: {candidate}: {error}", 503, "source_estate_unavailable") from error
    if not resolved.is_dir() or resolved.is_symlink():
        raise SourceProductError(f"configured source estate is not a real directory: {resolved}", 503, "source_estate_invalid")
    return resolved


def configured_job_root(override: Path | None = None) -> Path:
    raw = os.environ.get("LAPLACE_SOURCE_JOB_ROOT")
    root = override or (Path(raw) if raw else DEFAULT_JOB_ROOT)
    if not root.is_absolute():
        raise SourceProductError("source job root must be absolute", 503, "source_job_root_invalid")
    if root.exists() or root.is_symlink():
        if root.is_symlink() or not root.is_dir():
            raise SourceProductError(f"unsafe source job root: {root}", 503, "source_job_root_invalid")
    else:
        try:
            root.mkdir(parents=True, mode=0o750)
        except OSError as error:
            raise SourceProductError(f"cannot create source job root {root}: {error}", 503, "source_job_root_unavailable") from error
    return root.resolve()


def selected_boundary(root: Path | None = None) -> tuple[dict[str, Any], str]:
    path = boundary_path(root)
    boundary = load_json(path)
    if boundary.get("schema") != BOUNDARY_SCHEMA:
        raise SourceProductError("selected-source boundary schema is invalid", 503, "source_boundary_invalid")
    obligations = boundary.get("profile_obligations")
    if not isinstance(obligations, list):
        raise SourceProductError("selected-source boundary has no profile obligations", 503, "source_boundary_invalid")
    return boundary, sha256_file(path)


def obligation_map(boundary: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for value in boundary.get("profile_obligations", []):
        if not isinstance(value, dict):
            continue
        source_id = value.get("id")
        if isinstance(source_id, str) and source_id:
            result[source_id] = value
    return result


def profile_path(source_id: str, root: Path | None = None) -> Path:
    if not source_id or "/" in source_id or "\\" in source_id or source_id in {".", ".."}:
        raise SourceProductError("invalid selected source id", 400, "invalid_source_id")
    return profiles_root(root) / f"{source_id}.json"


def maybe_profile(source_id: str, root: Path | None = None) -> tuple[dict[str, Any] | None, str | None]:
    path = profile_path(source_id, root)
    if not path.is_file() or path.is_symlink():
        return None, None
    profile = load_json(path)
    artifacts = profile.get("artifacts")
    recipe = profile.get("recipe_program")
    if not isinstance(artifacts, list) or not artifacts or not isinstance(recipe, dict):
        raise SourceProductError(f"installed profile is incomplete: {source_id}", 503, "source_profile_invalid")
    return profile, sha256_file(path)


def artifact_specs(profile: dict[str, Any]) -> list[dict[str, Any]]:
    specs: list[dict[str, Any]] = []
    for artifact in profile.get("artifacts", []):
        if not isinstance(artifact, dict):
            raise SourceProductError("source profile artifact is not an object", 503, "source_profile_invalid")
        relative = artifact.get("local_discovery_path")
        size = artifact.get("byte_count")
        digest = artifact.get("sha256")
        if not isinstance(relative, str) or not relative or Path(relative).is_absolute() or ".." in Path(relative).parts:
            raise SourceProductError("source profile contains unsafe local_discovery_path", 503, "source_profile_invalid")
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            raise SourceProductError("source profile artifact byte_count is invalid", 503, "source_profile_invalid")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None:
            raise SourceProductError("source profile artifact sha256 is invalid", 503, "source_profile_invalid")
        specs.append({"relative_path": relative, "byte_count": size, "sha256": digest.lower(), "name": artifact.get("name")})
    return specs


def candidate_summary(entry: Path, specs: list[dict[str, Any]], exact: bool) -> dict[str, Any] | None:
    observations: list[dict[str, Any]] = []
    present = 0
    exact_count = 0
    for spec in specs:
        path = entry / spec["relative_path"]
        observation = {"relative_path": spec["relative_path"], "expected_bytes": spec["byte_count"], "present": path.is_file()}
        if path.is_file() and not path.is_symlink():
            present += 1
            observed_bytes = path.stat().st_size
            observation["observed_bytes"] = observed_bytes
            size_ok = observed_bytes == spec["byte_count"]
            observation["size_match"] = size_ok
            if exact and size_ok:
                digest = sha256_file(path)
                observation["observed_sha256"] = digest
                observation["sha256_match"] = digest == spec["sha256"]
                if observation["sha256_match"]:
                    exact_count += 1
            elif exact:
                observation["sha256_match"] = False
        observations.append(observation)
    if present == 0:
        return None
    result: dict[str, Any] = {
        "entry": entry.name,
        "present_artifacts": present,
        "artifact_count": len(specs),
        "all_artifacts_present": present == len(specs),
    }
    if exact:
        result["exact_artifacts"] = exact_count
        result["exact"] = exact_count == len(specs)
        result["artifacts"] = observations
    return result


def resolve_candidates(profile: dict[str, Any], estate: Path, exact: bool) -> list[dict[str, Any]]:
    specs = artifact_specs(profile)
    candidates: list[dict[str, Any]] = []
    for entry in sorted(estate.iterdir(), key=lambda path: path.name):
        if not entry.is_dir() or entry.is_symlink():
            continue
        value = candidate_summary(entry, specs, exact)
        if value is not None:
            candidates.append(value)
    return candidates


def catalog(metadata: Path | None = None, estate: Path | None = None) -> dict[str, Any]:
    boundary, boundary_sha = selected_boundary(metadata)
    obligations = obligation_map(boundary)
    estate_root = configured_estate_root(estate)
    rows: list[dict[str, Any]] = []
    for source_id in sorted(obligations):
        obligation = obligations[source_id]
        profile, profile_sha = maybe_profile(source_id, metadata)
        row: dict[str, Any] = {
            "source_id": source_id,
            "state": obligation.get("state"),
            "providers": obligation.get("providers", []),
            "profile_available": profile is not None,
            "profile_sha256": profile_sha,
            "preflight_available": False,
        }
        if profile is not None:
            candidates = resolve_candidates(profile, estate_root, exact=False)
            row["candidate_entries"] = candidates
            row["preflight_available"] = str(obligation.get("state", "")).startswith("selected") and any(candidate["all_artifacts_present"] for candidate in candidates)
            coordinate = profile.get("coordinate")
            recipe = profile.get("recipe_program")
            row["coordinate"] = coordinate if isinstance(coordinate, dict) else None
            row["recipe_coordinate"] = recipe.get("coordinate") if isinstance(recipe, dict) else None
            row["denominators"] = profile.get("denominators")
        rows.append(row)
    return {
        "schema": CATALOG_SCHEMA,
        "boundary_schema": boundary.get("schema"),
        "boundary_sha256": boundary_sha,
        "classification": boundary.get("classification"),
        "estate_root": str(estate_root),
        "rows": rows,
        "row_count": len(rows),
    }


def preflight(source_id: str, metadata: Path | None = None, estate: Path | None = None) -> dict[str, Any]:
    boundary, boundary_sha = selected_boundary(metadata)
    obligation = obligation_map(boundary).get(source_id)
    if obligation is None:
        raise SourceProductError(f"source is not in the configured selected boundary: {source_id}", 404, "source_not_selected")
    state = obligation.get("state")
    if not isinstance(state, str) or not state.startswith("selected"):
        raise SourceProductError(f"source disposition does not permit admission: {source_id}: {state}", 409, "source_not_admissible")
    profile, profile_sha = maybe_profile(source_id, metadata)
    if profile is None or profile_sha is None:
        raise SourceProductError(f"selected source has no installed exact profile: {source_id}", 409, "source_profile_unavailable")
    estate_root = configured_estate_root(estate)
    candidates = resolve_candidates(profile, estate_root, exact=True)
    exact = [candidate for candidate in candidates if candidate.get("exact") is True]
    if len(exact) != 1:
        raise SourceProductError(
            f"selected source does not resolve to exactly one exact physical artifact graph: {source_id}: matches={len(exact)}",
            409,
            "source_physical_resolution_failed",
        )
    source_root = (estate_root / exact[0]["entry"]).resolve(strict=True)
    try:
        source_root.relative_to(estate_root)
    except ValueError as error:
        raise SourceProductError("resolved source escaped configured estate", 503, "source_estate_escape") from error
    unicode_root = (estate_root / DEFAULT_UNICODE_RELATIVE).resolve(strict=True)
    try:
        unicode_root.relative_to(estate_root)
    except ValueError as error:
        raise SourceProductError("resolved Unicode root escaped configured estate", 503, "source_estate_escape") from error
    recipe = profile.get("recipe_program")
    plan: dict[str, Any] = {
        "schema": PLAN_SCHEMA,
        "source_id": source_id,
        "source_state": state,
        "providers": obligation.get("providers", []),
        "boundary_sha256": boundary_sha,
        "profile_sha256": profile_sha,
        "source_root": str(source_root),
        "unicode_root": str(unicode_root),
        "physical_resolution": exact[0],
        "recipe_coordinate": recipe.get("coordinate") if isinstance(recipe, dict) else None,
        "denominators": profile.get("denominators"),
        "target": {"database": "laplace_refactor", "operation": "source-admission"},
    }
    plan["plan_id"] = sha256_bytes(canonical_bytes(plan))
    return plan


def atomic_write_json(path: Path, value: dict[str, Any]) -> None:
    temporary = path.parent / f".{path.name}.{os.getpid()}.{secrets.token_hex(6)}.tmp"
    encoded = canonical_bytes(value) + b"\n"
    fd: int | None = None
    try:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
        fd = os.open(temporary, flags, 0o640)
        with os.fdopen(fd, "wb", closefd=True) as stream:
            fd = None
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if fd is not None:
            os.close(fd)
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def validate_submission(payload: Any) -> tuple[str, str, str]:
    if not isinstance(payload, dict):
        raise SourceProductError("source job body must be a JSON object")
    extra = sorted(set(payload) - {"source_id", "plan_id", "idempotency_key"})
    if extra:
        raise SourceProductError("unsupported source job field(s): " + ", ".join(extra), 400, "unsupported_parameter")
    source_id = payload.get("source_id")
    plan_id = payload.get("plan_id")
    key = payload.get("idempotency_key")
    if not isinstance(source_id, str) or not source_id:
        raise SourceProductError("source_id is required", 400, "invalid_source_id")
    if not isinstance(plan_id, str) or JOB_ID.fullmatch(plan_id) is None:
        raise SourceProductError("plan_id must be a 64-character lowercase hexadecimal identity", 400, "invalid_plan_id")
    if not isinstance(key, str) or IDEMPOTENCY.fullmatch(key) is None:
        raise SourceProductError("idempotency_key must use 1-128 letters, digits, '.', '_', ':' or '-'", 400, "invalid_idempotency_key")
    return source_id, plan_id, key


def worker_executable() -> Path:
    installed = Path(__file__).resolve().parent / "laplace-source-job-worker"
    if installed.is_file() and not installed.is_symlink() and os.access(installed, os.X_OK):
        return installed
    source = Path(__file__).resolve()
    if source.is_file() and not source.is_symlink():
        return source
    raise SourceProductError("source job worker executable is unavailable", 503, "source_worker_unavailable")


def submit(payload: Any, *, metadata: Path | None = None, estate: Path | None = None, jobs: Path | None = None, launch: bool = True) -> dict[str, Any]:
    source_id, expected_plan_id, key = validate_submission(payload)
    plan = preflight(source_id, metadata, estate)
    if plan["plan_id"] != expected_plan_id:
        raise SourceProductError("reviewed source plan is stale; preflight again before submission", 409, "source_plan_changed")
    job_id = sha256_bytes((expected_plan_id + "\0" + key).encode("utf-8"))
    job_root = configured_job_root(jobs)
    directory = job_root / job_id
    created = False
    try:
        directory.mkdir(mode=0o750)
        created = True
    except FileExistsError:
        pass
    if directory.is_symlink() or not directory.is_dir():
        raise SourceProductError("unsafe source job directory", 503, "source_job_storage_invalid")
    job_path = directory / "job.json"
    if created:
        job = {
            "schema": JOB_SCHEMA,
            "job_id": job_id,
            "idempotency_key": key,
            "state": "queued",
            "attempt": 1,
            "created_at": int(time.time()),
            "plan": plan,
        }
        atomic_write_json(job_path, job)
        if launch:
            stdout = (directory / "worker.stdout.log").open("ab", buffering=0)
            stderr = (directory / "worker.stderr.log").open("ab", buffering=0)
            try:
                subprocess.Popen(
                    [str(worker_executable()), "--worker", str(directory)],
                    stdin=subprocess.DEVNULL,
                    stdout=stdout,
                    stderr=stderr,
                    close_fds=True,
                    start_new_session=True,
                )
            except OSError as error:
                job["state"] = "failed"
                job["finished_at"] = int(time.time())
                job["error"] = f"worker launch failed: {error}"
                atomic_write_json(job_path, job)
                raise SourceProductError(job["error"], 503, "source_worker_launch_failed") from error
            finally:
                stdout.close()
                stderr.close()
    job = load_json(job_path)
    if job.get("job_id") != job_id or job.get("idempotency_key") != key or job.get("plan", {}).get("plan_id") != expected_plan_id:
        raise SourceProductError("idempotent source job identity collided with different state", 500, "source_job_identity_collision")
    return job


def job_status(job_id: str, jobs: Path | None = None) -> dict[str, Any]:
    if JOB_ID.fullmatch(job_id) is None:
        raise SourceProductError("invalid source job id", 400, "invalid_job_id")
    root = configured_job_root(jobs)
    directory = root / job_id
    if directory.is_symlink() or not directory.is_dir():
        raise SourceProductError("source job not found", 404, "source_job_not_found")
    path = directory / "job.json"
    if path.is_symlink() or not path.is_file():
        raise SourceProductError("source job state is unavailable", 500, "source_job_state_invalid")
    return load_json(path)


def installed_sibling(name: str) -> Path:
    path = Path(__file__).resolve().parent / name
    if not path.is_file() or path.is_symlink() or not os.access(path, os.X_OK):
        raise SourceProductError(f"installed product command is unavailable: {path}", 503, "source_worker_dependency_unavailable")
    return path


def run_worker(directory: Path) -> int:
    if not directory.is_absolute() or directory.is_symlink() or not directory.is_dir():
        raise SourceProductError("worker job directory is unsafe", 500, "source_job_storage_invalid")
    lock_path = directory / ".worker.lock"
    fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0), 0o640)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise SourceProductError("source worker lock is not a regular file", 500, "source_job_storage_invalid")
        fcntl.flock(fd, fcntl.LOCK_EX)
        job_path = directory / "job.json"
        job = load_json(job_path)
        if job.get("schema") != JOB_SCHEMA:
            raise SourceProductError("source job schema is invalid", 500, "source_job_state_invalid")
        if job.get("state") == "succeeded":
            return 0
        if job.get("state") not in {"queued", "interrupted", "failed"}:
            raise SourceProductError(f"source job is not runnable from state {job.get('state')}", 409, "source_job_state_conflict")
        job["state"] = "running"
        job["started_at"] = int(time.time())
        job.pop("error", None)
        atomic_write_json(job_path, job)
        plan = job.get("plan")
        if not isinstance(plan, dict):
            raise SourceProductError("source job plan is missing", 500, "source_job_state_invalid")
        command = [
            str(installed_sibling("laplace-admit-source")),
            str(plan["source_id"]),
            str(plan["source_root"]),
            "--unicode-root",
            str(plan["unicode_root"]),
        ]
        try:
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False, timeout=3600, close_fds=True)
        except (OSError, subprocess.TimeoutExpired) as error:
            job["state"] = "interrupted"
            job["finished_at"] = int(time.time())
            job["error"] = f"source admission execution interrupted: {error}"
            atomic_write_json(job_path, job)
            return 1
        if result.returncode != 0:
            job["state"] = "failed"
            job["finished_at"] = int(time.time())
            job["error"] = (result.stderr.strip() or result.stdout.strip() or f"source admission exited {result.returncode}")[-8000:]
            atomic_write_json(job_path, job)
            return 1
        try:
            admission = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            job["state"] = "failed"
            job["finished_at"] = int(time.time())
            job["error"] = f"source admission returned invalid JSON: {error}"
            atomic_write_json(job_path, job)
            return 1
        if not isinstance(admission, dict) or admission.get("schema") != "laplace.admit-source/v1" or admission.get("profile") != plan["source_id"]:
            job["state"] = "failed"
            job["finished_at"] = int(time.time())
            job["error"] = "source admission returned an invalid result contract"
            atomic_write_json(job_path, job)
            return 1
        inspect = subprocess.run(
            [str(installed_sibling("laplace-inspect")), "source-profiles", "--limit", "100"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            timeout=60,
            close_fds=True,
        )
        if inspect.returncode != 0:
            job["state"] = "failed"
            job["finished_at"] = int(time.time())
            job["error"] = (inspect.stderr.strip() or "source-profile readback failed")[-8000:]
            atomic_write_json(job_path, job)
            return 1
        try:
            readback = json.loads(inspect.stdout)
        except json.JSONDecodeError as error:
            job["state"] = "failed"
            job["finished_at"] = int(time.time())
            job["error"] = f"source-profile readback returned invalid JSON: {error}"
            atomic_write_json(job_path, job)
            return 1
        job["state"] = "succeeded"
        job["finished_at"] = int(time.time())
        job["result"] = admission
        job["readback"] = readback
        job["result_sha256"] = sha256_bytes(canonical_bytes({"admission": admission, "readback": readback}))
        atomic_write_json(job_path, job)
        return 0
    finally:
        try:
            fcntl.flock(fd, fcntl.LOCK_UN)
        finally:
            os.close(fd)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", action="store_true")
    parser.add_argument("--preflight")
    parser.add_argument("--job")
    parser.add_argument("--worker", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.catalog:
            value = catalog()
        elif args.preflight is not None:
            value = preflight(args.preflight)
        elif args.job is not None:
            value = job_status(args.job)
        elif args.worker is not None:
            return run_worker(args.worker)
        else:
            raise SourceProductError("select one source-product operation")
        print(json.dumps(value, ensure_ascii=False, sort_keys=True))
        return 0
    except SourceProductError as error:
        print(f"laplace-source-product: {error.message}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
