#!/usr/bin/env python3
"""Local execution service for the installed persistent Laplace cognition route."""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import subprocess
import sys
from threading import BoundedSemaphore
from typing import Any

HEX128 = re.compile(r"^[0-9a-f]{32}$")
HEX256 = re.compile(r"^[0-9a-f]{64}$")
HEXBYTES = re.compile(r"^[0-9a-f]*$")
RELATIONS = {"container", "constituent", "predecessor", "successor", "cooccur", "semantic"}
ACTIVE = Path("/opt/laplace/current")
RECEIPT_ROOT = Path("/opt/laplace/receipts/postgresql/refactor")
SOCKET_DIRECTORY = Path("/opt/laplace/runtime/postgresql/refactor")
DATABASE_PORT = 55433
DATABASE = "laplace_refactor"
DATABASE_ROLE = "laplace_admin"
MAXIMUM_REQUEST_BYTES = 1024 * 1024
MAXIMUM_CONCURRENT_REQUESTS = 4


class CognitionError(RuntimeError):
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


def run_psql(release: Path, sql: str, timeout: int = 300) -> dict[str, Any]:
    psql = release / "pgsql-18/bin/psql"
    if not psql.is_file() or psql.is_symlink():
        raise CognitionError(f"packaged psql is unavailable: {psql}")
    command = [
        str(psql),
        "--host", str(SOCKET_DIRECTORY),
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


def compile_firmware(release: Path, relations: list[str]) -> dict[str, Any]:
    if not relations or any(relation not in RELATIONS for relation in relations):
        raise CognitionError("relations must use installed Laplace relation families")
    executable = release / "bin/laplace_cognition_firmware_compile"
    if not executable.is_file() or executable.is_symlink():
        raise CognitionError("installed cognition firmware compiler is unavailable")
    completed = subprocess.run(
        [str(executable), "--relations", ",".join(relations)],
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
    program_id = value.get("program_id")
    image_hex = value.get("image_hex")
    if (
        not isinstance(value, dict)
        or value.get("schema") != "laplace.cognition-firmware-image/v1"
        or value.get("relations") != relations
        or not isinstance(program_id, str)
        or HEX256.fullmatch(program_id) is None
        or not isinstance(image_hex, str)
        or HEXBYTES.fullmatch(image_hex) is None
        or len(image_hex) % 2
        or value.get("image_bytes") != len(image_hex) // 2
    ):
        raise CognitionError("firmware compiler returned an invalid image")
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


def load_identities(package_id: str) -> tuple[dict[str, Any], dict[str, Any]]:
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
    return activation, identities


def context_sql(
    identities: dict[str, Any],
    program_id: str,
    perfcache_epoch: str,
    numeric_epoch: str,
) -> str:
    epochs = [
        identities["source_epoch"],
        identities["identity_epoch"],
        identities["geometry_epoch"],
        identities["evidence_epoch"],
        program_id,
        identities["dependency_epoch"],
        identities["database_epoch"],
        perfcache_epoch,
        numeric_epoch,
        identities["package_epoch"],
    ]
    if any(HEX256.fullmatch(str(value)) is None for value in epochs):
        raise CognitionError("execution epoch is invalid")
    return (
        "ROW(ARRAY[" + ",".join(bytea_literal(str(value)) for value in epochs) + "]::bytea[],"
        + bytea_literal(identities["authority_fingerprint"])
        + ",1073741824::bigint,6,2,1023::bigint,1::smallint,6::smallint,1)"
        "::laplace.execution_context"
    )


def prompt_scope_sql(identities: dict[str, Any]) -> str:
    values = [
        identities["source_epoch"],
        identities["identity_epoch"],
        identities["geometry_epoch"],
        identities["geometry_epoch"],
        identities["request_fingerprint"],
        identities["package_epoch"],
        identities["database_epoch"],
        identities["dependency_epoch"],
        identities["source_epoch"],
        identities["numeric_epoch"],
    ]
    return (
        "ROW(" + ",".join(bytea_literal(value) for value in values)
        + ",0::numeric,0,1::numeric,1048576::numeric)::laplace.cognition_prompt_scope"
    )


def product_request_sql(identities: dict[str, Any], program_id: str, relation_count: int) -> str:
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
    return (
        "ROW("
        + bytea_literal(program_id) + ","
        + bytea_literal(identities["evidence_epoch"]) + ","
        + bytea_literal(identities["request_fingerprint"]) + ","
        + bytea_literal("00" * 32) + ",false,"
        + search + "," + forward + "," + materialization
        + ",1048576::numeric,8388608::numeric,0,1)::laplace.cognition_firmware_product_request"
    )


def execute_cognition(prompt: str, relations: list[str]) -> dict[str, Any]:
    if not isinstance(prompt, str) or not prompt:
        raise CognitionError("prompt must be non-empty UTF-8 text")
    encoded_prompt = prompt.encode("utf-8")
    if len(encoded_prompt) > MAXIMUM_REQUEST_BYTES:
        raise CognitionError("prompt exceeds the 1 MiB service request limit")

    package_id, release = selected_product()
    firmware = compile_firmware(release, relations)
    _, identities = load_identities(package_id)
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

    sql = f"""
SELECT pg_catalog.row_to_json(result)::text
FROM laplace.cognition_firmware_execute_product(
  {context_sql(identities, firmware['program_id'], perfcache_epoch, numeric_epoch)},
  {bytea_literal(firmware['image_hex'])},
  pg_catalog.convert_from({bytea_literal(encoded_prompt.hex())}, 'UTF8'),
  {prompt_scope_sql(identities)},
  {product_request_sql(identities, firmware['program_id'], len(relations))},
  decode('','hex'),
  1073741824::bigint
) AS result;
"""
    result = run_psql(release, sql)
    output_hex = parse_bytea(result.get("output"), "output")
    output_bytes = bytes.fromhex(output_hex)
    try:
        output_utf8: str | None = output_bytes.decode("utf-8")
    except UnicodeDecodeError:
        output_utf8 = None
    response = {
        "schema": "laplace.cognition-response/v1",
        "package_id": package_id,
        "relations": relations,
        "program_id": firmware["program_id"],
        "status": result.get("status"),
        "output_utf8": output_utf8,
        "output_hex": output_hex,
        "execution": result,
    }
    return response


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
                "status": "ready",
                "package_id": package_id,
                "database": DATABASE,
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
            self._json(413, {"error": "request body must be between 1 byte and 1 MiB"})
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
        relations = payload.get("relations", ["semantic"])
        if not isinstance(prompt, str) or not prompt:
            self._json(400, {"error": "prompt must be non-empty text"})
            return
        if (
            not isinstance(relations, list)
            or not relations
            or any(not isinstance(item, str) or item not in RELATIONS for item in relations)
        ):
            self._json(400, {"error": "relations contain an unsupported relation family"})
            return
        if not self.semaphore.acquire(blocking=False):
            self._json(503, {"error": "cognition concurrency limit reached"})
            return
        try:
            result = execute_cognition(prompt, relations)
            status = result.get("status")
            self._json(200 if status == 0 else 422, result)
        except CognitionError as error:
            self._json(500, {"error": str(error)})
        except Exception as error:
            self._json(500, {"error": f"unexpected cognition failure: {error}"})
        finally:
            self.semaphore.release()

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"laplace-cognition-service: {self.address_string()} {fmt % args}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.listen, args.port), Handler)
    print(f"laplace-cognition-service listening on {args.listen}:{args.port}", file=sys.stderr)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
