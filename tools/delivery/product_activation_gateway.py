#!/usr/bin/python3
"""Installed root entry point for exact whole-product activation.

The repository-side request compiler remains ``product_activation.py``.  The
installed gateway loads that implementation from the immutable bundle and
adapts receipt verification to the canonical newline-terminated JSON identity
law used by the PostgreSQL, Unicode, and Highway producers.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from typing import Any, Sequence


def implementation_path() -> Path:
    installed = Path(__file__).resolve().parent.parent / "controllers/product_activation_impl.py"
    if installed.is_file():
        return installed
    source = Path(__file__).with_name("product_activation.py")
    if source.is_file():
        return source
    raise RuntimeError("product activation implementation is absent")


def load_implementation() -> Any:
    path = implementation_path()
    specification = importlib.util.spec_from_file_location(
        "laplace_product_activation_gateway_impl", path
    )
    if specification is None or specification.loader is None:
        raise RuntimeError("cannot load product activation implementation")
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


activation = load_implementation()


def producer_document_identity(document: dict[str, Any], field: str) -> str:
    """Return the receipt identity emitted by newline-canonical controllers."""

    payload = {key: value for key, value in document.items() if key != field}
    return activation.sha256_bytes(activation.canonical_bytes(payload) + b"\n")


def validate_package_installation(
    contract: dict[str, Any], result: dict[str, Any], payload: dict[str, Any]
) -> None:
    package = payload["package"]
    expected_release = f"{contract['product']['package_release_root']}/{package['id']}"
    if (
        result.get("schema") != contract["product"]["package_installation_schema"]
        or result.get("phase") != "installed"
        or result.get("package_id") != package["id"]
        or result.get("package_manifest_sha256") != package["manifest_sha256"]
        or result.get("package_root") != expected_release
        or result.get("installation_root") != "/"
        or result.get("installed_release") != expected_release
        or result.get("source_physical_root")
        != str(Path(package["source_root"]).resolve())
        or result.get("source_package_verified") is not True
        or result.get("installed_package_verified") is not True
        or result.get("overwrite_performed") is not False
        or result.get("installation_receipt_sha256")
        != producer_document_identity(result, "installation_receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "product package installation result is not exact and complete"
        )


def validate_cluster_success(
    contract: dict[str, Any], result: dict[str, Any], package_id: str
) -> Path:
    operation = contract["operation"]
    if (
        result.get("schema") != operation["cluster_success_schema"]
        or result.get("phase") != operation["cluster_success_phase"]
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("active_target") != f"releases/{package_id}"
        or result.get("activation_receipt_sha256")
        != producer_document_identity(result, "activation_receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "cluster activation result is not exact and complete"
        )
    plan = activation.require_absolute(result.get("cluster_plan_path"), "cluster plan path")
    evidence_root = Path(contract["product"]["cluster_activation_root"]) / package_id
    activation.require_below(plan, evidence_root, "cluster plan")
    if not plan.is_file() or plan.is_symlink():
        raise activation.ActivationGatewayError("cluster plan evidence is absent")
    return plan


def validate_unicode_success(
    contract: dict[str, Any], result: dict[str, Any], package_id: str
) -> None:
    if (
        result.get("schema") != contract["operation"]["unicode_success_schema"]
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_public_readback_proven") is not True
        or result.get("reverse_inversion_proven") is not True
        or result.get("receipt_sha256")
        != producer_document_identity(result, "receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "Unicode activation result is not exact and complete"
        )


def validate_highway_success(
    contract: dict[str, Any], result: dict[str, Any], package_id: str
) -> None:
    if (
        result.get("schema") != contract["operation"]["highway_success_schema"]
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_application_readback_proven") is not True
        or result.get("receipt_sha256")
        != producer_document_identity(result, "receipt_sha256")
    ):
        raise activation.ActivationGatewayError(
            "Highway activation result is not exact and complete"
        )


def patch_implementation() -> None:
    activation.validate_package_installation = validate_package_installation
    activation.validate_cluster_success = validate_cluster_success
    activation.validate_unicode_success = validate_unicode_success
    activation.validate_highway_success = validate_highway_success


patch_implementation()


def main(argv: Sequence[str] | None = None) -> int:
    return activation.main(argv)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except activation.ActivationGatewayError as error:
        print(f"product activation gateway: {error}", file=sys.stderr)
        raise SystemExit(1)
