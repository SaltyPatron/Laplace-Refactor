#!/usr/bin/env python3

import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
ACQUIRE = REPOSITORY / "tools" / "dependencies" / "acquire-locked-git.sh"


class LockedGitAcquisitionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory(
            prefix="laplace-locked-git-acquisition."
        )
        self.root = Path(self.temporary_directory.name)
        self.remote = self.root / "remote"
        self.work = self.root / "work"
        subprocess.run(["/usr/bin/git", "init", "--quiet", "--bare", self.remote], check=True)
        subprocess.run(["/usr/bin/git", "init", "--quiet", self.work], check=True)
        subprocess.run(
            ["/usr/bin/git", "-C", self.work, "config", "user.email", "laplace@example.invalid"],
            check=True,
        )
        subprocess.run(
            ["/usr/bin/git", "-C", self.work, "config", "user.name", "Laplace Test"],
            check=True,
        )
        (self.work / "source.txt").write_text("exact source\n", encoding="utf-8")
        subprocess.run(["/usr/bin/git", "-C", self.work, "add", "source.txt"], check=True)
        subprocess.run(["/usr/bin/git", "-C", self.work, "commit", "--quiet", "-m", "source"], check=True)
        self.revision = subprocess.check_output(
            ["/usr/bin/git", "-C", self.work, "rev-parse", "HEAD"], text=True
        ).strip()
        subprocess.run(
            ["/usr/bin/git", "-C", self.work, "push", "--quiet", str(self.remote), "HEAD:main"],
            check=True,
        )
        self.lock = self.root / "lock.json"
        self.lock.write_text(
            json.dumps(
                {
                    "dependencies": {
                        "fixture": {
                            "upstream": str(self.remote),
                            "revision": self.revision,
                        }
                    }
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def test_transient_fetch_failure_is_retried_without_weakening_revision_check(self) -> None:
        fake_bin = self.root / "fake-bin"
        fake_bin.mkdir()
        marker = self.root / "first-fetch-failed"
        wrapper = fake_bin / "git"
        wrapper.write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "if [[ \" $* \" == *\" fetch \"* && ! -e \"$LAPLACE_TEST_FETCH_MARKER\" ]]; then\n"
            "  : > \"$LAPLACE_TEST_FETCH_MARKER\"\n"
            "  exit 128\n"
            "fi\n"
            "exec /usr/bin/git \"$@\"\n",
            encoding="utf-8",
        )
        wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
        destination = self.root / "acquired"
        environment = dict(os.environ)
        environment.update(
            {
                "PATH": f"{fake_bin}:/usr/bin:/bin",
                "LAPLACE_GIT_FETCH_RETRY_ATTEMPTS": "2",
                "LAPLACE_GIT_FETCH_RETRY_DELAY_SECONDS": "0",
                "LAPLACE_TEST_FETCH_MARKER": str(marker),
            }
        )

        subprocess.run(
            [str(ACQUIRE), str(self.lock), str(destination), "fixture"],
            env=environment,
            check=True,
        )

        self.assertTrue(marker.is_file())
        observed = subprocess.check_output(
            ["/usr/bin/git", "-C", destination / "fixture", "rev-parse", "HEAD"],
            text=True,
        ).strip()
        self.assertEqual(observed, self.revision)

    def test_unknown_dependency_fails_without_publishing_destination(self) -> None:
        destination = self.root / "acquired"
        completed = subprocess.run(
            [str(ACQUIRE), str(self.lock), str(destination), "missing"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
