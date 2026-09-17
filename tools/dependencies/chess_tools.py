#!/usr/bin/env python3
"""Install and probe locked external chess tools; never select Laplace moves."""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import platform
import queue
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from urllib.parse import urlsplit
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.dependencies.git_checkout import GitCheckoutError, snapshot as git_snapshot
SCRATCH = Path("/build/laplace/work")
USER_AGENT = "Laplace-Refactor-chess-dependency-check/1"
REQUESTED_STOCKFISH_SOURCE = Path("/vault/External/Stockfish/SF_19")
SOURCE_SELECTION_SCHEMA = "laplace.stockfish-source-selection/v1"


class ChessToolError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ChessToolError(message)


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def json_read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def json_write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def configuration() -> tuple[dict, dict]:
    selected = json_read(ROOT / "dependencies/chess-release-selection.json")
    artifacts = json_read(ROOT / "dependencies/artifact-lock.json")["artifacts"]
    require(selected.get("schema") == "laplace.chess-release-selection/v1", "invalid chess selection schema")
    locked = json_read(ROOT / "dependencies/lock.json")["dependencies"]
    for tool, release in selected["releases"].items():
        require(locked[tool]["version"] == release["tag"], f"{tool} release and Git lock versions disagree")
    network = artifacts[selected["releases"]["stockfish"]["network"]]
    require(network["version"] == selected["releases"]["stockfish"]["tag"], "Stockfish network generation differs")
    require(network["filename"] == f'nn-{network["sha256"][:12]}.nnue', "Stockfish network filename and digest disagree")
    return selected, artifacts


def github_release_request(repository: str) -> urllib.request.Request:
    require(bool(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository)),
            "invalid GitHub release repository")
    request = urllib.request.Request(
        f"https://api.github.com/repos/{repository}/releases/latest",
        headers={"User-Agent": USER_AGENT, "Accept": "application/vnd.github+json"})
    token = os.environ.get("GITHUB_TOKEN", "").strip()
    if token:
        # urllib excludes unredirected headers from every redirected request.
        # This credential is only for the initial, fixed GitHub API origin.
        request.add_unredirected_header("Authorization", f"Bearer {token}")
    return request


def upstream_versions(selected: dict) -> dict:
    result = {}
    for name, release in selected["releases"].items():
        request = github_release_request(release["repository"])
        with urllib.request.urlopen(request, timeout=30) as response:
            observed = json.load(response)
        require(not observed.get("draft") and not observed.get("prerelease"), f"{name} upstream did not return a stable release")
        result[name] = {"selected": release["tag"], "latest": observed["tag_name"], "current": observed["tag_name"] == release["tag"], "url": observed["html_url"]}
    return result


def acquire(artifact: dict, cache: Path, offline: bool) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / artifact["filename"]
    if not target.exists():
        require(not offline, f"offline artifact missing: {target}")
        fd, name = tempfile.mkstemp(prefix=".download-", dir=cache)
        os.close(fd)
        part = Path(name)
        try:
            request = urllib.request.Request(artifact["url"], headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=60) as response, part.open("wb") as output:
                shutil.copyfileobj(response, output, 1024 * 1024)
            verify_artifact(part, artifact)
            os.replace(part, target)
        finally:
            part.unlink(missing_ok=True)
    verify_artifact(target, artifact)
    return target


def verify_artifact(path: Path, artifact: dict) -> None:
    require(path.is_file() and path.stat().st_size == artifact["size"], f"artifact size mismatch: {path}")
    require(digest(path) == artifact["sha256"], f"artifact SHA-256 mismatch: {path}")


def safe_member(name: str) -> Path:
    item = PurePosixPath(name)
    require(not item.is_absolute() and ".." not in item.parts and "\\" not in name and ":" not in name, f"unsafe archive member: {name}")
    return Path(*item.parts)


def execute(argv: list[str], **options) -> str:
    result = subprocess.run(argv, check=False, capture_output=True, text=True, timeout=options.pop("timeout", 30), **options)
    require(result.returncode == 0, f"{Path(argv[0]).name} failed ({result.returncode}): {(result.stdout + result.stderr)[-3000:]}")
    return result.stdout + result.stderr


def require_build(condition: bool, message: str, log: Path) -> None:
    if not condition:
        with log.open("rb") as source:
            source.seek(max(0, log.stat().st_size - 8192))
            tail = source.read().decode("utf-8", "replace")
        raise ChessToolError(f"{message}; see {log}\n{tail}")


def build_environment() -> dict[str, str]:
    """Keep child Git commands bound to the verified original object identities."""
    return {**os.environ, "GIT_NO_REPLACE_OBJECTS": "1"}


def qt_environment(prefix: Path) -> dict[str, str]:
    """Keep selected SDK tools and their libraries together in child processes."""
    environment = build_environment()
    paths = {"PATH": prefix / "bin"}
    if platform.system() == "Linux":
        paths["LD_LIBRARY_PATH"] = prefix / "lib"
    elif platform.system() == "Darwin":
        paths.update({"DYLD_LIBRARY_PATH": prefix / "lib", "DYLD_FRAMEWORK_PATH": prefix / "lib"})
    for name, directory in paths.items():
        # An inherited system Qt path can override the SDK tool's ELF RUNPATH.
        # Scope the correction to this process tree; preserve other dependencies.
        previous = environment.get(name, "")
        environment[name] = str(directory) + (os.pathsep + previous if previous else "")
    return environment


def tool_environment(tool: dict) -> dict[str, str]:
    prefix = tool.get("qt_prefix")
    return qt_environment(Path(prefix)) if prefix else os.environ.copy()


def single_match(root: Path, pattern: str) -> Path:
    found = [p for p in root.rglob(pattern) if p.is_file()]
    require(len(found) == 1, f"expected one {pattern} in {root}; found {len(found)}")
    return found[0]


def probe_stockfish(argv: list[str], network: dict) -> dict:
    process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    lines: queue.Queue = queue.Queue()

    def reader() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.put(line.rstrip())
        lines.put(None)

    worker = threading.Thread(target=reader, daemon=True)
    worker.start()

    def command(value: str) -> None:
        assert process.stdin is not None
        process.stdin.write(value + "\n")
        process.stdin.flush()

    def until(prefix: str) -> list[str]:
        observed = []
        deadline = time.monotonic() + 30
        while True:
            try:
                line = lines.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty as error:
                raise ChessToolError(f"Stockfish timed out waiting for {prefix}") from error
            require(line is not None, f"Stockfish exited before {prefix}")
            require("CRITICAL ERROR" not in line, f"Stockfish rejected its probe: {line}")
            observed.append(line)
            if line.startswith(prefix):
                return observed

    try:
        command("uci")
        handshake = until("uciok")
        require("id name Stockfish 19" in handshake, "selected binary does not identify as Stockfish 19")
        require(any(line.startswith("option name EvalFile ") and network["filename"] in line for line in handshake), "Stockfish default NNUE differs from the locked network")
        require(not any(line.startswith("option name EvalFileSmall ") for line in handshake), "obsolete secondary NNUE option remains")
        for option in ("Threads", "Hash", "SyzygyPath", "UCI_LimitStrength"):
            require(any(line.startswith(f"option name {option} ") for line in handshake), f"Stockfish lacks required UCI option {option}")
        command("setoption name Threads value 1")
        command("setoption name Hash value 16")
        command("setoption name UCI_LimitStrength value false")
        command("isready")
        until("readyok")
        command("position startpos")
        command("go depth 6")
        search = until("bestmove ")
        bestmove = search[-1].split()[1]
        legal = {f"{file}2{file}{rank}" for file in "abcdefgh" for rank in "34"} | {"b1a3", "b1c3", "g1f3", "g1h3"}
        require(bestmove in legal, f"Stockfish returned an invalid initial move: {bestmove}")
        require(any("depth 6 " in line for line in search), "Stockfish did not complete the requested depth")
        command("quit")
        require(process.wait(timeout=5) == 0, "Stockfish did not exit cleanly")
        return {"uci": "ready", "depth": 6, "bestmove": bestmove, "network": network["filename"], "network_sha256": network["sha256"]}
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        worker.join(timeout=2)
        for stream in (process.stdin, process.stdout):
            if stream:
                stream.close()


