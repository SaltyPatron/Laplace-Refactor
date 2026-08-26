#!/usr/bin/env python3
"""Validate that continuation authority preserves the whole Laplace invention."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUTHORITY_PATH = ROOT / "contracts" / "authority-stack.json"
RECIPE_PATH = ROOT / "contracts" / "recipe-model.json"
PROFILE_PATH = ROOT / "contracts" / "source-profile-model.json"
ADMISSION_PATH = ROOT / "contracts" / "source-admission.json"
OPERATION_PATH = ROOT / "contracts" / "operation-model.json"
CONTINUATION_PATH = ROOT / "state" / "continuation.json"
VERIFY_PHYSICAL_CONTINUATION = os.environ.get("LAPLACE_VERIFY_CONTINUATION_PHYSICAL") == "1"


def load(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} is not a JSON object")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def contains_all(value: object, terms: tuple[str, ...], message: str) -> None:
    text = json.dumps(value, sort_keys=True).lower()
    missing = [term for term in terms if term.lower() not in text]
    require(not missing, f"{message}: {', '.join(missing)}")


def validate_authority(document: dict) -> None:
    require(document.get("schema") == "laplace.authority-stack/v1", "authority schema drift")
    thesis = document.get("product_thesis", {})
    contains_all(
        thesis,
        ("any exact digital content", "typed universal AST", "Merkle DAG", "SQL", "native C/C++", "even playing field", "no gradient-training", "without requiring a GPU", "witnessed artifacts", "Laplace is a computer", "knowledge", "governance", "personality_firmware", "creative_extension", "cognitive_traffic", "finite_machine", "exception_machine", "application_symmetry", "entity_web", "deployment_symmetry"),
        "product thesis collapsed",
    )
    classes = document.get("authority_classes", {})
    contains_all(classes, ("exact inventor-authored messages", "locates and digests", "cannot manufacture a requirement", "traceable synthesis", "verified_working_projection", "cannot create inventor authority", "program_execution_projection", "without creating product law"), "authority provenance collapsed")
    evidence = document.get("direct_evidence", {})
    manifest_path = ROOT / evidence.get("continuation_manifest", "")
    require(manifest_path.is_file(), "continuation direct-evidence manifest is missing")
    lines = [line for line in manifest_path.read_text(encoding="utf-8").splitlines() if line]
    require(len(lines) == evidence.get("continuation_records"), "continuation direct-evidence record count drift")
    require(hashlib.sha256(manifest_path.read_bytes()).hexdigest() == evidence.get("continuation_manifest_sha256"), "continuation direct-evidence manifest digest drift")
    records = [json.loads(line) for line in lines]
    identifiers = {record.get("message_id") for record in records}
    required_identifiers = {
        "msg_01a03f36-b021-7740-9212-1083901f2aa8",
        "msg_01a03f73-fa64-7b00-a808-e3b800fcc7aa",
        "msg_01a03f7c-2bb5-7c10-827f-ddb2d7441dbd",
        "msg_01a03f7c-e62b-7463-8dbd-c7fcc015b211",
        "msg_01a03f7f-e5a6-7160-90ed-36da4113c84b",
        "msg_01a03fb1-75ee-75c2-8bbc-7fca17869594",
        "msg_01a03fb6-1f76-7741-afdd-cf7cb7885143",
        "msg_01a03fb7-9472-74f1-8846-22c58e73ed53",
        "msg_01a03fb7-f294-75c3-80eb-a49c37ae5051",
        "msg_01a03fb8-3faf-7b13-b0a5-a4a0ef1fbdef",
        "msg_01a03fc0-8030-7cb1-b340-7afb5d74a65c",
        "msg_01a03fc0-8036-7602-aefd-190fa91c0759",
        "msg_01a03fc0-cda5-7df0-8dea-06b0b95eb454",
        "msg_01a03fc1-4d40-7681-82e1-464908b8f9c0",
        "msg_01a03fc6-c1e9-7702-87fc-0d664e5f5d6a",
        "msg_01a03fc8-ba24-7b52-a828-b4c624711b8e",
    }
    require(required_identifiers.issubset(identifiers), "continuation direct corrections were omitted")
    contains_all(evidence, ("generator", "raw_source", "quoted agent prose", "reconciliation lead", "corroborated"), "quoted review content can impersonate direct inventor evidence")
    order = document.get("required_load_order", [])
    require([item.get("order") for item in order] == list(range(1, len(order) + 1)), "authority order is not contiguous")
    paths = [item.get("path") for item in order]
    require(paths[:3] == ["docs/audits/DIRECT_REQUIREMENT_EVIDENCE.md", "docs/product/CONSTITUTION.md", "docs/product/INVENTION_MODEL.md"], "direct evidence and whole invention are not loaded first")
    agents = next(item for item in order if item.get("path") == "AGENTS.md")
    require(agents.get("class") == "verified_working_projection", "AGENTS projection was promoted to inventor authority")
    agents_text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    contains_all(agents_text, ("verified agent-facing projection", "editing it cannot create", "LP-AST-001", "LP-GOVERNANCE-001", "LP-RECIPE-001", "LP-ADMISSION-001", "LP-CONNECTION-001", "LP-LIMITS-001", "LP-EXCEPTION-001", "LP-ENTITY-WEB-001", "LP-FEDERATION-001", "LP-ACTIVATION-001"), "AGENTS projection lost its authority joins")
    require(paths[-1] == "state/continuation.json", "observed state is not loaded last")
    transformation_text = (ROOT / "requirements" / "features" / "universal_ast_recipe.feature").read_text(encoding="utf-8")
    contains_all(transformation_text, ("I don't feel so good Mr Stark", "I feel fucking great, Tony!", "recipe transforms the typed AST", "produces the exact content"), "inventor's exact structural transformation was approximated")
    require("may produce the exact content" not in transformation_text, "exact transformation acceptance became optional")
    connection_text = (ROOT / "requirements" / "features" / "query_neighborhood.feature").read_text(encoding="utf-8")
    contains_all(connection_text, ("Equal hop counts", "known path is not proof", "upper bound", "Why not", "finite evidence authority", "partial work"), "typed connection or finite-machine law was lost")
    entity_web_text = (ROOT / "requirements" / "features" / "entity_web_federation.feature").read_text(encoding="utf-8")
    contains_all(entity_web_text, ("person cannot collapse", "personal web", "professional claim", "achievement cookies", "Raspberry Pi remains unsupported", "federation", "remote PostgreSQL"), "application or deployment modality law was lost")
    exception_text = (ROOT / "requirements" / "features" / "machine_exceptions.feature").read_text(encoding="utf-8")
    contains_all(exception_text, ("Hardware failure is not an epistemic unknown", "durable boundary", "restartable fault", "priority law", "same generated registry"), "processor-grade machine exception law was lost")
    model_text = (ROOT / "requirements" / "features" / "model_compilation.feature").read_text(encoding="utf-8")
    contains_all(model_text, ("flattened representation", "universal adjacency", "Faithful is not an acceptance class", "independent oracle", "deliberate defect"), "model export can hide flattening behind faithful")
    roadmap_text = (ROOT / "docs" / "product" / "ROADMAP.md").read_text(encoding="utf-8")
    contains_all(roadmap_text, ("substitute for implementation", "Integration proven", "not product activated", "foundational knowledge seed", "not begun", "machine.handle-exceptions", "critical path", "not a runtime waterfall", "GitHub Project #2"), "roadmap promoted requirements or lost the machine critical path")
    require(paths.index("contracts/recipe-model.json") < paths.index("contracts/source-profile-model.json") < paths.index("contracts/source-admission.json"), "source profile load order bypasses recipe or topology law")
    require(paths.index("contracts/recipe-model.json") < paths.index("contracts/source-admission.json"), "source admission precedes recipe law")
    for item in order:
        path = item.get("path")
        require(isinstance(path, str) and (ROOT / path).is_file(), f"authority path is missing: {path}")
    forbidden = document.get("forbidden_substitutions", [])
    contains_all(forbidden, ("source ingestion for the product purpose", "ETL rows", "retrieval for cognition", "raw hop count", "training for admission", "GPU availability", "firmware for knowledge identity", "personality prompt", "application account", "external entitlement", "physical node", "partial bounded", "unqualified word faithful", "independent evidence", "personhood"), "forbidden substitutions incomplete")
    gate = document.get("work_selection_gate", [])
    contains_all(gate, ("complete product behavior", "universal AST", "one native semantic owner", "continuation checkpoint"), "work selection gate is component-first")


def validate_recipe(document: dict) -> None:
    require(document.get("schema") == "laplace.recipe-model/v1", "recipe schema drift")
    tree = document.get("universal_tree", {})
    contains_all(tree, ("every admitted digital structure", "persistent", "Merkle DAG", "Unicode", "equal subtrees", "nonflattening"), "universal tree law collapsed")
    identity = document.get("recipe_identity", {})
    require(identity.get("coordinate") == ["kind", "authority", "release", "namespace", "local_identifier", "version"], "recipe coordinate scope collapsed")
    kinds = set(document.get("recipe_kinds", []))
    required_kinds = {
        "grammar-parse-lower-and-recompose",
        "canonicalize-and-identify",
        "observe-and-record-testimony",
        "compile-goal-and-search",
        "calculate-operator-and-select-act",
        "realize-modality-or-effect",
        "materialize-authorized-entity-world",
        "federate-authorized-world-state",
        "decompose-model-witness",
        "compile-target-artifact",
        "execute-firmware-or-calculus-extension",
    }
    require(required_kinds.issubset(kinds), "recipe system collapsed to source canonicalization")
    declaration = document.get("required_declaration", [])
    contains_all(declaration, ("grammar", "recomposition", "loss", "inverse", "epochs", "authority", "resource", "deliberate-defect", "completion", "receipt"), "recipe declaration incomplete")
    grammar = document.get("grammar_boundary", {})
    contains_all(grammar, ("Tree-sitter", "Laplace-native grammars", "concrete syntax", "universal typed AST", "recomposition", "round_trip", "structural_transformation"), "grammar decomposition/recomposition symmetry lost")
    compiler = document.get("compiler", {})
    contains_all(compiler, ("ISA program", "whole recipe", "no source modality language model", "private dispatcher"), "recipe compiler permits a private engine")
    execution = document.get("execution", {})
    contains_all(execution, ("vector", "whole-working-set", "one item", "without semantic recalculation", "cannot change semantic output", "conserved", "replay"), "generic execution law incomplete")
    application = document.get("application_and_federation", {})
    contains_all(application, ("universal_primitives", "profile feed resume", "authenticated external assertions", "web mobile API document", "content-addressed exchange", "cannot become semantic authority", "unsupported targets"), "application or federation installed a private engine")
    controls = set(document.get("required_negative_controls", []))
    required_controls = {
        "opaque-record-instead-of-AST",
        "source-specific-semantic-engine",
        "recipe-callback-bypasses-ISA",
        "provider-substitution-changes-meaning",
        "string-replacement-impersonates-structural-transformation",
        "same-local-identifier-collides-across-authority-or-release",
        "profile-blob-becomes-person-identity",
        "product-surface-installs-private-semantics",
        "federation-provider-becomes-identity-authority",
        "unsupported-node-claims-semantic-support",
        "hardware-fault-collapses-to-unknown",
        "failed-effect-publishes-semantic-result",
        "replay-crosses-unreceipted-durability-boundary",
    }
    require(required_controls.issubset(controls), "recipe deliberate defects incomplete")


def walk_keys(value: object) -> set[str]:
    keys: set[str] = set()
    if isinstance(value, dict):
        keys.update(value)
        for child in value.values():
            keys.update(walk_keys(child))
    elif isinstance(value, list):
        for child in value:
            keys.update(walk_keys(child))
    return keys


def validate_profile(document: dict) -> None:
    require(document.get("schema") == "laplace.source-profile-model/v1", "source profile schema drift")
    contains_all(document.get("purpose", ""), ("exact authority release artifact grammar recipe witness", "world state", "not importer configuration", "not a claim that the source is true"), "source profile purpose collapsed")
    identity = document.get("profile_identity", {})
    require(identity.get("coordinate") == ["kind", "authority", "release", "namespace", "local_identifier", "version"], "source profile coordinate scope collapsed")
    contains_all(identity, ("BLAKE3-256", "artifact graph", "path", "never source identity", "append-only"), "source profile identity is path or latest based")
    sections = document.get("required_sections", {})
    required_sections = {
        "purpose_and_roles",
        "authority_and_release",
        "artifact_graph",
        "syntax_authority",
        "recipe_program",
        "universal_ast_mapping",
        "highway_and_references",
        "epistemic_witnessing",
        "denominators_and_dispositions",
        "conformance_and_completion",
    }
    require(set(sections) == required_sections, "source profile lost Unicode-level exactness")
    contains_all(sections, ("license", "digest", "scanner", "error nodes", "typed AST role", "recipe coordinates", "source-local dependency DAG", "unresolved", "dependence roots", "every concrete symbol", "expected bytes", "negative controls", "PostgreSQL semantic parity", "readback", "cancellation", "profile completion"), "source profile exactness is incomplete")
    generic = document.get("generic_execution_law", {})
    contains_all(generic, ("thin", "cannot own identity AST evidence cognition realization", "typed data", "common recipe compiler and ISA", "whole-working-set", "shared", "unsupported"), "source profiles can create private engines")
    controls = set(document.get("required_negative_controls", []))
    required_controls = {
        "local-path-used-as-source-authority",
        "source-named-callback-bypasses-recipe-ISA",
        "concrete-field-or-error-node-silently-dropped",
        "unscoped-local-identifier-enters-highway",
        "opaque-record-replaces-universal-AST-mapping",
        "one-profile-completion-claims-generic-admission",
    }
    require(required_controls.issubset(controls), "source profile deliberate defects incomplete")


def validate_admission(document: dict) -> None:
    require(document.get("schema") == "laplace.source-admission/v1", "source admission schema drift")
    contains_all(document.get("purpose", ""), ("attributable exact observations", "AST", "Merkle-DAG", "without training"), "source purpose regressed to ETL")
    contains_all(document.get("epistemic_result", {}), ("even_playing_field", "calculation_inputs", "SQL", "translation", "learning"), "why sources are admitted is incomplete")
    program = document.get("admission_program", {})
    contains_all(program, ("typed recipe program", "whole working set", "concrete structure", "universal typed AST", "common native engine", "testimony", "set-oriented", "every byte", "readback"), "generic admission program incomplete")
    scheduling = document.get("scheduling", {})
    require(set(scheduling) == {"law", "not_a_source_order", "not_fixed_horizontal_passes", "cross_source_working_set", "publication"}, "source scheduling became a waterfall")
    contains_all(scheduling, ("recipe operations", "interleave", "source families do not form", "whole working set", "distinct coherent boundaries"), "source scheduling became a waterfall")
    forbidden_schedule_keys = {"stage", "depends_on", "runtime_order", "passes"}
    require(not (walk_keys(document) & forbidden_schedule_keys), "source manifest encodes a global waterfall or fixed passes")
    universe = document.get("selected_source_universe", [])
    identifiers = {item.get("id") for item in universe}
    required = {
        "unicode-and-character-standards",
        "grammar-authorities",
        "documents-and-lexical-witnesses",
        "predicate-frame-and-bridge-witnesses",
        "interaction-and-execution-history",
        "image-audio-video-geospatial-and-future-media",
        "domain-and-transition-witnesses",
        "model-artifact-and-behavior-witnesses",
        "identity-organization-entitlement-and-achievement-witnesses",
        "professional-project-and-delivery-history",
    }
    require(required.issubset(identifiers), "known heterogeneous witness universe was narrowed")
    grammar = next(item for item in universe if item.get("id") == "grammar-authorities")
    contains_all(grammar, ("Tree-sitter", "Laplace-native custom grammars", "AST", "conformance"), "grammar estate misclassified as code corpus")
    completion = document.get("completion", {})
    require(set(completion) == {"admission_capability", "source_profile_complete", "source_profile_world_admitted", "configured_foundational_seed", "nonexemplar"}, "admission completion states collapsed")
    contains_all(completion, ("unrelated grammars modalities", "without claiming product installation", "activated product database", "configured heterogeneous seed boundary", "cross-profile endpoints", "no individual corpus grammar modality model"), "exemplar can claim admission or seed completion")


def physical_dirty_digest(worktree: Path) -> str:
    result = subprocess.run(
        ["git", "diff", "--binary"],
        cwd=worktree,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return hashlib.sha256(result.stdout).hexdigest()


def physical_file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_output(worktree: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=worktree,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def validate_continuation(document: dict, verify_physical: bool = True) -> None:
    require(document.get("schema") == "laplace.continuation-checkpoint/v1", "continuation schema drift")
    require(document.get("classification") == "observed-development-state-not-product-law", "observed state promoted to product law")
    inputs = document.get("development_inputs", {})
    require(inputs.get("data_root", {}).get("observed_path") == "/vault/Data", "data root observation lost")
    require(inputs.get("model_root", {}).get("observed_path") == "/vault/models", "model root observation lost")
    contains_all(inputs.get("data_root", {}).get("observed_top_level_entries", []), ("UCD", "TreeSitter", "Wordnet", "FrameNet", "Tatoeba", "Games", "code-authority"), "known data-root inventory was narrowed")
    contains_all(inputs.get("model_root", {}).get("observed_top_level_entries", []), ("code-corpus", "stack-v2", "gguf", "Florence", "audio", "embedding", "reranker"), "known model-root inventory was narrowed")
    require(inputs.get("old_iteration", {}).get("observed_path") == "/home/ahart/Projects/Laplace", "old iteration identity lost")
    require(inputs.get("old_iteration", {}).get("authority") is False, "old implementation became authority")
    grammar_evidence = inputs.get("known_laplace_custom_grammar_location", {})
    require(grammar_evidence.get("state") == "historical-evidence-recovered-current-clean-room-authority-unresolved", "custom grammar recovery state was flattened")
    contains_all(grammar_evidence, ("PGN", "SQL tags", "behavioral evidence", "downstream domain counterexample", "independently specify", "still_unresolved"), "custom grammar evidence or limitation was lost")
    observations = grammar_evidence.get("historical_observations", [])
    require(len(observations) == 3 and all(item.get("authority") is False for item in observations), "historical grammar code became product authority")
    unicode = document.get("unicode", {})
    require(unicode.get("implemented") is True and unicode.get("integration_proven") is True, "Unicode integration evidence lost")
    require(unicode.get("product_activated") is False and unicode.get("world_admitted") is False and unicode.get("seeded") is False and unicode.get("released") is False, "Unicode state was promoted")
    work = document.get("interrupted_work", {})
    require(work.get("capability") == "substrate.bulk-deposit", "interrupted capability drift")
    contains_all(work.get("whole_product_reason", ""), ("generic AST Merkle-DAG", "before Unicode product activation", "not a corpus importer"), "interrupted work lost its product reason")
    require(len(work.get("untracked_files", [])) == 6, "interrupted untracked boundary drift")
    github = document.get("github_observation", {})
    require(github.get("main_commit") == document.get("repository", {}).get("base_commit"), "GitHub main and continuation base diverged")
    require(github.get("unicode_product_activation_issue", {}).get("state") == "open", "Unicode product activation issue was prematurely closed")
    require(github.get("product_path_gate_issue") == 54 and github.get("required_product_path_gate_present") is False, "product-path gate observation drift")
    contains_all(github.get("observed_required_checks", []), ("requirements", "native (linux-dev)", "native (linux-sanitize)"), "required-check observation narrowed")
    require(github.get("github_environments") == 0 and github.get("github_deployments") == 0 and github.get("github_releases") == 0, "GitHub integration state was promoted to delivery state")
    require(github.get("repository_runner", {}).get("name") == "hart-server-refactor", "repository runner observation lost")
    runs = {item.get("workflow"): item for item in github.get("latest_main_workflow_runs", [])}
    require(runs.get("clean-room-ci", {}).get("linux_dev_registered_tests") == 239 and runs.get("clean-room-ci", {}).get("linux_sanitize_registered_tests") == 239, "published hosted test-count observation drift")
    require(runs.get("custom-stack-ci", {}).get("registered_tests") == 258 and runs.get("custom-stack-ci", {}).get("observed_postgresql_version") == "18.3", "published custom-stack observation drift")
    if verify_physical:
        worktree = Path(document.get("repository", {}).get("active_worktree", ""))
        if worktree.is_dir() and (worktree / ".git").exists():
            repository = document.get("repository", {})
            require(git_output(worktree, "rev-parse", "HEAD") == repository.get("base_commit"), "continuation base commit is stale")
            require(git_output(worktree, "branch", "--show-current") == repository.get("active_branch"), "continuation active branch is stale")
            observed = physical_dirty_digest(worktree)
            require(observed == work.get("tracked_patch_sha256"), "continuation tracked patch is stale")
            tracked_paths = sorted(filter(None, git_output(worktree, "diff", "--name-only").splitlines()))
            require(tracked_paths == sorted(work.get("tracked_files", [])), "continuation tracked-file boundary is stale")
            untracked_paths = sorted(filter(None, git_output(worktree, "ls-files", "--others", "--exclude-standard").splitlines()))
            expected_untracked = sorted(item.get("path") for item in work.get("untracked_files", []))
            require(untracked_paths == expected_untracked, "continuation untracked-file boundary is stale")
            for observation in work.get("untracked_files", []):
                observed_path = worktree / observation.get("path", "")
                require(observed_path.is_file(), f"continuation untracked file is missing: {observed_path}")
                require(observed_path.stat().st_size == observation.get("bytes"), f"continuation untracked file size is stale: {observed_path}")
                require(physical_file_digest(observed_path) == observation.get("sha256"), f"continuation untracked file digest is stale: {observed_path}")
        for input_name in ("data_root", "model_root"):
            root_observation = inputs.get(input_name, {})
            observed_root = Path(root_observation.get("observed_path", ""))
            if observed_root.is_dir():
                actual_entries = sorted(item.name for item in observed_root.iterdir())
                expected_entries = sorted(root_observation.get("observed_top_level_entries", []))
                require(actual_entries == expected_entries, f"continuation {input_name} inventory is stale")
        for observation in observations:
            observed_path = Path(observation.get("observed_path", ""))
            if observed_path.is_file():
                require(observed_path.stat().st_size == observation.get("bytes"), f"historical grammar observation size is stale: {observed_path}")
                require(physical_file_digest(observed_path) == observation.get("sha256"), f"historical grammar observation digest is stale: {observed_path}")


def validate_operation(document: dict) -> None:
    require(document.get("schema") == "laplace.operation-model/v1", "operation schema drift")
    semantics = document.get("graph_semantics", {})
    contains_all(semantics, ("implementation capability", "not a source runtime order", "cyclic", "source-profile world admission", "foundational seed completion", "separate states", "typed universal AST"), "operation graph semantics collapsed")
    cycles = document.get("runtime_cycles", {})
    require(set(cycles) == {"observe_calculate_realize", "evidence_learning", "calculus_extension", "model_symmetry"}, "whole machine cycles were omitted")
    stages = {stage.get("id"): stage for stage in document.get("stages", [])}
    required = {"framework.execution", "machine.handle-exceptions", "substrate.compose-physicality", "substrate.bulk-deposit", "substrate.highway", "evidence.record-lineage", "world.admit-witnesses", "evidence.adjudicate", "query.guidance-search", "cognition.realize-effect", "learning.discovery-ooda", "model.ingest-generate", "product.materialize-entity-world", "runtime.federate-nodes"}
    require(required.issubset(stages), "whole capability graph was narrowed")
    require("seed.heterogeneous" not in stages, "monolithic seed stage returned")
    require(set(stages["world.admit-witnesses"].get("depends_on", [])) == {"substrate.highway", "substrate.bulk-deposit", "evidence.record-lineage"}, "world admission dependencies drift")
    require(stages["evidence.adjudicate"].get("depends_on") == ["evidence.record-lineage"], "evidence capability incorrectly waits for complete seed")
    require(stages["model.ingest-generate"].get("depends_on", [None])[0] == "world.admit-witnesses", "models bypass shared world admission")
    require("LP-RECIPE-001" in stages["framework.execution"].get("product_requirements", []), "recipe compiler missing from framework")
    require("LP-AST-001" in stages["substrate.compose-physicality"].get("product_requirements", []), "AST missing from substrate")
    require("LP-ADMISSION-001" in stages["world.admit-witnesses"].get("product_requirements", []), "world admission requirement missing")
    require({"LP-CONNECTION-001", "LP-LIMITS-001", "LP-EXCEPTION-001"}.issubset(stages["query.guidance-search"].get("product_requirements", [])), "typed connection finite-machine or exception law missing")
    require(stages["machine.handle-exceptions"].get("depends_on") == ["framework.execution"], "machine exception model bypasses the common framework")
    require(stages["machine.handle-exceptions"].get("github_issues") == [56], "machine exception issue ownership drift")
    require("LP-EXCEPTION-001" in stages["machine.handle-exceptions"].get("product_requirements", []), "processor-grade exception law missing")
    require({"LP-APPLICATION-001", "LP-ENTITY-WEB-001", "LP-ENTITLEMENT-001"}.issubset(stages["product.materialize-entity-world"].get("product_requirements", [])), "entity-world product stage lost universal application semantics")
    require({"LP-NODE-001", "LP-FEDERATION-001", "LP-PLACEMENT-001"}.issubset(stages["runtime.federate-nodes"].get("product_requirements", [])), "node federation placement stage incomplete")
    require(stages["runtime.federate-nodes"].get("depends_on", [])[-1] == "product.materialize-entity-world", "federation bypasses authorized entity-world projection")
    require(stages["substrate.highway"].get("github_issues") == [52], "highway issue ownership drift")
    require(stages["world.admit-witnesses"].get("github_issues") == [53, 59], "world admission issue ownership drift")
    contains_all(stages["framework.execution"].get("implementation", {}), ("published code proves", "universal AST type system", "recipe compiler", "remain unimplemented"), "framework partial state overclaims the recipe machine")
    contains_all(stages["bootstrap.unicode-root"].get("implementation", {}), ("controlled integration", "PostgreSQL 18.6 product cluster", "not proven"), "Unicode integration was promoted to product or generic-machine proof")
    contains_all(stages["substrate.bulk-deposit"].get("implementation", {}), ("separate dirty worktree", "not implemented or published"), "unpublished composition work was promoted")
    contains_all(stages["delivery.activate-product"].get("implementation", {}), ("no accepted package", "no deployment or release state"), "delivery infrastructure was promoted to product delivery")


class ProgramAuthorityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.authority = load(AUTHORITY_PATH)
        self.recipe = load(RECIPE_PATH)
        self.profile = load(PROFILE_PATH)
        self.admission = load(ADMISSION_PATH)
        self.operation = load(OPERATION_PATH)
        self.continuation = load(CONTINUATION_PATH)

    def test_whole_invention_authority_is_coherent(self) -> None:
        validate_authority(self.authority)
        validate_recipe(self.recipe)
        validate_profile(self.profile)
        validate_admission(self.admission)
        validate_operation(self.operation)
        validate_continuation(self.continuation, verify_physical=VERIFY_PHYSICAL_CONTINUATION)

    def test_mutation_observed_state_loaded_first_is_detected(self) -> None:
        mutant = copy.deepcopy(self.authority)
        checkpoint = mutant["required_load_order"].pop()
        mutant["required_load_order"].insert(0, checkpoint)
        for index, item in enumerate(mutant["required_load_order"], start=1):
            item["order"] = index
        with self.assertRaisesRegex(ValueError, "whole invention|loaded last"):
            validate_authority(mutant)

    def test_mutation_product_thesis_reduced_to_etl_is_detected(self) -> None:
        mutant = copy.deepcopy(self.authority)
        mutant["product_thesis"] = {"input": "source records", "result": "database rows"}
        with self.assertRaisesRegex(ValueError, "product thesis"):
            validate_authority(mutant)

    def test_mutation_agents_projection_becomes_direct_authority_is_detected(self) -> None:
        mutant = copy.deepcopy(self.authority)
        agents = next(item for item in mutant["required_load_order"] if item["path"] == "AGENTS.md")
        agents["class"] = "inventor_direct_evidence"
        with self.assertRaisesRegex(ValueError, "AGENTS projection"):
            validate_authority(mutant)

    def test_mutation_opaque_record_replaces_ast_is_detected(self) -> None:
        mutant = copy.deepcopy(self.recipe)
        mutant["universal_tree"]["abstract_form"] = "every source is one opaque record"
        mutant["universal_tree"]["persistent_form"] = "rows"
        with self.assertRaisesRegex(ValueError, "universal tree"):
            validate_recipe(mutant)

    def test_mutation_recipe_collapses_to_importer_is_detected(self) -> None:
        mutant = copy.deepcopy(self.recipe)
        mutant["recipe_kinds"] = ["canonicalize-and-identify"]
        with self.assertRaisesRegex(ValueError, "source canonicalization"):
            validate_recipe(mutant)

    def test_mutation_recomposition_becomes_string_replacement_is_detected(self) -> None:
        mutant = copy.deepcopy(self.recipe)
        mutant["grammar_boundary"].pop("laplace_recomposition")
        mutant["grammar_boundary"].pop("structural_transformation")
        with self.assertRaisesRegex(ValueError, "symmetry"):
            validate_recipe(mutant)

    def test_mutation_person_becomes_profile_blob_is_detected(self) -> None:
        mutant = copy.deepcopy(self.recipe)
        mutant["application_and_federation"] = {"projection_law": "store one profile blob in a user row"}
        with self.assertRaisesRegex(ValueError, "application or federation"):
            validate_recipe(mutant)

    def test_mutation_node_gets_private_engine_is_detected(self) -> None:
        mutant = copy.deepcopy(self.recipe)
        mutant["application_and_federation"]["node_law"] = "Raspberry Pi uses a separate simplified semantic engine"
        with self.assertRaisesRegex(ValueError, "application or federation"):
            validate_recipe(mutant)

    def test_mutation_hardware_fault_collapses_to_unknown_is_detected(self) -> None:
        mutant = copy.deepcopy(self.recipe)
        mutant["required_negative_controls"].remove("hardware-fault-collapses-to-unknown")
        with self.assertRaisesRegex(ValueError, "deliberate defects"):
            validate_recipe(mutant)

    def test_mutation_source_waterfall_is_detected(self) -> None:
        mutant = copy.deepcopy(self.admission)
        mutant["selected_source_universe"][1]["stage"] = 2
        with self.assertRaisesRegex(ValueError, "waterfall"):
            validate_admission(mutant)

    def test_mutation_source_profile_becomes_importer_config_is_detected(self) -> None:
        mutant = copy.deepcopy(self.profile)
        mutant["purpose"] = "copy records from a local path into database rows"
        with self.assertRaisesRegex(ValueError, "source profile purpose"):
            validate_profile(mutant)

    def test_mutation_grammar_estate_becomes_code_corpus_is_detected(self) -> None:
        mutant = copy.deepcopy(self.admission)
        grammar = next(item for item in mutant["selected_source_universe"] if item["id"] == "grammar-authorities")
        grammar["roles"] = ["code corpus"]
        grammar["candidates"] = ["one programming language"]
        with self.assertRaisesRegex(ValueError, "grammar estate"):
            validate_admission(mutant)

    def test_mutation_evidence_waits_for_complete_world_is_detected(self) -> None:
        mutant = copy.deepcopy(self.operation)
        stage = next(item for item in mutant["stages"] if item["id"] == "evidence.adjudicate")
        stage["depends_on"] = ["world.admit-witnesses"]
        with self.assertRaisesRegex(ValueError, "waits for complete seed"):
            validate_operation(mutant)

    def test_mutation_integration_promoted_to_product_is_detected(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        mutant["unicode"]["product_activated"] = True
        with self.assertRaisesRegex(ValueError, "promoted"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_one_source_promoted_to_seed_is_detected(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        mutant["unicode"]["seeded"] = True
        with self.assertRaisesRegex(ValueError, "promoted"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_profile_completion_promoted_to_seed_is_detected(self) -> None:
        mutant = copy.deepcopy(self.admission)
        mutant["completion"]["configured_foundational_seed"] = "one source profile completed"
        with self.assertRaisesRegex(ValueError, "seed completion"):
            validate_admission(mutant)

    def test_mutation_old_iteration_becomes_authority_is_detected(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        mutant["development_inputs"]["old_iteration"]["authority"] = True
        with self.assertRaisesRegex(ValueError, "old implementation"):
            validate_continuation(mutant, verify_physical=False)


if __name__ == "__main__":
    unittest.main()
