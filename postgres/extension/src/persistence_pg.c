#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/persistence.h"
#include "laplace_pg_internal.h"
#include "persistence_rows_pg.h"
#include "set_pg.h"

PG_FUNCTION_INFO_V1(LAPLACE_PG_PERSISTENCE_DEPOSIT_SYMBOL);

typedef struct staged_record {
    laplace_persistence_record record;
    const uint8_t* frame;
} staged_record;

typedef struct persistence_sink_state {
    staged_record* records;
    uint64_t capacity;
    uint64_t count;
    uint64_t next_logical_ordinal;
    laplace_persistence_summary summary;
    uint64_t inserted[4];
    uint32_t plan_ids[LAPLACE_PERSISTENCE_PG_PLAN_COUNT];
    laplace_digest256 plan_sequence_fingerprint;
    uint32_t plan_count;
    int spi_connected;
} persistence_sink_state;

static SPIPlanPtr reference_plan = NULL;
static SPIPlanPtr insert_plans[4] = {NULL, NULL, NULL, NULL};
static SPIPlanPtr verify_plans[4] = {NULL, NULL, NULL, NULL};
static SPIPlanPtr deposit_receipt_insert_plan = NULL;
static SPIPlanPtr deposit_receipt_verify_plan = NULL;

static const char* const record_type_names[4] = {
    "canonical_entity_record",
    "physicality_record",
    "trajectory_vertex_record",
    "observed_occurrence_record"};

static void note_plan(persistence_sink_state* state, uint32_t plan_id) {
    if (state->plan_count >= LAPLACE_PERSISTENCE_PG_PLAN_COUNT || plan_id == 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence plan sequence exceeded its contract")));
    }
    state->plan_ids[state->plan_count++] = plan_id;
}

