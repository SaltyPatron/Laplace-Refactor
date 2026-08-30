#!/usr/bin/env python3
from pathlib import Path

package_path = Path("tools/product/build-package.py")
package = package_path.read_text(encoding="utf-8")
old_import = "import argparse\nimport hashlib\n"
new_import = "import argparse\nimport fcntl\nimport hashlib\n"
if old_import not in package:
    raise SystemExit("build-package import anchor differs")
package = package.replace(old_import, new_import, 1)

start = package.index("\ndef select_or_build_product(")
end = package.index("\ndef write_result", start)
replacement = r'''
def product_plan_lock_path(plan: Mapping[str, Any]) -> Path:
    plan_id = require_string(plan.get("plan_id"), "product plan id")
    if HEX_256.fullmatch(plan_id) is None:
        raise ProductPackageError("product plan id is not a lowercase SHA-256 identity")
    build_directory = Path(
        require_string(plan.get("build_directory"), "product build directory")
    )
    stage_directory = Path(
        require_string(plan.get("stage_directory"), "product stage directory")
    )
    if build_directory.name != plan_id or stage_directory.name != plan_id:
        raise ProductPackageError("product build and stage destinations are not plan-addressed")
    if build_directory.parent.name != "build" or stage_directory.parent.name != "stage":
        raise ProductPackageError("product build and stage destinations escaped their named roots")
    if build_directory.parent.parent != stage_directory.parent.parent:
        raise ProductPackageError("product build and stage destinations do not share a product root")
    lock_root = build_directory.parent.parent / "locks"
    lock_root.mkdir(parents=True, exist_ok=True, mode=0o700)
    if not lock_root.is_dir() or lock_root.is_symlink():
        raise ProductPackageError("product plan lock root is not a physical directory")
    os.chmod(lock_root, 0o700)
    return lock_root / f"{plan_id}.lock"


def remove_incomplete_plan_workspace(plan: Mapping[str, Any]) -> None:
    product_plan_lock_path(plan)
    for field in ("build_directory", "stage_directory"):
        path = Path(require_string(plan.get(field), field))
        if not path.exists() and not path.is_symlink():
            continue
        if path.is_symlink() or not path.is_dir():
            raise ProductPackageError(
                f"incomplete product plan workspace is not a physical directory: {path}"
            )
        shutil.rmtree(path)


def select_or_build_product(
    contract: dict[str, Any], repository: Path, plan: dict[str, Any]
) -> dict[str, Any]:
    build_directory = Path(plan["build_directory"])
    stage_directory = Path(plan["stage_directory"])
    receipt_path = build_directory / "package-receipt.json"
    lock_path = product_plan_lock_path(plan)
    descriptor = os.open(
        lock_path,
        os.O_CREAT | os.O_RDWR | os.O_CLOEXEC | os.O_NOFOLLOW,
        0o600,
    )
    built_new = False
    with os.fdopen(descriptor, "a+b") as lock_stream:
        fcntl.flock(lock_stream.fileno(), fcntl.LOCK_EX)
        if not receipt_path.exists():
            if (
                build_directory.exists()
                or build_directory.is_symlink()
                or stage_directory.exists()
                or stage_directory.is_symlink()
            ):
                remove_incomplete_plan_workspace(plan)
            execute_plan(contract, repository, plan)
            built_new = True
        if not receipt_path.is_file() or receipt_path.is_symlink():
            raise ProductPackageError("product package receipt is absent or not physical")
        receipt = load_json(receipt_path)
        if (
            receipt.get("schema") != RECEIPT_SCHEMA
            or receipt.get("plan_sha256") != plan["plan_sha256"]
            or receipt.get("activation_eligible") is not True
            or receipt.get("build_input_closure_complete") is not True
            or receipt.get("product_activated") is not False
        ):
            raise ProductPackageError(
                "selected product package receipt differs from its exact plan"
            )
        manifest_path = Path(
            require_string(receipt.get("manifest"), "product receipt manifest")
        )
        expected_manifest_path = build_directory / "package-manifest.json"
        if (
            manifest_path != expected_manifest_path
            or not manifest_path.is_file()
            or manifest_path.is_symlink()
        ):
            raise ProductPackageError(
                "selected product manifest is absent or not paired with its receipt"
            )
        if sha256_file(manifest_path) != receipt.get("manifest_sha256"):
            raise ProductPackageError(
                "selected product manifest bytes differ from its receipt"
            )
        manifest = load_json(manifest_path)
        package_id = require_string(
            receipt.get("package_id"), "product receipt package id"
        )
        if HEX_256.fullmatch(package_id) is None or manifest.get("package_id") != package_id:
            raise ProductPackageError("selected product package identity differs")
        expected_physical_root = stage_directory / "root" / str(
            manifest.get("root", "")
        ).lstrip("/")
        if (
            receipt.get("physical_root") != str(expected_physical_root)
            or not expected_physical_root.is_dir()
        ):
            raise ProductPackageError("selected product physical root differs from its plan")
    return {
        "schema": SELECTION_SCHEMA,
        "plan_id": plan["plan_id"],
        "plan_sha256": plan["plan_sha256"],
        "build_directory": plan["build_directory"],
        "stage_directory": plan["stage_directory"],
        "product_receipt": str(receipt_path),
        "package_id": package_id,
        "built_new": built_new,
    }
'''
package = package[:start] + replacement + package[end:]
package_path.write_text(package, encoding="utf-8")