def git(source: Path, *arguments: str) -> str:
    return execute(["git", "--no-optional-locks", "--no-replace-objects", "-c", f"safe.directory={source}", "-C", str(source), *arguments], timeout=180).strip()


def verify_tracked_inputs(source: Path, revision: str | None = None) -> dict:
    try:
        return git_snapshot(source, revision)[0]
    except GitCheckoutError as error:
        raise ChessToolError(str(error) + "; preserving source: " + str(source)) from error


def verify_source(source: Path, entry: dict) -> dict:
    verify_origin(source, entry)
    require(git(source, "status", "--porcelain=v1", "--untracked-files=all") == "", f"source has local changes; preserving them: {source}")
    require(git(source, "rev-parse", "HEAD") == entry["revision"], f"source revision differs: {source}")
    observed = verify_tracked_inputs(source, entry["revision"])
    process = subprocess.Popen(["git", "--no-optional-locks", "--no-replace-objects", "-c", f"safe.directory={source}", "-C", str(source), "archive", "--format=tar", entry["revision"]], stdout=subprocess.PIPE)
    checksum = hashlib.sha256()
    assert process.stdout is not None
    for block in iter(lambda: process.stdout.read(1024 * 1024), b""):
        checksum.update(block)
    process.stdout.close()
    require(process.wait() == 0 and checksum.hexdigest() == entry["git_archive_sha256"], f"source archive differs: {source}")
    for license_file in entry["licenses"]:
        require(digest(source / license_file["path"]) == license_file["sha256"], f"source license differs: {source}")
    return observed


def official_git_origin_matches(origin: str, upstream: str) -> bool:
    """Match the declared GitHub repository without accepting credential URLs."""
    def repository(value: str) -> tuple[str, str] | None:
        if value.startswith("git@github.com:"):
            value = "ssh://git@github.com/" + value[len("git@github.com:"):]
        try:
            parsed = urlsplit(value)
            if parsed.hostname is None or parsed.hostname.lower() != "github.com" or parsed.query or parsed.fragment:
                return None
            if parsed.scheme == "https":
                if parsed.username is not None or parsed.password is not None or parsed.port not in (None, 443):
                    return None
            elif parsed.scheme == "ssh":
                if parsed.username != "git" or parsed.password is not None or parsed.port not in (None, 22):
                    return None
            else:
                return None
            path = parsed.path.removesuffix("/").removesuffix(".git")
            if re.fullmatch(r"/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", path) is None:
                return None
            return parsed.hostname.lower(), path.lower()
        except ValueError:
            return None
    expected = repository(upstream)
    return expected is not None and repository(origin) == expected


def verify_origin(source: Path, entry: dict) -> None:
    origin = git(source, "remote", "get-url", "origin")
    # The existing import-verified.sh creates no-hardlink clones with a local
    # origin. Exact archive and license identity still proves that imported tree.
    local_import = Path(origin).is_absolute() or origin.startswith("file:///")
    require(local_import or official_git_origin_matches(origin, entry["upstream"]), f"refusing unrelated source origin: {source}")


def update_source(source: Path, entry: dict, offline: bool) -> None:
    created = not source.exists()
    if not source.exists():
        require(not offline, f"offline source checkout is missing: {source}")
        source.parent.mkdir(parents=True, exist_ok=True)
        execute(["git", "init", "--quiet", str(source)])
        git(source, "remote", "add", "origin", entry["upstream"])
    require((source / ".git").exists(), f"source is not a Git checkout: {source}")
    verify_origin(source, entry)
    require(git(source, "status", "--porcelain=v1", "--untracked-files=all") == "", f"source has local changes; preserving them: {source}")
    if not created:
        # Check the current tree before checkout, even when it is an older release.
        # Index flags must not allow checkout to overwrite hidden operator changes.
        verify_tracked_inputs(source)
    if not offline:
        # Fetch the declared upstream even when a verified import keeps a local
        # origin; preserve that provenance instead of rewriting its remote.
        git(source, "fetch", "--depth=1", entry["upstream"], entry["revision"])
        git(source, "checkout", "--quiet", "--detach", entry["revision"])
    verify_source(source, entry)



def recorded_stockfish_source(prefix: Path) -> Path | None:
    receipt = prefix / "current.json"
    if not receipt.exists():
        return None
    installation = json_read(receipt)
    tools = installation.get("tools")
    require(isinstance(tools, dict), "chess installation receipt has no tools mapping")
    if "stockfish" not in tools:
        return None
    require(isinstance(tools["stockfish"], dict), "recorded Stockfish tool is not a mapping")
    value = tools["stockfish"].get("source")
    require(isinstance(value, str) and bool(value.strip()),
            "recorded Stockfish source is missing; refusing a different checkout")
    return Path(value)


def observe_stockfish_checkout(path: Path, entry: dict, *, official: bool) -> dict:
    """Read only; unavailable candidates never cause clone, checkout or relink."""
    result = {"path": str(path), "exists": None, "available": False,
              "source": None, "git_root": None, "origin": None, "commit": None}
    try:
        require("\n" not in str(path) and "\r" not in str(path),
                "Stockfish input path cannot be exported safely")
        path.lstat()
        result["exists"] = True
        source = path.resolve(strict=True)
        if not source.is_dir() or not (source / ".git").exists():
            result["reason"] = "not-an-existing-git-checkout"
            return result
        root = Path(git(source, "rev-parse", "--show-toplevel")).resolve(strict=True)
        if root != source:
            result["reason"] = "path-is-not-the-git-root"
            return result
        origin = git(source, "remote", "get-url", "origin")
        official_origin = official_git_origin_matches(origin, entry["upstream"])
        local_import = Path(origin).is_absolute() or origin.startswith("file:///")
        if not official_origin and (official or not local_import):
            # Never copy a rejected remote URL (possibly with credentials) into evidence.
            result["reason"] = "origin-is-not-the-official-stockfish-repository"
            return result
        commit = git(source, "rev-parse", "HEAD")
        require(re.fullmatch(r"[0-9a-f]{40,64}", commit) is not None,
                "Stockfish checkout returned an invalid commit")
        require("\n" not in str(source) and "\r" not in str(source),
                "Stockfish source path cannot be exported safely")
        result.update({"available": True, "source": str(source), "git_root": str(root),
                       "origin": origin if official_origin else None, "commit": commit,
                       "origin_kind": "official" if official_origin else "local-import",
                       "reason": "existing-checkout"})
    except FileNotFoundError:
        if result["exists"] is None:
            result["exists"] = False
        result["reason"] = "requested-local-path-unavailable" if not result["exists"] else "checkout-target-unavailable"
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError):
        result["reason"] = "checkout-observation-failed"
    return result


