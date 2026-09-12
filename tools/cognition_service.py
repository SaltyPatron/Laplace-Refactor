#!/usr/bin/env python3
"""Local execution service for the installed persistent Laplace cognition route."""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler
import json
import os
from pathlib import Path
import re
import socketserver
import stat
import subprocess
import sys
from threading import BoundedSemaphore
from typing import Any

HEX256 = re.compile(r"^[0-9a-f]{64}$")
HEXBYTES = re.compile(r"^[0-9a-f]*$")
ACTIVE_RELATIONS = (
    "container", "constituent", "predecessor", "successor", "cooccur", "semantic"
)
RELATIONS = set(ACTIVE_RELATIONS)
ACTIVE_RELATION_MASK = 63
ACTIVE = Path("/opt/laplace/current")
RECEIPT_ROOT = Path("/opt/laplace/receipts/postgresql/refactor")
POSTGRES_SOCKET_DIRECTORY = Path("/opt/laplace/runtime/postgresql/refactor")
DEFAULT_SERVICE_SOCKET = Path("/opt/laplace/runtime/laplace-cognition.sock")
DATABASE_PORT = 55433
DATABASE = "laplace_refactor"
DATABASE_ROLE = "laplace_admin"
MAXIMUM_PROMPT_BYTES = 1024 * 1024
MAXIMUM_CHECKPOINT_BYTES = 8 * 1024 * 1024
MAXIMUM_REQUEST_BYTES = 20 * 1024 * 1024
MAXIMUM_CONCURRENT_REQUESTS = 4


class CognitionError(RuntimeError):
    pass


