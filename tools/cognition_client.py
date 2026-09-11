#!/usr/bin/env python3
"""Execute a prompt against the active persistent Laplace cognition service."""
from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request

RELATIONS = {"container", "constituent", "predecessor", "successor", "cooccur", "semantic"}
DEFAULT_ENDPOINT = "http://127.0.0.1:8080/v1/cognition"


def parse_relations(raw: str) -> list[str]:
    values = [value.strip() for value in raw.split(",") if value.strip()]
    if not values or any(value not in RELATIONS for value in values):
        raise ValueError(
            "relations must be a comma-separated sequence of: "
            + ", ".join(sorted(RELATIONS))
        )
    return values


def read_prompt(argument: str | None) -> str:
    if argument is not None:
        return argument
    value = sys.stdin.read()
    if not value:
        raise ValueError("provide a prompt argument or pipe prompt text on stdin")
    return value


def request_cognition(endpoint: str, prompt: str, relations: list[str]) -> dict:
    body = json.dumps(
        {"prompt": prompt, "relations": relations},
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=body,
        method="POST",
        headers={"Content-Type": "application/json; charset=utf-8"},
    )
    try:
        with urllib.request.urlopen(request, timeout=310) as response:
            payload = response.read()
    except urllib.error.HTTPError as error:
        payload = error.read()
        try:
            value = json.loads(payload)
        except json.JSONDecodeError:
            raise RuntimeError(f"cognition service returned HTTP {error.code}") from error
        detail = value.get("error") if isinstance(value, dict) else None
        if not detail and isinstance(value, dict):
            execution = value.get("execution")
            if isinstance(execution, dict):
                detail = (
                    f"status={execution.get('status')} failed_step={execution.get('failed_step')} "
                    f"native_status={execution.get('native_status')}"
                )
        raise RuntimeError(detail or f"cognition service returned HTTP {error.code}") from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"cognition service is unavailable: {error.reason}") from error
    try:
        value = json.loads(payload)
    except json.JSONDecodeError as error:
        raise RuntimeError("cognition service returned invalid JSON") from error
    if not isinstance(value, dict):
        raise RuntimeError("cognition service returned an invalid response")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prompt", nargs="?", help="exact UTF-8 prompt; omit to read stdin")
    parser.add_argument(
        "--relations",
        default="semantic",
        help="ordered cognition relation chain (default: semantic)",
    )
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    parser.add_argument("--json", action="store_true", help="print the complete execution result")
    args = parser.parse_args()
    try:
        prompt = read_prompt(args.prompt)
        relations = parse_relations(args.relations)
        result = request_cognition(args.endpoint, prompt, relations)
        if result.get("status") != 0:
            raise RuntimeError(f"cognition returned status {result.get('status')}")
        if args.json:
            print(json.dumps(result, ensure_ascii=False, sort_keys=True))
            return 0
        output = result.get("output_utf8")
        if isinstance(output, str):
            sys.stdout.write(output)
            if not output.endswith("\n"):
                sys.stdout.write("\n")
            return 0
        output_hex = result.get("output_hex")
        if not isinstance(output_hex, str):
            raise RuntimeError("cognition returned neither UTF-8 nor binary output")
        sys.stdout.buffer.write(bytes.fromhex(output_hex))
        return 0
    except (RuntimeError, ValueError) as error:
        print(f"laplace-cognition: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
