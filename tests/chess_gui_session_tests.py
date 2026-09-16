#!/usr/bin/env python3
"""Controls for the external X11 acceptance observer, not substitute GUI evidence."""
import json
import os
import shutil
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
from tools.dependencies import chess_gui_session as proof


class CommandReplies:
    def __init__(self):
        self.pid = 7654
        self.dialog = False
        self.focus = 0
        self.dismiss = True
        self.owner = self.pid
        self.parent = 100
        self.title = "New Game"
        self.keys = []

    def __call__(self, argv, env, deadline, **kwargs):
        name = argv[0]
        if argv[1] == "search":
            return "100\n200\n" if self.dialog else "100\n"
        if name == "xprop":
            return (f"_NET_WM_PID(CARDINAL) = {self.owner}\n"
                    'WM_CLASS(STRING) = "cutechess", "Cutechess"\n'
                    + (f"WM_TRANSIENT_FOR(WINDOW): window id # {hex(self.parent)}\n"
                       if argv[2] == "200" else "WM_TRANSIENT_FOR: not found.\n"))
        if name == "xwininfo":
            return " Width: 600\n Height: 500\n Map State: IsViewable\n"
        if argv[1] == "getwindowname":
            return ("test vs test" if argv[2] == "100" else self.title) + "\n"
        if argv[1] == "windowfocus":
            self.focus = int(argv[2])
            return ""
        if argv[1] == "getwindowfocus":
            return str(self.focus) + "\n"
        if argv[1] == "key":
            assert argv[2] == "--clearmodifiers" and len(argv) == 4
            key = argv[3]
            self.keys.append(key)
            if key == "ctrl+n":
                self.dialog = True
            if key == "Escape" and self.dismiss:
                self.dialog = False
            return ""
        raise AssertionError(argv)


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux X11 collector")
class GuiSessionControls(unittest.TestCase):
    def test_complete_window_protocol_requires_all_four_observed_actions(self):
        replies = CommandReplies()
        child = Mock(pid=replies.pid)
        child.wait.return_value = 0
        observations = []
        with patch.object(proof, "bounded_command", side_effect=replies), \
             patch.object(proof, "process_stamp", return_value=(replies.pid, "/official/cutechess", 19)):
            result = proof.exercise_windows(child, Path("/official/cutechess"), {},
                                            time.monotonic() + 1,
                                            lambda action, value: observations.append((action, value)))
        self.assertEqual(["main", "dialog", "cancel", "quit"], [item[0] for item in observations])
        self.assertEqual(["ctrl+n", "Escape", "ctrl+q"], replies.keys)
        self.assertEqual(100, observations[1][1]["transient_parent"])
        self.assertEqual(replies.pid, result["pid"])

    def test_unrelated_dialog_and_ignored_escape_never_reach_quit(self):
        for field, value in (("parent", 999), ("title", "Unrelated"), ("dismiss", False)):
            with self.subTest(field=field):
                replies = CommandReplies()
                setattr(replies, field, value)
                child = Mock(pid=replies.pid)
                clock = [0.0]
                def advance(duration):
                    clock[0] += max(duration, .001)
                with patch.object(proof, "bounded_command", side_effect=replies), \
                     patch.object(proof, "process_stamp", return_value=(replies.pid, "/gui", 1)), \
                     patch.object(proof.time, "monotonic", side_effect=lambda: clock[0]), \
                     patch.object(proof.time, "sleep", side_effect=advance):
                    with self.assertRaises(TimeoutError):
                        proof.exercise_windows(child, Path("/gui"), {}, .2, lambda *_: None)
                self.assertNotIn("ctrl+q", replies.keys)
                child.wait.assert_not_called()

    def test_nonzero_quit_and_changed_process_are_rejected(self):
        replies = CommandReplies()
        child = Mock(pid=replies.pid)
        child.wait.return_value = 9
        with patch.object(proof, "bounded_command", side_effect=replies), \
             patch.object(proof, "process_stamp", return_value=(replies.pid, "/gui", 1)):
            with self.assertRaisesRegex(RuntimeError, "normal exit"):
                proof.exercise_windows(child, Path("/gui"), {}, time.monotonic() + 1, lambda *_: None)
        with patch.object(proof, "process_stamp", side_effect=[(1, "/gui", 1), (1, "/gui", 2)]), \
             patch.object(proof, "bounded_command") as command:
            with self.assertRaisesRegex(RuntimeError, "replaced"):
                proof.exercise_windows(child, Path("/gui"), {}, time.monotonic() + 1, lambda *_: None)
            command.assert_not_called()

    def test_window_record_requires_real_owner_class_mapping_and_dimensions(self):
        values = {
            "xprop": '_NET_WM_PID(CARDINAL) = 77\nWM_CLASS(STRING) = "cutechess", "Cutechess"\n',
            "xwininfo": " Width: 800\n Height: 600\n Map State: IsViewable\n",
            "xdotool": "a vs b\n",
        }
        self.assertEqual([800, 600], proof.window_record(100, 77, lambda argv: values[argv[0]])["size"])
        for tool, replacement in (
                ("xprop", values["xprop"].replace("= 77", "= 78")),
                ("xprop", values["xprop"].replace("cutechess", "another").replace("Cutechess", "Another")),
                ("xwininfo", values["xwininfo"].replace("IsViewable", "IsUnMapped")),
                ("xwininfo", values["xwininfo"].replace("800", "0")),
                ("xwininfo", values["xwininfo"].replace("600", "1025"))):
            with self.subTest(replacement=replacement):
                changed = dict(values)
                changed[tool] = replacement
                with self.assertRaises(RuntimeError):
                    proof.window_record(100, 77, lambda argv: changed[argv[0]])

    def test_display_acknowledgement_is_bounded_and_in_preauthorized_range(self):
        for data, expected in ((b"0\n", 0), (b"63\n", 63), (b"64\n", None),
                               (b"-1\n", None), (b"1\n2\n", None), (b"", None), (b"1" * 33, None)):
            with self.subTest(data=data):
                reader, writer = os.pipe()
                try:
                    os.write(writer, data)
                    os.close(writer)
                    if expected is None:
                        with self.assertRaises(RuntimeError):
                            proof.display_number(reader, time.monotonic() + .2)
                    else:
                        self.assertEqual(expected, proof.display_number(reader, time.monotonic() + .2))
                finally:
                    os.close(reader)

    def test_xauthority_is_private_precreated_and_never_uses_existing_display(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "authority"
            with patch.object(proof, "bounded_command") as command:
                proof.authorize_display(path, {"DISPLAY": ":999"}, time.monotonic() + 1)
            argv, environment, _ = command.call_args.args
            self.assertEqual(["xauth", "-f", str(path), "source", "-"], argv)
            self.assertEqual(0o600, path.stat().st_mode & 0o777)
            rows = command.call_args.kwargs["input_text"].splitlines()
            self.assertEqual(64, len(rows))
            self.assertEqual([f":{number}" for number in range(64)], [row.split()[1] for row in rows])
            self.assertEqual({"MIT-MAGIC-COOKIE-1"}, {row.split()[2] for row in rows})
            cookies = {row.split()[3] for row in rows}
            self.assertEqual(1, len(cookies))
            self.assertRegex(cookies.pop(), r"^[0-9a-f]{32}$")
            with self.assertRaises(FileExistsError):
                proof.authorize_display(path, {}, time.monotonic() + 1)

    def test_qt_loader_must_identify_selected_plugin_not_same_named_other_sdk(self):
        with tempfile.TemporaryDirectory() as temporary:
            selected = Path(temporary) / "selected/libqxcb.so"
            other = Path(temporary) / "other/libqxcb.so"
            for path in (selected, other):
                path.parent.mkdir()
                path.write_bytes(b"fixture-only-not-a-Qt-library")
            proof.selected_xcb(f'qt.core.library: "{selected}" loaded library\n', selected)
            for log in ("", f'qt.core.library: "{other}" loaded library\n',
                        f'qt.core.library: "{selected}" loaded library\nqt.core.library: "{other}" loaded library\n'):
                with self.subTest(log=log), self.assertRaisesRegex(RuntimeError, "selected xcb"):
                    proof.selected_xcb(log, selected)

    def test_execution_deadline_interrupts_blocking_work_and_restores_handlers(self):
        alarm, term = signal.getsignal(signal.SIGALRM), signal.getsignal(signal.SIGTERM)
        started = time.monotonic()
        with self.assertRaises(TimeoutError):
            with proof.execution_deadline(.03):
                time.sleep(1)
        self.assertLess(time.monotonic() - started, .5)
        self.assertEqual(alarm, signal.getsignal(signal.SIGALRM))
        self.assertEqual(term, signal.getsignal(signal.SIGTERM))
        self.assertEqual(0, signal.getitimer(signal.ITIMER_REAL)[0])
        with self.assertRaises(KeyboardInterrupt):
            with proof.execution_deadline(1):
                os.kill(os.getpid(), signal.SIGTERM)
        self.assertEqual(term, signal.getsignal(signal.SIGTERM))

    def test_group_cleanup_does_not_touch_another_owned_test_process(self):
        outside = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
        target = subprocess.Popen([sys.executable, "-c",
                                   "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); print('ready',flush=True); time.sleep(30)"],
                                  start_new_session=True, stdout=subprocess.PIPE, text=True)
        try:
            with selectors.DefaultSelector() as ready:
                ready.register(target.stdout, selectors.EVENT_READ)
                self.assertTrue(ready.select(timeout=2))
                self.assertEqual("ready", target.stdout.readline().strip())
            bound = proof.process_stamp(target, Path(sys.executable))
            self.assertEqual(target.pid, bound[0])
            with self.assertRaisesRegex(RuntimeError, "executable"):
                proof.process_stamp(target, Path("/bin/false"))
            proof.reap_group(target)
            self.assertIsNotNone(target.poll())
            self.assertIsNone(outside.poll())
        finally:
            proof.reap_group(target)
            outside.terminate()
            outside.wait(timeout=2)
            target.stdout.close()


    def test_cleanup_errors_downgrade_passed_receipt_and_attempt_every_resource(self):
        report = {"status": "passed"}
        gui, server = Mock(), Mock()
        gui.poll.return_value = 0
        server.poll.return_value = None
        stream, private = Mock(), Mock()
        stream.close.side_effect = OSError("fixture log close failure")
        private.cleanup.side_effect = PermissionError("fixture private settings failure")
        with patch.object(proof, "reap_group", side_effect=[PermissionError("fixture group"), None]) as reap:
            proof.cleanup_resources(report, gui, server, [stream], private)
        self.assertEqual([gui, server], [call.args[0] for call in reap.call_args_list])
        stream.close.assert_called_once()
        private.cleanup.assert_called_once()
        self.assertEqual("failed", report["status"])
        self.assertFalse(report["cleanup"]["completed"])
        self.assertEqual(
            [("gui-process-group", "PermissionError"), ("log-stream-0", "OSError"),
             ("private-display-settings", "PermissionError")],
            [(entry["resource"], entry["type"]) for entry in report["cleanup"]["errors"]])
        clean = {"status": "passed"}
        proof.cleanup_resources(clean, None, None, [], None)
        self.assertEqual("passed", clean["status"])
        self.assertTrue(clean["cleanup"]["completed"])

    def test_command_stdin_protocol_and_failed_tool_are_observed(self):
        reply = proof.bounded_command(
            [sys.executable, "-c", "import sys; print(sys.stdin.read(), end='')"],
            dict(os.environ), time.monotonic() + 2, input_text="private xauth fixture\n")
        self.assertEqual("private xauth fixture\n", reply)
        with self.assertRaisesRegex(RuntimeError, "X11 tool failed"):
            proof.bounded_command([sys.executable, "-c", "raise SystemExit(9)"],
                                  dict(os.environ), time.monotonic() + 2)

    def test_package_provisioning_is_idempotent_and_only_requests_missing_packages(self):
        script = REPO / "scripts/provision-chess-x11.sh"
        bash = shutil.which("bash")
        self.assertIsNotNone(bash)
        timeout = shutil.which("timeout")
        self.assertIsNotNone(timeout)
        setup = (REPO / "scripts/setup-chess.sh").read_text()
        self.assertIn('timeout --signal=TERM --kill-after=10s 300s bash "$repository/scripts/provision-chess-x11.sh"', setup)
        apt_options = ["-o", "DPkg::Lock::Timeout=30", "-o", "Acquire::Retries=2",
                       "-o", "Acquire::http::Timeout=30", "-o", "Acquire::https::Timeout=30"]
        fixture = "\n".join([
            "#!" + sys.executable,
            "import json,os,sys",
            "from pathlib import Path",
            "name=Path(sys.argv[0]).name",
            "with open(os.environ['X11_FIXTURE_LOG'],'a') as log: log.write(json.dumps([name]+sys.argv[1:])+'\\n')",
            "if os.environ.get('X11_FIXTURE_WAIT')=='1':",
            "    import time; time.sleep(30)",
            "if name=='dpkg-query':",
            "    if sys.argv[2]=='-f=${db:Status-Status}':",
            "        print('' if sys.argv[-1]==os.environ.get('X11_FIXTURE_MISSING') else 'installed')",
            "    else: print('fixture inventory only')",
            "elif name=='sudo': raise SystemExit(71)",
            "elif name=='apt-get' and os.environ.get('X11_FIXTURE_APT_FAIL')=='1': raise SystemExit(79)",
            "",
        ])
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in ("dpkg-query", "apt-get", "sudo"):
                executable = directory / name
                executable.write_text(fixture)
                executable.chmod(0o700)
            log = directory / "commands.jsonl"
            environment = dict(os.environ, PATH=str(directory) + os.pathsep + os.environ["PATH"],
                               X11_FIXTURE_LOG=str(log), X11_FIXTURE_MISSING="", X11_FIXTURE_APT_FAIL="", X11_FIXTURE_WAIT="")
            def run():
                log.write_text("")
                result = subprocess.run([bash, str(script)], env=environment, capture_output=True,
                                        text=True, timeout=10)
                return result, [json.loads(line) for line in log.read_text().splitlines()]
            result, commands = run()
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertFalse([row for row in commands if row[0] in ("apt-get", "sudo")])
            self.assertIn("fixture inventory only", result.stdout)
            environment["X11_FIXTURE_MISSING"] = "xauth"
            result, commands = run()
            changes = [row for row in commands if row[0] in ("apt-get", "sudo")]
            if os.geteuid() == 0:
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual([["apt-get"] + apt_options + ["update"],
                                  ["apt-get"] + apt_options + ["install", "--yes", "--no-install-recommends", "xauth"]], changes)
                environment["X11_FIXTURE_APT_FAIL"] = "1"
                result, commands = run()
                self.assertEqual(79, result.returncode)
                self.assertEqual([["apt-get"] + apt_options + ["update"]], [row for row in commands if row[0] == "apt-get"])
            else:
                self.assertEqual(71, result.returncode)
                self.assertEqual([["sudo", "-n", "bash", str(script)]], changes)
            environment["X11_FIXTURE_WAIT"] = "1"
            log.write_text("")
            started = time.monotonic()
            result = subprocess.run([timeout, "--signal=TERM", "--kill-after=1s", "1s", bash, str(script)],
                                    env=environment, capture_output=True, text=True, timeout=4)
            self.assertEqual(124, result.returncode)
            self.assertLess(time.monotonic() - started, 2.5)
            self.assertTrue(log.read_text(), "the bounded provisioner did not start its tool fixture")


if __name__ == "__main__":
    unittest.main()
