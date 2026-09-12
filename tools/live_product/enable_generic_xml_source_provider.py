#!/usr/bin/env python3
"""Repair common source admission to be provider- and recipe-driven at runtime.

This patch is intentionally source-neutral:
- the existing generic XML provider joins the common provider set;
- a runtime recipe may leave its *derived* artifact-graph fingerprint zero, in
  which case the native source engine derives it from the exact artifact,
  reference, and mapping declarations before compilation.

No source name or WordNet-specific branch is introduced.
"""

from __future__ import annotations

from pathlib import Path

TARGET = Path("integrations/postgresql/extension/src/source_admission_pg.c")


def replace_once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one patch anchor, found {count}: {old[:100]!r}")
    return text.replace(old, new, 1)


def bind_xml(text: str) -> tuple[str, bool]:
    if "laplace_pg_source_xml_provider_fingerprint" in text:
        return text, False

    text = replace_once(
        text,
        '#include "laplace/decomposition_uax29.h"\n',
        '#include "laplace/decomposition_uax29.h"\n#include "laplace/decomposition_xml.h"\n',
    )
    marker = """static laplace_tabular_source_status
laplace_pg_source_decomposition_plan_create(
"""
    helper = """static laplace_digest256 laplace_pg_source_xml_provider_fingerprint(void) {
    static const char domain[] = "laplace-decomposition-xml-v1";
    laplace_digest256 result;
    blake3_hasher hasher;
    memset(&result, 0, sizeof(result));
    blake3_hasher_init(&hasher);
    laplace_pg_source_hash_bytes(&hasher, domain, sizeof(domain) - 1u);
    laplace_pg_source_hash_bytes(&hasher, NULL, 0u);
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

"""
    text = replace_once(text, marker, helper + marker)
    text = replace_once(
        text,
        """    laplace_decomposition_uax29_provider uax_provider;
    laplace_pg_active_uax_authority uax_authority;
    laplace_digest256 uax_fingerprint;
    laplace_tabular_source_status status;
""",
        """    laplace_decomposition_uax29_provider uax_provider;
    laplace_decomposition_xml_provider xml_provider;
    laplace_decomposition_provider_v1 providers[2];
    laplace_pg_active_uax_authority uax_authority;
    laplace_digest256 uax_fingerprint;
    laplace_digest256 xml_fingerprint;
    laplace_tabular_source_status status;
""",
    )
    text = replace_once(
        text,
        """    memset(&uax_provider, 0, sizeof(uax_provider));
    memset(&uax_authority, 0, sizeof(uax_authority));
""",
        """    memset(&uax_provider, 0, sizeof(uax_provider));
    memset(&xml_provider, 0, sizeof(xml_provider));
    memset(providers, 0, sizeof(providers));
    memset(&uax_authority, 0, sizeof(uax_authority));
""",
    )
    text = replace_once(
        text,
        """    status = laplace_source_decomposition_plan_create(
        input, &uax_provider.provider, 1u, plan);
""",
        """    /* Generic providers advertise applicability from media/syntax, so one
     * common provider set can execute unrelated source recipes without a
     * source-family dispatcher. */
    xml_fingerprint = laplace_pg_source_xml_provider_fingerprint();
    if (laplace_decomposition_xml_provider_init(
            &xml_provider, UINT64_C(0x584d4c0000000000), &xml_fingerprint) !=
        LAPLACE_DECOMPOSITION_OK) {
        laplace_uax29_tables_destroy(&uax_tables);
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
    providers[0] = uax_provider.provider;
    providers[1] = xml_provider.provider;
    status = laplace_source_decomposition_plan_create(
        input, providers, 2u, plan);
""",
    )
    return text, True


def bind_runtime_artifact_graph(text: str) -> tuple[str, bool]:
    if "laplace_pg_source_runtime_artifact_graph" in text:
        return text, False

    helper_marker = """static laplace_tabular_source_status
laplace_pg_source_decomposition_plan_create(
"""
    helper = """static int laplace_pg_source_digest_zero(const laplace_digest256* value) {
    size_t index;
    if (value == NULL) return 1;
    for (index = 0u; index < sizeof(value->bytes); ++index) {
        if (value->bytes[index] != 0u) return 0;
    }
    return 1;
}

static laplace_tabular_source_status laplace_pg_source_runtime_artifact_graph(
    laplace_tabular_source_input* input) {
    laplace_digest256 graph;
    laplace_tabular_source_status status;
    if (input == NULL) return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    if (!laplace_pg_source_digest_zero(
            &input->profile_declaration.artifact_graph_fingerprint)) {
        return LAPLACE_TABULAR_SOURCE_OK;
    }
    memset(&graph, 0, sizeof(graph));
    status = laplace_tabular_source_graph_identify(
        input->artifacts, (size_t)input->artifact_count,
        input->reference_rules, (size_t)input->reference_rule_count,
        input->mapping_rules, (size_t)input->mapping_rule_count,
        &graph);
    if (status != LAPLACE_TABULAR_SOURCE_OK) return status;
    input->profile_declaration.artifact_graph_fingerprint = graph;
    return LAPLACE_TABULAR_SOURCE_OK;
}

"""
    text = replace_once(text, helper_marker, helper + helper_marker)

    text = replace_once(
        text,
        """    laplace_digest256 xml_fingerprint;
    laplace_tabular_source_status status;
""",
        """    laplace_digest256 xml_fingerprint;
    laplace_tabular_source_input runtime_input;
    laplace_tabular_source_status status;
""",
    )
    text = replace_once(
        text,
        """    memset(&uax_authority, 0, sizeof(uax_authority));

    /* Product UAX authority""",
        """    memset(&uax_authority, 0, sizeof(uax_authority));
    runtime_input = *input;
    status = laplace_pg_source_runtime_artifact_graph(&runtime_input);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        return status;
    }

    /* Product UAX authority""",
    )
    text = replace_once(
        text,
        """    status = laplace_source_decomposition_plan_create(
        input, providers, 2u, plan);
""",
        """    status = laplace_source_decomposition_plan_create(
        &runtime_input, providers, 2u, plan);
""",
    )
    return text, True


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    text, xml_changed = bind_xml(text)
    text, graph_changed = bind_runtime_artifact_graph(text)
    if not (xml_changed or graph_changed):
        print("common source admission already has generic XML + runtime artifact graph")
        return 0
    TARGET.write_text(text, encoding="utf-8")
    changes = []
    if xml_changed:
        changes.append("generic XML provider")
    if graph_changed:
        changes.append("runtime native artifact-graph derivation")
    print(f"patched {TARGET}: " + ", ".join(changes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