def _path_kind(path: Path) -> str:
    """Observe a directory entry without following its final symlink."""
    try:
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            return "symlink"
        if stat.S_ISDIR(mode):
            return "directory"
        return "regular-file" if stat.S_ISREG(mode) else "other"
    except FileNotFoundError:
        return "missing"
    except NotADirectoryError:
        return "not-a-directory"
    except PermissionError:
        return "permission-denied"
    except (OSError, ValueError, RuntimeError):
        return "observation-failed"


def diagnose_stockfish_path(path: Path, entry: dict) -> dict:
    """Bounded read-only metadata, never an authority for source selection."""
    result = {"schema": "laplace.stockfish-path-diagnostic/v1",
              "input_kind": None, "resolved_path": None, "resolved_kind": None,
              "resolution": "not-observed", "git_marker_kind": None,
              "ancestry": [], "nearest_existing_directory": None,
              "enclosing_git": {"status": "not-observed", "stage": None,
                                "top_level": None, "origin": None, "commit": None}}
    try:
        # No descendant traversal, marker-content reads, or raw Git error retention.
        require(len(str(path)) <= 4096 and "\n" not in str(path) and "\r" not in str(path),
                "diagnostic input path exceeds its envelope")
        absolute = path.absolute()
        ancestors = [absolute, *absolute.parents]
        require(len(ancestors) <= 64, "diagnostic ancestry exceeds its envelope")
        result["input_kind"] = _path_kind(path)
        result["ancestry"] = [{"path": str(item), "kind": _path_kind(item)}
                              for item in reversed(ancestors)]
        try:
            resolved = path.resolve(strict=True)
            require("\n" not in str(resolved) and "\r" not in str(resolved),
                    "diagnostic resolved path is not a single line")
            result.update({"resolved_path": str(resolved), "resolved_kind": _path_kind(resolved),
                           "resolution": "resolved"})
            if result["resolved_kind"] == "directory":
                result["git_marker_kind"] = _path_kind(resolved / ".git")
        except FileNotFoundError:
            result["resolution"] = "missing-target"
        except NotADirectoryError:
            result["resolution"] = "non-directory-component"
        except PermissionError:
            result["resolution"] = "permission-denied"
        except (OSError, ValueError, RuntimeError):
            result["resolution"] = "resolution-failed"

        nearest = None
        for item in ancestors:
            try:
                candidate = item.resolve(strict=True)
                if _path_kind(candidate) == "directory":
                    require("\n" not in str(candidate) and "\r" not in str(candidate),
                            "diagnostic ancestor is not a single line")
                    nearest = candidate
                    break
            except (OSError, ValueError, RuntimeError):
                continue
        if nearest is None:
            result["enclosing_git"]["status"] = "no-observable-directory"
            return result
        result["nearest_existing_directory"] = str(nearest)

        # Inherited Git repository/configuration selectors must not make an
        # unrelated GIT_DIR or GIT_WORK_TREE masquerade as this path's parent.
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("GIT_")}
        environment.update({"GIT_NO_REPLACE_OBJECTS": "1", "GIT_TERMINAL_PROMPT": "0",
                            "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull})
        directories = [nearest, *nearest.parents]
        require(len(directories) <= 64, "diagnostic resolved ancestry exceeds its envelope")
        options = ["git", "--no-optional-locks", "--no-replace-objects"]
        for directory in directories:
            options.extend(["-c", "safe.directory=" + str(directory)])

        def probe(*arguments: str) -> str:
            completed = subprocess.run(
                [*options, "-C", str(nearest), *arguments], stdin=subprocess.DEVNULL,
                capture_output=True, text=True, check=False, timeout=2, env=environment)
            require(completed.returncode == 0, "diagnostic Git command failed")
            # This bounds retained metadata, not subprocess capture memory.
            value = completed.stdout.rstrip("\r\n")
            require(bool(value) and len(value) <= 4096 and "\n" not in value and "\r" not in value,
                    "diagnostic Git output is not one bounded line")
            return value

        git_result = result["enclosing_git"]
        git_result["stage"] = "top-level"
        top = Path(probe("rev-parse", "--show-toplevel"))
        require(top.is_absolute(), "diagnostic Git root is not absolute")
        top = top.resolve(strict=True)
        require(top == nearest or top in nearest.parents,
                "diagnostic Git root is not an ancestor")
        require("\n" not in str(top) and "\r" not in str(top),
                "diagnostic Git root is not a single line")
        git_result["top_level"] = str(top)
        git_result["stage"] = "origin"
        origin = probe("remote", "get-url", "origin")
        if not official_git_origin_matches(origin, entry["upstream"]):
            git_result["status"] = "origin-not-official"
            return result
        git_result["origin"] = origin
        git_result["stage"] = "commit"
        commit = probe("rev-parse", "--verify", "HEAD^{commit}")
        require(re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit) is not None,
                "diagnostic Git commit is invalid")
        git_result.update({"status": "observed-official-checkout", "stage": None, "commit": commit})
    except subprocess.TimeoutExpired:
        result["enclosing_git"]["status"] = "command-timeout"
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError):
        result["enclosing_git"]["status"] = "observation-failed"
    return result


def select_stockfish_source(prefix: Path, explicit: Path | None = None) -> dict:
    """Choose a host checkout without changing it or inventing a runner path."""
    entry = json_read(ROOT / "dependencies/lock.json")["dependencies"]["stockfish"]
    configured = recorded_stockfish_source(prefix)
    requested = observe_stockfish_checkout(REQUESTED_STOCKFISH_SOURCE, entry, official=True)
    requested["diagnostic"] = diagnose_stockfish_path(REQUESTED_STOCKFISH_SOURCE, entry)
    current = observe_stockfish_checkout(configured, entry, official=False) if configured is not None else None
    receipt = {"schema": SOURCE_SELECTION_SCHEMA,
               "requested_local_path": str(REQUESTED_STOCKFISH_SOURCE),
               "requested_checkout": requested,
               "configured_source": str(configured) if configured is not None else None,
               "configured_checkout": current, "explicit_override": str(explicit) if explicit is not None else None,
               "selected_source": None, "selected_checkout": None, "disposition": "refused"}
    if explicit is not None:
        chosen = observe_stockfish_checkout(explicit, entry, official=True)
        reason = "explicit-override"
    elif requested["available"]:
        chosen, reason = requested, "requested-existing-official-checkout"
    elif current is not None:
        chosen, reason = current, "retained-configured-checkout"
    else:
        receipt.update({"disposition": "first-install-default",
                        "selection_reason": "no-existing-requested-or-configured-checkout; portable setup default remains",
                        "planned_official_repository": entry["upstream"]})
        return receipt
    receipt["selection_reason"] = reason
    receipt["selected_checkout"] = chosen
    if chosen["available"]:
        receipt.update({"disposition": "selected", "selected_source": chosen["source"]})
    return receipt


