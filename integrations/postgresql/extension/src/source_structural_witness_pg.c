#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/xact.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "utils/array.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace_pg_internal.h"
#include "source_structural_witness_pg.h"
#include "laplace/decomposition.h"
#include "source_profile_pg.h"
#include "miscadmin.h"
#include "utils/builtins.h"

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    uint8_t bytes[4];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_bytes(
    blake3_hasher* hasher,
    const uint8_t* bytes,
    size_t byte_count) {
    hash_u64(hasher, (uint64_t)byte_count);
    if (byte_count != 0u) {
        blake3_hasher_update(hasher, bytes, byte_count);
    }
}

static void finish_digest(blake3_hasher* hasher, laplace_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

static void hash_witness_row(
    blake3_hasher* hasher,
    const laplace_tabular_decomposition_witness* witness,
    const laplace_id128* selected_entity,
    const laplace_digest256* selected_physicality, const uint8_t* media) {
    const laplace_id128 entity_id = *selected_entity;
    hash_bytes(
        hasher, witness->trace_fingerprint.bytes,
        sizeof(witness->trace_fingerprint.bytes));
    hash_bytes(
        hasher, witness->provider_fingerprint.bytes,
        sizeof(witness->provider_fingerprint.bytes));
    hash_bytes(hasher, entity_id.bytes, sizeof(entity_id.bytes));
    hash_bytes(hasher, selected_physicality->bytes, sizeof(selected_physicality->bytes));
    hash_u64(hasher, witness->artifact_index);
    hash_u64(hasher, witness->span_index);
    hash_u64(hasher, witness->parent_span_index);
    hash_u64(hasher, witness->byte_start);
    hash_u64(hasher, witness->byte_end);
    hash_u64(hasher, witness->kind);
    hash_u64(hasher, witness->grammar_kind);
    hash_u64(hasher, witness->field_kind);
    hash_u64(hasher, witness->sibling_ordinal);
    hash_bytes(hasher, media, (size_t)witness->media_type_byte_count);
    hash_u32(hasher, witness->depth);
    hash_u32(hasher, witness->flags);
    hash_u32(hasher, witness->syntax_flags);
}

static bool spi_boolean(void) {
    bool is_null = false;
    Datum value;
    if (SPI_processed != 1u || SPI_tuptable == NULL) {
        return false;
    }
    value = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    return !is_null && DatumGetBool(value);
}

static int64 spi_int64_column(int column) {
    bool is_null = false;
    Datum value;
    if (SPI_processed != 1u || SPI_tuptable == NULL || column < 1 ||
        column > SPI_tuptable->tupdesc->natts) {
        return -1;
    }
    value = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, column, &is_null);
    return is_null ? -1 : DatumGetInt64(value);
}

typedef struct structural_canonical_reference {
    laplace_id128 entity_id;
    laplace_digest256 physicality_id;
} structural_canonical_reference;

