#!/usr/bin/env python3
"""Resolve only explicitly registered final-convergence merge conflicts.

This script is intentionally fail-closed. It is run only after `git merge` has
reported conflicts on the single convergence branch. It verifies the exact
conflict set, resolves only the named diff3 hunks, applies the documented
cross-lineage synthesis, stages those paths, and refuses any unknown conflict.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


UCDXML_HEAD = "fix/native-cognition-execution"
UCDXML_CONFLICTS = {
    Path("engine/src/cognition_prompt_admission.cpp"),
    Path("engine/src/tabular_source.cpp"),
    Path("integrations/postgresql/extension/src/source_admission_pg.c"),
}


def git(*args: str, check: bool = True) -> str:
    proc = subprocess.run(
        ["git", *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed ({proc.returncode}): {proc.stderr.strip()}"
        )
    return proc.stdout


def unresolved() -> set[Path]:
    return {
        Path(line.strip())
        for line in git("diff", "--name-only", "--diff-filter=U").splitlines()
        if line.strip()
    }


def resolve_diff3(text: str, choice: str) -> str:
    if choice not in {"ours", "theirs"}:
        raise ValueError(choice)
    lines = text.splitlines(keepends=True)
    output: list[str] = []
    index = 0
    resolved = 0
    while index < len(lines):
        if not lines[index].startswith("<<<<<<< "):
            output.append(lines[index])
            index += 1
            continue

        resolved += 1
        index += 1
        ours: list[str] = []
        while index < len(lines) and not lines[index].startswith("||||||| "):
            ours.append(lines[index])
            index += 1
        if index >= len(lines):
            raise RuntimeError("missing diff3 base marker")

        index += 1
        while index < len(lines) and not lines[index].startswith("======="):
            index += 1
        if index >= len(lines):
            raise RuntimeError("missing diff3 separator")

        index += 1
        theirs: list[str] = []
        while index < len(lines) and not lines[index].startswith(">>>>>>> "):
            theirs.append(lines[index])
            index += 1
        if index >= len(lines):
            raise RuntimeError("missing diff3 end marker")
        index += 1
        output.extend(theirs if choice == "theirs" else ours)

    if resolved == 0:
        raise RuntimeError("expected at least one diff3 conflict marker")
    return "".join(output)


def resolve_path(path: Path, choice: str) -> None:
    git("checkout", "--conflict=diff3", "--", str(path))
    path.write_text(
        resolve_diff3(path.read_text(encoding="utf-8"), choice),
        encoding="utf-8",
    )


def preserve_effect_publication_law() -> None:
    path = Path("engine/src/content_admission.cpp")
    text = path.read_text(encoding="utf-8")
    old = """    *producer = laplace_framework_producer_v1{};
    if (admission->working_set == nullptr) {
        return LAPLACE_CONTENT_ADMISSION_NO_PUBLICATION_REQUIRED;
    }
    if (laplace_composition_working_set_producer(
            admission->working_set, producer) != LAPLACE_COMPOSITION_OK) {
        *producer = laplace_framework_producer_v1{};
        return LAPLACE_CONTENT_ADMISSION_COMPOSITION_FAILURE;
    }
    return LAPLACE_CONTENT_ADMISSION_OK;
"""
    new = """    *producer = laplace_framework_producer_v1{};
    if (admission->working_set == nullptr) {
        return LAPLACE_CONTENT_ADMISSION_NO_PUBLICATION_REQUIRED;
    }
    std::uint32_t effect_disposition = LAPLACE_FRAMEWORK_EFFECT_NONE;
    if (laplace_composition_working_set_effect_disposition_get(
            admission->working_set, &effect_disposition) != LAPLACE_COMPOSITION_OK) {
        return LAPLACE_CONTENT_ADMISSION_COMPOSITION_FAILURE;
    }
    if (effect_disposition == LAPLACE_FRAMEWORK_EFFECT_NONE) {
        return LAPLACE_CONTENT_ADMISSION_NO_PUBLICATION_REQUIRED;
    }
    if (effect_disposition != LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT) {
        return LAPLACE_CONTENT_ADMISSION_COMPOSITION_FAILURE;
    }
    if (laplace_composition_working_set_producer(
            admission->working_set, producer) != LAPLACE_COMPOSITION_OK) {
        *producer = laplace_framework_producer_v1{};
        return LAPLACE_CONTENT_ADMISSION_COMPOSITION_FAILURE;
    }
    return LAPLACE_CONTENT_ADMISSION_OK;
