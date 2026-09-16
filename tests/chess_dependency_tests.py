#!/usr/bin/env python3
"""Executable failure controls for upstream chess source and runtime dependencies."""

from __future__ import annotations

import contextlib
import argparse
import hashlib
import importlib.util
import io
import json
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("chess_tools", ROOT / "tools/dependencies/chess_tools.py")
assert spec and spec.loader
TOOLS = importlib.util.module_from_spec(spec)
spec.loader.exec_module(TOOLS)


source_spec = importlib.util.spec_from_file_location("source_estate", ROOT / "tools/dependencies/source_estate.py")
ESTATE = importlib.util.module_from_spec(source_spec)
source_spec.loader.exec_module(ESTATE)

sys.path.insert(0, str(ROOT / "tools/dependencies"))
pgn_spec = importlib.util.spec_from_file_location("chess_pgn", ROOT / "tools/dependencies/chess_pgn.py")
assert pgn_spec and pgn_spec.loader
PGN = importlib.util.module_from_spec(pgn_spec)
pgn_spec.loader.exec_module(PGN)


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
        (source / "include").mkdir()
        (source / "include/source.h").write_text("#define FIXTURE_VALUE 1\n")
        (source / "empty").write_bytes(b"")
        (source / "binary.bin").write_bytes(b"\x00\xff\x01")
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
        source = self.directory / "sources/stockfish"
        (source / "src").mkdir(parents=True)
        (source / "src/evaluate.h").write_text(f'#define EvalFileDefaultName "{self.network["filename"]}"\n')
        downloaded = self.directory / "network-fixture"
        downloaded.write_bytes(b"fixture network; never loaded")
        entry = {"revision": "fixture", "git_archive_sha256": "0" * 64}
        arguments = argparse.Namespace(tool="stockfish", source_root=source.parent,
            build_root=self.directory / "build", prefix=self.directory / "receipt",
            cache=self.directory / "cache", offline=True, jobs=1, profile=True,
            arch="x86-64", compiler="gcc")
        commands = []
        def make_command(command, **kwargs):
            self.assertEqual("make", command[0])
            self.assertEqual("1", kwargs["env"]["GIT_NO_REPLACE_OBJECTS"])
            commands.append(command)
            if "profile-build" in command:
                filename = "stockfish.exe" if platform.system() == "Windows" else "stockfish"
                (source / "src" / filename).write_bytes(b"fixture executable; never run")
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
        with patch.dict(os.environ, {"GIT_NO_REPLACE_OBJECTS": "0"}), \
             patch.object(TOOLS, "json_read", return_value={"dependencies": {"stockfish": entry}}), \
             patch.object(TOOLS, "update_source"), \
             patch.object(TOOLS, "verify_source", return_value={"fixture": True}), \
             patch.object(TOOLS, "acquire", return_value=downloaded), \
             patch.object(TOOLS, "probe_stockfish", return_value={"fixture": True}), \
             patch.object(TOOLS.subprocess, "run", side_effect=make_command):
            TOOLS.build_tools(arguments, self.selected, self.artifacts)
            self.assertEqual("0", os.environ["GIT_NO_REPLACE_OBJECTS"])
        self.assertEqual(2, len(commands))
        self.assertEqual("clean", commands[0][-1])
        self.assertIn("profile-build", commands[1])

    def test_exact_source_passes_and_changed_archive_fails(self) -> None:
        source, entry = self.source_fixture()
        TOOLS.update_source(source, entry, True)
        entry["git_archive_sha256"] = "0" * 64
        with self.assertRaisesRegex(TOOLS.ChessToolError, "source archive differs"):
            TOOLS.verify_source(source, entry)

    def test_explicit_checkout_build_keeps_operator_path_and_receipt(self) -> None:
        source, entry = self.source_fixture()
        selected_path = self.directory / "External/Stockfish/SF_19 with spaces"
        selected_path.parent.mkdir(parents=True)
        source.rename(selected_path)
        source = selected_path
        (source / "src").mkdir()
        (source / "src/evaluate.h").write_text(f'#define EvalFileDefaultName "{self.network["filename"]}"\n')
        (source / ".gitignore").write_text("src/stockfish\nsrc/stockfish.exe\nsrc/*.nnue\n")
        TOOLS.git(source, "add", ".")
        TOOLS.git(source, "-c", "user.name=Dependency Test", "-c", "user.email=test@example.invalid", "commit", "--quiet", "-m", "build shape")
        entry["revision"] = TOOLS.git(source, "rev-parse", "HEAD")
        entry["git_archive_sha256"] = hashlib.sha256(subprocess.check_output(["git", "-C", str(source), "archive", "--format=tar", "HEAD"])).hexdigest()
        arguments = argparse.Namespace(tool="stockfish", stockfish_source=source,
            source_root=self.directory / "unrelated estate", build_root=self.directory / "build",
            prefix=self.directory / "receipt", cache=self.directory / "cache", offline=True,
            jobs=2, profile=True, arch="native", compiler="gcc")
        network = self.directory / "network-fixture"
        network.write_bytes(b"command transport fixture; not an evaluated NNUE")
        executable = source / "src" / ("stockfish.exe" if platform.system() == "Windows" else "stockfish")
        real_run, real_json = subprocess.run, TOOLS.json_read
        commands, probes = [], []
        def run(command, **kwargs):
            if command[0] != "make":
                return real_run(command, **kwargs)
            commands.append(command)
            self.assertEqual(str(source / "src"), command[2])
            if "profile-build" in command:
                executable.write_bytes(b"command transport fixture; never executed")
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
        def read(path):
            return {"dependencies": {"stockfish": entry}} if path == ROOT / "dependencies/lock.json" else real_json(path)
        def probe(argv, selected_network):
            probes.append(argv)
            self.assertEqual(self.network, selected_network)
            return {"scope": "test transport only"}
        with patch.object(TOOLS, "json_read", side_effect=read), \
             patch.object(TOOLS, "acquire", return_value=network), \
             patch.object(TOOLS, "probe_stockfish", side_effect=probe), \
             patch.object(TOOLS.subprocess, "run", side_effect=run):
            receipt = TOOLS.build_tools(arguments, self.selected, self.artifacts)
        self.assertEqual(["clean", "profile-build"], ["clean" if "clean" in c else "profile-build" for c in commands])
        self.assertEqual([[str(executable)]], probes)
        self.assertFalse(arguments.source_root.exists())
        tool = receipt["tools"]["stockfish"]
        self.assertEqual(str(source), tool["source"])
        self.assertEqual(str(executable), tool["executable"])
        self.assertEqual(entry["revision"], tool["tracked_source"]["commit"])
        self.assertEqual(TOOLS.digest(executable), tool["sha256"])
        self.assertEqual(receipt, json.loads((arguments.prefix / "current.json").read_text()))

    def test_explicit_checkout_refuses_missing_or_non_git_without_creating_it(self) -> None:
        missing = self.directory / "SF_19"
        args = argparse.Namespace(stockfish_source=missing, source_root=self.directory / "estate")
        with self.assertRaises(FileNotFoundError):
            TOOLS.tool_source(args, "stockfish")
        self.assertFalse(missing.exists())
        missing.mkdir()
        with self.assertRaisesRegex(TOOLS.ChessToolError, "existing Git checkout"):
            TOOLS.tool_source(args, "stockfish")
        self.assertEqual([], list(missing.iterdir()))

    def test_explicit_checkout_preserves_dirty_source_and_rejects_unrelated_origin(self) -> None:
        source, entry = self.source_fixture()
        args = argparse.Namespace(stockfish_source=source, source_root=None)
        selected = TOOLS.tool_source(args, "stockfish")
        (source / "source.cpp").write_text("operator changes must survive\n")
        with self.assertRaisesRegex(TOOLS.ChessToolError, "preserving"):
            TOOLS.update_source(selected, entry, True)
        self.assertEqual("operator changes must survive\n", (source / "source.cpp").read_text())
        TOOLS.git(source, "checkout", "--", "source.cpp")
        TOOLS.git(source, "remote", "set-url", "origin", "https://example.invalid/unrelated.git")
        with self.assertRaisesRegex(TOOLS.ChessToolError, "unrelated source origin"):
            TOOLS.update_source(selected, entry, True)
        self.assertEqual(entry["revision"], TOOLS.git(source, "rev-parse", "HEAD"))

    def test_explicit_stockfish_selection_does_not_redirect_cutechess(self) -> None:
        args = argparse.Namespace(stockfish_source=self.directory / "missing custom SF_19",
                                  source_root=self.directory / "estate")
        self.assertEqual(args.source_root / "cutechess", TOOLS.tool_source(args, "cutechess"))

    def test_cli_checkout_precedes_environment_without_unrelated_estate_selection(self) -> None:
        for cli in ([], ["--stockfish-source", str(self.directory / "command SF_19")]):
            with self.subTest(cli=cli), \
                 patch.dict(os.environ, {"LAPLACE_STOCKFISH_SOURCE": str(self.directory / "environment SF_19"), "LAPLACE_VERIFIED_SOURCE_ROOT": ""}), \
                 patch.object(sys, "argv", ["chess_tools.py", "install", "--offline", "--tool", "stockfish", *cli]), \
                 patch.object(TOOLS.subprocess, "check_output", side_effect=AssertionError("unrelated estate must not be selected")), \
                 patch.object(TOOLS, "build_tools", return_value={}) as build, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, TOOLS.main())
                args = build.call_args.args[0]
                self.assertEqual(Path(cli[1]) if cli else self.directory / "environment SF_19", args.stockfish_source)
                self.assertIsNone(args.source_root)

    def test_tracked_snapshot_retains_binary_empty_and_exact_git_identities(self) -> None:
        source, entry = self.source_fixture()
        observed, files = TOOLS.git_snapshot(source, entry["revision"], retain_bytes=True)
        self.assertEqual(5, observed["file_count"])
        self.assertEqual(sum(len(data) for data in files.values()), observed["byte_count"])
        self.assertEqual(b"", files["empty"])
        self.assertEqual(b"\x00\xff\x01", files["binary.bin"])
        self.assertEqual(entry["revision"], observed["commit"])
        self.assertEqual(TOOLS.git(source, "rev-parse", "HEAD^{tree}"), observed["tree"])
        for artifact in observed["artifacts"]:
            self.assertEqual(hashlib.sha256(files[artifact["path"]]).hexdigest(), artifact["sha256"])
            self.assertEqual(TOOLS.git(source, "rev-parse", "HEAD:" + artifact["path"]), artifact["git_blob"])

    def test_snapshot_stops_when_a_file_grows_past_its_observed_bound(self) -> None:
        source, entry = self.source_fixture()
        path = source / "source.cpp"
        identity = (path.stat().st_dev, path.stat().st_ino)
        verifier = sys.modules[TOOLS.git_snapshot.__module__]
        original_fstat = os.fstat
        changed = False
        def grow_after_stat(fd):
            nonlocal changed
            before = original_fstat(fd)
            if not changed and (before.st_dev, before.st_ino) == identity:
                changed = True
                with path.open("ab") as stream:
                    stream.write(b"x" * (1024 * 1024))
            return before
        with patch.object(verifier.os, "fstat", side_effect=grow_after_stat):
            with self.assertRaisesRegex(TOOLS.GitCheckoutError, "grew beyond"):
                TOOLS.git_snapshot(source, entry["revision"], retain_bytes=True, maximum_file_bytes=1024)
        self.assertTrue(changed)

    @unittest.skipUnless(os.name == "posix", "requires POSIX file kinds")
    def test_tracked_symlink_and_fifo_replacements_reject_without_following(self) -> None:
        source, entry = self.source_fixture()
        path = source / "source.cpp"
        original = path.read_bytes()
        TOOLS.git(source, "update-index", "--assume-unchanged", "source.cpp")
        for kind in ("symlink", "fifo"):
            with self.subTest(kind=kind):
                path.unlink()
                if kind == "symlink":
                    path.symlink_to("license")
                else:
                    os.mkfifo(path)
                with self.assertRaises(TOOLS.ChessToolError):
                    TOOLS.verify_source(source, entry)
                path.unlink()
                path.write_bytes(original)

    def test_local_replacement_commit_does_not_redefine_locked_input(self) -> None:
        source, entry = self.source_fixture()
        original = (source / "source.cpp").read_bytes()
        (source / "source.cpp").write_bytes(b"replacement source bytes\n")
        TOOLS.git(source, "add", "source.cpp")
        TOOLS.git(source, "-c", "user.name=Dependency Test", "-c", "user.email=test@example.invalid", "commit", "--quiet", "-m", "replacement fixture")
        replacement = TOOLS.git(source, "rev-parse", "HEAD")
        TOOLS.git(source, "update-ref", "HEAD", entry["revision"])
        TOOLS.git(source, "replace", entry["revision"], replacement)
        TOOLS.git(source, "update-index", "--assume-unchanged", "source.cpp")
        with self.assertRaises(TOOLS.ChessToolError):
            TOOLS.verify_source(source, entry)
        # The actual original bytes still verify even while the local replacement
        # exists; it has no authority over the selected locked commit or archive.
        (source / "source.cpp").write_bytes(original)
        TOOLS.git(source, "reset", "--mixed", "HEAD")
        observed = TOOLS.verify_source(source, entry)
        self.assertEqual(entry["revision"], observed["commit"])

    def test_source_edits_are_preserved_before_update(self) -> None:
        source, entry = self.source_fixture()
        original_head = TOOLS.git(source, "rev-parse", "HEAD")
        (source / "source.cpp").write_text("operator work\n")
        with self.assertRaisesRegex(TOOLS.ChessToolError, "local changes; preserving"):
            TOOLS.update_source(source, entry, False)
        self.assertEqual((source / "source.cpp").read_text(), "operator work\n")
        self.assertEqual(TOOLS.git(source, "rev-parse", "HEAD"), original_head)

    def test_index_flags_cannot_hide_changed_compiler_inputs(self) -> None:
        source, entry = self.source_fixture()
        for flag, relative in (("--assume-unchanged", "source.cpp"), ("--skip-worktree", "include/source.h")):
            with self.subTest(flag=flag, relative=relative):
                path = source / relative
                original = path.read_bytes()
                TOOLS.git(source, "update-index", flag, "--", relative)
                path.write_bytes(b"hidden operator changes\n")
                self.assertEqual("", TOOLS.git(source, "status", "--porcelain=v1", "--untracked-files=all"))
                with self.assertRaises(TOOLS.ChessToolError):
                    TOOLS.verify_source(source, entry)
                with self.assertRaises(TOOLS.ChessToolError):
                    TOOLS.update_source(source, entry, True)
                self.assertEqual(b"hidden operator changes\n", path.read_bytes())
                self.assertEqual(entry["revision"], TOOLS.git(source, "rev-parse", "HEAD"))
                path.write_bytes(original)
                TOOLS.git(source, "update-index", "--no-" + flag[2:], "--", relative)

    @unittest.skipUnless(os.name == "posix", "requires POSIX executable mode")
    def test_filemode_configuration_cannot_hide_changed_executable_mode(self) -> None:
        source, entry = self.source_fixture()
        TOOLS.git(source, "config", "core.fileMode", "false")
        path = source / "source.cpp"
        path.chmod(path.stat().st_mode | 0o100)
        self.assertEqual("", TOOLS.git(source, "status", "--porcelain=v1", "--untracked-files=all"))
        with self.assertRaises(TOOLS.ChessToolError):
            TOOLS.verify_source(source, entry)
        with self.assertRaises(TOOLS.ChessToolError):
            TOOLS.update_source(source, entry, True)
        self.assertTrue(path.stat().st_mode & 0o100)

    def test_changed_header_during_build_cannot_publish_official_receipt(self) -> None:
        source, entry = self.source_fixture()
        selected_source = self.directory / "sources/cutechess"
        selected_source.parent.mkdir()
        source.rename(selected_source)
        source = selected_source
        work = self.directory / "build/cutechess" / entry["revision"]
        qt = self.directory / "qt"
        qt.mkdir()
        arguments = argparse.Namespace(tool="cutechess", source_root=source.parent,
            build_root=self.directory / "build", prefix=self.directory / "receipt",
            offline=True, qt_prefix=qt, jobs=1)
        original_run = subprocess.run
        def build_command(command, **kwargs):
            if command[0] == "cmake":
                self.assertEqual("1", kwargs["env"]["GIT_NO_REPLACE_OBJECTS"])
                if "--build" in command:
                    self.assertIn("--clean-first", command)
                    (work / "cutechess-cli").write_bytes(b"fixture executable; never run")
                    TOOLS.git(source, "update-index", "--assume-unchanged", "include/source.h")
                    (source / "include/source.h").write_text("hidden build-time mutation\n")
                return subprocess.CompletedProcess(command, 0)
            return original_run(command, **kwargs)
        with patch.dict(os.environ, {"GIT_NO_REPLACE_OBJECTS": "0"}), \
             patch.object(TOOLS, "json_read", return_value={"dependencies": {"cutechess": entry}}), \
             patch.object(TOOLS, "probe_cutechess", return_value={"fixture": True}), \
             patch.object(TOOLS.subprocess, "run", side_effect=build_command):
            with self.assertRaises(TOOLS.ChessToolError):
                TOOLS.build_tools(arguments, self.selected, self.artifacts)
            self.assertEqual("0", os.environ["GIT_NO_REPLACE_OBJECTS"])
        self.assertFalse((arguments.prefix / "current.json").exists())
        self.assertEqual("hidden build-time mutation\n", (source / "include/source.h").read_text())

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

    def test_runner_source_parent_is_persisted_inside_the_configured_estate(self) -> None:
        external = self.directory / "physical-external"
        external.mkdir()
        alias = self.directory / "configured-external"
        alias.symlink_to(external, target_is_directory=True)
        legacy = external / "source-generations"
        legacy.mkdir(mode=0o755)
        lock = legacy / ".source-acquisition.lock"
        lock.write_text("operator lock")
        original = {p: p.lstat() for p in (alias, legacy, lock)}
        selection = ESTATE.select_generation(ROOT / "dependencies/lock.json", alias)
        self.assertEqual(selection.parent, external / "refactor-source-generations")
        self.assertEqual(selection.name, TOOLS.digest(ROOT / "dependencies/lock.json"))
        receipt = external / "refactor-source-selection.json"
        raw = receipt.read_bytes()
        for p, metadata in original.items():
            self.assertEqual((p.lstat().st_ino, p.lstat().st_mode, p.lstat().st_uid), (metadata.st_ino, metadata.st_mode, metadata.st_uid))
        self.assertEqual(lock.read_text(), "operator lock")
        legacy.chmod(0o2775)
        self.assertEqual(ESTATE.select_generation(ROOT / "dependencies/lock.json", alias), selection)
        self.assertEqual(receipt.read_bytes(), raw)
        document = json.loads(raw)
        document["generation_parent"] = str(self.directory / "unrelated")
        receipt.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, "does not match"):
            ESTATE.select_generation(ROOT / "dependencies/lock.json", alias)

    def test_writable_shared_source_parent_remains_selected(self) -> None:
        external = self.directory / "external"
        external.mkdir()
        parent = external / "source-generations"
        parent.mkdir(mode=0o2775)
        parent.chmod(0o2775)
        self.assertEqual(ESTATE.select_generation(ROOT / "dependencies/lock.json", external).parent, parent)
        self.assertFalse((external / "refactor-source-generations").exists())

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

    @unittest.skipUnless(platform.system() == "Linux" and shutil.which("cc"), "requires the Linux ELF loader and C compiler")
    def test_selected_sdk_precedes_conflicting_inherited_elf_library(self) -> None:
        sdk = self.directory / "selected-sdk"
        selected = sdk / "lib"
        conflicting = self.directory / "other-sdk"
        selected.mkdir(parents=True)
        conflicting.mkdir()
        source = self.directory / "library.c"
        for directory, value in ((selected, 611), (conflicting, 602)):
            source.write_text(f"int selected_sdk(void) {{ return {value}; }}\n")
            subprocess.run(["cc", "-shared", "-fPIC", str(source), "-Wl,-soname,libselection.so", "-o", str(directory / "libselection.so")], check=True, capture_output=True)
        source.write_text("#include <stdio.h>\nint selected_sdk(void);\nint main(void) { printf(\"%d\\n\", selected_sdk()); }\n")
        program = self.directory / "sdk-tool"
        subprocess.run(["cc", str(source), "-L", str(selected), "-lselection", "-Wl,--enable-new-dtags", f"-Wl,-rpath,{selected}", "-o", str(program)], check=True, capture_output=True)
        with patch.dict(os.environ, {"LD_LIBRARY_PATH": str(conflicting)}):
            before = os.environ.copy()
            # Deliberate break: ambient loader precedence defeats executable RUNPATH.
            self.assertEqual(TOOLS.execute([str(program)]).strip(), "602")
            environment = TOOLS.tool_environment({"qt_prefix": str(sdk)})
            self.assertEqual(TOOLS.execute([str(program)], env=environment).strip(), "611")
            self.assertEqual(os.environ.copy(), before)
            self.assertEqual(environment["LD_LIBRARY_PATH"], os.pathsep.join((str(selected), str(conflicting))))

    def test_cutechess_probe_uses_selected_environment_for_version_and_help(self) -> None:
        environment = {"PATH": "selected-sdk"}
        with patch.object(TOOLS, "execute", side_effect=["cutechess-cli 1.5.1\nUsing Qt version 6.11.2\n", "-engine -pgnout -repeat -openings"]) as execute:
            TOOLS.probe_cutechess(["selected-cli"], environment=environment)
        self.assertEqual([call.kwargs["env"] for call in execute.call_args_list], [environment, environment])

    def test_sdk_pip_bootstrap_does_not_require_unsupported_report_option(self) -> None:
        _, artifacts = TOOLS.configuration()
        artifact = artifacts["pip"]
        wheel = self.directory / artifact["filename"]
        python = self.directory / "sdk-venv/bin/python"
        commands = []

        def old_then_selected_pip(command, **kwargs):
            commands.append(command)
            if "install" in command:
                self.assertNotIn("--report", command)
                self.assertIn("--no-index", command)
                self.assertIn("--no-deps", command)
                self.assertEqual(command[-1], str(wheel))
                return "Successfully installed selected pip"
            return f"pip {artifact['version']} from {python.parent}/site-packages/pip (python 3.10)"

        with patch.object(TOOLS, "acquire", return_value=wheel) as acquire, patch.object(TOOLS, "execute", side_effect=old_then_selected_pip):
            TOOLS.bootstrap_pip(python, self.directory, self.directory / "cache", artifact)
        acquire.assert_called_once_with(artifact, self.directory / "cache", False)
        self.assertEqual(len(commands), 2)
        receipt = json.loads((self.directory / "pip-bootstrap.json").read_text())
        self.assertEqual(receipt["artifact_sha256"], artifact["sha256"])
        self.assertEqual(receipt["python"], str(python))

    def test_sdk_pip_wrong_installed_version_cannot_publish_bootstrap(self) -> None:
        _, artifacts = TOOLS.configuration()
        with patch.object(TOOLS, "acquire", return_value=self.directory / "pip.whl"), patch.object(TOOLS, "execute", return_value="pip 22.0.2 from old pip"):
            with self.assertRaisesRegex(TOOLS.ChessToolError, "version differs"):
                TOOLS.bootstrap_pip(self.directory / "python", self.directory, self.directory, artifacts["pip"])
        self.assertFalse((self.directory / "pip-bootstrap.json").exists())

    def test_failed_source_build_retains_bounded_diagnostic_tail(self) -> None:
        log = self.directory / "build.log"
        log.write_text("early lines\n" * 10000 + "actual compiler failure\n")
        with self.assertRaises(TOOLS.ChessToolError) as caught:
            TOOLS.require_build(False, "source build failed", log)
        self.assertIn("actual compiler failure", str(caught.exception))
        self.assertLess(len(str(caught.exception)), 8500)

    def test_qt_retry_preserves_original_and_each_aqt_acquisition_report(self) -> None:
        original = self.directory / "aqt-acquisition.json"
        original.write_text('{"install":[{"download_info":{"url":"original-wheel"}}]}\n')
        original_bytes = original.read_bytes()
        reports = [{"install": [{"download_info": {"url": "first-wheel"}}]}, {"install": []}]

        def install(command, **kwargs):
            report = Path(command[command.index("--report") + 1])
            self.assertFalse(report.exists())
            report.write_text(json.dumps(reports.pop(0)))
            return "installed"

        with patch.object(TOOLS, "execute", side_effect=install):
            first = TOOLS.install_aqt(self.directory / "python", self.directory, self.directory / "aqt.whl")
            first_bytes = first.read_bytes()
            second = TOOLS.install_aqt(self.directory / "python", self.directory, self.directory / "aqt.whl")
        self.assertNotEqual(first, second)
        self.assertEqual(original.read_bytes(), original_bytes)
        self.assertEqual(first.read_bytes(), first_bytes)
        self.assertEqual(json.loads(second.read_text()), {"install": []})

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


