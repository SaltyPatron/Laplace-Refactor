#!/usr/bin/env python3
"""Hosted pointer-to-member grammar experiment; no installed provider changes."""
import collections
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time
import traceback
import urllib.request

REPOSITORY = "SaltyPatron/Laplace-Refactor"
SOURCES = {
    "generator": ("tree-sitter/tree-sitter", "470813116b99578956e67abb7138e993833af67a", "f26d8ff7b7c569815c8118724fad05d473c01b7e"),
    "c": ("tree-sitter/tree-sitter-c", "7fa1be1b694b6e763686793d97da01f36a0e5c12", "5392349d60304fc2920be64ac9aa5410c0f8c385"),
    "cpp": ("tree-sitter/tree-sitter-cpp", "8b5b49eb196bec7040441bee33b2c9a4838d6967", "d5ed868917a07bcc8272f37e551d707e7a836f95"),
}
ASSETS = {
    "candidate.grammar.js": ("c0cc5faedb20c5f0925375eacb2f612501fcc51b", "3a47547ab927ff4262d7c760ca6df6f86e1c56012848867c17f24ba7e1360bd1"),
    "fixtures.json": ("35b958c279832720f390602d1f347bcf63b7da7f", "9b34692c98169799fdf932951ed26f65c1396f996bf9be6728c63fd0990a2723"),
    "probe.c": ("838be4faa7e3913b6918a9ae63df1b8cfb4824f3", "22d91697ce7944e98974f296e911b6f7ee86ee2ab0722cfd57e94096bfdaeb1e"),
}
EXPECTED_CPP_ARCHIVE = "a162edeecfebe736bc561d826dd6d694b78ac970f70a1d4b6dbf5f07429a768a"
EXPECTED_RULES = {"_for_statement_body", "operator_cast_declaration", "using_declaration"}
STOCKFISH_COMMIT = "edb0d9db6731067ec50ce619ff372b463bc4dd5d"
START = time.monotonic()
ROOT = Path(os.environ["QUALIFICATION_ROOT"]).resolve()
EVIDENCE = ROOT / "evidence"
WORK = ROOT / "work"
REPORT = {
    "schema": "laplace.hosted-cpp-pointer-member-experiment/v1",
    "status": "running",
    "scope": "scratch parser generation and syntax experiment; not installed canonical provider qualification",
    "source_proposal_manifest": "2eb692ea78e870f0d8e2a1e375f6443c310bc3f3",
    "canonical_lock_modified": False,
    "installed_provider_modified": False,
    "executable_semantics_verified": False,
    "stockfish_full_corpus_reparsed": False,
    "pointer_member_fix_included": True,
    "commands": [], "sources": {}, "variants": {}, "failures": [],
}
TOKEN = os.environ.pop("GH_TOKEN", "")
ENV = os.environ.copy()
for key in list(ENV):
    if key in {"GITHUB_TOKEN", "GH_TOKEN", "NODE_OPTIONS", "LD_PRELOAD", "LD_LIBRARY_PATH", "TREE_SITTER_ABI_VERSION", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
               "OBJC_INCLUDE_PATH", "LIBRARY_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX", "RUSTFLAGS", "CARGO_ENCODED_RUSTFLAGS"}:
        ENV.pop(key, None)
ENV.update({
    "NO_COLOR": "1", "TERM": "dumb", "CARGO_TARGET_DIR": str(WORK / "cargo-target"),
    "CARGO_HOME": str(WORK / "cargo-home"), "TMPDIR": str(ROOT / "tmp"),
    "TMP": str(ROOT / "tmp"), "TEMP": str(ROOT / "tmp"),
    "XDG_CACHE_HOME": str(WORK / "cache"), "XDG_CONFIG_HOME": str(WORK / "config"),
    "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": "/dev/null",
})

def digest(data):
    return hashlib.sha256(data).hexdigest()

def file_identity(path):
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": digest(data)}

def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".writing")
    tmp.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    tmp.replace(path)

def save():
    REPORT["elapsed_seconds"] = round(time.monotonic() - START, 3)
    write_json(EVIDENCE / "report.json", REPORT)

def notice(message):
    clean = str(message).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::notice title=Hosted grammar experiment::" + clean, flush=True)

def failure(message):
    REPORT["failures"].append(str(message))
    save()

