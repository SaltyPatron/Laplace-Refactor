#!/usr/bin/env python3
"""Observe real Cute Chess windows/actions on a private Linux virtual display.

The selected dependency source and manifest remain owned by chess_tools.
A passed result covers virtual X11 interaction, not an operator desktop or chess play.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
import math
import os
from pathlib import Path
import re
import secrets
import selectors
import shutil
import signal
import subprocess
import sys
import tempfile
import time

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY))
from tools.dependencies import chess_tools as dependencies
from tools.dependencies import x11_runtime

SCHEMA = "laplace.refactor-cutechess-x11/v1"
TOOLS = ("Xvfb", "xauth", "xdotool", "xprop", "xwininfo")
AUTHORIZED_DISPLAY_COUNT = 64


def check(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def checkpoint(output, report):
    temporary = output / "session.json.pending"
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n")
    temporary.replace(output / "session.json")


@contextmanager
def execution_deadline(seconds):
    previous_alarm = signal.getsignal(signal.SIGALRM)
    previous_term = signal.getsignal(signal.SIGTERM)
    timer = signal.getitimer(signal.ITIMER_REAL)
    check(timer[0] == 0, "GUI acceptance cannot replace an existing interval timer")
    def expired(_signum, _frame):
        raise TimeoutError("GUI acceptance execution deadline expired")
    def cancelled(_signum, _frame):
        raise KeyboardInterrupt("GUI acceptance interrupted")
    signal.signal(signal.SIGALRM, expired)
    signal.signal(signal.SIGTERM, cancelled)
    signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield time.monotonic() + seconds
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous_alarm)
        signal.signal(signal.SIGTERM, previous_term)


def reap_group(child):
    if child is None:
        return
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(child.pid, sig)
        except ProcessLookupError:
            break
        try:
            child.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
    child.wait(timeout=2)



def cleanup_resources(report, gui, server, streams, private):
    """Attempt every owned cleanup and never retain a passed receipt on failure."""
    cleanup = {"gui_was_running": gui is not None and gui.poll() is None,
               "server_was_running": server is not None and server.poll() is None,
               "errors": []}
    report["cleanup"] = cleanup
    operations = [("gui-process-group", lambda: reap_group(gui)),
                  ("display-process-group", lambda: reap_group(server))]
    operations.extend((f"log-stream-{number}", stream.close)
                      for number, stream in enumerate(streams))
    if private is not None:
        operations.append(("private-display-settings", private.cleanup))
    for resource, operation in operations:
        try:
            operation()
        except (Exception, KeyboardInterrupt) as error:
            cleanup["errors"].append({"resource": resource, "type": type(error).__name__})
    cleanup["completed"] = not cleanup["errors"]
    if not cleanup["completed"]:
        report["status"] = "failed"
        report.setdefault("error", "owned GUI acceptance cleanup failed")

def bounded_command(arguments, environment, deadline, *, input_text=None, absent=False):
    left = deadline - time.monotonic()
    check(left > 0, "GUI acceptance execution deadline expired")
    result = subprocess.run(arguments, env=environment, input=input_text,
                            stdin=subprocess.DEVNULL if input_text is None else None,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                            timeout=min(5, left))
    # This validates retained command output, not subprocess capture allocation.
    check(len(result.stdout) + len(result.stderr) <= 131072, "X11 tool output is too large")
    if absent and result.returncode == 1:
        return ""
    check(result.returncode == 0, "X11 tool failed: " + arguments[0])
    return result.stdout


def authorize_display(path, environment, deadline):
    path.touch(mode=0o600, exist_ok=False)
    cookie = secrets.token_hex(16)
    # Authorize a finite display range before Xvfb starts. -displayfd still chooses
    # a free display atomically; no existing X server or authority file is touched.
    commands = "".join(f"add :{number} MIT-MAGIC-COOKIE-1 {cookie}\n"
                       for number in range(AUTHORIZED_DISPLAY_COUNT))
    bounded_command(["xauth", "-f", str(path), "source", "-"], environment, deadline,
                    input_text=commands)
    path.chmod(0o600)


def display_number(descriptor, deadline):
    data = bytearray()
    with selectors.DefaultSelector() as readiness:
        readiness.register(descriptor, selectors.EVENT_READ)
        while b"\n" not in data:
            check(readiness.select(timeout=max(0, deadline - time.monotonic())),
                  "Xvfb did not publish a ready display")
            block = os.read(descriptor, 32)
            check(block and len(data) + len(block) <= 32, "invalid Xvfb display acknowledgement")
            data.extend(block)
    check(re.fullmatch(rb"[0-9]+\n", data) is not None, "invalid Xvfb display acknowledgement")
    number = int(data)
    check(0 <= number < AUTHORIZED_DISPLAY_COUNT, "Xvfb selected a display outside the authorization envelope")
    return number


def process_stamp(child, executable):
    check(child.poll() is None, "Cute Chess exited before the interaction completed")
    resolved = Path(f"/proc/{child.pid}/exe").resolve(strict=True)
    check(resolved == executable.resolve(strict=True), "Cute Chess process executable changed")
    data = Path(f"/proc/{child.pid}/stat").read_text()
    fields = data[data.rfind(")") + 2:].split()
    check(len(fields) > 19, "Cute Chess process start time is missing")
    return (child.pid, str(resolved), int(fields[19]))


def window_record(identifier, pid, command):
    properties = command(["xprop", "-id", str(identifier), "_NET_WM_PID", "WM_CLASS", "WM_TRANSIENT_FOR"])
    geometry = command(["xwininfo", "-id", str(identifier)])
    caption = command(["xdotool", "getwindowname", str(identifier)]).rstrip("\n")
    owner = re.search(r"^_NET_WM_PID\(CARDINAL\) = (\d+)$", properties, re.M)
    check(owner and int(owner[1]) == pid, "window PID does not match the owned Cute Chess process")
    check(re.search(r'^WM_CLASS\(STRING\) = .*"cutechess"', properties, re.M | re.I),
          "window class does not identify Cute Chess")
    check("Map State: IsViewable" in geometry, "window is not mapped and viewable")
    size = [re.search(r"^\s*" + key + r": (\d+)$", geometry, re.M) for key in ("Width", "Height")]
    check(all(size) and 0 < int(size[0][1]) <= 1280 and 0 < int(size[1][1]) <= 1024,
          "window dimensions do not fit the admitted virtual display")
    transient = re.search(r"WM_TRANSIENT_FOR\(WINDOW\): window id # (0x[0-9a-f]+)", properties, re.I)
    return {"window_id": identifier, "process_id": pid, "caption": caption,
            "size": [int(value[1]) for value in size], "mapped": True,
            "transient_parent": int(transient[1], 16) if transient else None,
            "x_properties": properties, "x_geometry": geometry}


def exercise_windows(child, executable, environment, deadline, observe):
    stamp = process_stamp(child, executable)
    def command(arguments):
        check(process_stamp(child, executable) == stamp, "owned GUI process was replaced")
        return bounded_command(arguments, environment, deadline,
                               absent=arguments[:2] == ["xdotool", "search"])
    def inventory():
        lines = command(["xdotool", "search", "--all", "--onlyvisible",
                         "--pid", str(child.pid), "--name", ".*"]).splitlines()
        check(len(lines) <= 24 and all(re.fullmatch(r"[1-9]\d*", line) for line in lines),
              "invalid visible-window inventory")
        return [window_record(value, child.pid, command) for value in sorted(set(map(int, lines)))]
    def until(predicate, label):
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(min(.04, max(0, deadline - time.monotonic())))
        raise TimeoutError(label)
    def unique(rows, predicate):
        selected = [row for row in rows if predicate(row)]
        check(len(selected) <= 1, "ambiguous Cute Chess window selection")
        return selected[0] if selected else None
    def focus(identifier):
        command(["xdotool", "windowfocus", str(identifier)])
        until(lambda: command(["xdotool", "getwindowfocus", "-f"]).strip() == str(identifier),
              "GUI keyboard focus was not observed")
    def key(value):
        # XTEST input goes to observed focus; no SendEvent/window-manager action.
        command(["xdotool", "key", "--clearmodifiers", value])

    main = until(lambda: unique(inventory(), lambda row: row["transient_parent"] is None
                               and " vs " in row["caption"]), "main game window did not map")
    observe("main", main)
    existing = {row["window_id"] for row in inventory()}
    focus(main["window_id"])
    key("ctrl+n")
    dialog = until(lambda: unique(inventory(), lambda row: row["window_id"] not in existing
                                 and row["caption"] == "New Game"
                                 and row["transient_parent"] == main["window_id"]),
                   "Ctrl+N did not open the New Game dialog")
    observe("dialog", dialog)
    focus(dialog["window_id"])
    key("Escape")
    until(lambda: dialog["window_id"] not in {row["window_id"] for row in inventory()},
          "Escape did not dismiss the New Game dialog")
    check(main["window_id"] in {row["window_id"] for row in inventory()},
          "cancelling the dialog also removed the main window")
    observe("cancel", {"dismissed_window": dialog["window_id"]})
    focus(main["window_id"])
    key("ctrl+q")
    code = child.wait(timeout=min(10, max(.001, deadline - time.monotonic())))
    check(code == 0, "Ctrl+Q did not produce a successful normal exit")
    observe("quit", {"return_code": code})
    return {"pid": stamp[0], "executable": stamp[1], "start_ticks": stamp[2]}


def selected_xcb(log, plugin):
    paths = []
    for match in re.finditer(r'^qt\.core\.library: ("(?:\\.|[^"\\])*") loaded library\s*$', log, re.M):
        path = Path(json.loads(match[1]))
        if path.name == "libqxcb.so":
            paths.append(str(path.resolve(strict=True)))
    check(set(paths) == {str(plugin.resolve(strict=True))},
          "GUI loader did not identify the selected xcb platform plugin")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("/opt/laplace/tools/chess"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--deadline-seconds", type=float, default=60)
    args = parser.parse_args(argv)
    check(sys.platform.startswith("linux"), "this acceptance covers Linux virtual X11 only")
    check(math.isfinite(args.deadline_seconds) and 10 <= args.deadline_seconds <= 120,
          "execution deadline must be within 10..120 seconds")
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema": SCHEMA, "status": "failed", "scope": "virtual-X11-interaction",
              "operator_desktop_verified": False, "engine_game_verified": False,
              "visual_board_correctness_verified": False, "observations": []}
    begun = time.monotonic()
    server = gui = None
    private = None
    streams = []
    try:
        with execution_deadline(args.deadline_seconds) as deadline:
            check(dependencies.SCRATCH.is_dir(), "dependency scratch directory is missing")
            selected, artifacts = dependencies.configuration()
            manifest = dependencies.verify_installation(args.prefix, selected, artifacts,
                                                        "cutechess", with_gui=True)
            tool = manifest["tools"]["cutechess"]
            binary = Path(tool["gui"]["executable"]).resolve(strict=True)
            qt = Path(tool["qt_prefix"]).resolve(strict=True)
            plugin = qt / "plugins/platforms/libqxcb.so"
            check(plugin.is_file(), "the selected Qt SDK has no xcb platform plugin")
            identity = {"source": tool["source"], "revision": tool["revision"],
                        "binary": str(binary), "binary_sha256": dependencies.digest(binary),
                        "qt_prefix": str(qt), "xcb": str(plugin),
                        "xcb_sha256": dependencies.digest(plugin),
                        "manifest_sha256": dependencies.digest(args.prefix / "current.json")}
            check(identity["binary_sha256"] == tool["gui"]["sha256"], "GUI differs from the selected manifest")
            runtime = x11_runtime.load()
            environment = dependencies.qt_gui_environment(qt)
            if runtime is not None:
                environment = x11_runtime.selected_environment(runtime, environment, qt_prefix=qt)
            unavailable = [name for name in TOOLS if not shutil.which(name, path=environment.get("PATH"))]
            check(not unavailable, "missing virtual X11 tools: " + ", ".join(unavailable))
            selected_tools = {name: str(Path(shutil.which(name, path=environment.get("PATH"))).resolve(strict=True))
                              for name in TOOLS}
            runtime_files = None
            if runtime is not None:
                observed_tools, runtime_files = x11_runtime.loaded_files(environment, deadline)
                check(observed_tools == runtime["tools"], "X11 tools differ from their selected runtime")
                report["private_runtime"] = {
                    "runtime_id": runtime["runtime_id"], "manifest_sha256": runtime["manifest_sha256"],
                    "root": runtime["root"], "packages": runtime["packages"],
                    "loaded_files": runtime_files, "host_packages_installed": False}
            report.update(selection=identity, tools=selected_tools)
            checkpoint(output, report)
            private = tempfile.TemporaryDirectory(prefix="refactor-cutechess-x11-", dir=dependencies.SCRATCH)
            directory = Path(private.name)
            for name in ("DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM",
                         "QT_QPA_PLATFORMTHEME", "QT_QPA_GENERIC_PLUGINS", "QT_STYLE_OVERRIDE"):
                environment.pop(name, None)
            environment.update({"LC_ALL": "C", "LANG": "C", "QT_DEBUG_PLUGINS": "1",
                                "QT_MESSAGE_PATTERN": "%{category}: %{message}",
                                "QT_LOGGING_RULES": "qt.core.library=true;qt.core.plugin.*=true"})
            for name in ("TMPDIR", "TMP", "TEMP"):
                environment[name] = str(directory)
            for name in ("XDG_CONFIG_HOME", "XDG_CONFIG_DIRS", "XDG_DATA_HOME",
                         "XDG_DATA_DIRS", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
                target = directory / name.lower()
                target.mkdir(mode=0o700)
                environment[name] = str(target)
            authority = directory / "authority"
            authorize_display(authority, environment, deadline)
            reader, writer = os.pipe()
            server_log = (output / "xvfb.log").open("w")
            streams.append(server_log)
            try:
                server = subprocess.Popen(["Xvfb", "-displayfd", str(writer), "-auth", str(authority),
                                           "-screen", "0", "1280x1024x24", "-nolisten", "tcp", "-noreset"],
                                          env=environment, stdin=subprocess.DEVNULL,
                                          stdout=server_log, stderr=subprocess.STDOUT,
                                          pass_fds=(writer,), start_new_session=True)
                os.close(writer)
                writer = None
                number = display_number(reader, deadline)
            finally:
                os.close(reader)
                if writer is not None:
                    os.close(writer)
            environment.update(DISPLAY=f":{number}", XAUTHORITY=str(authority))
            report["display"] = {"number": number, "server_pid": server.pid, "size": [1280, 1024, 24],
                                 "authentication": "private MIT-MAGIC-COOKIE-1", "tcp_listening": False}
            gui_log = (output / "gui.log").open("w")
            streams.append(gui_log)
            gui = subprocess.Popen([str(binary), "-platform", "xcb"], env=environment,
                                   stdin=subprocess.DEVNULL, stdout=gui_log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
            def observe(action, value):
                report["observations"].append({"action": action, "observed": value})
                checkpoint(output, report)
            report["process"] = exercise_windows(gui, binary, environment, deadline, observe)
            gui_log.flush()
            selected_xcb((output / "gui.log").read_text(errors="replace"), plugin)
            check(dependencies.digest(binary) == identity["binary_sha256"]
                  and dependencies.digest(plugin) == identity["xcb_sha256"]
                  and dependencies.digest(args.prefix / "current.json") == identity["manifest_sha256"],
                  "selected GUI, Qt plugin or dependency manifest changed during interaction")
            if runtime is not None:
                current_runtime = x11_runtime.load()
                check(current_runtime is not None
                      and current_runtime["manifest_sha256"] == runtime["manifest_sha256"],
                      "selected X11 runtime changed during interaction")
                observed_tools, final_files = x11_runtime.loaded_files(environment, deadline)
                check(observed_tools == runtime["tools"] and final_files == runtime_files,
                      "selected X11 executable or shared library changed during interaction")
            report.update(status="passed", mapped_windows_and_actions_verified=True,
                          normal_exit_verified=True, selected_xcb_loaded=True,
                          gui_log_sha256=dependencies.digest(output / "gui.log"))
    except (RuntimeError, OSError, ValueError, KeyError, TypeError,
            subprocess.SubprocessError, KeyboardInterrupt) as error:
        report["error"] = str(error) if isinstance(error, (RuntimeError, TimeoutError)) else type(error).__name__
    finally:
        # Deadlines no longer interrupt cleanup of only the groups created above.
        cleanup_resources(report, gui, server, streams, private)
        report["seconds_including_cleanup"] = time.monotonic() - begun
        report["execution_deadline_seconds"] = args.deadline_seconds
        report["cleanup_allowance_seconds"] = 12
        checkpoint(output, report)
    print(json.dumps({"schema": SCHEMA, "status": report["status"],
                      "receipt": str(output / "session.json"), "scope": report["scope"]}))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
