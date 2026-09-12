#!/usr/bin/env python3
"""Bind the existing generic XML decomposition provider into product source admission.

This is intentionally source-neutral. XML applicability is decided by the existing
laplace_decomposition_xml provider from the artifact media type; no WordNet/source
name appears in the native execution path.
"""

from __future__ import annotations

from pathlib import Path

TARGET = Path("integrations/postgresql/extension/src/source_admission_pg.c")


def replace_once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one patch anchor, found {count}: {old[:80]!r}")
    return text.replace(old, new, 1)


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    if "laplace_pg_source_xml_provider_fingerprint" in text:
        print("generic XML source provider is already bound")
        return 0

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
    /* Match the existing generic XML provider identity used by the native
     * XML/universal-AST route: length-prefixed domain plus empty payload. */
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
        """    /* The generic XML provider is always present in the common provider set.
     * Its own applicability contract restricts it to application/xml, text/xml,
     * and +xml grammar-input spans, so non-XML source profiles are unchanged. */
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

    TARGET.write_text(text, encoding="utf-8")
    print(f"patched {TARGET}: generic UAX + XML provider set")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
