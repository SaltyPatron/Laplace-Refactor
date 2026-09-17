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


class LichessAccountScopeReadback(unittest.TestCase):
    TOKEN = "credential-fixture-never-retained"
    ACCOUNT = {"id": "fixturebot", "username": "FixtureBot", "title": "BOT"}

    def check(self, documents, *, online=True, token=None):
        calls, responses = [], []
        def receive(request, timeout):
            calls.append(request)
            self.assertEqual(timeout, 15)
            self.assertEqual(request.full_url, "https://lichess.org" +
                             ("/api/account" if len(calls) == 1 else "/api/token/test"))
            item = documents[len(calls) - 1]
            if isinstance(item, Exception):
                raise item
            response = io.BytesIO(item if isinstance(item, bytes) else json.dumps(item).encode())
            response.status = 200
            responses.append(response)
            return response
        opener = types.SimpleNamespace(open=receive)
        with patch.dict(os.environ, {"LICHESS_TOKEN": self.TOKEN if token is None else token}, clear=True), \
             patch.object(TOOLS.urllib.request, "build_opener", return_value=opener):
            result = TOOLS.lichess_readback(online)
        self.assertNotIn(self.TOKEN, json.dumps(result))
        self.assertTrue(all(response.closed for response in responses))
        self.assertFalse(result["product_gameplay_ready"])
        self.assertEqual(result["product_bot_api_adapter"], "not-implemented")
        self.assertEqual(result["operator_stop_marker"], "not-applicable-no-product-service")
        return result, calls

    def test_offline_and_missing_token_perform_no_requests(self):
        for online, token in ((False, self.TOKEN), (True, ""), (True, "  ")):
            with self.subTest(online=online, configured=bool(token.strip())):
                result, calls = self.check([], online=online, token=token)
                self.assertEqual(calls, [])
                self.assertFalse(result["online_attempted"])
                self.assertFalse(result["account_prerequisites_ready"])
                self.assertEqual(result["disposition"], "not-verified")

    def test_matching_bot_and_scope_are_observed_without_promoting_gameplay(self):
        result, calls = self.check([self.ACCOUNT, {
            self.TOKEN: {"userId": "fixturebot", "scopes": "preference:read, bot:play"}}])
        self.assertTrue(result["account_prerequisites_ready"])
        self.assertTrue(result["token_valid"])
        self.assertEqual(result["account_id"], "fixturebot")
        self.assertEqual(result["account_username"], "FixtureBot")
        self.assertEqual(result["disposition"], "account-and-scope-readback-only")
        self.assertEqual([call.get_method() for call in calls], ["GET", "POST"])
        self.assertEqual(calls[0].get_header("Authorization"), "Bearer " + self.TOKEN)
        self.assertNotIn("Authorization", calls[0].headers)
        self.assertIsNone(calls[0].data)
        self.assertIsNone(calls[1].get_header("Authorization"))
        self.assertEqual(calls[1].data, self.TOKEN.encode())
        self.assertEqual(calls[1].get_header("Content-type"), "text/plain")

    def test_human_account_scope_substring_and_other_identity_cannot_pass(self):
        cases = [
            ({**self.ACCOUNT, "title": "GM"}, "fixturebot", "bot:play", False, True),
            (self.ACCOUNT, "fixturebot", "bot:play:extra", True, False),
            (self.ACCOUNT, "fixturebot", "", True, False),
            (self.ACCOUNT, "someoneelse", "bot:play", True, None),
        ]
        for account, owner, scopes, bot, scope in cases:
            with self.subTest(owner=owner, scopes=scopes, title=account.get("title")):
                result, calls = self.check([account, {self.TOKEN: {"userId": owner, "scopes": scopes}}])
                self.assertFalse(result["account_prerequisites_ready"])
                self.assertEqual(result["bot_account"], bot)
                self.assertEqual(result["bot_play_scope"], scope)
                self.assertEqual(len(calls), 2)

    def test_revoked_token_and_unauthorized_account_are_distinct(self):
        result, calls = self.check([self.ACCOUNT, {self.TOKEN: None}])
        self.assertFalse(result["token_valid"])
        self.assertEqual(result["disposition"], "token-rejected")
        for phase in (0, 1):
            error = TOOLS.urllib.error.HTTPError("https://lichess.org/", 401,
                                                self.TOKEN, {}, io.BytesIO(self.TOKEN.encode()))
            result, calls = self.check(([self.ACCOUNT] if phase else []) + [error])
            self.assertFalse(result["token_valid"])
            self.assertEqual(result["http_status"], 401)
            self.assertEqual(len(calls), phase + 1)
            self.assertTrue(error.fp.closed)

    def test_bad_or_oversized_scope_transport_never_exposes_secret_or_claims_scope(self):
        for payload in (b'{"' + self.TOKEN.encode() + b'":',
                        {self.TOKEN: {"userId": "fixturebot"}},
                        {self.TOKEN: {"userId": "fixturebot", "scopes": ["bot:play"]}},
                        {}, [], b"x" * (64 * 1024 + 1),
                        TOOLS.http.client.IncompleteRead(self.TOKEN.encode())):
            with self.subTest(kind=type(payload).__name__):
                result, calls = self.check([self.ACCOUNT, payload])
                self.assertEqual(result["disposition"], "account-readback-failed")
                self.assertEqual(result["failed_operation"], "token-scope")
                self.assertIsNone(result["bot_play_scope"])
                self.assertFalse(result["account_prerequisites_ready"])
                self.assertEqual(len(calls), 2)

    def test_invalid_account_prevents_scope_request(self):
        for account in ({}, {"username": "FixtureBot"}, {"id": "fixturebot"}, [],
                        OSError(self.TOKEN), ValueError(self.TOKEN)):
            with self.subTest(kind=type(account).__name__):
                result, calls = self.check([account])
                self.assertEqual(result["disposition"], "account-readback-failed")
                self.assertIsNone(result["token_valid"])
                self.assertEqual(len(calls), 1)

    def test_rate_limit_stops_without_retry_and_retains_minimum_delay(self):
        for value, minimum in (("", 60), ("5", 60), ("120", 120), ("9" * 100, 60)):
            error = TOOLS.urllib.error.HTTPError("https://lichess.org/", 429,
                                                self.TOKEN, {"Retry-After": value}, io.BytesIO())
            result, calls = self.check([error])
            self.assertEqual(len(calls), 1)
            self.assertEqual(result["minimum_retry_delay_seconds"], minimum)
            self.assertEqual(result["http_status"], 429)
            self.assertTrue(error.fp.closed)

    def test_redirect_handler_never_reissues_bearer_or_post_body(self):
        handler = TOOLS.LichessNoRedirect()
        for method, data in (("GET", None), ("POST", self.TOKEN.encode())):
            request = TOOLS.urllib.request.Request("https://lichess.org/api/token/test",
                                                   data=data, method=method)
            request.add_unredirected_header("Authorization", "Bearer " + self.TOKEN)
            for code in (301, 302, 303, 307, 308):
                self.assertIsNone(handler.redirect_request(request, None, code, self.TOKEN,
                                                           {}, "https://example.invalid/"))
        for code in (302, 403, 503):
            error = TOOLS.urllib.error.HTTPError("https://lichess.org/", code,
                                                self.TOKEN, {}, io.BytesIO(self.TOKEN.encode()))
            result, calls = self.check([error])
            self.assertEqual(result["http_status"], code)
            self.assertEqual(len(calls), 1)
            self.assertFalse(result["account_prerequisites_ready"])


