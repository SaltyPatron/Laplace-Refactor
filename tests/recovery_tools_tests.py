#!/usr/bin/env python3
"""Behavior and negative-control tests for recovery evidence tooling."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_tool(name: str) -> types.ModuleType:
    path = ROOT / "tools" / "recovery" / name
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SHELL = load_tool("inventory-claude-shell-mutations.py")
SQL = load_tool("inventory-claude-sql-activity.py")
CORPORA = load_tool("inventory-claude-corpora.py")
DATABASE_CORPORA = load_tool("inventory-claude-database-corpora.py")
EVIDENCE_INDEX = load_tool("index-claude-evidence.py")
DESTRUCTIVE_SHELL = load_tool("index-claude-destructive-shell-actions.py")
FILE_HISTORY_CORPORA = load_tool("index-claude-file-history-corpora.py")
DESTRUCTIVE_STATE = load_tool("correlate-claude-destructive-state.py")


class ShellMutationRecoveryTests(unittest.TestCase):
    def test_quoted_body_resolves_assignment_directory_and_target(self) -> None:
        command = (
            "OUT=/tmp/audit\n"
            "cd /home/ahart/Projects/Laplace && cat > \"$OUT/probe.sql\" <<'SQL'\n"
            "SELECT 4;\n"
            "SQL\n"
        )
        documents, _ = SHELL.extract_heredocs(command, "/home/ahart")
        self.assertEqual(1, len(documents))
        document = documents[0]
        self.assertTrue(document["execution_body_exact"])
        self.assertEqual(b"SELECT 4;\n", document["delivered_body"])
        self.assertEqual("/tmp/audit/probe.sql", document["resolved_target"])
        self.assertEqual("replace", document["sink"]["mode"])

    def test_unquoted_body_is_not_claimed_as_executed_bytes(self) -> None:
        command = "cat > /tmp/value <<EOF\n$VALUE\nEOF\n"
        documents, _ = SHELL.extract_heredocs(command, "/tmp")
        self.assertFalse(documents[0]["execution_body_exact"])
        self.assertIsNone(documents[0]["delivered_body"])
        self.assertEqual(b"$VALUE\n", documents[0]["source_body"])

    def test_tab_stripping_matches_shell_delivery(self) -> None:
        command = "cat > /tmp/value <<-'EOF'\n\talpha\n\t\tbeta\nEOF\n"
        documents, _ = SHELL.extract_heredocs(command, "/tmp")
        self.assertEqual(b"alpha\nbeta\n", documents[0]["delivered_body"])

    def test_content_address_store_detects_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            objects = pathlib.Path(directory)
            digest = SHELL.store_object(objects, b"expected")
            (objects / digest).write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError, "content-address collision"):
                SHELL.store_object(objects, b"expected")

    def test_worktree_target_maps_to_repository_path(self) -> None:
        repository = pathlib.Path("/home/ahart/Projects/Laplace")
        target = repository / ".worktrees" / "worker" / "app" / "Probe.cs"
        self.assertEqual(
            str(repository / "app" / "Probe.cs"),
            SHELL.canonical_repository_path(str(target), repository),
        )


class DestructiveShellIndexTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repository = pathlib.Path("/home/ahart/Projects/Laplace")

    def classify(self, command: str) -> list[dict[str, object]]:
        records, error = DESTRUCTIVE_SHELL.invocation_records(
            command, str(self.repository), self.repository
        )
        self.assertEqual("", error)
        return records

    def test_search_argument_is_not_treated_as_executable_action(self) -> None:
        self.assertEqual([], self.classify("rg -n 'git reset --hard|rm -rf' ."))

    def test_inert_heredoc_body_is_not_treated_as_executable_action(self) -> None:
        command = "cat > report.txt <<'EOF'\nrm -rf /\ngit reset --hard\nEOF\n"
        self.assertEqual([], self.classify(command))

    def test_shell_heredoc_body_is_executable(self) -> None:
        records = self.classify("bash <<'EOF'\ngit clean -fd\nEOF\n")
        self.assertEqual(["git-untracked-delete"], [item["category"] for item in records])
        self.assertEqual("nested:shell-heredoc-source", records[0]["origin"])

    def test_repository_and_scratch_deletes_remain_distinct(self) -> None:
        records = self.classify("rm -rf tests/probe /tmp/claude-1000/probe")
        classes = [item["target_class"] for item in records[0]["targets"]]
        self.assertEqual(["repository-worktree", "claude-scratch"], classes)

    def test_git_reset_revision_is_subject_not_filesystem_target(self) -> None:
        records = self.classify("git reset --hard origin/main")
        self.assertEqual("git-worktree-reset", records[0]["category"])
        self.assertEqual(["origin/main"], [item["expanded"] for item in records[0]["subjects"]])
        self.assertEqual([], records[0]["targets"])

    def test_nested_xargs_git_checkout_is_recognized(self) -> None:
        records = self.classify("printf '%s\\n' docs/a.md | xargs git checkout --")
        self.assertEqual("git-path-overwrite", records[0]["category"])
        self.assertEqual("nested:xargs", records[0]["origin"])

    def test_force_ref_update_is_not_mislabeled_as_delete(self) -> None:
        records = self.classify("git branch -f main origin/main")
        self.assertEqual("git-ref-force-update", records[0]["category"])
        self.assertEqual(
            ["main", "origin/main"],
            [item["expanded"] for item in records[0]["subjects"]],
        )

    def test_redirection_descriptors_are_not_targets_or_ref_names(self) -> None:
        records = self.classify("git branch -D probe 2>&1; rm -f file 2>/dev/null")
        self.assertEqual(
            ["probe"], [item["expanded"] for item in records[0]["subjects"]]
        )
        self.assertEqual(["file"], [item["token"] for item in records[1]["targets"]])

    def test_dry_run_clean_is_not_a_destructive_action(self) -> None:
        self.assertEqual([], self.classify("git clean -fdn"))

    def test_line_recovery_ignores_inert_multiline_text_and_finds_action(self) -> None:
        command = "printf '%s' 'unterminated text\nrm -rf inert\nrm -f actual"
        recovered, unresolved = DESTRUCTIVE_SHELL.recover_invocations_by_logical_line(
            command, str(self.repository), self.repository
        )
        self.assertEqual([], recovered)
        self.assertEqual([], unresolved)

    def test_powershell_quoted_action_text_is_inert(self) -> None:
        records, error = DESTRUCTIVE_SHELL.powershell_invocation_records(
            'Write-Output "git reset --hard; Remove-Item C:\\repo"',
            "D:\\Repositories\\Laplace",
            self.repository,
        )
        self.assertEqual("", error)
        self.assertEqual([], records)

    def test_powershell_delete_and_force_ref_update_are_recognized(self) -> None:
        command = (
            "$p = 'D:\\Temp\\probe'; "
            "if (Test-Path $p) { Remove-Item -LiteralPath $p -Recurse -Force }; "
            "cd D:\\Repositories\\Laplace; git branch -f main origin/main"
        )
        records, error = DESTRUCTIVE_SHELL.powershell_invocation_records(
            command, "D:\\Repositories\\Laplace", self.repository
        )
        self.assertEqual("", error)
        self.assertEqual(
            ["powershell-filesystem-delete", "git-ref-force-update"],
            [item["category"] for item in records],
        )
        self.assertEqual("external-windows", records[0]["targets"][0]["target_class"])

    def test_powershell_here_string_is_data_but_its_writer_is_action(self) -> None:
        command = (
            "Set-Content -LiteralPath 'C:\\Temp\\plan.md' -Value @'\n"
            "Remove-Item C:\\repository\n"
            "'@"
        )
        records, error = DESTRUCTIVE_SHELL.powershell_invocation_records(
            command, "D:\\Repositories\\Laplace", self.repository
        )
        self.assertEqual("", error)
        self.assertEqual(["powershell-filesystem-overwrite"], [item["category"] for item in records])
        self.assertEqual("C:\\Temp\\plan.md", records[0]["targets"][0]["resolved"])

        command = "rm -f actual\necho 'unterminated\n"
        recovered, unresolved = DESTRUCTIVE_SHELL.recover_invocations_by_logical_line(
            command, str(self.repository), self.repository
        )
        self.assertEqual(["actual"], [item["token"] for item in recovered[0]["targets"]])
        self.assertEqual([], unresolved)

    def test_unresolved_variable_and_glob_are_not_resolved_as_exact_paths(self) -> None:
        records = self.classify("rm -rf \"$UNKNOWN/*.tmp\"")
        target = records[0]["targets"][0]
        self.assertEqual("unresolved", target["target_class"])
        self.assertEqual(["UNKNOWN", "glob"], target["unresolved"])

    def test_cross_corpus_duplicate_content_is_parsed_once(self) -> None:
        events = [
            {
                "type": "assistant",
                "sessionId": "session-a",
                "uuid": "message-call",
                "timestamp": "2026-08-25T00:00:00Z",
                "cwd": "/repository",
                "message": {
                    "role": "assistant",
                    "content": [
                        {
                            "type": "tool_use",
                            "id": "tool-a",
                            "name": "Bash",
                            "input": {"command": "git reset --hard HEAD"},
                        }
                    ],
                },
            },
            {
                "type": "user",
                "sessionId": "session-a",
                "uuid": "message-result",
                "timestamp": "2026-08-25T00:00:01Z",
                "message": {
                    "role": "user",
                    "content": [
                        {
                            "type": "tool_result",
                            "tool_use_id": "tool-a",
                            "is_error": False,
                            "content": "done",
                        }
                    ],
                },
            },
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            log = root / "session.jsonl"
            log.write_bytes(
                b"".join(DESTRUCTIVE_SHELL.canonical_json_bytes(event) + b"\n" for event in events)
            )
            copy = root / "session-copy.jsonl"
            copy.write_bytes(log.read_bytes())
            digest = DESTRUCTIVE_SHELL.sha256_file(log)
            corpora = root / "corpora.json"
            corpora.write_text(
                json.dumps(
                    {
                        "content_groups": [
                            {
                                "bytes": log.stat().st_size,
                                "content_sha256": digest,
                                "corpora": ["one", "two"],
                                "paths": [str(log), str(copy)],
                            }
                        ]
                    }
                )
            )
            repository = root / "repository"
            (repository / ".git").mkdir(parents=True)
            output = root / "index"
            manifest = DESTRUCTIVE_SHELL.build_index(
                types.SimpleNamespace(
                    corpora_manifest=str(corpora),
                    repository=str(repository),
                    output_directory=str(output),
                )
            )
            actions = (output / "destructive-actions.jsonl").read_text().splitlines()
        self.assertEqual(1, manifest["summary"]["content_group_count"])
        self.assertEqual(2, manifest["summary"]["source_occurrence_count"])
        self.assertEqual(1, manifest["summary"]["destructive_tool_action_count"])
        self.assertEqual(1, len(actions))

    def test_source_tamper_is_rejected_without_publication(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            log = root / "session.jsonl"
            log.write_text('{"type":"assistant"}\n')
            corpora = root / "corpora.json"
            corpora.write_text(
                json.dumps(
                    {
                        "content_groups": [
                            {
                                "bytes": log.stat().st_size,
                                "content_sha256": "0" * 64,
                                "corpora": ["fixture"],
                                "paths": [str(log)],
                            }
                        ]
                    }
                )
            )
            repository = root / "repository"
            (repository / ".git").mkdir(parents=True)
            output = root / "index"
            with self.assertRaisesRegex(RuntimeError, "source verification failed"):
                DESTRUCTIVE_SHELL.build_index(
                    types.SimpleNamespace(
                        corpora_manifest=str(corpora),
                        repository=str(repository),
                        output_directory=str(output),
                    )
                )
            self.assertFalse(output.exists())

    def test_result_locator_retains_exact_source_coordinates(self) -> None:
        record = {"uuid": "message-result", "timestamp": "2026-08-25T00:00:00Z"}
        block = {
            "type": "tool_result",
            "tool_use_id": "tool-a",
            "is_error": False,
            "content": "observed",
        }
        result = DESTRUCTIVE_SHELL.result_descriptor(record, block, "a" * 64, 17, 3)
        self.assertEqual("a" * 64, result["source_content_sha256"])
        self.assertEqual(17, result["source_line"])
        self.assertEqual(3, result["block_index"])
        self.assertEqual("tool-a", result["tool_use_id"])


class FileHistoryCorporaIndexTests(unittest.TestCase):
    SESSION_A = "11111111-1111-1111-1111-111111111111"
    SESSION_B = "22222222-2222-2222-2222-222222222222"

    def initialize_repository(self, root: pathlib.Path) -> tuple[pathlib.Path, str]:
        repository = root / "repository"
        repository.mkdir()
        subprocess.run(["git", "init", "-q", str(repository)], check=True)
        subprocess.run(
            ["git", "-C", str(repository), "config", "user.name", "Fixture"], check=True
        )
        subprocess.run(
            [
                "git", "-C", str(repository), "config", "user.email",
                "fixture@example.invalid",
            ],
            check=True,
        )
        (repository / "seed").write_text("seed")
        subprocess.run(["git", "-C", str(repository), "add", "seed"], check=True)
        subprocess.run(
            ["git", "-C", str(repository), "commit", "-q", "-m", "fixture"],
            check=True,
        )
        baseline = subprocess.run(
            ["git", "-C", str(repository), "rev-parse", "HEAD"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        ).stdout.strip()
        return repository, baseline

    def log_bytes(
        self,
        *,
        session: str,
        tracking_path: str = "engine/probe.c",
        backup_name: str | None = "backup@v1",
        delta: bool = False,
    ) -> bytes:
        descriptor = {"backupFileName": backup_name, "version": 1, "backupTime": "2026-08-25T00:00:00Z"}
        if delta:
            record = {
                "type": "file-history-delta",
                "trackingPath": tracking_path,
                "snapshotMessageId": "snapshot",
                "messageId": "delta",
                "backup": descriptor,
            }
        else:
            record = {
                "type": "file-history-snapshot",
                "messageId": "snapshot",
                "snapshot": {
                    "messageId": "snapshot",
                    "timestamp": "2026-08-25T00:00:00Z",
                    "trackedFileBackups": {tracking_path: descriptor},
                },
            }
        return FILE_HISTORY_CORPORA.canonical_json_bytes(record) + b"\n"

    def corpus_group(
        self,
        root: pathlib.Path,
        session: str,
        content: bytes,
        suffix: str = "",
        copy_count: int = 1,
    ) -> dict[str, object]:
        paths = []
        for index in range(copy_count):
            path = (
                root / f"source{suffix}-{index}" / "projects" / "project" /
                f"{session}.jsonl"
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            paths.append(str(path))
        return {
            "bytes": len(content),
            "content_sha256": FILE_HISTORY_CORPORA.sha256_bytes(content),
            "corpora": ["fixture"],
            "paths": paths,
        }

    def archive_binding(
        self,
        root: pathlib.Path,
        label: str,
        bodies: list[tuple[str, str, bytes]],
    ) -> FILE_HISTORY_CORPORA.ArchiveBinding:
        live_root = root / f"live-{label}"
        entries = []
        for session, backup_name, content in bodies:
            relative = pathlib.PurePosixPath(
                ".claude", "file-history", session, backup_name
            )
            body_path = live_root.joinpath(*relative.parts)
            body_path.parent.mkdir(parents=True, exist_ok=True)
            body_path.write_bytes(content)
            entries.append(
                {
                    "kind": "file",
                    "path": relative.as_posix(),
                    "size": len(content),
                    "sha256": FILE_HISTORY_CORPORA.sha256_bytes(content),
                }
            )
        manifest = root / f"{label}-archive.json"
        manifest.write_text(
            json.dumps(
                {
                    "schema": "laplace.gzip-tar-content-inventory/v1",
                    "archive": str(root / f"{label}.tar.gz"),
                    "archive_sha256": "a" * 64,
                    "entries": entries,
                }
            )
        )
        return FILE_HISTORY_CORPORA.ArchiveBinding(manifest, live_root, label)

    def build(
        self,
        root: pathlib.Path,
        groups: list[dict[str, object]],
        bindings: list[FILE_HISTORY_CORPORA.ArchiveBinding],
    ) -> tuple[dict[str, object], list[dict[str, object]], pathlib.Path]:
        corpora = root / "corpora.json"
        corpora.write_text(json.dumps({"content_groups": groups}))
        repository, baseline = self.initialize_repository(root)
        output = root / "index"
        manifest = FILE_HISTORY_CORPORA.build_index(
            types.SimpleNamespace(
                corpora_manifest=str(corpora),
                repository=str(repository),
                baseline=baseline,
                archive_binding=bindings,
                windows_repository_root=["D:\\Repositories\\Laplace"],
                output_directory=str(output),
            )
        )
        references = [
            json.loads(line)
            for line in (output / "file-history-references.jsonl").read_text().splitlines()
        ]
        return manifest, references, output

    def test_same_backup_name_in_distinct_sessions_does_not_merge(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            group_a = self.corpus_group(root, self.SESSION_A, self.log_bytes(session=self.SESSION_A), "a")
            group_b = self.corpus_group(root, self.SESSION_B, self.log_bytes(session=self.SESSION_B), "b")
            binding = self.archive_binding(
                root,
                "archive",
                [
                    (self.SESSION_A, "backup@v1", b"content-a"),
                    (self.SESSION_B, "backup@v1", b"content-b"),
                ],
            )
            manifest, references, _ = self.build(root, [group_a, group_b], [binding])
        self.assertEqual(2, len(references))
        self.assertEqual(2, len({item["reference_id"] for item in references}))
        self.assertEqual(2, len({item["content_sha256_variants"][0] for item in references}))
        self.assertEqual(1, manifest["summary"]["session_scoped_backup_name_collision_count"])

    def test_repeated_source_occurrence_does_not_duplicate_observation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            content = self.log_bytes(session=self.SESSION_A)
            group = self.corpus_group(root, self.SESSION_A, content, copy_count=2)
            binding = self.archive_binding(
                root, "archive", [(self.SESSION_A, "backup@v1", b"body")]
            )
            _, references, _ = self.build(root, [group], [binding])
        self.assertEqual(1, len(references))
        self.assertEqual(2, len(references[0]["source_occurrences"]))
        self.assertEqual(1, len(references[0]["observations"]))

    def test_distinct_transcript_copies_share_one_canonical_state_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            first = self.log_bytes(session=self.SESSION_A)
            second = first.replace(b'"messageId":"snapshot"', b'"messageId":"snapshot-two"')
            groups = [
                self.corpus_group(root, self.SESSION_A, first, "first"),
                self.corpus_group(root, self.SESSION_A, second, "second"),
            ]
            binding = self.archive_binding(
                root, "archive", [(self.SESSION_A, "backup@v1", b"body")]
            )
            manifest, references, output = self.build(root, groups, [binding])
            states = [
                json.loads(line)
                for line in (output / "state-versions.jsonl").read_text().splitlines()
            ]
        self.assertEqual(2, len(references))
        self.assertEqual(1, len(states))
        self.assertEqual(2, len(states[0]["reference_ids"]))
        self.assertEqual(1, manifest["summary"]["state_version_count"])

    def test_null_backup_reference_is_published_explicitly(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            content = self.log_bytes(session=self.SESSION_A, backup_name=None, delta=True)
            group = self.corpus_group(root, self.SESSION_A, content)
            manifest, references, _ = self.build(root, [group], [])
        self.assertEqual("no-backup-reference", references[0]["resolution"])
        self.assertIsNone(references[0]["backup_file_name"])
        self.assertEqual(1, manifest["summary"]["null_backup_reference_count"])

    def test_archive_copies_with_same_body_resolve_as_repeated_content(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            content = self.log_bytes(session=self.SESSION_A)
            group = self.corpus_group(root, self.SESSION_A, content)
            bindings = [
                self.archive_binding(root, "one", [(self.SESSION_A, "backup@v1", b"body")]),
                self.archive_binding(root, "two", [(self.SESSION_A, "backup@v1", b"body")]),
            ]
            _, references, _ = self.build(root, [group], bindings)
        self.assertEqual("repeated-identical-content", references[0]["resolution"])
        self.assertEqual(2, len(references[0]["archive_candidates"]))
        self.assertEqual(1, len(references[0]["content_sha256_variants"]))

    def test_conflicting_archive_bodies_remain_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            content = self.log_bytes(session=self.SESSION_A)
            group = self.corpus_group(root, self.SESSION_A, content)
            bindings = [
                self.archive_binding(root, "one", [(self.SESSION_A, "backup@v1", b"one")]),
                self.archive_binding(root, "two", [(self.SESSION_A, "backup@v1", b"two")]),
            ]
            _, references, output = self.build(root, [group], bindings)
            object_count = len(list((output / "objects").iterdir()))
        self.assertEqual("conflicting-content", references[0]["resolution"])
        self.assertEqual(2, len(references[0]["content_sha256_variants"]))
        self.assertEqual(2, object_count)

    def test_source_tamper_aborts_without_publication(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            content = self.log_bytes(session=self.SESSION_A)
            group = self.corpus_group(root, self.SESSION_A, content)
            group["content_sha256"] = "0" * 64
            corpora = root / "corpora.json"
            corpora.write_text(json.dumps({"content_groups": [group]}))
            repository, baseline = self.initialize_repository(root)
            output = root / "index"
            with self.assertRaisesRegex(RuntimeError, "source verification failed"):
                FILE_HISTORY_CORPORA.build_index(
                    types.SimpleNamespace(
                        corpora_manifest=str(corpora),
                        repository=str(repository),
                        baseline=baseline,
                        archive_binding=[],
                        windows_repository_root=[],
                        output_directory=str(output),
                    )
                )
            self.assertFalse(output.exists())


class DestructiveStateCorrelationTests(unittest.TestCase):
    def test_null_semantics_require_all_source_markers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "claude"
            executable.write_bytes(
                b"\0".join(marker for _, marker, _ in DESTRUCTIVE_STATE.NULL_SEMANTIC_MARKERS)
            )
            receipt = DESTRUCTIVE_STATE.verify_null_semantics(executable)
            self.assertEqual(
                len(DESTRUCTIVE_STATE.NULL_SEMANTIC_MARKERS),
                len(receipt["matched_markers"]),
            )
            executable.write_bytes(b"missing required source markers")
            with self.assertRaisesRegex(RuntimeError, "required null-state marker"):
                DESTRUCTIVE_STATE.verify_null_semantics(executable)

    def test_repeated_snapshot_observation_sets_later_state_time(self) -> None:
        state = {
            "state_version_id": "state",
            "descriptor_variants": [
                {"backupTime": "2026-08-25T00:00:01Z"}
            ],
            "observations": [
                {
                    "kind": "snapshot",
                    "snapshot_timestamp": "2026-08-25T00:00:09Z",
                    "source_content_sha256": "a" * 64,
                    "source_line": 7,
                }
            ],
        }
        points = DESTRUCTIVE_STATE.state_observation_points(state)
        self.assertEqual(1, len(points))
        self.assertEqual(
            DESTRUCTIVE_STATE.epoch_seconds("2026-08-25T00:00:09Z"),
            points[0]["_time"],
        )
        self.assertEqual(7, points[0]["_observation"]["source_line"])

    def test_worktree_target_maps_to_canonical_repository_path(self) -> None:
        repository = pathlib.Path("/home/ahart/Projects/Laplace")
        self.assertEqual(
            "engine/probe.c",
            DESTRUCTIVE_STATE.canonical_repository_path(
                "/home/ahart/Projects/Laplace/.worktrees/worker/engine/probe.c",
                repository,
                ["D:\\Repositories\\Laplace"],
            ),
        )
        self.assertEqual(
            "engine/probe.c",
            DESTRUCTIVE_STATE.canonical_repository_path(
                "D:\\Repositories\\Laplace\\engine\\probe.c",
                repository,
                ["D:\\Repositories\\Laplace"],
            ),
        )

    def test_history_neighbor_reports_non_git_body_without_claiming_loss(self) -> None:
        states = [
            {
                "state_version_id": "state-before",
                "_time": 10.0,
                "version": 1,
                "backup_file_name": "body@v1",
                "resolution": "single-content",
                "content_sha256_variants": ["a" * 64],
            },
            {
                "state_version_id": "state-after",
                "_time": 30.0,
                "version": 2,
                "backup_file_name": None,
                "resolution": "no-backup-reference",
                "content_sha256_variants": [],
            },
        ]
        contents = {
            "a" * 64: {
                "git_object_present": False,
                "reachable_from_any_ref": False,
                "reachable_from_baseline": False,
            }
        }
        result = DESTRUCTIVE_STATE.history_neighbors(states, 20.0, 20.0, contents)
        self.assertTrue(result["nearest_prior"]["body_absent_from_git_object_database"])
        self.assertEqual("state-after", result["nearest_following"]["state_version_id"])
        self.assertNotIn("unique_state_loss", result)

    def test_reset_reflog_corroboration_requires_time_ref_and_operation(self) -> None:
        call = {"timestamp": "2026-08-25T00:00:00Z"}
        results = [{"timestamp": "2026-08-25T00:00:05Z"}]
        timestamp = int(DESTRUCTIVE_STATE.epoch_seconds("2026-08-25T00:00:02Z"))
        matching = {
            "valid": True,
            "event_id": "match",
            "ref_name": "worktrees/worker/HEAD",
            "timestamp": timestamp,
            "source_line": 1,
            "message": "reset: moving to origin/main",
            "object_id_changed": True,
        }
        wrong_ref = {**matching, "event_id": "wrong-ref", "ref_name": "HEAD"}
        wrong_operation = {**matching, "event_id": "wrong-op", "message": "checkout: move"}
        invocation = {
            "category": "git-worktree-reset",
            "operation": "reset",
            "working_directory": "/home/ahart/Projects/Laplace/.worktrees/worker",
        }
        records = DESTRUCTIVE_STATE.matching_reflog_events(
            invocation,
            call,
            results,
            [matching, wrong_ref, wrong_operation],
            pathlib.Path("/home/ahart/Projects/Laplace"),
        )
        self.assertEqual(["match"], [item["event_id"] for item in records])

    def test_exact_delete_transition_resolves_discard_and_recovery(self) -> None:
        prior_time = 10.0
        following_time = 30.0
        neighbors = {
            "nearest_prior": {
                "observation_time_epoch": prior_time,
                "backup_file_name": "body@v1",
                "content_sha256_variants": ["a" * 64],
                "body_absent_from_git_object_database": True,
            },
            "nearest_following": {
                "observation_time_epoch": following_time,
                "backup_file_name": None,
                "content_sha256_variants": [],
            },
        }
        findings = DESTRUCTIVE_STATE.target_transition(
            "filesystem-delete",
            "tool-reported-success-without-explicit-exit",
            "delete-action",
            "app/Probe.cs",
            neighbors,
            [],
            [],
            True,
        )
        self.assertEqual("observed", findings["actual_state_mutation"]["status"])
        self.assertEqual("observed", findings["discarded_worktree_state"]["status"])
        self.assertEqual("recovered", findings["recoverability"]["status"])
        self.assertEqual(
            "not-observed", findings["unrecoverable_unique_state_loss"]["status"]
        )

    def test_competing_mutator_keeps_transition_unresolved(self) -> None:
        neighbors = {
            "nearest_prior": {
                "observation_time_epoch": 10.0,
                "backup_file_name": "body@v1",
                "content_sha256_variants": ["a" * 64],
                "body_absent_from_git_object_database": True,
            },
            "nearest_following": {
                "observation_time_epoch": 30.0,
                "backup_file_name": None,
                "content_sha256_variants": [],
            },
        }
        calls = [
            {
                "time_epoch": 20.0,
                "tool_action_id": "other-action",
                "mutation_kind": "direct-file",
                "repository_paths": ["app/Probe.cs"],
                "unresolved_mutation_scope": False,
            }
        ]
        findings = DESTRUCTIVE_STATE.target_transition(
            "filesystem-delete",
            "tool-reported-success-without-explicit-exit",
            "delete-action",
            "app/Probe.cs",
            neighbors,
            calls,
            [20.0],
            True,
        )
        self.assertEqual("unresolved", findings["actual_state_mutation"]["status"])
        self.assertEqual(
            1, findings["intervening_operations"]["same_target_mutator_count"]
        )

    def test_unbound_historical_null_semantics_keep_transition_unresolved(self) -> None:
        neighbors = {
            "nearest_prior": {
                "observation_time_epoch": 10.0,
                "backup_file_name": "body@v1",
                "content_sha256_variants": ["a" * 64],
                "body_absent_from_git_object_database": True,
            },
            "nearest_following": {
                "observation_time_epoch": 30.0,
                "backup_file_name": None,
                "content_sha256_variants": [],
            },
        }
        findings = DESTRUCTIVE_STATE.target_transition(
            "filesystem-delete",
            "tool-reported-success-without-explicit-exit",
            "delete-action",
            "app/Probe.cs",
            neighbors,
            [],
            [],
            False,
        )
        self.assertEqual("unresolved", findings["actual_state_mutation"]["status"])
        self.assertFalse(findings["historical_null_semantics_bound"])

    def test_read_only_shell_does_not_block_target_transition(self) -> None:
        profile = DESTRUCTIVE_STATE.shell_profile(
            "Bash",
            {"command": "grep -n public app/Other.cs | head -40"},
            "/home/ahart/Projects/Laplace",
            pathlib.Path("/home/ahart/Projects/Laplace"),
            ["D:\\Repositories\\Laplace"],
        )
        self.assertEqual("none", profile["mutation_kind"])
        self.assertFalse(profile["unresolved_mutation_scope"])


class SqlActivityRecoveryTests(unittest.TestCase):
    def test_combined_psql_command_option_is_recovered(self) -> None:
        tokens = SQL.shell_tokens(
            'timeout 120 psql -h localhost -d laplace -Atc "SELECT 4" 2>&1 | head'
        )
        index = SQL.psql_indices(tokens)[0]
        invocation = tokens[index + 1 : SQL.invocation_end(tokens, index)]
        self.assertEqual(["SELECT 4"], SQL.command_values(invocation))
        self.assertEqual("laplace", SQL.option_value(invocation, "-d", "--dbname"))
        self.assertEqual(120.0, SQL.client_timeout(tokens, index))

    def test_combined_psql_file_option_is_recovered(self) -> None:
        tokens = SQL.shell_tokens("psql -tAf sql/probe.sql -d laplace")
        index = SQL.psql_indices(tokens)[0]
        invocation = tokens[index + 1 : SQL.invocation_end(tokens, index)]
        self.assertEqual(["sql/probe.sql"], SQL.file_values(invocation))

    def test_heredoc_body_is_removed_before_command_tokenization(self) -> None:
        command = (
            "python3 - <<'PY'\n"
            "print('psql is data here')\n"
            "PY\n"
            "psql -d laplace -c 'SELECT 4'\n"
        )
        action = {
            "heredocs": [
                {"status": "complete", "body_start_line": 2, "body_end_line": 2}
            ]
        }
        scrubbed = SQL.command_without_heredoc_bodies(action, command)
        self.assertEqual(1, len(SQL.psql_indices(SQL.shell_tokens(scrubbed))))

    def test_assignment_prefixed_psql_heredoc_is_recovered(self) -> None:
        command = (
            "PGPASSWORD=postgres psql -d laplace <<'SQL'\n"
            "SELECT 4;\n"
            "SQL\n"
        )
        documents, _ = SHELL.extract_heredocs(command, "/tmp")
        with tempfile.TemporaryDirectory() as directory:
            objects = pathlib.Path(directory)
            encoded = []
            for document in documents:
                body = document["delivered_body"]
                digest = DATABASE_CORPORA.store_object(objects, body)
                encoded.append(
                    {
                        key: value
                        for key, value in document.items()
                        if key not in {"source_body", "delivered_body"}
                    }
                    | {"delivered_body_sha256": digest}
                )
            sources = SQL.heredoc_sql_sources(
                {"heredocs": encoded}, command, objects
            )
        self.assertEqual(1, len(sources))
        self.assertEqual("SELECT 4;\n", sources[0]["sql"])
        self.assertTrue(sources[0]["execution_body_exact"])

    def test_variable_invoked_psql_heredoc_is_recovered(self) -> None:
        command = (
            'PSQL="/c/Program Files/PostgreSQL/18/bin/psql.exe"\n'
            '"$PSQL" -d laplace <<\'SQL\'\n'
            "SELECT 4;\n"
            "SQL\n"
        )
        documents, _ = SHELL.extract_heredocs(command, "/tmp")
        with tempfile.TemporaryDirectory() as directory:
            objects = pathlib.Path(directory)
            encoded = []
            for document in documents:
                body = document["delivered_body"]
                digest = DATABASE_CORPORA.store_object(objects, body)
                encoded.append(
                    {
                        key: value
                        for key, value in document.items()
                        if key not in {"source_body", "delivered_body"}
                    }
                    | {"delivered_body_sha256": digest}
                )
            sources = SQL.heredoc_sql_sources(
                {"heredocs": encoded, "tool_name": "Bash"}, command, objects
            )
        self.assertEqual(1, len(sources))
        self.assertEqual("SELECT 4;\n", sources[0]["sql"])

    def test_pipeline_input_skips_environment_assignment(self) -> None:
        tokens = SQL.shell_tokens(
            'printf "SELECT 4" | PGPASSWORD=secret psql -d laplace'
        )
        index = SQL.psql_indices(tokens)[0]
        self.assertTrue(DATABASE_CORPORA.has_pipeline_input(tokens, index))

    def test_client_operation_classification(self) -> None:
        self.assertTrue(DATABASE_CORPORA.is_client_version_or_help(["--version"]))
        self.assertTrue(DATABASE_CORPORA.is_client_version_or_help(["-V"]))
        self.assertTrue(DATABASE_CORPORA.is_client_database_list(["-lqt"]))
        self.assertTrue(DATABASE_CORPORA.is_client_database_list(["--list"]))
        self.assertFalse(DATABASE_CORPORA.is_client_database_list(["-Atc", "SELECT 4"]))

    def test_powershell_continuations_and_windows_executable_are_recovered(self) -> None:
        command = (
            "$env:PGPASSWORD='postgres'; & 'D:\\Postgres\\bin\\psql.exe' `\n"
            "  -d laplace `\n"
            "  -c \"SELECT 4\""
        )
        tokens = SQL.shell_tokens(command, powershell=True)
        index = SQL.psql_indices(tokens)[0]
        invocation = tokens[index + 1 : SQL.invocation_end(tokens, index)]
        self.assertEqual("laplace", SQL.option_value(invocation, "-d", "--dbname"))
        self.assertEqual(["SELECT 4"], SQL.command_values(invocation))

    def test_psql_path_assignment_is_not_an_invocation_but_variable_calls_are(self) -> None:
        tokens = SQL.shell_tokens(
            'PSQL="/c/Program Files/PostgreSQL/18/bin/psql.exe"\n'
            '"$PSQL" -d laplace -c "SELECT 4"\n'
            '"$PSQL" -d laplace -c "SELECT 5"'
        )
        indices = SQL.psql_indices(tokens)
        self.assertEqual(2, len(indices))
        self.assertEqual(
            [["SELECT 4"], ["SELECT 5"]],
            [
                SQL.command_values(tokens[index + 1 : SQL.invocation_end(tokens, index)])
                for index in indices
            ],
        )

    def test_dynamic_psql_variable_name_recovers_execution(self) -> None:
        tokens = SQL.shell_tokens(
            'PSQL=$(command -v psql)\n"$PSQL" -d laplace -Atc "SELECT 4"'
        )
        indices = SQL.psql_indices(tokens)
        self.assertEqual(1, len(indices))
        invocation = tokens[indices[0] + 1 : SQL.invocation_end(tokens, indices[0])]
        self.assertEqual(["SELECT 4"], SQL.command_values(invocation))

    def test_normalization_preserves_query_and_shape_equivalence(self) -> None:
        left = "SELECT count(*) -- note\n FROM laplace.entities WHERE tier = 2;"
        right = " select COUNT(*) from laplace.entities where tier=9 "
        self.assertNotEqual(SQL.normalize_sql(left, False), SQL.normalize_sql(right, False))
        self.assertEqual(SQL.normalize_sql(left, True), SQL.normalize_sql(right, True))

    def test_statement_split_ignores_literal_and_dollar_body_semicolons(self) -> None:
        statements = SQL.split_statements("SELECT ';'; SELECT $$a;b$$; SELECT 4")
        self.assertEqual(3, len(statements))
        self.assertEqual("SELECT $$a;b$$", statements[1])

    def test_statement_facts_expose_server_objects_and_4d_operations(self) -> None:
        normalized = SQL.normalize_sql(
            "SELECT public.ST_AsText(p.coord) FROM laplace.physicalities p "
            "JOIN laplace.entities e ON e.id=p.entity_id",
            False,
        )
        facts = SQL.statement_facts(normalized)
        self.assertEqual(
            ["laplace.entities", "laplace.physicalities"], facts["relations"]
        )
        self.assertIn("public.st_astext", facts["functions"])
        self.assertTrue(facts["has_4d_or_postgis"])


class ClaudeCorporaTests(unittest.TestCase):
    def test_event_log_classification_rejects_jsonl_dataset(self) -> None:
        event = {
            "type": "user",
            "sessionId": "session-a",
            "message": {"role": "user", "content": "inspect"},
        }
        dataset = {"pos": "noun", "forms": ["records"]}
        path = pathlib.Path("projects/Laplace/session.jsonl")
        self.assertTrue(CORPORA.looks_like_claude_log(path, event))
        self.assertFalse(CORPORA.looks_like_claude_log(path, dataset))

    def test_log_analysis_distinguishes_human_tool_result_and_psql_call(self) -> None:
        events = [
            {
                "type": "user",
                "sessionId": "session-a",
                "timestamp": "2026-08-24T00:00:00Z",
                "message": {"role": "user", "content": "inspect"},
            },
            {
                "type": "assistant",
                "sessionId": "session-a",
                "timestamp": "2026-08-24T00:00:01Z",
                "message": {
                    "role": "assistant",
                    "content": [
                        {
                            "type": "tool_use",
                            "name": "Bash",
                            "input": {"command": "psql -d laplace -c 'SELECT 4'"},
                        }
                    ],
                },
            },
            {
                "type": "user",
                "sessionId": "session-a",
                "timestamp": "2026-08-24T00:00:02Z",
                "message": {
                    "role": "user",
                    "content": [{"type": "tool_result", "content": "4"}],
                },
            },
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "session.jsonl"
            path.write_text("".join(json.dumps(event) + "\n" for event in events))
            result = CORPORA.analyze_log(path)
        self.assertEqual(3, result["record_count"])
        self.assertEqual(1, result["human_message_count"])
        self.assertEqual(1, result["tool_result_count"])
        self.assertEqual(1, result["tool_call_counts"]["Bash"])
        self.assertEqual(1, result["psql_bash_call_count"])

    def test_archive_entry_mapping_accepts_archive_root_prefix(self) -> None:
        root = pathlib.Path("/vault/.claude")
        source = root / "projects" / "Laplace" / "session.jsonl"
        entry = {"path": ".claude/projects/Laplace/session.jsonl", "sha256": "a"}
        self.assertEqual(
            entry,
            CORPORA.find_archive_entry({entry["path"]: entry}, root, source),
        )

    def test_pipeline_file_source_preceding_psql_is_recovered(self) -> None:
        tokens = SQL.shell_tokens("sed -n '1,20p' sql/probe.sql | psql -d laplace")
        psql_index = SQL.psql_indices(tokens)[0]
        self.assertEqual(
            ["sql/probe.sql"],
            DATABASE_CORPORA.pipeline_file_sources(tokens, psql_index),
        )

    def test_raw_sql_tool_input_uses_only_named_sql_fields(self) -> None:
        payload = {
            "sql": "SELECT 4",
            "metadata": {"query": "SELECT 5", "description": "SELECT 6"},
        }
        self.assertEqual(
            ["SELECT 4", "SELECT 5"], DATABASE_CORPORA.sql_values(payload)
        )

    def test_psql_input_redirection_is_recovered_as_file_reference(self) -> None:
        tokens = SQL.shell_tokens("psql -d laplace < sql/probe.sql")
        psql_index = SQL.psql_indices(tokens)[0]
        invocation = tokens[psql_index + 1 : SQL.invocation_end(tokens, psql_index)]
        self.assertEqual(
            ["sql/probe.sql"], DATABASE_CORPORA.input_redirection_sources(invocation)
        )


class ClaudeEvidenceIndexTests(unittest.TestCase):
    def test_tool_identity_is_scoped_to_transcript_content(self) -> None:
        self.assertNotEqual(
            EVIDENCE_INDEX.tool_action_id("a" * 64, "tool-reused"),
            EVIDENCE_INDEX.tool_action_id("b" * 64, "tool-reused"),
        )

    def test_repeated_transcript_records_do_not_create_independent_actions(self) -> None:
        call = {"message_id": "call", "tool_name": "Read", "input_sha256": "a" * 64}
        result = {
            "message_id": "result",
            "is_error": False,
            "content_sha256": "b" * 64,
        }
        classification = EVIDENCE_INDEX.classify_tool_records(
            [call, dict(call)], [result, dict(result)]
        )
        self.assertEqual("paired-repeated-records", classification["status"])
        self.assertEqual(1, classification["call_variant_count"])
        self.assertEqual(1, classification["result_variant_count"])
        self.assertFalse(classification["conflicting"])

    def test_conflicting_records_remain_explicit(self) -> None:
        calls = [
            {"message_id": "call-a", "tool_name": "Read", "input_sha256": "a" * 64},
            {"message_id": "call-b", "tool_name": "Read", "input_sha256": "b" * 64},
        ]
        result = {
            "message_id": "result",
            "is_error": False,
            "content_sha256": "c" * 64,
        }
        classification = EVIDENCE_INDEX.classify_tool_records(calls, [result])
        self.assertEqual("conflicting-records", classification["status"])
        self.assertTrue(classification["conflicting"])

    def test_generated_user_notice_is_not_direct_human_testimony(self) -> None:
        blocks = [{"type": "text", "text": "<task-notification>done</task-notification>"}]
        self.assertFalse(EVIDENCE_INDEX.direct_human_message("user", blocks))
        self.assertTrue(
            EVIDENCE_INDEX.direct_human_message(
                "user", [{"type": "text", "text": "implement the complete operation"}]
            )
        )

    def test_unique_abbreviated_commit_reference_resolves(self) -> None:
        commit = "abcdef0123456789abcdef0123456789abcdef01"
        record = EVIDENCE_INDEX.reference_record(
            event_id="event",
            block_index=0,
            origin="message-text",
            kind="git-commit-prefix",
            value="abcdef01",
            git_commits={commit},
            recovery_commits={commit},
        )
        self.assertTrue(record["known_git_commit"])
        self.assertTrue(record["recovered_nonbaseline_commit"])
        self.assertEqual([commit], record["resolved_git_commits"])

    def test_commit_prefix_extraction_requires_commit_context(self) -> None:
        arbitrary = "session 12345678-1234-1234-1234-123456789abc"
        self.assertNotIn(
            ("git-commit-prefix", "12345678"),
            list(EVIDENCE_INDEX.text_references(arbitrary)),
        )
        self.assertIn(
            ("git-commit-prefix", "abcdef01"),
            list(EVIDENCE_INDEX.text_references("commit `abcdef01` changed the path")),
        )

    def test_index_links_tools_and_preserves_unknown_commit_references(self) -> None:
        commit = "a" * 40
        events = [
            {
                "type": "user",
                "sessionId": "session-a",
                "uuid": "message-user",
                "timestamp": "2026-08-24T00:00:00Z",
                "message": {"role": "user", "content": "inspect the implementation"},
            },
            {
                "type": "assistant",
                "sessionId": "session-a",
                "uuid": "message-agent",
                "parentUuid": "message-user",
                "timestamp": "2026-08-24T00:00:01Z",
                "cwd": "/home/ahart/Projects/Laplace",
                "message": {
                    "role": "assistant",
                    "content": [
                        {"type": "text", "text": f"commit {commit} changed `engine/probe.c`"},
                        {
                            "type": "tool_use",
                            "id": "tool-a",
                            "name": "Read",
                            "input": {"file_path": "/home/ahart/Projects/Laplace/engine/probe.c"},
                        },
                    ],
                },
            },
            {
                "type": "user",
                "sessionId": "session-a",
                "uuid": "message-result",
                "parentUuid": "message-agent",
                "timestamp": "2026-08-24T00:00:02Z",
                "message": {
                    "role": "user",
                    "content": [
                        {
                            "type": "tool_result",
                            "tool_use_id": "tool-a",
                            "content": "observed bytes",
                        }
                    ],
                },
            },
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            log = root / "session.jsonl"
            log.write_bytes(b"".join(EVIDENCE_INDEX.canonical_json_bytes(event) + b"\n" for event in events))
            log_digest = EVIDENCE_INDEX.sha256_file(log)
            corpora = root / "corpora.json"
            corpora.write_text(
                json.dumps(
                    {
                        "content_groups": [
                            {
                                "bytes": log.stat().st_size,
                                "content_sha256": log_digest,
                                "corpora": ["fixture"],
                                "paths": [str(log)],
                            }
                        ]
                    }
                )
            )
            git = root / "git.json"
            git.write_text(json.dumps({"commits": [{"commit": commit}]}))
            repository = root / "repository"
            repository.mkdir()
            subprocess.run(["git", "init", "-q", str(repository)], check=True)
            subprocess.run(["git", "-C", str(repository), "config", "user.name", "Fixture"], check=True)
            subprocess.run(
                ["git", "-C", str(repository), "config", "user.email", "fixture@example.invalid"],
                check=True,
            )
            (repository / "probe").write_text("fixture")
            subprocess.run(["git", "-C", str(repository), "add", "probe"], check=True)
            subprocess.run(["git", "-C", str(repository), "commit", "-q", "-m", "fixture"], check=True)
            actual_commit = subprocess.run(
                ["git", "-C", str(repository), "rev-parse", "HEAD"],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
            ).stdout.strip()
            git.write_text(json.dumps({"commits": [{"commit": actual_commit}]}))
            output = root / "index"
            arguments = types.SimpleNamespace(
                corpora_manifest=str(corpora),
                git_manifest=str(git),
                git_repository=str(repository),
                output_directory=str(output),
            )
            manifest = EVIDENCE_INDEX.build_index(arguments)
            actions = [json.loads(line) for line in (output / "tool-actions.jsonl").read_text().splitlines()]
            references = [
                json.loads(line)
                for line in (output / "artifact-references.jsonl").read_text().splitlines()
            ]

        self.assertEqual(3, manifest["summary"]["event_count"])
        self.assertEqual(1, manifest["summary"]["direct_human_message_count"])
        self.assertEqual(1, len(actions))
        self.assertTrue(actions[0]["paired"])
        self.assertEqual("paired-single-records", actions[0]["status"])
        self.assertFalse(actions[0]["conflicting"])
        self.assertTrue(
            any(
                item["kind"] == "git-commit"
                and item["value"] == commit
                and not item["known_git_commit"]
                for item in references
            )
        )
        self.assertTrue(
            any(item["kind"] == "path" and item["origin"] == "tool-input" for item in references)
        )

    def test_source_hash_mismatch_is_rejected_without_publication(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            log = root / "session.jsonl"
            log.write_text('{"type":"user"}\n')
            corpora = root / "corpora.json"
            corpora.write_text(
                json.dumps(
                    {
                        "content_groups": [
                            {
                                "bytes": log.stat().st_size,
                                "content_sha256": "0" * 64,
                                "corpora": ["fixture"],
                                "paths": [str(log)],
                            }
                        ]
                    }
                )
            )
            git = root / "git.json"
            git.write_text('{"commits":[]}')
            repository = root / "repository"
            repository.mkdir()
            subprocess.run(["git", "init", "-q", str(repository)], check=True)
            subprocess.run(["git", "-C", str(repository), "config", "user.name", "Fixture"], check=True)
            subprocess.run(
                ["git", "-C", str(repository), "config", "user.email", "fixture@example.invalid"],
                check=True,
            )
            (repository / "probe").write_text("fixture")
            subprocess.run(["git", "-C", str(repository), "add", "probe"], check=True)
            subprocess.run(["git", "-C", str(repository), "commit", "-q", "-m", "fixture"], check=True)
            output = root / "index"
            arguments = types.SimpleNamespace(
                corpora_manifest=str(corpora),
                git_manifest=str(git),
                git_repository=str(repository),
                output_directory=str(output),
            )
            with self.assertRaisesRegex(RuntimeError, "source verification failed"):
                EVIDENCE_INDEX.build_index(arguments)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