static structural_canonical_reference canonical_reference(
    const laplace_tabular_decomposition_witness* witness,
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* composition_input) {
    structural_canonical_reference selected;
    const laplace_composition_operand* reference = &witness->canonical_content;
    memset(&selected, 0, sizeof(selected));
    if (witness->byte_start == witness->byte_end &&
        (witness->syntax_flags & (LAPLACE_DECOMPOSITION_SYNTAX_MISSING | LAPLACE_DECOMPOSITION_SYNTAX_EMPTY)) != 0u) {
        laplace_composition_operand absent;
        memset(&absent, 0, sizeof(absent));
        if (memcmp(reference, &absent, sizeof(absent)) != 0) {
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace missing syntax witness must have absent canonical content")));
        }
        return selected;
    }
    if (reference->multiplicity != 1u ||
        reference->relationship_metadata != 0u || reference->flags != 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace structural witness contains non-canonical reference metadata")));
    }
    if (reference->reference_kind ==
        LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
        if (reference->reference_index >= composition_input->known_entity_count ||
            composition_input->known_entities == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace structural witness known-entity reference is out of range")));
        }
        selected.entity_id = composition_input->known_entities[reference->reference_index].entity_id;
        selected.physicality_id = composition_input->known_entities[reference->reference_index].physicality_id;
    } else if (reference->reference_kind ==
        LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
        if (reference->reference_index >= execution->result_count ||
            execution->results == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace structural witness result reference is out of range")));
        }
        selected.entity_id = execution->results[reference->reference_index].entity_id;
        selected.physicality_id = execution->results[reference->reference_index].physicality_id;
    } else {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace structural witness uses an unknown canonical reference kind")));
    }
    {
        static const laplace_id128 zero_id = {{0}};
        static const laplace_digest256 zero_digest = {{0}};
        if (memcmp(selected.entity_id.bytes, zero_id.bytes, sizeof(zero_id.bytes)) == 0 ||
            memcmp(selected.physicality_id.bytes, zero_digest.bytes, sizeof(zero_digest.bytes)) == 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace structural witness lacks its native selected physicality")));
    }
    return selected;
}

static ArrayType* bytea_array(Datum* values, size_t count) {
    if (count > (size_t)INT_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace structural witness array exceeds PostgreSQL capacity")));
    }
    return construct_array(
        values, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static ArrayType* numeric_array(Datum* values, size_t count) {
    if (count > (size_t)INT_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace structural witness array exceeds PostgreSQL capacity")));
    }
    return construct_array(
        values, (int)count, NUMERICOID, -1, false, TYPALIGN_INT);
}

static uint64 structural_witness_encoded_bytes(
    const laplace_tabular_decomposition_witness* witness) {
    /* Canonical batch accounting is independent of PostgreSQL Datum layout:
     * four fixed identities, nine u64 coordinates, one length-prefixed media
     * value, and three u32 values. */
    static const uint64 fixed_bytes =
        UINT64_C(32) + UINT64_C(32) + UINT64_C(16) + UINT64_C(32) +
        UINT64_C(9) * UINT64_C(8) + UINT64_C(8) +
        UINT64_C(3) * UINT64_C(4);
    if (witness->media_type_byte_count > UINT64_MAX - fixed_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace structural witness encoded size overflowed")));
    }
    return fixed_bytes + witness->media_type_byte_count;
}

static size_t structural_witness_batch_count(
    const laplace_tabular_decomposition_witness* witnesses,
    size_t start,
    size_t count,
    uint64 preferred_batch_bytes) {
    size_t batch_count = 0u;
    uint64 encoded_bytes = 0u;
    while (batch_count < count) {
        const uint64 record_bytes = structural_witness_encoded_bytes(
            &witnesses[start + batch_count]);
        if (record_bytes > preferred_batch_bytes) {
            if (batch_count == 0u) {
                ereport(ERROR,
                        (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                         errmsg("Laplace structural witness exceeds its encoded batch authority"),
                         errdetail("record_bytes=%llu preferred_batch_bytes=%llu",
                                   (unsigned long long)record_bytes,
                                   (unsigned long long)preferred_batch_bytes)));
            }
            break;
        }
        if (encoded_bytes > preferred_batch_bytes - record_bytes) {
            break;
        }
        encoded_bytes += record_bytes;
        ++batch_count;
    }
    return batch_count;
}

void laplace_pg_persist_source_structural_witnesses(
    const laplace_tabular_source_plan* plan,
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* composition_input,
    const laplace_source_profile_manifest* profile) {
    static const char witness_domain[] =
        "laplace.source-structural-witness-set/v3";
    static const char receipt_domain[] =
        "laplace.source-structural-witness-receipt/v3";
    static const uint8_t empty_byte = 0u;
    static const char witnesses_insert_sql[] =
        "WITH input AS (SELECT $1::bytea AS source_profile_id,u.* FROM unnest("
        "$2::bytea[],$3::bytea[],$4::bytea[],$5::numeric[],$6::numeric[],"
        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::numeric[],"
        "$12::numeric[],$13::numeric[],$14::bytea[],$15::numeric[],$16::numeric[],"
        "$17::numeric[],$18::bytea[]) AS u(trace_fingerprint,provider_fingerprint,canonical_entity_id,"
        "artifact_index,span_index,parent_span_index,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags,canonical_physicality_id)) "
        "INSERT INTO " LAPLACE_PG_SCHEMA ".source_structural_witness AS s("
        "source_profile_id,artifact_index,span_index,parent_span_index,trace_fingerprint,"
        "provider_fingerprint,canonical_entity_id,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags,canonical_physicality_id) SELECT "
        "source_profile_id,artifact_index,span_index,parent_span_index,trace_fingerprint,"
        "provider_fingerprint,CASE WHEN byte_start=byte_end AND (syntax_flags::bigint & 34)<>0 "
        "THEN NULL ELSE canonical_entity_id END,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags,"
        "CASE WHEN byte_start=byte_end AND (syntax_flags::bigint & 34)<>0 "
        "THEN NULL ELSE canonical_physicality_id END FROM input "
        "ON CONFLICT (source_profile_id,artifact_index,span_index) DO UPDATE SET "
        "grammar_kind=EXCLUDED.grammar_kind,field_kind=EXCLUDED.field_kind,"
        "sibling_ordinal=EXCLUDED.sibling_ordinal,syntax_flags=EXCLUDED.syntax_flags,"
        "canonical_physicality_id=EXCLUDED.canonical_physicality_id "
        "WHERE ((s.grammar_kind IS NULL AND s.field_kind IS NULL "
        "AND s.sibling_ordinal IS NULL AND s.syntax_flags IS NULL) OR "
        "(s.canonical_physicality_id IS NULL AND EXCLUDED.canonical_physicality_id IS NOT NULL "
        "AND s.grammar_kind IS NOT DISTINCT FROM EXCLUDED.grammar_kind "
        "AND s.field_kind IS NOT DISTINCT FROM EXCLUDED.field_kind "
        "AND s.sibling_ordinal IS NOT DISTINCT FROM EXCLUDED.sibling_ordinal "
        "AND s.syntax_flags IS NOT DISTINCT FROM EXCLUDED.syntax_flags)) "
        "AND (s.canonical_physicality_id IS NULL OR "
        "s.canonical_physicality_id IS NOT DISTINCT FROM EXCLUDED.canonical_physicality_id) "
        "AND s.parent_span_index=EXCLUDED.parent_span_index "
        "AND s.trace_fingerprint=EXCLUDED.trace_fingerprint "
        "AND s.provider_fingerprint=EXCLUDED.provider_fingerprint "
        "AND s.canonical_entity_id IS NOT DISTINCT FROM EXCLUDED.canonical_entity_id "
        "AND s.byte_start=EXCLUDED.byte_start AND s.byte_end=EXCLUDED.byte_end "
        "AND s.kind=EXCLUDED.kind AND s.media_type=EXCLUDED.media_type "
        "AND s.depth=EXCLUDED.depth AND s.flags=EXCLUDED.flags";
    static const char witnesses_verify_sql[] =
        "WITH input AS (SELECT $1::bytea AS source_profile_id,u.* FROM unnest("
        "$2::bytea[],$3::bytea[],$4::bytea[],$5::numeric[],$6::numeric[],"
        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::numeric[],"
        "$12::numeric[],$13::numeric[],$14::bytea[],$15::numeric[],$16::numeric[],"
        "$17::numeric[],$18::bytea[]) AS u(trace_fingerprint,provider_fingerprint,canonical_entity_id,"
        "artifact_index,span_index,parent_span_index,byte_start,byte_end,kind,grammar_kind,"
        "field_kind,sibling_ordinal,media_type,depth,flags,syntax_flags,canonical_physicality_id)), "
        "mismatched AS (SELECT 1 FROM input i "
        "LEFT JOIN " LAPLACE_PG_SCHEMA ".source_structural_witness s ON "
        "s.source_profile_id=i.source_profile_id AND s.artifact_index=i.artifact_index "
        "AND s.span_index=i.span_index WHERE s.source_profile_id IS NULL OR "
        "s.parent_span_index<>i.parent_span_index OR s.trace_fingerprint<>i.trace_fingerprint "
        "OR s.provider_fingerprint<>i.provider_fingerprint OR "
        "s.canonical_entity_id IS DISTINCT FROM (CASE WHEN i.byte_start=i.byte_end AND "
        "(i.syntax_flags::bigint & 34)<>0 THEN NULL ELSE i.canonical_entity_id END) OR s.byte_start<>i.byte_start OR "
        "s.byte_end<>i.byte_end OR s.kind<>i.kind OR s.grammar_kind IS DISTINCT FROM i.grammar_kind OR "
        "s.field_kind IS DISTINCT FROM i.field_kind OR s.sibling_ordinal IS DISTINCT FROM i.sibling_ordinal OR "
        "s.media_type<>i.media_type OR s.depth<>i.depth OR s.flags<>i.flags OR "
        "s.syntax_flags IS DISTINCT FROM i.syntax_flags OR "
        "s.canonical_physicality_id IS DISTINCT FROM (CASE WHEN i.byte_start=i.byte_end AND "
        "(i.syntax_flags::bigint & 34)<>0 THEN NULL ELSE i.canonical_physicality_id END)) "
        "SELECT count(*) FROM mismatched";
    /* Failure-only bounded comparison: preserve the successful admission query
     * and report one exact differing row without changing stored evidence.
     * Capture both physicality records before the failing transaction rolls back;
     * float8send retains exact geometry bits, while trajectory payloads stay private. */
    static const char witnesses_diagnose_sql[] =
        "WITH input AS (SELECT $1::bytea AS source_profile_id,u.* FROM "
        "unnest($2::bytea[],$3::bytea[],$4::bytea[],$5::numeric[],$6::numeric[],$7::numeric[],$8::numeric[],$9::numeric"
        "[],$10::numeric[],$11::numeric[],$12::numeric[],$13::numeric[],$14::bytea[],$15::numeric[],$16::numeric[],$17:"
        ":numeric[],$18::bytea[]) AS u(trace_fingerprint,provider_fingerprint,canonical_entity_id,artifact_index,span_i"
        "ndex,parent_span_index,byte_start,byte_end,kind,grammar_kind,field_kind,sibling_ordinal,media_type,depth,flags"
        ",syntax_flags,canonical_physicality_id)), first_mismatch AS (SELECT "
        "jsonb_build_object('source_profile_id',encode(i.source_profile_id,'hex'),'artifact_index',i.artifact_index::te"
        "xt,'span_index',i.span_index::text,'parent_span_index',i.parent_span_index::text,'trace_fingerprint',encode(i."
        "trace_fingerprint,'hex'),'provider_fingerprint',encode(i.provider_fingerprint,'hex'),'canonical_entity_id',enc"
        "ode(CASE WHEN i.byte_start=i.byte_end AND (i.syntax_flags::bigint & 34)<>0 THEN NULL ELSE "
        "i.canonical_entity_id END,'hex'),'canonical_physicality_id',encode(CASE WHEN i.byte_start=i.byte_end AND "
        "(i.syntax_flags::bigint & 34)<>0 THEN NULL ELSE i.canonical_physicality_id "
        "END,'hex'),'byte_start',i.byte_start::text,'byte_end',i.byte_end::text,'kind',i.kind::text,'grammar_kind',i.gr"
        "ammar_kind::text,'field_kind',i.field_kind::text,'sibling_ordinal',i.sibling_ordinal::text,'media_type',encode"
        "(i.media_type,'hex'),'depth',i.depth::text,'flags',i.flags::text,'syntax_flags',i.syntax_flags::text) AS "
        "expected,jsonb_build_object('source_profile_id',encode(s.source_profile_id,'hex'),'artifact_index',s.artifact_"
        "index::text,'span_index',s.span_index::text,'parent_span_index',s.parent_span_index::text,'trace_fingerprint',"
        "encode(s.trace_fingerprint,'hex'),'provider_fingerprint',encode(s.provider_fingerprint,'hex'),'canonical_entit"
        "y_id',encode(s.canonical_entity_id,'hex'),'canonical_physicality_id',encode(s.canonical_physicality_id,'hex'),"
        "'byte_start',s.byte_start::text,'byte_end',s.byte_end::text,'kind',s.kind::text,'grammar_kind',s.grammar_kind:"
        ":text,'field_kind',s.field_kind::text,'sibling_ordinal',s.sibling_ordinal::text,'media_type',encode(s.media_ty"
        "pe,'hex'),'depth',s.depth::text,'flags',s.flags::text,'syntax_flags',s.syntax_flags::text) AS stored FROM "
        "input i LEFT JOIN " LAPLACE_PG_SCHEMA ".source_structural_witness s ON s.source_profile_id=i.source_profile_id AND "
        "s.artifact_index=i.artifact_index AND s.span_index=i.span_index WHERE s.source_profile_id IS NULL OR "
        "s.parent_span_index<>i.parent_span_index OR s.trace_fingerprint<>i.trace_fingerprint OR "
        "s.provider_fingerprint<>i.provider_fingerprint OR s.canonical_entity_id IS DISTINCT FROM (CASE WHEN "
        "i.byte_start=i.byte_end AND (i.syntax_flags::bigint & 34)<>0 THEN NULL ELSE i.canonical_entity_id END) OR "
        "s.byte_start<>i.byte_start OR s.byte_end<>i.byte_end OR s.kind<>i.kind OR s.grammar_kind IS DISTINCT FROM "
        "i.grammar_kind OR s.field_kind IS DISTINCT FROM i.field_kind OR s.sibling_ordinal IS DISTINCT FROM "
        "i.sibling_ordinal OR s.media_type<>i.media_type OR s.depth<>i.depth OR s.flags<>i.flags OR s.syntax_flags IS "
        "DISTINCT FROM i.syntax_flags OR s.canonical_physicality_id IS DISTINCT FROM (CASE WHEN i.byte_start=i.byte_end "
        "AND (i.syntax_flags::bigint & 34)<>0 THEN NULL ELSE i.canonical_physicality_id END) ORDER BY "
        "i.artifact_index,i.span_index LIMIT 1) SELECT jsonb_build_object('mismatched_fields',(SELECT jsonb_agg(key "
        "ORDER BY key) FROM jsonb_each(expected) WHERE value IS DISTINCT FROM "
        "stored->key),'expected',expected,'stored',stored,'expected_physicality',(SELECT "
        "jsonb_build_object('physicality_id',encode(p.physicality_id,'hex'),'entity_id',encode(p.entity_id,'hex'),'phys"
        "icality_type',p.physicality_type::text,'vertex_class',p.vertex_class::text,'recipe_version',p.recipe_version::"
        "text,'structural_form',p.structural_form::text,'dimension_count',p.dimension_count::text,'flags',p.flags::text"
        ",'recipe_fingerprint',encode(p.recipe_fingerprint,'hex'),'geometry_epoch',encode(p.geometry_epoch,'hex'),'traj"
        "ectory_fingerprint',encode(p.trajectory_fingerprint,'hex'),'centroid_x',encode(float8send(p.centroid_x),'hex')"
        ",'centroid_y',encode(float8send(p.centroid_y),'hex'),'centroid_z',encode(float8send(p.centroid_z),'hex'),'cent"
        "roid_m',encode(float8send(p.centroid_m),'hex'),'radius',encode(float8send(p.radius),'hex'),'logical_count',p.l"
        "ogical_count::text,'vertex_count',p.vertex_count::text,'trajectory_bytes',octet_length(p.trajectory)::text) "
        "FROM " LAPLACE_PG_SCHEMA ".physicality p WHERE p.physicality_id=decode(expected->>'canonical_physicality_id','hex')),'sto"
        "red_physicality',(SELECT jsonb_build_object('physicality_id',encode(p.physicality_id,'hex'),'entity_id',encode"
        "(p.entity_id,'hex'),'physicality_type',p.physicality_type::text,'vertex_class',p.vertex_class::text,'recipe_ve"
        "rsion',p.recipe_version::text,'structural_form',p.structural_form::text,'dimension_count',p.dimension_count::t"
        "ext,'flags',p.flags::text,'recipe_fingerprint',encode(p.recipe_fingerprint,'hex'),'geometry_epoch',encode(p.ge"
        "ometry_epoch,'hex'),'trajectory_fingerprint',encode(p.trajectory_fingerprint,'hex'),'centroid_x',encode(float8"
        "send(p.centroid_x),'hex'),'centroid_y',encode(float8send(p.centroid_y),'hex'),'centroid_z',encode(float8send(p"
        ".centroid_z),'hex'),'centroid_m',encode(float8send(p.centroid_m),'hex'),'radius',encode(float8send(p.radius),'"
        "hex'),'logical_count',p.logical_count::text,'vertex_count',p.vertex_count::text,'trajectory_bytes',octet_lengt"
        "h(p.trajectory)::text) FROM " LAPLACE_PG_SCHEMA ".physicality p WHERE "
        "p.physicality_id=decode(stored->>'canonical_physicality_id','hex')))::text FROM first_mismatch";
    static const char witnesses_count_sql[] =
        "SELECT count(*) FROM " LAPLACE_PG_SCHEMA
        ".source_structural_witness WHERE source_profile_id=$1";
    static const char receipt_insert_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA
        ".source_structural_witness_receipt(receipt_id,source_profile_id,"
        "composition_working_set_receipt,witness_fingerprint,witness_count,version) "
        "VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING";
    static const char receipt_verify_sql[] =
        "SELECT EXISTS (SELECT FROM "
        LAPLACE_PG_SCHEMA ".source_structural_witness_receipt WHERE receipt_id=$1 "
        "AND source_profile_id=$2 AND composition_working_set_receipt=$3 "
        "AND witness_fingerprint=$4 AND witness_count=$5 AND version=$6)";
    laplace_tabular_source_plan_view view;
    const laplace_tabular_decomposition_witness* witnesses;
    size_t witness_count;
    size_t media_type_byte_count;
    const uint8_t* media_types;
    laplace_digest256 witness_fingerprint;
    laplace_digest256 receipt_id;
    blake3_hasher hasher;
    size_t index;
    size_t batch_start;
    Oid witness_types[18] = {
        BYTEAOID, BYTEAARRAYOID, BYTEAARRAYOID, BYTEAARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID, BYTEAARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID, BYTEAARRAYOID};
    Datum witness_parameters[18];
    Oid receipt_types[6] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, NUMERICOID, INT4OID};
    Datum receipt_parameters[6];
    Oid count_types[1] = {BYTEAOID};
    Datum count_parameters[1];
    MemoryContext batch_context;
    int result;
    uint64 inserted_count = 0u;
    uint64 preferred_batch_bytes;

    memset(&view, 0, sizeof(view));
    if (plan == NULL || execution == NULL || composition_input == NULL ||
        profile == NULL || execution->results == NULL ||
        composition_input->known_entities == NULL ||
        laplace_tabular_source_plan_view_get(plan, &view) !=
            LAPLACE_TABULAR_SOURCE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace structural witness deposition input is incomplete")));
    }
    if (view.decomposition_witness_count == 0u) {
        return;
    }
    if (view.decomposition_witness_count > (uint64_t)INT_MAX ||
        view.decomposition_witness_media_type_byte_count > (uint64_t)SIZE_MAX ||
        view.decomposition_witnesses == NULL ||
        (view.decomposition_witness_media_type_byte_count != 0u &&
         view.decomposition_witness_media_types == NULL)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace retained structural witness view is invalid")));
    }
    witness_count = (size_t)view.decomposition_witness_count;
    media_type_byte_count =
        (size_t)view.decomposition_witness_media_type_byte_count;
    witnesses = view.decomposition_witnesses;
    media_types = view.decomposition_witness_media_types;
    preferred_batch_bytes = composition_input->preferred_batch_bytes;
    if (preferred_batch_bytes == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace structural witness batch authority is zero")));
    }

    blake3_hasher_init(&hasher);
    hash_bytes(
        &hasher, (const uint8_t*)witness_domain, sizeof(witness_domain) - 1u);
    hash_bytes(&hasher, profile->profile_id.bytes, sizeof(profile->profile_id.bytes));
    hash_u64(&hasher, (uint64_t)witness_count);

    for (index = 0u; index < witness_count; ++index) {
        const laplace_tabular_decomposition_witness* witness = &witnesses[index];
        const structural_canonical_reference canonical = canonical_reference(
            witness, execution, composition_input);
        const uint64_t media_end =
            witness->media_type_byte_offset + witness->media_type_byte_count;
        const uint8_t* media = &empty_byte;
        if (media_end < witness->media_type_byte_offset ||
            media_end > (uint64_t)media_type_byte_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace structural witness media-type range is invalid")));
        }
        if (witness->media_type_byte_count != 0u) {
            media = media_types + (size_t)witness->media_type_byte_offset;
        }

        hash_witness_row(&hasher, witness, &canonical.entity_id, &canonical.physicality_id, media);
    }
    finish_digest(&hasher, &witness_fingerprint);

    blake3_hasher_init(&hasher);
    hash_bytes(
        &hasher, (const uint8_t*)receipt_domain, sizeof(receipt_domain) - 1u);
    hash_bytes(&hasher, profile->profile_id.bytes, sizeof(profile->profile_id.bytes));
    hash_bytes(
        &hasher, execution->summary.receipt_id.bytes,
        sizeof(execution->summary.receipt_id.bytes));
    hash_bytes(
        &hasher, witness_fingerprint.bytes, sizeof(witness_fingerprint.bytes));
    hash_u64(&hasher, (uint64_t)witness_count);
    finish_digest(&hasher, &receipt_id);

    witness_parameters[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->profile_id.bytes, sizeof(profile->profile_id.bytes)));
    count_parameters[0] = witness_parameters[0];

    receipt_parameters[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt_id.bytes, sizeof(receipt_id.bytes)));
    receipt_parameters[1] = witness_parameters[0];
    receipt_parameters[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution->summary.receipt_id.bytes,
        sizeof(execution->summary.receipt_id.bytes)));
    receipt_parameters[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        witness_fingerprint.bytes, sizeof(witness_fingerprint.bytes)));
    receipt_parameters[4] = laplace_pg_numeric_from_uint64((uint64_t)witness_count);
    receipt_parameters[5] = Int32GetDatum(3);

    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace structural witness deposition could not connect")));
    }
    batch_context = AllocSetContextCreate(
        CurrentMemoryContext,
        "Laplace structural witness bounded batch",
        ALLOCSET_DEFAULT_SIZES);
    for (batch_start = 0u; batch_start < witness_count;) {
        const size_t remaining = witness_count - batch_start;
        const size_t batch_count = structural_witness_batch_count(
            witnesses, batch_start, remaining, preferred_batch_bytes);
        MemoryContext prior_context = MemoryContextSwitchTo(batch_context);
        Datum* trace_values = (Datum*)palloc(sizeof(*trace_values) * batch_count);
        Datum* provider_values = (Datum*)palloc(sizeof(*provider_values) * batch_count);
        Datum* entity_values = (Datum*)palloc(sizeof(*entity_values) * batch_count);
        Datum* physicality_values = (Datum*)palloc(sizeof(*physicality_values) * batch_count);
        Datum* artifact_values = (Datum*)palloc(sizeof(*artifact_values) * batch_count);
        Datum* span_values = (Datum*)palloc(sizeof(*span_values) * batch_count);
        Datum* parent_values = (Datum*)palloc(sizeof(*parent_values) * batch_count);
        Datum* start_values = (Datum*)palloc(sizeof(*start_values) * batch_count);
        Datum* end_values = (Datum*)palloc(sizeof(*end_values) * batch_count);
        Datum* kind_values = (Datum*)palloc(sizeof(*kind_values) * batch_count);
        Datum* grammar_kind_values = (Datum*)palloc(sizeof(*grammar_kind_values) * batch_count);
        Datum* field_kind_values = (Datum*)palloc(sizeof(*field_kind_values) * batch_count);
        Datum* sibling_ordinal_values = (Datum*)palloc(sizeof(*sibling_ordinal_values) * batch_count);
        Datum* media_values = (Datum*)palloc(sizeof(*media_values) * batch_count);
        Datum* depth_values = (Datum*)palloc(sizeof(*depth_values) * batch_count);
        Datum* flag_values = (Datum*)palloc(sizeof(*flag_values) * batch_count);
        Datum* syntax_flag_values = (Datum*)palloc(sizeof(*syntax_flag_values) * batch_count);
        size_t batch_index;

        for (batch_index = 0u; batch_index < batch_count; ++batch_index) {
            const laplace_tabular_decomposition_witness* witness =
                &witnesses[batch_start + batch_index];
            const structural_canonical_reference canonical = canonical_reference(
                witness, execution, composition_input);
            const uint64_t media_end =
                witness->media_type_byte_offset + witness->media_type_byte_count;
            const uint8_t* media = &empty_byte;
            if (media_end < witness->media_type_byte_offset ||
                media_end > (uint64_t)media_type_byte_count) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace structural witness media-type range is invalid")));
            }
            if (witness->media_type_byte_count != 0u) {
                media = media_types + (size_t)witness->media_type_byte_offset;
            }
            trace_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                witness->trace_fingerprint.bytes,
                sizeof(witness->trace_fingerprint.bytes)));
            provider_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                witness->provider_fingerprint.bytes,
                sizeof(witness->provider_fingerprint.bytes)));
            entity_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                canonical.entity_id.bytes, sizeof(canonical.entity_id.bytes)));
            physicality_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                canonical.physicality_id.bytes, sizeof(canonical.physicality_id.bytes)));
            artifact_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->artifact_index);
            span_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->span_index);
            parent_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->parent_span_index);
            start_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->byte_start);
            end_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->byte_end);
            kind_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->kind);
            grammar_kind_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->grammar_kind);
            field_kind_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->field_kind);
            sibling_ordinal_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->sibling_ordinal);
            media_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                media, (size_t)witness->media_type_byte_count));
            depth_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->depth);
            flag_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->flags);
            syntax_flag_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->syntax_flags);
        }
        witness_parameters[1] = PointerGetDatum(bytea_array(trace_values, batch_count));
        witness_parameters[2] = PointerGetDatum(bytea_array(provider_values, batch_count));
        witness_parameters[3] = PointerGetDatum(bytea_array(entity_values, batch_count));
        witness_parameters[4] = PointerGetDatum(numeric_array(artifact_values, batch_count));
        witness_parameters[5] = PointerGetDatum(numeric_array(span_values, batch_count));
        witness_parameters[6] = PointerGetDatum(numeric_array(parent_values, batch_count));
        witness_parameters[7] = PointerGetDatum(numeric_array(start_values, batch_count));
        witness_parameters[8] = PointerGetDatum(numeric_array(end_values, batch_count));
        witness_parameters[9] = PointerGetDatum(numeric_array(kind_values, batch_count));
        witness_parameters[10] = PointerGetDatum(numeric_array(grammar_kind_values, batch_count));
        witness_parameters[11] = PointerGetDatum(numeric_array(field_kind_values, batch_count));
        witness_parameters[12] = PointerGetDatum(numeric_array(sibling_ordinal_values, batch_count));
        witness_parameters[13] = PointerGetDatum(bytea_array(media_values, batch_count));
        witness_parameters[14] = PointerGetDatum(numeric_array(depth_values, batch_count));
        witness_parameters[15] = PointerGetDatum(numeric_array(flag_values, batch_count));
        witness_parameters[16] = PointerGetDatum(numeric_array(syntax_flag_values, batch_count));
        witness_parameters[17] = PointerGetDatum(bytea_array(physicality_values, batch_count));
        MemoryContextSwitchTo(prior_context);

        result = SPI_execute_with_args(
            witnesses_insert_sql, 18, witness_types, witness_parameters,
            NULL, false, 0);
        if (result != SPI_OK_INSERT ||
            UINT64_MAX - inserted_count < (uint64_t)SPI_processed) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace structural witness deposition failed"),
                     errdetail("SPI status=%d", result)));
        }
        inserted_count += (uint64_t)SPI_processed;
        CommandCounterIncrement();
        result = SPI_execute_with_args(
            witnesses_verify_sql, 18, witness_types, witness_parameters,
            NULL, false, 1);
        {
            const int verification_status = result;
            const int64 mismatch_count =
                result == SPI_OK_SELECT ? spi_int64_column(1) : -1;
            if (verification_status != SPI_OK_SELECT || mismatch_count != 0) {
                char* first_mismatch = NULL;
                if (verification_status == SPI_OK_SELECT && mismatch_count > 0) {
                    const int diagnostic_status = SPI_execute_with_args(
                        witnesses_diagnose_sql, 18, witness_types, witness_parameters,
                        NULL, false, 1);
                    if (diagnostic_status == SPI_OK_SELECT &&
                        SPI_processed == 1u && SPI_tuptable != NULL) {
                        first_mismatch = SPI_getvalue(
                            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);
                    }
                }
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace structural witness deposition/readback diverged"),
                         errdetail("batch_start=%llu batch_count=%llu inserted_total=%llu "
                                   "verification_status=%d mismatch_count=%lld first_mismatch=%s",
                                   (unsigned long long)batch_start,
                                   (unsigned long long)batch_count,
                                   (unsigned long long)inserted_count,
                                   verification_status, (long long)mismatch_count,
                                   first_mismatch != NULL ? first_mismatch : "unavailable")));
            }
        }
        batch_start += batch_count;
        MemoryContextReset(batch_context);
    }
    MemoryContextDelete(batch_context);
    result = SPI_execute_with_args(
        witnesses_count_sql, 1, count_types, count_parameters, NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        spi_int64_column(1) != (int64)witness_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace structural witness set cardinality diverged"),
                 errdetail("expected=%llu stored=%lld inserted=%llu",
                           (unsigned long long)witness_count,
                           (long long)spi_int64_column(1),
                           (unsigned long long)inserted_count)));
    }
    result = SPI_execute_with_args(
        receipt_insert_sql, 6, receipt_types, receipt_parameters, NULL, false, 0);
    if (result != SPI_OK_INSERT) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace structural witness receipt deposition failed"),
                 errdetail("SPI status=%d", result)));
    }
    CommandCounterIncrement();
    result = SPI_execute_with_args(
        receipt_verify_sql, 6, receipt_types, receipt_parameters, NULL, false, 1);
    if (result != SPI_OK_SELECT || !spi_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace structural witness receipt replay diverged")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace structural witness deposition could not close")));
    }
}

