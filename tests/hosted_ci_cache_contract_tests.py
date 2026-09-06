#!/usr/bin/env python3
"""Contract tests for hosted native compiler-object reuse.

The cache is a physical build acceleration only.  It must stay preset-isolated,
compiler-content-checked, bounded, and pinned to an exact action revision.  The
underlying CMake presets/tests remain authoritative; cache hits cannot bypass build
or acceptance steps.
"""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
CACHE_ACTION = "actions/cache@27d5ce7f107fe9357f9df03efb73ab90386fccae"


class HostedCiCacheContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_cache_action_is_exactly_pinned_and_preset_isolated(self) -> None:
        self.assertIn(CACHE_ACTION, self.workflow)
        self.assertNotIn("actions/cache@v", self.workflow)
        self.assertIn(
            'cache_dir="$RUNNER_TEMP/laplace-ccache/${{ matrix.preset }}"',
            self.workflow,
        )
        self.assertIn(
            "path: ${{ runner.temp }}/laplace-ccache/${{ matrix.preset }}",
            self.workflow,
        )
        self.assertIn(
            "laplace-ccache-${{ runner.os }}-${{ runner.arch }}-${{ matrix.preset }}-",
            self.workflow,
        )

    def test_cache_environment_is_initialized_after_runner_assignment(self) -> None:
        self.assertNotIn(
            "env:\n      CCACHE_DIR: ${{ runner.temp }}",
            self.workflow,
        )
        self.assertIn('export CCACHE_DIR="$cache_dir"', self.workflow)
        self.assertIn('export CCACHE_BASEDIR="$GITHUB_WORKSPACE"', self.workflow)
        self.assertIn("export CCACHE_COMPILERCHECK=content", self.workflow)
        self.assertIn("export CCACHE_MAXSIZE=512M", self.workflow)
        self.assertIn(
            "printf 'CCACHE_DIR=%s\\n' \"$CCACHE_DIR\" >> \"$GITHUB_ENV\"",
            self.workflow,
        )

    def test_cache_checks_compiler_content_and_stays_bounded(self) -> None:
        self.assertIn('ccache --set-config=max_size="$CCACHE_MAXSIZE"', self.workflow)
        self.assertIn(
            'ccache --set-config=compiler_check="$CCACHE_COMPILERCHECK"',
            self.workflow,
        )
        self.assertIn('ccache --set-config=base_dir="$CCACHE_BASEDIR"', self.workflow)
        self.assertIn("ccache --set-config=hash_dir=false", self.workflow)

    def test_cache_is_only_a_compiler_launcher_not_an_acceptance_shortcut(self) -> None:
        self.assertIn("-DCMAKE_C_COMPILER_LAUNCHER=ccache", self.workflow)
        self.assertIn("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache", self.workflow)
        self.assertIn(
            'cmake --build "$LAPLACE_CI_BUILD_DIRECTORY" --parallel 2',
            self.workflow,
        )
        self.assertIn(
            'ctest --test-dir "$LAPLACE_CI_BUILD_DIRECTORY" --output-on-failure',
            self.workflow,
        )
        self.assertIn(
            './tools/tests/verify-registry.sh "$LAPLACE_CI_BUILD_DIRECTORY"',
            self.workflow,
        )
        self.assertIn("ccache --show-stats --verbose", self.workflow)

    def test_cache_restore_key_cannot_cross_native_profiles(self) -> None:
        restore_lines = [
            line.strip()
            for line in self.workflow.splitlines()
            if line.strip().startswith("laplace-ccache-")
        ]
        self.assertTrue(restore_lines)
        for line in restore_lines:
            self.assertIn("${{ matrix.preset }}", line)


if __name__ == "__main__":
    unittest.main()