def verify_stockfish_selection(selection: dict, prefix: Path, explicit: Path | None = None) -> dict:
    require(selection.get("schema") == SOURCE_SELECTION_SCHEMA,
            "invalid Stockfish source selection receipt")
    require(selection.get("disposition") in {"selected", "first-install-default"},
            "Stockfish source selection was refused")
    configured = recorded_stockfish_source(prefix)
    require(configured is not None, "Stockfish build receipt is missing")
    source = configured.resolve(strict=True)
    expected = selection.get("selected_source")
    if expected is not None:
        require(isinstance(expected, str) and source == Path(expected).resolve(strict=True),
                "Stockfish current.json source differs from the selected checkout")
    else:
        require(selection["disposition"] == "first-install-default" and
                selection.get("configured_source") is None,
                "Stockfish selection does not name a checkout")
    entry = json_read(ROOT / "dependencies/lock.json")["dependencies"]["stockfish"]
    if explicit is not None:
        override = observe_stockfish_checkout(explicit, entry, official=True)
        require(override["available"] and override["source"] == str(source),
                "Stockfish current.json source differs from the explicit official checkout")
    observation = observe_stockfish_checkout(source, entry, official=False)
    require(observation["available"], "selected Stockfish checkout is unavailable")
    current = json_read(prefix / "current.json")
    tool = current["tools"]["stockfish"]
    tracked = verify_source(source, entry)
    require(tool.get("revision") == entry["revision"] and
            tool.get("source_archive_sha256") == entry["git_archive_sha256"] and tool.get("tracked_source") == tracked,
            "Stockfish current.json committed-source provenance differs from the selected checkout")
    expected_executable = source / "src" / ("stockfish.exe" if platform.system() == "Windows" else "stockfish")
    require(Path(tool["executable"]) == expected_executable and not expected_executable.is_symlink() and
            digest(expected_executable) == tool["sha256"],
            "Stockfish current.json executable differs from the selected direct source build")
    # The build may update an older selected HEAD to the lock. Bind its resulting
    # exact provenance rather than treating the pre-build commit as immutable.
    return {"schema": "laplace.stockfish-source-selection-verification/v1",
            "selection": selection, "selected_source": str(source),
            "selected_checkout": observation, "current_receipt_sha256": digest(prefix / "current.json"),
            "locked_commit": entry["revision"], "disposition": "verified"}


def tool_source(arguments: argparse.Namespace, name: str) -> Path:
    explicit = getattr(arguments, "stockfish_source", None) if name == "stockfish" else None
    if explicit is not None:
        # An operator-selected checkout is already their source. Do not silently
        # initialize another repository after a typo or reshape its directory.
        source = explicit.resolve(strict=True)
        require(source.is_dir() and (source / ".git").exists(),
                f"selected Stockfish source is not an existing Git checkout: {explicit}")
        entry = json_read(ROOT / "dependencies/lock.json")["dependencies"]["stockfish"]
        observation = observe_stockfish_checkout(source, entry, official=True)
        require(observation["available"], "selected Stockfish source is not an existing official checkout")
        return source
    if name == "stockfish" and getattr(arguments, "prefix", None) is not None:
        recorded = recorded_stockfish_source(arguments.prefix)
        if recorded is not None:
            source = recorded.resolve(strict=True)
            require(source.is_dir() and (source / ".git").exists(),
                    "recorded Stockfish source is unavailable; refusing a different checkout")
            return source
    require(arguments.source_root is not None, "default source estate is not selected")
    return arguments.source_root / name


def build_tools(arguments: argparse.Namespace, selected: dict, artifacts: dict) -> dict:
    locked = json_read(ROOT / "dependencies/lock.json")["dependencies"]
    names = [arguments.tool] if arguments.tool else ["stockfish", "cutechess"]
    result = {"schema": "laplace.chess-source-build/v1", "product_chess_activated": False, "tools": {}}
    prior_receipt = arguments.prefix / "current.json"
    prior = json_read(prior_receipt) if prior_receipt.exists() else {}
    for name in names:
        entry = locked[name]
        source = tool_source(arguments, name)
        update_source(source, entry, arguments.offline)
        work = arguments.build_root / name / entry["revision"]
        work.mkdir(parents=True, exist_ok=True)
        log = work / "build.log"
        if name == "stockfish":
            network = artifacts[selected["releases"][name]["network"]]
            upstream_header = (source / "src/evaluate.h").read_text(encoding="utf-8")
            require(f'#define EvalFileDefaultName "{network["filename"]}"' in upstream_header, "source does not select the locked NNUE network")
            downloaded = acquire(network, arguments.cache, arguments.offline)
            target = source / "src" / network["filename"]
            if not target.is_file() or digest(target) != network["sha256"]:
                shutil.copy2(downloaded, target)
            command = ["make", "-C", str(source / "src"), f"-j{arguments.jobs}", "profile-build" if arguments.profile else "build", f"ARCH={arguments.arch}", f"COMP={arguments.compiler}"]
            # Upstream make owns its ignored intermediate objects; clear them before a
            # rebuild so a changed CPU/compiler cannot reuse an incompatible object.
            environment = build_environment()
            execute(["make", "-C", str(source / "src"), "clean"], env=environment)
            with log.open("w") as output:
                completed = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False, env=environment)
            require_build(completed.returncode == 0, "Stockfish source build failed", log)
            executable = source / "src" / ("stockfish.exe" if platform.system() == "Windows" else "stockfish")
            checks = probe_stockfish([str(executable)], network)
        else:
            with_gui = bool(getattr(arguments, "cutechess_gui", False) or
                            prior.get("tools", {}).get("cutechess", {}).get("gui"))
            if arguments.qt_prefix is None:
                arguments.qt_prefix = acquire_qt(arguments, selected, artifacts)
            arguments.qt_prefix = arguments.qt_prefix.resolve()
            environment = qt_environment(arguments.qt_prefix)
            command = ["cmake", "-S", str(source), "-B", str(work), "-UQt6*", "-DCMAKE_BUILD_TYPE=Release", "-DWITH_TESTS=OFF", "-DCMAKE_CXX_STANDARD=17"]
            if arguments.qt_prefix:
                command.append(f"-DCMAKE_PREFIX_PATH={arguments.qt_prefix}")
            with log.open("w") as output:
                configured = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False, env=environment)
                require_build(configured.returncode == 0, "Cute Chess configure failed; requires CMake >=3.20, C++17 and Qt >=6.8 (Core, Widgets, Svg, Concurrent, PrintSupport, Core5Compat); set --qt-prefix for the installed Qt SDK", log)
                # Verified current source bytes cannot authenticate a cached object
                # that was compiled before a hidden local edit was restored.
                targets = ["cli", "gui"] if with_gui else ["cli"]
                built = subprocess.run(["cmake", "--build", str(work), "--clean-first", "--config", "Release", "--target", *targets, "--parallel", str(arguments.jobs)], stdout=output, stderr=subprocess.STDOUT, check=False, env=environment)
                require_build(built.returncode == 0, "Cute Chess source build failed", log)
            executable = single_match(work, "cutechess-cli.exe" if platform.system() == "Windows" else "cutechess-cli")
            if platform.system() == "Windows":
                # Retain the source-build executable and deploy its SDK DLLs next
                # to it; the Windows loader cannot infer our Qt SDK location.
                deployment = arguments.qt_prefix / "bin/windeployqt.exe"
                require(deployment.is_file(), f"Qt runtime deployment tool is missing: {deployment}")
                execute([str(deployment), "--release", "--no-translations", str(executable)], timeout=180, env=environment)
            checks = probe_cutechess([str(executable)], environment=environment)
            gui = None
            if with_gui:
                gui_executable = single_match(work, "cutechess.exe" if platform.system() == "Windows" else "cutechess")
                require(not gui_executable.is_symlink(), "Cute Chess GUI executable cannot be a symlink")
                if platform.system() == "Windows":
                    execute([str(deployment), "--release", "--no-translations", str(gui_executable)], timeout=180, env=environment)
                gui_checks = probe_cutechess_gui([str(gui_executable)], arguments.qt_prefix)
                gui = {"executable": str(gui_executable), "sha256": digest(gui_executable),
                       "build_target": "gui", "checks": gui_checks,
                       "direct_launch": {"argv": [str(gui_executable)], "qt_prefix": str(arguments.qt_prefix),
                                         "plugin_path": str(arguments.qt_prefix / "plugins"),
                                         "environment_prepend": {"PATH": str(arguments.qt_prefix / "bin"),
                                                                 **({"LD_LIBRARY_PATH": str(arguments.qt_prefix / "lib")} if platform.system() == "Linux" else
                                                                    {"DYLD_LIBRARY_PATH": str(arguments.qt_prefix / "lib"), "DYLD_FRAMEWORK_PATH": str(arguments.qt_prefix / "lib")} if platform.system() == "Darwin" else {})},
                                         "environment_set": {"QT_PLUGIN_PATH": str(arguments.qt_prefix / "plugins"),
                                                             "QT_QPA_PLATFORM_PLUGIN_PATH": str(arguments.qt_prefix / "plugins/platforms")},
                                         "interactive_session_required": True}}
        source_observation = verify_source(source, entry)
        result["tools"][name] = {"source": str(source), "revision": entry["revision"], "source_archive_sha256": entry["git_archive_sha256"], "tracked_source": source_observation, "source_verifier_sha256": digest(Path(__file__).with_name("git_checkout.py")), "executable": str(executable), "sha256": digest(executable), "build_command": command, "build_log": str(log), "checks": checks}
        if name == "cutechess":
            result["tools"][name]["qt_prefix"] = str(arguments.qt_prefix)
            if gui is not None:
                result["tools"][name]["gui"] = gui
    arguments.prefix.mkdir(parents=True, exist_ok=True)
    receipt = arguments.prefix / "current.json"
    if receipt.exists():
        prior = json_read(receipt)
        for name, tool in prior.get("tools", {}).items():
            if name not in result["tools"]:
                result["tools"][name] = tool
    fd, temporary = tempfile.mkstemp(prefix=".chess-current-", dir=arguments.prefix)
    os.close(fd)
    try:
        json_write(Path(temporary), result)
        os.replace(temporary, receipt)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return result