static ArrayType* build_entity_array(const persistence_sink_state* state) {
    laplace_pg_composite_binding binding;
    Datum* rows = state->summary.entity_count == 0 ? NULL :
        (Datum*)palloc(sizeof(*rows) * (size_t)state->summary.entity_count);
    ArrayType* result;
    uint64_t row = 0;
    uint64_t index;
    laplace_pg_entity_binding_open(&binding);
    for (index = 0; index < state->count; ++index) {
        const staged_record* staged = &state->records[index];
        if (staged->record.kind == LAPLACE_PERSISTENCE_RECORD_ENTITY) {
            rows[row++] = laplace_pg_entity_record(
                &binding, &staged->record.value.entity);
        }
    }
    result = laplace_pg_composite_array(&binding, rows, row);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* build_physicality_array(const persistence_sink_state* state) {
    laplace_pg_composite_binding binding;
    Datum* rows = state->summary.physicality_count == 0 ? NULL :
        (Datum*)palloc(sizeof(*rows) *
                      (size_t)state->summary.physicality_count);
    ArrayType* result;
    uint64_t row = 0;
    uint64_t index;
    laplace_pg_physicality_binding_open(&binding);
    for (index = 0; index < state->count; ++index) {
        const staged_record* staged = &state->records[index];
        if (staged->record.kind == LAPLACE_PERSISTENCE_RECORD_PHYSICALITY) {
            rows[row++] = laplace_pg_physicality_record(
                &binding, &staged->record.value.physicality);
        }
    }
    result = laplace_pg_composite_array(&binding, rows, row);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* build_trajectory_array(const persistence_sink_state* state) {
    static const Oid attribute_types[11] = {
        BYTEAOID, NUMERICOID, BYTEAOID, BYTEAOID, NUMERICOID,
        INT8OID, INT8OID, INT4OID, INT4OID, INT2OID, BOOLOID};
    static const int32 attribute_typmods[11] = {
        -1, LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1, -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = state->summary.trajectory_vertex_count == 0 ? NULL :
        (Datum*)palloc(sizeof(*rows) *
                      (size_t)state->summary.trajectory_vertex_count);
    ArrayType* result;
    uint64_t row = 0;
    uint64_t index;
    laplace_pg_composite_binding_open(
        record_type_names[2], attribute_types, attribute_typmods,
        11, &binding);
    for (index = 0; index < state->count; ++index) {
        const staged_record* staged = &state->records[index];
        if (staged->record.kind == LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX) {
            const laplace_persistence_trajectory_record* value =
                &staged->record.value.trajectory;
            Datum fields[11];
            bool nulls[11] = {false};
            fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->physicality_id.bytes, sizeof(value->physicality_id.bytes)));
            fields[1] = laplace_pg_numeric_from_uint64(value->vertex_index);
            fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                staged->frame + LAPLACE_PERSISTENCE_FRAME_HEADER_BYTES + 40u,
                sizeof(value->carrier)));
            fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->occurrence.entity_id.bytes,
                sizeof(value->occurrence.entity_id.bytes)));
            fields[4] = laplace_pg_numeric_from_uint64(
                value->occurrence.logical_ordinal);
            fields[5] = Int64GetDatum(laplace_pg_checked_int64(
                value->occurrence.metadata, "trajectory metadata"));
            fields[6] = Int64GetDatum((int64)value->occurrence.atom);
            fields[7] = Int32GetDatum((int32)value->occurrence.packed_ordinal);
            fields[8] = Int32GetDatum((int32)value->occurrence.run_length);
            fields[9] = Int16GetDatum((int16)value->occurrence.tier);
            fields[10] = BoolGetDatum(value->occurrence.has_atom != 0);
            rows[row++] = laplace_pg_composite_record(&binding, fields, nulls);
        }
    }
    result = laplace_pg_composite_array(&binding, rows, row);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* build_occurrence_array(const persistence_sink_state* state) {
    static const Oid attribute_types[7] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, INT4OID};
    static const int32 attribute_typmods[7] = {
        -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1};
    laplace_pg_composite_binding binding;
    Datum* rows = state->summary.occurrence_count == 0 ? NULL :
        (Datum*)palloc(sizeof(*rows) * (size_t)state->summary.occurrence_count);
    ArrayType* result;
    uint64_t row = 0;
    uint64_t index;
    laplace_pg_composite_binding_open(
        record_type_names[3], attribute_types, attribute_typmods,
        7, &binding);
    for (index = 0; index < state->count; ++index) {
        const staged_record* staged = &state->records[index];
        if (staged->record.kind == LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE) {
            const laplace_persistence_occurrence_record* value =
                &staged->record.value.occurrence;
            const bool has_physicality =
                (value->flags & LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY) != 0;
            Datum fields[7];
            bool nulls[7] = {false};
            fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->occurrence_id.bytes, sizeof(value->occurrence_id.bytes)));
            fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->entity_id.bytes, sizeof(value->entity_id.bytes)));
            if (has_physicality) {
                fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                    value->physicality_id.bytes, sizeof(value->physicality_id.bytes)));
            } else {
                fields[2] = (Datum)0;
                nulls[2] = true;
            }
            fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->source_fingerprint.bytes,
                sizeof(value->source_fingerprint.bytes)));
            fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->context_fingerprint.bytes,
                sizeof(value->context_fingerprint.bytes)));
            fields[5] = laplace_pg_numeric_from_uint64(value->source_ordinal);
            fields[6] = Int32GetDatum((int32)value->flags);
            rows[row++] = laplace_pg_composite_record(&binding, fields, nulls);
        }
    }
    result = laplace_pg_composite_array(&binding, rows, row);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static void ensure_reference_plan(void) {
    static const char sql[] =
        "WITH incoming_entity AS (SELECT entity_id FROM unnest($1::" LAPLACE_PG_SCHEMA ".canonical_entity_record[])), "
        "incoming_physicality AS (SELECT physicality_id, entity_id FROM unnest($2::" LAPLACE_PG_SCHEMA ".physicality_record[])), "
        "incoming_vertex AS (SELECT physicality_id, constituent_entity_id FROM unnest($3::" LAPLACE_PG_SCHEMA ".trajectory_vertex_record[])), "
        "incoming_occurrence AS (SELECT entity_id, physicality_id FROM unnest($4::" LAPLACE_PG_SCHEMA ".observed_occurrence_record[])), "
        "entity_reference AS (SELECT entity_id FROM incoming_physicality UNION SELECT constituent_entity_id FROM incoming_vertex UNION SELECT entity_id FROM incoming_occurrence), "
        "physicality_reference AS (SELECT physicality_id FROM incoming_vertex UNION SELECT physicality_id FROM incoming_occurrence WHERE physicality_id IS NOT NULL) "
        "SELECT "
        "(SELECT count(*) FROM entity_reference r LEFT JOIN " LAPLACE_PG_SCHEMA ".canonical_entity e ON e.entity_id=r.entity_id "
        " WHERE e.entity_id IS NULL AND NOT EXISTS (SELECT 1 FROM incoming_entity i WHERE i.entity_id=r.entity_id)), "
        "(SELECT count(*) FROM physicality_reference r LEFT JOIN " LAPLACE_PG_SCHEMA ".physicality p ON p.physicality_id=r.physicality_id "
        " WHERE p.physicality_id IS NULL AND NOT EXISTS (SELECT 1 FROM incoming_physicality i WHERE i.physicality_id=r.physicality_id))";
    Oid types[4] = {
        laplace_pg_composite_array_oid(record_type_names[0]),
        laplace_pg_composite_array_oid(record_type_names[1]),
        laplace_pg_composite_array_oid(record_type_names[2]),
        laplace_pg_composite_array_oid(record_type_names[3])};
    laplace_pg_keep_plan(&reference_plan, sql, 4, types);
}

