#!/usr/bin/env python3
"""Execute the actual native PGN source owner and independent syntax controls."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

REPO = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--grammar-lock", type=Path, default=REPO / "dependencies/tree-sitter-grammars.lock.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {"schema": "laplace.pgn-native-source-qualification/v1", "status": "failed",
              "scope": "exact-source-syntax-and-canonical-source-composition",
              "chess_legality_verified": False, "complete_game_verified": False,
              "canonical_chess_line_admission": False, "postgresql_admission": False,
              "recorded_game_rate_measured": False, "observations": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        selection_bytes = (REPO / "dependencies/pgn-provider-selection.json").read_bytes()
        selection = json.loads(selection_bytes)
        receipt_bytes = args.receipt.read_bytes()
        receipt = json.loads(receipt_bytes)
        lock_bytes = args.grammar_lock.read_bytes()
        entries = json.loads(lock_bytes)["repositories"]
        matches = [entry for entry in entries if entry["name"] == selection["source"]["name"]]
        require(len(matches) == 1, "PGN grammar is not uniquely locked")
        grammar = matches[0]
        dependencies = json.loads((REPO / "dependencies/lock.json").read_bytes())
        runtime = dependencies["dependencies"]["tree-sitter"]
        require(receipt["schema"] == "laplace.grammar-provider-build/v1"
                and receipt["phase"] == "source-built"
                and receipt["grammar_name"] == grammar["name"]
                and receipt["exit_code"] == 0, "PGN provider does not have a successful source-build receipt")
        for key in ("upstream", "revision"):
            require(grammar[key] == selection["source"][key], "PGN selection differs from its source lock")
        for key in ("upstream", "revision", "git_archive_sha256"):
            require(receipt["grammar"][key] == grammar[key], "PGN build differs from locked source")
            require(receipt["runtime"][key] == runtime[key], "PGN build differs from locked runtime")
        require(receipt["grammar_lock_sha256"] == sha(lock_bytes), "PGN build used another grammar lock")
        require(receipt["dependency_lock_sha256"] == sha((REPO / "dependencies/lock.json").read_bytes()),
                "PGN build used another dependency lock")
        require(receipt["implementation_sha256"] == sha((REPO / "tools/sources/qualify_grammar.py").read_bytes()),
                "PGN build used another qualification implementation")
        require(receipt["generated_sources"] == grammar["generated_parsers"] + grammar["external_scanners"],
                "PGN build omitted or changed a locked parser/scanner")
        library = Path(receipt["library"]["path"])
        data = library.read_bytes()
        require(sha(data) == receipt["library"]["sha256"] and len(data) == receipt["library"]["byte_count"],
                "PGN shared-object bytes differ from their source build")
        fixture_path = REPO / "tests/fixtures/pgn-provider/manifest.json"
        fixture_bytes = fixture_path.read_bytes()
        fixtures = json.loads(fixture_bytes)
        require(fixtures["schema"] == "laplace.pgn-provider-fixtures/v1"
                and fixtures["scope"] == "syntax-only", "invalid fixture scope")
        require(fixtures["upstream"]["revision"] == grammar["revision"], "fixtures target another grammar")
        cases = fixtures["cases"]
        require(1 <= len(cases) <= 64, "fixture count is outside the native envelope")
        paths = []
        for case in cases:
            require(re.fullmatch(r"[a-z0-9-]+\.pgn", case["path"]) is not None,
                    "fixture path is not an admitted leaf")
            path = fixture_path.parent / case["path"]
            require(not path.is_symlink() and path.is_file(), "fixture is not an exact file")
            source = path.read_bytes()
            require(len(source) == case["source_bytes"], "fixture byte denominator differs")
            paths.append(path)
        require(len({case["id"] for case in cases}) == len(cases), "duplicate fixture identity")
        probe = args.probe.resolve(strict=True)
        declaration = sha(receipt_bytes)
        base = [str(probe), str(library), receipt["library"]["sha256"],
                str(receipt["library"]["byte_count"]), declaration]

        def execute(selected_paths, mode="normal", digest=None):
            command = list(base)
            if digest is not None:
                command[2] = digest
            command += [mode, *map(str, selected_paths)]
            return subprocess.run(command, capture_output=True, text=True, timeout=60)

        def rows(result, expected_count):
            require(result.returncode == 0, "native PGN probe failed: " + result.stderr[-4096:])
            decoded = [json.loads(line) for line in result.stdout.splitlines()]
            require(len(decoded) == expected_count, "native result omitted an artifact")
            return decoded

        def validate(row, case, path, ordinal):
            require(row["artifact_ordinal"] == ordinal, "native artifact order changed")
            require(row["bytes"] == case["source_bytes"] and row["source_sha256"] == sha(path.read_bytes()),
                    "native source byte receipt differs")
            for name, expected in case["expected_named_node_counts"].items():
                require(row["named_node_counts"].get(name, 0) == expected,
                        f"{case['id']}: node:{name} differs from independent expectation")
            if "expected_mainline_san_moves" in case:
                require(row["mainline_san_moves"] == case["expected_mainline_san_moves"],
                        "main-line and variation moves were conflated")
            errors = row["errors"] + row["missing"]
            require(errors >= case.get("min_error_or_missing", 0), "syntax recovery disposition disappeared")
            require("max_error_or_missing" not in case or errors <= case["max_error_or_missing"],
                    "unexpected syntax error or missing node")
            for field in ("exact_reconstruction", "replay_identity_equal", "occurrence_context_identity_equal",
                          "artifact_name_identity_equal"):
                require(row[field] is True, "native exact-source control failed")
            for field in ("chess_legality_verified", "complete_game_verified", "postgresql_admission"):
                require(row[field] is False, "syntax provider made an unsupported product claim")
            require(row["semantic_attestations"] == 0, "source observation created attestations")
            require(re.fullmatch("[0-9a-f]{32}", row["canonical_source_root"]) is not None
                    and re.fullmatch("[0-9a-f]{64}", row["canonical_source_witness"]) is not None,
                    "canonical source identity is missing")

        batch = rows(execute(paths), len(paths))
        for ordinal, (row, case, path) in enumerate(zip(batch, cases, paths)):
            validate(row, case, path, ordinal)
            scalar = rows(execute([path]), 1)[0]
            validate(scalar, case, path, 0)
            require({key: value for key, value in scalar.items() if key != "artifact_ordinal"}
                    == {key: value for key, value in row.items() if key != "artifact_ordinal"},
                    "scalar versus whole-working-set source semantics diverged")
        report["observations"] = [dict(row, fixture=case["id"]) for row, case in zip(batch, cases)]
        require(len({row["canonical_source_root"] for row in batch}) == len(batch),
                "distinct exact source fixtures collapsed to one identity")

        comment_index = next(index for index, case in enumerate(cases) if case["id"] == "command-comments")
        changed = rows(execute([paths[comment_index]], "comment-kind-defect"), 1)[0]
        try:
            validate(changed, cases[comment_index], paths[comment_index], 0)
        except RuntimeError as error:
            require("node:" in str(error), "deliberate comment defect failed for another reason")
        else:
            raise RuntimeError("deliberate comment-kind defect escaped conformance")
        require(changed["named_node_counts"].get("inline_comment", 0) == 0
                and changed["canonical_source_root"] == batch[comment_index]["canonical_source_root"],
                "comment defect did not separate syntax evidence from exact source identity")
        shallow = execute([paths[comment_index]], "depth-exhaustion")
        require(shallow.returncode == 42 and "native source decomposition refused" in shallow.stderr,
                "depth exhaustion did not refuse the complete native source plan")
        wrong = execute([paths[0]], digest="0" * 64)
        require(wrong.returncode == 42 and "verified PGN grammar load failed" in wrong.stderr,
                "mismatched grammar bytes reached native execution")
        report.update(status="passed", source_build_receipt_sha256=declaration,
                      grammar=receipt["grammar"], runtime=receipt["runtime"],
                      grammar_lock_sha256=sha(lock_bytes), selection_sha256=sha(selection_bytes),
                      fixture_manifest_sha256=sha(fixture_bytes), native_probe_sha256=sha(probe.read_bytes()),
                      shared_library=receipt["library"], files=len(cases),
                      bytes=sum(case["source_bytes"] for case in cases),
                      scalar_batch_parity=True, syntax_defect_detected=True,
                      depth_exhaustion_detected=True, shared_object_digest_mismatch_detected=True,
                      packaging_eligible=grammar["packaging_eligible"])
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