def qt_package_version(prefix: Path) -> tuple[str, list[Path]]:
    """Read exact selected-SDK version files without evaluating CMake."""
    version_file = prefix / "lib/cmake/Qt6/Qt6ConfigVersion.cmake"
    version_paths = [version_file]
    version_text = version_file.read_text(encoding="utf-8")
    # Current Qt packages wrap CMake's generated version implementation.
    # Read only this fixed sibling; never execute or follow arbitrary CMake code.
    if re.search(r'(?m)^\s*include\(\s*"\$\{CMAKE_CURRENT_LIST_DIR\}/Qt6ConfigVersionImpl\.cmake"\s*\)',
                 version_text):
        implementation = version_file.with_name("Qt6ConfigVersionImpl.cmake")
        version_paths.append(implementation)
        version_text += "\n" + implementation.read_text(encoding="utf-8")
    versions = re.findall(r'(?m)^\s*set\(\s*PACKAGE_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"\s*\)',
                          version_text)
    require(bool(versions) and len(set(versions)) == 1,
            "Qt package version is missing or conflicting")
    version = versions[0]
    return version, version_paths


def acquire_qt(arguments: argparse.Namespace, selected: dict, artifacts: dict) -> Path:
    """Acquire the SDK prerequisite; Cute Chess itself is always built from source."""
    sdk = selected["qt"]
    candidates = [os.environ.get("LAPLACE_QT_PREFIX", ""), os.environ.get("QT_ROOT_DIR", "")]
    candidates += re.split(r"[;]" if platform.system() == "Windows" else r"[;:]", os.environ.get("CMAKE_PREFIX_PATH", ""))
    for value in candidates:
        if not value:
            continue
        candidate = Path(value)
        version_file = candidate / "lib/cmake/Qt6/Qt6ConfigVersion.cmake"
        if version_file.is_file() and (candidate / "lib/cmake/Qt6Core5Compat/Qt6Core5CompatConfig.cmake").is_file() and (candidate / "lib/cmake/Qt6Svg/Qt6SvgConfig.cmake").is_file():
            try:
                version, _ = qt_package_version(candidate)
            except (ChessToolError, OSError, ValueError):
                continue
            if version == sdk["version"]:
                return candidate.resolve()
    platform_key = f"{platform.system()}-{platform.machine()}"
    provider = sdk["platforms"].get(platform_key)
    require(provider is not None, f"Qt SDK acquisition is not selected for {platform_key}; provide --qt-prefix for Qt >=6.8")
    qt_root = arguments.prefix / "qt"
    qt_prefix = qt_root / sdk["version"] / provider["directory"]
    if (qt_prefix / "lib/cmake/Qt6Core5Compat/Qt6Core5CompatConfig.cmake").is_file() and (qt_prefix / "lib/cmake/Qt6Svg/Qt6SvgConfig.cmake").is_file():
        return qt_prefix
    require(not arguments.offline, f"Qt SDK is missing at {qt_prefix}; run online setup or provide --qt-prefix")
    qt_root.mkdir(parents=True, exist_ok=True)
    environment = qt_root / "aqt-venv"
    python = environment / ("Scripts/python.exe" if platform.system() == "Windows" else "bin/python")
    if not python.is_file():
        execute([sys.executable, "-m", "venv", str(environment)], timeout=120)
    bootstrap_pip(python, qt_root, arguments.cache, artifacts[sdk["pip_artifact"]])
    wheel = acquire(artifacts[sdk["aqt_artifact"]], arguments.cache, False)
    # pip's report records exact transitive wheels, versions and acquisition hashes.
    # aqt verifies the Qt archive digests published in the official Qt repository.
    install_aqt(python, qt_root, wheel)
    # In Qt 6.11 the base SDK includes Svg; requesting a separate qtsvg module
    # fails because that package is not present in the official catalog.
    execute([str(python), "-m", "aqt", "install-qt", provider["host"], "desktop", sdk["version"], provider["architecture"], "--outputdir", str(qt_root), "--modules", "qt5compat"], timeout=1800)
    require((qt_prefix / "lib/cmake/Qt6Core5Compat/Qt6Core5CompatConfig.cmake").is_file(), f"Qt Core5Compat missing after acquisition: {qt_prefix}")
    require((qt_prefix / "lib/cmake/Qt6Svg/Qt6SvgConfig.cmake").is_file(), f"Qt Svg missing after acquisition: {qt_prefix}")
    return qt_prefix


def bootstrap_pip(python: Path, qt_root: Path, cache: Path, artifact: dict) -> None:
    """Upgrade only the SDK venv from verified bytes before using pip reports."""
    wheel = acquire(artifact, cache, False)
    # Ubuntu 22.04 ensurepip seeds pip 22.0.2, predating --report (22.2).
    # The bootstrap command intentionally needs no report or package index.
    execute([str(python), "-m", "pip", "install", "--disable-pip-version-check",
             "--no-index", "--no-deps", "--upgrade", str(wheel)], timeout=120)
    observed = execute([str(python), "-m", "pip", "--version"]).strip()
    require(observed.startswith(f"pip {artifact['version']} "), "Qt acquisition pip version differs from selected artifact")
    json_write(qt_root / "pip-bootstrap.json", {
        "schema": "laplace.chess-pip-bootstrap/v1", "version": artifact["version"],
        "artifact_sha256": artifact["sha256"], "artifact_size": artifact["size"],
        "python": str(python), "observed": observed,
    })


def install_aqt(python: Path, qt_root: Path, wheel: Path) -> Path:
    # A retry after a Qt download failure may install no wheels. Give every pip
    # attempt its own report so that this empty delta cannot erase acquisition
    # provenance from the successful earlier aqt installation.
    attempt = Path(tempfile.mkdtemp(prefix="aqt-acquisition-", dir=qt_root))
    report = attempt / "report.json"
    execute([str(python), "-m", "pip", "install", "--disable-pip-version-check",
             "--report", str(report), str(wheel)], timeout=600)
    return report


