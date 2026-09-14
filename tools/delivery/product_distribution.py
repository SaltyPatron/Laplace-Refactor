#!/usr/bin/env python3
"""Product distribution entry point including split controller implementation files."""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


_CORE_PATH = Path(__file__).with_name("product_distribution_core.py")
_SPEC = importlib.util.spec_from_file_location("laplace_product_distribution_core", _CORE_PATH)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot load product distribution core")
_core = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _core
_SPEC.loader.exec_module(_core)
_core.__file__ = str(Path(__file__).resolve())
_core.CONTROL_SOURCES = set(_core.CONTROL_SOURCES) | {
    "tools/postgresql/unicodectl_core.py",
    "tools/delivery/product_distribution_core.py",
}

for _name in dir(_core):
    if not _name.startswith("__"):
        globals()[_name] = getattr(_core, _name)
CONTROL_SOURCES = _core.CONTROL_SOURCES


def main(argv=None) -> int:
    return _core.main(argv)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except _core.DistributionError as error:
        print(f"product distribution: {error}", file=sys.stderr)
        raise SystemExit(1)