def command(label, argv, cwd=WORK, timeout=600):
    remaining = 4500 - (time.monotonic() - START)
    if remaining <= 0:
        raise TimeoutError("75-minute experiment ceiling reached")
    number = len(REPORT["commands"]) + 1
    stem = "%03d-%s" % (number, label)
    out = EVIDENCE / "logs" / (stem + ".stdout")
    err = EVIDENCE / "logs" / (stem + ".stderr")
    out.parent.mkdir(parents=True, exist_ok=True)
    row = {"label": label, "argv": [str(x) for x in argv], "cwd": str(cwd),
           "stdout": str(out), "stderr": str(err), "timeout_seconds": min(timeout, remaining)}
    REPORT["commands"].append(row)
    save()
    begin = time.monotonic()
    try:
        with out.open("wb") as stdout, err.open("wb") as stderr:
            child = subprocess.Popen(row["argv"], cwd=cwd, env=ENV,
                                     stdout=stdout, stderr=stderr, start_new_session=True)
            try:
                row["returncode"] = child.wait(timeout=row["timeout_seconds"])
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                row["returncode"] = child.wait()
                row["timed_out"] = True
    except Exception as exc:
        row["exception"] = repr(exc)
        row["returncode"] = None
    row["elapsed_seconds"] = round(time.monotonic() - begin, 3)
    row["stdout_identity"] = file_identity(out)
    row["stderr_identity"] = file_identity(err)
    save()
    return row

def require_command(label, argv, cwd=WORK, timeout=600):
    row = command(label, argv, cwd, timeout)
    if row["returncode"] != 0:
        raise RuntimeError("%s failed; retained stdout/stderr: %s" % (label, row))
    return row

def output(row):
    return Path(row["stdout"]).read_text(errors="replace")

def fetch_bytes(url, authenticated=False):
    headers = {"User-Agent": "Laplace-hosted-grammar-experiment"}
    if authenticated:
        if not TOKEN:
            raise RuntimeError("missing repository read token for immutable prepared blobs")
        headers.update({"Authorization": "Bearer " + TOKEN,
                        "Accept": "application/vnd.github.raw+json",
                        "X-GitHub-Api-Version": "2022-11-28"})
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=90) as response:
        return response.read()

def asset(name, blob, sha):
    data = fetch_bytes("https://api.github.com/repos/%s/git/blobs/%s" % (REPOSITORY, blob), True)
    if hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() != blob or digest(data) != sha:
        raise RuntimeError("prepared asset identity mismatch: " + name)
    path = EVIDENCE / "inputs" / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path

def checkout(name, spec):
    repository, commit, tree = spec
    path = WORK / name
    path.mkdir()
    require_command(name + "-init", ["git", "init", "--quiet", path])
    require_command(name + "-remote", ["git", "-C", path, "remote", "add", "origin", "https://github.com/" + repository + ".git"])
    require_command(name + "-fetch", ["git", "-C", path, "-c", "protocol.version=2", "fetch", "--depth=1", "origin", commit], timeout=360)
    require_command(name + "-checkout", ["git", "-C", path, "checkout", "--detach", "--quiet", commit])
    actual_commit = output(require_command(name + "-commit", ["git", "-C", path, "rev-parse", "HEAD"])).strip()
    actual_tree = output(require_command(name + "-tree", ["git", "-C", path, "rev-parse", "HEAD^{tree}"])).strip()
    if (actual_commit, actual_tree) != (commit, tree):
        raise RuntimeError(name + " immutable source mismatch")
    require_command(name + "-fsck", ["git", "-C", path, "fsck", "--strict", "--no-reflogs"])
    if output(require_command(name + "-clean", ["git", "-C", path, "status", "--porcelain"])).strip():
        raise RuntimeError(name + " source is not clean")
    rows = require_command(name + "-tracked-blobs", ["git", "-C", path, "ls-tree", "-r", "-z", "HEAD"])
    checked = []
    for entry in Path(rows["stdout"]).read_bytes().split(b"\0"):
        if not entry:
            continue
        meta, raw_path = entry.split(b"\t", 1)
        mode, kind, expected_blob = meta.decode().split()
        relative = raw_path.decode()
        source = path / relative
        if kind != "blob":
            raise RuntimeError(name + " has an unhandled Git source entry: " + relative)
        data = os.readlink(source).encode() if mode == "120000" else source.read_bytes()
        observed = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if observed != expected_blob:
            raise RuntimeError(name + " tracked bytes differ: " + relative)
        checked.append({"path": relative, "mode": mode, "git_blob": observed, "sha256": digest(data), "bytes": len(data)})
    write_json(EVIDENCE / "sources" / (name + "-tracked-files.json"), checked)
    archive = EVIDENCE / "sources" / (name + ".tar")
    archive.parent.mkdir(parents=True, exist_ok=True)
    require_command(name + "-archive", ["git", "-C", path, "archive", "--format=tar", "--output=" + str(archive), commit])
    identity = file_identity(archive)
    if name == "cpp" and identity["sha256"] != EXPECTED_CPP_ARCHIVE:
        raise RuntimeError("C++ git archive differs from frozen canonical lock")
    REPORT["sources"][name] = {"repository": repository, "commit": commit, "tree": tree, "archive": identity}
    save()
    return path