def qt_gui_environment(prefix: Path) -> dict[str, str]:
    environment = qt_environment(prefix)
    environment["QT_PLUGIN_PATH"] = str(prefix / "plugins")
    environment["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(prefix / "plugins/platforms")
    return environment


def qt_gui_inventory(prefix: Path) -> dict:
    """Presence/identity inventory is separate from actual plugin initialization."""
    modules = ("Core", "Gui", "Widgets", "Concurrent", "Svg", "PrintSupport", "Core5Compat")
    module_paths = {name: prefix / f"lib/cmake/Qt6{name}/Qt6{name}Config.cmake" for name in modules}
    for name, path in module_paths.items():
        require(path.is_file(), f"Cute Chess GUI Qt module is missing: {name} ({path})")
    version, version_paths = qt_package_version(prefix)
    require(tuple(map(int, version.split("."))) >= (6, 8, 0),
            "Cute Chess GUI requires Qt >=6.8")
    system = platform.system()
    suffix, lead = (".dll", "") if system == "Windows" else (".dylib", "lib") if system == "Darwin" else (".so", "lib")
    names = {"offscreen": "platforms/qoffscreen", "minimal": "platforms/qminimal",
             "svg_image": "imageformats/qsvg", "svg_icon": "iconengines/qsvgicon"}
    if system == "Linux":
        # Qt 6.11 uses qwayland; compatible Qt 6.8 SDKs split generic/EGL.
        # Inventory filenames independently; presence does not prove loadability.
        names.update({"xcb": "platforms/qxcb", "wayland": "platforms/qwayland",
                      "wayland_generic": "platforms/qwayland-generic",
                      "wayland_egl": "platforms/qwayland-egl"})
    elif system == "Windows":
        names["windows"] = "platforms/qwindows"
    elif system == "Darwin":
        names["cocoa"] = "platforms/qcocoa"
    plugins = {}
    for name, relative in names.items():
        item = Path(relative)
        path = prefix / "plugins" / item.parent / (lead + item.name + suffix)
        present = path.is_file()
        plugins[name] = {"path": str(path), "present": present,
                         "sha256": digest(path) if present else None}
    require(plugins["offscreen"]["present"], "Cute Chess GUI offscreen platform plugin is missing")
    return {"qt_version": version,
            "version_files": [{"path": str(path), "sha256": digest(path)} for path in version_paths],
            "modules": {name: {"path": str(path), "sha256": digest(path)} for name, path in module_paths.items()},
            "module_identity_scope": "CMake package configuration files; Qt shared-library binaries are not hashed",
            "plugins": plugins, "scope": "file inventory; plugin presence alone is not loadability"}


def probe_cutechess_gui(argv: list[str], prefix: Path) -> dict:
    inventory = qt_gui_inventory(prefix)
    environment = qt_gui_environment(prefix)
    display = {name: bool(environment.get(name)) for name in ("DISPLAY", "WAYLAND_DISPLAY")}
    command = [*argv, "-platform", "offscreen", "--version"]
    # QApplication initializes before upstream's --version branch. It returns
    # before newDefaultGame and app.exec: no window/event-loop proof is implied.
    require(SCRATCH.is_dir(), f"Required GUI probe scratch is missing: {SCRATCH}")
    with tempfile.TemporaryDirectory(prefix="cutechess-gui-probe-", dir=SCRATCH) as scratch:
        directory = Path(scratch)
        for name, relative in (("XDG_CONFIG_HOME", "config"), ("XDG_CONFIG_DIRS", "system-config"),
                               ("XDG_CACHE_HOME", "cache"), ("XDG_DATA_HOME", "data"),
                               ("XDG_RUNTIME_DIR", "runtime")):
            target = directory / relative
            target.mkdir(mode=0o700)
            environment[name] = str(target)
        environment.update({"QT_QPA_PLATFORM": "offscreen", "QT_DEBUG_PLUGINS": "1",
                            "QT_LOGGING_RULES": "qt.core.library=true;qt.core.plugin.*=true",
                            "QT_MESSAGE_PATTERN": "%{category}: %{message}"})
        for name in ("DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORMTHEME", "QT_QPA_GENERIC_PLUGINS"):
            environment.pop(name, None)
        started = time.perf_counter()
        completed = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True,
                                   text=True, check=False, timeout=30, env=environment)
        elapsed = time.perf_counter() - started
    output = completed.stdout + completed.stderr
    require(completed.returncode == 0,
            f"Cute Chess GUI headless initialization failed ({completed.returncode}): {output[-6000:]}")
    version = re.search(r"^Cute Chess (1\.5\.1)(?:\s|$)", completed.stdout, re.M)
    qt = re.search(r"Using Qt version ([0-9.]+)", completed.stdout)
    require(version is not None, "Cute Chess GUI version mismatch")
    require(qt is not None and qt.group(1) == inventory["qt_version"],
            "Cute Chess GUI loaded Qt differs from the selected SDK")
    loaded = []
    for match in re.finditer(r'^qt\.core\.library: ("(?:\\.|[^"\\])*") loaded library\s*$', output, re.M):
        value = json.loads(match.group(1))
        if Path(value).name == Path(inventory["plugins"]["offscreen"]["path"]).name:
            loaded.append(str(Path(value).resolve(strict=True)))
    expected = str(Path(inventory["plugins"]["offscreen"]["path"]).resolve(strict=True))
    require(set(loaded) == {expected}, "Cute Chess GUI did not prove loading the selected offscreen plugin")
    require(qt_gui_inventory(prefix) == inventory, "Cute Chess GUI Qt plugin files changed during the probe")
    return {"schema": "laplace.cutechess-gui-runtime/v1", "disposition": "ready-headless",
            "qapplication_initialized": True, "platform": "offscreen", "scope": "official GUI QApplication initialization and version; no window or event loop",
            "command": command, "elapsed_seconds": elapsed, "version": version.group(1),
            "qt": inventory, "loaded_offscreen_plugin": {"path": expected, "sha256": inventory["plugins"]["offscreen"]["sha256"]},
            "plugin_load_diagnostic_sha256": hashlib.sha256(output.encode("utf-8")).hexdigest(),
            "interactive_desktop_tested": False, "interactive_desktop_ready": None,
            "display_environment_present": display,
            # Upstream selects IniFormat. On Darwin its user path honors XDG,
            # but system INI fallbacks and Qt native preferences do not.
            "settings_scope": {"Windows": "Windows-user-known-folder; version branch only",
                               "Darwin": "isolated-XDG-INI-user-settings; system and Qt native settings not isolated"
                               }.get(platform.system(), "isolated-XDG-probe")}


def verify_cutechess_gui(tool: dict) -> dict:
    gui = tool.get("gui")
    require(isinstance(gui, dict), "Cute Chess GUI has not been built; install --cutechess-gui")
    executable = Path(gui["executable"])
    require(executable.is_file() and not executable.is_symlink() and digest(executable) == gui["sha256"],
            "Cute Chess GUI executable differs; rebuild")
    prefix = Path(tool["qt_prefix"])
    require(qt_gui_inventory(prefix) == gui["checks"]["qt"], "Cute Chess GUI Qt plugin inventory differs; rebuild")
    checks = probe_cutechess_gui([str(executable)], prefix)
    require(digest(executable) == gui["sha256"], "Cute Chess GUI executable changed during the probe")
    return checks