static const char* insert_sql(size_t kind) {
    static const char* const statements[4] = {
        NULL,
        NULL,
        "INSERT INTO " LAPLACE_PG_SCHEMA ".composition_trajectory_vertex SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA ".trajectory_vertex_record[]) ON CONFLICT DO NOTHING",
        "INSERT INTO " LAPLACE_PG_SCHEMA ".observed_occurrence SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA ".observed_occurrence_record[]) ON CONFLICT DO NOTHING"};
    if (kind == 0) {
        return laplace_pg_entity_insert_sql();
    }
    if (kind == 1) {
        return laplace_pg_physicality_insert_sql();
    }
    return statements[kind];
}

static const char* verify_sql(size_t kind) {
    static const char* const statements[4] = {
        NULL,
        NULL,
        "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA ".trajectory_vertex_record[]) i JOIN " LAPLACE_PG_SCHEMA ".composition_trajectory_vertex s ON s.physicality_id=i.physicality_id AND s.vertex_index=i.vertex_index AND s.carrier=i.carrier AND s.constituent_entity_id=i.constituent_entity_id AND s.logical_ordinal=i.logical_ordinal AND s.metadata=i.metadata AND s.atom=i.atom AND s.packed_ordinal=i.packed_ordinal AND s.run_length=i.run_length AND s.tier=i.tier AND s.has_atom=i.has_atom",
        "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA ".observed_occurrence_record[]) i JOIN " LAPLACE_PG_SCHEMA ".observed_occurrence s ON s.occurrence_id=i.occurrence_id AND s.entity_id=i.entity_id AND s.physicality_id IS NOT DISTINCT FROM i.physicality_id AND s.source_fingerprint=i.source_fingerprint AND s.context_fingerprint=i.context_fingerprint AND s.source_ordinal=i.source_ordinal AND s.flags=i.flags"};
    if (kind == 0) {
        return laplace_pg_entity_verify_sql();
    }
    if (kind == 1) {
        return laplace_pg_physicality_verify_sql();
    }
    return statements[kind];
}

static void execute_reference_check(
    persistence_sink_state* state,
    ArrayType* arrays[4]) {
    Datum values[4] = {
        PointerGetDatum(arrays[0]), PointerGetDatum(arrays[1]),
        PointerGetDatum(arrays[2]), PointerGetDatum(arrays[3])};
    bool is_null = false;
    Datum missing_entity;
    Datum missing_physicality;
    int result;
    ensure_reference_plan();
    result = SPI_execute_plan(reference_plan, values, NULL, true, 1);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_REFERENCE_PREFLIGHT);
    if (result != SPI_OK_SELECT || SPI_processed != 1) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence reference preflight was not bounded")));
    }
    missing_entity = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence reference count is null")));
    }
    missing_physicality = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2, &is_null);
    if (is_null || DatumGetInt64(missing_entity) != 0 ||
        DatumGetInt64(missing_physicality) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_FOREIGN_KEY_VIOLATION),
                 errmsg("Laplace persistence stream contains an unresolved reference")));
    }
}

