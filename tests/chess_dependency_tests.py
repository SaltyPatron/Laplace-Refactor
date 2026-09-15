#!/usr/bin/env python3
"""Executable failure controls for upstream chess source and runtime dependencies."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("chess_tools", ROOT / "tools/dependencies/chess_tools.py")
assert spec and spec.loader
TOOLS = importlib.util.module_from_spec(spec)
spec.loader.exec_module(TOOLS)


class ChessDependencies(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.selected, self.artifacts = TOOLS.configuration()
        self.network = self.artifacts[self.selected["releases"]["stockfish"]["network"]]

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def source_fixture(self) -> tuple[Path, dict]:
        source = self.directory / "upstream"
        subprocess.run(["git", "init", "--quiet", str(source)], check=True)
        (source / "license").write_text("upstream fixture license\n")
        (source / "source.cpp").write_text("int main() { return 0; }\n")
        subprocess.run(["git", "-C", str(source), "add", "."], check=True)
        subprocess.run(["git", "-C", str(source), "-c", "user.name=Dependency Test", "-c", "user.email=test@example.invalid", "commit", "--quiet", "-m", "source fixture"], check=True)
        upstream = "https://github.com/official-stockfish/Stockfish.git"
        subprocess.run(["git", "-C", str(source), "remote", "add", "origin", upstream], check=True)
        archive = subprocess.check_output(["git", "-C", str(source), "archive", "--format=tar", "HEAD"])
        return source, {"upstream": upstream, "revision": TOOLS.git(source, "rev-parse", "HEAD"), "git_archive_sha256": hashlib.sha256(archive).hexdigest(), "licenses": [{"path": "license", "sha256": TOOLS.digest(source / "license")}]}

    def engine_fixture(self, *, fail: bool = False, move: str = "e2e4", small: bool = False) -> list[str]:
        engine = self.directory / "protocol_fixture.py"
        engine.write_text("\n".join([
            "import sys",
            "for raw in sys.stdin:",
            "    command = raw.strip()",
            "    if command == 'uci':",
            "        print('id name Stockfish 19')",
            f"        print('option name EvalFile type string default {self.network['filename']}')",
            "        for option in ['Threads', 'Hash', 'SyzygyPath', 'UCI_LimitStrength']:",
            "            print('option name ' + option + ' type string')",
            f"        if {small!r}: print('option name EvalFileSmall type string')",
            "        print('uciok', flush=True)",
            "    elif command == 'isready': print('readyok', flush=True)",
            "    elif command.startswith('go '):",
            f"        if {fail!r}: sys.exit(3)",
            "        print('info depth 6 score cp 1 nodes 15')",
            f"        print('bestmove {move}', flush=True)",
            "    elif command == 'quit': break",
        ]) + "\n")
        return [sys.executable, str(engine)]

    def test_release_source_and_single_network_agree(self) -> None:
        self.assertEqual(self.selected["releases"]["stockfish"]["tag"], "sf_19")
        self.assertNotIn("stockfish-nnue-small", self.artifacts)
        self.assertEqual(self.network["filename"], "nn-1a298aa575a0.nnue")

    def test_exact_source_passes_and_changed_archive_fails(self) -> None:
        source, entry = self.source_fixture()
        TOOLS.update_source(source, entry, True)
        entry["git_archive_sha256"] = "0" * 64
        with self.assertRaisesRegex(TOOLS.ChessToolError, "source archive differs"):
            TOOLS.verify_source(source, entry)

    def test_source_edits_are_preserved_before_update(self) -> None:
        source, entry = self.source_fixture()
        original_head = TOOLS.git(source, "rev-parse", "HEAD")
        (source / "source.cpp").write_text("operator work\n")
        with self.assertRaisesRegex(TOOLS.ChessToolError, "local changes; preserving"):
            TOOLS.update_source(source, entry, False)
        self.assertEqual((source / "source.cpp").read_text(), "operator work\n")
        self.assertEqual(TOOLS.git(source, "rev-parse", "HEAD"), original_head)

    def test_unrelated_git_origin_is_rejected(self) -> None:
        source, entry = self.source_fixture()
        entry["upstream"] = "https://example.invalid/unrelated.git"
        with self.assertRaisesRegex(TOOLS.ChessToolError, "unrelated source origin"):
            TOOLS.update_source(source, entry, False)

    def test_verified_import_with_local_origin_remains_usable(self) -> None:
        source, entry = self.source_fixture()
        TOOLS.git(source, "remote", "set-url", "origin", "/independent/upstream-assets/stockfish")
        TOOLS.update_source(source, entry, True)

    @unittest.skipUnless(shutil.which("jq") and shutil.which("flock"), "shell dependency helpers require jq and flock")
    def test_missing_generation_is_acquired_and_dirty_generation_is_preserved(self) -> None:
        source, entry = self.source_fixture()
        entry["upstream"] = str(source)
        repository = self.directory / "repository"
        helpers = repository / "tools/dependencies"
        helpers.mkdir(parents=True)
        (repository / "dependencies").mkdir()
        (repository / "dependencies/lock.json").write_text(json.dumps({"dependencies": {"fixture": entry}}))
        for name in ("ensure-locked-git.sh", "acquire-locked-git.sh", "verify-lock.sh"):
            shutil.copy2(ROOT / "tools/dependencies" / name, helpers / name)
        destination = self.directory / "generation"
        command = ["bash", str(helpers / "ensure-locked-git.sh"), str(destination)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        commands = self.directory / "commands"
        commands.mkdir()
        (commands / "flock").write_text("#!/bin/sh\nexit 99\n")
        (commands / "flock").chmod(0o755)
        subprocess.run(command, check=True, capture_output=True, text=True,
                       env={**os.environ, "PATH": f"{commands}:{os.environ['PATH']}"})
        (destination / "fixture/source.cpp").write_text("preserve operator changes\n")
        failure = subprocess.run(command, check=False, capture_output=True, text=True)
        self.assertNotEqual(failure.returncode, 0)
        self.assertIn("source contains changes", failure.stderr)
        self.assertEqual((destination / "fixture/source.cpp").read_text(), "preserve operator changes\n")

    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires POSIX source-parent permissions")
    def test_source_parent_and_lock_repair_preserve_uid_inode_and_bytes(self) -> None:
        parent = self.directory / "source-generations"
        parent.mkdir(mode=0o700)
        lock = parent / ".source-acquisition.lock"
        lock.write_text("preserved lock bytes\n")
        lock.chmod(0o600)
        previous_parent = parent.stat()
        previous_lock = lock.stat()
        group = subprocess.check_output(["id", "-gn"], text=True).strip()
        script = ROOT / "tools/dependencies/prepare-source-parents.sh"
        command = ['bash', '-c', 'source "$1"; repair_source_parent "$2" "$3"; repair_source_lock "$2/.source-acquisition.lock" "$3"', 'test', str(script), str(parent), group]
        subprocess.run(command, check=True, capture_output=True, text=True)
        self.assertEqual(parent.stat().st_uid, previous_parent.st_uid)
        self.assertEqual((lock.stat().st_ino, lock.stat().st_uid), (previous_lock.st_ino, previous_lock.st_uid))
        self.assertEqual(parent.stat().st_mode & 0o2070, 0o2070)
        self.assertEqual(lock.stat().st_mode & 0o060, 0o060)
        self.assertEqual(lock.read_text(), "preserved lock bytes\n")
        # A compliant shared parent must not need another privileged mutation.
        subprocess.run(['bash', '-c', 'source "$1"; chmod() { return 99; }; chgrp() { return 99; }; sudo() { return 99; }; repair_source_parent "$2" "$3"; repair_source_lock "$2/.source-acquisition.lock" "$3"', 'test', str(script), str(parent), group], check=True, capture_output=True, text=True)

    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires POSIX source-parent permissions")
    def test_source_parent_repair_refuses_symlink(self) -> None:
        target = self.directory / "operator"
        target.mkdir(mode=0o700)
        link = self.directory / "alias"
        link.symlink_to(target, target_is_directory=True)
        script = ROOT / "tools/dependencies/prepare-source-parents.sh"
        result = subprocess.run(['bash', '-c', 'source "$1"; repair_source_parent "$2" "$3"', 'test', str(script), str(link), subprocess.check_output(["id", "-gn"], text=True).strip()], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("physical source-cache", result.stderr)
        self.assertEqual(target.stat().st_mode & 0o777, 0o700)

    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires POSIX source-parent permissions")
    def test_configured_source_alias_is_preserved_while_physical_parents_are_repaired(self) -> None:
        target = self.directory / "operator-estate"
        target.mkdir(mode=0o700)
        generations = target / "source-generations"
        generations.mkdir(mode=0o700)
        lock = generations / ".source-acquisition.lock"
        lock.write_text("preserve acquisition lock")
        lock.chmod(0o600)
        link = self.directory / "configured-estate"
        link.symlink_to(target, target_is_directory=True)
        before = {path: path.lstat() for path in (target, generations, lock, link)}
        group = subprocess.check_output(["id", "-gn"], text=True).strip()
        command = ['bash', '-c', 'source "$1"; prepare_source_cache "$2" "$3"', 'test', str(ROOT / "tools/dependencies/prepare-source-parents.sh"), str(link), group]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        self.assertIn(f"{link} -> {target}", result.stderr)
        for path, previous in before.items():
            self.assertEqual((path.lstat().st_ino, path.lstat().st_uid), (previous.st_ino, previous.st_uid))
        self.assertTrue(link.is_symlink())
        self.assertEqual(link.resolve(), target)
        self.assertEqual(generations.stat().st_mode & 0o2070, 0o2070)
        self.assertEqual(lock.read_text(), "preserve acquisition lock")
        # Only the explicitly configured estate alias may resolve a symlink.
        lock.unlink()
        lock.symlink_to(target / "operator-file")
        (target / "operator-file").write_text("untouched")
        rejected = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual((target / "operator-file").read_text(), "untouched")

    def test_cached_artifact_corruption_is_rejected(self) -> None:
        artifact = {"filename": "network", "size": 4, "sha256": hashlib.sha256(b"good").hexdigest()}
        (self.directory / "network").write_bytes(b"evil")
        with self.assertRaisesRegex(TOOLS.ChessToolError, "SHA-256 mismatch"):
            TOOLS.acquire(artifact, self.directory, True)

    def test_uci_search_waits_for_actual_bestmove(self) -> None:
        result = TOOLS.probe_stockfish(self.engine_fixture(), self.network)
        self.assertEqual((result["depth"], result["bestmove"]), (6, "e2e4"))

    def test_engine_crash_cannot_pass_readback(self) -> None:
        with self.assertRaisesRegex(TOOLS.ChessToolError, "exited before bestmove"):
            TOOLS.probe_stockfish(self.engine_fixture(fail=True), self.network)

    def test_invalid_move_cannot_pass_readback(self) -> None:
        with self.assertRaisesRegex(TOOLS.ChessToolError, "invalid initial move"):
            TOOLS.probe_stockfish(self.engine_fixture(move="e2e5"), self.network)

    def test_obsolete_secondary_network_is_rejected(self) -> None:
        with self.assertRaisesRegex(TOOLS.ChessToolError, "secondary NNUE"):
            TOOLS.probe_stockfish(self.engine_fixture(small=True), self.network)

    def test_qt_too_old_is_reported(self) -> None:
        with patch.object(TOOLS, "execute", return_value="cutechess-cli 1.5.1\nUsing Qt version 6.4.2\n"):
            with self.assertRaisesRegex(TOOLS.ChessToolError, "Qt >=6.8"):
                TOOLS.probe_cutechess(["fixture"])

    def test_tablebase_bytes_do_not_claim_probe_capability(self) -> None:
        files = []
        for suffix in (".rtbw", ".rtbz"):
            path = self.directory / ("KQvK" + suffix)
            path.write_bytes(b"fixture bytes")
            files.append({"path": path.name, "size": path.stat().st_size, "sha256": TOOLS.digest(path)})
        manifest = self.directory / "manifest.json"
        manifest.write_text(json.dumps({"root": str(self.directory), "coverage": "fixture only", "files": files}))
        self.assertEqual(TOOLS.tablebase_readback(manifest)["disposition"], "manifest-bytes-verified-probe-not-implemented")
        manifest.write_text(json.dumps({"root": str(self.directory), "coverage": "fixture only", "files": files[:1]}))
        with self.assertRaisesRegex(TOOLS.ChessToolError, "missing WDL or DTZ"):
            TOOLS.tablebase_readback(manifest)

    def test_missing_token_does_not_claim_lichess_ready(self) -> None:
        with patch.dict(os.environ, {}, clear=True):
            result = TOOLS.lichess_readback(True)
        self.assertFalse(result["credentials_present"])
        self.assertEqual(result["disposition"], "not-verified")

    def test_options_after_action_are_parsed(self) -> None:
        stream = io.StringIO()
        with patch.object(sys, "argv", ["chess_tools.py", "check", "--prefix", str(self.directory)]), contextlib.redirect_stdout(stream):
            self.assertEqual(TOOLS.main(), 1)
        self.assertIn(str(self.directory / "current.json"), json.loads(stream.getvalue())["error"])


if __name__ == "__main__":
    unittest.main()