static Datum structural_column(HeapTuple tuple, TupleDesc descriptor, int column) {
    bool is_null;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
        errmsg("Laplace retained structural receipt has a null required field")));
    return value;
}

static void structural_exact(Datum value, void* target, size_t width) {
    bytea* bytes = DatumGetByteaPP(value);
    if ((size_t)VARSIZE_ANY_EXHDR(bytes) != width)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace retained structural identity has an invalid width")));
    memcpy(target, VARDATA_ANY(bytes), width);
}

void laplace_pg_verify_source_structural_roots(
    const laplace_digest256* profile_id, const laplace_digest256* receipt_id,
    const uint64_t* artifact_indexes, size_t artifact_count, uint64_t maximum_witnesses,
    laplace_pg_source_readback_binding* bindings) {
    static const char receipt_sql[] =
        "SELECT p,r.composition_working_set_receipt,r.witness_fingerprint,r.witness_count "
        "FROM " LAPLACE_PG_SCHEMA ".source_structural_witness_receipt r "
        "JOIN " LAPLACE_PG_SCHEMA ".source_profile p ON p.profile_id=r.source_profile_id "
        "JOIN " LAPLACE_PG_SCHEMA ".composition_execution_receipt c "
        "ON c.working_set_receipt=r.composition_working_set_receipt "
        "WHERE r.source_profile_id=$1 AND r.receipt_id=$2 AND r.version=3";
    static const char rows_sql[] =
        "SELECT trace_fingerprint,provider_fingerprint,canonical_entity_id,artifact_index,"
        "span_index,parent_span_index,byte_start,byte_end,kind,grammar_kind,field_kind,"
        "sibling_ordinal,CASE WHEN octet_length(media_type)<=127 THEN media_type ELSE NULL END,depth,flags,syntax_flags,canonical_physicality_id FROM " LAPLACE_PG_SCHEMA
        ".source_structural_witness WHERE source_profile_id=$1 ORDER BY artifact_index,span_index";
    static const char witness_domain[] = "laplace.source-structural-witness-set/v3";
    static const char receipt_domain[] = "laplace.source-structural-witness-receipt/v3";
    Oid types[2] = {BYTEAOID, BYTEAOID};
    Datum values[2];
    laplace_source_profile_manifest profile;
    laplace_source_profile_receipt profile_receipt;
    laplace_source_profile_error profile_error;
    laplace_digest256 expected_witness, actual_witness, actual_receipt;
    blake3_hasher hasher;
    SPIPlanPtr plan;
    Portal cursor;
    uint64_t count = 0u;
    laplace_pg_source_readback_binding* binding = bindings;
    bool* selected;
    size_t selected_index;
    size_t next_selected = 0u;
    int result;
    if (artifact_indexes == NULL || bindings == NULL || artifact_count == 0u || artifact_count > 4096u)
        ereport(ERROR,(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),errmsg("Laplace source readback artifact count is outside its finite bound")));
    for (selected_index=1u; selected_index<artifact_count; ++selected_index)
        if (artifact_indexes[selected_index-1u]>=artifact_indexes[selected_index])
            ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),errmsg("Laplace source readback indexes must be sorted and unique")));
    memset(bindings, 0, sizeof(*bindings)*artifact_count);
    selected = (bool*)palloc0(sizeof(*selected)*artifact_count);
    memset(&profile, 0, sizeof(profile));
    if (maximum_witnesses == 0u) ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
        errmsg("Laplace source readback requires a finite nonzero witness bound")));
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile_id->bytes, 32u));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt_id->bytes, 32u));
    result = SPI_execute_with_args(receipt_sql, 2, types, values, NULL, true, 2);
    ++binding->database_operations;
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace source readback requires one retained v3 structural receipt with native physicality bindings"),
            errhint("Replay source admission to obtain exact native physicality bindings for older receipts.")));
    {
        HeapTuple tuple = SPI_tuptable->vals[0];
        TupleDesc descriptor = SPI_tuptable->tupdesc;
        laplace_pg_read_source_profile(DatumGetHeapTupleHeader(
            structural_column(tuple, descriptor, 1)), &profile);
        structural_exact(structural_column(tuple, descriptor, 2),
            &binding->composition_receipt_id, 32u);
        structural_exact(structural_column(tuple, descriptor, 3), &expected_witness, 32u);
        binding->witness_count = laplace_pg_uint64_from_numeric(
            structural_column(tuple, descriptor, 4), "structural witness count");
    }
    SPI_freetuptable(SPI_tuptable);
    if (laplace_source_profile_validate_batch(&profile, 1u, &profile_receipt, &profile_error) !=
            LAPLACE_SOURCE_PROFILE_OK || memcmp(profile.profile_id.bytes, profile_id->bytes,32u) != 0 ||
        artifact_indexes[artifact_count-1u] >= profile.file_count || binding->witness_count == 0u)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace source readback profile identity or artifact boundary is invalid")));
    if (binding->witness_count > maximum_witnesses)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace source witness set exceeds the admitted readback bound")));
    binding->recipe_id = profile.recipe_program_fingerprint;
    blake3_hasher_init(&hasher);
    hash_bytes(&hasher, (const uint8_t*)witness_domain, sizeof(witness_domain)-1u);
    hash_bytes(&hasher, profile_id->bytes, 32u);
    hash_u64(&hasher, binding->witness_count);
    plan = SPI_prepare(rows_sql, 1, types);
    if (plan == NULL) ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
        errmsg("Laplace structural readback could not prepare its bounded cursor")));
    cursor = SPI_cursor_open(NULL, plan, values, NULL, true);
    ++binding->database_operations;
    SPI_freeplan(plan);
    if (cursor == NULL) ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
        errmsg("Laplace structural readback could not open its bounded cursor")));
    for (;;) {
        uint64 row;
        SPI_cursor_fetch(cursor, true, 128);
        ++binding->database_operations;
        if (SPI_processed == 0u) { if (SPI_tuptable != NULL) SPI_freetuptable(SPI_tuptable); break; }
        for (row = 0u; row < SPI_processed; ++row) {
            HeapTuple tuple = SPI_tuptable->vals[row];
            TupleDesc descriptor = SPI_tuptable->tupdesc;
            laplace_tabular_decomposition_witness witness;
            laplace_id128 entity;
            laplace_digest256 physicality;
            bytea* media;
            bool absent;
            bool physicality_absent;
            Datum canonical;
            uint64_t coordinates[12];
            int index;
            memset(&witness, 0, sizeof(witness));
            memset(&entity, 0, sizeof(entity));
            memset(&physicality, 0, sizeof(physicality));
            CHECK_FOR_INTERRUPTS();
            if (++count > binding->witness_count || count > maximum_witnesses)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace retained structural witness count exceeds its receipt")));
            structural_exact(structural_column(tuple, descriptor, 1), &witness.trace_fingerprint,32u);
            structural_exact(structural_column(tuple, descriptor, 2), &witness.provider_fingerprint,32u);
            canonical = SPI_getbinval(tuple, descriptor, 3, &absent);
            if (!absent) structural_exact(canonical, &entity,16u);
            canonical = SPI_getbinval(tuple, descriptor, 17, &physicality_absent);
            if (!physicality_absent) structural_exact(canonical, &physicality,32u);
            if (absent != physicality_absent)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace v3 structural witness lacks its exact physicality binding")));
            for (index=0; index<9; ++index) coordinates[index] = laplace_pg_uint64_from_numeric(
                structural_column(tuple, descriptor, index+4), "structural witness coordinate");
            for (index=9; index<12; ++index) coordinates[index] = laplace_pg_uint64_from_numeric(
                structural_column(tuple, descriptor, index+5), "structural witness flags");
            witness.artifact_index=coordinates[0]; witness.span_index=coordinates[1];
            witness.parent_span_index=coordinates[2]; witness.byte_start=coordinates[3];
            witness.byte_end=coordinates[4]; witness.kind=coordinates[5];
            witness.grammar_kind=coordinates[6]; witness.field_kind=coordinates[7];
            witness.sibling_ordinal=coordinates[8];
            if (coordinates[9]>UINT32_MAX || coordinates[10]>UINT32_MAX || coordinates[11]>UINT32_MAX)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace structural flags overflow")));
            witness.depth=(uint32_t)coordinates[9]; witness.flags=(uint32_t)coordinates[10];
            witness.syntax_flags=(uint32_t)coordinates[11];
            media=DatumGetByteaPP(structural_column(tuple, descriptor,13));
            witness.media_type_byte_count=(uint64_t)VARSIZE_ANY_EXHDR(media);
            if (witness.byte_start > witness.byte_end ||
                (absent != (witness.byte_start==witness.byte_end)) ||
                (absent && !(witness.syntax_flags &
                    (LAPLACE_DECOMPOSITION_SYNTAX_MISSING | LAPLACE_DECOMPOSITION_SYNTAX_EMPTY))))
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace structural content absence disagrees with its syntax span")));
            hash_witness_row(&hasher,&witness,&entity,&physicality,(const uint8_t*)VARDATA_ANY(media));
            if (witness.span_index == 0u) {
                while (next_selected < artifact_count &&
                    artifact_indexes[next_selected] < witness.artifact_index) ++next_selected;
                if (next_selected == artifact_count ||
                    artifact_indexes[next_selected] != witness.artifact_index) continue;
                selected_index = next_selected++;
                if (selected[selected_index] || absent || witness.byte_start!=0u || witness.byte_end==0u ||
                    witness.parent_span_index!=UINT64_MAX || witness.depth!=0u ||
                    !(witness.flags & LAPLACE_DECOMPOSITION_SPAN_TEXT))
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace source readback root is not an exact Unicode artifact span")));
                selected[selected_index]=true;
                bindings[selected_index].root_content_id=entity;
                bindings[selected_index].root_physicality_id=physicality;
                bindings[selected_index].byte_count=witness.byte_end;
            }
        }
        SPI_freetuptable(SPI_tuptable);
    }
    SPI_cursor_close(cursor);
    finish_digest(&hasher,&actual_witness);
    for (selected_index=0u; selected_index<artifact_count; ++selected_index) {
        if (!selected[selected_index]) ereport(ERROR,(errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace source readback is missing a requested artifact root")));
        if (selected_index!=0u) {
            bindings[selected_index].recipe_id=binding->recipe_id;
            bindings[selected_index].composition_receipt_id=binding->composition_receipt_id;
            bindings[selected_index].witness_count=binding->witness_count;
        }
    }
    pfree(selected);
    if (count!=binding->witness_count || memcmp(actual_witness.bytes,expected_witness.bytes,32u)!=0)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace retained structural witnesses no longer match their receipt")));
    blake3_hasher_init(&hasher);
    hash_bytes(&hasher,(const uint8_t*)receipt_domain,sizeof(receipt_domain)-1u);
    hash_bytes(&hasher,profile_id->bytes,32u);
    hash_bytes(&hasher,binding->composition_receipt_id.bytes,32u);
    hash_bytes(&hasher,actual_witness.bytes,32u);
    hash_u64(&hasher,count);
    finish_digest(&hasher,&actual_receipt);
    if (memcmp(actual_receipt.bytes,receipt_id->bytes,32u)!=0)
        ereport(ERROR,(errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace structural receipt identity failed native recomputation")));
}

void laplace_pg_verify_source_structural_root(
    const laplace_digest256* profile_id, const laplace_digest256* receipt_id,
    uint64_t artifact_index, uint64_t maximum_witnesses,
    laplace_pg_source_readback_binding* binding) {
    laplace_pg_verify_source_structural_roots(profile_id, receipt_id, &artifact_index,
        1u, maximum_witnesses, binding);
}
