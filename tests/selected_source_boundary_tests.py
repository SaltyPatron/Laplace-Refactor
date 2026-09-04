#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOUNDARY = ROOT / "contracts" / "selected-source-boundary-20260903.json"
STALE_CILI_PROFILE = ROOT / "contracts" / "sources" / "cili-pwn-mappings-20240611.json"

EXPECTED_JOB_IDS = {
    "omw-2.0", "omw-index", "oewn-2025-plus", "cili-current",
    "semlink-current", "verbnet-current", "propbank-current", "verbatlas-1.1",
    "ud-2.18", "tatoeba-sentences", "tatoeba-links", "tatoeba-audio-map",
    "tatoeba-detailed", "tatoeba-cc0", "tatoeba-lists", "tatoeba-tags",
    "tatoeba-tags-detailed", "tatoeba-transcriptions", "tatoeba-user-languages",
    "tatoeba-users-sentences", "tatoeba-tag-metadata", "wiktionary-raw-2026-08-28",
    "atomic10x", "lichess-openings", "twic-1651", "twic-1652", "twic-1653",
    "twic-1654", "twic-1655", "twic-1656", "twic-1657", "twic-1658",
    "twic-1659", "twic-1660", "geonames-allcountries", "geonames-altnames-v2",
    "geonames-hierarchy", "geonames-country-info", "geonames-admin1",
    "geonames-admin2", "geonames-feature-codes", "geonames-language-codes",
    "geonames-timezones", "naturalearth-admin0-5.1.1",
    "naturalearth-populated-5.1.2", "hatecheck", "sghatecheck", "xstest",
    "social-bias-frames-v2", "social-chemistry-101", "civil-comments", "toxigen",
    "measuring-hate-speech", "prosocial-dialog", "real-toxicity-prompts",
}

EXPECTED_LANES = {"semantic": 8, "mutable": 26, "geo": 11, "safety": 10}
REQUIRED_TATOEBA = {
    "tatoeba-sentences", "tatoeba-links", "tatoeba-audio-map", "tatoeba-detailed",
    "tatoeba-cc0", "tatoeba-lists", "tatoeba-tags", "tatoeba-tags-detailed",
    "tatoeba-transcriptions", "tatoeba-user-languages", "tatoeba-users-sentences",
    "tatoeba-tag-metadata",
}
REQUIRED_PROFILE_IDS = {
    "unicode-ucd-17", "unicode-ducet-17", "iso-639-3-20260415", "cili-current",
    "pwn-3.0", "oewn-2025-plus", "omw-2.0", "wiktionary-20260828",
    "framenet-1.7", "framebase-2.0", "verbnet-current", "propbank-current",
    "semlink-current", "verbatlas-1.1", "conceptnet-5.7", "atomic2020",
    "atomic10x", "ud-2.18", "tatoeba-20260829", "opensubtitles-v2024",
    "project-gutenberg-curated", "geonames-20260903",
    "natural-earth-admin0-5.1.1", "natural-earth-populated-5.1.2",
    "twic-1651-1660", "lichess-openings", "syzygy-3-5", "hatecheck",
    "sghatecheck", "xstest", "civil-comments", "toxigen", "measuring-hate-speech",
    "social-bias-frames-v2", "prosocial-dialog", "social-chemistry-101",
    "real-toxicity-prompts", "code-authority-and-git", "interaction-execution-history",
    "model-estate-snackrifices",
}
REQUIRED_TERMINAL_EXCEPTIONS = {
    "dense-atomic": "absent",
    "wikipedia": "absent",
    "wikidata": "absent",
    "syzygy-7": "excluded-with-reason",
    "multilingual-hatecheck": "absent",
    "hatexplain": "unsupported-with-why-not",
}
REQUIRED_PROVIDER_GAPS = {
    "wn-lmf", "xml", "json", "jsonl", "rdf", "conllu", "parquet", "shapefile",
    "pgn", "tablebase", "model-container",
}
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
HEX32 = re.compile(r"^[0-9a-f]{32}$")