"""
    if text.count(old) != 1:
        raise RuntimeError("content-admission producer shape changed")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def reconcile_tabular_provider_boundary() -> None:
    path = Path("engine/src/tabular_source.cpp")
    text = path.read_text(encoding="utf-8")

    old_validation = """    if (input == nullptr || common_providers == nullptr ||
        common_provider_count == 0u ||
        common_provider_count >= static_cast<std::uint64_t>(SIZE_MAX) ||
        plan == nullptr || *plan != nullptr) {
"""
    new_validation = """    if (input == nullptr || plan == nullptr || *plan != nullptr ||
        (common_provider_count != 0u && common_providers == nullptr) ||
        common_provider_count >= static_cast<std::uint64_t>(SIZE_MAX)) {
"""
    if text.count(old_validation) != 1:
        raise RuntimeError("tabular provider validation shape changed")
    text = text.replace(old_validation, new_validation, 1)

    grammar_anchor = """            providers.push_back(fixed_width_provider.provider);
        }

        static constexpr char FallbackDelimitedMediaType[] =
"""
    grammar_replacement = """            providers.push_back(fixed_width_provider.provider);
        }
        if (providers.empty()) {
            status = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            goto recursive_failure;
        }

        static constexpr char FallbackDelimitedMediaType[] =
"""
    if text.count(grammar_anchor) != 1:
        raise RuntimeError("tabular grammar-provider anchor changed")
    text = text.replace(grammar_anchor, grammar_replacement, 1)

    generic = """extern \"C\" laplace_tabular_source_status
laplace_source_decomposition_plan_create(
    const laplace_tabular_source_input* input,
    const laplace_decomposition_provider_v1* providers,
    const std::uint64_t provider_count,
    laplace_tabular_source_plan** plan) {
    try {
        return recursive_admission::BuildRecursive(
            input, providers, provider_count, plan);
    } catch (const std::bad_alloc&) {
        return LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
    }
}

"""
    compatibility = generic + """extern \"C\" laplace_tabular_source_status
laplace_tabular_source_plan_create_recursive_with_providers(
    const laplace_tabular_source_input* input,
    const laplace_decomposition_provider_v1* common_providers,
    const std::uint64_t common_provider_count,
    laplace_tabular_source_plan** plan) {
    return laplace_source_decomposition_plan_create(
        input, common_providers, common_provider_count, plan);
}

"""
    if text.count(generic) != 1:
        raise RuntimeError("generic source-decomposition API shape changed")
    path.write_text(text.replace(generic, compatibility, 1), encoding="utf-8")


def resolve_ucdxml() -> None:
    actual = unresolved()
    if actual != UCDXML_CONFLICTS:
        raise RuntimeError(
            "unexpected UCDXML conflict set: "
            f"expected={sorted(map(str, UCDXML_CONFLICTS))} "
            f"actual={sorted(map(str, actual))}"
        )

    # Generic prompt/content ownership from #301; preserve all non-conflicting
    # current-main edits already produced by Git's three-way merge.
    resolve_path(Path("engine/src/cognition_prompt_admission.cpp"), "theirs")

    # Keep #302's stronger generic source-provider identity/cache API in the
    # overlapping hunks, then add #301's valid zero-extra-provider semantics.
    resolve_path(Path("engine/src/tabular_source.cpp"), "ours")

    # #302 is the newer horizontal product owner: UAX comes from active canonical
    # Unicode state and the perfcache epoch is pinned across execution. #301's
    # UCDXML/evidence machinery is independent and auto-merges outside these hunks.
    resolve_path(
        Path("integrations/postgresql/extension/src/source_admission_pg.c"), "ours"
    )

    preserve_effect_publication_law()
    reconcile_tabular_provider_boundary()

    paths = sorted(
        UCDXML_CONFLICTS
        | {
            Path("engine/src/content_admission.cpp"),
        }
    )
    git("add", "--", *(str(path) for path in paths))
    remaining = unresolved()
    if remaining:
        raise RuntimeError(f"unresolved conflicts remain: {sorted(map(str, remaining))}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--head", required=True)
    args = parser.parse_args()

    try:
        if args.head == UCDXML_HEAD:
            resolve_ucdxml()
        else:
            raise RuntimeError(f"no registered resolution for {args.head}")
    except Exception as error:
        print(f"final-convergence-resolve: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
