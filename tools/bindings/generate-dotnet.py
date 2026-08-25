#!/usr/bin/env python3
"""Generate the typed .NET ISA declaration surface from contracts/isa.json."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
from typing import Any


def fail(message: str) -> None:
    raise ValueError(message)


def require_mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{name} must be an object")
    return value


def require_uint(value: Any, name: str, maximum: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0 or value > maximum:
        fail(f"{name} must be an unsigned integer no greater than {maximum}")
    return value


def require_identifier(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{name} must be a non-empty string")
    if not (value[0].isalpha() or value[0] == "_"):
        fail(f"{name} is not a C# identifier")
    if not all(character.isalnum() or character == "_" for character in value):
        fail(f"{name} is not a C# identifier")
    return value


def require_namespace(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{name} must be a non-empty namespace")
    for segment in value.split("."):
        require_identifier(segment, name)
    return value


def csharp_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render(contract_bytes: bytes) -> bytes:
    contract = require_mapping(json.loads(contract_bytes), "contract")
    if contract.get("schema") != "laplace.isa-contract/v1":
        fail("unsupported ISA contract schema")

    version = require_mapping(contract.get("version"), "version")
    major = require_uint(version.get("major"), "version.major", 0xFFFF)
    minor = require_uint(version.get("minor"), "version.minor", 0xFFFF)
    value_types = require_mapping(contract.get("value_types"), "value_types")
    opcodes = require_mapping(contract.get("opcodes"), "opcodes")
    modules = require_mapping(contract.get("modules"), "modules")
    operations = require_mapping(contract.get("operation_contracts"), "operation_contracts")
    instruction_versions = require_mapping(
        contract.get("instruction_versions"), "instruction_versions"
    )
    introduced_minor = require_mapping(contract.get("introduced_minor"), "introduced_minor")
    receipt = require_mapping(contract.get("receipt"), "receipt")
    bindings = require_mapping(contract.get("bindings"), "bindings")
    dotnet = require_mapping(bindings.get("dotnet"), "bindings.dotnet")
    managed_types = require_mapping(dotnet.get("value_types"), "bindings.dotnet.value_types")
    declarations = require_mapping(
        dotnet.get("operation_declarations"),
        "bindings.dotnet.operation_declarations",
    )

    namespace = require_namespace(dotnet.get("namespace"), "bindings.dotnet.namespace")
    target_framework = dotnet.get("target_framework")
    native_library = dotnet.get("native_library")
    execute_symbol = dotnet.get("execute_symbol")
    if target_framework != "net10.0":
        fail("bindings.dotnet.target_framework must be net10.0")
    for value, name in (
        (native_library, "bindings.dotnet.native_library"),
        (execute_symbol, "bindings.dotnet.execute_symbol"),
    ):
        if not isinstance(value, str) or not value:
            fail(f"{name} must be a non-empty string")

    if set(managed_types) != set(value_types):
        fail("the .NET value-type map must exactly cover value_types")
    if set(declarations) != set(operations):
        fail("the .NET operation declaration map must exactly cover operation_contracts")
    if set(opcodes) != set(operations):
        fail("opcodes must exactly cover operation_contracts")
    if set(instruction_versions) != set(operations):
        fail("instruction_versions must exactly cover operation_contracts")
    if set(introduced_minor) != set(operations):
        fail("introduced_minor must exactly cover operation_contracts")

    value_rows: list[tuple[int, str, str]] = []
    seen_value_ids: set[int] = set()
    for name, numeric_value in value_types.items():
        value_id = require_uint(numeric_value, f"value_types.{name}", 0xFFFFFFFF)
        if value_id in seen_value_ids:
            fail(f"duplicate ISA value type identifier: {value_id}")
        seen_value_ids.add(value_id)
        managed_type = managed_types[name]
        if not isinstance(managed_type, str) or not managed_type.startswith("global::"):
            fail(f"bindings.dotnet.value_types.{name} must be a global C# type name")
        value_rows.append((value_id, name, managed_type))
    value_rows.sort()

    operation_rows: list[dict[str, Any]] = []
    seen_opcodes: set[int] = set()
    seen_declarations: set[str] = set()
    for name, raw_operation in operations.items():
        operation = require_mapping(raw_operation, f"operation_contracts.{name}")
        module_name = operation.get("module")
        input_name = operation.get("input_type")
        output_name = operation.get("output_type")
        if module_name not in modules:
            fail(f"operation {name} references unknown module {module_name}")
        if input_name not in value_types or output_name not in value_types:
            fail(f"operation {name} references an unknown value type")
        opcode = require_uint(opcodes[name], f"opcodes.{name}", 0xFFFFFFFF)
        if opcode in seen_opcodes:
            fail(f"duplicate ISA opcode: {opcode}")
        seen_opcodes.add(opcode)
        declaration = require_identifier(
            declarations[name], f"bindings.dotnet.operation_declarations.{name}"
        )
        if declaration in seen_declarations:
            fail(f"duplicate .NET operation declaration: {declaration}")
        seen_declarations.add(declaration)
        operation_rows.append(
            {
                "name": name,
                "declaration": declaration,
                "opcode": opcode,
                "module": require_uint(
                    modules[module_name], f"modules.{module_name}", 0xFFFFFFFF
                ),
                "input_id": require_uint(
                    value_types[input_name], f"value_types.{input_name}", 0xFFFFFFFF
                ),
                "output_id": require_uint(
                    value_types[output_name], f"value_types.{output_name}", 0xFFFFFFFF
                ),
                "input_type": managed_types[input_name],
                "output_type": managed_types[output_name],
                "version": require_uint(
                    instruction_versions[name],
                    f"instruction_versions.{name}",
                    0xFFFF,
                ),
                "introduced_minor": require_uint(
                    introduced_minor[name], f"introduced_minor.{name}", 0xFFFF
                ),
            }
        )
    operation_rows.sort(key=lambda row: row["opcode"])

    digest_bytes = require_uint(receipt.get("digest_bytes"), "receipt.digest_bytes", 0xFFFFFFFF)
    receipt_detail = require_uint(receipt.get("detail_full"), "receipt.detail_full", 0xFFFFFFFF)
    digest_algorithm = receipt.get("digest_algorithm")
    if not isinstance(digest_algorithm, str) or not digest_algorithm:
        fail("receipt.digest_algorithm must be a non-empty string")
    known_program_flags = require_uint(
        contract.get("known_program_flags"), "known_program_flags", 0xFFFFFFFF
    )
    known_instruction_flags = require_uint(
        contract.get("known_instruction_flags"), "known_instruction_flags", 0xFFFF
    )
    known_value_flags = require_uint(
        contract.get("known_value_flags"), "known_value_flags", 0xFFFFFFFF
    )
    source_digest = hashlib.sha256(contract_bytes).hexdigest()

    lines = [
        "// <auto-generated />",
        "// Generator: tools/bindings/generate-dotnet.py",
        "// Source: contracts/isa.json",
        f"// Source SHA-256: {source_digest}",
        "#nullable enable",
        "",
        f"namespace {namespace};",
        "",
        "public static class LaplaceIsaContract",
        "{",
        f"    public const ushort Major = {major};",
        f"    public const ushort Minor = {minor};",
        f"    public const uint ReceiptDigestBytes = {digest_bytes}U;",
        f"    public const string ReceiptDigestAlgorithm = {csharp_string(digest_algorithm)};",
        f"    public const uint ReceiptDetailFull = {receipt_detail}U;",
        f"    public const uint KnownProgramFlags = {known_program_flags}U;",
        f"    public const ushort KnownInstructionFlags = {known_instruction_flags};",
        f"    public const uint KnownValueFlags = {known_value_flags}U;",
        f"    public const string NativeLibrary = {csharp_string(native_library)};",
        f"    public const string ExecuteSymbol = {csharp_string(execute_symbol)};",
        f"    public const string TargetFramework = {csharp_string(target_framework)};",
        "",
        "    private static readonly LaplaceValueTypeDescriptor[] ValueTypeStorage =",
        "    [",
    ]
    for value_id, name, managed_type in value_rows:
        lines.append(
            "        new(" + f"{value_id}U, {csharp_string(name)}, {csharp_string(managed_type)}" + "),"
        )
    lines.extend(
        [
            "    ];",
            "",
            "    private static readonly LaplaceOperationDescriptor[] OperationStorage =",
            "    [",
        ]
    )
    for row in operation_rows:
        lines.append(
            "        new("
            + f"{row['opcode']}U, {row['module']}U, {row['input_id']}U, {row['output_id']}U, "
            + f"{row['version']}, {row['introduced_minor']}, {csharp_string(row['name'])}),"
        )
    lines.extend(
        [
            "    ];",
            "",
            "    public static global::System.ReadOnlySpan<LaplaceValueTypeDescriptor> ValueTypes => ValueTypeStorage;",
            "    public static global::System.ReadOnlySpan<LaplaceOperationDescriptor> Operations => OperationStorage;",
            "}",
            "",
        ]
    )
    for row in operation_rows:
        lines.extend(
            [
                f"public readonly struct {row['declaration']} :",
                f"    ILaplaceOperation<{row['input_type']}, {row['output_type']}>",
                "{",
                "    public static LaplaceOperationDescriptor Descriptor =>",
                "        new("
                + f"{row['opcode']}U, {row['module']}U, {row['input_id']}U, {row['output_id']}U, "
                + f"{row['version']}, {row['introduced_minor']}, {csharp_string(row['name'])});",
                "}",
                "",
            ]
        )
    return ("\n".join(lines)).encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True, type=pathlib.Path)
    output_group = parser.add_mutually_exclusive_group(required=True)
    output_group.add_argument("--output", type=pathlib.Path)
    output_group.add_argument("--verify", type=pathlib.Path)
    args = parser.parse_args()

    try:
        expected = render(args.contract.read_bytes())
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"dotnet ISA generation failed: {error}", file=sys.stderr)
        return 2

    if args.verify is not None:
        try:
            observed = args.verify.read_bytes()
        except OSError as error:
            print(f"cannot read generated binding: {error}", file=sys.stderr)
            return 2
        if observed != expected:
            print("generated .NET ISA binding differs from contracts/isa.json", file=sys.stderr)
            return 1
        return 0

    assert args.output is not None
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(expected)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