proof_path = Path("tools/product/prove-package.py")
proof = proof_path.read_text(encoding="utf-8")
start = proof.index("\ndef run(command: Sequence[str], label: str)")
end = proof.index("\ndef repository_identity", start)
replacement = r'''
def command_failure_detail(
    returncode: int, stdout: str, stderr: str, *, limit: int = 16000
) -> str:
    parts: list[str] = []
    for label, value in (("stderr", stderr.strip()), ("stdout", stdout.strip())):
        if not value:
            continue
        if len(value) > limit:
            value = "...<truncated to final output>\n" + value[-limit:]
        parts.append(f"{label}:\n{value}")
    return "\n".join(parts) if parts else f"exit {returncode}"


def run(command: Sequence[str], label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        list(command),
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        detail = command_failure_detail(
            completed.returncode, completed.stdout, completed.stderr
        )
        raise PackageProductProofError(f"{label} failed: {detail}")
    return completed


def run_bytes(command: Sequence[str], label: str) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(
        list(command),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        detail = command_failure_detail(
            completed.returncode,
            completed.stdout.decode("utf-8", errors="replace"),
            completed.stderr.decode("utf-8", errors="replace"),
        )
        raise PackageProductProofError(f"{label} failed: {detail}")
    return completed
'''
proof = proof[:start] + replacement + proof[end:]
proof_path.write_text(proof, encoding="utf-8")

tests_path = Path("tests/product_package_tests.py")
tests = tests_path.read_text(encoding="utf-8")
old_import = "import unittest\nfrom pathlib import Path\n"
new_import = "import unittest\nfrom pathlib import Path\nfrom unittest import mock\n"
if old_import not in tests:
    raise SystemExit("product package test import anchor differs")
tests = tests.replace(old_import, new_import, 1)

old_fixture = '''        build = self.root / "build"\n        stage = self.root / "stage"\n        build.mkdir()\n'''
new_fixture = '''        plan_id = "c" * 64\n        build = self.root / "product/build" / plan_id\n        stage = self.root / "product/stage" / plan_id\n        build.mkdir(parents=True)\n'''
if old_fixture not in tests:
    raise SystemExit("exact product selection fixture anchor differs")
tests = tests.replace(old_fixture, new_fixture, 1)
if '            "plan_id": "c" * 64,\n' not in tests:
    raise SystemExit("product selection plan-id anchor differs")
tests = tests.replace('            "plan_id": "c" * 64,\n', '            "plan_id": plan_id,\n', 1)

marker = "    def test_selected_tool_bytes_are_reverified(self) -> None:\n"
if marker not in tests:
    raise SystemExit("product package test insertion anchor differs")
