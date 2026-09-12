#!/usr/bin/env python3
"""Preserve generic decomposition syntax coordinates through source persistence.

The source-neutral decomposition ABI already carries kind, grammar_kind, field_kind,
sibling_ordinal and syntax_flags. Source admission must retain all of them so runtime
recipes can interpret structured sources without reparsing or source-named code.
"""

from __future__ import annotations

from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def replace_all_exact(path: Path, old: str, new: str, expected: int) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{path}: expected {expected} anchors, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new), encoding="utf-8")


def main() -> int:
    header = Path("engine/include/laplace/tabular_source.h")
    merge = Path("engine/src/tabular_source_recursive_merge.hpp")
    source = Path("engine/src/tabular_source.cpp")
    schema = Path("integrations/postgresql/extension/source_structural_witness.sql.in")
    pg = Path("integrations/postgresql/extension/src/source_structural_witness_pg.c")

    if "uint64_t grammar_kind;" not in header.read_text(encoding="utf-8").split(
        "typedef struct laplace_tabular_decomposition_witness", 1
    )[1].split("} laplace_tabular_decomposition_witness;", 1)[0]:
        replace_once(
            header,
            """    uint64_t kind;
    uint64_t media_type_byte_offset;
    uint64_t media_type_byte_count;
    uint32_t depth;
    uint32_t flags;
""",
            """    uint64_t kind;
    uint64_t grammar_kind;
    uint64_t field_kind;
    uint64_t sibling_ordinal;
    uint64_t media_type_byte_offset;
    uint64_t media_type_byte_count;
    uint32_t depth;
    uint32_t flags;
    uint32_t syntax_flags;
    uint32_t reserved;
""",
        )

    merge_text = merge.read_text(encoding="utf-8")
    if "std::uint64_t grammar_kind{};" not in merge_text:
        replace_once(
            merge,
            """    std::uint64_t parent_span_index{};
    std::uint64_t kind{};
    std::uint64_t media_type_byte_count{};
    std::uint32_t depth{};
    std::uint32_t flags{};
""",
            """    std::uint64_t parent_span_index{};
    std::uint64_t kind{};
    std::uint64_t grammar_kind{};
    std::uint64_t field_kind{};
    std::uint64_t sibling_ordinal{};
    std::uint64_t media_type_byte_count{};
    std::uint32_t depth{};
    std::uint32_t flags{};
    std::uint32_t syntax_flags{};
""",
        )
        replace_once(
            merge,
            """        witness.kind = span.kind;
        witness.media_type_byte_offset = media_offset;
        witness.media_type_byte_count = span.media_type_byte_count;
        witness.depth = span.depth;
        witness.flags = span.flags;
""",
            """        witness.kind = span.kind;
        witness.grammar_kind = span.grammar_kind;
        witness.field_kind = span.field_kind;
        witness.sibling_ordinal = span.sibling_ordinal;
        witness.media_type_byte_offset = media_offset;
        witness.media_type_byte_count = span.media_type_byte_count;
        witness.depth = span.depth;
        witness.flags = span.flags;
        witness.syntax_flags = span.syntax_flags;
""",
        )

    source_text = source.read_text(encoding="utf-8")
    if "witness.grammar_kind = span.grammar_kind;" not in source_text:
        replace_once(
            source,
            """                witness.parent_span_index = span.parent_span_index;
                witness.kind = span.kind;
                witness.media_type_byte_count =
                    static_cast<std::uint64_t>(media_type_bytes);
                witness.depth = span.depth;
                witness.flags = span.flags;
""",
            """                witness.parent_span_index = span.parent_span_index;
                witness.kind = span.kind;
                witness.grammar_kind = span.grammar_kind;
                witness.field_kind = span.field_kind;
                witness.sibling_ordinal = span.sibling_ordinal;
                witness.media_type_byte_count =
                    static_cast<std::uint64_t>(media_type_bytes);
                witness.depth = span.depth;
                witness.flags = span.flags;
                witness.syntax_flags = span.syntax_flags;
""",
        )

    schema_text = schema.read_text(encoding="utf-8")
    if "grammar_kind numeric(20, 0)" not in schema_text:
        replace_once(
            schema,
            """    kind numeric(20, 0) NOT NULL
        CHECK (kind BETWEEN 0 AND 18446744073709551615),
    media_type bytea NOT NULL,
    depth numeric(20, 0) NOT NULL CHECK (depth BETWEEN 0 AND 4294967295),
    flags numeric(20, 0) NOT NULL CHECK (flags BETWEEN 0 AND 4294967295),
""",
            """    kind numeric(20, 0) NOT NULL
        CHECK (kind BETWEEN 0 AND 18446744073709551615),
    grammar_kind numeric(20, 0) NOT NULL
        CHECK (grammar_kind BETWEEN 0 AND 18446744073709551615),
    field_kind numeric(20, 0) NOT NULL
        CHECK (field_kind BETWEEN 0 AND 18446744073709551615),
    sibling_ordinal numeric(20, 0) NOT NULL
        CHECK (sibling_ordinal BETWEEN 0 AND 18446744073709551615),
    media_type bytea NOT NULL,
    depth numeric(20, 0) NOT NULL CHECK (depth BETWEEN 0 AND 4294967295),
    flags numeric(20, 0) NOT NULL CHECK (flags BETWEEN 0 AND 4294967295),
    syntax_flags numeric(20, 0) NOT NULL CHECK (syntax_flags BETWEEN 0 AND 4294967295),
""",
        )

    pg_text = pg.read_text(encoding="utf-8")
    if "grammar_kind,field_kind,sibling_ordinal" not in pg_text:
        replace_once(
            pg,
            """    /* Canonical batch accounting is independent of PostgreSQL Datum layout:
     * three fixed identities, six u64 coordinates, one length-prefixed media
     * value, and two u32 values. */
    static const uint64 fixed_bytes =
        UINT64_C(32) + UINT64_C(32) + UINT64_C(16) +
        UINT64_C(6) * UINT64_C(8) + UINT64_C(8) +
        UINT64_C(2) * UINT64_C(4);
""",
            """    /* Canonical batch accounting is independent of PostgreSQL Datum layout:
     * three fixed identities, nine u64 coordinates, one length-prefixed media
     * value, and three u32 values. */
    static const uint64 fixed_bytes =
        UINT64_C(32) + UINT64_C(32) + UINT64_C(16) +
        UINT64_C(9) * UINT64_C(8) + UINT64_C(8) +
        UINT64_C(3) * UINT64_C(4);
""",
        )
        replace_once(
            pg,
            """        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::bytea[],"
        "$12::numeric[],$13::numeric[]) AS u(trace_fingerprint,provider_fingerprint,"
        "canonical_entity_id,artifact_index,span_index,parent_span_index,byte_start,"
        "byte_end,kind,media_type,depth,flags)) "
        "INSERT INTO " LAPLACE_PG_SCHEMA ".source_structural_witness("
        "source_profile_id,artifact_index,span_index,parent_span_index,trace_fingerprint,"
        "provider_fingerprint,canonical_entity_id,byte_start,byte_end,kind,media_type,"
        "depth,flags) SELECT source_profile_id,artifact_index,span_index,parent_span_index,"
        "trace_fingerprint,provider_fingerprint,canonical_entity_id,byte_start,byte_end,"
        "kind,media_type,depth,flags FROM input ON CONFLICT DO NOTHING";
""",
            """        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::numeric[],"
        "$12::numeric[],$13::numeric[],$14::bytea[],$15::numeric[],$16::numeric[],"
        "$17::numeric[]) AS u(trace_fingerprint,provider_fingerprint,canonical_entity_id,"
        "artifact_index,span_index,parent_span_index,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags)) "
        "INSERT INTO " LAPLACE_PG_SCHEMA ".source_structural_witness("
        "source_profile_id,artifact_index,span_index,parent_span_index,trace_fingerprint,"
        "provider_fingerprint,canonical_entity_id,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags) SELECT "
        "source_profile_id,artifact_index,span_index,parent_span_index,trace_fingerprint,"
        "provider_fingerprint,canonical_entity_id,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags FROM input "
        "ON CONFLICT DO NOTHING";
""",
        )
        replace_once(
            pg,
            """        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::bytea[],"
        "$12::numeric[],$13::numeric[]) AS u(trace_fingerprint,provider_fingerprint,"
        "canonical_entity_id,artifact_index,span_index,parent_span_index,byte_start,"
        "byte_end,kind,media_type,depth,flags)), "
""",
            """        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::numeric[],"
        "$12::numeric[],$13::numeric[],$14::bytea[],$15::numeric[],$16::numeric[],"
        "$17::numeric[]) AS u(trace_fingerprint,provider_fingerprint,canonical_entity_id,"
        "artifact_index,span_index,parent_span_index,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags)), "
""",
        )
        replace_once(
            pg,
            """        "s.byte_end<>i.byte_end OR s.kind<>i.kind OR s.media_type<>i.media_type OR "
        "s.depth<>i.depth OR s.flags<>i.flags) "
""",
            """        "s.byte_end<>i.byte_end OR s.kind<>i.kind OR s.grammar_kind<>i.grammar_kind OR "
        "s.field_kind<>i.field_kind OR s.sibling_ordinal<>i.sibling_ordinal OR "
        "s.media_type<>i.media_type OR s.depth<>i.depth OR s.flags<>i.flags OR "
        "s.syntax_flags<>i.syntax_flags) "
""",
        )
        replace_once(
            pg,
            """    Oid witness_types[13] = {
        BYTEAOID, BYTEAARRAYOID, BYTEAARRAYOID, BYTEAARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, BYTEAARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID};
    Datum witness_parameters[13];
""",
            """    Oid witness_types[17] = {
        BYTEAOID, BYTEAARRAYOID, BYTEAARRAYOID, BYTEAARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID, BYTEAARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID};
    Datum witness_parameters[17];
""",
        )
        replace_once(
            pg,
            """        hash_u64(&hasher, witness->kind);
        hash_bytes(&hasher, media, (size_t)witness->media_type_byte_count);
        hash_u32(&hasher, witness->depth);
        hash_u32(&hasher, witness->flags);
""",
            """        hash_u64(&hasher, witness->kind);
        hash_u64(&hasher, witness->grammar_kind);
        hash_u64(&hasher, witness->field_kind);
        hash_u64(&hasher, witness->sibling_ordinal);
        hash_bytes(&hasher, media, (size_t)witness->media_type_byte_count);
        hash_u32(&hasher, witness->depth);
        hash_u32(&hasher, witness->flags);
        hash_u32(&hasher, witness->syntax_flags);
""",
        )
        replace_once(
            pg,
            """        Datum* kind_values = (Datum*)palloc(sizeof(*kind_values) * batch_count);
        Datum* media_values = (Datum*)palloc(sizeof(*media_values) * batch_count);
        Datum* depth_values = (Datum*)palloc(sizeof(*depth_values) * batch_count);
        Datum* flag_values = (Datum*)palloc(sizeof(*flag_values) * batch_count);
""",
            """        Datum* kind_values = (Datum*)palloc(sizeof(*kind_values) * batch_count);
        Datum* grammar_kind_values = (Datum*)palloc(sizeof(*grammar_kind_values) * batch_count);
        Datum* field_kind_values = (Datum*)palloc(sizeof(*field_kind_values) * batch_count);
        Datum* sibling_ordinal_values = (Datum*)palloc(sizeof(*sibling_ordinal_values) * batch_count);
        Datum* media_values = (Datum*)palloc(sizeof(*media_values) * batch_count);
        Datum* depth_values = (Datum*)palloc(sizeof(*depth_values) * batch_count);
        Datum* flag_values = (Datum*)palloc(sizeof(*flag_values) * batch_count);
        Datum* syntax_flag_values = (Datum*)palloc(sizeof(*syntax_flag_values) * batch_count);
""",
        )
        replace_once(
            pg,
            """            kind_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->kind);
            media_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
""",
            """            kind_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->kind);
            grammar_kind_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->grammar_kind);
            field_kind_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->field_kind);
            sibling_ordinal_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->sibling_ordinal);
            media_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
""",
        )
        replace_once(
            pg,
            """            flag_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->flags);
""",
            """            flag_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->flags);
            syntax_flag_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->syntax_flags);
""",
        )
        replace_once(
            pg,
            """        witness_parameters[9] = PointerGetDatum(numeric_array(kind_values, batch_count));
        witness_parameters[10] = PointerGetDatum(bytea_array(media_values, batch_count));
        witness_parameters[11] = PointerGetDatum(numeric_array(depth_values, batch_count));
        witness_parameters[12] = PointerGetDatum(numeric_array(flag_values, batch_count));
""",
            """        witness_parameters[9] = PointerGetDatum(numeric_array(kind_values, batch_count));
        witness_parameters[10] = PointerGetDatum(numeric_array(grammar_kind_values, batch_count));
        witness_parameters[11] = PointerGetDatum(numeric_array(field_kind_values, batch_count));
        witness_parameters[12] = PointerGetDatum(numeric_array(sibling_ordinal_values, batch_count));
        witness_parameters[13] = PointerGetDatum(bytea_array(media_values, batch_count));
        witness_parameters[14] = PointerGetDatum(numeric_array(depth_values, batch_count));
        witness_parameters[15] = PointerGetDatum(numeric_array(flag_values, batch_count));
        witness_parameters[16] = PointerGetDatum(numeric_array(syntax_flag_values, batch_count));
""",
        )
        replace_all_exact(
            pg,
            """            witnesses_insert_sql, 13, witness_types, witness_parameters,""",
            """            witnesses_insert_sql, 17, witness_types, witness_parameters,""",
            1,
        )
        replace_all_exact(
            pg,
            """            witnesses_verify_sql, 13, witness_types, witness_parameters,""",
            """            witnesses_verify_sql, 17, witness_types, witness_parameters,""",
            1,
        )

    print("preserved grammar_kind field_kind sibling_ordinal syntax_flags end-to-end")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
