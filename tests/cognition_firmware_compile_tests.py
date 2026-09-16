#!/usr/bin/env python3
"""Execute the real native firmware compiler and its proof transport contract."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
COMPILER = Path(sys.argv.pop(1)).resolve(strict=True)


class FirmwareCompilerTests(unittest.TestCase):
    def compile(self, *arguments: str) -> tuple[dict, subprocess.CompletedProcess]:
        completed = subprocess.run([str(COMPILER), *arguments], capture_output=True,
                                   text=True, timeout=30, check=False)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual(document["schema"], "laplace.cognition-firmware-image/v1")
        self.assertEqual(document["image_bytes"], len(bytes.fromhex(document["image_hex"])))
        self.assertRegex(document["program_id"], r"^[0-9a-f]{64}$")
        return document, completed

    def test_native_image_binds_observation_and_prior_answer_without_literal_ids(self):
        arguments = ("--relations", "constituent,container")
        unbound, _ = self.compile(*arguments)
        observed, _ = self.compile(*arguments, "--goal", "1:observation")
        earlier, _ = self.compile(*arguments, "--goal", "1:answer:0")
        self.assertEqual(unbound["goal_bindings"], [])
        self.assertEqual(observed["goal_bindings"],
                         [{"step": 1, "source": "observation", "source_step": 0}])
        self.assertEqual(earlier["goal_bindings"],
                         [{"step": 1, "source": "answer", "source_step": 0}])
        self.assertEqual(len({item["program_id"] for item in (unbound, observed, earlier)}), 3)
        self.assertEqual(len({item["image_hex"] for item in (unbound, observed, earlier)}), 3)
        repeated, _ = self.compile(*arguments, "--goal", "1:observation")
        self.assertEqual(repeated, observed)
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            path = Path(temporary) / "stored-program.bin"
            stored, _ = self.compile(*arguments, "--goal", "1:observation",
                                     "--output", str(path))
            self.assertEqual(path.read_bytes(), bytes.fromhex(observed["image_hex"]))
            self.assertEqual(stored["program_id"], observed["program_id"])

    def test_invalid_goal_bindings_never_publish_an_image(self):
        invalid = (
            ("--goal", "0:observation"),
            ("--goal", "2:observation"),
            ("--goal", "1:answer:1"),
            ("--goal", "1:answer:2"),
            ("--goal", "1:observation", "--goal", "1:answer:0"),
            ("--goal", "1:literal:" + "ab" * 16),
            ("--goal", "-1:observation"),
            ("--goal", "4294967296:observation"),
            ("--goal", "1:answer:-1"),
            ("--goal", "1:answer:4294967296"),
            ("--goal", "1:observation:0"),
            ("--goal", "1:"),
        )
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            path = Path(temporary) / "must-not-exist.bin"
            for arguments in invalid:
                with self.subTest(arguments=arguments):
                    completed = subprocess.run(
                        [str(COMPILER), "--relations", "constituent,container",
                         "--output", str(path), *arguments],
                        text=True, capture_output=True, timeout=30, check=False)
                    self.assertEqual(completed.returncode, 64, completed.stderr)
                    self.assertEqual(completed.stdout, "")
                    self.assertFalse(path.exists())
            for arguments in (("--auto", "--goal", "1:observation"),
                              ("--relation", "constituent", "--goal", "1:observation"),
                              ("--relations", "constituent,container", "--goal")):
                completed = subprocess.run([str(COMPILER), *arguments], text=True,
                                           capture_output=True, timeout=30, check=False)
                self.assertEqual(completed.returncode, 64, completed.stderr)
                self.assertEqual(completed.stdout, "")

    def test_proof_owner_requires_exact_native_goal_echo_and_compiler_identity(self):
        path = ROOT / "tools/delivery/product_cognition_live_proof.py"
        spec = importlib.util.spec_from_file_location("goal_compiler_proof", path)
        assert spec is not None and spec.loader is not None
        owner = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = owner
        spec.loader.exec_module(owner)
        goals = ((1, "observation", 0),)
        document, receipt = owner.compile_firmware(COMPILER, ("constituent", "container"), goals)
        self.assertEqual(receipt["argv"][-2:], ["--goal", "1:observation"])
        self.assertEqual(receipt["compiler"], owner.compiler_identity(COMPILER))
        self.assertEqual(receipt["compiler"]["sha256"], owner.u.sha256_file(COMPILER))
        for changed in ([], [{"step": 1, "source": "answer", "source_step": 0}],
                        [{"step": True, "source": "observation", "source_step": 0}]):
            with self.subTest(changed=changed), mock.patch.object(
                    owner.subprocess, "run",
                    return_value=subprocess.CompletedProcess(
                        receipt["argv"], 0, json.dumps({**document, "goal_bindings": changed}), "")):
                with self.assertRaisesRegex(RuntimeError, "compiler result is invalid"):
                    owner.compile_firmware(COMPILER, ("constituent", "container"), goals)
        identities = [receipt["compiler"], {**receipt["compiler"], "sha256": "00" * 32}]
        with mock.patch.object(owner, "compiler_identity", side_effect=identities), \
             self.assertRaisesRegex(RuntimeError, "compiler changed during execution"):
            owner.compile_firmware(COMPILER, ("constituent", "container"), goals)


if __name__ == "__main__":
    unittest.main(verbosity=2)