static void execute_record_family(
    persistence_sink_state* state,
    size_t kind,
    ArrayType* records,
    uint64_t count) {
    Oid types[1] = {laplace_pg_composite_array_oid(record_type_names[kind])};
    Datum values[1] = {PointerGetDatum(records)};
    int result;
    uint64_t verified;
    uint64_t inserted;
    if (count == 0) {
        return;
    }
    laplace_pg_keep_plan(&insert_plans[kind], insert_sql(kind), 1, types);
    result = SPI_execute_plan(insert_plans[kind], values, NULL, false, 0);
    note_plan(state,
        kind == 0 ? LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_INSERT :
        kind == 1 ? LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_INSERT :
        kind == 2 ? LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_INSERT :
                    LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_INSERT);
    if (result != SPI_OK_INSERT || SPI_processed > count) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence insert was not set-bounded")));
    }
    inserted = (uint64_t)SPI_processed;
    state->inserted[kind] = inserted;
    laplace_pg_keep_plan(&verify_plans[kind], verify_sql(kind), 1, types);
    result = SPI_execute_plan(verify_plans[kind], values, NULL, false, 1);
    note_plan(state,
        kind == 0 ? LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_VERIFY :
        kind == 1 ? LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_VERIFY :
        kind == 2 ? LAPLACE_PERSISTENCE_PG_PLAN_TRAJECTORY_VERIFY :
                    LAPLACE_PERSISTENCE_PG_PLAN_OCCURRENCE_VERIFY);
    if (result != SPI_OK_SELECT) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence exact-collision verification failed")));
    }
    verified = laplace_pg_scalar_count("persistence conflict verification");
#if defined(LAPLACE_TEST_BLIND_CONFLICT)
    (void)verified;
#else
    if (verified != count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace persistence record family %zu collides with different normalized fields",
                        kind),
                 errdetail("input=%llu inserted=%llu verified=%llu",
                           (unsigned long long)count,
                           (unsigned long long)inserted,
                           (unsigned long long)verified)));
    }
#endif
}

