"""Load an artifact-locked external PGN rules provider without a global install.

This provider validates external benchmark games. It is not a native Laplace
chess implementation. Upstream uses absolute chess imports, so an unrelated
already-imported package is refused instead of silently reused or replaced.
"""
from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tarfile
import tempfile
import threading
from types import ModuleType

import chess_tools as tools


_MODULES = ("chess", "chess.svg", "chess.engine", "chess.variant", "chess.pgn",
            "chess.polyglot", "chess.syzygy", "chess.gaviota")
_loaded: dict[str, ModuleType] = {}
_lock = threading.RLock()


def _module_path(name: str) -> str:
    return "chess/__init__.py" if name == "chess" else name.replace(".", "/") + ".py"


def _regular_file(path: Path) -> bool:
    try:
        status = path.lstat()
    except FileNotFoundError:
        return False
    return stat.S_ISREG(status.st_mode) and status.st_nlink == 1


def _runtime_files(archive: Path, version: str) -> dict[str, bytes]:
    """Inspect every member; read only the official runtime and its license."""
    prefix = f"chess-{version}"
    files: dict[str, bytes] = {}
    seen: set[str] = set()
    with tarfile.open(archive, "r:gz") as source:
        for member in source:
            path = PurePosixPath(member.name)
            tools.require(bool(path.parts) and not path.is_absolute() and ".." not in path.parts and
                          "\\" not in member.name and ":" not in member.name and
                          str(path) == member.name.rstrip("/") and
                          path.parts[0] == prefix, f"unsafe PGN provider archive path: {member.name}")
            tools.require(str(path) not in seen, f"duplicate PGN provider archive member: {member.name}")
            seen.add(str(path))
            tools.require(member.isdir() or member.isfile(),
                          f"PGN provider archive links or special files are forbidden: {member.name}")
            relative = PurePosixPath(*path.parts[1:])
            if member.isdir() or not (str(relative) == "LICENSE.txt" or relative.parts[:1] == ("chess",)):
                continue
            tools.require(member.size <= 4 * 1024 * 1024, "PGN provider runtime member exceeds its finite envelope")
            content = source.extractfile(member)
            tools.require(content is not None, "PGN provider runtime member is unreadable")
            body = content.read()
            tools.require(len(body) == member.size, "PGN provider runtime member is truncated")
            files[str(relative)] = body
    required = {_module_path(name) for name in _MODULES} | {"chess/py.typed", "LICENSE.txt"}
    tools.require(set(files) == required, "PGN provider runtime or license inventory differs from its supported package")
    return files


def _verify_runtime(root: Path, files: dict[str, bytes]) -> None:
    tools.require(root.is_dir() and not root.is_symlink(), "PGN provider runtime must be a physical directory")
    observed = set()
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        tools.require(not path.is_symlink(), f"PGN provider runtime link is forbidden: {relative}")
        if path.is_dir():
            tools.require(relative == "chess", f"extra PGN provider runtime directory: {relative}")
            continue
        tools.require(relative in files and _regular_file(path), f"extra or nonregular PGN provider runtime file: {relative}")
        tools.require(path.read_bytes() == files[relative], f"PGN provider runtime bytes differ: {relative}")
        observed.add(relative)
    tools.require(observed == set(files), "PGN provider runtime inventory is incomplete")


def _prepare_runtime(cache: Path, files: dict[str, bytes]) -> Path:
    root = cache / "runtime"
    if not root.exists() and not root.is_symlink():
        staged = Path(tempfile.mkdtemp(prefix=".runtime-", dir=cache))
        try:
            for relative, body in files.items():
                target = staged / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(body)
            _verify_runtime(staged, files)
            try:
                staged.rename(root)
            except FileExistsError:
                pass  # Another complete publication must pass the same validation.
        finally:
            if staged.exists():
                shutil.rmtree(staged)
    _verify_runtime(root, files)
    return root


