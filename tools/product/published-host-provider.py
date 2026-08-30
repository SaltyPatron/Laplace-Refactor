#!/usr/bin/env python3
"""Replay a published PostgreSQL host-provider receipt for product composition.

The PostgreSQL receipt retains the exact host and kernel that qualified that historical
build. Product composition must reuse the exact receipted provider bytes without
pretending that a later composition run occurred on that historical kernel.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from typing import Sequence


HOST_PROVIDER_PATH = Path(__file__).resolve().parents[1] / "postgresql/host-provider.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_postgresql_host_provider", HOST_PROVIDER_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {HOST_PROVIDER_PATH}")
HOST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOST)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--receipt", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    receipt_path = Path(args.receipt).resolve()
    receipt = HOST.read_receipt(receipt_path)
    verified = HOST.verify_inputs(receipt)
    print(json.dumps(verified, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (HOST.ProviderError, OSError, json.JSONDecodeError) as error:
        print(f"published-postgresql-host-provider: {error}", file=sys.stderr)
        raise SystemExit(1) from error