class SessionConflict(CognitionError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CognitionError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise CognitionError(f"{path} is not a JSON object")
    return value


def selected_product() -> tuple[str, Path]:
    try:
        release = ACTIVE.resolve(strict=True)
    except OSError as error:
        raise CognitionError(f"active product is unavailable: {error}") from error
    package_id = release.name
    if HEX256.fullmatch(package_id) is None:
        raise CognitionError("active product target is not content-addressed")
    return package_id, release


def bytea_literal(value: str) -> str:
    if HEXBYTES.fullmatch(value) is None or len(value) % 2:
        raise CognitionError("invalid hexadecimal byte string")
    return f"decode('{value}','hex')"


def parse_bytea(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.startswith("\\x"):
        raise CognitionError(f"{field} is not PostgreSQL bytea JSON")
    encoded = value[2:].lower()
    if HEXBYTES.fullmatch(encoded) is None or len(encoded) % 2:
        raise CognitionError(f"{field} contains invalid bytea hex")
    return encoded


def parse_digest_bytea(value: Any, field: str) -> str:
    encoded = parse_bytea(value, field)
    if HEX256.fullmatch(encoded) is None:
        raise CognitionError(f"{field} is not a 256-bit digest")
    return encoded


def run_psql(release: Path, sql: str, timeout: int = 300) -> dict[str, Any]:
    psql = release / "pgsql-18/bin/psql"
    if not psql.is_file() or psql.is_symlink():
        raise CognitionError(f"packaged psql is unavailable: {psql}")
    command = [
        str(psql),
        "--host", str(POSTGRES_SOCKET_DIRECTORY),
        "--port", str(DATABASE_PORT),
        "--username", DATABASE_ROLE,
        "--dbname", DATABASE,
        "--no-psqlrc",
        "--set", "ON_ERROR_STOP=1",
        "--quiet",
        "--tuples-only",
        "--no-align",
    ]
    completed = subprocess.run(
        command,
        cwd="/",
        input=sql,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
        timeout=timeout,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise CognitionError(f"persistent cognition SQL failed: {detail[-4000:]}")
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise CognitionError(f"persistent cognition returned {len(lines)} result rows")
    try:
        value = json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise CognitionError("persistent cognition did not return JSON") from error
    if not isinstance(value, dict):
        raise CognitionError("persistent cognition result is not an object")
    return value


def compile_firmware(release: Path, relations: list[str] | None) -> dict[str, Any]:
    executable = release / "bin/laplace_cognition_firmware_compile"
    if not executable.is_file() or executable.is_symlink():
        raise CognitionError("installed cognition firmware compiler is unavailable")
    if relations is None:
        command = [str(executable), "--auto"]
    else:
        if not relations or any(relation not in RELATIONS for relation in relations):
            raise CognitionError("relations must use installed Laplace relation families")
        command = [str(executable), "--relations", ",".join(relations)]
    completed = subprocess.run(
        command,
        cwd="/",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise CognitionError(f"firmware compilation failed: {detail[-2000:]}")
    try:
        value = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise CognitionError("firmware compiler returned invalid JSON") from error
    if not isinstance(value, dict):
        raise CognitionError("firmware compiler returned an invalid image")
    program_id = value.get("program_id")
    image_hex = value.get("image_hex")
    common_valid = (
        value.get("schema") == "laplace.cognition-firmware-image/v1"
        and isinstance(program_id, str)
        and HEX256.fullmatch(program_id) is not None
        and isinstance(image_hex, str)
        and HEXBYTES.fullmatch(image_hex) is not None
        and len(image_hex) % 2 == 0
        and value.get("image_bytes") == len(image_hex) // 2
        and value.get("image_bytes", 0) > 0
    )
    if not common_valid:
        raise CognitionError("firmware compiler returned an invalid image")
    if relations is None:
        if (
            value.get("mode") != "auto"
            or value.get("program") != "adaptive-active-relation-microcycle"
            or value.get("step_count") != 2
            or value.get("relation_mask") != ACTIVE_RELATION_MASK
            or value.get("eligible_relations") != list(ACTIVE_RELATIONS)
        ):
            raise CognitionError("firmware compiler returned an invalid automatic program")
    elif (
        value.get("mode") != "explicit"
        or value.get("program") != "relation-chain-exact-witnessed-output"
        or value.get("relations") != relations
        or value.get("step_count") != len(relations) + 1
    ):
        raise CognitionError("firmware compiler returned an invalid explicit program")
    return value


def active_runtime_state(release: Path) -> dict[str, Any]:
    return run_psql(
        release,
        """
SELECT pg_catalog.json_build_object(
  'unicode_present', u.active_present,
  'perfcache_epoch', pg_catalog.encode(u.epoch_fingerprint,'hex'),
  'highway_present', h.active_present,
  'numeric_epoch', pg_catalog.encode(h.activation_epoch_fingerprint,'hex')
)::text
FROM laplace.perfcache_active_control AS u
CROSS JOIN laplace.highway_registry_active_control AS h
WHERE u.singleton AND h.singleton;
""",
        timeout=30,
    )


def load_identities(package_id: str) -> dict[str, Any]:
    activation = load_json(
        RECEIPT_ROOT / "cluster-activation" / package_id / "unicode-product-activation.json"
    )
    if activation.get("phase") != "product-activated" or activation.get("package_id") != package_id:
        raise CognitionError("active Unicode evidence does not belong to the selected package")
    request_fingerprint = activation.get("request_fingerprint")
    if not isinstance(request_fingerprint, str) or HEX256.fullmatch(request_fingerprint) is None:
        raise CognitionError("Unicode request fingerprint is invalid")
    identities = load_json(RECEIPT_ROOT / "unicode" / request_fingerprint / "identities.json")
    required = (
        "source_epoch", "identity_epoch", "geometry_epoch", "evidence_epoch",
        "dependency_epoch", "database_epoch", "package_epoch", "numeric_epoch",
        "authority_fingerprint", "request_fingerprint",
    )
    for name in required:
        value = identities.get(name)
        if not isinstance(value, str) or HEX256.fullmatch(value) is None:
            raise CognitionError(f"active cognition identity is invalid: {name}")
    return identities


def parse_session(value: Any) -> dict[str, Any] | None:
    if value is None:
        return None
    if not isinstance(value, dict):
        raise CognitionError("session must be an object")
    session_fingerprint = value.get("session_fingerprint")
    discourse_id = value.get("discourse_id")
    turn_ordinal = value.get("turn_ordinal")
    if not isinstance(session_fingerprint, str) or HEX256.fullmatch(session_fingerprint) is None:
        raise CognitionError("session_fingerprint must be a 256-bit hexadecimal digest")
    if not isinstance(discourse_id, str) or HEX256.fullmatch(discourse_id) is None:
        raise CognitionError("discourse_id must be a 256-bit hexadecimal digest")
    if isinstance(turn_ordinal, bool) or not isinstance(turn_ordinal, int) or turn_ordinal < 0:
        raise CognitionError("turn_ordinal must be a nonnegative integer")
    result: dict[str, Any] = {
        "session_fingerprint": session_fingerprint,
        "discourse_id": discourse_id,
        "turn_ordinal": turn_ordinal,
    }
    previous_fields = (
        "previous_checkpoint_hex",
        "previous_checkpoint_fingerprint",
        "previous_package_id",
        "previous_program_id",
    )
    if turn_ordinal == 0:
        if any(value.get(name) not in (None, "") for name in previous_fields):
            raise CognitionError("first session turn cannot supply a previous checkpoint")
        return result
    for name in previous_fields:
        previous = value.get(name)
        if not isinstance(previous, str) or not previous:
            raise CognitionError(f"continued session requires {name}")
        result[name] = previous.lower()
    checkpoint = result["previous_checkpoint_hex"]
    if HEXBYTES.fullmatch(checkpoint) is None or len(checkpoint) % 2 or not checkpoint:
        raise CognitionError("previous_checkpoint_hex is invalid")
    if len(checkpoint) // 2 > MAXIMUM_CHECKPOINT_BYTES:
        raise CognitionError("previous checkpoint exceeds the 8 MiB product limit")
    for name in (
        "previous_checkpoint_fingerprint", "previous_package_id", "previous_program_id"
    ):
        if HEX256.fullmatch(result[name]) is None:
            raise CognitionError(f"{name} must be a 256-bit hexadecimal digest")
    return result


def context_sql(
    identities: dict[str, Any],
    program_id: str,
    perfcache_epoch: str,
    numeric_epoch: str,
) -> str:
    epochs = [
        identities["source_epoch"], identities["identity_epoch"],
        identities["geometry_epoch"], identities["evidence_epoch"],
        program_id, identities["dependency_epoch"], identities["database_epoch"],
        perfcache_epoch, numeric_epoch, identities["package_epoch"],
    ]
    if any(HEX256.fullmatch(str(value)) is None for value in epochs):
        raise CognitionError("execution epoch is invalid")
    return (
        "ROW(ARRAY[" + ",".join(bytea_literal(str(value)) for value in epochs) + "]::bytea[],"
        + bytea_literal(identities["authority_fingerprint"])
        + ",1073741824::bigint,6,2,1023::bigint,1::smallint,6::smallint,1)"
        "::laplace.execution_context"
    )


def prompt_scope_sql(
    identities: dict[str, Any], session: dict[str, Any] | None
) -> str:
    session_fingerprint = (
        session["session_fingerprint"] if session is not None else identities["database_epoch"]
    )
    discourse_id = session["discourse_id"] if session is not None else identities["dependency_epoch"]
    turn_ordinal = session["turn_ordinal"] if session is not None else 0
    turn_flags = 1 if turn_ordinal > 0 else 0
    values = [
        identities["source_epoch"], identities["identity_epoch"],
        identities["geometry_epoch"], identities["geometry_epoch"],
        identities["request_fingerprint"], identities["package_epoch"],
        session_fingerprint, discourse_id,
        identities["source_epoch"], identities["numeric_epoch"],
    ]
    return (
        "ROW(" + ",".join(bytea_literal(value) for value in values)
        + f",{turn_ordinal}::numeric,{turn_flags},1::numeric,1048576::numeric)"
        "::laplace.cognition_prompt_scope"
    )


def product_request_sql(
    identities: dict[str, Any],
    program_id: str,
    relation_count: int,
    session: dict[str, Any] | None,
) -> str:
    max_depth = max(16, relation_count * 4)
    max_layers = max(32, relation_count * 4 + 4)
    search = (
        f"ROW(4096::numeric,16384::numeric,4096::numeric,2048::numeric,268435456::numeric,"
        f"4096::numeric,4096::numeric,4096::numeric,{max_depth},1,128,1024)"
        "::laplace.cognition_observation_search_budget"
    )
    forward = (
        f"ROW({max_layers}::numeric,4096::numeric,4096::numeric,4096::numeric,4096::numeric,"
        "268435456::numeric,4096::numeric,4096::numeric,256,256)"
        "::laplace.cognition_observation_forward_limits"
    )
    materialization = (
        "ROW(4096::numeric,65536::numeric,1048576::numeric,64,1)"
        "::laplace.cognition_firmware_materialization_limits"
    )
    continued = session is not None and session["turn_ordinal"] > 0
    previous_fingerprint = (
        session["previous_checkpoint_fingerprint"] if continued else "00" * 32
    )
    previous_present = "true" if continued else "false"
    return (
        "ROW(" + bytea_literal(program_id) + ","
        + bytea_literal(identities["evidence_epoch"]) + ","
        + bytea_literal(identities["request_fingerprint"]) + ","
        + bytea_literal(previous_fingerprint) + "," + previous_present + ","
        + search + "," + forward + "," + materialization
        + ",1048576::numeric,8388608::numeric,0,1)::laplace.cognition_firmware_product_request"
    )


def execute_cognition(
    prompt: str,
    relations: list[str] | None,
    session: dict[str, Any] | None,
) -> dict[str, Any]:
    if not prompt:
        raise CognitionError("prompt must be non-empty UTF-8 text")
    encoded_prompt = prompt.encode("utf-8")
    if len(encoded_prompt) > MAXIMUM_PROMPT_BYTES:
        raise CognitionError("prompt exceeds the 1 MiB product limit")

    package_id, release = selected_product()
    firmware = compile_firmware(release, relations)
    program_id = firmware["program_id"]
    continued = session is not None and session["turn_ordinal"] > 0
    if continued:
        if session["previous_package_id"] != package_id:
            raise SessionConflict(
                "selected product changed since the previous turn; reset the local session"
            )
        if session["previous_program_id"] != program_id:
            raise SessionConflict(
                "selected cognition program changed since the previous turn; reset the local session"
            )

    identities = load_identities(package_id)
    runtime = active_runtime_state(release)
    perfcache_epoch = runtime.get("perfcache_epoch")
    numeric_epoch = runtime.get("numeric_epoch")
    if (
        runtime.get("unicode_present") is not True
        or runtime.get("highway_present") is not True
        or not isinstance(perfcache_epoch, str)
        or HEX256.fullmatch(perfcache_epoch) is None
        or not isinstance(numeric_epoch, str)
        or HEX256.fullmatch(numeric_epoch) is None
    ):
        raise CognitionError("active Unicode/Highway runtime epochs are unavailable")

    previous_checkpoint = session["previous_checkpoint_hex"] if continued else ""
    relation_count = len(relations) if relations is not None else len(ACTIVE_RELATIONS)
    common_arguments = (
        f"  {context_sql(identities, program_id, perfcache_epoch, numeric_epoch)},\n"
        f"  pg_catalog.convert_from({bytea_literal(encoded_prompt.hex())}, 'UTF8'),\n"
        f"  {prompt_scope_sql(identities, session)},\n"
        f"  {product_request_sql(identities, program_id, relation_count, session)},\n"
        f"  {bytea_literal(previous_checkpoint)},\n"
        "  1073741824::bigint\n"
    )
    if relations is None:
        execution_route = "native-conversation"
        sql = f"""
SELECT pg_catalog.row_to_json(result)::text
FROM laplace.cognition_conversation_execute_product(
{common_arguments}) AS result;
"""
    else:
        execution_route = "explicit-firmware"
        sql = f"""
SELECT pg_catalog.row_to_json(result)::text
FROM laplace.cognition_firmware_execute_product(
  {context_sql(identities, program_id, perfcache_epoch, numeric_epoch)},
  {bytea_literal(firmware['image_hex'])},
  pg_catalog.convert_from({bytea_literal(encoded_prompt.hex())}, 'UTF8'),
  {prompt_scope_sql(identities, session)},
  {product_request_sql(identities, program_id, relation_count, session)},
  {bytea_literal(previous_checkpoint)},
  1073741824::bigint
) AS result;
"""
    result = run_psql(release, sql)
    status = result.get("status")
    if isinstance(status, bool) or not isinstance(status, int):
        raise CognitionError("persistent cognition returned an invalid native status")
    output_hex = ""
    output_utf8: str | None = None
    if result.get("output") is not None:
        output_hex = parse_bytea(result.get("output"), "output")
        output_bytes = bytes.fromhex(output_hex)
        try:
            output_utf8 = output_bytes.decode("utf-8")
        except UnicodeDecodeError:
            output_utf8 = None

    checkpoint_hex = ""
    checkpoint_fingerprint: str | None = None
    if status == 0:
        checkpoint_hex = parse_bytea(result.get("checkpoint"), "checkpoint")
        if not checkpoint_hex:
            raise CognitionError("successful cognition returned an empty checkpoint")
        if len(checkpoint_hex) // 2 > MAXIMUM_CHECKPOINT_BYTES:
            raise CognitionError("successful cognition checkpoint exceeds the product limit")
        checkpoint_fingerprint = parse_digest_bytea(
            result.get("next_checkpoint_fingerprint"), "next_checkpoint_fingerprint"
        )

    execution = dict(result)
    execution.pop("checkpoint", None)
    mode = firmware["mode"]
    response: dict[str, Any] = {
        "schema": "laplace.cognition-response/v1",
        "package_id": package_id,
        "mode": mode,
        "execution_route": execution_route,
        "relations": relations if relations is not None else [],
        "eligible_relations": (
            firmware.get("eligible_relations", []) if mode == "auto" else relations
        ),
        "relation_mask": firmware.get("relation_mask"),
        "program_id": program_id,
        "turn_ordinal": session["turn_ordinal"] if session is not None else 0,
        "continued": continued,
        "status": status,
        "output_utf8": output_utf8,
        "output_hex": output_hex,
        "checkpoint_hex": checkpoint_hex,
        "next_checkpoint_fingerprint": checkpoint_fingerprint,
        "execution": execution,
    }
    return response


class ThreadingUnixServer(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True
    allow_reuse_address = False


class Handler(BaseHTTPRequestHandler):
    semaphore = BoundedSemaphore(MAXIMUM_CONCURRENT_REQUESTS)

    def _json(self, status: int, value: dict[str, Any]) -> None:
        body = (json.dumps(value, ensure_ascii=False, sort_keys=True) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path != "/health":
            self._json(404, {"error": "route not found"})
            return
        try:
            package_id, release = selected_product()
            state = active_runtime_state(release)
            self._json(200, {
                "status": "ready", "package_id": package_id, "database": DATABASE,
                "unicode_present": state.get("unicode_present"),
                "highway_present": state.get("highway_present"),
            })
        except Exception as error:
            self._json(503, {"status": "unavailable", "error": str(error)})

    def do_POST(self) -> None:
        if self.path != "/v1/cognition":
            self._json(404, {"error": "route not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._json(400, {"error": "invalid Content-Length"})
            return
        if length <= 0 or length > MAXIMUM_REQUEST_BYTES:
            self._json(413, {"error": "request body exceeds the cognition transport limit"})
            return
        try:
            payload = json.loads(self.rfile.read(length))
        except Exception:
            self._json(400, {"error": "request body must be JSON"})
            return
        if not isinstance(payload, dict):
            self._json(400, {"error": "request body must be an object"})
            return
        prompt = payload.get("prompt")
        if not isinstance(prompt, str) or not prompt:
            self._json(400, {"error": "prompt must be non-empty text"})
            return
        relations_value = payload.get("relations") if "relations" in payload else None
        relations: list[str] | None
        if relations_value is None:
            relations = None
        elif (
            not isinstance(relations_value, list) or not relations_value
            or any(not isinstance(item, str) or item not in RELATIONS for item in relations_value)
        ):
            self._json(400, {"error": "relations contain an unsupported relation family"})
            return
        else:
            relations = relations_value
        try:
            session = parse_session(payload.get("session"))
        except CognitionError as error:
            self._json(400, {"error": str(error)})
            return
        if not self.semaphore.acquire(blocking=False):
            self._json(503, {"error": "cognition concurrency limit reached"})
            return
        try:
            result = execute_cognition(prompt, relations, session)
            self._json(200, result)
        except SessionConflict as error:
            self._json(409, {"error": str(error)})
        except CognitionError as error:
            self._json(500, {"error": str(error)})
        except Exception as error:
            self._json(500, {"error": f"unexpected cognition failure: {error}"})
        finally:
            self.semaphore.release()

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"laplace-cognition-service: {fmt % args}", file=sys.stderr)


def prepare_socket(path: Path) -> None:
    if not path.is_absolute() or path.parent != Path("/opt/laplace/runtime"):
        raise CognitionError("service socket must be an exact child of /opt/laplace/runtime")
    if not path.parent.is_dir() or path.parent.is_symlink():
        raise CognitionError("Laplace runtime directory is absent or unsafe")
    if path.exists() or path.is_symlink():
        mode = path.lstat().st_mode
        if not stat.S_ISSOCK(mode):
            raise CognitionError(f"refusing to replace non-socket runtime object: {path}")
        path.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", type=Path, default=DEFAULT_SERVICE_SOCKET)
    args = parser.parse_args()
    prepare_socket(args.socket)
    server = ThreadingUnixServer(str(args.socket), Handler)
    os.chmod(args.socket, 0o666)
    print(f"laplace-cognition-service listening on {args.socket}", file=sys.stderr)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        try:
            if args.socket.exists() and stat.S_ISSOCK(args.socket.lstat().st_mode):
                args.socket.unlink()
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CognitionError as error:
        print(f"laplace-cognition-service: {error}", file=sys.stderr)
        raise SystemExit(1) from error