def copy_grammar(pristine, name, inherited):
    path = WORK / name
    shutil.copytree(pristine, path, ignore=shutil.ignore_patterns(".git", "node_modules"))
    (path / "node_modules").mkdir()
    (path / "node_modules" / "tree-sitter-c").symlink_to(inherited, target_is_directory=True)
    return path

def generated_evidence(name, path):
    target = EVIDENCE / "generated" / name
    files = []
    for relative in ("grammar.js", "src/grammar.json", "src/node-types.json", "src/parser.c", "src/scanner.c",
                     "src/tree_sitter/parser.h", "src/tree_sitter/alloc.h", "src/tree_sitter/array.h"):
        source = path / relative
        if source.is_file():
            destination = target / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            files.append(file_identity(destination))
    REPORT["variants"][name]["generated_files"] = files
    save()

def walk(node):
    yield node
    for child in node["children"]:
        yield from walk(child)

def node_text(node, data):
    return data[node["start"]:node["end"]].decode("utf-8")

def typed(node, name):
    return [n for n in walk(node) if n["type"] == name]

def child_field(node, field):
    return next((n for n in node["children"] if n["field"] == field), None)

def syntactic_check(case_id, parsed, data):
    nodes = list(walk(parsed["root"]))
    errors = [n for n in nodes if n["error"] or n["missing"]]
    valid = not errors and not parsed["has_error"]
    checks = {"no_error_or_missing": valid}
    if case_id == "for-condition-declaration":
        loops = typed(parsed["root"], "for_statement")
        cond = child_field(loops[0], "condition") if len(loops) == 1 else None
        init = child_field(loops[0], "initializer") if len(loops) == 1 else None
        update = child_field(loops[0], "update") if len(loops) == 1 else None
        checks["condition_declaration"] = bool(cond and cond["type"] == "declaration" and
                                              "dest" in node_text(cond, data) and typed(cond, "call_expression"))
        checks["distinct_initializer_update"] = bool(init and update and cond and
                                                     init["end"] <= cond["start"] and cond["end"] <= update["start"] and
                                                     "Square s = sq" in node_text(init, data) and "s += d" in node_text(update, data))
    elif case_id == "deleted-conversion-operator":
        clauses = typed(parsed["root"], "delete_method_clause")
        checks["one_conversion"] = len(typed(parsed["root"], "operator_cast")) == 1
        checks["one_delete_clause_semicolon"] = len(clauses) == 1 and node_text(clauses[0], data).strip() == "= delete;"
    elif case_id == "using-pack-expansion":
        using = typed(parsed["root"], "using_declaration")
        checks["ellipsis_in_using"] = len(using) == 1 and any(
            n["type"] == "..." and node_text(n, data) == "..." for n in using[0]["children"])
        checks["qualified_operator"] = bool(using and typed(using[0], "qualified_identifier") and typed(using[0], "operator_name"))
    elif case_id == "existing-range-loop":
        checks["range_loop_preserved"] = len(typed(parsed["root"], "for_range_loop")) == 1
    return {"valid": valid, "error_node_count": sum(n["error"] for n in nodes),
            "missing_node_count": sum(n["missing"] for n in nodes), "checks": checks}