def probe_cutechess(argv: list[str], *, environment: dict[str, str] | None = None) -> dict:
    output = execute([*argv, "--version"], env=environment)
    require(re.search(r"^cutechess-cli 1\.5\.1(?:\s|$)", output, re.M) is not None, "Cute Chess version mismatch")
    qt = re.search(r"Using Qt version (\d+)\.(\d+)\.(\d+)", output)
    require(qt is not None and tuple(map(int, qt.groups())) >= (6, 8, 0), "Cute Chess needs Qt >=6.8")
    help_text = execute([*argv, "--help"], env=environment)
    for option in ("-engine", "-pgnout", "-repeat", "-openings"):
        require(option in help_text, f"Cute Chess lacks {option}")
    return {"disposition": "ready", "version_output": output.strip()}


def verify_installation(prefix: Path, selected: dict, artifacts: dict, only: str | None = None, *, with_gui: bool = False) -> dict:
    installation = json_read(prefix / "current.json")
    locked = json_read(ROOT / "dependencies/lock.json")["dependencies"]
    names = [only] if only else ["stockfish", "cutechess"]
    for name in names:
        require(name in installation["tools"], f"{name} has not been built; run install")
        tool = installation["tools"][name]
        require(tool["revision"] == locked[name]["revision"], f"{name} release selection differs; rebuild")
        verify_source(Path(tool["source"]), locked[name])
        if name == "stockfish":
            network = artifacts[selected["releases"][name]["network"]]
            verify_artifact(Path(tool["source"]) / "src" / network["filename"], network)
        require(digest(Path(tool["executable"])) == tool["sha256"], f"{name} executable differs; rebuild")
        tool["checks"] = probe_stockfish([tool["executable"]], artifacts[selected["releases"]["stockfish"]["network"]]) if name == "stockfish" else probe_cutechess([tool["executable"]], environment=tool_environment(tool))
        if name == "cutechess" and (with_gui or tool.get("gui") is not None):
            gui_checks = verify_cutechess_gui(tool)
            tool["gui"]["checks"] = gui_checks
    return installation


def tablebase_readback(manifest_path: Path | None) -> dict:
    if manifest_path is None:
        return {"disposition": "not-verified", "configured_path": os.environ.get("SYZYGY_PATH"), "why": "Pass --syzygy-manifest with exact WDL and DTZ files, sizes and SHA-256. A directory alone is not a complete table set."}
    manifest = json_read(manifest_path)
    require(manifest.get("coverage") and manifest.get("files"), "Syzygy manifest must name material coverage and files")
    root = Path(manifest["root"])
    types = set()
    for item in manifest["files"]:
        path = root / safe_member(item["path"])
        require(path.suffix in {".rtbw", ".rtbz"}, f"unexpected Syzygy file: {path}")
        verify_artifact(path, item)
        types.add(path.suffix)
    require(types == {".rtbw", ".rtbz"}, "Syzygy coverage is missing WDL or DTZ files")
    return {"disposition": "manifest-bytes-verified-probe-not-implemented", "coverage": manifest["coverage"], "file_count": len(manifest["files"]), "manifest_sha256": digest(manifest_path)}


class LichessNoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, location):
        # Neither the Authorization header nor the token-test POST body may move
        # to another endpoint. Authentication observes only these fixed origins.
        return None


def lichess_document(opener, path: str, token: str) -> dict:
    require(path in {"/api/account", "/api/token/test"}, "unsupported Lichess account probe")
    headers = {"User-Agent": USER_AGENT, "Accept": "application/json"}
    data = None
    if path == "/api/token/test":
        headers["Content-Type"] = "text/plain"
        data = token.encode("utf-8")
    request = urllib.request.Request("https://lichess.org" + path, headers=headers, data=data)
    if data is None:
        request.add_unredirected_header("Authorization", f"Bearer {token}")
    with opener.open(request, timeout=15) as response:
        require(response.status == 200, "unexpected Lichess account response")
        # A token-test response names the secret in its object key. Neither the
        # body nor JSON/HTTP exception details belong in retained diagnostics.
        payload = response.read(64 * 1024 + 1)
    require(len(payload) <= 64 * 1024, "Lichess account response exceeded 64 KiB")
    document = json.loads(payload)
    require(isinstance(document, dict), "Lichess account response is not an object")
    return document


