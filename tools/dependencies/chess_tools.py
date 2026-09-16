#!/usr/bin/env python3
"""Install and probe locked external chess tools; never select Laplace moves."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import queue
import re
import shutil
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


def upstream_versions(selected: dict) -> dict:
    result = {}
    for name, release in selected["releases"].items():
        url = f'https://api.github.com/repos/{release["repository"]}/releases/latest'
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/vnd.github+json"})
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


def select_stockfish_source(prefix: Path, explicit: Path | None = None) -> dict:
    """Choose a host checkout without changing it or inventing a runner path."""
    entry = json_read(ROOT / "dependencies/lock.json")["dependencies"]["stockfish"]
    configured = recorded_stockfish_source(prefix)
    requested = observe_stockfish_checkout(REQUESTED_STOCKFISH_SOURCE, entry, official=True)
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
                built = subprocess.run(["cmake", "--build", str(work), "--clean-first", "--config", "Release", "--target", "cli", "--parallel", str(arguments.jobs)], stdout=output, stderr=subprocess.STDOUT, check=False, env=environment)
                require_build(built.returncode == 0, "Cute Chess source build failed", log)
            executable = single_match(work, "cutechess-cli.exe" if platform.system() == "Windows" else "cutechess-cli")
            if platform.system() == "Windows":
                # Retain the source-build executable and deploy its SDK DLLs next
                # to it; the Windows loader cannot infer our Qt SDK location.
                deployment = arguments.qt_prefix / "bin/windeployqt.exe"
                require(deployment.is_file(), f"Qt runtime deployment tool is missing: {deployment}")
                execute([str(deployment), "--release", "--no-translations", str(executable)], timeout=180, env=environment)
            checks = probe_cutechess([str(executable)], environment=environment)
        source_observation = verify_source(source, entry)
        result["tools"][name] = {"source": str(source), "revision": entry["revision"], "source_archive_sha256": entry["git_archive_sha256"], "tracked_source": source_observation, "source_verifier_sha256": digest(Path(__file__).with_name("git_checkout.py")), "executable": str(executable), "sha256": digest(executable), "build_command": command, "build_log": str(log), "checks": checks}
        if name == "cutechess":
            result["tools"][name]["qt_prefix"] = str(arguments.qt_prefix)
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
            version = re.search(r'set\(PACKAGE_VERSION "([0-9.]+)"\)', version_file.read_text())
            if version and version.group(1) == sdk["version"]:
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


def probe_cutechess(argv: list[str], *, environment: dict[str, str] | None = None) -> dict:
    output = execute([*argv, "--version"], env=environment)
    require(re.search(r"^cutechess-cli 1\.5\.1(?:\s|$)", output, re.M) is not None, "Cute Chess version mismatch")
    qt = re.search(r"Using Qt version (\d+)\.(\d+)\.(\d+)", output)
    require(qt is not None and tuple(map(int, qt.groups())) >= (6, 8, 0), "Cute Chess needs Qt >=6.8")
    help_text = execute([*argv, "--help"], env=environment)
    for option in ("-engine", "-pgnout", "-repeat", "-openings"):
        require(option in help_text, f"Cute Chess lacks {option}")
    return {"disposition": "ready", "version_output": output.strip()}


def verify_installation(prefix: Path, selected: dict, artifacts: dict, only: str | None = None) -> dict:
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


def lichess_readback(online: bool) -> dict:
    token = os.environ.get("LICHESS_TOKEN", "").strip()
    result = {"disposition": "not-verified", "credentials_present": bool(token), "required_scope": "bot:play", "product_bot_api_adapter": "not-implemented"}
    if online and token:
        request = urllib.request.Request("https://lichess.org/api/account", headers={"Authorization": f"Bearer {token}", "User-Agent": USER_AGENT})
        with urllib.request.urlopen(request, timeout=30) as response:
            account = json.load(response)
        result.update({"disposition": "account-readback-only", "bot_account": account.get("title") == "BOT", "bot_play_scope": "not-verified-by-account-endpoint"})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["install", "check", "latest", "run", "select-source", "check-source-selection"])
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
    arguments, extra = parser.parse_known_args()
    if arguments.action != "run" and extra:
        parser.error("unrecognized arguments: " + " ".join(extra))
    try:
        selected, artifacts = configuration()
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
        if arguments.action == "run":
            require(arguments.tool is not None, "run requires --tool")
            current = json_read(arguments.prefix / "current.json")
            require(arguments.tool in current["tools"], f"{arguments.tool} has not been built")
            extra = extra[1:] if extra[:1] == ["--"] else extra
            tool = current["tools"][arguments.tool]
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
            report["installation"] = verify_installation(arguments.prefix, selected, artifacts, arguments.tool)
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