def pointer_shape(node, data):
    kind = node["type"]
    text = node_text(node, data).strip()
    named = [n for n in node["children"] if n["named"]]
    if kind == "identifier":
        return text
    if kind == "parenthesized_expression" and len(named) == 1:
        value = pointer_shape(named[0], data)
        if named[0]["type"] == "conditional_expression":
            return "parenthesized_conditional"
        if value.startswith("pointer_member("):
            return "parenthesized_pointer_member"
        return "parenthesized(" + value + ")"
    if kind in ("binary_expression", "assignment_expression"):
        left, right = child_field(node, "left"), child_field(node, "right")
        operator = child_field(node, "operator")
        if left and right and operator:
            op = node_text(operator, data)
            role = {"*": "multiply", "->*": "pointer_member", ".*": "pointer_member", "=": "assignment"}.get(op, op)
            return "%s(%s,%s)" % (role, pointer_shape(left, data), pointer_shape(right, data))
    if kind == "field_expression":
        argument, field = child_field(node, "argument"), child_field(node, "field")
        operator = child_field(node, "operator")
        if argument and field and operator and node_text(operator, data) in ("->*", ".*"):
            return "pointer_member(%s,%s)" % (pointer_shape(argument, data), node_text(field, data))
    if kind == "cast_expression":
        type_node, value = child_field(node, "type"), child_field(node, "value")
        if type_node and value:
            return "cast(%s,%s)" % ("".join(node_text(type_node, data).split()), pointer_shape(value, data))
    if kind == "pointer_expression":
        operator, argument = child_field(node, "operator"), child_field(node, "argument")
        if operator and argument and node_text(operator, data) == "*":
            return "dereference(%s)" % pointer_shape(argument, data)
    if kind == "call_expression":
        function, arguments = child_field(node, "function"), child_field(node, "arguments")
        if function and arguments:
            return "call(%s,[%s])" % (pointer_shape(function, data), ",".join(
                pointer_shape(n, data) for n in arguments["children"] if n["named"]))
    if kind == "number_literal":
        return text
    return "%s{%s}" % (kind, text)

def corpus(cli, name, path):
    row = command(name + "-upstream-corpus", [cli, "test", "--grammar-path", path, "--json-summary", "--config-path", EVIDENCE / "inputs/cli-config.json"], cwd=path, timeout=600)
    summary = None
    text = output(row)
    # CLI may emit compiler notices before its single JSON summary.
    for index, char in enumerate(text):
        if char == "{":
            try:
                value, _ = json.JSONDecoder().raw_decode(text[index:])
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict) and "parse_results" in value:
                summary = value
                break
    counts = collections.Counter()
    def count(entries):
        for entry in entries:
            if "children" in entry:
                count(entry["children"])
            else:
                counts[str(entry["outcome"])] += 1
    if summary is not None:
        count(summary["parse_results"])
        write_json(EVIDENCE / "corpora" / (name + ".json"), summary)
    result = {"command_returncode": row["returncode"], "parse_outcomes": dict(counts),
              "summary_present": summary is not None, "update_requested": False,
              "selection": ("unfiltered upstream corpus with exact owned pointer-member golden" if name == "pointer-candidate" else "unfiltered unchanged upstream test directory")}
    # Upstream explicitly marked skipped/platform cases remain visible and are not passes.
    result["passed"] = row["returncode"] == 0 and counts["Passed"] > 0 and not counts["Failed"] and not counts["Updated"]
    if not result["passed"]:
        failure(name + " upstream tests failed or produced no authenticated test summary")
        print("POINTER_CORPUS_FAILURE " + json.dumps({"variant": name, "parse_failures": summary.get("parse_failures", []) if summary else None, "stderr": Path(row["stderr"]).read_text(errors="replace")[:12000]}), flush=True)
    return result

def compile_probe(name, path, generator, probe):
    destination = EVIDENCE / "binaries" / name
    destination.mkdir(parents=True, exist_ok=True)
    executable = destination / "parse-cst"
    row = command(name + "-compile-cst-probe", [
        "cc", "-std=c11", "-O2", "-D_DEFAULT_SOURCE",
        "-I" + str(generator / "lib/include"), "-I" + str(generator / "lib/src"),
        "-I" + str(path / "src"), probe, generator / "lib/src/lib.c",
        path / "src/parser.c", path / "src/scanner.c", "-o", executable, "-pthread"
    ], timeout=480)
    if row["returncode"] != 0:
        failure(name + " exact-source C runtime/parser/probe compilation failed")
        return None
    REPORT["variants"][name]["probe_binary"] = file_identity(executable)
    save()
    return executable

