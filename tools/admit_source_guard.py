#!/usr/bin/env python3
"""Constrain installed source admission to the configured local source estate."""
from __future__ import annotations

import os
from pathlib import Path
import sys
from typing import Sequence

DEFAULT_SOURCE_ESTATE_ROOT = Path("/vault/Data")
SOURCE_ESTATE_ENV = "LAPLACE_SOURCE_ESTATE_ROOT"
DEFAULT_UNICODE_RELATIVE = Path("UCD/Public/UCD/latest")


class AdmissionGuardError(RuntimeError):
    pass


def configured_estate_root() -> Path:
    raw = os.environ.get(SOURCE_ESTATE_ENV)
    root = Path(raw) if raw else DEFAULT_SOURCE_ESTATE_ROOT
    if not root.is_absolute():
        raise AdmissionGuardError(
            f"{SOURCE_ESTATE_ENV} must be an absolute directory path"
        )
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise AdmissionGuardError(
            f"configured source estate is unavailable: {root}: {error}"
        ) from error
    if not resolved.is_dir():
        raise AdmissionGuardError(
            f"configured source estate is not a directory: {resolved}"
        )
    return resolved


def resolve_estate_directory(root: Path, value: str, label: str) -> Path:
    candidate = Path(value)
    if not candidate.is_absolute():
        raise AdmissionGuardError(f"{label} must be an absolute server path")
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as error:
        raise AdmissionGuardError(f"{label} is unavailable: {candidate}: {error}") from error
    if not resolved.is_dir():
        raise AdmissionGuardError(f"{label} is not a directory: {resolved}")
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise AdmissionGuardError(
            f"{label} is outside the configured source estate {root}: {resolved}"
        ) from error
    return resolved


def option_index(argv: Sequence[str], name: str) -> int | None:
    for index, value in enumerate(argv):
        if value == name:
            return index
        if value.startswith(name + "="):
            return index
    return None


def guarded_arguments(argv: Sequence[str], estate_root: Path | None = None) -> list[str]:
    values = list(argv)
    if any(value in {"-h", "--help"} for value in values[1:]) or len(values) < 3:
        return values

    root = configured_estate_root() if estate_root is None else estate_root.resolve(strict=True)
    if not root.is_dir():
        raise AdmissionGuardError(f"configured source estate is not a directory: {root}")

    values[2] = str(resolve_estate_directory(root, values[2], "source_root"))

    unicode_index = option_index(values, "--unicode-root")
    if unicode_index is None:
        unicode_root = resolve_estate_directory(
            root, str(root / DEFAULT_UNICODE_RELATIVE), "unicode_root"
        )
        values.extend(["--unicode-root", str(unicode_root)])
    else:
        option = values[unicode_index]
        if option == "--unicode-root":
            if unicode_index + 1 >= len(values):
                raise AdmissionGuardError("--unicode-root requires a path")
            values[unicode_index + 1] = str(
                resolve_estate_directory(
                    root, values[unicode_index + 1], "unicode_root"
                )
            )
        else:
            _, raw = option.split("=", 1)
            values[unicode_index] = "--unicode-root=" + str(
                resolve_estate_directory(root, raw, "unicode_root")
            )
    return values


def main(argv: Sequence[str] | None = None) -> int:
    values = list(sys.argv if argv is None else argv)
    core = Path(__file__).resolve().parent / "laplace-admit-source-core"
    try:
        if not core.is_file() or core.is_symlink() or not os.access(core, os.X_OK):
            raise AdmissionGuardError(
                f"installed source-admission core is unavailable: {core}"
            )
        guarded = guarded_arguments(values)
    except AdmissionGuardError as error:
        print(f"laplace-admit-source: {error}", file=sys.stderr)
        return 2

    os.execv(str(core), [str(core), *guarded[1:]])
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