static laplace_framework_status sink_begin(
    void* opaque,
    const laplace_framework_context* context,
    uint32_t record_type,
    uint64_t total_records,
    uint64_t total_bytes) {
    persistence_sink_state* state = (persistence_sink_state*)opaque;
    uint64_t allocation_bytes;
    uint64_t stream_bytes;
    uint64_t record_bytes;
    if (record_type != LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE ||
        total_records == 0 || total_records > SIZE_MAX / sizeof(staged_record) ||
        total_bytes > UINT64_MAX / LAPLACE_PERSISTENCE_PG_STREAM_BYTE_MULTIPLIER ||
        total_records > UINT64_MAX /
            (sizeof(staged_record) +
             LAPLACE_PERSISTENCE_PG_PER_RECORD_OVERHEAD_BYTES)) {
        return LAPLACE_FRAMEWORK_STREAM_INVALID;
    }
    stream_bytes =
        total_bytes * LAPLACE_PERSISTENCE_PG_STREAM_BYTE_MULTIPLIER;
    record_bytes = total_records *
        (sizeof(staged_record) +
         LAPLACE_PERSISTENCE_PG_PER_RECORD_OVERHEAD_BYTES);
    if (stream_bytes > UINT64_MAX - record_bytes) {
        return LAPLACE_FRAMEWORK_STREAM_INVALID;
    }
    allocation_bytes = stream_bytes + record_bytes;
    if (allocation_bytes > context->resource_grant.memory_bytes) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    state->capacity = total_records;
    state->records = (staged_record*)palloc0(
        (size_t)total_records * sizeof(*state->records));
    if (SPI_connect() != SPI_OK_CONNECT) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    state->spi_connected = 1;
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status sink_stage(
    void* opaque,
    const laplace_framework_canonical_batch* batch) {
    persistence_sink_state* state = (persistence_sink_state*)opaque;
    size_t offset = 0;
    while (offset < (size_t)batch->byte_count) {
        staged_record* destination;
        size_t consumed = 0;
        laplace_persistence_status status;
        if (state->count >= state->capacity) {
            return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        }
        destination = &state->records[state->count];
        destination->frame = batch->canonical_bytes + offset;
        status = laplace_persistence_frame_decode(
            destination->frame, (size_t)batch->byte_count - offset,
            &destination->record, &consumed);
        if (status != LAPLACE_PERSISTENCE_OK) {
            return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        }
        if (destination->record.kind == LAPLACE_PERSISTENCE_RECORD_PHYSICALITY) {
            state->next_logical_ordinal = 1u;
        } else if (destination->record.kind ==
                   LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX) {
            laplace_composition_occurrence occurrence;
            if (laplace_trajectory_composition_decode_one(
                    &destination->record.value.trajectory.carrier,
                    state->next_logical_ordinal, &occurrence) !=
                LAPLACE_TRAJECTORY_OK ||
                UINT64_MAX - state->next_logical_ordinal < occurrence.run_length) {
                return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
            }
            destination->record.value.trajectory.occurrence = occurrence;
            state->next_logical_ordinal += occurrence.run_length;
        }
        state->count += 1u;
        offset += consumed;
    }
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status sink_seal(
    void* opaque,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    persistence_sink_state* state = (persistence_sink_state*)opaque;
    ArrayType* arrays[4];
    const uint64_t counts[4] = {
        state->summary.entity_count,
        state->summary.physicality_count,
        state->summary.trajectory_vertex_count,
        state->summary.occurrence_count};
    size_t kind;
    if (!state->spi_connected || state->count != state->capacity) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    arrays[0] = build_entity_array(state);
    arrays[1] = build_physicality_array(state);
    arrays[2] = build_trajectory_array(state);
    arrays[3] = build_occurrence_array(state);
    execute_reference_check(state, arrays);
    for (kind = 0; kind < 4; ++kind) {
        execute_record_family(state, kind, arrays[kind], counts[kind]);
    }
    *artifact_fingerprint = *stream_fingerprint;
    return LAPLACE_FRAMEWORK_OK;
}

static void sink_abort(void* opaque) {
    persistence_sink_state* state = (persistence_sink_state*)opaque;
    if (state->spi_connected) {
        (void)SPI_finish();
        state->spi_connected = 0;
    }
}

static void read_digest_argument(Datum argument, laplace_digest256* digest) {
    bytea* value = DatumGetByteaPP(argument);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(digest->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace persistence fingerprint must contain exactly 32 bytes")));
    }
    memcpy(digest->bytes, VARDATA_ANY(value), sizeof(digest->bytes));
}

static void persist_deposit_receipt(
    persistence_sink_state* state,
    const laplace_framework_stream_receipt* receipt) {
    static const char insert_sql_text[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".canonical_deposit_receipt("
        "receipt_id,context_fingerprint,source_fingerprint,recipe_fingerprint,stream_fingerprint,sink_artifacts_fingerprint,"
        "total_records,total_bytes,batch_count,sink_count,record_type,effect_disposition,status,"
        "entity_count,physicality_count,trajectory_vertex_count,occurrence_count,logical_occurrence_count,plan_sequence_fingerprint,plan_count) "
        "VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20) ON CONFLICT DO NOTHING";
    static const char verify_sql_text[] =
        "SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".canonical_deposit_receipt WHERE "
        "receipt_id=$1 AND context_fingerprint=$2 AND source_fingerprint=$3 AND recipe_fingerprint=$4 AND stream_fingerprint=$5 AND sink_artifacts_fingerprint=$6 AND "
        "total_records=$7 AND total_bytes=$8 AND batch_count=$9 AND sink_count=$10 AND record_type=$11 AND effect_disposition=$12 AND status=$13 AND "
        "entity_count=$14 AND physicality_count=$15 AND trajectory_vertex_count=$16 AND occurrence_count=$17 AND logical_occurrence_count=$18 AND plan_sequence_fingerprint=$19 AND plan_count=$20";
    Oid types[20] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID,
        INT4OID, INT4OID, INT4OID,
        NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID,
        BYTEAOID, INT4OID};
    Datum values[20];
    int result;
    const size_t digest_bytes = sizeof(receipt->receipt_id.bytes);
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->receipt_id.bytes, digest_bytes));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->context_fingerprint.bytes, digest_bytes));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->source_fingerprint.bytes, digest_bytes));
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->recipe_fingerprint.bytes, digest_bytes));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->stream_fingerprint.bytes, digest_bytes));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->sink_artifacts_fingerprint.bytes, digest_bytes));
    values[6] = laplace_pg_numeric_from_uint64(receipt->total_records);
    values[7] = laplace_pg_numeric_from_uint64(receipt->total_bytes);
    values[8] = laplace_pg_numeric_from_uint64(receipt->batch_count);
    values[9] = laplace_pg_numeric_from_uint64(receipt->sink_count);
    values[10] = Int32GetDatum((int32)receipt->record_type);
    values[11] = Int32GetDatum((int32)receipt->effect_disposition);
    values[12] = Int32GetDatum((int32)receipt->status);
    values[13] = laplace_pg_numeric_from_uint64(state->summary.entity_count);
    values[14] = laplace_pg_numeric_from_uint64(state->summary.physicality_count);
    values[15] = laplace_pg_numeric_from_uint64(state->summary.trajectory_vertex_count);
    values[16] = laplace_pg_numeric_from_uint64(state->summary.occurrence_count);
    values[17] = laplace_pg_numeric_from_uint64(state->summary.logical_occurrence_count);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_INSERT);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_RECEIPT_VERIFY);
    if (laplace_persistence_plan_sequence_fingerprint(
            state->plan_ids, state->plan_count,
            &state->plan_sequence_fingerprint) != LAPLACE_PERSISTENCE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence plan sequence cannot be receipted")));
    }
    values[18] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->plan_sequence_fingerprint.bytes,
        sizeof(state->plan_sequence_fingerprint.bytes)));
    values[19] = Int32GetDatum((int32)state->plan_count);
    laplace_pg_keep_plan(
        &deposit_receipt_insert_plan, insert_sql_text, 20, types);
    result = SPI_execute_plan(deposit_receipt_insert_plan, values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > 1) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace deposit receipt insert was not bounded")));
    }
    laplace_pg_keep_plan(
        &deposit_receipt_verify_plan, verify_sql_text, 20, types);
    result = SPI_execute_plan(deposit_receipt_verify_plan, values, NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        laplace_pg_scalar_count("persistence receipt verification") != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace deposit receipt collides with different stored fields")));
    }
}