def parse_cases(name, executable, cases):
    results = {}
    for case in cases:
        case_id = case["id"]
        data = case["input"].encode()
        path = EVIDENCE / "case-inputs" / (case_id + ".cpp")
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists() and path.read_bytes() != data:
            raise RuntimeError("case identifier collision")
        path.write_bytes(data)
        row = command(name + "-" + case_id, [executable, path], timeout=20)
        if row["returncode"] != 0:
            results[case_id] = {"execution_failed": True, "command_returncode": row["returncode"]}
            failure(name + "/" + case_id + " CST execution failed")
            continue
        parsed = json.loads(output(row))
        checks = syntactic_check(case_id, parsed, data)
        item = {"input_sha256": digest(data), "result": checks, "cst": parsed}
        if case["role"] == "pointer-control":
            statements = typed(parsed["root"], "expression_statement")
            expressions = [n for n in statements[0]["children"] if n["named"]] if len(statements) == 1 else []
            actual_shape = pointer_shape(expressions[0], data) if len(expressions) == 1 else "unresolved"
            item["expected_future_shape"] = case["shape"]
            item["observed_shape"] = actual_shape
            item["matches_future_shape"] = checks["valid"] and actual_shape == case["shape"]
            item["qualification_role"] = ("candidate exact syntax and precedence control" if name == "pointer-candidate"
                                          else "baseline observation; not an acceptance of unresolved syntax")
        write_json(EVIDENCE / "csts" / name / (case_id + ".json"), item)
        results[case_id] = item
    return results


def authenticated_cli(generator):
    import io
    import zipfile
    repository = "https://api.github.com/repos/" + REPOSITORY
    metadata = json.loads(fetch_bytes(repository + "/actions/artifacts/10480207378", True))
    expected_sha = "043f838112100a2889eb28c29dea0de1f8a3bb619a45f6f79bdd273f9ece2cfb"
    if (metadata["id"] != 10480207378 or metadata["expired"] or
            metadata["size_in_bytes"] != 14413749 or metadata["digest"] != "sha256:" + expected_sha or
            metadata["workflow_run"]["id"] != 35181390710):
        raise RuntimeError("retained exact generator artifact identity changed")
    request = urllib.request.Request(repository + "/actions/artifacts/10480207378/zip",
                                    headers={"Accept": "application/vnd.github+json",
                                             "User-Agent": "Laplace-pointer-grammar-experiment"})
    request.add_unredirected_header("Authorization", "Bearer " + TOKEN)
    with urllib.request.urlopen(request, timeout=90) as response:
        data = response.read(14413750)
    if len(data) != 14413749 or digest(data) != expected_sha:
        raise RuntimeError("retained exact generator ZIP bytes differ")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        prior = json.loads(archive.read("report.json"))
        if (prior["generator"]["version_output"].split()[1] != "0.26.5" or
                prior["sources"]["generator"]["commit"] != SOURCES["generator"][1]):
            raise RuntimeError("generator source or version differs")
        body = archive.read("binaries/generator/tree-sitter")
        old_identity = prior["generator"]["binary"]
        if digest(body) != old_identity["sha256"] or len(body) != old_identity["bytes"]:
            raise RuntimeError("retained CLI differs from its actual compilation receipt")
        if archive.read("inputs/generator.Cargo.lock") != (generator / "Cargo.lock").read_bytes():
            raise RuntimeError("generator Cargo.lock differs from exact current source")
    cli = WORK / "tree-sitter"
    cli.write_bytes(body)
    cli.chmod(0o755)
    version = output(require_command("reused-generator-version", [cli, "--version"])).strip()
    if version != prior["generator"]["version_output"]:
        raise RuntimeError("actual reused CLI version differs")
    REPORT["generator"] = {"binary": file_identity(cli), "actual_version": version,
        "retained_producer_run": 35181390710, "artifact": metadata,
        "zip_bytes": len(data), "zip_sha256": expected_sha, "source_commit": SOURCES["generator"][1],
        "reuse": "Identical previously built CLI; this experiment does not rebuild or reinstall it"}
    return cli


