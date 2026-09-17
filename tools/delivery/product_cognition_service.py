#!/usr/bin/env python3
"""Converge one persistent cognition service owner for the declared runner.

This is service lifecycle orchestration. An enabled unit is not cold-boot proof
or cognition acceptance. Restart additionally verifies the existing activated
package owner and the PID-bound local health transport.
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import pwd
import re
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
UNIT = "laplace-refactor-cognition.service"
USER = "laplace-runner"
MARKER = Path("/opt/laplace/receipts/bootstrap/cognition-user-service.json")
SCHEMA = "laplace.cognition-user-service-owner/v1"
MANAGED_OPERATOR_FILE = Path("/opt/laplace/secrets/operator.env")
HTTP_AUTH_MANAGED_OPERATOR = "managed-operator"
RESULT_SCHEMA = "laplace.cognition-service-lifecycle/v1"
PROPERTIES = ("LoadState", "ActiveState", "SubState", "UnitFileState",
              "FragmentPath", "NeedDaemonReload", "User", "MainPID", "DropInPaths", "ControlGroup")


class ServiceError(RuntimeError):
    pass


def require(value: bool, message: str) -> None:
    if not value:
        raise ServiceError(message)


def canonical(value: dict) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def file_bytes(path: Path, uid: int | None = None) -> bytes:
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and not path.is_symlink() and
            info.st_size <= 1024 * 1024, "unsafe service file: " + str(path))
    require(uid is None or info.st_uid == uid, "service file owner differs: " + str(path))
    require(uid is None or not info.st_mode & 0o022,
            "service file is writable by another identity: " + str(path))
    return path.read_bytes()


def physical_directory(path: Path, uid: int | None = None) -> None:
    require(path.is_absolute(), "service directory must be absolute")
    for parent in (*reversed(path.parents), path):
        require(parent.is_dir() and not parent.is_symlink(),
                "service directory is absent or linked: " + str(parent))
    require(uid is None or path.stat().st_uid == uid,
            "service directory owner differs: " + str(path))


def atomic_write(path: Path, content: bytes) -> None:
    physical_directory(path.parent)
    require(not path.is_symlink(), "refusing a linked service destination")
    descriptor, temporary = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            os.fchmod(stream.fileno(), 0o644)
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def run_command(command: list[str], environment: dict, timeout: float = 30) -> subprocess.CompletedProcess:
    return subprocess.run(command, env=environment, text=True, capture_output=True,
                          timeout=timeout, check=False)


class Owner:
    def __init__(self, *, uid: int, home: Path, repository: Path = ROOT,
                 marker: Path = MARKER, runtime: Path | None = None,
                 system_unit: Path = Path("/etc/systemd/system") / UNIT,
                 execute=run_command):
        self.uid = uid
        self.home = home
        self.repository = repository
        self.marker = marker
        self.runtime = runtime or Path("/run/user") / str(uid)
        self.system_unit = system_unit
        self.user_unit = home / ".config/systemd/user" / UNIT
        self.execute = execute

    def environment(self) -> dict:
        environment = os.environ.copy()
        # The manager and unit search path must belong to the declared account,
        # independently of inherited CI HOME/XDG variables.
        environment.update(HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / ".config"),
                           XDG_RUNTIME_DIR=str(self.runtime),
                           DBUS_SESSION_BUS_ADDRESS="unix:path=" + str(self.runtime / "bus"))
        for name in ("SYSTEMD_UNIT_PATH", "SYSTEMD_BUS_ADDRESS", "SYSTEMD_HOST",
                     "SYSTEMD_MACHINE"):
            environment.pop(name, None)
        return environment

    def command(self, arguments: list[str], *, scope: str = "user",
                allowed: tuple[int, ...] = (0,), timeout: float = 30) -> subprocess.CompletedProcess:
        executable = ["/usr/bin/systemctl"]
        if scope == "user":
            executable.append("--user")
        result = self.execute(executable + arguments, self.environment(), timeout)
        require(result.returncode in allowed,
                "service manager command failed: " + " ".join(executable + arguments) +
                ": " + result.stderr.strip()[:2000])
        return result

    def observe(self, scope: str) -> dict:
        result = self.command(["show", UNIT, "--no-pager",
                               "--property=" + ",".join(PROPERTIES)],
                              scope=scope, allowed=(0, 1, 4))
        values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
        require(values.get("LoadState") in ("loaded", "not-found"),
                "service manager did not return a supported unit state")
        if result.returncode:
            require(values["LoadState"] == "not-found",
                    "service manager failed to observe the selected unit")
        return values

    @staticmethod
    def running(state: dict) -> bool:
        return (state.get("ActiveState") in ("active", "activating", "reloading", "deactivating")
                or int(state.get("MainPID", "0")) != 0)

    def manager(self, *, enable_linger: bool) -> None:
        if enable_linger:
            result = self.execute(["/usr/bin/loginctl", "enable-linger", USER],
                                  self.environment(), 30)
            require(result.returncode == 0, "could not enable persistent runner user manager")
        result = self.execute(["/usr/bin/loginctl", "show-user", USER, "--property=Linger", "--value"],
                              self.environment(), 30)
        require(result.returncode == 0 and result.stdout.strip() == "yes",
                "runner linger is not enabled")
        physical_directory(self.runtime, self.uid)
        require(stat.S_IMODE(self.runtime.stat().st_mode) == 0o700,
                "runner runtime directory must have mode 0700")
        bus = (self.runtime / "bus").lstat()
        require(stat.S_ISSOCK(bus.st_mode) and bus.st_uid == self.uid,
                "runner user manager bus is not the owned socket")

    def http_auth(self, selection: dict | None = None) -> dict:
        selection = self.receipt() if selection is None else selection
        mode = selection.get("http_auth", "none") if selection is not None else "none"
        return ({"mode": mode, "credential_source": str(MANAGED_OPERATOR_FILE)}
                if mode == HTTP_AUTH_MANAGED_OPERATOR else {"mode": "none"})

    def desired(self, scope: str, *, http_auth: str | None = None) -> bytes:
        relative = "packaging/systemd/" + ("user/" if scope == "user" else "") + UNIT
        content = file_bytes(self.repository / relative)
        # The shipped envelopes stay suitable for standalone loopback use.
        # Only an explicit, retained user-service selection adds this fixed path.
        if scope == "user":
            mode = self.http_auth()["mode"] if http_auth is None else http_auth
            require(mode in ("none", HTTP_AUTH_MANAGED_OPERATOR), "unknown cognition HTTP authority")
            if mode == HTTP_AUTH_MANAGED_OPERATOR:
                before = (b"ExecStart=/opt/laplace/runtime/refactor/bin/laplace-cognition-service "
                          b"--socket /opt/laplace/runtime/laplace-cognition.sock\n")
                require(content.count(before) == 1, "cognition service ExecStart template differs")
                content = content.replace(before, before[:-1] + b" --api-operator-token-file " +
                                          str(MANAGED_OPERATOR_FILE).encode("ascii") + b"\n")
        return content

    def receipt(self) -> dict | None:
        if not self.marker.exists() and not self.marker.is_symlink():
            return None
        physical_directory(self.user_unit.parent, self.uid)
        value = json.loads(file_bytes(self.marker, self.uid))
        require(isinstance(value, dict), "service owner receipt is not an object")
        expected = {"schema", "scope", "uid", "user", "unit", "unit_path",
                    "unit_sha256", "persistent_manager", "cold_boot_proven", "receipt_sha256"}
        require(set(value) in (expected, expected | {"http_auth"}) and
                value.get("http_auth", HTTP_AUTH_MANAGED_OPERATOR) == HTTP_AUTH_MANAGED_OPERATOR and
                value["schema"] == SCHEMA and
                value["scope"] == "user" and value["uid"] == self.uid and
                value["user"] == USER and value["unit"] == UNIT and
                value["unit_path"] == str(self.user_unit) and
                value["persistent_manager"] is True and value["cold_boot_proven"] is False,
                "service selection receipt identifies another owner")
        identity = value["receipt_sha256"]
        payload = {key: item for key, item in value.items() if key != "receipt_sha256"}
        require(identity == digest(canonical(payload)), "service selection receipt identity differs")
        require(digest(file_bytes(self.user_unit, self.uid)) == value["unit_sha256"],
                "selected user unit differs from its retained receipt")
        return value

    def check_system(self, state: dict, *, selected: bool) -> None:
        if state["LoadState"] == "not-found":
            require(not selected and not self.system_unit.exists() and not self.system_unit.is_symlink(),
                    "system unit file and manager state disagree")
            return
        require(state.get("FragmentPath") == str(self.system_unit) and
                state.get("NeedDaemonReload") == "no" and state.get("DropInPaths") == "",
                "system unit origin or loaded generation differs")
        require(file_bytes(self.system_unit, 0) == self.desired("system"),
                "system unit does not have the exact mutually exclusive envelope")
        require(state.get("User") == USER, "system cognition unit has another identity")
        if selected:
            require(state.get("UnitFileState") == "enabled", "system unit is not enabled")
        else:
            require(not self.running(state), "system cognition owner is already running")

    def user_state_if_available(self) -> dict:
        if (self.runtime / "bus").exists():
            return self.observe("user")
        require(not self.user_unit.exists() and not self.user_unit.is_symlink(),
                "an existing user unit has no observable manager")
        return {"LoadState": "not-found", "ActiveState": "inactive", "MainPID": "0"}

    def verify(self) -> dict:
        selection = self.receipt()
        system = self.observe("system")
        if selection is None:
            self.check_system(system, selected=True)
            user = self.user_state_if_available()
            require(not self.running(user), "a competing user cognition owner is running")
            return {"scope": "system", "unit": system, "competing_unit": user,
                    "http_auth": {"mode": "none"}}
        self.check_system(system, selected=False)
        self.manager(enable_linger=False)
        require(file_bytes(self.user_unit, self.uid) == self.desired("user"),
                "selected user unit differs from the current source envelope")
        user = self.observe("user")
        require(user.get("LoadState") == "loaded" and
                user.get("FragmentPath") == str(self.user_unit) and
                user.get("NeedDaemonReload") == "no" and user.get("DropInPaths") == "" and
                user.get("UnitFileState") == "enabled",
                "selected user unit is not loaded and persistently enabled")
        return {"scope": "user", "unit": user, "competing_unit": system,
                "selection_receipt_sha256": selection["receipt_sha256"],
                "http_auth": self.http_auth(selection)}

    def ensure(self, *, http_auth: str | None = None) -> dict:
        require(http_auth in (None, HTTP_AUTH_MANAGED_OPERATOR), "unsupported HTTP authority selection")
        previous = self.receipt()
        mode = http_auth or self.http_auth(previous)["mode"]
        if mode == HTTP_AUTH_MANAGED_OPERATOR:
            sys.path.insert(0, str(self.repository / "tools"))
            from openai_api_service import load_operator_token
            # Validate the selected existing authority before any unit mutation.
            load_operator_token(MANAGED_OPERATOR_FILE)
        system = self.observe("system")
        if previous is None and system["LoadState"] == "loaded":
            require(mode == "none", "managed operator authority requires the owned user-service selection")
            self.check_system(system, selected=True)
            return self.verify()
        self.check_system(system, selected=False)
        desired = self.desired("user", http_auth=mode)
        self.manager(enable_linger=previous is None)
        user_before = self.observe("user")
        require(previous is not None or not self.running(user_before),
                "an unrecorded user cognition owner is already running")
        physical_directory(self.home, self.uid)
        directory = self.home
        for component in (".config", "systemd", "user"):
            directory /= component
            if not directory.exists() and not directory.is_symlink():
                directory.mkdir(mode=0o755)
            physical_directory(directory, self.uid)
            require(not directory.stat().st_mode & 0o022,
                    "user unit directory is writable by another identity")
        if self.user_unit.exists() or self.user_unit.is_symlink():
            current = file_bytes(self.user_unit, self.uid)
            require(previous is not None or current == desired,
                    "refusing to replace an unrecorded user unit")
        old_unit = file_bytes(self.user_unit, self.uid) if previous is not None else None
        old_marker = file_bytes(self.marker, self.uid) if previous is not None else None
        try:
            atomic_write(self.user_unit, desired)
            self.command(["daemon-reload"])
            self.command(["enable", UNIT])
            observed = self.observe("user")
            require(observed.get("LoadState") == "loaded" and
                    observed.get("FragmentPath") == str(self.user_unit) and
                    observed.get("NeedDaemonReload") == "no" and observed.get("DropInPaths") == "" and
                    observed.get("UnitFileState") == "enabled",
                    "user unit enablement did not persist")
            # This selection also inhibits the system envelope. Publish it only after
            # installation and enablement, never as a promise of future setup.
            payload = {"schema": SCHEMA, "scope": "user", "uid": self.uid, "user": USER,
                       "unit": UNIT, "unit_path": str(self.user_unit),
                       "unit_sha256": digest(desired), "persistent_manager": True,
                       "cold_boot_proven": False}
            if mode == HTTP_AUTH_MANAGED_OPERATOR:
                payload["http_auth"] = mode
            payload["receipt_sha256"] = digest(canonical(payload))
            atomic_write(self.marker, canonical(payload))
            return self.verify()
        except Exception:
            if old_unit is not None and old_marker is not None:
                # Keep a previously selected owner repairable after an interrupted
                # envelope update. A failed first install has no selection marker.
                atomic_write(self.user_unit, old_unit)
                atomic_write(self.marker, old_marker)
                self.command(["daemon-reload"])
            raise

    def restart(self, expected_sha: str, output: Path, *, observe_activation=None,
                stable_activation=None, health=None) -> dict:
        require(re.fullmatch(r"[0-9a-f]{40}", expected_sha) is not None,
                "restart requires the exact expected repository commit")
        if observe_activation is None:
            sys.path.insert(0, str(self.repository / "tools"))
            from sources import stockfish_corpus_acceptance as acceptance
            observe_activation = acceptance.observe_activation
            stable_activation = acceptance.stable_activation
        activation_started = time.monotonic()
        before = observe_activation(expected_sha, output / "activation-before.json")
        activation_before_seconds = time.monotonic() - activation_started
        selected = self.verify()
        restart_started = time.monotonic()
        if selected["scope"] == "user":
            self.command(["restart", UNIT], timeout=90)
        else:
            result = self.execute(["/usr/bin/sudo", "-n", "/usr/bin/systemctl", "restart", UNIT],
                                  self.environment(), 90)
            require(result.returncode == 0, "fixed system cognition restart failed")
        command_seconds = time.monotonic() - restart_started
        deadline = time.monotonic() + 90
        last = "service has not become healthy"
        while time.monotonic() < deadline:
            current = self.verify()
            require(current["scope"] == selected["scope"], "service owner changed during restart")
            try:
                require(current["unit"].get("ActiveState") == "active",
                        "selected cognition service is not active")
                pid = int(current["unit"].get("MainPID", "0"))
                require(pid > 0, "selected cognition service has no process")
                observation = (health(pid, self.uid, before) if health is not None else
                               observe_health(pid, self.uid, before, deadline=deadline))
                break
            except (ServiceError, OSError, http.client.HTTPException, ValueError) as error:
                last = str(error)
                time.sleep(0.5)
        else:
            raise ServiceError("cognition readiness deadline: " + last)
        health_seconds = time.monotonic() - restart_started
        activation_after_started = time.monotonic()
        after = observe_activation(expected_sha, output / "activation-after.json")
        activation_after_seconds = time.monotonic() - activation_after_started
        require(stable_activation(before) == stable_activation(after),
                "activated product changed during cognition restart")
        final = self.verify()
        require(final["scope"] == selected["scope"] and
                final["unit"].get("MainPID") == str(pid) and
                final["unit"].get("ActiveState") == "active",
                "cognition process changed during final activation readback")
        return {**final, "package_id": before["package_id"],
                "repository_commit": expected_sha, "health": observation,
                "activated_package_verified": True,
                "timings_seconds": {"activation_before": activation_before_seconds,
                                    "restart_command": command_seconds,
                                    "restart_to_verified_health": health_seconds,
                                    "activation_after": activation_after_seconds}}


def process_identity(pid: int, uid: int, executable: Path) -> dict:
    process = Path("/proc") / str(pid)
    require(process.stat().st_uid == uid, "cognition process has another uid")
    command = [os.fsdecode(part) for part in (process / "cmdline").read_bytes().split(b"\0") if part]
    image = (process / "exe").resolve(strict=True)
    require(executable.is_file() and not executable.is_symlink(),
            "cognition executable is absent or linked")
    with executable.open("rb") as stream:
        header = stream.read(4)
    if header.startswith(b"\x7fELF"):
        require(image == executable, "cognition process loaded another executable")
        kind = "native-executable"
    else:
        # The shipped Python supervisor is selected at argv[1] after its real
        # Python interpreter. An unrelated argument naming the package is not
        # executable identity. Do not accept shell wrappers or interpreter flags.
        interpreters = {Path(sys.executable).resolve(), Path("/usr/bin/python3").resolve()}
        require(header.startswith(b"#!") and image in interpreters and len(command) >= 2 and
                re.fullmatch(r"python(?:3(?:[.][0-9]+)?)?", Path(command[0]).name) is not None and
                Path(command[1]).is_absolute() and Path(command[1]).resolve() == executable,
                "cognition process selects another executable or script position")
        kind = "python-script"
    fields = (process / "stat").read_text().rsplit(")", 1)[1].split()
    return {"pid": pid, "uid": uid, "parent_pid": int(fields[1]),
            "start_ticks": int(fields[19]), "cgroup": (process / "cgroup").read_text(),
            "kind": kind, "loaded_image": str(image),
            "loaded_image_sha256": file_digest(image),
            "executable": str(executable), "executable_sha256": file_digest(executable)}


def observe_health(pid: int, uid: int, activation: dict, *,
                   socket_path: Path = Path("/opt/laplace/runtime/laplace-cognition.sock"),
                   deadline: float | None = None) -> dict:
    # Reuse the HTTP owner\'s per-receive absolute deadline, including status and
    # header parsing. A socket inactivity timeout alone permits endless trickling.
    sys.path.insert(0, str(ROOT / "tools"))
    from delivery.product_gateway_live_proof import ReadinessResponseSocket, readiness_timeout
    deadline = min(deadline if deadline is not None else float("inf"), time.monotonic() + 5)
    release = Path(activation["release"])
    wrapper_path = release / "bin/laplace-cognition-service"
    core_path = release / "bin/laplace-cognition-core-service"
    wrapper = process_identity(pid, uid, wrapper_path)
    transport = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    transport.settimeout(readiness_timeout(deadline))
    try:
        transport.connect(str(socket_path))
        peer_pid, peer_uid, _ = struct.unpack("3i", transport.getsockopt(
            socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i")))
        require(peer_uid == uid, "cognition socket belongs to another identity")
        core = process_identity(peer_pid, uid, core_path)
        require(core["parent_pid"] == pid and core["start_ticks"] >= wrapper["start_ticks"] and
                core["cgroup"] == wrapper["cgroup"],
                "cognition socket is not the selected wrapper's current core child")
        transport.sendall(b"GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        with http.client.HTTPResponse(ReadinessResponseSocket(transport, deadline)) as response:
            response.begin()
            body = response.read(65537)
            require(len(body) <= 65536 and response.status == 200, "cognition health is unavailable")
        readiness_timeout(deadline)
        value = json.loads(body)
        require(value.get("status") == "ready" and
                value.get("package_id") == activation["package_id"] and
                value.get("unicode_present") is True and value.get("highway_present") is True,
                "cognition health does not bind the activated native floor")
        require(process_identity(pid, uid, wrapper_path) == wrapper and
                process_identity(peer_pid, uid, core_path) == core,
                "cognition wrapper or core identity changed during health readback")
        return {"wrapper": wrapper, "core": core,
                "socket_peer_pid": peer_pid, "socket_peer_uid": peer_uid, "response": value}
    except (RuntimeError, OSError, http.client.HTTPException) as error:
        if time.monotonic() >= deadline:
            raise ServiceError("cognition HTTP health deadline expired") from error
        raise
    finally:
        transport.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("ensure", "verify", "restart"))
    parser.add_argument("--expected-uid", type=int)
    parser.add_argument("--expected-sha")
    parser.add_argument("--http-auth", choices=(HTTP_AUTH_MANAGED_OPERATOR,),
                        help="explicitly select the existing managed operator authority for this user service")
    parser.add_argument("--output", type=Path, required=True,
                        help="new evidence directory outside the source checkout")
    args = parser.parse_args()
    if args.http_auth is not None and args.action != "ensure":
        parser.error("--http-auth is selected only by ensure")
    report = {"schema": RESULT_SCHEMA, "action": args.action, "status": "failed",
              "cold_boot_proven": False, "cognition_acceptance_proven": False}
    output_created = False
    try:
        account = pwd.getpwnam(USER)
        require(os.geteuid() == account.pw_uid and account.pw_uid != 0 and
                (args.expected_uid is None or args.expected_uid == account.pw_uid),
                "service owner must run as the declared non-root laplace-runner")
        output = args.output.absolute()
        require(ROOT != output.resolve() and ROOT not in output.resolve().parents,
                "service evidence must be outside the source checkout")
        physical_directory(output.parent)
        output.mkdir(mode=0o750)
        output_created = True
        owner = Owner(uid=account.pw_uid, home=Path(account.pw_dir))
        physical_directory(owner.marker.parent)
        lock_path = owner.marker.parent / ".cognition-service.lock"
        descriptor = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        with os.fdopen(descriptor, "a") as lock:
            require(os.fstat(lock.fileno()).st_uid == account.pw_uid,
                    "service lifecycle lock belongs to another identity")
            deadline = time.monotonic() + 30
            while True:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    require(time.monotonic() < deadline, "service lifecycle lock deadline")
                    time.sleep(0.1)
            if args.action == "restart":
                require(args.expected_sha is not None, "restart requires --expected-sha")
                observation = owner.restart(args.expected_sha, output)
            elif args.action == "ensure":
                observation = owner.ensure(http_auth=args.http_auth)
            else:
                observation = owner.verify()
        report.update(status="passed", uid=account.pw_uid, observation=observation)
        atomic_write(output / "result.json", canonical(report))
        print(json.dumps(report, sort_keys=True))
        return 0
    except Exception as error:
        report["error"] = str(error)
        if output_created and output.is_dir() and not output.is_symlink():
            atomic_write(output / "result.json", canonical(report))
        print(json.dumps(report, sort_keys=True), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
