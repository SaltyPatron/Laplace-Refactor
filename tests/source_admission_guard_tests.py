#!/usr/bin/env python3
"""Regression coverage for the installed source-admission estate/runtime boundary."""
from __future__ import annotations

import importlib.util
import contextlib
import io
import hashlib
import os
import shutil
import subprocess
import json
from pathlib import Path
import tempfile
import unittest
import sys
from unittest import mock

if len(sys.argv) != 2:
    raise RuntimeError("expected path to admit_source_guard.py")
GUARD_PATH = Path(sys.argv[1])
sys.argv[:] = sys.argv[:1]


def load_guard(path: Path):
    spec = importlib.util.spec_from_file_location("laplace_admission_guard", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load guard module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SourceAdmissionEstateBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.guard = load_guard(GUARD_PATH)

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.estate = self.root / "estate"
        self.source = self.estate / "ISO639"
        self.unicode = self.estate / "UCD/Public/UCD/latest"
        self.outside = self.root / "outside"
        self.source.mkdir(parents=True)
        self.unicode.mkdir(parents=True)
        self.outside.mkdir()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_accepts_and_canonicalizes_directories_inside_estate(self) -> None:
        argv = ["laplace-admit-source", "iso-639-3-20260415", str(self.source)]
        guarded = self.guard.guarded_arguments(argv, self.estate)
        self.assertEqual(guarded[2], str(self.source.resolve()))
        self.assertEqual(
            guarded[-2:], ["--unicode-root", str(self.unicode.resolve())]
        )

    def test_rejects_source_root_outside_estate(self) -> None:
        argv = ["laplace-admit-source", "iso-639-3-20260415", str(self.outside)]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)

    def test_rejects_explicit_unicode_root_outside_estate(self) -> None:
        argv = [
            "laplace-admit-source",
            "iso-639-3-20260415",
            str(self.source),
            "--unicode-root",
            str(self.outside),
        ]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)

    def test_rejects_symlink_escape(self) -> None:
        escape = self.estate / "escape"
        try:
            escape.symlink_to(self.outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlink unavailable: {error}")
        argv = ["laplace-admit-source", "iso-639-3-20260415", str(escape)]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)

    def test_rejects_relative_source_path(self) -> None:
        argv = ["laplace-admit-source", "iso-639-3-20260415", "ISO639"]
        with self.assertRaises(self.guard.AdmissionGuardError):
            self.guard.guarded_arguments(argv, self.estate)

    def test_verified_git_uses_active_unicode_without_raw_source_directory(self) -> None:
        self.unicode.rmdir()
        argv = ["laplace-admit-source", "verified-git-code", str(self.source)]
        self.assertEqual(self.guard.guarded_arguments(argv, self.estate), argv)

    def test_tabular_profile_still_requires_its_raw_unicode_compiler_input(self) -> None:
        self.unicode.rmdir()
        argv = ["laplace-admit-source", "iso-639-3-20260415", str(self.source)]
        with self.assertRaisesRegex(self.guard.AdmissionGuardError, "unicode_root is unavailable"):
            self.guard.guarded_arguments(argv, self.estate)

    def test_verified_git_still_rejects_source_outside_the_estate(self) -> None:
        self.unicode.rmdir()
        argv = ["laplace-admit-source", "verified-git-code", str(self.outside)]
        with self.assertRaisesRegex(self.guard.AdmissionGuardError, "source_root is outside"):
            self.guard.guarded_arguments(argv, self.estate)


class SourceAdmissionUnpublishedPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.guard = load_guard(GUARD_PATH)

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.receipts = self.root / "receipts"
        self.releases = self.root / "releases"
        self.releases.mkdir()
        (self.receipts / "cluster-activation").mkdir(parents=True)
        (self.receipts / "unicode").mkdir()
        self.tool_root = self.root / "branch-release"
        self.tool_root.mkdir()
        self.active = self.root / "missing-current"
        self.live = {
            "activation_epoch_id": "11" * 16,
            "activation_epoch_fingerprint": "22" * 32,
            "geometry_epoch": "33" * 32,
            "perfcache_epoch": "22" * 32,
            "numeric_epoch": "44" * 32,
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def add_generation(
        self,
        package_byte: str,
        request_byte: str,
        *,
        geometry_epoch: str | None = None,
        numeric_epoch: str | None = None,
    ) -> Path:
        package = package_byte * 64
        request = request_byte * 64
        generation = self.receipts / "cluster-activation" / package
        generation.mkdir()
        (generation / "unicode-product-activation.json").write_text(
            json.dumps(
                {
                    "schema": "laplace.unicode-product-activation-receipt/v1",
                    "phase": "product-activated",
                    "package_id": package,
                    "request_fingerprint": request,
                    "activation_epoch_id": self.live["activation_epoch_id"],
                    "activation_epoch_fingerprint": self.live[
                        "activation_epoch_fingerprint"
                    ],
                }
            ),
            encoding="utf-8",
        )
        identities = self.receipts / "unicode" / request
        identities.mkdir()
        (identities / "identities.json").write_text(
            json.dumps(
                {
                    "schema": "laplace.unicode-activation-identities/v1",
                    "request_fingerprint": request,
                    "geometry_epoch": geometry_epoch or self.live["geometry_epoch"],
                    "perfcache_epoch": "66" * 32,
                    "numeric_epoch": numeric_epoch or "77" * 32,
                }
            ),
            encoding="utf-8",
        )
        release = self.releases / package
        release.mkdir()
        return generation

    def argv(self) -> list[str]:
        return [
            "laplace-admit-source",
            "iso-639-3-20260415",
            "/vault/Data/ISO639",
            "--tool-root",
            str(self.tool_root),
            "--receipt-root",
            str(self.receipts),
        ]

    def test_binds_unique_live_activation_for_prepublication_package_proof(self) -> None:
        generation = self.add_generation("a", "b")
        values = self.guard.bind_unpublished_runtime(
            self.argv(), active_path=self.active, release_root=self.releases, live_state=self.live
        )
        self.assertEqual(
            values[-2:],
            ["--active", str((self.releases / generation.name).resolve())],
        )

    def test_rejects_ambiguous_live_activation_receipts(self) -> None:
        self.add_generation("a", "b")
        self.add_generation("c", "d")
        with self.assertRaisesRegex(
            self.guard.AdmissionGuardError, "exactly one activation receipt"
        ):
            self.guard.bind_unpublished_runtime(
                self.argv(), active_path=self.active, release_root=self.releases, live_state=self.live
            )

    def test_ignores_nonmatching_activation_receipt(self) -> None:
        generation = self.add_generation("a", "b")
        self.add_generation("c", "d", geometry_epoch="55" * 32)
        values = self.guard.bind_unpublished_runtime(
            self.argv(), active_path=self.active, release_root=self.releases, live_state=self.live
        )
        self.assertEqual(
            values[-2:],
            ["--active", str((self.releases / generation.name).resolve())],
        )

    def test_explicit_active_selector_is_never_rewritten(self) -> None:
        explicit = self.root / ("e" * 64)
        explicit.mkdir()
        values = self.argv() + ["--active", str(explicit)]
        self.assertEqual(
            self.guard.bind_unpublished_runtime(
                values, active_path=self.active, live_state=self.live
            ),
            values,
        )

    def test_ordinary_use_without_tool_root_stays_fail_closed(self) -> None:
        values = [
            "laplace-admit-source",
            "iso-639-3-20260415",
            "/vault/Data/ISO639",
        ]
        self.assertEqual(
            self.guard.bind_unpublished_runtime(
                values, active_path=self.active, live_state=self.live
            ),
            values,
        )

    def test_broken_active_selector_is_not_bypassed(self) -> None:
        broken = self.root / "current"
        try:
            broken.symlink_to(self.root / "missing-release", target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlink unavailable: {error}")
        values = self.argv()
        self.assertEqual(
            self.guard.bind_unpublished_runtime(
                values, active_path=broken, live_state=self.live
            ),
            values,
        )


class SourceAdmissionInstalledImportTests(unittest.TestCase):
    def test_writable_installed_import_preserves_exact_package_and_missing_policy_creates_caches(self):
        # Execute real packaged modules without main(), credentials, native stubs
        # or any database operation. The filesystem remains writable so removing
        # the entry-point policy demonstrates the original bytecode defect.
        with tempfile.TemporaryDirectory(prefix="source-import-package-") as temporary:
            workspace = Path(temporary)
            tools = Path(__file__).resolve().parents[1] / "tools"
            entrypoints = {
                "laplace-admit-source-core": tools / "admit_source.py",
                "laplace-admit-source": tools / "admit_source_guard.py",
            }
            policy = "sys.dont_write_bytecode = True"
            script = r"""
import json, runpy, sys
from pathlib import Path
directory = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(directory))
# The packaged grammar tool also imports from its own executable directory.
sys.path.insert(0, str(directory / "sources"))
runpy.run_path(str(directory / sys.argv[2]), run_name="installed_import_contract")
from sources import git_profile, verified_git, qualify_grammar, runtime_state
from dependencies import git_checkout
modules = [git_profile, verified_git, qualify_grammar, runtime_state, git_checkout]
paths = [str(Path(item.__file__).resolve().relative_to(directory)) for item in modules]
print(json.dumps({"modules": paths, "dont_write_bytecode": sys.dont_write_bytecode}))
"""
            expected_modules = ["sources/git_profile.py", "sources/verified_git.py",
                                "sources/qualify_grammar.py", "sources/runtime_state.py",
                                "dependencies/git_checkout.py"]
            environment = dict(os.environ)
            for key in ("PYTHONDONTWRITEBYTECODE", "PYTHONPYCACHEPREFIX", "PYTHONPATH"):
                environment.pop(key, None)

            def inventory(root):
                return {str(item.relative_to(root)):
                        ("directory" if item.is_dir() else hashlib.sha256(item.read_bytes()).hexdigest())
                        for item in root.rglob("*")}

            for entrypoint, source in entrypoints.items():
                core_source = source.read_text(encoding="utf-8")
                self.assertEqual(core_source.count(policy), 1)
                for mode in ("entry-point-policy", "old-core-without-envelope", "old-core-with-envelope"):
                    has_policy = mode == "entry-point-policy"
                    environmental_policy = mode == "old-core-with-envelope"
                    expected_read_only = has_policy or environmental_policy
                    with self.subTest(entrypoint=entrypoint, mode=mode):
                        package = workspace / entrypoint / mode / "bin"
                        package.mkdir(parents=True, mode=0o700)
                        core = package / entrypoint
                        core.write_text(core_source if has_policy else
                                        core_source.replace(policy, "", 1),
                                        encoding="utf-8")
                        for relative in expected_modules:
                            target = package / relative
                            target.parent.mkdir(mode=0o700, exist_ok=True)
                            shutil.copyfile(tools / relative, target)
                            target.chmod(0o600)
                        before = inventory(package)
                        child_environment = dict(environment)
                        if environmental_policy:
                            child_environment["PYTHONDONTWRITEBYTECODE"] = "1"
                        # -I deliberately ignores environment for the permanent
                        # entry-point proof. The already-built old core is tested
                        # under the real environment-only execution envelope.
                        arguments = [sys.executable, *(["-I"] if has_policy else []),
                                     "-c", script, str(package), entrypoint]
                        for repeat in range(2):
                            result = subprocess.run(arguments,
                                                    env=child_environment, capture_output=True, text=True,
                                                    check=False, timeout=15)
                            self.assertEqual(result.returncode, 0, result.stderr)
                            observed = json.loads(result.stdout)
                            self.assertEqual(observed["modules"], expected_modules)
                            self.assertEqual(observed["dont_write_bytecode"], expected_read_only)
                        after = inventory(package)
                        caches = list(package.rglob("*.pyc"))
                        if expected_read_only:
                            self.assertEqual(before, after)
                            self.assertEqual(caches, [])
                        else:
                            self.assertNotEqual(before, after)
                            self.assertTrue(any(item.parent.name == "__pycache__" for item in caches))
                            self.assertTrue(any(item.name.startswith("git_checkout.") for item in caches))
                            self.assertTrue(any(item.name.startswith("git_profile.") for item in caches))
                            self.assertTrue(any(item.name.startswith("runtime_state.") for item in caches))
                            # The counterexample adds bytecode; it does not corrupt
                            # the original package modules or stand in for admission.
                            self.assertEqual(before, {name: after[name] for name in before})


class SourceAdmissionLiveContextTests(unittest.TestCase):
    """The real main boundary must validate live geometry before any compiler/write."""
    def test_geometry_mismatch_refuses_before_compile_or_admission(self):
        tools = Path(__file__).resolve().parents[1] / "tools"
        core = load_guard(tools / "admit_source.py")
        live = {"activation_epoch_id": "11" * 16,
                "activation_epoch_fingerprint": "22" * 32,
                "geometry_epoch": "33" * 32, "perfcache_epoch": "22" * 32,
                "numeric_epoch": "44" * 32}
        with (mock.patch.object(sys, "path", [str(tools), *sys.path]),
              mock.patch.object(core, "selected_package", return_value=("a" * 64, tools)),
              mock.patch.object(core, "selected_tool_release", return_value=tools),
              mock.patch.object(core, "load_activation_state",
                                return_value={"geometry_epoch": "55" * 32}),
              mock.patch.object(core, "psql_command", return_value=["psql"]),
              mock.patch.object(core, "run_scalar", return_value=json.dumps(live)) as read,
              mock.patch.object(core, "compile_profile") as compile_profile,
              mock.patch.object(core, "render_sql") as render,
              mock.patch.object(core, "run_sql") as write,
              contextlib.redirect_stderr(io.StringIO()) as errors):
            result = core.main(["iso-639-3-20260415", str(tools)])
        self.assertEqual(2, result)
        self.assertIn("geometry epoch differs", errors.getvalue())
        read.assert_called_once()
        compile_profile.assert_not_called()
        render.assert_not_called()
        write.assert_not_called()

    def test_current_geometry_and_live_successor_epochs_reach_same_render_owner(self):
        tools = Path(__file__).resolve().parents[1] / "tools"
        core = load_guard(tools / "admit_source.py")
        live = {"activation_epoch_id": "11" * 16,
                "activation_epoch_fingerprint": "22" * 32,
                "geometry_epoch": "33" * 32, "perfcache_epoch": "22" * 32,
                "numeric_epoch": "44" * 32}
        identities = {"geometry_epoch": live["geometry_epoch"],
                      "perfcache_epoch": "66" * 32, "numeric_epoch": "77" * 32}
        compiled = {"fixture": "boundary dispatch only"}
        with (mock.patch.object(sys, "path", [str(tools), *sys.path]),
              mock.patch.object(core, "selected_package", return_value=("a" * 64, tools)),
              mock.patch.object(core, "selected_tool_release", return_value=tools),
              mock.patch.object(core, "load_activation_state", return_value=identities),
              mock.patch.object(core, "psql_command", return_value=["psql"]),
              mock.patch.object(core, "run_scalar", return_value=json.dumps(live)) as read,
              mock.patch.object(core, "compile_profile", return_value=compiled) as compile_profile,
              mock.patch.object(core, "render_sql", return_value="fixture render only") as render,
              mock.patch.object(core, "run_sql") as write,
              contextlib.redirect_stdout(io.StringIO()) as output):
            result = core.main(["iso-639-3-20260415", str(tools), "--render-sql"])
            from sources.runtime_state import LIVE_RUNTIME_SQL
            self.assertEqual(LIVE_RUNTIME_SQL, read.call_args.args[1])
        self.assertEqual(0, result)
        self.assertEqual("fixture render only", output.getvalue())
        self.assertEqual(live["geometry_epoch"], compile_profile.call_args.args[-1])
        self.assertEqual((compiled, identities, tools, live["perfcache_epoch"],
                          live["numeric_epoch"], None), render.call_args.args)
        write.assert_not_called()


if __name__ == "__main__":
    unittest.main()