class ChessPgnProvider(unittest.TestCase):
    """Real pinned rules execution, with independent archive/import refusals."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.artifact = PGN.tools.json_read(ROOT / "dependencies/artifact-lock.json")["artifacts"]["chess-pgn-validator"]
        cache = Path(os.environ.get("LAPLACE_CHESS_PGN_TEST_CACHE", "/build/laplace/work/chess-pgn-provider-test-cache"))
        cls.archive = PGN.tools.acquire(cls.artifact, cache, os.environ.get("LAPLACE_CHESS_PGN_OFFLINE") == "1")

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.cache = self.directory / "provider"
        self.cache.mkdir()
        shutil.copyfile(self.archive, self.cache / self.artifact["filename"])
        self.previous = {key: value for key, value in sys.modules.items()
                         if key == "chess" or key.startswith("chess.")}
        for key in self.previous:
            del sys.modules[key]

    def tearDown(self) -> None:
        for key in list(sys.modules):
            if key == "chess" or key.startswith("chess."):
                del sys.modules[key]
        sys.modules.update(self.previous)
        self.temporary.cleanup()

    def load(self):
        return PGN.load_provider(self.cache, offline=True)

    def test_real_rules_parse_legal_checkmate_and_refuse_illegal_san(self) -> None:
        chess, pgn, receipt = self.load()
        game = pgn.read_game(io.StringIO('[Result "0-1"]\n\n1. f3 e5 2. g4 Qh4# 0-1\n'))
        self.assertEqual(game.errors, [])
        board = game.end().board()
        self.assertEqual(board.outcome().termination, chess.Termination.CHECKMATE)
        self.assertEqual(board.result(), "0-1")
        self.assertEqual(game.end().ply(), 4)
        with self.assertLogs("chess.pgn", level="ERROR"):
            illegal = pgn.read_game(io.StringIO('[Result "0-1"]\n\n1. e5 0-1\n'))
        self.assertTrue(illegal.errors)
        self.assertIsNone(illegal.end().board().outcome())
        self.assertEqual(receipt["archive"]["sha256"], self.artifact["sha256"])
        self.assertEqual(receipt["version"], "1.11.2")
        self.assertEqual(len(receipt["files"]), 10)
        self.assertEqual(len(receipt["modules"]), 8)
        self.assertTrue(Path(receipt["license"]["path"]).read_text().startswith("                    GNU GENERAL PUBLIC LICENSE"))
        self.assertFalse(list(self.cache.rglob("*.pyc")))

    def test_exact_cache_and_modules_reuse_without_download(self) -> None:
        chess, pgn, receipt = self.load()
        with patch.object(PGN.tools.urllib.request, "urlopen", side_effect=AssertionError("offline reuse tried a download")):
            repeated = self.load()
        self.assertIs(repeated[0], chess)
        self.assertIs(repeated[1], pgn)
        self.assertEqual(repeated[2], receipt)

    def test_corrupt_or_missing_archive_is_refused_before_import(self) -> None:
        archive = self.cache / self.artifact["filename"]
        data = archive.read_bytes()
        archive.write_bytes(bytes([data[0] ^ 1]) + data[1:])
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "SHA-256 mismatch"):
            self.load()
        self.assertNotIn("chess", sys.modules)
        archive.write_bytes(data[:-1])
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "size mismatch"):
            self.load()
        archive.unlink()
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "offline artifact missing"):
            self.load()

    def test_every_runtime_file_and_license_is_reverified_on_reuse(self) -> None:
        _, _, receipt = self.load()
        root = Path(receipt["runtime_root"])
        for item in receipt["files"]:
            with self.subTest(path=item["path"]):
                path = root / item["path"]
                original = path.read_bytes()
                path.write_bytes(original + b"\n# deliberate runtime mutation\n")
                with self.assertRaisesRegex(PGN.tools.ChessToolError, "runtime bytes differ"):
                    self.load()
                path.write_bytes(original)
        (root / "chess/pgn.py").unlink()
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "inventory is incomplete"):
            self.load()

    def test_extra_importable_files_and_bytecode_directories_are_refused(self) -> None:
        self.load()
        root = self.cache / "runtime/chess"
        for name in ("extra.py", "extra.pyc", "extra.so"):
            with self.subTest(name=name):
                path = root / name
                path.write_bytes(b"untrusted extra")
                with self.assertRaisesRegex(PGN.tools.ChessToolError, "extra or nonregular"):
                    self.load()
                path.unlink()
        (root / "__pycache__").mkdir()
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "extra.*directory"):
            self.load()

    def test_runtime_and_archive_links_are_refused_even_for_identical_bytes(self) -> None:
        self.load()
        for relative in ("runtime/chess/pgn.py", self.artifact["filename"]):
            with self.subTest(path=relative):
                path = self.cache / relative
                original = path.read_bytes()
                outside = self.directory / "outside"
                outside.write_bytes(original)
                path.unlink()
                path.symlink_to(outside)
                with self.assertRaisesRegex(PGN.tools.ChessToolError, "link|physical regular"):
                    self.load()
                path.unlink()
                os.link(outside, path)
                with self.assertRaisesRegex(PGN.tools.ChessToolError, "nonregular|physical regular"):
                    self.load()
                path.unlink()
                outside.unlink()
                path.write_bytes(original)

    def test_ambient_module_is_not_replaced_or_used(self) -> None:
        substitute = types.ModuleType("chess")
        substitute.__version__ = "1.11.2"
        sys.modules["chess"] = substitute
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "module inventory differs"):
            self.load()
        self.assertIs(sys.modules["chess"], substitute)

    def test_cache_and_runtime_directory_symlinks_are_refused(self) -> None:
        self.load()
        alias = self.directory / "alias"
        alias.symlink_to(self.cache, target_is_directory=True)
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "cache must not be a symlink"):
            PGN.load_provider(alias, offline=True)
        runtime = self.cache / "runtime"
        outside = self.directory / "original-runtime"
        runtime.rename(outside)
        runtime.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "runtime must be a physical directory"):
            self.load()

    def test_loaded_module_version_origin_and_identity_substitutions_are_refused(self) -> None:
        chess, pgn, _ = self.load()
        for owner, attribute, value, message in (
                (chess, "__version__", "substituted", "version differs"),
                (pgn, "__file__", "/ambient/chess/pgn.py", "origin differs"),
                (pgn.__spec__, "origin", "/ambient/chess/pgn.py", "origin differs"),
                (chess, "__path__", ["/ambient/chess"], "search path differs"),
                (pgn, "chess", types.ModuleType("chess"), "absolute import substitution"),
                (chess, "pgn", types.ModuleType("chess.pgn"), "package module substitution")):
            with self.subTest(attribute=attribute):
                previous = getattr(owner, attribute)
                setattr(owner, attribute, value)
                with self.assertRaisesRegex(PGN.tools.ChessToolError, message):
                    self.load()
                setattr(owner, attribute, previous)
        sys.modules["chess.pgn"] = types.ModuleType("chess.pgn")
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "module substitution"):
            self.load()

    def test_unsafe_tar_paths_links_and_duplicate_names_are_refused(self) -> None:
        # These archives test extraction rejection only; game validation always
        # uses the exact official artifact acquired in setUpClass.
        cases = [("../escape.py", tarfile.REGTYPE), ("/escape.py", tarfile.REGTYPE),
                 ("chess-1.11.2/chess/../escape.py", tarfile.REGTYPE),
                 ("chess-1.11.2/chess\\escape.py", tarfile.REGTYPE),
                 ("chess-1.11.2/chess/pgn.py", tarfile.SYMTYPE),
                 ("chess-1.11.2/chess/pgn.py", tarfile.LNKTYPE)]
        for index, (name, kind) in enumerate(cases):
            with self.subTest(name=name, kind=kind):
                path = self.directory / f"unsafe-{index}.tar.gz"
                with tarfile.open(path, "w:gz") as archive:
                    member = tarfile.TarInfo(name)
                    member.type = kind
                    member.linkname = "/outside"
                    archive.addfile(member, io.BytesIO(b""))
                with self.assertRaisesRegex(PGN.tools.ChessToolError, "unsafe|links or special"):
                    PGN._runtime_files(path, "1.11.2")
        path = self.directory / "duplicate.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            for _ in range(2):
                archive.addfile(tarfile.TarInfo("chess-1.11.2/chess/pgn.py"), io.BytesIO(b""))
        with self.assertRaisesRegex(PGN.tools.ChessToolError, "duplicate"):
            PGN._runtime_files(path, "1.11.2")


if __name__ == "__main__":
    unittest.main()
