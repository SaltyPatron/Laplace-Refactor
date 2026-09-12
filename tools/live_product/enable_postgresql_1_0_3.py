#!/usr/bin/env python3
"""Publish the generic structural-witness ABI as additive Laplace extension 1.0.3."""

from __future__ import annotations

import json
from pathlib import Path

CMAKE = Path("integrations/postgresql/extension/CMakeLists.txt")
CONTROL = Path("integrations/postgresql/extension/laplace.control.in")
PREFIX = Path("integrations/postgresql/extension/product_cognition_upgrade_prefix.sql")
RECONCILE = Path("tools/delivery/product_activation_reconcile.py")
PACKAGE = Path("contracts/product-package.json")


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> int:
    control = CONTROL.read_text(encoding="utf-8")
    if "default_version = '1.0.3'" not in control:
        replace_once(CONTROL, "default_version = '1.0.2'", "default_version = '1.0.3'")

    cmake = CMAKE.read_text(encoding="utf-8")
    if "laplace--1.0.2--1.0.3.sql" not in cmake:
        old = '''set(extension_101_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.1.sql")
file(READ "${extension_sql}" previous_schema)
file(READ "${persisted_cognition_upgrade_sql}" persisted_cognition_delta)
file(WRITE "${extension_101_sql}" "${previous_schema}\\n${persisted_cognition_delta}")

set(product_cognition_upgrade_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.1--1.0.2.sql")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cognition_product_route.sql.in"
    "${product_cognition_upgrade_sql}" @ONLY)
set(current_extension_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.2.sql")
file(READ "${product_cognition_upgrade_sql}" product_cognition_delta)
file(WRITE "${current_extension_sql}"
    "${previous_schema}\\n${persisted_cognition_delta}\\n${product_cognition_delta}")

install(TARGETS laplace_pg
    LIBRARY DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/lib")
install(FILES "${extension_control}" "${extension_sql}"
    "${extension_101_sql}" "${current_extension_sql}"
    "${persisted_cognition_upgrade_sql}" "${product_cognition_upgrade_sql}"
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace-public-readback.sql"
    DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/share/extension")
'''
        new = '''# Preserve the exact 1.0.0/1.0.1/1.0.2 predecessor schemas even though the
# current source tree now emits the richer 1.0.3 structural-witness table.
file(READ "${extension_sql}" current_base_schema)
set(source_structural_witness_102_sql
    "${CMAKE_CURRENT_BINARY_DIR}/source_structural_witness_1_0_2.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/source_structural_witness_1_0_2.sql.in"
    "${source_structural_witness_102_sql}"
    @ONLY)
file(READ "${source_structural_witness_102_sql}"
    LAPLACE_SOURCE_STRUCTURAL_WITNESS_102_SQL)
string(REPLACE
    "${LAPLACE_SOURCE_STRUCTURAL_WITNESS_SQL}"
    "${LAPLACE_SOURCE_STRUCTURAL_WITNESS_102_SQL}"
    previous_schema
    "${current_base_schema}")
# laplace--1.0.0.sql is a historical predecessor, not an alias for current schema.
file(WRITE "${extension_sql}" "${previous_schema}")

set(extension_101_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.1.sql")
file(READ "${persisted_cognition_upgrade_sql}" persisted_cognition_delta)
file(WRITE "${extension_101_sql}" "${previous_schema}\\n${persisted_cognition_delta}")

set(product_cognition_upgrade_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.1--1.0.2.sql")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cognition_product_route.sql.in"
    "${product_cognition_upgrade_sql}" @ONLY)
set(extension_102_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.2.sql")
file(READ "${product_cognition_upgrade_sql}" product_cognition_delta)
file(WRITE "${extension_102_sql}"
    "${previous_schema}\\n${persisted_cognition_delta}\\n${product_cognition_delta}")

set(structural_witness_103_upgrade_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.2--1.0.3.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/source_structural_witness_1_0_2_to_1_0_3.sql.in"
    "${structural_witness_103_upgrade_sql}"
    @ONLY)
set(current_extension_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.3.sql")
file(WRITE "${current_extension_sql}"
    "${current_base_schema}\\n${persisted_cognition_delta}\\n${product_cognition_delta}")

install(TARGETS laplace_pg
    LIBRARY DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/lib")
install(FILES "${extension_control}" "${extension_sql}"
    "${extension_101_sql}" "${extension_102_sql}" "${current_extension_sql}"
    "${persisted_cognition_upgrade_sql}" "${product_cognition_upgrade_sql}"
    "${structural_witness_103_upgrade_sql}"
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace-public-readback.sql"
    DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/share/extension")
'''
        replace_once(CMAKE, old, new)

    prefix = PREFIX.read_text(encoding="utf-8")
    if "ALTER EXTENSION laplace UPDATE TO '1.0.3';" not in prefix:
        replace_once(
            PREFIX,
            """    IF version = '1.0.1' THEN
        ALTER EXTENSION laplace UPDATE TO '1.0.2';
    ELSIF version <> '1.0.2' THEN
        RAISE EXCEPTION 'unsupported product cognition predecessor version: %', version;
    END IF;
""",
            """    IF version = '1.0.1' THEN
        ALTER EXTENSION laplace UPDATE TO '1.0.2';
        version := '1.0.2';
    END IF;
    IF version = '1.0.2' THEN
        ALTER EXTENSION laplace UPDATE TO '1.0.3';
    ELSIF version <> '1.0.3' THEN
        RAISE EXCEPTION 'unsupported product cognition predecessor version: %', version;
    END IF;
""",
        )

    reconciler = RECONCILE.read_text(encoding="utf-8")
    if "successor changed during reconciliation: %', version;\n    END IF;" in reconciler and "version <> '1.0.3'" not in reconciler:
        reconciler = reconciler.replace(
            'if current.get("version") != "1.0.2":',
            'if current.get("version") != "1.0.3":',
            1,
        )
        reconciler = reconciler.replace(
            '"laplace--1.0.0--1.0.1.sql"',
            '"laplace--1.0.2--1.0.3.sql"',
            1,
        )
        reconciler = reconciler.replace(
            "IF version <> '1.0.2' THEN\n        RAISE EXCEPTION 'product cognition successor changed during reconciliation: %', version;",
            "IF version <> '1.0.3' THEN\n        RAISE EXCEPTION 'product cognition successor changed during reconciliation: %', version;",
            1,
        )
        reconciler = reconciler.replace(
            "'version':'1.0.2'", "'version':'1.0.3'"
        )
        reconciler = reconciler.replace(
            '"version": "1.0.2",', '"version": "1.0.3",', 1
        )
        RECONCILE.write_text(reconciler, encoding="utf-8")

    document = json.loads(PACKAGE.read_text(encoding="utf-8"))
    required = document["package"]["required_files"]
    for relative in (
        "pgsql-18/share/extension/laplace--1.0.3.sql",
        "pgsql-18/share/extension/laplace--1.0.2--1.0.3.sql",
    ):
        if relative not in required:
            required.append(relative)
    PACKAGE.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")

    print("published additive PostgreSQL extension 1.0.3 with historical predecessors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
