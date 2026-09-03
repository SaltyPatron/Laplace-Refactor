#!/usr/bin/env python3
"""Compute non-secret source identities for the product activation trust boundary."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
from typing import Any, Mapping, Sequence


REPOSITORY = Path(__file__).resolve().parents[2]
GATEWAY_PATH = Path(__file__).with_name("product_activation_gateway.py")
INSTALLER_PATH = Path(__file__).with_name("install_product_activation_gateway.py")


def load_module(name: str, path: Path) -> Any:
    specification = importlib.util.spec_from_file_location(name, path)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


gateway = load_module("laplace_activation_preflight_gateway", GATEWAY_PATH)
installer = load_module("laplace_activation_preflight_installer", INSTALLER_PATH)
activation = gateway.activation


def source_preflight(
    repository: Path,
    contract_path: Path,
    environment: Mapping[str, str],
) -> dict[str, str]:
    repository = repository.resolve()
    contract = activation.load_json(contract_path)
    activation.validate_contract(contract)
    secret_name = contract["request"]["secret_environment"]
    secret = environment.get(secret_name)
    if not secret:
        raise activation.ActivationGatewayError(
            f"required deployment secret is absent: {secret_name}"
        )
    key = activation.decode_key(secret)
    bundle = installer.bundle_manifest(installer.exact_sources(repository, contract))
    return {
        "schema": "laplace.product-activation-source-preflight/v1",
        "bundle_id": bundle["bundle_id"],
        "contract_sha256": activation.sha256_bytes(activation.canonical_bytes(contract)),
        "key_fingerprint_sha256": gateway.activation_key_fingerprint(key),
    }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=str(REPOSITORY))
    parser.add_argument(
        "--contract", default="contracts/product-activation-gateway.json"
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    repository = Path(arguments.repository).resolve()
    contract = Path(arguments.contract)
    if not contract.is_absolute():
        contract = repository / contract
    result = source_preflight(repository, contract, os.environ)
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except activation.ActivationGatewayError as error:
        print(f"product activation preflight: {error}", file=sys.stderr)
        raise SystemExit(1)
