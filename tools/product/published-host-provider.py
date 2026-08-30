#!/usr/bin/env python3
"""Verify the historical PostgreSQL host-provider receipt carried by a publication.

A PostgreSQL package publication retains the exact host-provider receipt that qualified
that historical build. Product composition verifies that retained receipt as immutable
historical provenance; it must not requalify the already-built package against whatever
ambient /usr or kernel happens to exist on the later composition host.

Fresh PostgreSQL builds still use tools/postgresql/host-provider.py verify/verify-inputs
to prove the live build inputs they actually consume.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any, Sequence


HOST_PROVIDER_PATH = Path(__file__).resolve().parents[1] / "postgresql/host-provider.py"
SPEC = importlib.util.spec_from_file_location(
    "laplace_postgresql_host_provider", HOST_PROVIDER_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {HOST_PROVIDER_PATH}")
HOST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOST)


def verify_published_receipt(receipt: dict[str, Any]) -> dict[str, Any]:
    """Verify retained historical provenance without touching ambient provider paths."""
    HOST.verify_identity(receipt)
    if (
        receipt.get("scope") != "build-time-provider-only"
        or receipt.get("product_runtime_authority") is not False
    ):
        raise HOST.ProviderError("published host provider receipt scope differs")
    return receipt


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
    verified = verify_published_receipt(receipt)
    print(json.dumps(verified, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (HOST.ProviderError, OSError, json.JSONDecodeError) as error:
        print(f"published-postgresql-host-provider: {error}", file=sys.stderr)
        raise SystemExit(1) from error