def _verify_modules(root: Path, version: str) -> None:
    present = {name: value for name, value in sys.modules.items()
               if name == "chess" or name.startswith("chess.")}
    tools.require(set(present) == set(_MODULES), "PGN provider imported module inventory differs")
    for name, module in present.items():
        origin = str(root / _module_path(name))
        tools.require(module is _loaded.get(name), f"PGN provider module substitution: {name}")
        spec = getattr(module, "__spec__", None)
        tools.require(getattr(module, "__file__", None) == origin and spec is not None and
                      spec.origin == origin, f"PGN provider module origin differs: {name}")
        if name != "chess":
            tools.require(getattr(module, "chess", None) is present["chess"],
                          f"PGN provider absolute import substitution: {name}")
    tools.require(getattr(present["chess"], "__version__", None) == version, "PGN provider version differs from its artifact lock")
    tools.require(getattr(present["chess"], "__path__", None) == [str(root / "chess")], "PGN provider package search path differs")
    for name in _MODULES[1:]:
        tools.require(getattr(present["chess"], name.split(".")[1], None) is present[name],
                      f"PGN provider package module substitution: {name}")


def load_provider(cache: Path, offline: bool = False) -> tuple[ModuleType, ModuleType, dict]:
    """Return the exact chess module, PGN module, and retained provenance receipt."""
    with _lock:
        artifact = tools.json_read(tools.ROOT / "dependencies/artifact-lock.json")["artifacts"]["chess-pgn-validator"]
        tools.require(PurePosixPath(artifact["filename"]).name == artifact["filename"] and
                      "\\" not in artifact["filename"] and ":" not in artifact["filename"],
                      "unsafe PGN provider archive filename")
        cache = Path(cache).absolute()
        tools.require(not cache.is_symlink(), "PGN provider cache must not be a symlink")
        cache.mkdir(parents=True, exist_ok=True)
        cache = cache.resolve(strict=True)
        target = cache / artifact["filename"]
        tools.require(not target.is_symlink() and (not target.exists() or _regular_file(target)),
                      "PGN provider archive must be a physical regular file")
        archive = tools.acquire(artifact, cache, offline)
        tools.verify_artifact(archive, artifact)
        files = _runtime_files(archive, artifact["version"])
        root = _prepare_runtime(cache, files)
        present = {name for name in sys.modules if name == "chess" or name.startswith("chess.")}
        if not present:
            _loaded.clear()
            try:
                for name in _MODULES:
                    path = root / _module_path(name)
                    spec = importlib.util.spec_from_file_location(name, path)
                    tools.require(spec is not None and spec.loader is not None, f"PGN provider module cannot load: {name}")
                    module = importlib.util.module_from_spec(spec)
                    sys.modules[name] = module
                    _loaded[name] = module
                    # Compile the verified archive bytes, never an ambient .pyc.
                    exec(compile(files[_module_path(name)], str(path), "exec"), module.__dict__)
                    if name != "chess":
                        setattr(_loaded["chess"], name.split(".")[1], module)
            except BaseException:
                for name, module in _loaded.items():
                    if sys.modules.get(name) is module:
                        del sys.modules[name]
                _loaded.clear()
                raise
        _verify_modules(root, artifact["version"])
        _verify_runtime(root, files)
        inventory = [{"path": relative, "size": len(body), "sha256": hashlib.sha256(body).hexdigest()}
                     for relative, body in sorted(files.items())]
        modules = {name: {"origin": str(root / _module_path(name)),
                          "sha256": hashlib.sha256(files[_module_path(name)]).hexdigest()} for name in _MODULES}
        receipt = {"schema": "laplace.chess-pgn-provider/v1", "scope": "external benchmark legal-move and game-outcome validation",
                   "artifact_key": "chess-pgn-validator", "version": artifact["version"],
                   "archive": {"path": str(archive), "url": artifact["url"], "size": artifact["size"], "sha256": artifact["sha256"]},
                   "runtime_root": str(root), "files": inventory, "modules": modules,
                   "license": {"path": str(root / "LICENSE.txt"), "sha256": hashlib.sha256(files["LICENSE.txt"]).hexdigest()},
                   "validation": "archive, every runtime file, module origin and version verified"}
        return _loaded["chess"], _loaded["chess.pgn"], receipt