def lichess_readback(online: bool) -> dict:
    token = os.environ.get("LICHESS_TOKEN", "").strip()
    result = {
        "disposition": "not-verified", "credentials_present": bool(token),
        "required_scope": "bot:play", "product_bot_api_adapter": "not-implemented",
        "product_service": "not-implemented", "product_gameplay_ready": False,
        "operator_stop_marker": "not-applicable-no-product-service",
        "token_valid": None, "bot_account": None, "bot_play_scope": None,
        "account_prerequisites_ready": False, "online_attempted": False,
    }
    if not online or not token:
        return result
    # The official API specifies sequential requests. Stop immediately on any
    # unsuccessful response; this doctor never retries or starts a game stream.
    # https://github.com/lichess-org/api/blob/master/doc/specs/tags/oauth/api-token-test.yaml
    result["online_attempted"] = True
    operation = "account"
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), LichessNoRedirect())
    try:
        account = lichess_document(opener, "/api/account", token)
        account_id, username = account.get("id"), account.get("username")
        require(isinstance(account_id, str) and bool(account_id.strip())
                and isinstance(username, str) and bool(username.strip()),
                "Lichess account response omitted identity")
        result.update({"token_valid": True, "bot_account": account.get("title") == "BOT",
                       "account_id": account_id, "account_username": username})
        operation = "token-scope"
        document = lichess_document(opener, "/api/token/test", token)
        require(token in document, "Lichess token response omitted requested token")
        info = document[token]
        if info is None:
            result.update({"token_valid": False, "disposition": "token-rejected"})
            return result
        require(isinstance(info, dict) and isinstance(info.get("scopes"), str),
                "Lichess token response omitted scopes")
        require(info.get("userId") == account_id, "Lichess token/account identity mismatch")
        result["bot_play_scope"] = "bot:play" in {item.strip() for item in info["scopes"].split(",")}
        result["account_prerequisites_ready"] = result["bot_account"] and result["bot_play_scope"]
        result["disposition"] = "account-and-scope-readback-only"
    except urllib.error.HTTPError as error:
        result.update({"disposition": "account-readback-failed", "failed_operation": operation,
                       "http_status": error.code})
        if error.code == 401:
            result["token_valid"] = False
        if error.code == 429:
            # No retry occurs here. A server-supplied longer delay remains visible.
            retry = error.headers.get("Retry-After", "") if error.headers else ""
            result["minimum_retry_delay_seconds"] = max(60, int(retry)) if retry.isascii() and retry.isdigit() and len(retry) <= 12 else 60
        error.close()
    except (OSError, ValueError, http.client.HTTPException, ChessToolError):
        result.update({"disposition": "account-readback-failed", "failed_operation": operation,
                       "error": "Lichess account/scope transport or response validation failed"})
    return result

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["install", "check", "latest", "run", "activate-profile", "run-profile", "select-source", "check-source-selection"])
    parser.add_argument("--prefix", type=Path, default=Path("/opt/laplace/tools/chess"))
    parser.add_argument("--cache", type=Path, default=Path("/opt/laplace/external/chess-downloads"))
    parser.add_argument("--source-root", type=Path, default=Path(os.environ["LAPLACE_VERIFIED_SOURCE_ROOT"]) if os.environ.get("LAPLACE_VERIFIED_SOURCE_ROOT") else None)
    parser.add_argument("--stockfish-source", type=Path,
                        default=Path(os.environ["LAPLACE_STOCKFISH_SOURCE"].strip()) if os.environ.get("LAPLACE_STOCKFISH_SOURCE", "").strip() else None,
                        help="build this existing official Stockfish Git checkout directly (or LAPLACE_STOCKFISH_SOURCE); overrides source-root/stockfish")
    parser.add_argument("--selection-output", type=Path, help="retain a read-only source selection or verification receipt")
    parser.add_argument("--selection-receipt", type=Path, help="source selection to compare with the completed build")
    parser.add_argument("--build-root", type=Path, default=Path("/build/laplace/build/chess"))
    parser.add_argument("--qt-prefix", type=Path, help="use this compatible Qt SDK instead of acquiring the selected SDK")
    parser.add_argument("--cutechess-gui", action="store_true",
                        help="install/check the official GUI as well as CLI, or run the GUI with --tool cutechess")
    available_cpus = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 1)
    build_jobs = os.environ.get("LAPLACE_BUILD_JOBS") or os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL") or str(available_cpus)
    parser.add_argument("--jobs", type=int, default=build_jobs)
    parser.add_argument("--arch", default="native")
    parser.add_argument("--compiler", choices=["gcc", "clang", "icx", "mingw"], default="gcc")
    parser.add_argument("--profile", action="store_true", help="use the upstream profile-build target for PGO")
    parser.add_argument("--offline", action="store_true", help="use already verified cached downloads; omit online freshness readback")
    parser.add_argument("--online", action="store_true", help="read latest releases and, if configured, the Lichess account; sends no game or chat actions")
    parser.add_argument("--syzygy-manifest", type=Path)
    parser.add_argument("--tool", choices=["stockfish", "cutechess"])
    parser.add_argument("--calibration-receipt", type=Path,
                        help="completed local v2 calibration receipt to activate")
    parser.add_argument("--calibration-sha256", help="explicit SHA256 of that completed receipt")
    parser.add_argument("--profile-mode", choices=["analysis", "games", "both"], default="both")
    parser.add_argument("--profile-max-age", type=int, default=604800,
                        help="maximum calibration age in seconds, retained in the activated profile")
    parser.add_argument("--profile-output", type=Path,
                        help="new directory for actual measured-profile invocation evidence")
    parser.add_argument("--uci-input", type=Path, help="finite UCI command file for analysis run-profile")
    parser.add_argument("--profile-timeout", type=float, default=600,
                        help="finite wall limit for one measured-profile invocation")
    arguments, extra = parser.parse_known_args()
    if arguments.action not in {"run", "run-profile"} and extra:
        parser.error("unrecognized arguments: " + " ".join(extra))
    try:
        selected, artifacts = configuration()
        if arguments.cutechess_gui:
            require(arguments.action in {"install", "check", "run"} and arguments.tool != "stockfish",
                    "--cutechess-gui requires install/check, or run --tool cutechess")
        if arguments.action in {"select-source", "check-source-selection"}:
            require(arguments.selection_output is not None, "--selection-output is required")
            if arguments.action == "select-source":
                observation = select_stockfish_source(arguments.prefix, arguments.stockfish_source)
            else:
                require(arguments.selection_receipt is not None, "--selection-receipt is required")
                observation = verify_stockfish_selection(json_read(arguments.selection_receipt), arguments.prefix, arguments.stockfish_source)
            destination = arguments.selection_output.resolve()
            for checkout in (observation.get("requested_checkout"), observation.get("configured_checkout"),
                             observation.get("selected_checkout")):
                if checkout and checkout.get("source"):
                    source = Path(checkout["source"])
                    require(destination != source and source not in destination.parents,
                            "selection evidence must be outside the upstream checkout")
            arguments.selection_output.parent.mkdir(parents=True, exist_ok=True)
            json_write(arguments.selection_output, observation)
            require(observation["disposition"] != "refused", "Stockfish source selection refused; consult retained receipt")
            print(json.dumps(observation, indent=2, sort_keys=True))
            return 0
        if arguments.action in {"activate-profile", "run-profile"}:
            # One explicit consumer owns measured profile activation and execution.
            # Ordinary run remains the unrestricted direct executable route.
            if __name__ == "__main__":
                sys.modules.setdefault("chess_tools", sys.modules[__name__])
            import chess_profiles
            require(not arguments.cutechess_gui and arguments.tool is None,
                    "measured profile mode selects the direct engine or CuteChess CLI")
            if arguments.action == "activate-profile":
                require(arguments.calibration_receipt is not None and
                        arguments.calibration_sha256 is not None,
                        "activate-profile requires --calibration-receipt and --calibration-sha256")
                result = chess_profiles.activate(arguments.prefix, arguments.calibration_receipt,
                    arguments.calibration_sha256, arguments.profile_mode, arguments.profile_max_age)
                print(json.dumps(result, indent=2, sort_keys=True))
                return 0
            require(arguments.profile_output is not None,
                    "run-profile requires --profile-output")
            extra = extra[1:] if extra[:1] == ["--"] else extra
            return chess_profiles.run(arguments.prefix, arguments.profile_mode,
                arguments.profile_output, arguments.uci_input, extra, arguments.profile_timeout)
        if arguments.action == "run":
            require(arguments.tool is not None, "run requires --tool")
            current = json_read(arguments.prefix / "current.json")
            require(arguments.tool in current["tools"], f"{arguments.tool} has not been built")
            extra = extra[1:] if extra[:1] == ["--"] else extra
            tool = current["tools"][arguments.tool]
            if arguments.cutechess_gui:
                verify_cutechess_gui(tool)
                return subprocess.call([tool["gui"]["executable"], *extra], env=qt_gui_environment(Path(tool["qt_prefix"])))
            require(digest(Path(tool["executable"])) == tool["sha256"], "direct source executable changed; run check or rebuild")
            return subprocess.call([tool["executable"], *extra], env=tool_environment(tool))
        report = {"schema": "laplace.chess-dependency-readback/v1", "product_chess_activated": False, "capability_boundary": selected["capability_boundary"]}
        if arguments.action == "latest" or arguments.online or (arguments.action == "install" and not arguments.offline):
            report["upstream"] = upstream_versions(selected)
            require(all(item["current"] for item in report["upstream"].values()), "newer upstream stable release exists; update exact release and artifact locks before creating a new experiment generation: " + json.dumps(report["upstream"]))
        if arguments.action == "install":
            require(arguments.jobs > 0, "--jobs must be positive")
            if arguments.source_root is None and not (arguments.tool == "stockfish" and
                    (arguments.stockfish_source is not None or recorded_stockfish_source(arguments.prefix) is not None)):
                arguments.source_root = Path(subprocess.check_output([sys.executable, str(ROOT / "tools/dependencies/source_estate.py")], text=True).strip())
            if arguments.source_root is not None:
                arguments.source_root = arguments.source_root.resolve()
            arguments.build_root = arguments.build_root.resolve()
            report["installation"] = build_tools(arguments, selected, artifacts)
        elif arguments.action == "check":
            report["installation"] = verify_installation(arguments.prefix, selected, artifacts, arguments.tool, with_gui=arguments.cutechess_gui)
        if arguments.action != "latest":
            report["syzygy"] = tablebase_readback(arguments.syzygy_manifest)
            report["lichess"] = lichess_readback(arguments.online)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except (ChessToolError, OSError, ValueError, KeyError, subprocess.SubprocessError, urllib.error.URLError) as error:
        print(json.dumps({"disposition": "dependency-check-failed", "error": str(error), "product_chess_activated": False}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
