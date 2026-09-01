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

static laplace_id128 canonical_entity_id(
    const laplace_composition_operand* reference,
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* composition_input) {
    laplace_id128 entity_id;
    memset(&entity_id, 0, sizeof(entity_id));
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
        return composition_input->known_entities[reference->reference_index].entity_id;
    }
    if (reference->reference_kind ==
        LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
        if (reference->reference_index >= execution->result_count ||
            execution->results == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace structural witness result reference is out of range")));
        }
        return execution->results[reference->reference_index].entity_id;
    }
    ereport(ERROR,
            (errcode(ERRCODE_DATA_CORRUPTED),
             errmsg("Laplace structural witness uses an unknown canonical reference kind")));
    return entity_id;
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
     * three fixed identities, six u64 coordinates, one length-prefixed media
     * value, and two u32 values. */
    static const uint64 fixed_bytes =
        UINT64_C(32) + UINT64_C(32) + UINT64_C(16) +
        UINT64_C(6) * UINT64_C(8) + UINT64_C(8) +
        UINT64_C(2) * UINT64_C(4);
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
        "laplace.source-structural-witness-set/v1";
    static const char receipt_domain[] =
        "laplace.source-structural-witness-receipt/v1";
    static const uint8_t empty_byte = 0u;
    static const char witnesses_insert_sql[] =
        "WITH input AS (SELECT $1::bytea AS source_profile_id,u.* FROM unnest("
        "$2::bytea[],$3::bytea[],$4::bytea[],$5::numeric[],$6::numeric[],"
        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::bytea[],"
        "$12::numeric[],$13::numeric[]) AS u(trace_fingerprint,provider_fingerprint,"
        "canonical_entity_id,artifact_index,span_index,parent_span_index,byte_start,"
        "byte_end,kind,media_type,depth,flags)) "
        "INSERT INTO " LAPLACE_PG_SCHEMA ".source_structural_witness("
        "source_profile_id,artifact_index,span_index,parent_span_index,trace_fingerprint,"
        "provider_fingerprint,canonical_entity_id,byte_start,byte_end,kind,media_type,"
        "depth,flags) SELECT source_profile_id,artifact_index,span_index,parent_span_index,"
        "trace_fingerprint,provider_fingerprint,canonical_entity_id,byte_start,byte_end,"
        "kind,media_type,depth,flags FROM input ON CONFLICT DO NOTHING";
    static const char witnesses_verify_sql[] =
        "WITH input AS (SELECT $1::bytea AS source_profile_id,u.* FROM unnest("
        "$2::bytea[],$3::bytea[],$4::bytea[],$5::numeric[],$6::numeric[],"
        "$7::numeric[],$8::numeric[],$9::numeric[],$10::numeric[],$11::bytea[],"
        "$12::numeric[],$13::numeric[]) AS u(trace_fingerprint,provider_fingerprint,"
        "canonical_entity_id,artifact_index,span_index,parent_span_index,byte_start,"
        "byte_end,kind,media_type,depth,flags)), "
        "mismatched AS (SELECT 1 FROM input i "
        "LEFT JOIN " LAPLACE_PG_SCHEMA ".source_structural_witness s ON "
        "s.source_profile_id=i.source_profile_id AND s.artifact_index=i.artifact_index "
        "AND s.span_index=i.span_index WHERE s.source_profile_id IS NULL OR "
        "s.parent_span_index<>i.parent_span_index OR s.trace_fingerprint<>i.trace_fingerprint "
        "OR s.provider_fingerprint<>i.provider_fingerprint OR "
        "s.canonical_entity_id<>i.canonical_entity_id OR s.byte_start<>i.byte_start OR "
        "s.byte_end<>i.byte_end OR s.kind<>i.kind OR s.media_type<>i.media_type OR "
        "s.depth<>i.depth OR s.flags<>i.flags) "
        "SELECT count(*) FROM mismatched";
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
    Oid witness_types[13] = {
        BYTEAOID, BYTEAARRAYOID, BYTEAARRAYOID, BYTEAARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID, NUMERICARRAYOID, BYTEAARRAYOID, NUMERICARRAYOID,
        NUMERICARRAYOID};
    Datum witness_parameters[13];
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
        const laplace_id128 entity_id = canonical_entity_id(
            &witness->canonical_content, execution, composition_input);
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

        hash_bytes(
            &hasher, witness->trace_fingerprint.bytes,
            sizeof(witness->trace_fingerprint.bytes));
        hash_bytes(
            &hasher, witness->provider_fingerprint.bytes,
            sizeof(witness->provider_fingerprint.bytes));
        hash_bytes(&hasher, entity_id.bytes, sizeof(entity_id.bytes));
        hash_u64(&hasher, witness->artifact_index);
        hash_u64(&hasher, witness->span_index);
        hash_u64(&hasher, witness->parent_span_index);
        hash_u64(&hasher, witness->byte_start);
        hash_u64(&hasher, witness->byte_end);
        hash_u64(&hasher, witness->kind);
        hash_bytes(&hasher, media, (size_t)witness->media_type_byte_count);
        hash_u32(&hasher, witness->depth);
        hash_u32(&hasher, witness->flags);
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
    receipt_parameters[5] = Int32GetDatum(1);

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
        Datum* artifact_values = (Datum*)palloc(sizeof(*artifact_values) * batch_count);
        Datum* span_values = (Datum*)palloc(sizeof(*span_values) * batch_count);
        Datum* parent_values = (Datum*)palloc(sizeof(*parent_values) * batch_count);
        Datum* start_values = (Datum*)palloc(sizeof(*start_values) * batch_count);
        Datum* end_values = (Datum*)palloc(sizeof(*end_values) * batch_count);
        Datum* kind_values = (Datum*)palloc(sizeof(*kind_values) * batch_count);
        Datum* media_values = (Datum*)palloc(sizeof(*media_values) * batch_count);
        Datum* depth_values = (Datum*)palloc(sizeof(*depth_values) * batch_count);
        Datum* flag_values = (Datum*)palloc(sizeof(*flag_values) * batch_count);
        size_t batch_index;

        for (batch_index = 0u; batch_index < batch_count; ++batch_index) {
            const laplace_tabular_decomposition_witness* witness =
                &witnesses[batch_start + batch_index];
            const laplace_id128 entity_id = canonical_entity_id(
                &witness->canonical_content, execution, composition_input);
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
                entity_id.bytes, sizeof(entity_id.bytes)));
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
            media_values[batch_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                media, (size_t)witness->media_type_byte_count));
            depth_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->depth);
            flag_values[batch_index] =
                laplace_pg_numeric_from_uint64(witness->flags);
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
        witness_parameters[10] = PointerGetDatum(bytea_array(media_values, batch_count));
        witness_parameters[11] = PointerGetDatum(numeric_array(depth_values, batch_count));
        witness_parameters[12] = PointerGetDatum(numeric_array(flag_values, batch_count));
        MemoryContextSwitchTo(prior_context);

        result = SPI_execute_with_args(
            witnesses_insert_sql, 13, witness_types, witness_parameters,
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
            witnesses_verify_sql, 13, witness_types, witness_parameters,
            NULL, false, 1);
        if (result != SPI_OK_SELECT || spi_int64_column(1) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace structural witness deposition/readback diverged"),
                     errdetail("batch_start=%llu batch_count=%llu inserted_total=%llu",
                               (unsigned long long)batch_start,
                               (unsigned long long)batch_count,
                               (unsigned long long)inserted_count)));
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
