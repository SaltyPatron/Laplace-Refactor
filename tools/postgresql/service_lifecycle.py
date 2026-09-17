#!/usr/bin/env python3
"""Select the existing system PostgreSQL owner for the canonical persistent instance.

Disposable and relocated proof instances retain pg_ctl. This owner never installs
units, changes grants, initializes a database, or claims that a warm restart proves
a cold boot. Invoke the explicit convergence CLI through tools/host/run-exclusive.sh.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import signal
import stat
import subprocess
import sys

ROOT = (Path(__file__).resolve().parent.parent if Path(__file__).parent.name == "controllers"
        else Path(__file__).resolve().parents[2])
UNIT = "laplace-refactor-postgresql.service"
PROVIDER = "systemd-system"
UNIT_BLOB = "7c11d67f63396cd49cdce38f5f5e35f812b81238"
SCHEMA = "laplace.postgresql-service-convergence/v1"
CANONICAL = {
    "id": "refactor", "service": UNIT, "os_user": "laplace-runner", "os_group": "laplace-runner",
    "database": "laplace_refactor", "admin_role": "laplace_admin", "app_role": "laplace_app",
    "port": 55433, "socket_directory": "/opt/laplace/runtime/postgresql/refactor",
    "data_directory": "/opt/laplace/pgdata/refactor/data", "wal_directory": "/var/lib/pgwal/refactor",
    "temp_directory": "/pgtemp/refactor", "perfcache_directory": "/opt/laplace/pgdata/refactor/perfcache",
    "config_directory": "/etc/laplace/instances/refactor",
    "log_directory": "/var/log/laplace/postgresql/refactor",
    "receipt_directory": "/opt/laplace/receipts/postgresql/refactor",
}
PROPERTIES = ("LoadState", "ActiveState", "SubState", "UnitFileState", "FragmentPath",
              "NeedDaemonReload", "User", "Group", "MainPID", "ControlGroup", "DropInPaths")


class ServiceError(RuntimeError):
    pass


def require(value, message):
    if not value:
        raise ServiceError(message)


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def identity(value):
    return hashlib.sha256(canonical({key: item for key, item in value.items()
                                     if key != "receipt_sha256"})).hexdigest()


def persistent(plan):
    instance = plan.get("instance", {})
    # Fixture observation paths are logical. The activation owner separately
    # rejects a typed fixture when its actual mutation root is the live "/";
    # the real command adapter rejects all typed fixture provenance.
    if plan.get("collision_observation_source") == "laplace_typed_fixture":
        return False
    protected = ("data_directory", "wal_directory", "socket_directory",
                 "config_directory", "receipt_directory")
    touches = any(instance.get(key) == CANONICAL[key] for key in protected)
    if not touches:
        return False
    require(instance == CANONICAL and plan.get("active_link") == "/opt/laplace/current" and
            plan.get("runtime_link") == "/opt/laplace/runtime/refactor" and
            plan.get("package_root") == "/opt/laplace/releases/" + str(plan.get("package_id")) and
            plan.get("collision_observation_root") == "/",
            "partially relocated cluster overlaps persistent service state")
    return True


def read_file(path, *, uid=None, maximum=65536):
    metadata = path.lstat()
    require(stat.S_ISREG(metadata.st_mode) and metadata.st_size <= maximum,
            "service evidence is not a bounded physical regular file")
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        opened = os.fstat(descriptor)
        require((metadata.st_dev, metadata.st_ino) == (opened.st_dev, opened.st_ino),
                "service evidence changed before open")
        require(uid is None or opened.st_uid == uid, "service evidence has another owner")
        data = os.read(descriptor, maximum + 1)
        after = os.fstat(descriptor)
        require(len(data) == opened.st_size and len(data) <= maximum and
                (opened.st_size, opened.st_mtime_ns, opened.st_ctime_ns) ==
                (after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                "service evidence changed during read")
        return data, opened
    finally:
        os.close(descriptor)


class Owner:
    def __init__(self, ctl, *, unit_path=Path("/etc/systemd/system") / UNIT,
                 proc=Path("/proc"), execute=None, root_uid=0, root_gid=0):
        self.ctl = ctl
        self.unit_path = unit_path
        self.proc = proc
        self.execute = execute or ctl.execute_activation_command
        self.root_uid = root_uid
        self.root_gid = root_gid

    def command(self, arguments, timeout=30):
        return subprocess.run(arguments, cwd="/", env=self.ctl.activation_environment(),
                              capture_output=True, text=True, check=False, timeout=timeout)

    def verify(self):
        raw, metadata = read_file(self.unit_path, uid=self.root_uid)
        require(metadata.st_gid == self.root_gid and stat.S_IMODE(metadata.st_mode) == 0o644,
                "PostgreSQL unit metadata differs")
        framed = b"blob " + str(len(raw)).encode() + b"\0" + raw
        require(hashlib.sha1(framed).hexdigest() == UNIT_BLOB,
                "PostgreSQL unit differs from the canonical static envelope")
        observed = self.command(["/usr/bin/systemctl", "show", UNIT, "--no-pager",
                                 "--property=" + ",".join(PROPERTIES)])
        require(observed.returncode == 0 and len(observed.stdout) <= 16384,
                "cannot observe the fixed PostgreSQL unit")
        fields = dict(line.split("=", 1) for line in observed.stdout.splitlines() if "=" in line)
        require(set(fields) == set(PROPERTIES) and
                fields["LoadState"] == "loaded" and fields["UnitFileState"] == "enabled" and
                fields["FragmentPath"] == str(self.unit_path) and fields["NeedDaemonReload"] == "no" and
                fields["DropInPaths"] == "" and fields["User"] == CANONICAL["os_user"] and
                fields["Group"] == CANONICAL["os_group"],
                "PostgreSQL unit is not the exact enabled loaded system owner")
        require(fields["MainPID"].isdigit(), "invalid system PostgreSQL PID")
        for action in ("start", "stop", "restart"):
            authority = self.command(["/usr/bin/sudo", "-n", "-l", "/usr/bin/systemctl",
                                      action, UNIT])
            require(authority.returncode == 0, "fixed PostgreSQL lifecycle authority is unavailable")
        return {"provider": PROVIDER, "unit": UNIT, "unit_sha256": hashlib.sha256(raw).hexdigest(),
                "boot_enabled": True, "cold_boot_proven": False, "properties": fields}

    def selection(self, plan):
        require(persistent(plan), "system PostgreSQL owner requires the canonical persistent plan")
        runtime = Path(plan["runtime_link"])
        require(runtime.is_symlink() and os.readlink(runtime) == "../releases/" + plan["package_id"] and
                runtime.resolve(strict=True) == Path(plan["package_root"]),
                "service runtime pointer differs from the selected package")
        for item in plan["files"]:
            path = Path(item["path"])
            require(path.is_file() and not path.is_symlink() and
                    self.ctl.sha256_file(path) == item["sha256"],
                    "service configuration differs from the exact cluster plan")

    def process(self, plan, state):
        pid = int(state["properties"]["MainPID"])
        require(pid > 0 and state["properties"]["ActiveState"] == "active",
                "system owner does not supervise an active PostgreSQL process")
        data, _ = read_file(Path(plan["instance"]["data_directory"]) / "postmaster.pid",
                            uid=pwd.getpwnam(CANONICAL["os_user"]).pw_uid)
        rows = data.decode("utf-8").splitlines()
        require(len(rows) >= 2 and rows[0] == str(pid) and
                rows[1] == plan["instance"]["data_directory"],
                "system MainPID differs from the selected postmaster")
        process = self.proc / str(pid)
        birth = (process / "stat").read_text().rsplit(")", 1)[1].split()[19]
        require(process.stat().st_uid == pwd.getpwnam(CANONICAL["os_user"]).pw_uid,
                "system postmaster has another uid")
        expected = Path(plan["package_root"]) / "pgsql-18/bin/postgres"
        require((process / "exe").resolve(strict=True) == expected,
                "system postmaster executes another package")
        groups = (process / "cgroup").read_text().splitlines()
        control_group = state["properties"]["ControlGroup"]
        require(control_group == "/system.slice/" + UNIT and
                any(line.split(":", 2)[-1] == control_group for line in groups),
                "postmaster is outside its exact system unit cgroup")
        require((process / "stat").read_text().rsplit(")", 1)[1].split()[19] == birth and
                read_file(Path(plan["instance"]["data_directory"]) / "postmaster.pid")[0] == data,
                "postmaster identity changed during service observation")
        return {"postmaster_pid": pid, "start_ticks": int(birth), "control_group": control_group}

    def observe(self, plan, loaded):
        self.selection(plan)
        state = self.verify()
        process = self.process(plan, state)
        require(process["postmaster_pid"] == loaded["postmaster_pid"],
                "system owner and authenticated SQL postmaster differ")
        return {**state, **process}

    def action(self, plan, label, action, timeout):
        require(action in ("start", "stop"), "unsupported recurring PostgreSQL service action")
        self.selection(plan)
        state = self.verify()
        status = self.command(self.ctl._pg_ctl_command(plan, "status"))
        require(status.returncode in (0, 3), "cannot establish selected PostgreSQL status")
        if status.returncode == 0:
            # Never adopt or stop an unmanaged process merely because its PGDATA matches.
            self.process(plan, state)
        else:
            allowed_states = ("inactive", "failed", "activating", "deactivating") if action == "stop" else ("inactive", "failed")
            require(int(state["properties"]["MainPID"]) == 0 and
                    state["properties"]["ActiveState"] in allowed_states,
                    "stopped PostgreSQL and system owner disagree")
        receipt = self.execute(label, ["/usr/bin/sudo", "-n", "/usr/bin/systemctl", action, UNIT],
                               min(timeout, 300))
        receipt = {**receipt, "provider": PROVIDER, "unit_sha256": state["unit_sha256"]}
        if action == "stop":
            stopped = self.verify()
            require(int(stopped["properties"]["MainPID"]) == 0 and
                    stopped["properties"]["ActiveState"] in ("inactive", "failed"),
                    "system owner did not stop the selected postmaster")
            self.ctl._stopped_live(plan)
        return receipt


def selection_path(plan):
    return Path(plan["instance"]["receipt_directory"]) / "postgresql-service-owner.json"


def observe_selected(ctl, plan, loaded):
    if not persistent(plan):
        return None
    path = selection_path(plan)
    if not path.exists() and not path.is_symlink():
        return None
    raw, metadata = read_file(path, uid=os.geteuid())
    require(not metadata.st_mode & 0o022, "service selection is writable by another identity")
    receipt = json.loads(raw)
    require(receipt.get("schema") == SCHEMA and receipt.get("status") == "passed" and
            receipt.get("receipt_sha256") == identity(receipt) and
            receipt.get("provider") == PROVIDER and receipt.get("cold_boot_proven") is False and
            receipt.get("instance") == CANONICAL and
            str(receipt.get("system_identifier")) == str(loaded["system_identifier"]),
            "retained PostgreSQL service convergence identity differs")
    observed = Owner(ctl).observe(plan, loaded)
    require(observed["unit_sha256"] == receipt["service"]["unit_sha256"],
            "system unit differs from its durable convergence")
    return {"convergence_receipt_sha256": receipt["receipt_sha256"], "service": observed,
            "historical_package_id": receipt["package_id"],
            "historical_repository_commit": receipt["repository_commit"],
            "historical_cluster_activation_receipt_sha256": receipt["cluster_activation_receipt_sha256"]}


def same_database(ctl, plan, contract, expected, *, managed):
    loaded = ctl.observe_loaded_live(plan, contract, Path("/"))
    ctl.verify_loaded(plan, contract, loaded)
    for key in ("system_identifier", "loaded_objects", "config_files"):
        require(loaded[key] == expected[key], "service transition changed " + key)
    if managed:
        Owner(ctl).observe(plan, loaded)
    return loaded


def converge(expected_sha, output, *, acceptance=None):
    # Import the established whole-product observer instead of reinterpreting an
    # old activation receipt or confusing today's checkout with installed source.
    sys.path.insert(0, str(ROOT / "tools"))
    if acceptance is None:
        from sources import stockfish_corpus_acceptance as acceptance
    ctl = acceptance.runner.clusterctl
    ctl._require_runner()
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    report = {"schema": SCHEMA, "status": "running", "provider": PROVIDER,
              "cold_boot_proven": False, "command_receipts": []}
    before = None
    owner = Owner(ctl)
    old_unmanaged_stopped = False
    system_mutation_started = False
    publication_started = False

    def retain():
        ctl.write_json(output / "result.json", report)

    def run(label, function):
        report["phase"] = label
        retain()
        value = function()
        report["command_receipts"].append(value)
        retain()
        return value

    retain()
    try:
        before = acceptance.observe_activation(expected_sha, output / "activation-before.json")
        plan = before["cluster_plan"]
        contract = ctl.load_json(ROOT / "contracts/postgresql-cluster.json")
        require(persistent(plan), "service convergence requires the live canonical instance")
        owner.selection(plan)
        state = owner.verify()
        report.update(package_id=before["package_id"], repository_commit=expected_sha,
                      plan_sha256=plan["plan_sha256"], instance=plan["instance"],
                      system_identifier=before["loaded"]["system_identifier"],
                      cluster_activation_receipt_sha256=
                          before["cluster_activation"]["activation_receipt_sha256"],
                      original_aggregate_receipt_sha256s=sorted(
                          item["document"]["result_sha256"] for item in before["runner_receipts"]),
                      service_before=state)
        retain()
        prior = observe_selected(ctl, plan, before["loaded"])
        if prior is not None:
            report.update(status="already-converged", previous=prior,
                          service=owner.observe(plan, before["loaded"]),
                          warm_restart_performed=False)
            after = acceptance.observe_activation(expected_sha, output / "activation-after.json")
            require(acceptance.stable_activation(before) == acceptance.stable_activation(after),
                    "activated product changed during service replay")
            report["receipt_sha256"] = identity(report)
            retain()
            return report

        initially_managed = int(state["properties"]["MainPID"]) != 0
        if initially_managed:
            owner.observe(plan, before["loaded"])
        else:
            require(state["properties"]["ActiveState"] in ("inactive", "failed"),
                    "system PostgreSQL owner is in a transition")
            same_database(ctl, plan, contract, before["loaded"], managed=False)
            # Inhibit the fixed unit before the only allowed direct-pg_ctl handoff.
            run("stop-system-owner-before-handoff", lambda: ctl.execute_activation_command(
                "stop-system-owner-before-handoff",
                ["/usr/bin/sudo", "-n", "/usr/bin/systemctl", "stop", UNIT], 180))
            state = owner.verify()
            require(int(state["properties"]["MainPID"]) == 0 and
                    state["properties"]["ActiveState"] in ("inactive", "failed"),
                    "system owner did not become inactive before handoff")
            refreshed = same_database(ctl, plan, contract, before["loaded"], managed=False)
            require(refreshed["postmaster_pid"] == before["loaded"]["postmaster_pid"],
                    "unmanaged postmaster changed before handoff")
            owner.selection(plan)
            # Mark the attempt first: even a command timeout can have stopped it.
            old_unmanaged_stopped = True
            run("stop-authenticated-unmanaged-postmaster", lambda: ctl.execute_activation_command(
                "stop-authenticated-unmanaged-postmaster", ctl._pg_ctl_command(plan, "stop"), 300))
            ctl._stopped_live(plan)

        system_mutation_started = True
        run("start-system-postmaster", lambda: owner.action(
            plan, "start-system-postmaster", "start", 300))
        run("system-postmaster-readiness", lambda: ctl.await_postgresql_ready(
            "system-postmaster-readiness", plan["commands"]["probe_readiness"], 300))
        initial = same_database(ctl, plan, contract, before["loaded"], managed=True)
        ctl.write_json(output / "loaded-system-initial.json", initial)
        run("stop-system-for-warm-restart", lambda: owner.action(
            plan, "stop-system-for-warm-restart", "stop", 300))
        run("start-system-after-warm-restart", lambda: owner.action(
            plan, "start-system-after-warm-restart", "start", 300))
        run("system-warm-restart-readiness", lambda: ctl.await_postgresql_ready(
            "system-warm-restart-readiness", plan["commands"]["probe_readiness"], 300))
        restarted = same_database(ctl, plan, contract, before["loaded"], managed=True)
        require(restarted["postmaster_pid"] != initial["postmaster_pid"],
                "system warm restart retained the original postmaster")
        ctl.write_json(output / "loaded-system-restarted.json", restarted)
        after = acceptance.observe_activation(expected_sha, output / "activation-after.json")
        require(acceptance.stable_activation(before) == acceptance.stable_activation(after),
                "activated product changed during system convergence")
        service = owner.observe(plan, after["loaded"])
        report.update(status="passed", phase="system-owner-converged", service=service,
                      boot_enabled=True, warm_restart_performed=True,
                      loaded_initial_observation_sha256=initial["observation_sha256"],
                      loaded_restart_observation_sha256=restarted["observation_sha256"],
                      historical_activation_receipts_rewritten=False)
        report["receipt_sha256"] = identity(report)
        # Publish the separate owner record only after both real identity checks.
        target = selection_path(plan)
        require(not target.exists() and not target.is_symlink(),
                "another PostgreSQL service convergence was published")
        publication_started = True
        ctl.write_json(target, report)
        retained, _ = read_file(target, uid=os.geteuid())
        require(json.loads(retained) == report, "durable service convergence readback differs")
        retain()
        return report
    except BaseException as error:
        report.update(status="failed", error_type=type(error).__name__, error=str(error)[:2000],
                      successful_selection_published=False)
        if publication_started:
            # Publication can succeed even when later evidence I/O fails. Never
            # revert the live owner beneath a possibly published successful record.
            report["successful_selection_published"] = "readback-required"
        if not publication_started and before is not None and (old_unmanaged_stopped or system_mutation_started):
            try:
                plan = before["cluster_plan"]
                owner.selection(plan)
                owner.verify()
                # Stop only the exact verified fixed envelope, then prove no
                # postmaster before restoring the owner that existed beforehand.
                report["rollback_commands"] = []
                stopped = ctl.execute_activation_command("stop-failed-system-handoff",
                    ["/usr/bin/sudo", "-n", "/usr/bin/systemctl", "stop", UNIT], 180)
                report["rollback_commands"].append(stopped)
                retain()
                ctl._stopped_live(plan)
                if old_unmanaged_stopped:
                    receipt = ctl.execute_activation_command(
                        "restore-authenticated-unmanaged-postmaster",
                        ctl._pg_ctl_command(plan, "start"), 300)
                else:
                    receipt = owner.action(plan, "restore-existing-system-postmaster", "start", 300)
                report["rollback_command"] = receipt
                report["rollback_commands"].append(receipt)
                retain()
                ctl.await_postgresql_ready("restored-postmaster-readiness",
                                           plan["commands"]["probe_readiness"], 300)
                restored = same_database(ctl, plan, contract, before["loaded"],
                                         managed=not old_unmanaged_stopped)
                ctl.write_json(output / "loaded-rollback.json", restored)
                report["previous_owner_restored"] = True
            except BaseException as rollback:
                report.update(previous_owner_restored=False,
                              rollback_error_type=type(rollback).__name__,
                              rollback_error=str(rollback)[:2000])
        retain()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("converge",))
    parser.add_argument("--expected-sha", required=True)
    parser.add_argument("--output-directory", required=True, type=Path)
    args = parser.parse_args()
    previous_term = signal.getsignal(signal.SIGTERM)
    previous_int = signal.getsignal(signal.SIGINT)
    interruption_started = False

    def interrupted(signum, _frame):
        nonlocal interruption_started
        # Runner cancellation sends INT then TERM; timeout may also send TERM.
        # Only the first interruption enters bounded rollback.
        if interruption_started:
            return
        interruption_started = True
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        if signum == signal.SIGINT:
            raise KeyboardInterrupt("PostgreSQL service convergence interrupted by SIGINT")
        raise InterruptedError("PostgreSQL service convergence interrupted by SIGTERM")

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        result = converge(args.expected_sha, args.output_directory)
    finally:
        signal.signal(signal.SIGTERM, previous_term)
        signal.signal(signal.SIGINT, previous_int)
    print(json.dumps({key: result[key] for key in (
        "schema", "status", "provider", "package_id", "repository_commit",
        "system_identifier", "cold_boot_proven", "receipt_sha256")}, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print("postgresql service convergence: " + str(error), file=sys.stderr)
        raise SystemExit(1) from error
