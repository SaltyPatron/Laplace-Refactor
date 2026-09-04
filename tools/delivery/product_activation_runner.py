#!/usr/bin/env python3
"""Activate the persistent DEV/BAT product as ``laplace-runner``.

This is the recurring product provider selected after one-time ``setup-host.sh``.
It owns package installation, PostgreSQL candidate/commit, Unicode, Highway, and
product receipts.  Root is never the product executor.  The only privileged calls
are the exact systemd start/stop/restart commands admitted by the bootstrap receipt.

The semantic and PostgreSQL operations remain in the existing cluster/Unicode/Highway
controllers; this module supplies the runner-owned physical providers they already
accept.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import pwd
import subprocess
import sys
import tempfile
from typing import Any, Sequence


REPOSITORY = Path(__file__).resolve().parents[2]
RUNNER_USER = "laplace-runner"
RESULT_SCHEMA = "laplace.product-activation-result/v1"


def load_module(name: str, path: Path) -> Any:
    specification = importlib.util.spec_from_file_location(name, path)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(specification)
    sys.modules[name] = module
    specification.loader.exec_module(module)
    return module


activation = load_module(
    "laplace_runner_product_activation_contract",
    REPOSITORY / "tools/delivery/product_activation.py",
)
clusterctl = load_module(
    "laplace_runner_clusterctl",
    REPOSITORY / "tools/postgresql/clusterctl.py",
)
unicodectl = load_module(
    "laplace_runner_unicodectl",
    REPOSITORY / "tools/postgresql/unicodectl.py",
)
highwayctl = load_module(
    "laplace_runner_highwayctl",
    REPOSITORY / "tools/postgresql/highwayctl.py",
)


class RunnerActivationError(RuntimeError):
    pass


def require_runner() -> None:
    expected = pwd.getpwnam(RUNNER_USER)
    if os.geteuid() != expected.pw_uid:
        actual = pwd.getpwuid(os.geteuid()).pw_name
        raise RunnerActivationError(
            f"persistent product activation requires {RUNNER_USER}, not {actual}"
        )


def canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def document_identity(value: dict[str, Any], field: str) -> str:
    payload = dict(value)
    payload.pop(field, None)
    return clusterctl.sha256_bytes(canonical_bytes(payload))


def write_json(path: Path, value: dict[str, Any]) -> None:
    content = json.dumps(value, indent=2, sort_keys=True) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o750)
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(0o640)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def load_json(path: Path) -> dict[str, Any]:
    return activation.load_json(path)


def source_root_for_product_receipt(
    product_receipt: dict[str, Any], manifest: dict[str, Any]
) -> Path:
    physical_release = Path(product_receipt.get("physical_root", ""))
    logical_release = Path(manifest["root"])
    physical_text = str(physical_release)
    logical_text = str(logical_release)
    if not physical_text.endswith(logical_text):
        raise RunnerActivationError(
            "product receipt physical root does not end in its logical release root"
        )
    prefix = physical_text[: -len(logical_text)]
    source = Path(prefix or "/")
    if not source.is_absolute():
        raise RunnerActivationError("product package source root is not absolute")
    return source


def validate_package_installation(
    result: dict[str, Any], package: dict[str, Any], source_root: Path
) -> None:
    if (
        result.get("schema") != clusterctl.INSTALLATION_SCHEMA
        or result.get("phase") != "installed"
        or result.get("package_id") != package["package_id"]
        or result.get("package_manifest_sha256")
        != clusterctl.sha256_bytes(clusterctl.canonical_bytes(package))
        or result.get("package_root") != package["root"]
        or result.get("installation_root") != "/"
        or result.get("installed_release") != package["root"]
        or result.get("source_physical_root") != str(source_root.resolve())
        or result.get("source_package_verified") is not True
        or result.get("installed_package_verified") is not True
        or result.get("overwrite_performed") is not False
        or result.get("installation_receipt_sha256")
        != clusterctl.state_observation_identity(result)
    ):
        raise RunnerActivationError("package installation receipt is incomplete")


def validate_cluster_result(result: dict[str, Any], package_id: str) -> Path:
    if (
        result.get("schema") != clusterctl.ACTIVATION_SCHEMA
        or result.get("phase") != "activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("boot_enabled") is not True
        or result.get("active_target") != f"releases/{package_id}"
        or result.get("runtime_target") != f"../releases/{package_id}"
        or result.get("activation_receipt_sha256")
        != clusterctl.state_observation_identity(result)
    ):
        raise RunnerActivationError("cluster activation receipt is incomplete")
    plan = Path(result.get("cluster_plan_path", ""))
    if not plan.is_file() or plan.is_symlink():
        raise RunnerActivationError("cluster activation plan receipt is absent")
    return plan


def validate_unicode_result(result: dict[str, Any], package_id: str) -> None:
    if (
        result.get("schema") != unicodectl.RECEIPT_SCHEMA
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_public_readback_proven") is not True
        or result.get("reverse_inversion_proven") is not True
        or result.get("receipt_sha256")
        != unicodectl.document_identity(result, "receipt_sha256")
    ):
        raise RunnerActivationError("Unicode product receipt is incomplete")


def validate_highway_result(result: dict[str, Any], package_id: str) -> None:
    if (
        result.get("schema") != highwayctl.RECEIPT_SCHEMA
        or result.get("phase") != "product-activated"
        or result.get("package_id") != package_id
        or result.get("restart_proven") is not True
        or result.get("cold_application_readback_proven") is not True
        or result.get("receipt_sha256")
        != unicodectl.document_identity(result, "receipt_sha256")
    ):
        raise RunnerActivationError("Highway product receipt is incomplete")


def runner_sql(
    plan: dict[str, Any],
    cluster_contract: dict[str, Any],
    sql: str,
    label: str,
    os_user: str,
    database_role: str,
    timeout: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    require_runner()
    if os_user != RUNNER_USER:
        raise RunnerActivationError(
            f"{label} requested OS identity {os_user}; product owner is {RUNNER_USER}"
        )
    instance = plan["instance"]
    psql = f"{plan['package_root']}/pgsql-{plan['postgresql_major']}/bin/psql"
    command = [
        psql,
        "--host",
        instance["socket_directory"],
        "--port",
        str(instance["port"]),
        "--username",
        database_role,
        "--dbname",
        instance["database"],
        "--no-psqlrc",
        "--set",
        "ON_ERROR_STOP=1",
        "--quiet",
        "--tuples-only",
        "--no-align",
    ]
    completed = subprocess.run(
        command,
        check=False,
        cwd="/",
        env=clusterctl.activation_environment(),
        input=sql,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    receipt = clusterctl.command_execution_receipt(label, command, completed)
    receipt["stdin_sha256"] = unicodectl.sha256_bytes(sql.encode("utf-8"))
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RunnerActivationError(
            f"{label} failed with exit {completed.returncode}: {detail[-1000:]}"
        )
    return unicodectl.parse_single_json(completed.stdout, label), receipt


def runner_work_directories(
    paths: Sequence[Path], cluster_contract: dict[str, Any], root: Path
) -> None:
    require_runner()
    for path in paths:
        if path.exists() or path.is_symlink():
            raise RunnerActivationError(f"Unicode activation path already exists: {path}")
    for path in paths:
        path.mkdir(mode=0o700)
        if path.stat().st_uid != os.geteuid() or path.stat().st_gid != os.getegid():
            raise RunnerActivationError(
                f"Unicode activation path is not owned by {RUNNER_USER}: {path}"
            )


def unicode_failure(
    error: BaseException,
    cluster_contract: dict[str, Any],
    activation_contract: dict[str, Any],
    source_contract: dict[str, Any],
    postgresql_contract: dict[str, Any],
    package: dict[str, Any],
    plan: dict[str, Any],
    cluster_result: dict[str, Any],
    source_root: Path,
) -> None:
    directory = (
        Path(cluster_contract["instance"]["receipt_directory"])
        / "unicode"
        / "failures"
    )
    unicodectl.write_failure(
        directory,
        error,
        {
            "activation_contract_sha256": unicodectl.sha256_bytes(
                unicodectl.canonical_bytes(activation_contract)
            ),
            "cluster_contract_sha256": unicodectl.sha256_bytes(
                unicodectl.canonical_bytes(cluster_contract)
            ),
            "source_contract_sha256": unicodectl.sha256_bytes(
                unicodectl.canonical_bytes(source_contract)
            ),
            "unicode_postgresql_contract_sha256": unicodectl.sha256_bytes(
                unicodectl.canonical_bytes(postgresql_contract)
            ),
            "package_id": package["package_id"],
            "cluster_plan_sha256": plan["plan_sha256"],
            "cluster_activation_receipt_sha256": cluster_result[
                "activation_receipt_sha256"
            ],
            "source_root": str(source_root),
            "semantic_activation_state": "must-be-reinspected-before-retry",
        },
    )


def execute(
    product_receipt_path: Path,
    resource_path: Path,
    repository_commit: str,
    output: Path,
) -> dict[str, Any]:
    require_runner()
    product_receipt = load_json(product_receipt_path)
    manifest_path = Path(product_receipt.get("manifest", ""))
    package = load_json(manifest_path)
    source_root = source_root_for_product_receipt(product_receipt, package)
    if product_receipt.get("package_id") != package.get("package_id"):
        raise RunnerActivationError("product receipt and package identity differ")
    if product_receipt.get("activation_eligible") is not True:
        raise RunnerActivationError("product package is not activation eligible")

    cluster_contract = load_json(REPOSITORY / "contracts/postgresql-cluster.json")
    clusterctl.validate_contract(cluster_contract)
    package_id = package["package_id"]
    receipt_root = Path(cluster_contract["instance"]["receipt_directory"])
    cluster_evidence = receipt_root / "cluster-activation" / package_id
    installation_path = cluster_evidence / "package-installation.json"
    cluster_result_path = cluster_evidence / "activation-result.json"

    installation = clusterctl.install_package(
        package,
        cluster_contract,
        source_root,
        Path("/"),
        True,
    )
    validate_package_installation(installation, package, source_root)
    write_json(installation_path, installation)

    if cluster_result_path.exists():
        cluster_result = load_json(cluster_result_path)
    else:
        cluster_result = clusterctl.activate_product(
            REPOSITORY / "contracts/postgresql-cluster.json",
            manifest_path,
            resource_path,
            cluster_evidence,
            False,
        )
        write_json(cluster_result_path, cluster_result)
    plan_path = validate_cluster_result(cluster_result, package_id)
    plan = load_json(plan_path)

    unicode_activation_contract = load_json(
        REPOSITORY / "contracts/unicode-product-activation.json"
    )
    unicode_source_contract = load_json(REPOSITORY / "contracts/unicode-source.json")
    unicode_postgresql_contract = load_json(
        REPOSITORY / "contracts/unicode-postgresql.json"
    )
    unicode_result_path = receipt_root / "unicode-product-activation.json"
    unicodectl.create_work_directories = runner_work_directories
    try:
        unicode_result = unicodectl.execute_unicode_activation(
            unicode_activation_contract,
            cluster_contract,
            unicode_source_contract,
            unicode_postgresql_contract,
            package,
            plan,
            cluster_result,
            Path("/vault/Data/UCD/Public/UCD/latest"),
            Path("/"),
            False,
            sql_runner=runner_sql,
            loaded_observer=clusterctl.observe_loaded_live,
            command_runner=clusterctl.execute_activation_command,
            readiness_runner=clusterctl.await_postgresql_ready,
        )
    except BaseException as error:
        unicode_failure(
            error,
            cluster_contract,
            unicode_activation_contract,
            unicode_source_contract,
            unicode_postgresql_contract,
            package,
            plan,
            cluster_result,
            Path("/vault/Data/UCD/Public/UCD/latest"),
        )
        raise
    validate_unicode_result(unicode_result, package_id)
    write_json(unicode_result_path, unicode_result)

    highway_contract = load_json(REPOSITORY / "contracts/highway-product-activation.json")
    registry_contract = load_json(REPOSITORY / "contracts/highway.json")
    previous_registry_contract = load_json(
        REPOSITORY / "contracts/history/highway-v1.json"
    )
    highway_result = highwayctl.execute_highway_activation(
        highway_contract,
        cluster_contract,
        unicode_activation_contract,
        registry_contract,
        previous_registry_contract,
        package,
        plan,
        cluster_result,
        unicode_result,
        Path("/"),
        False,
        sql_runner=runner_sql,
        loaded_observer=clusterctl.observe_loaded_live,
        command_runner=clusterctl.execute_activation_command,
        readiness_runner=clusterctl.await_postgresql_ready,
    )
    validate_highway_result(highway_result, package_id)
    highway_result_path = receipt_root / "highway-product-activation.json"
    write_json(highway_result_path, highway_result)

    result = {
        "schema": RESULT_SCHEMA,
        "phase": "product-unicode-and-highway-activated",
        "execution_owner": RUNNER_USER,
        "repository_commit": repository_commit,
        "package_id": package_id,
        "package_installation_receipt_sha256": installation[
            "installation_receipt_sha256"
        ],
        "cluster_activation_receipt_sha256": cluster_result[
            "activation_receipt_sha256"
        ],
        "unicode_activation_receipt_sha256": unicode_result["receipt_sha256"],
        "highway_activation_receipt_sha256": highway_result["receipt_sha256"],
        "cluster_result": str(cluster_result_path),
        "unicode_result": str(unicode_result_path),
        "highway_result": str(highway_result_path),
        "root_product_executor": False,
    }
    result["result_sha256"] = document_identity(result, "result_sha256")
    write_json(output, result)
    return result


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product-receipt", required=True)
    parser.add_argument("--resource-observation", required=True)
    parser.add_argument("--repository-commit", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    result = execute(
        Path(args.product_receipt),
        Path(args.resource_observation),
        args.repository_commit,
        Path(args.output),
    )
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"product_activation_runner: {error}", file=sys.stderr)
        raise SystemExit(1) from error