def pointer_candidate():
    blob = "ad4d5359e64b6c60501a15cebd90e425af58935a"
    data = fetch_bytes("https://api.github.com/repos/%s/git/blobs/%s" % (REPOSITORY, blob), True)
    if hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() != blob:
        raise RuntimeError("pointer grammar Git object identity differs")
    path = EVIDENCE / "inputs" / "pointer.grammar.js"
    path.write_bytes(data)
    REPORT["pointer_grammar_input"] = {"git_blob": blob, **file_identity(path)}
    return path


def pointer_golden(path):
    relative = "test/corpus/declarations.txt"
    old_blob = "273afad947371be38fbb635a90dbdd90a1ceceb5"
    new_blob = "410e7b96fad67983a1f0c656960aa15c45ae32b3"
    target = path / relative
    old = target.read_bytes()
    assert hashlib.sha1(b"blob " + str(len(old)).encode() + b"\0" + old).hexdigest() == old_blob
    data = fetch_bytes("https://api.github.com/repos/%s/git/blobs/%s" % (REPOSITORY, new_blob), True)
    assert hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() == new_blob
    before, after = old.splitlines(keepends=True), data.splitlines(keepends=True)
    assert len(before) == len(after)
    changed = [(index + 1, first, second) for index, (first, second) in enumerate(zip(before, after)) if first != second]
    assert changed == [
        (2234, b"            (field_expression\n", b"            (binary_expression\n"),
        (2236, b"              (field_identifier)))\n", b"              (identifier)))\n"),
    ]
    assert b"(obj.*member_function_ptr)(42);" in old
    assert b"Pointers in qualified identifiers and field expressions" in old
    target.write_bytes(data)
    retained = EVIDENCE / "inputs" / "pointer-corpus-declarations.txt"
    retained.write_bytes(data)
    REPORT["owned_corpus_delta"] = {
        "path": relative, "official_blob": old_blob, "owned_blob": new_blob,
        "changed_lines": [row[0] for row in changed], "source_bytes_unchanged": True,
        "reason": "Represent the existing .* expression as binary pointer-to-member syntax, matching ->*; only this expected CST changes",
        "file": file_identity(retained),
    }


def authentic_member_shape(parsed, data):
    members = [n for n in typed(parsed["root"], "binary_expression")
               if child_field(n, "operator") and node_text(child_field(n, "operator"), data) == "->*"]
    if len(members) != 1:
        return False
    member = members[0]
    left, right = child_field(member, "left"), child_field(member, "right")
    if not (left and right and left["type"] == "call_expression" and
            node_text(left, data) == "th->worker.get()" and node_text(right, data) == "member"):
        return False
    for node in typed(parsed["root"], "field_expression"):
        argument, field = child_field(node, "argument"), child_field(node, "field")
        if (argument and field and argument["type"] == "parenthesized_expression" and
                node_text(field, data) == "load" and
                argument["start"] < member["start"] and member["end"] < argument["end"]):
            return True
    return False


