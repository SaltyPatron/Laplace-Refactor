"""Translate verified Git observations to the existing native source input ABI.

These are declarations and acquisition checks. Native source admission computes
canonical content, the artifact graph, denominators and all database receipts.
"""
from __future__ import annotations

import json
from pathlib import Path
import re

from .verified_git import canonical, digest, exact_file, observe, require


def declaration(manifest_path: Path, source_root: Path, grammar_receipt: Path,
                geometry_epoch: str, *, maximum_depth: int = 256,
                preferred_batch_bytes: int = 1048576,
                dependency_lock: Path, grammar_lock: Path) -> tuple[dict[str, str], dict]:
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    require(manifest.get("schema") == "laplace.verified-git-corpus/v1" and
            manifest.get("phase") == "verified-source-input" and
            manifest.get("semantic_testimony_count") == 0 and
            manifest.get("canonical_admission_completed") is False,
            "an exact verified Git observation manifest is required")
    require(source_root.is_absolute() and not source_root.is_symlink() and
            source_root.resolve() == (manifest_path.parent / "files").resolve(),
            "source root must be the manifest's frozen files directory")
    # Revalidate the selected checkout and every Git blob; a JSON digest alone
    # cannot authorize a fabricated source inventory or altered snapshot.
    observed, _ = observe(Path(manifest["checkout"]), manifest["upstream"], manifest["commit"],
                          expected_archive_sha256=manifest.get("git_archive_sha256"), **manifest["bounds"])
    require(observed == manifest, "Git observation differs from its retained manifest")
    if "git_archive_sha256" in manifest:
        selected_dependencies = json.loads(dependency_lock.read_text())["dependencies"].values()
        require(any(item.get("upstream") == manifest["upstream"] and
                    item.get("revision") == manifest["commit"] and
                    item.get("git_archive_sha256") == manifest["git_archive_sha256"]
                    for item in selected_dependencies),
                "verified Git import is not selected by the installed product dependency lock")
    artifacts = manifest["artifacts"]
    require(0 < len(artifacts) <= 1024, "source artifact count exceeds this native admission boundary")
    for item in artifacts:
        content = exact_file(source_root, item["path"], manifest["bounds"]["maximum_file_bytes"])
        require(len(content) == item["byte_count"] and digest(content) == item["sha256"],
                "frozen source bytes differ: " + item["path"])
    grammar_bytes = grammar_receipt.read_bytes()
    grammar = json.loads(grammar_bytes)
    require(grammar.get("schema") == "laplace.grammar-provider-build/v1" and
            grammar.get("phase") == "source-built" and grammar.get("exit_code") == 0 and
            grammar.get("grammar_name") == "tree-sitter-cpp",
            "a successful locked C++ provider build receipt is required")
    require(grammar.get("dependency_lock_sha256") == digest(dependency_lock.read_bytes()) and
            grammar.get("grammar_lock_sha256") == digest(grammar_lock.read_bytes()),
            "grammar provider does not belong to the selected product's exact source locks")
    expected_runtime = json.loads(dependency_lock.read_text())["dependencies"]["tree-sitter"]
    expected_grammars = [item for item in json.loads(grammar_lock.read_text())["repositories"]
                         if item["name"] == "tree-sitter-cpp"]
    require(len(expected_grammars) == 1 and all(
        grammar[section].get(key) == expected[key]
        for section, expected in (("runtime", expected_runtime), ("grammar", expected_grammars[0]))
        for key in ("upstream", "revision", "git_archive_sha256")),
        "provider source/runtime identity differs from its locked product")
    # Grammar source and ABI define the source interpretation. Build directories,
    # compiler transcripts and executable packaging are separate physical proof.
    grammar_declaration = {
        "schema": "laplace.source-grammar-declaration/v1",
        "runtime": {key: expected_runtime[key] for key in
                    ("upstream", "revision", "git_archive_sha256")},
        "grammar": {key: expected_grammars[0][key] for key in
                    ("upstream", "revision", "git_archive_sha256", "generated_parsers", "external_scanners")},
        "language_symbol": "tree_sitter_cpp", "media_type": "text/x-c++",
        "kind_base": 0x4350500000000000,
        "interpretation": "concrete syntax observations; no executable semantics",
    }
    grammar_declaration_sha256 = digest(canonical(grammar_declaration))
    library = grammar["library"]
    require(Path(library["path"]).is_absolute() and
            re.fullmatch(r"[0-9a-f]{64}", library["sha256"]) is not None and
            0 < library["byte_count"] <= 128 * 1024 * 1024,
            "grammar shared-object identity is invalid")
    require(1 <= maximum_depth <= 4096 and 4096 <= preferred_batch_bytes <= 64 * 1024 * 1024,
            "source physical resource bounds are invalid")
    notice_names = {"license", "license.txt", "license.md", "copying", "copying.txt",
                    "unlicense", "unlicence", "licence", "licence.txt"}
    notices = [item for item in artifacts if Path(item["path"]).name.lower() in notice_names]
    require(bool(notices), "selected source must retain its exact license/notice artifacts")
    authority = {key: manifest[key] for key in ("upstream", "commit", "tree")}
    sections = {
        "AUTHORITY_RELEASE": authority,
        "LICENSE": {"retained_notice_artifacts": notices,
                    "law": "observe exact notices; this operation grants no distribution license"},
        "RECIPE_PROGRAM": {"owner": "laplace_source_decomposition_plan_create_bounded",
                           "recipe": "existing exact RAW envelope plus common Unicode content composition",
                           "grammar_declaration_sha256": grammar_declaration_sha256},
        "UNIVERSAL_AST_MAPPING": {"phase": "concrete-syntax-observation",
                                  "fields": ["kind", "grammar_kind", "field_kind", "sibling_ordinal",
                                             "byte_start", "byte_end", "syntax_flags"],
                                  "missing_content": "MISSING and non-missing EMPTY are distinct; no empty entity",
                                  "executable_semantics": "unsupported"},
        "HIGHWAY_REFERENCES": {"resolved_program_references": 0,
                               "symbol_resolution": "unsupported; no fabricated references"},
        "EPISTEMIC_WITNESSING": {"source_class": "observation", "source_type": "corpus",
                                "semantic_testimony": 0},
        "DENOMINATOR_DECLARATION": {"artifacts": artifacts, "records": "not applicable",
                                    "fields": "not applicable", "claims": 0,
                                    "parse_errors": "count every emitted ERROR or MISSING witness"},
        "CONFORMANCE": {"fixture": "verified_cpp_source_probe/v1",
                         "postgresql_fixture": "verified_cpp_source_contract/v1",
                         "declaration_is_execution_proof": False},
        "COMPLETION_LAW": {"source_bytes": "exact native reconstruction",
                            "syntax": "all provider witnesses with explicit parser errors",
                            "testimony": "zero", "database": "native source/world/structural receipts"},
        "SELECTED_BOUNDARY": {"authority": authority, "artifacts": artifacts,
                              "unsupported": ["C++ preprocessing", "name/type resolution",
                                              "execution correctness", "playing strength"]},
    }
    values: dict[str, str] = {
        "SCHEMA": "laplace.verified-git-source-declaration/v1", "PROFILE": "verified-git-code",
        "KIND": "17", "VERSION": "1", "RECONSTRUCTION_CLASS": "1", "SOURCE_FLAGS": "50",
        "GEOMETRY_EPOCH": geometry_epoch, "ARTIFACT_GRAPH_FINGERPRINT": "0" * 64,
        "SYNTAX_AUTHORITY_FINGERPRINT": grammar_declaration_sha256, "REFERENCE_RULE_COUNT": "0",
        "MAPPING_RULE_COUNT": "0", "ARTIFACT_COUNT": str(len(artifacts)),
        "PREFERRED_BATCH_BYTES": str(preferred_batch_bytes),
    }
    for key, value in {"AUTHORITY": manifest["upstream"], "RELEASE": manifest["commit"],
                       "NAMESPACE": "verified-git-code", "LOCAL_IDENTIFIER": manifest["tree"]}.items():
        values[key + "_ID"] = digest(canonical(["laplace-source-profile-scope-v1", key, value]))[:32]
    for key, section in sections.items():
        values[key + "_FINGERPRINT"] = digest(canonical(section))
    values["OCCURRENCE_CONTEXT_FINGERPRINT"] = digest(canonical(
        {"authority": authority, "artifacts": artifacts, "operation": "exact source observation"}))
    for index, item in enumerate(artifacts):
        prefix = f"ARTIFACT_{index}_"
        fields = {"ID": item["sha256"], "PARENT_ID": "0" * 64, "EXPECTED_SHA256": item["sha256"],
                  "NAME": item["path"].encode().hex(), "LOCAL_PATH": item["path"].encode().hex(),
                  "MEDIA_TYPE": item["media_type"].encode().hex(), "MODE": "1", "FLAGS": "4",
                  **{field: "0" for field in ("RECORDS", "FIELDS", "REFERENCE_MASK", "DELIMITER",
                     "TERMINATOR", "COLUMNS", "OUTCOME", "HEADER_RECORDS")}}
        values.update({prefix + key: value for key, value in fields.items()})
    return values, {"manifest_sha256": digest(manifest_bytes), "manifest": manifest,
                    "grammar_receipt_sha256": digest(grammar_bytes), "grammar_receipt": grammar,
                    "grammar_declaration_sha256": grammar_declaration_sha256,
                    "grammar_declaration": grammar_declaration,
                    "maximum_depth": maximum_depth, "declaration_sections": sections,
                    "provider_execution_identity_scope": {
                        "declaration_excludes_physical_build_paths": True,
                        "executed_provider_binds_actual_library_sha256": True,
                        "cross_build_execution_fingerprint_equality_claimed": False},
                    "executable_semantics_verified": False}