Datum LAPLACE_PG_PERSISTENCE_DEPOSIT_SYMBOL(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_framework_canonical_batch batch;
    laplace_framework_canonical_stream stream;
    laplace_framework_stream_receipt receipt;
    laplace_framework_sink_v1 sink;
    persistence_sink_state state;
    laplace_persistence_summary summary;
    ArrayType* frames = PG_GETARG_ARRAYTYPE_P(3);
    Datum* frame_datums = NULL;
    bool* frame_nulls = NULL;
    int frame_count = 0;
    uint8_t* canonical_bytes;
    uint64_t total_bytes = 0;
    uint64_t offset = 0;
    Datum result_values[23];
    bool result_nulls[23] = {false};
    HeapTuple result_tuple;
    int index;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    memset(&stream, 0, sizeof(stream));
    read_digest_argument(PG_GETARG_DATUM(1), &stream.source_fingerprint);
    read_digest_argument(PG_GETARG_DATUM(2), &stream.recipe_fingerprint);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("read-only Laplace execution context cannot deposit canonical state")));
    }
    deconstruct_array(
        frames, BYTEAOID, -1, false, TYPALIGN_INT,
        &frame_datums, &frame_nulls, &frame_count);
    if (frame_count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace persistence stream must contain at least one frame")));
    }
    for (index = 0; index < frame_count; ++index) {
        bytea* frame;
        laplace_persistence_record decoded;
        size_t consumed = 0;
        size_t length;
        if (frame_nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace persistence stream cannot contain a null frame")));
        }
        frame = DatumGetByteaPP(frame_datums[index]);
        length = (size_t)VARSIZE_ANY_EXHDR(frame);
        if (laplace_persistence_frame_decode(
                (const uint8_t*)VARDATA_ANY(frame), length,
                &decoded, &consumed) != LAPLACE_PERSISTENCE_OK ||
            consumed != length || UINT64_MAX - total_bytes < length) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                     errmsg("Laplace persistence frame violates the native contract")));
        }
        total_bytes += (uint64_t)length;
    }
    if (total_bytes > SIZE_MAX || total_bytes > context.resource_grant.memory_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persistence stream exceeds its resource grant")));
    }
    canonical_bytes = (uint8_t*)palloc((size_t)total_bytes);
    for (index = 0; index < frame_count; ++index) {
        bytea* frame = DatumGetByteaPP(frame_datums[index]);
        const size_t length = (size_t)VARSIZE_ANY_EXHDR(frame);
        memcpy(canonical_bytes + offset, VARDATA_ANY(frame), length);
        offset += (uint64_t)length;
    }
    memset(&batch, 0, sizeof(batch));
    batch.canonical_bytes = canonical_bytes;
    batch.byte_count = total_bytes;
    batch.record_count = (uint64_t)frame_count;
    batch.record_type = LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE;
    if (laplace_persistence_validate_stream(&batch, 1u, &summary) !=
        LAPLACE_PERSISTENCE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace persistence stream failed whole-stream native preflight")));
    }
    stream.batches = &batch;
    stream.batch_count = 1u;
    memset(&state, 0, sizeof(state));
    state.summary = summary;
    sink.state = &state;
    sink.begin = sink_begin;
    sink.stage = sink_stage;
    sink.seal = sink_seal;
    sink.abort = sink_abort;
    sink.abi_major = LAPLACE_FRAMEWORK_SINK_ABI_MAJOR;
    sink.abi_minor = LAPLACE_FRAMEWORK_SINK_ABI_MINOR;
    sink.flags = 0;
    sink.reserved = 0;
    memset(&receipt, 0, sizeof(receipt));
    if (laplace_framework_stage_canonical_stream(
            &context, &stream, &sink, 1u, &receipt) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace framework rejected canonical persistence staging with status %d at batch %llu sink %llu",
                        (int)receipt.status,
                        (unsigned long long)receipt.failed_batch_index,
                        (unsigned long long)receipt.failed_sink_index)));
    }
    persist_deposit_receipt(&state, &receipt);
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("cannot close Laplace persistence SPI provider")));
    }
    state.spi_connected = 0;
    result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.receipt_id.bytes, 32));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.context_fingerprint.bytes, 32));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.source_fingerprint.bytes, 32));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.recipe_fingerprint.bytes, 32));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.stream_fingerprint.bytes, 32));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.sink_artifacts_fingerprint.bytes, 32));
    result_values[6] = laplace_pg_numeric_from_uint64(receipt.total_records);
    result_values[7] = laplace_pg_numeric_from_uint64(receipt.total_bytes);
    result_values[8] = laplace_pg_numeric_from_uint64(receipt.batch_count);
    result_values[9] = laplace_pg_numeric_from_uint64(receipt.sink_count);
    result_values[10] = laplace_pg_numeric_from_uint64(summary.entity_count);
    result_values[11] = laplace_pg_numeric_from_uint64(summary.physicality_count);
    result_values[12] = laplace_pg_numeric_from_uint64(summary.trajectory_vertex_count);
    result_values[13] = laplace_pg_numeric_from_uint64(summary.occurrence_count);
    result_values[14] = laplace_pg_numeric_from_uint64(summary.logical_occurrence_count);
    result_values[15] = laplace_pg_numeric_from_uint64(state.inserted[0]);
    result_values[16] = laplace_pg_numeric_from_uint64(state.inserted[1]);
    result_values[17] = laplace_pg_numeric_from_uint64(state.inserted[2]);
    result_values[18] = laplace_pg_numeric_from_uint64(state.inserted[3]);
    result_values[19] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state.plan_sequence_fingerprint.bytes,
        sizeof(state.plan_sequence_fingerprint.bytes)));
    result_values[20] = Int32GetDatum((int32)state.plan_count);
    result_values[21] = Int32GetDatum((int32)receipt.effect_disposition);
    result_values[22] = Int32GetDatum((int32)receipt.status);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 23);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