def main():
    if os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise RuntimeError("this experiment requires a GitHub-hosted runner")
    WORK.mkdir(parents=True, exist_ok=False)
    (ROOT / "tmp").mkdir(exist_ok=True)
    REPORT.update(source_proposal_manifest=None,
                  scope="Scratch parser generation, full C/C++ corpora with one explicit owned pointer-member CST golden, and exact CST controls; no provider activation or executable equivalence",
                  runner={"environment": os.environ.get("RUNNER_ENVIRONMENT"), "os": platform.platform(),
                          "run_id": os.environ.get("GITHUB_RUN_ID"), "workflow_sha": os.environ.get("GITHUB_SHA")})
    assets = {name: asset(name, *spec) for name, spec in ASSETS.items()}
    candidate = pointer_candidate()
    fixture = json.loads(assets["fixtures.json"].read_text())
    roots = {name: checkout(name, spec) for name, spec in SOURCES.items()}
    cpp, inherited, generator = roots["cpp"], roots["c"], roots["generator"]
    cli = authenticated_cli(generator)
    config = EVIDENCE / "inputs/cli-config.json"
    config.write_text(json.dumps({"parser-directories": [str(WORK)]}) + "\n")
    files = {}
    for case in fixture["cases"]:
        binding = case["source_binding"]
        if binding["commit"] != STOCKFISH_COMMIT:
            raise RuntimeError("unexpected authentic Stockfish revision")
        key = binding["path"]
        if key not in files:
            files[key] = fetch_bytes("https://raw.githubusercontent.com/official-stockfish/Stockfish/" + STOCKFISH_COMMIT + "/" + key)
        data = files[key]
        excerpt = data[binding["byte_start"]:binding["byte_end"]]
        if (digest(data) != binding["file_sha256"] or digest(excerpt) != binding["excerpt_sha256"] or
                excerpt.decode() != binding["excerpt"] or
                digest(case["parser_input"].encode()) != case["parser_input_sha256"]):
            raise RuntimeError("authentic Stockfish bytes differ: " + key)
    REPORT["stockfish_source_files"] = {key: {"sha256": digest(data), "bytes": len(data)} for key, data in files.items()}
    variants = {
        "three-rule-baseline": copy_grammar(cpp, "cpp-three-rule-baseline", inherited),
        "pointer-candidate": copy_grammar(cpp, "cpp-pointer-candidate", inherited),
    }
    shutil.copy2(assets["candidate.grammar.js"], variants["three-rule-baseline"] / "grammar.js")
    shutil.copy2(candidate, variants["pointer-candidate"] / "grammar.js")
    pointer_golden(variants["pointer-candidate"])
    for name, path in variants.items():
        REPORT["variants"][name] = {"generation_status": "pending"}
        for relative in ("src/parser.c", "src/grammar.json", "src/node-types.json"):
            (path / relative).unlink()
        row = command(name + "-generate", [cli, "generate", "--abi", "15", "--js-runtime", "node", "--json-summary"],
                      cwd=path, timeout=600)
        REPORT["variants"][name]["generation_status"] = "passed" if row["returncode"] == 0 else "failed"
        generated_evidence(name, path)
        if row["returncode"] != 0:
            failure(name + " parser generation failed; exact conflicts retained")
            print("POINTER_GENERATION_FAILURE " + json.dumps({"variant": name,
                  "stdout": Path(row["stdout"]).read_text(errors="replace")[:20000],
                  "stderr": Path(row["stderr"]).read_text(errors="replace")[:20000]}), flush=True)
    if all(v["generation_status"] == "passed" for v in REPORT["variants"].values()):
        old = json.loads((variants["three-rule-baseline"] / "src/grammar.json").read_text())
        new = json.loads((variants["pointer-candidate"] / "src/grammar.json").read_text())
        changed = sorted(k for k in set(old["rules"]) | set(new["rules"]) if old["rules"].get(k) != new["rules"].get(k))
        properties_match = {k: v for k,v in old.items() if k != "rules"} == {k:v for k,v in new.items() if k != "rules"}
        REPORT["generated_delta"] = {"changed_rules": changed, "other_properties_match": properties_match}
        if set(changed) != {"binary_expression", "field_expression", "_assignment_left_expression", "_pointer_member_expression", "cast_expression"} or not properties_match:
            failure("generated grammar changed outside the five reviewed pointer/cast rules")
    cases = [{"id": c["id"], "input": c["parser_input"], "role": "authentic"} for c in fixture["cases"]]
    cases += [{"id": c["id"], "input": c["input"], "role": "control"} for c in fixture["controls"]]
    pointer_cases = fixture["pointer_member_followup_controls"] + [
        {"expression": "p->*pm + k", "shape": "+(pointer_member(p,pm),k)"},
        {"expression": "p->*(T*)q", "shape": "pointer_member(p,cast(T*,q))"},
        {"expression": "(T*)(p->*pm)", "shape": "cast(T*,parenthesized_pointer_member)"},
        {"expression": "p->*pm * k->*qm", "shape": "multiply(pointer_member(p,pm),pointer_member(k,qm))"},
        {"expression": "p->*pm = 7", "shape": "assignment(pointer_member(p,pm),7)"},
        {"expression": "object.*pm1.*pm2", "shape": "pointer_member(pointer_member(object,pm1),pm2)"},
        {"expression": "p->*pm->*qm", "shape": "pointer_member(pointer_member(p,pm),qm)"},
        {"expression": "p->*pm()", "shape": "pointer_member(p,call(pm,[]))"},
        {"expression": "(T*)(U*)p->*pm", "shape": "pointer_member(cast(T*,cast(U*,p)),pm)"},
        {"expression": "p->*(T*)(U*)q", "shape": "pointer_member(p,cast(T*,cast(U*,q)))"},
    ]
    cases += [{"id": "pointer-shape-%02d" % (i+1), "input": "void fixture() { " + c["expression"] + "; }\n",
               "role": "pointer-control", "shape": c["shape"]} for i,c in enumerate(pointer_cases)]
    cases += [
        {"id": "pointer-missing-right", "input": "void f() { p->*; }\n", "role": "negative"},
        {"id": "dot-member-missing-right", "input": "void f() { object.*; }\n", "role": "negative"},
        {"id": "pointer-missing-left", "input": "void f() { ->*pm; }\n", "role": "negative"},
    ]
    negatives = {"broken-condition", "deleted-conversion-missing-semicolon", "namespace-pack-not-admitted", "enum-pack-not-admitted",
                 "pointer-missing-right", "dot-member-missing-right", "pointer-missing-left"}
    parsed = {}
    REPORT["inherited_c_corpus"] = corpus(cli, "inherited-c", inherited)
    for name,path in variants.items():
        if REPORT["variants"][name]["generation_status"] != "passed":
            continue
        REPORT["variants"][name]["upstream_tests"] = corpus(cli, name, path)
        executable = compile_probe(name,path,generator,assets["probe.c"])
        if executable:
            parsed[name] = parse_cases(name,executable,cases)
        save()
    evaluations = []
    for name,results in parsed.items():
        for case in cases:
            item = results[case["id"]]
            result = item.get("result")
            if result is None:
                continue
            if case["id"] in negatives:
                expected = not result["valid"]
            elif case["role"] == "pointer-control":
                expected = item["matches_future_shape"] if name == "pointer-candidate" else None
            elif case["id"] == "pointer-to-member-operator":
                expected = (result["valid"] and authentic_member_shape(item["cst"],case["input"].encode())) if name == "pointer-candidate" else not result["valid"]
            else:
                expected = all(result["checks"].values())
            if expected is not None:
                evaluations.append({"variant":name,"case":case["id"],"passed":expected,
                                    "valid":result["valid"], "observed_shape": item.get("observed_shape")})
                if not expected:
                    failure(name + "/" + case["id"] + " failed the exact syntax or structural expectation")
    REPORT["evaluations"] = evaluations
    if all(n in parsed for n in variants):
        unchanged = [c for c in cases if c["role"] != "pointer-control" and c["id"] != "pointer-to-member-operator" and c["role"] != "negative"]
        REPORT["non_pointer_cst_parity"] = all(parsed["three-rule-baseline"][c["id"]]["cst"] == parsed["pointer-candidate"][c["id"]]["cst"] for c in unchanged)
        if not REPORT["non_pointer_cst_parity"]:
            failure("non-pointer regression CST changed")
    for name,path in roots.items():
        require_command(name + "-source-unchanged",["git","-C",path,"diff","--exit-code","HEAD","--"])
    REPORT["status"] = "failed" if REPORT["failures"] else "passed-hosted-syntax-experiment-only"
    save()
    print("POINTER_EXPERIMENT_RESULT " + json.dumps({"status":REPORT["status"], "failures":REPORT["failures"],
                                                  "evaluations":evaluations,"variants":{k:{"generation":v["generation_status"],"upstream":v.get("upstream_tests")} for k,v in REPORT["variants"].items()}}),flush=True)
    return int(bool(REPORT["failures"]))

if __name__ == "__main__":
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    try:
        code = main()
    except BaseException as exc:
        REPORT["status"] = "failed"
        REPORT["fatal_exception"] = repr(exc)
        (EVIDENCE / "exception.txt").write_text(traceback.format_exc())
        save()
        notice("Failed; partial evidence retained: " + repr(exc))
        code = 1
    finally:
        inventory = []
        for path in sorted(EVIDENCE.rglob("*")):
            if path.is_file() and path.name != "evidence-inventory.json":
                identity = file_identity(path)
                identity["path"] = str(path.relative_to(EVIDENCE))
                inventory.append(identity)
        write_json(EVIDENCE / "evidence-inventory.json", inventory)
    sys.exit(code)