class GitHubReleaseRequests(unittest.TestCase):
    def test_fixed_api_origin_uses_optional_token_and_preserves_release_selection(self) -> None:
        selected = {"releases": {"cutechess": {"repository": "cutechess/cutechess", "tag": "v1.5.1"}}}
        observed = {"draft": False, "prerelease": False, "tag_name": "v1.5.1",
                    "html_url": "https://github.com/cutechess/cutechess/releases/tag/v1.5.1"}
        calls = []
        def receive(request, timeout):
            self.assertEqual(request.full_url, "https://api.github.com/repos/cutechess/cutechess/releases/latest")
            self.assertEqual(timeout, 30)
            calls.append(request.get_header("Authorization"))
            return io.BytesIO(json.dumps(observed).encode())
        with patch.object(TOOLS.urllib.request, "urlopen", side_effect=receive):
            with patch.dict(os.environ, {"GITHUB_TOKEN": "fixture-token"}):
                actual = TOOLS.upstream_versions(selected)
            with patch.dict(os.environ, {"GITHUB_TOKEN": ""}):
                anonymous = TOOLS.upstream_versions(selected)
        self.assertEqual(calls, ["Bearer fixture-token", None])
        self.assertEqual(actual, anonymous)
        self.assertEqual(actual["cutechess"], {"selected": "v1.5.1", "latest": "v1.5.1",
                                             "current": True, "url": observed["html_url"]})

    def test_redirects_never_receive_api_authorization(self) -> None:
        with patch.dict(os.environ, {"GITHUB_TOKEN": "fixture-token"}):
            request = TOOLS.github_release_request("cutechess/cutechess")
        self.assertEqual(request.get_header("Authorization"), "Bearer fixture-token")
        handler = TOOLS.urllib.request.HTTPRedirectHandler()
        for location in ("https://api.github.com/redirected",
                         "https://github.com/cutechess/cutechess/releases",
                         "https://objects.githubusercontent.com/fixture",
                         "https://example.invalid/fixture"):
            for status in (301, 302, 303, 307, 308):
                with self.subTest(location=location, status=status):
                    redirected = handler.redirect_request(request, None, status, "redirect", {}, location)
                    self.assertIsNotNone(redirected)
                    self.assertIsNone(redirected.get_header("Authorization"))
                    self.assertNotIn("fixture-token", repr(redirected.header_items()))

    def test_repository_input_cannot_redirect_initial_credential(self) -> None:
        for repository in ("https://example.invalid/a", "a/b?redirect=x", "a/b#fragment",
                           "a/b/c", "a@evil.invalid/b", "a/b\nInjected: value", ""):
            with self.subTest(repository=repository), patch.dict(os.environ, {"GITHUB_TOKEN": "fixture-token"}):
                with self.assertRaisesRegex(TOOLS.ChessToolError, "invalid GitHub release repository"):
                    TOOLS.github_release_request(repository)

    def test_http_failure_is_not_masked_and_artifact_download_stays_unauthenticated(self) -> None:
        failure = TOOLS.urllib.error.HTTPError(
            "https://api.github.com/repos/cutechess/cutechess/releases/latest",
            403, "rate limit exceeded", {}, None)
        selected = {"releases": {"cutechess": {"repository": "cutechess/cutechess", "tag": "v1.5.1"}}}
        with patch.dict(os.environ, {"GITHUB_TOKEN": "fixture-token"}), \
             patch.object(TOOLS.urllib.request, "urlopen", side_effect=failure):
            with self.assertRaises(TOOLS.urllib.error.HTTPError) as caught:
                TOOLS.upstream_versions(selected)
        self.assertIs(caught.exception, failure)
        payload = b"bounded fixture artifact"
        artifact = {"filename": "fixture.bin", "url": "https://objects.githubusercontent.com/fixture",
                    "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
        def receive(request, timeout):
            self.assertEqual(request.full_url, artifact["url"])
            self.assertIsNone(request.get_header("Authorization"))
            self.assertEqual(timeout, 60)
            return io.BytesIO(payload)
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(os.environ, {"GITHUB_TOKEN": "fixture-token"}), \
             patch.object(TOOLS.urllib.request, "urlopen", side_effect=receive):
            acquired = TOOLS.acquire(artifact, Path(directory), False)
            self.assertEqual(acquired.read_bytes(), payload)


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


    def installed_source_fixture(self, source: Path, entry: dict) -> Path:
        # Real Git state and a retained executable hash; no engine-performance claim.
        (source / "src").mkdir(exist_ok=True)
        executable = source / "src" / ("stockfish.exe" if platform.system() == "Windows" else "stockfish")
        executable.write_bytes(b"source-selection executable fixture")
        with (source / ".git/info/exclude").open("a") as ignored:
            ignored.write("\n/src/stockfish\n/src/stockfish.exe\n")
        prefix = self.directory / "installation"
        prefix.mkdir(exist_ok=True)
        TOOLS.json_write(prefix / "current.json", {"tools": {"stockfish": {
            "source": str(source), "revision": entry["revision"],
            "source_archive_sha256": entry["git_archive_sha256"],
            "tracked_source": TOOLS.verify_tracked_inputs(source, entry["revision"]),
            "executable": str(executable), "sha256": TOOLS.digest(executable)}}})
        return prefix

    def test_selection_prefers_existing_requested_official_checkout_without_mutation(self) -> None:
        source, entry = self.source_fixture()
        requested = self.directory / "External/Stockfish/SF_19 with spaces"
        requested.parent.mkdir(parents=True)
        source.rename(requested)
        prefix = self.installed_source_fixture(requested, entry)
        head = TOOLS.git(requested, "rev-parse", "HEAD")
        before = (prefix / "current.json").read_bytes()
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", requested), \
             patch.object(TOOLS, "update_source", side_effect=AssertionError("selection must be read-only")):
            receipt = TOOLS.select_stockfish_source(prefix)
        self.assertEqual(receipt["selected_source"], str(requested))
        self.assertEqual(receipt["selected_checkout"]["git_root"], str(requested))
        self.assertEqual(receipt["selected_checkout"]["origin"], entry["upstream"])
        self.assertEqual(receipt["selected_checkout"]["commit"], head)
        self.assertTrue(receipt["requested_checkout"]["exists"])
        self.assertTrue(receipt["requested_checkout"]["available"])
        self.assertEqual(receipt["selection_reason"], "requested-existing-official-checkout")
        self.assertEqual((prefix / "current.json").read_bytes(), before)
        self.assertEqual(TOOLS.git(requested, "rev-parse", "HEAD"), head)

    def test_missing_requested_local_path_retains_actual_configured_checkout(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        missing = self.directory / "absent local SF_19"
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", missing):
            receipt = TOOLS.select_stockfish_source(prefix)
        self.assertFalse(receipt["requested_checkout"]["exists"])
        self.assertFalse(receipt["requested_checkout"]["available"])
        self.assertEqual(receipt["requested_checkout"]["reason"], "requested-local-path-unavailable")
        self.assertEqual(receipt["configured_source"], str(source))
        self.assertEqual(receipt["selected_source"], str(source))
        self.assertEqual(receipt["selection_reason"], "retained-configured-checkout")
        self.assertFalse(missing.exists())

    def test_ineligible_requested_path_is_not_reported_absent_or_populated(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        requested = self.directory / "not a repository"
        requested.mkdir()
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", requested):
            receipt = TOOLS.select_stockfish_source(prefix)
        self.assertTrue(receipt["requested_checkout"]["exists"])
        self.assertFalse(receipt["requested_checkout"]["available"])
        self.assertEqual(receipt["requested_checkout"]["reason"], "not-an-existing-git-checkout")
        self.assertEqual(receipt["selected_source"], str(source))
        self.assertEqual(list(requested.iterdir()), [])


    def test_unobservable_requested_path_retains_known_configured_source(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        requested = self.directory / "inaccessible local path"
        original = Path.lstat
        for exception in (PermissionError("unavailable provider"), RuntimeError("symlink cycle")):
            def denied(path, *args, **kwargs):
                if path == requested:
                    raise exception
                return original(path, *args, **kwargs)
            with self.subTest(exception=type(exception).__name__), \
                 patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", requested), \
                 patch.object(Path, "lstat", denied):
                receipt = TOOLS.select_stockfish_source(prefix)
            self.assertIsNone(receipt["requested_checkout"]["exists"])
            self.assertFalse(receipt["requested_checkout"]["available"])
            self.assertEqual(receipt["requested_checkout"]["reason"], "checkout-observation-failed")
            self.assertEqual(receipt["selected_source"], str(source))

    def test_explicit_missing_override_never_falls_back_to_available_requested(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        missing = self.directory / "explicit typo"
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", source):
            receipt = TOOLS.select_stockfish_source(prefix, missing)
        self.assertTrue(receipt["requested_checkout"]["available"])
        self.assertEqual(receipt["disposition"], "refused")
        self.assertIsNone(receipt["selected_source"])
        self.assertEqual(receipt["selection_reason"], "explicit-override")
        self.assertFalse(missing.exists())


    def test_official_ssh_origins_share_selection_and_build_validation(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        for origin in ("git@github.com:official-stockfish/Stockfish.git",
                       "ssh://git@github.com/official-stockfish/Stockfish.git"):
            with self.subTest(origin=origin):
                TOOLS.git(source, "remote", "set-url", "origin", origin)
                with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", source):
                    receipt = TOOLS.select_stockfish_source(prefix)
                self.assertTrue(receipt["requested_checkout"]["available"])
                self.assertEqual(receipt["selected_checkout"]["origin"], origin)
                args = argparse.Namespace(stockfish_source=source, source_root=None)
                self.assertEqual(TOOLS.tool_source(args, "stockfish"), source)
                TOOLS.update_source(source, entry, True)
                self.assertEqual(TOOLS.git(source, "rev-parse", "HEAD"), entry["revision"])

    def test_rejected_origin_is_not_copied_into_selection_evidence(self) -> None:
        source, _ = self.source_fixture()
        for origin in ("https://operator:secret-token@example.invalid/not-stockfish",
                       "https://operator:secret-token@github.com/official-stockfish/Stockfish.git",
                       "ssh://git:secret-token@github.com/official-stockfish/Stockfish.git"):
            with self.subTest(origin=origin):
                TOOLS.git(source, "remote", "set-url", "origin", origin)
                with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", source):
                    receipt = TOOLS.select_stockfish_source(self.directory / "no installation", source)
                self.assertEqual(receipt["disposition"], "refused")
                self.assertTrue(receipt["requested_checkout"]["exists"])
                self.assertFalse(receipt["requested_checkout"]["available"])
                self.assertNotIn("secret-token", json.dumps(receipt))
                self.assertIsNone(receipt["selected_checkout"]["origin"])

    def test_plain_setup_retains_current_source_and_refuses_missing_recorded_source(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        default = self.directory / "different estate"
        args = argparse.Namespace(stockfish_source=None, source_root=default, prefix=prefix)
        self.assertEqual(TOOLS.tool_source(args, "stockfish"), source)
        self.assertFalse(default.exists())
        moved = self.directory / "moved checkout"
        source.rename(moved)
        with self.assertRaises(FileNotFoundError):
            TOOLS.tool_source(args, "stockfish")
        self.assertFalse(default.exists())


    def test_recorded_local_import_keeps_existing_provenance_without_becoming_an_explicit_override(self) -> None:
        upstream, entry = self.source_fixture()
        source = self.directory / "verified imported checkout"
        subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", str(upstream), str(source)], check=True)
        prefix = self.installed_source_fixture(source, entry)
        real_read = TOOLS.json_read
        def read(path):
            return {"dependencies": {"stockfish": entry}} if path == ROOT / "dependencies/lock.json" else real_read(path)
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", self.directory / "absent"), \
             patch.object(TOOLS, "json_read", side_effect=read):
            receipt = TOOLS.select_stockfish_source(prefix)
            self.assertEqual(receipt["selected_checkout"]["origin_kind"], "local-import")
            self.assertIsNone(receipt["selected_checkout"]["origin"])
            self.assertEqual(TOOLS.verify_stockfish_selection(receipt, prefix)["selected_source"], str(source))
            refused = TOOLS.select_stockfish_source(prefix, source)
            self.assertEqual(refused["disposition"], "refused")
        args = argparse.Namespace(stockfish_source=None, prefix=prefix, source_root=self.directory / "other estate")
        self.assertEqual(TOOLS.tool_source(args, "stockfish"), source)

    def test_uninstalled_default_selection_preserves_portable_installer(self) -> None:
        requested = self.directory / "absent local"
        prefix = self.directory / "uninstalled"
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", requested):
            receipt = TOOLS.select_stockfish_source(prefix)
        self.assertEqual(receipt["disposition"], "first-install-default")
        self.assertIsNone(receipt["selected_source"])
        self.assertIsNone(receipt["configured_source"])
        self.assertEqual(receipt["planned_official_repository"].removesuffix(".git"), "https://github.com/official-stockfish/Stockfish")
        self.assertFalse(prefix.exists())
        self.assertFalse(requested.exists())
        args = argparse.Namespace(stockfish_source=None, prefix=prefix, source_root=self.directory / "estate")
        self.assertEqual(TOOLS.tool_source(args, "stockfish"), args.source_root / "stockfish")

    @unittest.skipIf(platform.system() == "Windows", "newline symlink path control uses POSIX names")
    def test_selection_refuses_input_and_resolved_newline_paths(self) -> None:
        source, _ = self.source_fixture()
        newline = self.directory / "checkout\nwith-newline"
        source.rename(newline)
        alias = self.directory / "apparently safe alias"
        alias.symlink_to(newline, target_is_directory=True)
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", self.directory / "absent"):
            for path in (newline, alias):
                with self.subTest(path=path):
                    receipt = TOOLS.select_stockfish_source(self.directory / "uninstalled", path)
                    self.assertEqual(receipt["disposition"], "refused")
                    self.assertIsNone(receipt["selected_source"])

    def test_postbuild_selection_accepts_updated_locked_head_but_rejects_stale_manifest(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", source):
            selection = TOOLS.select_stockfish_source(prefix)
        old_head = selection["selected_checkout"]["commit"]
        (source / "source.cpp").write_text("int main() { return 1; }\n")
        TOOLS.git(source, "add", "source.cpp")
        TOOLS.git(source, "-c", "user.name=Dependency Test", "-c", "user.email=test@example.invalid",
                  "commit", "--quiet", "-m", "new official fixture release")
        entry["revision"] = TOOLS.git(source, "rev-parse", "HEAD")
        entry["git_archive_sha256"] = hashlib.sha256(subprocess.check_output(
            ["git", "-C", str(source), "archive", "--format=tar", "HEAD"])).hexdigest()
        self.assertNotEqual(old_head, entry["revision"])
        real_read = TOOLS.json_read
        def read(path):
            return {"dependencies": {"stockfish": entry}} if path == ROOT / "dependencies/lock.json" else real_read(path)
        with patch.object(TOOLS, "json_read", side_effect=read):
            with self.assertRaisesRegex(TOOLS.ChessToolError, "committed-source provenance"):
                TOOLS.verify_stockfish_selection(selection, prefix)
            self.installed_source_fixture(source, entry)
            verified = TOOLS.verify_stockfish_selection(selection, prefix)
        self.assertEqual(verified["selected_source"], str(source))
        self.assertEqual(verified["locked_commit"], entry["revision"])
        self.assertEqual(verified["selected_checkout"]["commit"], entry["revision"])
        self.assertEqual(verified["selection"]["selected_checkout"]["commit"], old_head)

    def test_postbuild_selection_rejects_another_checkout_with_identical_git_content(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", source):
            selection = TOOLS.select_stockfish_source(prefix)
        other = self.directory / "different same-content checkout"
        subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", str(source), str(other)], check=True)
        manifest = TOOLS.json_read(prefix / "current.json")
        manifest["tools"]["stockfish"]["source"] = str(other)
        TOOLS.json_write(prefix / "current.json", manifest)
        with self.assertRaisesRegex(TOOLS.ChessToolError, "source differs from the selected checkout"):
            TOOLS.verify_stockfish_selection(selection, prefix)
        self.assertEqual(TOOLS.git(other, "rev-parse", "HEAD"), entry["revision"])

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

    def test_requested_diagnostic_distinguishes_absence_file_and_directory(self) -> None:
        source, entry = self.source_fixture()
        missing = self.directory / "absent parent" / "SF_19"
        absent = TOOLS.diagnose_stockfish_path(missing, entry)
        self.assertEqual("missing", absent["input_kind"])
        self.assertEqual("missing-target", absent["resolution"])
        self.assertEqual(str(self.directory.resolve()), absent["nearest_existing_directory"])
        self.assertEqual(str(missing), absent["ancestry"][-1]["path"])
        self.assertFalse(missing.parent.exists())
        plain = self.directory / "plain file"
        plain.write_bytes(b"preserved operator bytes")
        file_view = TOOLS.diagnose_stockfish_path(plain, entry)
        self.assertEqual("regular-file", file_view["input_kind"])
        self.assertEqual("regular-file", file_view["resolved_kind"])
        self.assertIsNone(file_view["git_marker_kind"])
        blocked = TOOLS.diagnose_stockfish_path(plain / "child", entry)
        self.assertEqual("not-a-directory", blocked["input_kind"])
        self.assertEqual("non-directory-component", blocked["resolution"])
        self.assertEqual(str(self.directory.resolve()), blocked["nearest_existing_directory"])
        directory = self.directory / "plain directory"
        directory.mkdir()
        directory_view = TOOLS.diagnose_stockfish_path(directory, entry)
        self.assertEqual("directory", directory_view["input_kind"])
        self.assertEqual("directory", directory_view["resolved_kind"])
        self.assertEqual("missing", directory_view["git_marker_kind"])
        self.assertEqual(b"preserved operator bytes", plain.read_bytes())
        self.assertEqual([], list(directory.iterdir()))

    def test_requested_diagnostic_records_exact_official_checkout_without_writes(self) -> None:
        source, entry = self.source_fixture()
        before = (source / ".git/config").read_bytes()
        head = (source / ".git/HEAD").read_bytes()
        view = TOOLS.diagnose_stockfish_path(source, entry)
        self.assertEqual("directory", view["git_marker_kind"])
        self.assertEqual({"status": "observed-official-checkout", "stage": None,
                          "top_level": str(source.resolve()), "origin": entry["upstream"],
                          "commit": entry["revision"]}, view["enclosing_git"])
        self.assertEqual(before, (source / ".git/config").read_bytes())
        self.assertEqual(head, (source / ".git/HEAD").read_bytes())

    def test_requested_diagnostic_finds_nested_root_without_promoting_selection(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        nested = source / "include"
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", nested):
            receipt = TOOLS.select_stockfish_source(prefix)
        requested = receipt["requested_checkout"]
        self.assertFalse(requested["available"])
        self.assertEqual("not-an-existing-git-checkout", requested["reason"])
        self.assertEqual("missing", requested["diagnostic"]["git_marker_kind"])
        self.assertEqual(str(source.resolve()), requested["diagnostic"]["enclosing_git"]["top_level"])
        self.assertEqual(entry["revision"], requested["diagnostic"]["enclosing_git"]["commit"])
        self.assertEqual("retained-configured-checkout", receipt["selection_reason"])
        self.assertEqual(str(source), receipt["selected_source"])

    def test_requested_diagnostic_accepts_real_git_file_worktree_marker(self) -> None:
        source, entry = self.source_fixture()
        worktree = self.directory / "linked worktree with spaces"
        TOOLS.git(source, "worktree", "add", "--detach", str(worktree), entry["revision"])
        marker = (worktree / ".git").read_bytes()
        view = TOOLS.diagnose_stockfish_path(worktree, entry)
        self.assertEqual("regular-file", view["git_marker_kind"])
        self.assertEqual(str(worktree.resolve()), view["enclosing_git"]["top_level"])
        self.assertEqual(entry["revision"], view["enclosing_git"]["commit"])
        self.assertTrue(TOOLS.observe_stockfish_checkout(worktree, entry, official=True)["available"])
        self.assertEqual(marker, (worktree / ".git").read_bytes())

    @unittest.skipUnless(os.name == "posix", "requires POSIX symlinks")
    def test_requested_diagnostic_keeps_link_kind_and_resolved_target_distinct(self) -> None:
        source, entry = self.source_fixture()
        alias = self.directory / "source alias"
        alias.symlink_to(source, target_is_directory=True)
        view = TOOLS.diagnose_stockfish_path(alias, entry)
        self.assertEqual("symlink", view["input_kind"])
        self.assertEqual("directory", view["resolved_kind"])
        self.assertEqual(str(source.resolve()), view["resolved_path"])
        self.assertEqual(str(source.resolve()), view["enclosing_git"]["top_level"])
        self.assertEqual("symlink", view["ancestry"][-1]["kind"])
        dangling = self.directory / "dangling alias"
        dangling.symlink_to(self.directory / "missing target")
        unavailable = TOOLS.diagnose_stockfish_path(dangling, entry)
        self.assertEqual("symlink", unavailable["input_kind"])
        self.assertEqual("missing-target", unavailable["resolution"])
        self.assertIsNone(unavailable["resolved_path"])
        loop = self.directory / "loop"
        loop.symlink_to(loop)
        self.assertEqual("resolution-failed", TOOLS.diagnose_stockfish_path(loop, entry)["resolution"])
        self.assertEqual(str(source), os.readlink(alias))
        self.assertTrue(dangling.is_symlink())

    def test_requested_diagnostic_invalid_marker_retains_kind_and_failed_stage(self) -> None:
        _, entry = self.source_fixture()
        directory = self.directory / "broken marker"
        directory.mkdir()
        marker = directory / ".git"
        marker.write_text("not a valid Git marker\n")
        view = TOOLS.diagnose_stockfish_path(directory, entry)
        self.assertEqual("regular-file", view["git_marker_kind"])
        self.assertEqual("observation-failed", view["enclosing_git"]["status"])
        self.assertEqual("top-level", view["enclosing_git"]["stage"])
        self.assertIsNone(view["enclosing_git"]["top_level"])
        self.assertEqual("not a valid Git marker\n", marker.read_text())

    def test_requested_diagnostic_does_not_echo_rejected_origin_or_command_errors(self) -> None:
        source, entry = self.source_fixture()
        origin = "https://operator:secret-token@example.invalid/unrelated.git"
        TOOLS.git(source, "remote", "set-url", "origin", origin)
        rejected = TOOLS.diagnose_stockfish_path(source, entry)
        self.assertEqual("origin-not-official", rejected["enclosing_git"]["status"])
        self.assertEqual(str(source.resolve()), rejected["enclosing_git"]["top_level"])
        self.assertIsNone(rejected["enclosing_git"]["origin"])
        self.assertIsNone(rejected["enclosing_git"]["commit"])
        with patch.object(TOOLS.subprocess, "run", return_value=subprocess.CompletedProcess(
                ["git"], 128, stdout=origin, stderr="secret-token: unsafe directory")):
            failed = TOOLS.diagnose_stockfish_path(source, entry)
        self.assertEqual("observation-failed", failed["enclosing_git"]["status"])
        self.assertEqual("top-level", failed["enclosing_git"]["stage"])
        self.assertNotIn("secret-token", json.dumps([rejected, failed]))

    def test_requested_diagnostic_refuses_oversized_or_multiline_git_metadata(self) -> None:
        source, entry = self.source_fixture()
        original = subprocess.run
        for stage, failed_call in (("top-level", 1), ("origin", 2), ("commit", 3)):
            for output in ("x" * 4097, "unexpected\nsecond-line"):
                with self.subTest(stage=stage, output_length=len(output)):
                    calls = 0
                    def invalid_output(command, **options):
                        nonlocal calls
                        calls += 1
                        if calls == failed_call:
                            return subprocess.CompletedProcess(command, 0, stdout=output, stderr="")
                        return original(command, **options)
                    with patch.object(TOOLS.subprocess, "run", side_effect=invalid_output):
                        view = TOOLS.diagnose_stockfish_path(source, entry)
                    self.assertEqual(failed_call, calls)
                    self.assertEqual(stage, view["enclosing_git"]["stage"])
                    self.assertEqual("observation-failed", view["enclosing_git"]["status"])
                    self.assertIsNone(view["enclosing_git"]["commit"])
                    self.assertNotIn(output, json.dumps(view))

    def test_requested_diagnostic_cannot_follow_inherited_repository_selectors(self) -> None:
        source, entry = self.source_fixture()
        unrelated = self.directory / "unrelated"
        unrelated.mkdir()
        subprocess.run(["git", "init", "--quiet", str(unrelated)], check=True)
        inherited = {"GIT_DIR": str(unrelated / ".git"), "GIT_WORK_TREE": str(unrelated),
                     "GIT_COMMON_DIR": str(unrelated / ".git"), "GIT_CONFIG_COUNT": "1",
                     "GIT_CONFIG_KEY_0": "core.worktree", "GIT_CONFIG_VALUE_0": str(unrelated)}
        original = subprocess.run
        calls = []
        def inspected(command, **options):
            calls.append(command)
            self.assertEqual(2, options["timeout"])
            self.assertEqual(subprocess.DEVNULL, options["stdin"])
            for key in inherited:
                self.assertNotIn(key, options["env"])
            self.assertEqual("1", options["env"]["GIT_NO_REPLACE_OBJECTS"])
            self.assertEqual("0", options["env"]["GIT_TERMINAL_PROMPT"])
            return original(command, **options)
        with patch.dict(os.environ, inherited), \
             patch.object(TOOLS.subprocess, "run", side_effect=inspected):
            view = TOOLS.diagnose_stockfish_path(source / "include", entry)
            self.assertEqual(inherited["GIT_DIR"], os.environ["GIT_DIR"])
        self.assertEqual(3, len(calls))
        self.assertEqual(str(source.resolve()), view["enclosing_git"]["top_level"])
        self.assertEqual(entry["revision"], view["enclosing_git"]["commit"])

    def test_requested_diagnostic_timeout_does_not_change_source_selection(self) -> None:
        source, entry = self.source_fixture()
        prefix = self.installed_source_fixture(source, entry)
        requested = self.directory / "plain requested"
        requested.mkdir()
        original = subprocess.run
        def timeout_diagnostic(command, **options):
            if options.get("timeout") == 2:
                raise subprocess.TimeoutExpired(command, 2, output="secret-token")
            return original(command, **options)
        with patch.object(TOOLS, "REQUESTED_STOCKFISH_SOURCE", requested), \
             patch.object(TOOLS.subprocess, "run", side_effect=timeout_diagnostic):
            receipt = TOOLS.select_stockfish_source(prefix)
        self.assertEqual("command-timeout", receipt["requested_checkout"]["diagnostic"]["enclosing_git"]["status"])
        self.assertEqual(str(source), receipt["selected_source"])
        self.assertEqual("retained-configured-checkout", receipt["selection_reason"])
        self.assertFalse(receipt["requested_checkout"]["available"])
        self.assertNotIn("secret-token", json.dumps(receipt))

    def test_requested_diagnostic_permission_failure_is_not_claimed_absent(self) -> None:
        _, entry = self.source_fixture()
        requested = self.directory / "inaccessible"
        requested.mkdir()
        original_lstat = Path.lstat
        original_resolve = Path.resolve
        def denied_lstat(path, *args, **kwargs):
            if path == requested:
                raise PermissionError("private detail")
            return original_lstat(path, *args, **kwargs)
        def denied_resolve(path, *args, **kwargs):
            if path == requested:
                raise PermissionError("private detail")
            return original_resolve(path, *args, **kwargs)
        with patch.object(Path, "lstat", denied_lstat), patch.object(Path, "resolve", denied_resolve):
            view = TOOLS.diagnose_stockfish_path(requested, entry)
        self.assertEqual("permission-denied", view["input_kind"])
        self.assertEqual("permission-denied", view["resolution"])
        self.assertEqual("permission-denied", view["ancestry"][-1]["kind"])
        self.assertEqual(str(self.directory.resolve()), view["nearest_existing_directory"])
        self.assertNotIn("private detail", json.dumps(view))

    def test_requested_diagnostic_ancestry_bound_prevents_git_probe(self) -> None:
        _, entry = self.source_fixture()
        with patch.object(TOOLS.subprocess, "run", side_effect=AssertionError("out-of-bound path must not run Git")):
            view = TOOLS.diagnose_stockfish_path(Path("/") / Path(*(["component"] * 65)), entry)
        self.assertEqual([], view["ancestry"])
        self.assertEqual("observation-failed", view["enclosing_git"]["status"])

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

    def gui_sdk_fixture(self) -> Path:
        # Files identify a controlled SDK fixture; they are never loaded as Qt.
        sdk = self.directory / "Qt SDK with spaces"
        for name in ("Core", "Gui", "Widgets", "Concurrent", "Svg", "PrintSupport", "Core5Compat"):
            path = sdk / f"lib/cmake/Qt6{name}/Qt6{name}Config.cmake"
            path.parent.mkdir(parents=True)
            path.write_text(f"# {name} fixture\n")
        version = sdk / "lib/cmake/Qt6/Qt6ConfigVersion.cmake"
        version.parent.mkdir(parents=True)
        version.write_text('include("${CMAKE_CURRENT_LIST_DIR}/Qt6ConfigVersionImpl.cmake")\n')
        version.with_name("Qt6ConfigVersionImpl.cmake").write_text('set(PACKAGE_VERSION "6.11.2")\n')
        suffix, lead = (".dll", "") if platform.system() == "Windows" else (".dylib", "lib") if platform.system() == "Darwin" else (".so", "lib")
        plugin = sdk / "plugins/platforms" / (lead + "qoffscreen" + suffix)
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b"fixture identity; not a loadable plugin")
        return sdk

    def gui_protocol_fixture(self, *, version: str = "1.5.1", qt: str = "6.11.2",
                             emit_plugin: bool = True, exit_code: int = 0,
                             plugin: str | None = None) -> list[str]:
        # This actual child process exercises receipt/probe transport only.
        # Actual Qt initialization is exercised by the candidate's official GUI.
        script = self.directory / "gui_protocol_fixture.py"
        script.write_text("\n".join([
            "import json, os, pathlib, sys",
            "assert sys.argv[1:] == ['-platform', 'offscreen', '--version']",
            "assert os.environ['QT_QPA_PLATFORM'] == 'offscreen'",
            "assert os.environ['QT_DEBUG_PLUGINS'] == '1'",
            "assert not os.environ.get('DISPLAY') and not os.environ.get('WAYLAND_DISPLAY')",
            "assert pathlib.Path(os.environ['XDG_CONFIG_HOME']).is_dir()",
            "assert pathlib.Path(os.environ['XDG_CONFIG_DIRS']).is_dir()",
            "assert sys.stdin.read() == ''",
            f"print('Cute Chess {version}')",
            f"print('Using Qt version {qt}')",
            "platforms = pathlib.Path(os.environ['QT_QPA_PLATFORM_PLUGIN_PATH'])",
            f"selected = {plugin!r}",
            "if selected is None: selected = str(next(platforms.glob('*qoffscreen.*')))",
            f"if {emit_plugin!r}: print('qt.core.library: ' + json.dumps(selected) + ' loaded library', file=sys.stderr)",
            f"sys.exit({exit_code})",
        ]) + "\n")
        return [sys.executable, str(script)]

    def test_gui_probe_runs_child_with_isolated_settings_and_exact_plugin_evidence(self) -> None:
        sdk = self.gui_sdk_fixture()
        command = self.gui_protocol_fixture()
        with patch.object(TOOLS, "SCRATCH", self.directory), \
             patch.dict(os.environ, {"DISPLAY": ":123", "WAYLAND_DISPLAY": "inherited-wayland",
                                     "QT_QPA_PLATFORM": "xcb", "QT_PLUGIN_PATH": "/other/sdk"}):
            before = os.environ.copy()
            receipt = TOOLS.probe_cutechess_gui(command, sdk)
            self.assertEqual(before, os.environ.copy())
        self.assertEqual("ready-headless", receipt["disposition"])
        self.assertTrue(receipt["qapplication_initialized"])
        self.assertFalse(receipt["interactive_desktop_tested"])
        self.assertIsNone(receipt["interactive_desktop_ready"])
        self.assertEqual({"DISPLAY": True, "WAYLAND_DISPLAY": True}, receipt["display_environment_present"])
        plugin = receipt["loaded_offscreen_plugin"]
        self.assertEqual(TOOLS.digest(Path(plugin["path"])), plugin["sha256"])
        self.assertEqual(command + ["-platform", "offscreen", "--version"], receipt["command"])
        self.assertGreaterEqual(receipt["elapsed_seconds"], 0)
        self.assertEqual([], list(self.directory.glob("cutechess-gui-probe-*")))

    def test_qt_environment_sdk_reuse_reads_actual_version_wrapper(self) -> None:
        sdk = self.gui_sdk_fixture()
        arguments = argparse.Namespace(prefix=self.directory / "installation", offline=True)
        variables = {"LAPLACE_QT_PREFIX": "", "QT_ROOT_DIR": "", "CMAKE_PREFIX_PATH": ""}
        with patch.object(TOOLS, "execute", side_effect=AssertionError("SDK reuse must not execute acquisition")), \
             patch.object(TOOLS, "acquire", side_effect=AssertionError("SDK reuse must not download")):
            for name in variables:
                with self.subTest(variable=name), patch.dict(os.environ, {**variables, name: str(sdk)}):
                    self.assertEqual(sdk.resolve(), TOOLS.acquire_qt(arguments, self.selected, self.artifacts))
            implementation = sdk / "lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake"
            with patch.dict(os.environ, {**variables, "LAPLACE_QT_PREFIX": str(sdk)}):
                implementation.write_text('set(PACKAGE_VERSION "6.10.0")\n')
                with self.assertRaisesRegex(TOOLS.ChessToolError, "Qt SDK is missing|acquisition is not selected"):
                    TOOLS.acquire_qt(arguments, self.selected, self.artifacts)
                implementation.unlink()
                with self.assertRaisesRegex(TOOLS.ChessToolError, "Qt SDK is missing|acquisition is not selected"):
                    TOOLS.acquire_qt(arguments, self.selected, self.artifacts)

    def test_gui_version_reads_real_qt_wrapper_and_retains_exact_inputs(self) -> None:
        sdk = self.gui_sdk_fixture()
        wrapper = sdk / "lib/cmake/Qt6/Qt6ConfigVersion.cmake"
        implementation = wrapper.with_name("Qt6ConfigVersionImpl.cmake")
        wrapper_bytes = wrapper.read_bytes()
        wrapped = TOOLS.qt_gui_inventory(sdk)
        self.assertEqual("6.11.2", wrapped["qt_version"])
        self.assertEqual([{"path": str(path), "sha256": TOOLS.digest(path)}
                          for path in (wrapper, implementation)], wrapped["version_files"])
        # Compatible SDKs may use CMake's version file directly.
        wrapper.write_bytes(implementation.read_bytes())
        implementation.unlink()
        direct = TOOLS.qt_gui_inventory(sdk)
        self.assertEqual("6.11.2", direct["qt_version"])
        self.assertEqual([{"path": str(wrapper), "sha256": TOOLS.digest(wrapper)}],
                         direct["version_files"])
        wrapper.write_bytes(wrapper_bytes)
        with patch.object(TOOLS.subprocess, "run", side_effect=AssertionError("bad version must refuse before launch")):
            with self.assertRaises(FileNotFoundError):
                TOOLS.probe_cutechess_gui(["unlaunched"], sdk)
            for content, message in (('set(PACKAGE_VERSION "6.7.3")\n', "requires Qt"),
                                     ('# no declared version\n', "missing or conflicting")):
                implementation.write_text(content)
                with self.assertRaisesRegex(TOOLS.ChessToolError, message):
                    TOOLS.probe_cutechess_gui(["unlaunched"], sdk)
            implementation.write_text('set(PACKAGE_VERSION "6.11.2")\n')
            wrapper.write_bytes(wrapper_bytes + b'set(PACKAGE_VERSION "6.10.0")\n')
            with self.assertRaisesRegex(TOOLS.ChessToolError, "missing or conflicting"):
                TOOLS.probe_cutechess_gui(["unlaunched"], sdk)

    def test_gui_inventory_records_both_split_and_unified_wayland_plugins(self) -> None:
        with patch.object(TOOLS.platform, "system", return_value="Linux"):
            sdk = self.gui_sdk_fixture()
            paths = {name: sdk / "plugins/platforms" / filename for name, filename in
                     (("wayland", "libqwayland.so"),
                      ("wayland_generic", "libqwayland-generic.so"),
                      ("wayland_egl", "libqwayland-egl.so"))}
            paths["wayland_generic"].write_bytes(b"split generic fixture")
            paths["wayland_egl"].write_bytes(b"split EGL fixture")
            split = TOOLS.qt_gui_inventory(sdk)
            self.assertFalse(split["plugins"]["wayland"]["present"])
            for name in ("wayland_generic", "wayland_egl"):
                self.assertTrue(split["plugins"][name]["present"])
                self.assertEqual(str(paths[name]), split["plugins"][name]["path"])
                self.assertEqual(TOOLS.digest(paths[name]), split["plugins"][name]["sha256"])
                paths[name].unlink()
            paths["wayland"].write_bytes(b"unified fixture")
            unified = TOOLS.qt_gui_inventory(sdk)
            self.assertTrue(unified["plugins"]["wayland"]["present"])
            self.assertEqual(TOOLS.digest(paths["wayland"]), unified["plugins"]["wayland"]["sha256"])
            for name in ("wayland_generic", "wayland_egl"):
                self.assertFalse(unified["plugins"][name]["present"])
                self.assertIsNone(unified["plugins"][name]["sha256"])
            self.assertIn("CMake package configuration files", unified["module_identity_scope"])
            self.assertIn("shared-library binaries are not hashed", unified["module_identity_scope"])

    def test_gui_probe_does_not_claim_cross_platform_settings_isolation(self) -> None:
        expected = {"Linux": "isolated-XDG-probe",
                    "Darwin": "isolated-XDG-INI-user-settings; system and Qt native settings not isolated",
                    "Windows": "Windows-user-known-folder; version branch only"}
        for system, scope in expected.items():
            with self.subTest(system=system), tempfile.TemporaryDirectory(dir=self.directory) as scratch:
                with patch.object(self, "directory", Path(scratch)), \
                     patch.object(TOOLS, "SCRATCH", Path(scratch)), \
                     patch.object(TOOLS.platform, "system", return_value=system):
                    sdk = self.gui_sdk_fixture()
                    receipt = TOOLS.probe_cutechess_gui(self.gui_protocol_fixture(), sdk)
                    self.assertEqual(scope, receipt["settings_scope"])
                    self.assertFalse(receipt["interactive_desktop_tested"])
                    self.assertEqual([], list(Path(scratch).glob("cutechess-gui-probe-*")))

    def test_gui_version_without_plugin_load_cannot_be_ready(self) -> None:
        sdk = self.gui_sdk_fixture()
        with patch.object(TOOLS, "SCRATCH", self.directory):
            for options, message in (({"emit_plugin": False}, "did not prove"),
                                     ({"version": "1.0.0"}, "version mismatch"),
                                     ({"qt": "6.8.0"}, "selected SDK"),
                                     ({"exit_code": 7}, "initialization failed")):
                with self.subTest(options=options), self.assertRaisesRegex(TOOLS.ChessToolError, message):
                    TOOLS.probe_cutechess_gui(self.gui_protocol_fixture(**options), sdk)

    def test_gui_probe_rejects_another_sdk_plugin_even_with_matching_bytes(self) -> None:
        sdk = self.gui_sdk_fixture()
        inventory = TOOLS.qt_gui_inventory(sdk)
        original = Path(inventory["plugins"]["offscreen"]["path"])
        other = self.directory / "other SDK" / original.name
        other.parent.mkdir()
        other.write_bytes(original.read_bytes())
        with patch.object(TOOLS, "SCRATCH", self.directory), \
             self.assertRaisesRegex(TOOLS.ChessToolError, "selected offscreen plugin"):
            TOOLS.probe_cutechess_gui(self.gui_protocol_fixture(plugin=str(other)), sdk)

    def test_gui_module_and_offscreen_absence_fail_before_process_launch(self) -> None:
        sdk = self.gui_sdk_fixture()
        svg = sdk / "lib/cmake/Qt6Svg/Qt6SvgConfig.cmake"
        raw = svg.read_bytes()
        with patch.object(TOOLS.subprocess, "run", side_effect=AssertionError("missing dependencies must refuse before launch")):
            svg.unlink()
            with self.assertRaisesRegex(TOOLS.ChessToolError, "module is missing: Svg"):
                TOOLS.probe_cutechess_gui(["unlaunched"], sdk)
            svg.write_bytes(raw)
            offscreen = Path(TOOLS.qt_gui_inventory(sdk)["plugins"]["offscreen"]["path"])
            offscreen.unlink()
            with self.assertRaisesRegex(TOOLS.ChessToolError, "offscreen platform plugin is missing"):
                TOOLS.probe_cutechess_gui(["unlaunched"], sdk)

    def test_gui_reuse_refuses_changed_binary_or_plugin_before_launch(self) -> None:
        sdk = self.gui_sdk_fixture()
        binary = self.directory / "gui bytes"
        binary.write_bytes(b"fixture artifact, not executed")
        tool = {"qt_prefix": str(sdk), "gui": {"executable": str(binary),
                "sha256": TOOLS.digest(binary), "checks": {"qt": TOOLS.qt_gui_inventory(sdk)}}}
        with patch.object(TOOLS, "probe_cutechess_gui", side_effect=AssertionError("tamper must refuse before launch")):
            binary.write_bytes(b"changed artifact")
            with self.assertRaisesRegex(TOOLS.ChessToolError, "executable differs"):
                TOOLS.verify_cutechess_gui(tool)
            binary.write_bytes(b"fixture artifact, not executed")
            Path(tool["gui"]["checks"]["qt"]["plugins"]["offscreen"]["path"]).write_bytes(b"changed plugin")
            with self.assertRaisesRegex(TOOLS.ChessToolError, "inventory differs"):
                TOOLS.verify_cutechess_gui(tool)

    def test_gui_timeout_cleans_settings_without_publishing_readiness(self) -> None:
        sdk = self.gui_sdk_fixture()
        with patch.object(TOOLS, "SCRATCH", self.directory), \
             patch.object(TOOLS.subprocess, "run", side_effect=subprocess.TimeoutExpired(["fixture"], 30)):
            with self.assertRaises(subprocess.TimeoutExpired):
                TOOLS.probe_cutechess_gui(["fixture"], sdk)
        self.assertEqual([], list(self.directory.glob("cutechess-gui-probe-*")))

    @unittest.skipUnless(os.name == "posix" and shutil.which("cmake"), "actual CMake fixture target dispatch requires POSIX and CMake")
    def test_gui_build_uses_actual_targets_and_survives_plain_reinstall(self) -> None:
        source, entry = self.source_fixture()
        sdk = self.gui_sdk_fixture()
        # A tiny independent CMake project proves our installer invokes both
        # upstream target names; its executable is a protocol fixture, not Qt.
        script = source / "fixture.py"
        script.write_text("\n".join([
            "#!/usr/bin/env python3",
            "import json, os, pathlib, sys",
            "gui = pathlib.Path(sys.argv[0]).name == 'cutechess'",
            "if '--help' in sys.argv: print('-engine -pgnout -repeat -openings')",
            "else:",
            "    print('Cute Chess 1.5.1' if gui else 'cutechess-cli 1.5.1')",
            "    print('Using Qt version 6.11.2')",
            "    if gui:",
            "        plugin = next(pathlib.Path(os.environ['QT_QPA_PLATFORM_PLUGIN_PATH']).glob('*qoffscreen.*'))",
            "        print('qt.core.library: ' + json.dumps(str(plugin)) + ' loaded library', file=sys.stderr)",
        ]) + "\n")
        script.chmod(0o755)
        (source / "CMakeLists.txt").write_text("\n".join([
            "cmake_minimum_required(VERSION 3.20)",
            "project(installer_dispatch_fixture NONE)",
            "foreach(pair IN ITEMS cli gui)",
            '  if(pair STREQUAL "cli")',
            "    set(output cutechess-cli)",
            "  else()",
            "    set(output cutechess)",
            "  endif()",
            "  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${output}",
            "    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/fixture.py ${CMAKE_BINARY_DIR}/${output}",
            "    DEPENDS ${CMAKE_SOURCE_DIR}/fixture.py)",
            "  add_custom_target(${pair} DEPENDS ${CMAKE_BINARY_DIR}/${output})",
            "endforeach()",
        ]) + "\n")
        TOOLS.git(source, "add", ".")
        TOOLS.git(source, "-c", "user.name=Dependency Test", "-c", "user.email=test@example.invalid",
                  "commit", "--quiet", "-m", "independent target dispatch fixture")
        entry["revision"] = TOOLS.git(source, "rev-parse", "HEAD")
        entry["git_archive_sha256"] = hashlib.sha256(subprocess.check_output(
            ["git", "-C", str(source), "archive", "--format=tar", "HEAD"])).hexdigest()
        arguments = argparse.Namespace(tool="cutechess", prefix=self.directory / "installed",
            build_root=self.directory / "build", qt_prefix=sdk, jobs=1, offline=True, cutechess_gui=False)
        original_read = TOOLS.json_read
        def fixture_lock(path):
            document = original_read(path)
            if path == TOOLS.ROOT / "dependencies/lock.json":
                # main() validates the full release selection before direct run.
                # Retain unrelated tools and CuteChess release metadata while
                # replacing only this fixture's source identity.
                document["dependencies"]["cutechess"].update(entry)
            return document
        with patch.object(TOOLS, "SCRATCH", self.directory), patch.object(TOOLS, "json_read", side_effect=fixture_lock), \
             patch.object(TOOLS, "tool_source", return_value=source):
            cli_only = TOOLS.build_tools(arguments, self.selected, self.artifacts)
            self.assertNotIn("gui", cli_only["tools"]["cutechess"])
            arguments.cutechess_gui = True
            installed = TOOLS.build_tools(arguments, self.selected, self.artifacts)
            gui = installed["tools"]["cutechess"]["gui"]
            self.assertEqual("gui", gui["build_target"])
            self.assertEqual("cutechess-cli", Path(installed["tools"]["cutechess"]["executable"]).name)
            self.assertEqual("cutechess", Path(gui["executable"]).name)
            self.assertEqual([gui["executable"]], gui["direct_launch"]["argv"])
            self.assertEqual(TOOLS.digest(Path(gui["executable"])), gui["sha256"])
            Path(gui["executable"]).write_text("stale former GUI")
            arguments.cutechess_gui = False
            rebuilt = TOOLS.build_tools(arguments, self.selected, self.artifacts)
            self.assertEqual(gui["sha256"], rebuilt["tools"]["cutechess"]["gui"]["sha256"])
            checked = TOOLS.verify_installation(arguments.prefix, self.selected, self.artifacts, "cutechess", with_gui=True)
            self.assertEqual("ready-headless", checked["tools"]["cutechess"]["gui"]["checks"]["disposition"])
            persisted = original_read(arguments.prefix / "current.json")
            self.assertEqual(rebuilt["tools"]["cutechess"]["gui"], persisted["tools"]["cutechess"]["gui"])
            for gui_flag, expected in (([], persisted["tools"]["cutechess"]["executable"]),
                                       (["--cutechess-gui"], gui["executable"])):
                argv = ["chess_tools.py", "run", "--prefix", str(arguments.prefix),
                        "--tool", "cutechess", *gui_flag, "--", "--version"]
                with patch.object(sys, "argv", argv), patch.object(TOOLS.subprocess, "call", return_value=23) as launched:
                    self.assertEqual(23, TOOLS.main())
                self.assertEqual([expected, "--version"], launched.call_args.args[0])
                if gui_flag:
                    self.assertEqual(str(sdk / "plugins"), launched.call_args.kwargs["env"]["QT_PLUGIN_PATH"])

    def test_gui_candidate_workflow_explicitly_installs_and_checks_gui(self) -> None:
        workflow = (ROOT / ".github/workflows/chess-calibration.yml").read_text()
        self.assertIn("scripts/setup-chess.sh --cutechess-gui", workflow)
        self.assertIn("chess_tools.py check --tool cutechess --cutechess-gui", workflow)
        self.assertIn('tee "$output/cutechess-gui-readiness.json"', workflow)

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
        scratch = Path(os.environ.get("TMPDIR") or os.environ.get("RUNNER_TEMP") or "/build/laplace/work")
        cache = Path(os.environ.get("LAPLACE_CHESS_PGN_TEST_CACHE") or scratch / "chess-pgn-provider-test-cache")
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



class MeasuredChessProfiles(unittest.TestCase):
    """Execute profile persistence, override, stale-evidence and UCI controls."""

    def setUp(self):
        import copy
        import time
        import chess_profiles
        self.copy = copy.deepcopy
        self.P = chess_profiles
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.prefix = self.root / "chess"
        self.prefix.mkdir()
        self.host = {key: None for key in self.P.HOST_FIELDS}
        self.host.update({
            "observed_at_unix": time.time() - 10,
            "kernel": "fixture-kernel", "machine": "x86_64",
            "cpu_models": ["fixture-cpu"], "cpu_flags": ["sse2 avx2"],
            "affinity": list(range(8)), "visible_logical_cpus": 8,
            "visible_physical_cores": 4, "topology": [{"cpu": 0, "core": "0"}],
            "numa": [{"node": "node0", "affinity_cpus": list(range(8))}],
            "memory_total_bytes": 32768 * self.P.MIB,
            "effective_cpu_equivalents": 8.0,
            "effective_memory_headroom_bytes": 8192 * self.P.MIB,
            "cgroup_visibility": "fixture-visible", "transparent_hugepage_policy": "[madvise]",
            "process_identity": {"consistent": True, "runtime_pid": 1, "proc_self_pid": 1},
            "cgroups": [{"path": "/fixture/runner-a", "cpu.max": "800000 100000",
                        "cpu.weight": "100", "cpuset.cpus.effective": "0-7",
                        "cpuset.mems.effective": "0", "memory.max": "max", "memory.high": "max"}]})
        self.installed = {"tools": {}}
        for name in ("stockfish", "cutechess"):
            source = self.root / name
            (source / "src").mkdir(parents=True)
            executable = source / "src" / ("stockfish" if name == "stockfish" else "cutechess-cli")
            executable.write_text(name)
            self.installed["tools"][name] = {
                "source": str(source), "revision": name + "-revision",
                "source_archive_sha256": name + "-archive",
                "executable": str(executable), "sha256": self.P.tools.digest(executable),
                "checks": {"network": "nn-fixture.nnue", "network_sha256": "a" * 64}}
        self.installed["tools"]["cutechess"]["qt_prefix"] = str(self.root / "qt")
        self.report = {
            "schema": "laplace.chess-benchmark-receipt/v2", "completion": "completed",
            "host": self.copy(self.host), "host_after": self.copy(self.host),
            "tool_identity": self.copy(self.installed), "stability_failures": [],
            "benchmark_implementation_sha256": self.P.tools.digest(Path(self.P.bench.__file__)),
            "profile_contract_sha256": self.P.tools.digest(ROOT / "contracts/chess-benchmark.json"),
            "resource_plan": {"cpu_budget": 8, "memory_mib": 8192,
                              "resident_engine_overhead_estimate_mib": 64, "memory_margin_mib": 128},
            "uci_options": {"Threads": {"min": "1", "max": "1024"},
                            "Hash": {"min": "1", "max": "33554432"}},
            "profile": {"games": 8},
            "game_workload": {"mode": "complete-legal-games", "max_moves": None,
                              "time_control": "60", "search_limit": {"type": "depth", "depth": 8}},
            "stockfish_samples": [], "cutechess_samples": []}
        for name in ("stockfish", "cutechess"):
            configs = ([{"threads": 1, "hash_mib": 16}, {"threads": 3, "hash_mib": 96}]
                       if name == "stockfish" else
                       [{"threads": 1, "hash_mib": 16, "concurrency": 1},
                        {"threads": 2, "hash_mib": 32, "concurrency": 3}])
            for index, configuration in enumerate(configs):
                for repetition in range(2):
                    sample = {"configuration": configuration, "warmup": False,
                              "completion": "completed", "wall_seconds": 2.0 / (index + 1),
                              "user_cpu_seconds": 0.5, "system_cpu_seconds": 0.1,
                              "sampled_peak": {"rss_bytes": 10 * self.P.MIB}}
                    if name == "stockfish":
                        sample["wall_nodes_per_second"] = 1000.0 * (index + 1)
                    else:
                        sample.update({"normal_completed_games_per_second": 4.0 * (index + 1),
                                       "games": 8, "normal_completed_games": 8,
                                       "capped_diagnostic_games": 0, "plies": 400})
                    self.report[name + "_samples"].append(sample)
        files = {item["executable"]: item["sha256"] for item in self.installed["tools"].values()}
        self.report.update({"executable_sha256_before": files, "executable_sha256_after": files})
        self.report["recommendations"] = self.P.bench.recommendations(
            self.P.bench.aggregates(self.report["stockfish_samples"],
                                   "median_wall_nodes_per_second", "wall_nodes_per_second"),
            self.P.bench.aggregates(self.report["cutechess_samples"],
                                   "median_normal_completed_games_per_second",
                                   "normal_completed_games_per_second"),
            self.report["game_workload"])
        self.receipt = self.root / "receipt.json"
        self.receipt.write_bytes(self.P.encoded(self.report))

    def tearDown(self):
        self.temporary.cleanup()

    def activate(self):
        with patch.object(self.P, "observed", return_value=(self.installed, self.host)):
            return self.P.activate(self.prefix, self.receipt, self.P.tools.digest(self.receipt))

    def profile(self, mode):
        self.activate()
        with patch.object(self.P, "observed", return_value=(self.installed, self.host)):
            return self.P.active(self.prefix, mode)[0]

    def test_completed_measurement_persists_and_replays_without_runner_temp(self):
        result = self.activate()
        original = self.receipt.read_bytes()
        self.receipt.unlink()
        archived = self.prefix / "calibrations" / result["calibration_sha256"] / "receipt.json"
        self.assertEqual(archived.read_bytes(), original)
        moved_host = self.copy(self.host)
        moved_host["observed_at_unix"] += 5
        moved_host["process_identity"]["runtime_pid"] = 999
        moved_host["process_identity"]["proc_self_pid"] = 999
        moved_host["cgroups"][0]["path"] = "/fixture/runner-b"
        moved_host["cgroups"][0]["memory.current"] = "100"
        with patch.object(self.P, "observed", return_value=(self.installed, moved_host)):
            analysis, _, _, _ = self.P.active(self.prefix, "analysis")
            games, _, _, _ = self.P.active(self.prefix, "games")
        self.assertEqual(analysis["configuration"], {"threads": 3, "hash_mib": 96})
        self.assertEqual(games["configuration"], {"threads": 2, "hash_mib": 32, "concurrency": 3})
        self.assertFalse(analysis["database_recording_capacity_measured"])
        self.assertFalse(games["playing_strength_inferred"])

    def test_rejected_game_grant_cannot_publish_an_analysis_selection(self):
        constrained = self.copy(self.host)
        # Analysis fits (288 MiB), but the selected paired game pool does not.
        constrained["effective_memory_headroom_bytes"] = 300 * self.P.MIB
        with patch.object(self.P, "observed", return_value=(self.installed, constrained)):
            with self.assertRaisesRegex(self.P.tools.ChessToolError, "memory grant"):
                self.P.activate(self.prefix, self.receipt, self.P.tools.digest(self.receipt))
        self.assertFalse((self.prefix / "profiles" / "active-analysis.json").exists())
        self.assertFalse((self.prefix / "profiles" / "active-games.json").exists())

    def test_receipt_digest_completion_samples_and_staleness_are_enforced(self):
        with self.assertRaisesRegex(self.P.tools.ChessToolError, "SHA256"):
            self.P.activate(self.prefix, self.receipt, "0" * 64)
        mutants = [
            ("completion", "failed"),
            ("stability_failures", ["changed"]),
            ("stockfish_override", {"executable": "/unproven"}),
        ]
        for key, value in mutants:
            with self.subTest(key=key):
                report = self.copy(self.report)
                report[key] = value
                with self.assertRaises(self.P.tools.ChessToolError):
                    self.P.validate(report, self.installed, self.host, 3600)
        for mutation in ("age", "recommendation", "diagnostic", "missing-sample", "NNUE", "binary", "source"):
            with self.subTest(mutation=mutation):
                report = self.copy(self.report)
                installed = self.copy(self.installed)
                if mutation == "age":
                    report["host_after"]["observed_at_unix"] -= 1000000
                elif mutation == "recommendation":
                    report["recommendations"]["single_engine_fixed_depth_suite"]["configuration"]["threads"] = 7
                elif mutation == "diagnostic":
                    report["game_workload"]["mode"] = "diagnostic"
                elif mutation == "missing-sample":
                    report["stockfish_samples"].pop()
                elif mutation == "NNUE":
                    installed["tools"]["stockfish"]["checks"]["network_sha256"] = "b" * 64
                elif mutation == "binary":
                    installed["tools"]["stockfish"]["sha256"] = "b" * 64
                else:
                    installed["tools"]["stockfish"]["revision"] = "new-source"
                with self.assertRaises(self.P.tools.ChessToolError):
                    self.P.validate(report, installed, self.host, 3600)

    def test_changed_machine_resource_controls_and_reduced_headroom_are_rejected(self):
        self.activate()
        for mutation in ("cpu", "affinity", "kernel", "quota", "memory"):
            with self.subTest(mutation=mutation):
                host = self.copy(self.host)
                if mutation == "cpu":
                    host["cpu_models"] = ["different-cpu"]
                elif mutation == "affinity":
                    host["affinity"] = [0]
                elif mutation == "kernel":
                    host["kernel"] = "new-kernel"
                elif mutation == "quota":
                    host["cgroups"][0]["cpu.max"] = "100000 100000"
                else:
                    host["effective_memory_headroom_bytes"] = 1
                with patch.object(self.P, "observed", return_value=(self.installed, host)):
                    with self.assertRaises(self.P.tools.ChessToolError):
                        self.P.active(self.prefix, "analysis")

    def test_mutated_archived_receipt_and_selected_profile_are_rejected(self):
        selected = self.activate()["profiles"]["analysis"]
        path = Path(selected["path"])
        original = path.read_bytes()
        path.write_bytes(original + b" ")
        with self.assertRaisesRegex(self.P.tools.ChessToolError, "SHA256"):
            self.P.active(self.prefix, "analysis")
        path.write_bytes(original)
        profile = json.loads(original)
        receipt = Path(profile["calibration"]["path"])
        receipt.write_bytes(receipt.read_bytes() + b" ")
        with self.assertRaisesRegex(self.P.tools.ChessToolError, "SHA256"):
            self.P.active(self.prefix, "analysis")

    def test_uci_explicit_overrides_win_and_unbounded_or_changed_network_refuse(self):
        profile = self.profile("analysis")
        network = {"network": "nn-fixture.nnue", "path": "/verified/nn-fixture.nnue"}
        caller = "setoption name Threads value 1\nsetoption name Hash value 16\nposition startpos\ngo depth 3\nquit\n"
        commands, configuration, _ = self.P.uci_plan(profile, self.report, self.host, caller, network)
        self.assertEqual(commands[-5:], caller.splitlines())
        self.assertEqual(configuration["effective_final"], {"threads": 1, "hash_mib": 16})
        self.assertIn("setoption name Threads value 3", commands)
        for text in ("setoption name Threads value 99\nquit\n",
                     "setoption name Hash value 999999\nquit\n",
                     "setoption name EvalFile value /other/nn-fixture.nnue\nquit\n",
                     "go infinite\nquit\n", "quit\nposition startpos\n"):
            with self.subTest(text=text), self.assertRaises(self.P.tools.ChessToolError):
                self.P.uci_plan(profile, self.report, self.host, text, network)

    def test_cutechess_common_and_per_engine_options_override_defaults_directly(self):
        profile = self.profile("games")
        extra = ["-each", "option.Threads=1", "option.Hash=24",
                 "-engine", "name=First", "option.Hash=48",
                 "-engine", "name=Second", "-concurrency", "2", "-games", "2"]
        command, configuration, _ = self.P.cutechess_plan(
            profile, self.report, self.host, self.installed, extra, self.root)
        sf = self.installed["tools"]["stockfish"]["executable"]
        self.assertEqual(command[0], self.installed["tools"]["cutechess"]["executable"])
        self.assertEqual(command.count("cmd=" + sf), 2)
        self.assertNotIn("-each", command)
        self.assertEqual(configuration["concurrency"], 2)
        self.assertEqual([item["option.Hash"] for item in configuration["engines"]], ["48", "24"])
        self.assertEqual([item["option.Threads"] for item in configuration["engines"]], ["1", "1"])
        self.assertEqual(command[command.index("-games") + 1], "2")
        for bad in (["-concurrency", "99"], ["-each", "option.Threads=99"],
                    ["-engine", "cmd=/other", "-engine", "name=Second"],
                    ["-each", "initstr=setoption name Threads value 99"],
                    ["-each", "ponder=true"]):
            with self.subTest(bad=bad), self.assertRaises(self.P.tools.ChessToolError):
                self.P.cutechess_plan(profile, self.report, self.host, self.installed, bad, self.root)

    def test_actual_direct_uci_process_receives_defaults_then_overrides_and_finishes_search(self):
        profile = self.profile("analysis")
        program = self.root / "uci-fixture.py"
        program.write_text(
            "import sys,time\n"
            "settings={}\n"
            "for line in sys.stdin:\n"
            " line=line.rstrip('\\n'); print('observed:'+line,flush=True)\n"
            " if line=='uci': print('uciok',flush=True)\n"
            " elif line=='isready': print('readyok',flush=True)\n"
            " elif line.startswith('setoption name '):\n"
            "  key,_,value=line[15:].partition(' value '); settings[key]=value\n"
            " elif line.startswith('go '):\n"
            "  assert settings['Threads']=='1' and settings['Hash']=='16'\n"
            "  time.sleep(0.05); print('bestmove e2e4',flush=True)\n"
            " elif line=='quit': break\n")
        caller = "setoption name Threads value 1\nsetoption name Hash value 16\nposition startpos\ngo depth 3\nquit\n"
        commands, _, grant = self.P.uci_plan(profile, self.report, self.host, caller,
                                             {"network": "nn-fixture.nnue", "path": "/verified/nn-fixture.nnue"})
        command = [sys.executable, str(program)]
        result = self.P.execute_uci(command, commands, self.root / "uci.log", self.host,
                                    10, grant["memory_mib"] * self.P.MIB, os.environ.copy())
        self.assertEqual(result["command"], command)
        self.assertEqual(result["completion"], "completed")
        transcript = (self.root / "uci.log").read_text()
        self.assertLess(transcript.index("Threads value 3"), transcript.index("Threads value 1"))
        self.assertLess(transcript.index("bestmove e2e4"), transcript.index("observed:quit"))
        # Deliberately remove the caller override: the actual child asserts the
        # final option value and the protocol owner must report failure.
        broken = [line for line in commands if line != "setoption name Threads value 1"]
        with self.assertRaises(self.P.tools.ChessToolError):
            self.P.execute_uci(command, broken, self.root / "broken.log", self.host,
                               10, grant["memory_mib"] * self.P.MIB, os.environ.copy())

    def test_canonical_entrypoint_requires_and_forwards_explicit_profile_arguments(self):
        with patch.object(self.P, "activate", return_value={"profiles": {}}) as activate, \
             patch.object(TOOLS, "configuration", return_value=({}, {})), \
             patch.object(sys, "argv", ["chess_tools.py", "activate-profile",
                 "--prefix", str(self.prefix), "--calibration-receipt", str(self.receipt),
                 "--calibration-sha256", "a" * 64, "--profile-mode", "analysis"]), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(TOOLS.main(), 0)
        self.assertEqual(activate.call_args.args[:4],
                         (self.prefix, self.receipt, "a" * 64, "analysis"))
        with patch.object(self.P, "run", return_value=7) as run, \
             patch.object(TOOLS, "configuration", return_value=({}, {})), \
             patch.object(sys, "argv", ["chess_tools.py", "run-profile",
                 "--prefix", str(self.prefix), "--profile-mode", "games",
                 "--profile-output", str(self.root / "run"), "--", "-games", "2"]):
            self.assertEqual(TOOLS.main(), 7)
        self.assertEqual(run.call_args.args[4], ["-games", "2"])

if __name__ == "__main__":
    unittest.main()
