#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "cmake/cognition_operator.h.in",
    """#define LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING UINT32_C(@LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING@)\n\n""",
    """#define LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING UINT32_C(@LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING@)\n#define LAPLACE_COGNITION_OPERATOR_SOURCE_KNOWN_MASK \\\n    ((UINT32_C(1) << (LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY - UINT32_C(1))) | \\\n     (UINT32_C(1) << (LAPLACE_COGNITION_OPERATOR_SOURCE_TESTIMONY - UINT32_C(1))) | \\\n     (UINT32_C(1) << (LAPLACE_COGNITION_OPERATOR_SOURCE_DERIVED - UINT32_C(1))) | \\\n     (UINT32_C(1) << (LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING - UINT32_C(1))))\n\n""",
)
replace_once(
    "engine/src/cognition_operator_part00.inc",
    "(program.eligible_source_mask & ~UINT32_C(15)) != 0U",
    "(program.eligible_source_mask &\n         ~LAPLACE_COGNITION_OPERATOR_SOURCE_KNOWN_MASK) != 0U",
)
replace_once(
    "engine/src/target_observation_compile.cpp",
    "(program.eligible_source_mask & ~UINT32_C(7)) != 0U",
    "(program.eligible_source_mask &\n         ~LAPLACE_COGNITION_OPERATOR_SOURCE_KNOWN_MASK) != 0U",
)
replace_once(
    "engine/src/target_scope_plan.inc",
    "(slot.eligible_source_mask & ~UINT32_C(7)) != 0U",
    "(slot.eligible_source_mask &\n         ~LAPLACE_COGNITION_OPERATOR_SOURCE_KNOWN_MASK) != 0U",
)

excerpt = Path("ci-failure-test34.txt")
if excerpt.exists():
    excerpt.unlink()
Path("tools/repair_standing_source_mask.py").unlink()
Path(".github/workflows/repair-standing-source-mask.yml").unlink()
