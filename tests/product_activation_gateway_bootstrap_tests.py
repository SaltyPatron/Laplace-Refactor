#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
INSTALLER_PATH = REPOSITORY / "tools/delivery/install_product_activation_gateway.py"
INSTALLER_SPEC = importlib.util.spec_from_file_location(
    "laplace_product_activation_gateway_bootstrap_tests", INSTALLER_PATH
)
if INSTALLER_SPEC is None or INSTALLER_SPEC.loader is None:
    raise RuntimeError("cannot load product activation gateway installer")
installer = importlib.util.module_from_spec(INSTALLER_SPEC)
sys.modules[INSTALLER_SPEC.name] = installer
INSTALLER_SPEC.loader.exec_module(installer)


class ProductActivationGatewayBootstrapTests(unittest.TestCase):
    def git(self, repository: Path, *arguments: str) -> str:
        completed = subprocess.run(
            ["git", "-C", str(repository), *arguments],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return completed.stdout.strip()

    def repository_fixture(self, root: Path) -> tuple[Path, str]:
        repository = root / "repository"
        repository.mkdir()
        self.git(repository, "init", "-q")
        self.git(repository, "config", "user.email", "laplace-tests@example.invalid")
        self.git(repository, "config", "user.name", "Laplace Tests")
        for relative in sorted(set(installer.SOURCE_MAP.values())):
            path = repository / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture:{relative}\n", encoding="utf-8")
        self.git(repository, "add", "--all")
        self.git(repository, "commit", "-qm", "gateway fixture")
        return repository, self.git(repository, "rev-parse", "HEAD")

    def test_exact_clean_commit_is_accepted_and_receipted(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-bootstrap-") as temporary:
            repository, commit = self.repository_fixture(Path(temporary))
            binding = installer.verify_repository_binding(repository, commit)
            self.assertEqual(binding["commit"], commit)
            self.assertEqual(binding["tree"], self.git(repository, "rev-parse", "HEAD^{tree}"))
            self.assertEqual(binding["trusted_paths"], sorted(set(installer.SOURCE_MAP.values())))

    def test_wrong_commit_is_a_deliberate_bootstrap_defect(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-bootstrap-wrong-") as temporary:
            repository, _commit = self.repository_fixture(Path(temporary))
            with self.assertRaisesRegex(installer.InstallError, "expected repository commit"):
                installer.verify_repository_binding(repository, "0" * 40)

    def test_dirty_trusted_source_is_a_deliberate_bootstrap_defect(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-bootstrap-dirty-") as temporary:
            repository, commit = self.repository_fixture(Path(temporary))
            trusted = repository / next(iter(installer.SOURCE_MAP.values()))
            trusted.write_text("mutated after review\n", encoding="utf-8")
            with self.assertRaisesRegex(installer.InstallError, "trusted gateway source checkout is not clean"):
                installer.verify_repository_binding(repository, commit)

    def test_untracked_replacement_of_trusted_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="laplace-gateway-bootstrap-untracked-") as temporary:
            repository, commit = self.repository_fixture(Path(temporary))
            trusted_relative = next(iter(installer.SOURCE_MAP.values()))
            self.git(repository, "rm", "-q", trusted_relative)
            trusted = repository / trusted_relative
            trusted.parent.mkdir(parents=True, exist_ok=True)
            trusted.write_text("untracked replacement\n", encoding="utf-8")
            with self.assertRaisesRegex(installer.InstallError, "trusted gateway source checkout is not clean"):
                installer.verify_repository_binding(repository, commit)


if __name__ == "__main__":
    unittest.main()