def load(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise AssertionError(f"{path} must contain one JSON object")
    return value


def validate(document: dict) -> list[str]:
    errors: list[str] = []
    if document.get("schema") != "laplace.configured-source-boundary/v1":
        errors.append("schema")
    if document.get("classification") != "authority-selected-closure-ledger-not-source-truth":
        errors.append("classification")
    if document.get("selected_at") != "2026-09-03":
        errors.append("selected-at")

    receipt = document.get("receipt", {})
    expected_receipt = {
        "jobs": 55, "unique_jobs": 55, "malformed": 0,
        "files": 2825, "bytes": 16913187330, "lanes": EXPECTED_LANES,
    }
    if receipt != expected_receipt:
        errors.append("receipt")

    jobs = document.get("acquisition_jobs", [])
    if not isinstance(jobs, list):
        return errors + ["jobs-not-list"]
    ids = [job.get("id") for job in jobs if isinstance(job, dict)]
    if len(jobs) != 55 or len(ids) != 55:
        errors.append("job-count")
    if set(ids) != EXPECTED_JOB_IDS or len(set(ids)) != len(ids):
        errors.append("job-identity-set")
    lane_counts = Counter(job.get("lane") for job in jobs if isinstance(job, dict))
    if dict(lane_counts) != EXPECTED_LANES:
        errors.append("lane-counts")
    if not REQUIRED_TATOEBA.issubset(set(ids)):
        errors.append("tatoeba-sidecars")

    for job in jobs:
        if not isinstance(job, dict):
            errors.append("job-shape")
            continue
        identity_fields = [name for name in ("commit", "sha256", "md5", "identity") if name in job]
        if len(identity_fields) != 1:
            errors.append(f"job-identity:{job.get('id')}")
            continue
        field = identity_fields[0]
        value = job[field]
        if field == "commit" and (not isinstance(value, str) or not HEX40.fullmatch(value)):
            errors.append(f"commit:{job.get('id')}")
        elif field == "sha256" and (not isinstance(value, str) or not HEX64.fullmatch(value)):
            errors.append(f"sha256:{job.get('id')}")
        elif field == "md5" and (not isinstance(value, str) or not HEX32.fullmatch(value)):
            errors.append(f"md5:{job.get('id')}")
        elif field == "identity" and value != "receipt-required":
            errors.append(f"untyped-identity:{job.get('id')}")

    overrides = document.get("selected_overrides", {})
    if overrides.get("cili-current", {}).get("commit") != "a895d7ecb18019dda3443f98901e59d81ce8722b":
        errors.append("cili-current-release")
    if overrides.get("naturalearth-admin0-5.1.1", {}).get("sha256") != "0f243aeac8ac6cf26f0417285b0bd33ac47f1b5bdb719fd3e0df37d03ea37110":
        errors.append("natural-earth-admin0")
    if overrides.get("naturalearth-populated-5.1.2", {}).get("sha256") != "29b901a2ae0a745741c0642123480e31101ed723086c4d0cae657a8d722c3b28":
        errors.append("natural-earth-populated")

    profiles = document.get("profile_obligations", [])
    profile_map = {item.get("id"): item for item in profiles if isinstance(item, dict)}
    if not REQUIRED_PROFILE_IDS.issubset(profile_map):
        errors.append("required-profiles")
    for profile_id, state in REQUIRED_TERMINAL_EXCEPTIONS.items():
        if profile_map.get(profile_id, {}).get("state") != state:
            errors.append(f"terminal-disposition:{profile_id}")
    if profile_map.get("model-estate-snackrifices", {}).get("state") != "selected-observed-profile-required":
        errors.append("model-estate-profile-required")
    if profile_map.get("atomic10x", {}).get("state") == profile_map.get("atomic2020", {}).get("state") == "selected":
        errors.append("atomic-lineage-collapse")

    provider_map = {
        item.get("id"): item.get("state")
        for item in document.get("provider_families", []) if isinstance(item, dict)
    }
    for provider in REQUIRED_PROVIDER_GAPS:
        if provider_map.get(provider) != "missing-common-provider":
            errors.append(f"provider-gap:{provider}")

    equivalence = {
        item.get("id"): item for item in document.get("equivalence", []) if isinstance(item, dict)
    }
    for group in ("cili-packages", "hatecheck-packages"):
        item = equivalence.get(group, {})
        if item.get("state") != "equivalent-packaging" or item.get("single_dependence_root") is not True:
            errors.append(f"equivalence:{group}")
    if equivalence.get("omw-legacy", {}).get("never_double_vote") is not True:
        errors.append("omw-double-vote")

    closure = document.get("closure", {})
    unresolved_jobs = any(job.get("identity") == "receipt-required" for job in jobs if isinstance(job, dict))
    unresolved_providers = any(state in {"missing-common-provider", "profile-required", "common-lowering-required"} for state in provider_map.values())
    if closure.get("configured_seed_complete") is not False:
        errors.append("premature-seed-completion")
    if not unresolved_jobs or not unresolved_providers:
        errors.append("ledger-no-longer-represents-known-open-boundary")

    return errors


def require_detected(mutant: dict, marker: str) -> None:
    errors = validate(mutant)
    if marker not in errors:
        raise AssertionError(f"deliberate defect was not detected: {marker}; errors={errors}")


def main() -> int:
    document = load(BOUNDARY)
    errors = validate(document)
    if errors:
        raise AssertionError(f"selected source boundary invalid: {errors}")

    stale_cili = load(STALE_CILI_PROFILE)
    stale_release = stale_cili.get("authority_and_release", {}).get("commit")
    selected_release = document["selected_overrides"]["cili-current"]["commit"]
    if stale_release == selected_release:
        raise AssertionError("stale mapping-only CILI profile unexpectedly impersonates selected current CILI boundary")
    if document["closure"]["configured_seed_complete"]:
        raise AssertionError("configured seed cannot close while selected CILI profile remains unresolved")

    missing_job = copy.deepcopy(document)
    missing_job["acquisition_jobs"] = missing_job["acquisition_jobs"][1:]
    require_detected(missing_job, "job-count")

    missing_sidecar = copy.deepcopy(document)
    missing_sidecar["acquisition_jobs"] = [
        job for job in missing_sidecar["acquisition_jobs"]
        if job.get("id") != "tatoeba-tag-metadata"
    ]
    require_detected(missing_sidecar, "tatoeba-sidecars")

    premature = copy.deepcopy(document)
    premature["closure"]["configured_seed_complete"] = True
    require_detected(premature, "premature-seed-completion")

    double_vote = copy.deepcopy(document)
    for item in double_vote["equivalence"]:
        if item.get("id") == "cili-packages":
            item["single_dependence_root"] = False
    require_detected(double_vote, "equivalence:cili-packages")

    print(
        "selected-source-boundary: jobs=55 files=2825 bytes=16913187330 "
        f"profiles={len(document['profile_obligations'])} configured_seed_complete=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
