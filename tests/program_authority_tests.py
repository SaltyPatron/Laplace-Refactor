#!/usr/bin/env python3
"""Validate that continuation authority preserves the whole Laplace invention."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import subprocess
import tempfile
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
        ("any exact digital content", "typed universal AST", "Merkle DAG", "SQL", "native C/C++", "even playing field", "no gradient-training", "without requiring a GPU", "witnessed artifacts", "Laplace is a computer", "knowledge", "governance", "personality_firmware", "creative_extension", "cognitive_traffic", "finite_machine", "exception_machine", "application_symmetry", "entity_web", "deployment_symmetry", "execution_cohesion", "model_behavior", "sparse_addressability"),
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
        "msg_01a03fdc-59c5-79f1-a97d-70c831c36ee2",
        "msg_01a03fe7-d096-7182-b039-4d80134ff2d8",
        "msg_01a03fea-f702-73c1-83cc-506ad6ea451c",
        "msg_01a03fee-e624-7a80-ae0d-91f6949fe305",
        "msg_01a03ff0-38b8-7fd1-b562-cf2bb00ab333",
        "msg_01a03ff2-e281-7250-b660-e38979f5d58c",
        "msg_01a03fff-6e83-7c21-88db-1448c68f33ea",
        "msg_01a03fff-893f-7133-843c-d5e3e654a4b9",
        "msg_01a04000-60b1-7f70-9d53-c2f28e66806f",
        "msg_01a04000-daeb-7623-888e-308467077a9c",
        "msg_01a04012-6ee1-7911-901c-c48321c67e09",
        "msg_01a04014-f4ef-7692-84fd-f1addd19e4be",
        "msg_01a04014-f4f2-7b23-a293-92a295d51640",
    }
    require(required_identifiers.issubset(identifiers), "continuation direct corrections were omitted")
    generated_context_identifiers = {
        "msg_01a03f32-5273-7920-aef6-ec0a0492d432",
        "msg_01a03ffe-f6a5-78b1-9d8c-55aacfd3b90b",
        "msg_01a03ffe-f6b0-78b3-b096-3fa1af79b9c7",
        "msg_01a03fff-9478-7c92-965e-4b6526930f67",
        "msg_01a03fff-947b-7cb3-886b-0f2b9f7a14c1",
    }
    require(identifiers.isdisjoint(generated_context_identifiers), "generated client context was promoted to inventor evidence")
    contains_all(evidence, ("generator", "raw_source", "quoted agent prose", "reconciliation lead", "corroborated", "transport role", "inventor authorship"), "quoted review or generated context can impersonate direct inventor evidence")
    contains_all(document.get("session_control", {}), ("direct user", "halts", "resumes only", "external state changes", "noticing", "not proof"), "pause and resume authority can be bypassed")
    order = document.get("required_load_order", [])
    require([item.get("order") for item in order] == list(range(1, len(order) + 1)), "authority order is not contiguous")
    paths = [item.get("path") for item in order]
    require(paths[:3] == ["docs/audits/DIRECT_REQUIREMENT_EVIDENCE.md", "docs/product/CONSTITUTION.md", "docs/product/INVENTION_MODEL.md"], "direct evidence and whole invention are not loaded first")
    agents = next(item for item in order if item.get("path") == "AGENTS.md")
    require(agents.get("class") == "verified_working_projection", "AGENTS projection was promoted to inventor authority")
    agents_text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    contains_all(agents_text, ("verified agent-facing projection", "editing it cannot create", "LP-AST-001", "LP-GOVERNANCE-001", "LP-RECIPE-001", "LP-COHESION-001", "LP-MODEL-006", "LP-ADMISSION-001", "LP-CONNECTION-001", "LP-LIMITS-001", "LP-EXCEPTION-001", "LP-ENTITY-WEB-001", "LP-FEDERATION-001", "LP-MATERIALIZATION-001", "LP-ACTIVATION-001"), "AGENTS projection lost its authority joins")
    require(paths[-1] == "state/continuation.json", "observed state is not loaded last")
    require("docs/audits/CONTINUATION_WORK_AUDIT_2026-08-26.md" in paths, "continuation work and action audit is not load-bearing")
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
    contains_all(model_text, ("flattened representation", "universal adjacency", "Faithful is not an acceptance class", "independent oracle", "deliberate defect", "typed experiment", "formally nonzero support", "arbitrary cutoff"), "model export or behavior admission can hide flattening or arbitrary support")
    framework_text = (ROOT / "requirements" / "features" / "framework.feature").read_text(encoding="utf-8")
    contains_all(framework_text, ("Unrelated product programs", "structural container search", "multilingual conversation", "game-state transition", "component-private", "cannot promote"), "component islands can impersonate one cohesive machine")
    derived_text = (ROOT / "requirements" / "features" / "derived_state.feature").read_text(encoding="utf-8")
    contains_all(derived_text, ("Sparse absence is not a dense noise floor", "absent, unobserved, unknown", "no N-squared surface", "Storage pays for measured addressability", "with and without", "removing the acceleration preserves exact logical results", "database size, row count, index count"), "sparse addressability or acceleration parity law was lost")
    storage_census = (ROOT / "docs" / "audits" / "OLD_ITERATION_STORAGE_CENSUS_2026-08-26.md").read_text(encoding="utf-8")
    contains_all(storage_census, ("295,775,270,591", "109,191,495,680", "186,482,507,776", "1.7078482771", "52,649,911", "141,390,181", "historical evidence", "does not prove"), "dated storage evidence became vague or promotional")
    roadmap_text = (ROOT / "docs" / "product" / "ROADMAP.md").read_text(encoding="utf-8")
    contains_all(roadmap_text, ("substitute for implementation", "Integration proven", "not product activated", "foundational knowledge seed", "not begun", "machine.handle-exceptions", "critical path", "not a runtime waterfall", "GitHub Project #2", "#72", "paid addressability"), "roadmap promoted requirements or lost the machine critical path")
    require(paths.index("contracts/recipe-model.json") < paths.index("contracts/source-profile-model.json") < paths.index("contracts/source-admission.json"), "source profile load order bypasses recipe or topology law")
    require(paths.index("contracts/recipe-model.json") < paths.index("contracts/source-admission.json"), "source admission precedes recipe law")
    for item in order:
        path = item.get("path")
        require(isinstance(path, str) and (ROOT / path).is_file(), f"authority path is missing: {path}")
    forbidden = document.get("forbidden_substitutions", [])
    contains_all(forbidden, ("source ingestion for the product purpose", "ETL rows", "retrieval for cognition", "raw hop count", "training for admission", "GPU availability", "firmware for knowledge identity", "personality prompt", "application account", "external entitlement", "physical node", "partial bounded", "unqualified word faithful", "independent evidence", "personhood", "component success", "formal nonzero model support"), "forbidden substitutions incomplete")
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
    storage = document.get("historical_old_iteration_storage_observation", {})
    require(storage.get("classification") == "dated-read-only-historical-evidence-not-clean-product-law", "historical storage observation became product law")
    require(storage.get("database_bytes") == 295775270591 and storage.get("index_bytes") == 186482507776, "historical storage measurement drift")
    require(storage.get("exact_rows", {}).get("four_population_total") == 396693675, "historical exact population total drift")
    contains_all(storage.get("nonclaims", []), ("not proven", "clean Laplace", "non-authoritative"), "historical storage census lost its nonclaims")
    completed = document.get("recently_completed_work", {})
    require(completed.get("capability") == "substrate.bulk-deposit", "completed composition capability drift")
    require(completed.get("state") == "integration-proven-and-merged", "composition merge proof state drift")
    require(completed.get("pull_request") == 73 and completed.get("github_issue") == 15, "composition delivery observation drift")
    contains_all(completed.get("implemented_and_locally_proven_boundary", []), ("direct-native", "provider-independent", "cancellation", "blind-presence", "five-sample", "329062"), "issue 15 implementation acceptance was narrowed")
    contains_all(completed.get("remaining_whole_product_boundary", []), ("500000", "30-seconds-per-GB", "issue 72"), "whole-product performance boundary was hidden")
    work = document.get("active_work", {})
    require(work.get("capability") == "bootstrap.unicode-root", "active capability drift")
    require(
        work.get("state")
        == "implementation-merged-unicode-product-activation-controller-integration-proven-successor-product-package-installed-and-physical-cluster-activation-pending",
        "Unicode activation implementation state drift",
    )
    require(work.get("github_issue") == 13 and work.get("pull_request") == 74, "Unicode activation ownership drift")
    progress = work.get("implementation_progress", {})
    runtime = progress.get("runtime_package", {})
    require(runtime.get("contract_schema") == "laplace.postgresql-runtime-build/v2", "runtime contract generation drift")
    require(runtime.get("plan_schema") == "laplace.postgresql-runtime-plan/v2", "runtime plan generation drift")
    require(runtime.get("receipt_schema") == "laplace.postgresql-runtime-package/v2", "runtime receipt generation drift")
    require(
        runtime.get("state")
        == "inert-runtime-package-built-activation-ineligible-provider-qualification-unresolved",
        "runtime package proof state drift",
    )
    require(
        runtime.get("latest_plan_id")
        == "0538f872ba3f0f5201368f969ad01cb6d071c336ea29c764ba3a6a8cc535a373",
        "runtime package plan identity drift",
    )
    require(runtime.get("logical_install_prefix") == "/opt/laplace/current", "runtime logical activation prefix drift")
    latest_failure = runtime.get("latest_failed_execution", {})
    require(
        latest_failure.get("build_input_id")
        == "30dc787a8a992ac8f9bbf4a8bea726943390c62a6453de1c8090f1987522396e",
        "failed v2 runtime plan identity drift",
    )
    require(
        latest_failure.get("successful_component_checkpoints")
        == ["zlib", "lz4", "zstandard", "icu", "openssl", "liburing"],
        "failed v2 runtime checkpoint frontier drift",
    )
    require(
        latest_failure.get("failed_stage") == "component-test-loading"
        and latest_failure.get("failed_component") == "libxml2"
        and latest_failure.get("libxml2_test_disposition")
        == "not-executed-dynamic-loader-failure"
        and latest_failure.get("libxml2_process_return_code") == 2
        and latest_failure.get("liburing_test_exit_code") == 2,
        "latest runtime component-test-loading failure drift",
    )
    require(
        set(latest_failure.get("liburing_failed_tests", []))
        == {"conn-unreach.t", "io-wq-unused-exit.t", "link-timeout.t", "mshot-shutdown-race.t", "send_recv.t", "wq-aff.t"}
        and set(latest_failure.get("liburing_timed_out_tests", []))
        == {"cancel-fd-userdata.t", "cancel-race.t", "recv-mshot-fair.t", "recvsend_bundle.t"},
        "latest liburing host-coupled evidence drift",
    )
    contains_all(
        latest_failure,
        (
            "libicudata.so.78.3",
            "/../lib",
            "GNU_STACK",
            "libcrypto.so.4",
            "libxml2.so.16",
            "second shell",
            "generated ICU pkgdata",
            "declared build-relative loader path",
            "not a suite pass",
        ),
        "third runtime execution defects or corrections were hidden",
    )
    require(
        latest_failure.get("package_receipt_issued") is False
        and latest_failure.get("product_activation_occurred") is False,
        "failed package verification was promoted",
    )
    prior_package_failure = runtime.get("prior_v2_package_runpath_failure", {})
    require(
        prior_package_failure.get("build_input_id")
        == "a5118bc9100b46f85da0ebd01e65323aa5ea9caf15052aeaa7b289b213813005"
        and prior_package_failure.get("successful_component_checkpoints")
        == ["zlib", "lz4", "zstandard", "icu", "openssl", "liburing", "libxml2"]
        and prior_package_failure.get("failed_stage")
        == "final-package-elf-verification",
        "prior v2 package RUNPATH failure identity drift",
    )
    contains_all(
        prior_package_failure,
        (
            "libicudata.so.78.3",
            "/../lib",
            "xmllint",
            "xmlcatalog",
            "/opt/laplace/current/lib",
            "quarantined",
            "re-imported and verified",
        ),
        "prior package RUNPATH defect or source recovery was hidden",
    )
    require(
        prior_package_failure.get("package_receipt_issued") is False
        and prior_package_failure.get("product_activation_occurred") is False,
        "prior package RUNPATH failure was promoted",
    )
    successful_package = runtime.get("latest_successful_package_execution", {})
    require(
        successful_package.get("build_input_id")
        == "0538f872ba3f0f5201368f969ad01cb6d071c336ea29c764ba3a6a8cc535a373"
        and successful_package.get("package_receipt_sha256")
        == "0e85406c0d32338e192577eebeaca3771f73fc925bf01cd7634f2877dfc89b8a"
        and successful_package.get("staged_tree_sha256")
        == "807948f289b201f076c53df8c72b12ea6d2f23ec31b1a7624d6e28b967c5268a",
        "successful inert runtime package identity drift",
    )
    require(
        successful_package.get("staged_file_count") == 744
        and successful_package.get("staged_total_file_bytes") == 182760585,
        "successful inert runtime package tree extent drift",
    )
    require(
        set(successful_package.get("component_checkpoints", {}))
        == {"zlib", "lz4", "zstandard", "icu", "openssl", "liburing", "libxml2"}
        and set(successful_package.get("required_component_test_dispositions", {}).values())
        == {"passed"}
        and successful_package.get("liburing_test_disposition")
        == "failed-under-observed-runtime-provider",
        "successful runtime component evidence drift",
    )
    require(
        set(successful_package.get("liburing_failed_tests", []))
        == {"conn-unreach.t", "io-wq-unused-exit.t", "mshot-shutdown-race.t", "send_recv.t", "wq-aff.t"}
        and set(successful_package.get("liburing_timed_out_tests", []))
        == {"cancel-fd-userdata.t", "cancel-race.t", "recv-mshot-fair.t", "recvsend_bundle.t"},
        "successful package liburing provider evidence drift",
    )
    contains_all(
        successful_package.get("corrected_boundaries_proven", []),
        ("ICU", "libicudata.so.78.3", "GNU_STACK", "libcrypto.so.4", "libxml2 make check", ".libs", "runtime-search", "COPY-relocation"),
        "successful runtime package lost corrected physical evidence",
    )
    require(
        successful_package.get("build_input_closure_complete") is False
        and successful_package.get("runtime_provider_qualification_complete") is False
        and successful_package.get("activation_eligible") is False
        and successful_package.get("product_activation_occurred") is False,
        "inert runtime package was promoted to an accepted product",
    )
    source_interface_failure = runtime.get("prior_v2_source_interface_failure", {})
    require(
        source_interface_failure.get("build_input_id")
        == "a94ec0eec0fa10e5a2276986dbc4531de5160d6d15d7c571f25f388baa24422a"
        and source_interface_failure.get("successful_component_checkpoints")
        == ["zlib", "lz4", "zstandard", "icu", "openssl", "liburing"],
        "prior v2 source-interface failure identity drift",
    )
    require(
        source_interface_failure.get("failed_component") == "libxml2"
        and source_interface_failure.get("libxml2_compilation_completed") is True
        and source_interface_failure.get("libxml2_ctest_passed") == 8
        and source_interface_failure.get("libxml2_ctest_total") == 22
        and source_interface_failure.get("libxml2_ctest_exit_code") == 8,
        "libxml2 release test-interface failure drift",
    )
    contains_all(
        source_interface_failure,
        (
            "run_and_diff.cmake",
            "test_xmlcatalog_add_del.cmake",
            "fourteen registered tests could not start",
            "declares and verifies its physical build and test entrypoints",
            "Autotools",
            "make check",
        ),
        "libxml2 source-interface defect or correction was hidden",
    )
    require(
        source_interface_failure.get("package_receipt_issued") is False
        and source_interface_failure.get("product_activation_occurred") is False,
        "prior failed libxml2 execution was promoted",
    )
    failure = runtime.get("failed_execution", {})
    require(failure.get("component") == "liburing" and failure.get("component_version") == "2.15", "liburing acceptance boundary drift")
    require(failure.get("suite_exit_code") == 2 and failure.get("io_uring_disabled") == 0, "liburing failure disposition drift")
    require(
        set(failure.get("failed_tests", []))
        == {"conn-unreach.t", "io-wq-unused-exit.t", "link-timeout.t", "mshot-shutdown-race.t", "send_recv.t", "wq-aff.t"},
        "liburing failed-test evidence drift",
    )
    require(
        set(failure.get("timed_out_tests", []))
        == {"cancel-fd-userdata.t", "cancel-race.t", "recv-mshot-fair.t", "recvsend_bundle.t"},
        "liburing timeout evidence drift",
    )
    contains_all(
        failure.get("disposition", ""),
        ("failed under", "does not by itself identify the cause", "superseded v1"),
        "liburing failure was causally overstated or erased",
    )
    scope_correction = runtime.get("upstream_test_scope_correction", {})
    require(
        scope_correction.get("source_sha256")
        == "1bb130b0d32f1c3f8430c7b4c5c052cc931621a3e8eec2f484310b9892e7944f",
        "liburing upstream test-scope evidence drift",
    )
    contains_all(
        scope_correction,
        ("both liburing and live kernel", "not expected to pass on older kernels", "separately requires"),
        "runtime package and provider qualification were collapsed",
    )
    qualification = runtime.get("runtime_provider_qualification", {})
    require(
        qualification.get("receipt_schema") == "laplace.runtime-provider-qualification/v1"
        and qualification.get("required_before_product_activation") is True
        and qualification.get("required_components") == ["liburing"],
        "runtime-provider qualification contract drift",
    )
    require(
        qualification.get("state") == "required-not-established"
        and qualification.get("current_host_is_accepted") is False,
        "runtime provider was promoted without qualification",
    )
    staging_violation = runtime.get("staging_violation", {})
    require(staging_violation.get("activated") is False, "unactivated OpenSSL staging violation became activation")
    require(staging_violation.get("file_count") == 161 and staging_violation.get("physical_bytes") == 78739606, "OpenSSL staging violation evidence drift")
    contains_all(staging_violation, ("environment-only DESTDIR", "quarantine", "GNU Make DESTDIR", "mutation test"), "OpenSSL staging violation or correction was hidden")
    contains_all(runtime.get("continuation_condition", ""), ("exact composed PostgreSQL package receipt", "exact runtime", "build-toolchain receipts", "Intel", "recursive ELF closure", "platform ABI", "separate accepted selected-runtime-provider qualification"), "runtime continuation was approximated")
    contains_all(runtime.get("nonclaims", []), ("not activation eligible", "not an installed product", "not been waived", "not been qualified", "not been installed qualified or accepted", "not been activated"), "runtime package or provider was promoted beyond evidence")
    toolchain = progress.get("build_toolchain_package", {})
    require(toolchain.get("contract_schema") == "laplace.toolchain-build-contract/v1", "toolchain package contract drift")
    require(toolchain.get("receipt_schema") == "laplace.toolchain-package-receipt/v1", "toolchain package receipt drift")
    require(toolchain.get("consumer_manifest_schema") == "laplace.toolchain-consumer-manifest/v1", "toolchain consumer manifest drift")
    require(toolchain.get("state") == "verified-build-toolchain-only", "toolchain package proof state drift")
    require(
        toolchain.get("build_input_id")
        == "2e899074f53c0495ab966196c15cd89de8b0bd61c87e6591edf5b2801ef55ea6"
        and toolchain.get("package_receipt_sha256")
        == "9cd1c23b21ab8504620faa5cf198e0d79ec625ec3a875184f757a6c1a49b1ee6"
        and toolchain.get("package_tree_sha256")
        == "3a0d268c402c1d0e8ade09985bcc9ad2a2ae3532f383eab77f026c47306abcbd"
        and toolchain.get("consumer_manifest_sha256")
        == "8adbefb6068dc9b988f3b9970306a0058968c722bb72b66502eb1a3067bd4246",
        "toolchain package identity drift",
    )
    require(
        toolchain.get("package_file_count") == 8283
        and toolchain.get("package_total_file_bytes") == 449574676,
        "toolchain package extent drift",
    )
    perl_modules = toolchain.get("selected_perl_modules", {})
    require(
        perl_modules.get("IO::Pty", {}).get("provider_sha256")
        == "ba6489cd0b74b1b0853399eb9c7dc903a4217f9e65b4ae855ce4915b27fff6f0"
        and perl_modules.get("IO::Tty", {}).get("provider_sha256")
        == "bd6a4046efb3e781df03c5d203c8b94ec97ecf5233a270c2e7aaf1e649af0f7f"
        and perl_modules.get("IPC::Run", {}).get("provider_sha256")
        == "67d86f857bfe03119e70707d08298b45b6189bf74a5bc429a329d76b0f27f82b"
        and perl_modules.get("IO::Tty", {}).get("native_provider_sha256")
        == "50feb3f1b3e63703b812f8c40f30d1a09b60a2d453e3ff4ddb87dfae47381d12",
        "toolchain packaged Perl provider identity drift",
    )
    require(
        toolchain.get("product_runtime_activation_eligible") is False
        and toolchain.get("product_activation_occurred") is False,
        "build toolchain was promoted into product runtime state",
    )
    postgresql = progress.get("postgresql_product_package", {})
    require(postgresql.get("contract_schema") == "laplace.postgresql-build-contract/v2", "PostgreSQL product composer contract drift")
    require(postgresql.get("state") == "inert-composed-postgresql-package-built-tested-and-recursively-closed-bootstrap-executable-successor-not-yet-built-activation-ineligible", "PostgreSQL package proof state drift")
    require(
        postgresql.get("build_input_id")
        == "006e7e31883563fa4c7633c88a9413fba53447940d2709d9f55d1689fb76e97d"
        and postgresql.get("toolchain_package_receipt_sha256")
        == "9cd1c23b21ab8504620faa5cf198e0d79ec625ec3a875184f757a6c1a49b1ee6"
        and postgresql.get("runtime_package_receipt_sha256")
        == "0e85406c0d32338e192577eebeaca3771f73fc925bf01cd7634f2877dfc89b8a",
        "PostgreSQL deterministic plan identity drift",
    )
    require(postgresql.get("logical_install_prefix") == "/opt/laplace/current/pgsql-18", "PostgreSQL logical product prefix drift")
    contains_all(postgresql.get("receipt_inputs", []), ("Perl TAP", "staged OpenSSL", "unresolved provider qualification retained", "installed-provider lock selection", "linker-map", "PostgreSQL 18.6"), "PostgreSQL input receipts lost exact providers or the provider gate")
    contains_all(postgresql.get("implemented_boundary", []), ("duplicate-key-safe", "runtime-subtree immutability", "packaged Make", "execution preflight", "configure selection receipt", "DESTDIR", "RUNPATH", "installed-provider selection", "recursively closed", "dynamic loader libc and libm", "independent recursive closure", "provider-qualification requirements", "activation remains false"), "PostgreSQL product composition boundary was narrowed")
    contains_all(postgresql.get("known_unclosed_inputs", []), ("Python standard library", "configure-time host utilities", "Intel-compiler-selected", "static archives", "compiler-runtime license", "selected kernel", "runtime-provider qualification"), "PostgreSQL input closure was overstated")
    successor = postgresql.get("next_successor_build_boundary", {})
    require(
        successor.get("implementation_commit")
        == "16126c4a4a90a6710528f94aacb401cf45fdea66"
        and successor.get("state")
        == "implemented-and-locally-proven-successor-package-not-built"
        and successor.get("package_receipt_issued") is False
        and successor.get("product_activation_occurred") is False,
        "PostgreSQL bootstrap successor was promoted without a package receipt",
    )
    contains_all(
        successor.get("implemented", []),
        ("shared toolchain receipt verifier", "Python 3.10.12", "dash", "plan identity", "executing driver", "/bin/sh", "PYTHON SHELL and CONFIG_SHELL", "mutations"),
        "PostgreSQL bootstrap executable boundary was narrowed",
    )
    successor_preflight = successor.get("live_preflight", {})
    require(
        successor_preflight.get("python_sha256")
        == "7d51cd6b48b521277f5caa4610a82126e315fa2be4df069823a8b1eeb5bd4a86"
        and successor_preflight.get("shell_sha256")
        == "4f291296e89b784cd35479fca606f228126e3641f5bcaee68dee36583d7c9483"
        and successor_preflight.get("bin_sh_resolved_path") == "/usr/bin/dash",
        "PostgreSQL bootstrap executable preflight identity drift",
    )
    previous_postgresql = postgresql.get("previous_rejected_execution", {})
    require(
        previous_postgresql.get("build_input_id")
        == "e473f3ccaf4aa0fb51452ff7f1292c38d5867b9384df083d052046824bdbd0f2"
        and previous_postgresql.get("selected_ambient_openssl") == "OpenSSL 3.0.2"
        and "IPC::Run" in previous_postgresql.get("failed_prerequisite", "")
        and previous_postgresql.get("execution_preflight_existed") is False
        and previous_postgresql.get("package_receipt_issued") is False
        and previous_postgresql.get("product_activation_occurred") is False,
        "rejected PostgreSQL execution evidence was erased or promoted",
    )
    latest_rejected_postgresql = postgresql.get("latest_rejected_execution", {})
    require(
        latest_rejected_postgresql.get("build_input_id")
        == "d783da914b7cd540591656bb17e75fd61e3c963d9f5b415aa84fe6cda34a03f3"
        and latest_rejected_postgresql.get("failed_stage")
        == "configure-selection-receipt-verification"
        and latest_rejected_postgresql.get("compilation_started") is False
        and latest_rejected_postgresql.get("package_receipt_issued") is False
        and latest_rejected_postgresql.get("product_activation_occurred") is False,
        "latest rejected PostgreSQL configure receipt was erased or promoted",
    )
    contains_all(
        latest_rejected_postgresql,
        ("IO::Pty", "IPC::Run", "OpenSSL 4.0.1", "TAP prerequisites", "uppercase Autoconf", "ambient-provider negative control"),
        "latest PostgreSQL configure proof or correction was hidden",
    )
    prior_successful_postgresql = postgresql.get(
        "prior_successful_package_before_recursive_closure", {}
    )
    require(
        prior_successful_postgresql.get("build_input_id")
        == "272d2394be339e985b62bfe7cb3427ab45ea7dbf34b318391ef42b8707d1331e"
        and prior_successful_postgresql.get("package_receipt_sha256")
        == "6fde367d6f19ed6fbe89d141f43d927d389b3ac2a5cb7383c33193d81240a96d"
        and prior_successful_postgresql.get("recursive_elf_closure_verified") is False
        and prior_successful_postgresql.get("activation_eligible") is False,
        "prior successful PostgreSQL runtime-closure blocker was erased or promoted",
    )
    successful_postgresql = postgresql.get("latest_successful_package_execution", {})
    require(
        successful_postgresql.get("receipt_schema")
        == "laplace.postgresql-package-receipt/v2"
        and successful_postgresql.get("build_input_id")
        == "006e7e31883563fa4c7633c88a9413fba53447940d2709d9f55d1689fb76e97d"
        and successful_postgresql.get("package_receipt_sha256")
        == "2240ad84fa52761b2795a4e9db1e77b68f27f4e39731097302883ac9cecd8b89"
        and successful_postgresql.get("package_tree_sha256")
        == "e6cca321cdde6ee7417fcc6bd6186361beac490b58712cbf828068b0576e69cb",
        "successful PostgreSQL package identity drift",
    )
    require(
        successful_postgresql.get("package_file_count") == 2689
        and successful_postgresql.get("package_total_file_bytes") == 334091769
        and successful_postgresql.get("completed_targets")
        == ["world-bin", "check-world", "install-world-bin"],
        "successful PostgreSQL package extent or test boundary drift",
    )
    installed_provider = successful_postgresql.get("installed_runtime_provider", {})
    require(
        installed_provider.get("selection_sha256")
        == "07a9f21bcb81cb669a6be3248eb889ffbceb20bfd05fc957c16373f5ed595137"
        and installed_provider.get("provider") == "intel-oneapi-runtime"
        and installed_provider.get("provider_version") == "2026.1.1"
        and installed_provider.get("provider_sha256")
        == "f14ba1d659dcc7c618386955da3daf025da48ebe5589cad3875028db6aeb12e6",
        "PostgreSQL installed runtime provider selection drift",
    )
    contains_all(
        installed_provider,
        ("imf-runtime", "svml-runtime", "intlc-runtime", "irng-runtime", "license", "compiler-third-party-programs"),
        "PostgreSQL installed runtime provider roles or licenses were narrowed",
    )
    recursive_elf = successful_postgresql.get("recursive_elf_closure", {})
    require(
        recursive_elf.get("receipt_schema")
        == "laplace.postgresql-recursive-elf-closure-receipt/v1"
        and recursive_elf.get("receipt_sha256")
        == "fe9d55b1516883f6cf182adc94680ccfd73586a4fae578239c451717209418cf"
        and recursive_elf.get("root_artifacts") == 198
        and recursive_elf.get("objects") == 166
        and recursive_elf.get("edges") == 774
        and recursive_elf.get("unresolved_edges") == 0
        and recursive_elf.get("resolution_conflicts") == 0
        and recursive_elf.get("parse_errors") == 0
        and recursive_elf.get("discovery_errors") == 0
        and recursive_elf.get("root_abi_family_collisions") == 0
        and recursive_elf.get("external_objects") == 0
        and recursive_elf.get("host_objects") == 3,
        "PostgreSQL accepted recursive ELF closure drift",
    )
    contains_all(
        recursive_elf,
        ("libimf.so", "libintlc.so.5", "libirng.so", "libsvml.so", "libgcc_s.so.1", "libstdc++.so.6", "ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "independent-recursive-elf-closure.json"),
        "PostgreSQL accepted recursive ELF providers or independent receipt were hidden",
    )
    require(
        successful_postgresql.get("build_input_closure_complete") is False
        and successful_postgresql.get("recursive_elf_closure_verified") is True
        and successful_postgresql.get("runtime_provider_qualification_complete") is False
        and successful_postgresql.get("activation_eligible") is False
        and successful_postgresql.get("product_activation_occurred") is False,
        "successful inert PostgreSQL package was promoted beyond its closure evidence",
    )
    accepted_postgresql = progress.get("accepted_postgresql_product_package", {})
    require(
        accepted_postgresql.get("receipt_schema")
        == "laplace.postgresql-package-receipt/v2"
        and accepted_postgresql.get("build_input_id")
        == "94df3101524e73af6effe9af8d7d6402a1238be0892acdbe5207b10ba6e788f7"
        and accepted_postgresql.get("package_receipt_sha256")
        == "f80ac4a4879fdee15d4eac36ad16a7db525c56c715c922933b4b57f9880dcdfd"
        and accepted_postgresql.get("package_tree_sha256")
        == "115297c23286f765a7023ddf81934410ccdc53c704a66c93a5177a97a9729d25",
        "accepted PostgreSQL package identity drift",
    )
    require(
        accepted_postgresql.get("package_file_count") == 2689
        and accepted_postgresql.get("package_total_file_bytes") == 334042909
        and accepted_postgresql.get("completed_targets")
        == ["world-bin", "check-world", "install-world-bin"],
        "accepted PostgreSQL package extent or test boundary drift",
    )
    accepted_postgresql_overlay = accepted_postgresql.get("source_test_overlay", {})
    require(
        accepted_postgresql_overlay.get("receipt_sha256")
        == "e337642808a3b67d20c39ca5c29d71235250a83a743c0afbc8527d658356268a"
        and accepted_postgresql_overlay.get("source_tree_sha256_before")
        == accepted_postgresql_overlay.get("source_tree_sha256_after")
        == "1f8ae7adf74b12aa3eb5258c01ed2777919de9e97f498d04388f5d69b8881dc5"
        and set(accepted_postgresql_overlay.get("permitted_transient_generated_files", []))
        == {"err_warn_msg.c", "err_warn_msg_informix.c"}
        and accepted_postgresql_overlay.get("generated_files_remaining_after_test") == [],
        "accepted PostgreSQL source-test overlay boundary drift",
    )
    accepted_runtime_qualification = accepted_postgresql.get(
        "runtime_provider_qualification", {}
    )
    require(
        accepted_runtime_qualification.get("schema")
        == "laplace.runtime-provider-qualification/v1"
        and accepted_runtime_qualification.get("scope")
        == "postgresql-18.6-selected-io_uring-product-path"
        and accepted_runtime_qualification.get("receipt_sha256")
        == "f57b734a753e9413cc74a65b8349aaaa4d7008414fe9d66bc8d12786df33dfc9"
        and accepted_runtime_qualification.get("accepted") is True
        and accepted_runtime_qualification.get("test_plan") == 529
        and accepted_runtime_qualification.get("io_uring_assertion_count") == 175
        and accepted_runtime_qualification.get("failed_assertion_count") == 0,
        "accepted PostgreSQL runtime-provider qualification drift",
    )
    accepted_postgresql_closure = accepted_postgresql.get("recursive_elf_closure", {})
    require(
        accepted_postgresql_closure.get("receipt_sha256")
        == "0be23b3c8a15871a633804e6935381e55e93fc45f7f7f7080324892c08448390"
        and accepted_postgresql_closure.get("root_artifacts") == 198
        and accepted_postgresql_closure.get("objects") == 166
        and accepted_postgresql_closure.get("edges") == 1220
        and accepted_postgresql_closure.get("custom_objects") == 163
        and accepted_postgresql_closure.get("host_objects") == 3
        and accepted_postgresql_closure.get("external_objects") == 0
        and accepted_postgresql_closure.get("unresolved_edges") == 0
        and accepted_postgresql_closure.get("resolution_conflicts") == 0
        and accepted_postgresql_closure.get("parse_errors") == 0
        and accepted_postgresql_closure.get("discovery_errors") == 0
        and accepted_postgresql_closure.get("root_abi_family_collisions") == 0,
        "accepted PostgreSQL recursive ELF closure drift",
    )
    require(
        accepted_postgresql.get("build_input_closure_complete") is True
        and accepted_postgresql.get("recursive_elf_closure_verified") is True
        and accepted_postgresql.get("runtime_provider_qualification_complete") is True
        and accepted_postgresql.get("activation_eligible") is True
        and accepted_postgresql.get("product_activation_occurred") is False,
        "accepted PostgreSQL package proof state drift",
    )
    complete_product = progress.get("complete_product_package", {})
    require(
        complete_product.get("receipt_schema")
        == "laplace.product-package-receipt/v1"
        and complete_product.get("build_plan_id")
        == "b935f98e36c7f9654b804c3128f6cc3c6d43c5f73ee4f01f0ec36d4e56cfcddf"
        and complete_product.get("package_id")
        == "8fcf93c32f10ef342e1b18382a3f682e6d81e99c76b8f175ff6ecd3e07403928"
        and complete_product.get("package_receipt_sha256")
        == "dd581ffafbfd5da6a6638a1990c26fe53ea09527e4b0b1537956e5c3792b665e"
        and complete_product.get("package_manifest_sha256")
        == "72abb060976cbf58f580c456d6f1cd94c6eb50fe97d0bf2bdb178e29c7feefb9",
        "complete product package identity drift",
    )
    require(
        complete_product.get("package_file_count") == 2745
        and complete_product.get("package_symlink_count") == 47
        and complete_product.get("package_total_file_bytes") == 532480227
        and complete_product.get("build_input_closure_complete") is True
        and complete_product.get("activation_eligible") is True
        and complete_product.get("current_contract_activation_eligible") is False
        and complete_product.get("immutable_release_installed") is True
        and complete_product.get("product_activated") is False,
        "complete product package extent or proof state drift",
    )
    contains_all(
        complete_product,
        ("native execution-resource observer", "bin/laplace_resource_observe"),
        "successor product package requirement was hidden",
    )
    installation = complete_product.get("installation", {})
    require(
        installation.get("receipt_schema")
        == "laplace.product-package-installation-receipt/v1"
        and installation.get("receipt_sha256")
        == "276e6002a8db85f3e5ca7207a8580cbcd4a5e7048e1ccbb5a6e7fbaf977d8e9e"
        and installation.get("installation_receipt_sha256")
        == "7c82e967dc843fb3bf8243cccb72900e7574e7031e3148fd7c686fc13fbc69a6"
        and installation.get("installed_package_verified") is True
        and installation.get("exact_replay_verified") is True
        and installation.get("overwrite_performed") is False
        and installation.get("root_ownership_complete") is False,
        "complete product installation receipt drift",
    )
    complete_closure = complete_product.get("recursive_elf_closure", {})
    require(
        complete_closure.get("receipt_sha256")
        == "a6b14c6aab214c42c8a9b85adf2ea53791d92d2966f285da37a1bd96baf74e7d"
        and complete_closure.get("root_artifacts") == 221
        and complete_closure.get("objects") == 184
        and complete_closure.get("edges") == 1346
        and complete_closure.get("custom_objects") == 178
        and complete_closure.get("host_objects") == 6
        and complete_closure.get("external_objects") == 0
        and complete_closure.get("unresolved_edges") == 0
        and complete_closure.get("resolution_conflicts") == 0
        and complete_closure.get("parse_errors") == 0
        and complete_closure.get("discovery_errors") == 0
        and complete_closure.get("root_abi_family_collisions") == 0,
        "complete product recursive ELF closure drift",
    )
    require(
        set(complete_closure.get("platform_abi_sha256", {}))
        == {
            "ld-linux-x86-64.so.2",
            "libc.so.6",
            "libm.so.6",
            "libdl.so.2",
            "libpthread.so.0",
            "librt.so.1",
        },
        "complete product platform ABI boundary narrowed",
    )
    current_product = progress.get("current_product_package", {})
    require(
        current_product.get("receipt_schema") == "laplace.product-package-receipt/v1"
        and current_product.get("build_plan_id")
        == "1e141ec9af7c27b002e3d8df72a45c4db64c1d280d2668f964c79b26548db40c"
        and current_product.get("package_id")
        == "4aa84caa56b02d4cfb05f98fb33e9c34714b1a40c31f6639c22c20573d2ea60b"
        and current_product.get("package_receipt_sha256")
        == "e4510b25d3912a30094129e822bf62179c3a9f8216c287889b246d91c8f531b8"
        and current_product.get("package_manifest_sha256")
        == "aad0a29e767d8f09cc32c531b9461c2d079835c3a1e434a3c1be27e792246112",
        "current product package identity drift",
    )
    require(
        current_product.get("package_file_count") == 2746
        and current_product.get("package_symlink_count") == 47
        and current_product.get("package_total_file_bytes") == 532714907
        and current_product.get("build_input_closure_complete") is True
        and current_product.get("activation_eligible") is True
        and current_product.get("current_contract_activation_eligible") is False
        and current_product.get("cluster_plan_issued") is False
        and current_product.get("product_activated") is False,
        "current product package extent or proof state drift",
    )
    require(
        current_product.get("native_resource_observer", {}).get("sha256")
        == "08629f039b84677b2b32e6dff957d3afca81b0e82f586f4ba586f581aa4be2da",
        "current native resource observer identity drift",
    )
    current_closure = current_product.get("recursive_elf_closure", {})
    require(
        current_closure.get("receipt_sha256")
        == "d4371f196d5b0a602ef708e978dd94b58e318c79b9aa2b1fb360b225f5ee284f"
        and current_closure.get("root_artifacts") == 222
        and current_closure.get("objects") == 185
        and current_closure.get("edges") == 1351
        and current_closure.get("custom_objects") == 179
        and current_closure.get("host_objects") == 6
        and current_closure.get("external_objects") == 0
        and current_closure.get("unresolved_edges") == 0
        and current_closure.get("resolution_conflicts") == 0,
        "current product recursive ELF closure drift",
    )
    current_installation = current_product.get("installation", {})
    require(
        current_installation.get("receipt_sha256")
        == "96ec845d1379dc0205c466552b163b9eb9113baffe665bf1a2e78410839b9088"
        and current_installation.get("installation_receipt_sha256")
        == "72776e65055ae862bf6ee8404cf7ca1bdf104f9251b5a843c874ca9bcf7c074e"
        and current_installation.get("installed_package_verified") is True
        and current_installation.get("overwrite_performed") is False
        and current_installation.get("root_ownership_complete") is False,
        "current product installation drift",
    )
    resource = current_product.get("resource_observation", {})
    require(
        resource.get("observation_sha256")
        == "196982b21915dda742adde29aee68664be14ee146b971716b4bfb81f40d65554"
        and resource.get("processor_ids") == [0, 1, 2, 3, 4, 5]
        and resource.get("cpu_slots") == 6
        and resource.get("memory_bytes") == 12884901888
        and resource.get("io_slots") == 4,
        "current native resource observation drift",
    )
    collision = current_product.get("collision_observation", {})
    require(
        collision.get("observation_sha256")
        == "50e90279ef7f1ca561da6393c48b7655b07a92bfe7d39264de65c804e4aa8f5a"
        and collision.get("collisions") == []
        and collision.get("accepted") is False
        and len(collision.get("inspection_errors", [])) == 3,
        "incomplete live collision observation was erased or promoted",
    )
    require(
        document.get("repository", {}).get("implementation_checkpoint_commit")
        == "3a74773608ede6d7fde8fc13c4bef6ffa5369aed",
        "product-package implementation checkpoint drift",
    )
    successor = progress.get("successor_product_package", {})
    require(
        successor.get("build_plan_id")
        == "e62dc5ff9bb063546ac7d4e738d1b6d8c434f4cce5521570f51d747d0cd80052"
        and successor.get("package_id")
        == "60684505c3249cd9f160e46b81dee396da18f25d74171cad7c565761dcf47445"
        and successor.get("package_file_count") == 2747
        and successor.get("package_symlink_count") == 47
        and successor.get("package_total_file_bytes") == 532926282
        and successor.get("build_input_closure_complete") is True
        and successor.get("activation_eligible") is True
        and successor.get("immutable_release_installed") is True
        and successor.get("cluster_plan_issued") is False
        and successor.get("product_activated") is False,
        "successor product package checkpoint drift",
    )
    successor_closure = successor.get("recursive_elf_closure", {})
    require(
        successor_closure.get("root_artifacts") == 223
        and successor_closure.get("objects") == 186
        and successor_closure.get("edges") == 1355
        and successor_closure.get("custom_objects") == 180
        and successor_closure.get("host_objects") == 6
        and successor_closure.get("external_objects") == 0
        and successor_closure.get("unresolved_edges") == 0
        and successor_closure.get("resolution_conflicts") == 0
        and successor_closure.get("parse_errors") == 0
        and successor_closure.get("discovery_errors") == 0,
        "successor product recursive ELF closure drift",
    )
    successor_installation = successor.get("installation", {})
    require(
        successor_installation.get("installation_receipt_sha256")
        == "586153c0a78d8ddfecdaf9c75ce681beec4cee3f26906aaa55a96710cd973239"
        and successor_installation.get("installed_package_verified") is True
        and successor_installation.get("overwrite_performed") is False
        and successor_installation.get("root_ownership_complete") is False,
        "successor product installation drift",
    )
    successor_resource = successor.get("resource_observation", {})
    require(
        successor_resource.get("observation_sha256")
        == "03172d8228622bacaf2f22ecc5f0e17226ff4628040cd9ab3246e6f61ff64d54"
        and successor_resource.get("processor_ids") == [0, 1, 2, 3, 4, 5]
        and successor_resource.get("cpu_slots") == 6
        and successor_resource.get("memory_bytes") == 12884901888
        and successor_resource.get("io_slots") == 4,
        "successor native resource observation drift",
    )
    successor_collision = successor.get("unprivileged_collision_observation", {})
    require(
        successor_collision.get("observation_sha256")
        == "92dc1d08cc33b11508849de0498223650c30674ec752cc3880c7e209ad001125"
        and successor_collision.get("collisions") == []
        and successor_collision.get("accepted") is False
        and len(successor_collision.get("inspection_errors", [])) == 4,
        "incomplete successor collision observation was erased or promoted",
    )
    unicode_access = progress.get("product_unicode_access_mechanism", {})
    require(
        unicode_access.get("implementation_commit")
        == "50f3a29044c96497ba60cdbbdc12921e6f67539c"
        and unicode_access.get("state")
        == "implemented-and-controlled-integration-proven-successor-package-built-installed-and-resource-observed-physical-activation-pending",
        "product Unicode public-access checkpoint drift",
    )
    contains_all(
        unicode_access.get("implemented_boundaries", []),
        (
            "production PostgreSQL batch operation",
            "exact Unicode evidence epoch",
            "canonical native perfcache access law",
            "normalized deposited PostgreSQL state",
            "reverse lookup",
            "stale expected epochs",
            "current build module",
        ),
        "product Unicode public-access boundary narrowed",
    )
    unicode_activation = progress.get("product_unicode_activation_controller", {})
    require(
        unicode_activation.get("implementation_commit")
        == "54d6f9d00d7a52556486a2758a4b55bb3cbe1e44"
        and unicode_activation.get("state")
        == "implemented-and-controlled-integration-proven-successor-package-and-physical-execution-pending",
        "product Unicode activation-controller checkpoint drift",
    )
    contains_all(
        unicode_activation.get("implemented_boundaries", []),
        (
            "exact contract",
            "native domain-separated BLAKE3 provider",
            "all thirty-three source files",
            "exactly once",
            "2230150-frame",
            "post-commit recovery",
            "cold application-role direct readback",
            "typed durable failure receipt",
        ),
        "product Unicode activation-controller boundary narrowed",
    )
    contains_all(
        unicode_activation.get("product_boundary", ""),
        (
            "installed successor package",
            "not executed",
            "accepted root collision inspection",
            "physical cluster activation",
            "no Unicode product activation receipt",
        ),
        "Unicode activation controller was promoted beyond evidence",
    )
    require(postgresql.get("implementation_commit") == "16126c4a4a90a6710528f94aacb401cf45fdea66", "historical PostgreSQL composer checkpoint drift")
    contains_all(work.get("whole_product_reason", ""), ("Unicode", "numerical highway", "not source-family ingestion"), "active work lost its product reason")
    contains_all(work.get("immediate_implementation_boundary", []), ("authorized traversal", "cluster plan", "PostgreSQL 18.6", "accepted Laplace package", "1114112", "without a second semantic calculation", "direct and reverse", "restart", "product-root activation receipt"), "Unicode product-activation boundary was narrowed")
    contains_all(work.get("nonclaims", []), ("not yet established", "root ownership", "not established", "not yet product activated", "not seeded", "not implemented", "not released"), "Unicode activation work was promoted beyond evidence")
    github = document.get("github_observation", {})
    require(github.get("main_commit") == document.get("repository", {}).get("base_commit"), "GitHub main and continuation base diverged")
    require(github.get("product_cluster_project_status") == "In Progress", "active product-cluster work returned to Todo")
    require(github.get("unicode_product_activation_issue", {}).get("state") == "open", "Unicode product activation issue was prematurely closed")
    runtime_pr = github.get("product_runtime_pull_request", {})
    require(
        runtime_pr.get("number") == 74
        and runtime_pr.get("state") == "merged"
        and runtime_pr.get("draft") is False
        and runtime_pr.get("main_commit")
        == "3a74773608ede6d7fde8fc13c4bef6ffa5369aed",
        "runtime PR merge observation drift",
    )
    contains_all(runtime_pr.get("proof_state", ""), ("implementation merged", "product cluster activation", "public Unicode Tier-0/reverse perfcache access", "Unicode product activation controller", "successor package", "root ownership", "physical cluster activation", "Unicode activation", "seed", "release remain false"), "merged runtime PR was promoted beyond evidence")
    retired = {item.get("number"): item for item in github.get("retired_dependency_pull_requests", [])}
    require(set(retired) == {31, 35} and all(item.get("state") == "closed" for item in retired.values()), "superseded dependency PR retirement drift")
    contains_all(list(retired.values()), ("reconciled into draft PR 74", "source branch retained as history"), "dependency reconciliation evidence was lost")
    require(github.get("product_path_gate_issue") == 54 and github.get("required_product_path_gate_present") is False, "product-path gate observation drift")
    contains_all(github.get("observed_required_checks", []), ("requirements", "native (linux-dev)", "native (linux-sanitize)"), "required-check observation narrowed")
    require(github.get("github_environments") == 0 and github.get("github_deployments") == 0 and github.get("github_releases") == 0, "GitHub integration state was promoted to delivery state")
    require(github.get("repository_runner", {}).get("name") == "hart-server-refactor", "repository runner observation lost")
    runs = {item.get("workflow"): item for item in github.get("latest_main_workflow_runs", [])}
    require(runs.get("clean-room-ci", {}).get("linux_dev_registered_tests") == 251 and runs.get("clean-room-ci", {}).get("linux_sanitize_registered_tests") == 251, "published hosted test-count observation drift")
    require(runs.get("custom-stack-ci", {}).get("registered_tests") == 279 and runs.get("custom-stack-ci", {}).get("observed_postgresql_version") == "18.3", "published custom-stack observation drift")
    if verify_physical:
        worktree = Path(document.get("repository", {}).get("active_worktree", ""))
        if worktree.is_dir() and (worktree / ".git").exists():
            repository = document.get("repository", {})
            require(git_output(worktree, "rev-parse", "origin/main") == repository.get("base_commit"), "continuation main base commit is stale")
            require(git_output(worktree, "branch", "--show-current") == repository.get("active_branch"), "continuation active branch is stale")
            checkpoint = repository.get("published_checkpoint_commit")
            require(git_output(worktree, "cat-file", "-t", checkpoint) == "commit", "published composition checkpoint is missing")
            subprocess.run(
                ["git", "merge-base", "--is-ancestor", checkpoint, "HEAD"],
                cwd=worktree,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            observed_head = repository.get("observed_branch_head")
            require(git_output(worktree, "cat-file", "-t", observed_head) == "commit", "observed branch checkpoint is missing")
            subprocess.run(
                ["git", "merge-base", "--is-ancestor", observed_head, "HEAD"],
                cwd=worktree,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        package_receipt_path = Path(successful_postgresql.get("package_receipt", ""))
        if package_receipt_path.is_file():
            require(
                physical_file_digest(package_receipt_path)
                == successful_postgresql.get("package_receipt_sha256"),
                "physical PostgreSQL package receipt digest differs from continuation state",
            )
            physical_package_receipt = load(package_receipt_path)
            physical_provider = physical_package_receipt.get(
                "installed_runtime_provider", {}
            )
            require(
                physical_package_receipt.get("build_input_id")
                == successful_postgresql.get("build_input_id")
                and physical_package_receipt.get("tree_sha256")
                == successful_postgresql.get("package_tree_sha256")
                and physical_package_receipt.get("file_count")
                == successful_postgresql.get("package_file_count")
                and physical_package_receipt.get("total_file_bytes")
                == successful_postgresql.get("package_total_file_bytes")
                and physical_provider.get("provider_selection_sha256")
                == installed_provider.get("selection_sha256")
                and physical_provider.get("provider_sha256")
                == installed_provider.get("provider_sha256"),
                "physical PostgreSQL package receipt identity differs from continuation state",
            )
            physical_closure = physical_package_receipt.get(
                "recursive_elf_closure", {}
            )
            require(
                physical_closure.get("report_sha256")
                == recursive_elf.get("receipt_sha256")
                and physical_package_receipt.get("recursive_elf_closure_verified")
                is True,
                "physical PostgreSQL recursive closure differs from continuation state",
            )
        accepted_postgresql_receipt_path = Path(
            accepted_postgresql.get("package_receipt", "")
        )
        require(
            accepted_postgresql_receipt_path.is_file(),
            "accepted PostgreSQL package receipt is missing from the observed machine",
        )
        require(
            physical_file_digest(accepted_postgresql_receipt_path)
            == accepted_postgresql.get("package_receipt_sha256"),
            "accepted PostgreSQL package receipt digest differs from continuation state",
        )
        physical_accepted_postgresql = load(accepted_postgresql_receipt_path)
        physical_accepted_postgresql_closure = physical_accepted_postgresql.get(
            "recursive_elf_closure", {}
        )
        require(
            physical_accepted_postgresql.get("build_input_id")
            == accepted_postgresql.get("build_input_id")
            and physical_accepted_postgresql.get("tree_sha256")
            == accepted_postgresql.get("package_tree_sha256")
            and physical_accepted_postgresql.get("file_count")
            == accepted_postgresql.get("package_file_count")
            and physical_accepted_postgresql.get("total_file_bytes")
            == accepted_postgresql.get("package_total_file_bytes")
            and physical_accepted_postgresql.get("build_input_closure_complete") is True
            and physical_accepted_postgresql.get(
                "runtime_provider_qualification_complete"
            )
            is True
            and physical_accepted_postgresql.get("activation_eligible") is True
            and physical_accepted_postgresql_closure.get("report_sha256")
            == accepted_postgresql_closure.get("receipt_sha256"),
            "physical accepted PostgreSQL package identity or proof state differs from continuation state",
        )
        for accepted_postgresql_evidence_path, accepted_postgresql_evidence_sha256 in (
            (
                accepted_postgresql_overlay.get("receipt", ""),
                accepted_postgresql_overlay.get("receipt_sha256"),
            ),
            (
                accepted_runtime_qualification.get("receipt", ""),
                accepted_runtime_qualification.get("receipt_sha256"),
            ),
            (
                accepted_postgresql_closure.get("receipt", ""),
                accepted_postgresql_closure.get("receipt_sha256"),
            ),
        ):
            accepted_evidence_path = Path(accepted_postgresql_evidence_path)
            require(
                accepted_evidence_path.is_file(),
                f"accepted PostgreSQL evidence receipt is missing: {accepted_evidence_path}",
            )
            require(
                physical_file_digest(accepted_evidence_path)
                == accepted_postgresql_evidence_sha256,
                f"accepted PostgreSQL evidence receipt digest differs: {accepted_evidence_path}",
            )
        complete_product_receipt_path = Path(complete_product.get("package_receipt", ""))
        require(
            complete_product_receipt_path.is_file(),
            "complete product package receipt is missing from the observed machine",
        )
        require(
            physical_file_digest(complete_product_receipt_path)
            == complete_product.get("package_receipt_sha256"),
            "complete product package receipt digest differs from continuation state",
        )
        physical_complete_product = load(complete_product_receipt_path)
        physical_complete_closure = physical_complete_product.get(
            "recursive_elf_closure", {}
        )
        require(
            physical_complete_product.get("package_id")
            == complete_product.get("package_id")
            and physical_complete_product.get("plan_sha256")
            == complete_product.get("plan_sha256")
            and physical_complete_product.get("file_count")
            == complete_product.get("package_file_count")
            and physical_complete_product.get("symlink_count")
            == complete_product.get("package_symlink_count")
            and physical_complete_product.get("total_file_bytes")
            == complete_product.get("package_total_file_bytes")
            and physical_complete_product.get("build_input_closure_complete") is True
            and physical_complete_product.get("activation_eligible") is True
            and physical_complete_product.get("product_activated") is False
            and physical_complete_closure.get("report_sha256")
            == complete_closure.get("receipt_sha256"),
            "physical complete product package identity or proof state differs from continuation state",
        )
        for complete_product_evidence_path, complete_product_evidence_sha256 in (
            (
                complete_product.get("package_manifest", ""),
                complete_product.get("package_manifest_sha256"),
            ),
            (
                complete_closure.get("receipt", ""),
                complete_closure.get("receipt_sha256"),
            ),
        ):
            complete_evidence_path = Path(complete_product_evidence_path)
            require(
                complete_evidence_path.is_file(),
                f"complete product package evidence is missing: {complete_evidence_path}",
            )
            require(
                physical_file_digest(complete_evidence_path)
                == complete_product_evidence_sha256,
                f"complete product package evidence digest differs: {complete_evidence_path}",
            )
        complete_product_physical_root = Path(complete_product.get("physical_root", ""))
        require(
            complete_product_physical_root.is_dir(),
            "complete staged product root is missing from the observed machine",
        )
        installation_receipt_path = Path(installation.get("receipt", ""))
        require(
            installation_receipt_path.is_file()
            and physical_file_digest(installation_receipt_path)
            == installation.get("receipt_sha256"),
            "physical package installation receipt differs from continuation state",
        )
        physical_installation = load(installation_receipt_path)
        immutable_product_release = Path(installation.get("installed_release", ""))
        require(
            physical_installation.get("installation_receipt_sha256")
            == installation.get("installation_receipt_sha256")
            and physical_installation.get("installed_package_verified") is True
            and immutable_product_release.is_dir(),
            "physical immutable package installation differs from continuation state",
        )
        for closure_key in ("receipt", "independent_reverification"):
            closure_value = recursive_elf.get(closure_key, {})
            if closure_key == "receipt":
                closure_path = Path(str(closure_value))
                closure_sha256 = recursive_elf.get("receipt_sha256")
            else:
                closure_path = Path(closure_value.get("receipt", ""))
                closure_sha256 = closure_value.get("receipt_sha256")
            if closure_path.is_file():
                require(
                    physical_file_digest(closure_path) == closure_sha256,
                    "physical PostgreSQL recursive closure receipt digest differs from continuation state",
                )
        require(
            not Path("/opt/laplace/current").exists(),
            "continuation claims an inert product but /opt/laplace/current exists",
        )
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
    contains_all(semantics, ("implementation capability", "not a source runtime order", "cyclic", "source-profile world admission", "foundational seed completion", "separate states", "typed universal AST", "complete public program", "measured effective support", "sparse_addressability", "all-pairs noise surfaces"), "operation graph semantics collapsed")
    cycles = document.get("runtime_cycles", {})
    require(set(cycles) == {"observe_calculate_realize", "evidence_learning", "calculus_extension", "model_symmetry"}, "whole machine cycles were omitted")
    stages = {stage.get("id"): stage for stage in document.get("stages", [])}
    required = {"framework.execution", "machine.handle-exceptions", "substrate.compose-physicality", "substrate.bulk-deposit", "substrate.highway", "evidence.record-lineage", "world.admit-witnesses", "evidence.adjudicate", "query.guidance-search", "cognition.realize-effect", "learning.discovery-ooda", "model.ingest-generate", "product.materialize-entity-world", "runtime.federate-nodes"}
    require(required.issubset(stages), "whole capability graph was narrowed")
    require("seed.heterogeneous" not in stages, "monolithic seed stage returned")
    require(set(stages["world.admit-witnesses"].get("depends_on", [])) == {"substrate.highway", "substrate.bulk-deposit", "evidence.record-lineage"}, "world admission dependencies drift")
    require(stages["evidence.adjudicate"].get("depends_on") == ["evidence.record-lineage"], "evidence capability incorrectly waits for complete seed")
    require(stages["model.ingest-generate"].get("depends_on", [None])[0] == "world.admit-witnesses", "models bypass shared world admission")
    require("LP-MODEL-006" in stages["model.ingest-generate"].get("product_requirements", []), "typed model behavior and effective-support law missing")
    require(71 in stages["model.ingest-generate"].get("github_issues", []), "model behavior issue ownership drift")
    require("LP-RECIPE-001" in stages["framework.execution"].get("product_requirements", []), "recipe compiler missing from framework")
    require("LP-COHESION-001" in stages["framework.execution"].get("product_requirements", []), "whole-route cohesion missing from framework")
    require(70 in stages["framework.execution"].get("github_issues", []), "whole-route cohesion issue ownership drift")
    require(72 in stages["substrate.bulk-deposit"].get("github_issues", []), "storage economics issue ownership drift")
    contains_all(stages["substrate.bulk-deposit"], ("workload-proven indexes", "outside semantic authority", "with-and-without acceleration", "bloat byte census"), "paid addressability execution receipts were narrowed")
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
    contains_all(stages["substrate.bulk-deposit"].get("implementation", {}), ("merged pull request 73", "semantic parity", "provider-independent", "six provider mutants", "five-sample", "500000-records-per-second", "product activation"), "composition proof state drift")
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

    def test_codex_generated_context_cannot_impersonate_inventor_evidence(self) -> None:
        records = [
            {
                "timestamp": "2026-08-26T00:00:00Z",
                "type": "response_item",
                "payload": {
                    "type": "message",
                    "id": "direct-1",
                    "role": "user",
                    "content": [{"type": "input_text", "text": "direct inventor text"}],
                },
            },
            {
                "timestamp": "2026-08-26T00:00:01Z",
                "type": "response_item",
                "payload": {
                    "type": "message",
                    "id": "generated-environment",
                    "role": "user",
                    "content": [{"type": "input_text", "text": "<environment_context>\n  <current_date>2026-08-26</current_date>\n</environment_context>"}],
                },
            },
            {
                "timestamp": "2026-08-26T00:00:02Z",
                "type": "response_item",
                "payload": {
                    "type": "message",
                    "id": "generated-goal",
                    "role": "user",
                    "content": [{"type": "input_text", "text": "<codex_internal_context source=\"goal\">\nContinue working.\n</codex_internal_context>\n"}],
                },
            },
            {
                "timestamp": "2026-08-26T00:00:03Z",
                "type": "response_item",
                "payload": {
                    "type": "message",
                    "id": "direct-quoted-wrapper",
                    "role": "user",
                    "content": [{"type": "input_text", "text": "Do not treat <environment_context> as my prose."}],
                },
            },
        ]
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "session.jsonl"
            source.write_text("".join(json.dumps(record) + "\n" for record in records), encoding="utf-8")
            result = subprocess.run(
                [str(ROOT / "tools" / "audit" / "index-human-messages.sh"), "codex", str(source)],
                check=True,
                capture_output=True,
                text=True,
            )
        indexed = [json.loads(line) for line in result.stdout.splitlines() if line]
        self.assertEqual([record["message_id"] for record in indexed], ["direct-1", "direct-quoted-wrapper"])

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

    def test_mutation_failed_runtime_promoted_to_accepted_package_is_detected(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        mutant["active_work"]["implementation_progress"]["runtime_package"]["state"] = "accepted-package-receipt-issued"
        with self.assertRaisesRegex(ValueError, "runtime package proof state"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_runtime_provider_promoted_without_qualification_is_detected(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        qualification = mutant["active_work"]["implementation_progress"][
            "runtime_package"
        ]["runtime_provider_qualification"]
        qualification["state"] = "accepted"
        qualification["current_host_is_accepted"] = True
        with self.assertRaisesRegex(ValueError, "promoted without qualification"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_build_toolchain_promoted_to_product_runtime_is_detected(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        toolchain = mutant["active_work"]["implementation_progress"][
            "build_toolchain_package"
        ]
        toolchain["product_runtime_activation_eligible"] = True
        toolchain["product_activation_occurred"] = True
        with self.assertRaisesRegex(ValueError, "build toolchain was promoted"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_rejected_postgresql_execution_is_promoted(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        rejected = mutant["active_work"]["implementation_progress"][
            "postgresql_product_package"
        ]["previous_rejected_execution"]
        rejected["package_receipt_issued"] = True
        rejected["product_activation_occurred"] = True
        with self.assertRaisesRegex(ValueError, "rejected PostgreSQL execution"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_latest_rejected_postgresql_execution_is_promoted(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        rejected = mutant["active_work"]["implementation_progress"][
            "postgresql_product_package"
        ]["latest_rejected_execution"]
        rejected["compilation_started"] = True
        rejected["package_receipt_issued"] = True
        with self.assertRaisesRegex(ValueError, "latest rejected PostgreSQL"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_inert_postgresql_package_is_promoted(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        package = mutant["active_work"]["implementation_progress"][
            "postgresql_product_package"
        ]["latest_successful_package_execution"]
        package["build_input_closure_complete"] = True
        package["activation_eligible"] = True
        with self.assertRaisesRegex(ValueError, "inert PostgreSQL package"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_recursive_elf_closure_is_erased(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        package = mutant["active_work"]["implementation_progress"][
            "postgresql_product_package"
        ]["latest_successful_package_execution"]
        package["recursive_elf_closure_verified"] = False
        with self.assertRaisesRegex(ValueError, "inert PostgreSQL package"):
            validate_continuation(mutant, verify_physical=False)

    def test_mutation_bootstrap_successor_is_promoted_without_package(self) -> None:
        mutant = copy.deepcopy(self.continuation)
        successor = mutant["active_work"]["implementation_progress"][
            "postgresql_product_package"
        ]["next_successor_build_boundary"]
        successor["package_receipt_issued"] = True
        with self.assertRaisesRegex(ValueError, "bootstrap successor was promoted"):
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