addition = r'''    def test_incomplete_same_plan_workspace_is_repaired_under_lock(self) -> None:
        plan_id = "c" * 64
        plan_sha = "d" * 64
        package_id = "e" * 64
        product_root = self.root / "product"
        build = product_root / "build" / plan_id
        stage = product_root / "stage" / plan_id
        build.mkdir(parents=True)
        stage.mkdir(parents=True)
        (build / "partial").write_text("incomplete\n", encoding="utf-8")
        plan = {
            "plan_id": plan_id,
            "plan_sha256": plan_sha,
            "build_directory": str(build),
            "stage_directory": str(stage),
        }

        def execute(_contract: dict, _repository: Path, supplied: dict) -> dict:
            self.assertEqual(supplied, plan)
            self.assertFalse(build.exists())
            self.assertFalse(stage.exists())
            build.mkdir(parents=True)
            release = stage / "root/opt/laplace/releases" / package_id
            release.mkdir(parents=True)
            manifest = {
                "package_id": package_id,
                "root": f"/opt/laplace/releases/{package_id}",
            }
            manifest_path = build / "package-manifest.json"
            manifest_path.write_bytes(PACKAGE.canonical_bytes(manifest))
            receipt = {
                "schema": PACKAGE.RECEIPT_SCHEMA,
                "plan_sha256": plan_sha,
                "package_id": package_id,
                "manifest": str(manifest_path),
                "manifest_sha256": PACKAGE.sha256_file(manifest_path),
                "physical_root": str(release),
                "activation_eligible": True,
                "build_input_closure_complete": True,
                "product_activated": False,
            }
            (build / "package-receipt.json").write_bytes(
                PACKAGE.canonical_bytes(receipt)
            )
            return receipt

        with mock.patch.object(PACKAGE, "execute_plan", side_effect=execute) as execute_mock:
            selected = PACKAGE.select_or_build_product(self.contract, REPOSITORY, plan)
        execute_mock.assert_called_once()
        self.assertTrue(selected["built_new"])
        self.assertEqual(selected["package_id"], package_id)
        self.assertTrue((product_root / "locks" / f"{plan_id}.lock").is_file())

    def test_product_plan_lock_rejects_cross_root_cleanup_authority(self) -> None:
        plan_id = "f" * 64
        plan = {
            "plan_id": plan_id,
            "build_directory": str(self.root / "product-a/build" / plan_id),
            "stage_directory": str(self.root / "product-b/stage" / plan_id),
        }
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "share a product root"):
            PACKAGE.product_plan_lock_path(plan)

    def test_product_plan_lock_rejects_wrong_named_roots(self) -> None:
        plan_id = "f" * 64
        plan = {
            "plan_id": plan_id,
            "build_directory": str(self.root / "product/work" / plan_id),
            "stage_directory": str(self.root / "product/stage" / plan_id),
        }
        with self.assertRaisesRegex(PACKAGE.ProductPackageError, "named roots"):
            PACKAGE.product_plan_lock_path(plan)

'''
tests = tests.replace(marker, addition + marker, 1)
tests_path.write_text(tests, encoding="utf-8")

proof_tests_path = Path("tests/package_product_proof_tests.py")
proof_tests = proof_tests_path.read_text(encoding="utf-8")
marker = "    def test_binding_canonicalization_is_stable(self) -> None:\n"
if marker not in proof_tests:
    raise SystemExit("package proof test insertion anchor differs")
addition = r'''    def test_command_failure_retains_inner_and_outer_output(self) -> None:
        completed = mock.Mock(
            returncode=1,
            stdout="inner sandbox build failure\n",
            stderr="outer package wrapper failure\n",
        )
        with mock.patch.object(proof.subprocess, "run", return_value=completed):
            with self.assertRaises(proof.PackageProductProofError) as caught:
                proof.run(["/usr/bin/false"], "product package composition")
        message = str(caught.exception)
        self.assertIn("outer package wrapper failure", message)
        self.assertIn("inner sandbox build failure", message)

'''
proof_tests = proof_tests.replace(marker, addition + marker, 1)
proof_tests_path.write_text(proof_tests, encoding="utf-8")
