#!/usr/bin/env python3
"""Compile the exact repository gateway bundle into an authenticated upgrade request."""

from __future__ import annotations

import argparse
import datetime as dt
import importlib.util
import os
from pathlib import Path
import sys


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", default=".")
    parser.add_argument("--contract", default="contracts/product-activation-gateway.json")
    parser.add_argument("--output", required=True)
    parser.add_argument("--actor", required=True)
    parser.add_argument("--workflow-run-id", required=True, type=int)
    args = parser.parse_args()

    repository = Path(args.repository).resolve()
    activation = load_module(
        "laplace_gateway_upgrade_activation",
        repository / "tools/delivery/product_activation.py",
    )
    installer = load_module(
        "laplace_gateway_upgrade_installer",
        repository / "tools/delivery/install_product_activation_gateway.py",
    )
    upgrade = load_module(
        "laplace_gateway_upgrade_compiler",
        repository / "tools/delivery/gateway_upgrade.py",
    )
    contract = activation.load_json(repository / args.contract)
    activation.validate_contract(contract)
    secret_name = contract["request"]["secret_environment"]
    secret = os.environ.get(secret_name, "")
    if not secret:
        raise RuntimeError(f"{secret_name} is required")
    key = activation.decode_key(secret)
    request = upgrade.build_request(
        repository,
        contract,
        installer.SOURCE_MAP,
        key,
        dt.datetime.now(dt.timezone.utc).replace(microsecond=0),
        args.actor,
        args.workflow_run_id,
    )
    output = Path(args.output)
    activation.atomic_write(output, activation.canonical_bytes(request) + b"\n", 0o600)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
