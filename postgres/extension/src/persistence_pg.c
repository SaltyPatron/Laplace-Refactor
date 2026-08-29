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
#include "utils/memutils.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/persistence.h"
#include "laplace_pg_internal.h"
#include "persistence_rows_pg.h"
#include "persistence_pg.h"
#include "set_pg.h"

PG_FUNCTION_INFO_V1(LAPLACE_PG_PERSISTENCE_DEPOSIT_SYMBOL);

typedef struct staged_record {
    laplace_persistence_record record;
    const uint8_t* frame;
} staged_record;

typedef struct persistence_batch_state {
    staged_record* records;
    uint64_t count;
    laplace_persistence_summary summary;
} persistence_batch_state;

typedef struct persistence_sink_state {
    uint64_t next_logical_ordinal;
    uint64_t expected_records;
    uint64_t expected_bytes;
    uint64_t memory_grant_bytes;
    laplace_persistence_summary summary;
    laplace_persistence_summary staged_summary;
    uint64_t inserted[5];
    uint32_t plan_ids[LAPLACE_PERSISTENCE_PG_PLAN_COUNT];
    laplace_digest256 plan_sequence_fingerprint;
    uint32_t plan_count;
    SPIPlanPtr write_partition_lock_plan;
    SPIPlanPtr reference_plan;
    SPIPlanPtr stage_plans[5];
    SPIPlanPtr insert_plans[5];
    SPIPlanPtr verify_plans[5];
    MemoryContext batch_context;
    int spi_connected;
} persistence_sink_state;

static SPIPlanPtr deposit_receipt_insert_plan = NULL;
#if !defined(LAPLACE_TEST_COMPOSITION_REPLAY_RECEIPT_VERIFY_BYPASS)
static SPIPlanPtr deposit_receipt_verify_plan = NULL;
#endif

static const char* const record_type_names[5] = {
    "entity_record",
    "physicality_record",
    "physicality_trajectory_segment_record",
    "attestation_record",
    "consensus_record"};

static void note_plan(persistence_sink_state* state, uint32_t plan_id) {
    uint32_t index;
    for (index = 0; index < state->plan_count; ++index) {
        if (state->plan_ids[index] == plan_id) {
            return;
        }
    }
    if (state->plan_count >= LAPLACE_PERSISTENCE_PG_PLAN_COUNT ||
        plan_id == 0 || plan_id != state->plan_count + 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence plan sequence departed from its contract")));
    }
    state->plan_ids[state->plan_count++] = plan_id;
}

static ArrayType* build_entity_array(const persistence_batch_state* state) {
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

static ArrayType* build_physicality_array(const persistence_batch_state* state) {
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

static ArrayType* build_trajectory_array(const persistence_batch_state* state) {
    static const Oid attribute_types[11] = {
        BYTEAOID, NUMERICOID, BYTEAOID, BYTEAOID, NUMERICOID,
        INT8OID, INT8OID, INT4OID, INT4OID, INT2OID, BOOLOID};
    static const int32 attribute_typmods[11] = {
        -1, LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1, -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = state->summary.trajectory_segment_count == 0 ? NULL :
        (Datum*)palloc(sizeof(*rows) *
                      (size_t)state->summary.trajectory_segment_count);
    ArrayType* result;
    uint64_t row = 0;
    uint64_t index;
    laplace_pg_composite_binding_open(
        record_type_names[2], attribute_types, attribute_typmods,
        11, &binding);
    for (index = 0; index < state->count; ++index) {
        const staged_record* staged = &state->records[index];
        if (staged->record.kind == LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT) {
            const laplace_persistence_trajectory_segment_record* value =
                &staged->record.value.trajectory_segment;
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

static ArrayType* build_attestation_array(const persistence_batch_state* state) {
    static const Oid attribute_types[8] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, INT4OID, INT4OID};
    static const int32 attribute_typmods[8] = {
        -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = state->summary.attestation_count == 0 ? NULL :
        (Datum*)palloc(sizeof(*rows) * (size_t)state->summary.attestation_count);
    ArrayType* result;
    uint64_t row = 0;
    uint64_t index;
    laplace_pg_composite_binding_open(
        record_type_names[3], attribute_types, attribute_typmods,
        8, &binding);
    for (index = 0; index < state->count; ++index) {
        const staged_record* staged = &state->records[index];
        if (staged->record.kind == LAPLACE_PERSISTENCE_RECORD_ATTESTATION) {
            const laplace_persistence_attestation_record* value =
                &staged->record.value.attestation;
            const bool has_physicality =
                (value->flags & LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY) != 0;
            Datum fields[8];
            bool nulls[8] = {false};
            fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->attestation_id.bytes, sizeof(value->attestation_id.bytes)));
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
            fields[7] = Int32GetDatum((int32)value->attestation_kind);
            rows[row++] = laplace_pg_composite_record(&binding, fields, nulls);
        }
    }
    result = laplace_pg_composite_array(&binding, rows, row);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* build_consensus_array(const persistence_batch_state* state) {
    static const Oid attribute_types[10] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, INT4OID, INT4OID, FLOAT8OID};
    static const int32 attribute_typmods[10] = {
        -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = state->summary.consensus_count == 0 ? NULL :
        (Datum*)palloc(sizeof(*rows) * (size_t)state->summary.consensus_count);
    ArrayType* result;
    uint64_t row = 0;
    uint64_t index;
    laplace_pg_composite_binding_open(
        record_type_names[4], attribute_types, attribute_typmods, 10, &binding);
    for (index = 0; index < state->count; ++index) {
        const staged_record* staged = &state->records[index];
        if (staged->record.kind == LAPLACE_PERSISTENCE_RECORD_CONSENSUS) {
            const laplace_persistence_consensus_record* value =
                &staged->record.value.consensus;
            Datum fields[10];
            bool nulls[10] = {false};
            fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->consensus_id.bytes, sizeof(value->consensus_id.bytes)));
            fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->proposition_entity_id.bytes,
                sizeof(value->proposition_entity_id.bytes)));
            fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->epoch_id.bytes, sizeof(value->epoch_id.bytes)));
            fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->evidence_boundary.bytes,
                sizeof(value->evidence_boundary.bytes)));
            fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                value->recipe_fingerprint.bytes,
                sizeof(value->recipe_fingerprint.bytes)));
            fields[5] = laplace_pg_numeric_from_uint64(value->observation_count);
            fields[6] = laplace_pg_numeric_from_uint64(
                value->independent_root_count);
            fields[7] = Int32GetDatum((int32)value->disposition);
            fields[8] = Int32GetDatum((int32)value->flags);
            fields[9] = Float8GetDatum(value->standing);
            rows[row++] = laplace_pg_composite_record(&binding, fields, nulls);
        }
    }
    result = laplace_pg_composite_array(&binding, rows, row);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static const char* reference_sql(void) {
    static const char sql[] =
        "WITH incoming_entity AS (SELECT entity_id FROM pg_temp.laplace_persistence_stage_entity), "
        "incoming_physicality AS (SELECT physicality_id, entity_id FROM pg_temp.laplace_persistence_stage_physicality), "
        "incoming_segment AS (SELECT physicality_id, constituent_entity_id FROM pg_temp.laplace_persistence_stage_trajectory_segment), "
        "incoming_attestation AS (SELECT entity_id, physicality_id FROM pg_temp.laplace_persistence_stage_attestation), "
        "incoming_consensus AS (SELECT proposition_entity_id AS entity_id FROM pg_temp.laplace_persistence_stage_consensus), "
        "entity_reference AS (SELECT entity_id FROM incoming_physicality UNION SELECT constituent_entity_id FROM incoming_segment UNION SELECT entity_id FROM incoming_attestation UNION SELECT entity_id FROM incoming_consensus), "
        "physicality_reference AS (SELECT physicality_id FROM incoming_segment UNION SELECT physicality_id FROM incoming_attestation WHERE physicality_id IS NOT NULL) "
        "SELECT "
        "(SELECT count(*) FROM entity_reference r LEFT JOIN " LAPLACE_PG_SCHEMA ".entity e ON e.entity_id=r.entity_id "
        " LEFT JOIN incoming_entity i ON i.entity_id=r.entity_id "
        " WHERE e.entity_id IS NULL AND i.entity_id IS NULL), "
        "(SELECT count(*) FROM physicality_reference r LEFT JOIN " LAPLACE_PG_SCHEMA ".physicality p ON p.physicality_id=r.physicality_id "
        " LEFT JOIN incoming_physicality i ON i.physicality_id=r.physicality_id "
        " WHERE p.physicality_id IS NULL AND i.physicality_id IS NULL)";
    return sql;
}

static const char* write_partition_lock_sql(void) {
    /* One of 64 bounded transaction lock partitions is selected from the first
     * identity byte.  Sorted acquisition prevents lock-order inversions.  This
     * is a physical concurrency mechanism, not a semantic plan or a per-record
     * conflict probe. */
    static const char sql[] =
        "WITH incoming(id) AS ("
        "SELECT entity_id FROM pg_temp.laplace_persistence_stage_entity UNION ALL "
        "SELECT physicality_id FROM pg_temp.laplace_persistence_stage_physicality UNION ALL "
        "SELECT attestation_id FROM pg_temp.laplace_persistence_stage_attestation UNION ALL "
        "SELECT consensus_id FROM pg_temp.laplace_persistence_stage_consensus), "
        "partitions AS (SELECT DISTINCT (get_byte(id,0) & 63)::integer AS partition FROM incoming) "
        "SELECT pg_advisory_xact_lock(1280331859,partition) FROM partitions ORDER BY partition";
    return sql;
}

static const char* stage_sql(size_t kind) {
    static const char* const statements[5] = {
        "INSERT INTO pg_temp.laplace_persistence_stage_entity SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA ".entity_record[])",
        "INSERT INTO pg_temp.laplace_persistence_stage_physicality SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA ".physicality_record[])",
        "INSERT INTO pg_temp.laplace_persistence_stage_trajectory_segment SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA ".physicality_trajectory_segment_record[])",
        "INSERT INTO pg_temp.laplace_persistence_stage_attestation SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA ".attestation_record[])",
        "INSERT INTO pg_temp.laplace_persistence_stage_consensus SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA ".consensus_record[])"};
    return statements[kind];
}

static const char* insert_sql(size_t kind) {
    if (kind == 0) {
        return "INSERT INTO " LAPLACE_PG_SCHEMA
            ".entity(entity_id,identity_witness) "
            "SELECT i.entity_id,i.identity_witness FROM pg_temp.laplace_persistence_stage_entity i "
            "WHERE NOT EXISTS (SELECT 1 FROM " LAPLACE_PG_SCHEMA ".entity s WHERE s.entity_id=i.entity_id)";
    }
    if (kind == 1) {
        return "WITH trajectory AS ("
            "SELECT physicality_id,string_agg(carrier,'\\x'::bytea ORDER BY vertex_index) AS bytes "
            "FROM pg_temp.laplace_persistence_stage_trajectory_segment GROUP BY physicality_id), "
            "incoming AS (SELECT p.*,CASE WHEN p.vertex_count=0 THEN '\\x'::bytea ELSE t.bytes END AS trajectory "
            "FROM pg_temp.laplace_persistence_stage_physicality p LEFT JOIN trajectory t USING(physicality_id)) "
            "INSERT INTO " LAPLACE_PG_SCHEMA ".physicality "
            "SELECT i.* FROM incoming i WHERE NOT EXISTS (SELECT 1 FROM "
            LAPLACE_PG_SCHEMA ".physicality s WHERE s.physicality_id=i.physicality_id)";
    }
    if (kind == 3) {
        return "INSERT INTO " LAPLACE_PG_SCHEMA ".attestation "
            "SELECT i.* FROM pg_temp.laplace_persistence_stage_attestation i "
            "WHERE NOT EXISTS (SELECT 1 FROM " LAPLACE_PG_SCHEMA
            ".attestation s WHERE s.attestation_id=i.attestation_id)";
    }
    if (kind == 4) {
        return "INSERT INTO " LAPLACE_PG_SCHEMA ".consensus "
            "SELECT i.* FROM pg_temp.laplace_persistence_stage_consensus i "
            "WHERE NOT EXISTS (SELECT 1 FROM " LAPLACE_PG_SCHEMA
            ".consensus s WHERE s.consensus_id=i.consensus_id)";
    }
    return NULL;
}

static const char* verify_sql(size_t kind) {
    if (kind == 0) {
        return "SELECT count(*) FROM pg_temp.laplace_persistence_stage_entity i JOIN "
            LAPLACE_PG_SCHEMA ".entity s ON s.entity_id=i.entity_id "
            "AND s.identity_witness=i.identity_witness";
    }
    if (kind == 1) {
        return "WITH trajectory AS (SELECT physicality_id,string_agg(carrier,'\\x'::bytea ORDER BY vertex_index) AS bytes FROM pg_temp.laplace_persistence_stage_trajectory_segment GROUP BY physicality_id), "
            "incoming AS (SELECT p.*,CASE WHEN p.vertex_count=0 THEN '\\x'::bytea ELSE t.bytes END AS trajectory FROM pg_temp.laplace_persistence_stage_physicality p LEFT JOIN trajectory t USING(physicality_id)) "
            "SELECT count(*) FROM incoming i JOIN "
            LAPLACE_PG_SCHEMA ".physicality s ON s.physicality_id=i.physicality_id "
            "AND s.entity_id=i.entity_id "
            "AND s.physicality_type=i.physicality_type "
            "AND s.vertex_class=i.vertex_class "
            "AND s.recipe_version=i.recipe_version "
            "AND s.structural_form=i.structural_form "
            "AND s.dimension_count=i.dimension_count AND s.flags=i.flags "
            "AND s.recipe_fingerprint=i.recipe_fingerprint "
            "AND s.geometry_epoch=i.geometry_epoch "
            "AND s.trajectory_fingerprint=i.trajectory_fingerprint "
            "AND float8send(s.centroid_x)=float8send(i.centroid_x) "
            "AND float8send(s.centroid_y)=float8send(i.centroid_y) "
            "AND float8send(s.centroid_z)=float8send(i.centroid_z) "
            "AND float8send(s.centroid_m)=float8send(i.centroid_m) "
            "AND float8send(s.radius)=float8send(i.radius) "
            "AND s.logical_count=i.logical_count "
            "AND s.vertex_count=i.vertex_count AND s.trajectory=i.trajectory";
    }
    if (kind == 3) {
        return "SELECT count(*) FROM pg_temp.laplace_persistence_stage_attestation i JOIN "
            LAPLACE_PG_SCHEMA ".attestation s ON s.attestation_id=i.attestation_id "
            "AND s.entity_id=i.entity_id AND s.physicality_id IS NOT DISTINCT FROM i.physicality_id "
            "AND s.source_fingerprint=i.source_fingerprint AND s.context_fingerprint=i.context_fingerprint "
            "AND s.source_ordinal=i.source_ordinal AND s.flags=i.flags "
            "AND s.attestation_kind=i.attestation_kind";
    }
    if (kind == 4) {
        return "SELECT count(*) FROM pg_temp.laplace_persistence_stage_consensus i JOIN "
            LAPLACE_PG_SCHEMA ".consensus s ON s.consensus_id=i.consensus_id "
            "AND s.proposition_entity_id=i.proposition_entity_id AND s.epoch_id=i.epoch_id "
            "AND s.evidence_boundary=i.evidence_boundary AND s.recipe_fingerprint=i.recipe_fingerprint "
            "AND s.observation_count=i.observation_count AND s.independent_root_count=i.independent_root_count "
            "AND s.disposition=i.disposition AND s.flags=i.flags "
            "AND float8send(s.standing)=float8send(i.standing)";
    }
    return NULL;
}

static void execute_reference_check(
    persistence_sink_state* state) {
    bool is_null = false;
    Datum missing_entity;
    Datum missing_physicality;
    int64 missing_entity_count;
    int64 missing_physicality_count;
    int result;
    result = SPI_execute_plan(state->reference_plan, NULL, NULL, false, 1);
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
    missing_entity_count = DatumGetInt64(missing_entity);
    missing_physicality_count = is_null ? -1 : DatumGetInt64(missing_physicality);
    if (is_null || missing_entity_count != 0 || missing_physicality_count != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_FOREIGN_KEY_VIOLATION),
                 errmsg("Laplace persistence stream contains an unresolved reference"),
                 errdetail("missing_entities=%lld missing_physicalities=%lld",
                           (long long)missing_entity_count,
                           (long long)missing_physicality_count)));
    }
}

static void acquire_write_partitions(persistence_sink_state* state) {
    const int result = SPI_execute_plan(
        state->write_partition_lock_plan, NULL, NULL, false, 0);
    if (result != SPI_OK_SELECT || SPI_processed > 64u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence write-partition acquisition was not bounded"),
                 errdetail("partition_count=%llu",
                           (unsigned long long)SPI_processed)));
    }
}

static void stage_record_family(
    persistence_sink_state* state,
    size_t kind,
    ArrayType* records,
    uint64_t count) {
    Datum values[1] = {PointerGetDatum(records)};
    int result;
    result = SPI_execute_plan(state->stage_plans[kind], values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed != count) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence staging was not set-bounded"),
                 errdetail("record_family=%zu input=%llu staged=%llu",
                           kind, (unsigned long long)count,
                           (unsigned long long)SPI_processed)));
    }
}

static void initialize_staging_tables(void) {
    static const char* const create_statements[5] = {
        "CREATE TEMP TABLE IF NOT EXISTS laplace_persistence_stage_entity "
        "(LIKE " LAPLACE_PG_SCHEMA ".entity) "
        "ON COMMIT DELETE ROWS",
        "CREATE TEMP TABLE IF NOT EXISTS laplace_persistence_stage_physicality "
        "AS SELECT physicality_id,entity_id,physicality_type,vertex_class,recipe_version,structural_form,dimension_count,flags,recipe_fingerprint,geometry_epoch,trajectory_fingerprint,centroid_x,centroid_y,centroid_z,centroid_m,radius,logical_count,vertex_count FROM " LAPLACE_PG_SCHEMA ".physicality WITH NO DATA",
        "CREATE TEMP TABLE IF NOT EXISTS laplace_persistence_stage_trajectory_segment "
        "(physicality_id bytea,vertex_index numeric(20,0),carrier bytea,constituent_entity_id bytea,logical_ordinal numeric(20,0),metadata bigint,atom bigint,packed_ordinal integer,run_length integer,tier smallint,has_atom boolean) "
        "ON COMMIT DELETE ROWS",
        "CREATE TEMP TABLE IF NOT EXISTS laplace_persistence_stage_attestation "
        "AS SELECT * FROM " LAPLACE_PG_SCHEMA ".attestation WITH NO DATA",
        "CREATE TEMP TABLE IF NOT EXISTS laplace_persistence_stage_consensus "
        "AS SELECT * FROM " LAPLACE_PG_SCHEMA ".consensus WITH NO DATA"
    };
    static const char* const index_statements[5] = {
        "CREATE INDEX IF NOT EXISTS laplace_persistence_stage_entity_id_idx "
        "ON pg_temp.laplace_persistence_stage_entity(entity_id)",
        "CREATE INDEX IF NOT EXISTS laplace_persistence_stage_physicality_id_idx "
        "ON pg_temp.laplace_persistence_stage_physicality(physicality_id)",
        "CREATE INDEX IF NOT EXISTS laplace_persistence_stage_trajectory_order_idx "
        "ON pg_temp.laplace_persistence_stage_trajectory_segment(physicality_id,vertex_index)",
        "CREATE INDEX IF NOT EXISTS laplace_persistence_stage_attestation_id_idx "
        "ON pg_temp.laplace_persistence_stage_attestation(attestation_id)",
        "CREATE INDEX IF NOT EXISTS laplace_persistence_stage_consensus_id_idx "
        "ON pg_temp.laplace_persistence_stage_consensus(consensus_id)"
    };
    static const char truncate_statement[] =
        "TRUNCATE TABLE "
        "pg_temp.laplace_persistence_stage_entity, "
        "pg_temp.laplace_persistence_stage_physicality, "
        "pg_temp.laplace_persistence_stage_trajectory_segment, "
        "pg_temp.laplace_persistence_stage_attestation, "
        "pg_temp.laplace_persistence_stage_consensus";
    size_t kind;
    int result;
    for (kind = 0; kind < 5; ++kind) {
        result = SPI_execute(create_statements[kind], false, 0);
        if (result != SPI_OK_UTILITY) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Laplace persistence staging table initialization failed"),
                     errdetail("record_family=%zu spi_result=%d", kind, result)));
        }
    }
    for (kind = 0; kind < 5; ++kind) {
        result = SPI_execute(index_statements[kind], false, 0);
        if (result != SPI_OK_UTILITY) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Laplace persistence staging index initialization failed"),
                     errdetail("record_family=%zu spi_result=%d", kind, result)));
        }
    }
    result = SPI_execute(truncate_statement, false, 0);
    if (result != SPI_OK_UTILITY) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence staging reset failed"),
                 errdetail("spi_result=%d", result)));
    }
}

static void analyze_staging_tables(void) {
    static const char statement[] =
        "ANALYZE pg_temp.laplace_persistence_stage_entity, "
        "pg_temp.laplace_persistence_stage_physicality, "
        "pg_temp.laplace_persistence_stage_trajectory_segment, "
        "pg_temp.laplace_persistence_stage_attestation, "
        "pg_temp.laplace_persistence_stage_consensus";
    const int result = SPI_execute(statement, false, 0);
    if (result != SPI_OK_UTILITY) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence staging statistics failed"),
                 errdetail("spi_result=%d", result)));
    }
}

static void prepare_staging_plans(persistence_sink_state* state) {
    size_t kind;
    state->write_partition_lock_plan = SPI_prepare(
        write_partition_lock_sql(), 0, NULL);
    state->reference_plan = SPI_prepare(reference_sql(), 0, NULL);
    if (state->write_partition_lock_plan == NULL ||
        state->reference_plan == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence reference plan preparation failed")));
    }
    for (kind = 0; kind < 5; ++kind) {
        Oid types[1] = {
            laplace_pg_composite_array_oid(record_type_names[kind])};
        state->stage_plans[kind] = SPI_prepare(stage_sql(kind), 1, types);
        if (kind != 2) {
            state->insert_plans[kind] = SPI_prepare(insert_sql(kind), 0, NULL);
            state->verify_plans[kind] = SPI_prepare(verify_sql(kind), 0, NULL);
        }
        if (state->stage_plans[kind] == NULL ||
            (kind != 2 && (state->insert_plans[kind] == NULL ||
                           state->verify_plans[kind] == NULL))) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Laplace persistence staging plan preparation failed"),
                     errdetail("record_family=%zu", kind)));
        }
    }
}

static void execute_record_family(
    persistence_sink_state* state,
    size_t kind,
    uint64_t count) {
    int result;
    uint64_t verified;
    uint64_t inserted;
    result = SPI_execute_plan(state->insert_plans[kind], NULL, NULL, false, 0);
    note_plan(state,
        kind == 0 ? LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_INSERT :
        kind == 1 ? LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_INSERT :
        kind == 3 ? LAPLACE_PERSISTENCE_PG_PLAN_ATTESTATION_INSERT :
                    LAPLACE_PERSISTENCE_PG_PLAN_CONSENSUS_INSERT);
    if (result != SPI_OK_INSERT || SPI_processed > count) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace persistence insert was not set-bounded")));
    }
    inserted = (uint64_t)SPI_processed;
    if (UINT64_MAX - state->inserted[kind] < inserted) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace persistence inserted count overflowed")));
    }
    state->inserted[kind] += inserted;
    result = SPI_execute_plan(state->verify_plans[kind], NULL, NULL, false, 1);
    note_plan(state,
        kind == 0 ? LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_VERIFY :
        kind == 1 ? LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_VERIFY :
        kind == 3 ? LAPLACE_PERSISTENCE_PG_PLAN_ATTESTATION_VERIFY :
                    LAPLACE_PERSISTENCE_PG_PLAN_CONSENSUS_VERIFY);
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
    if (record_type != LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE ||
        total_records == 0 || total_bytes == 0 ||
        total_records != state->summary.frame_count ||
        total_bytes != state->summary.byte_count) {
        return LAPLACE_FRAMEWORK_STREAM_INVALID;
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    state->spi_connected = 1;
    initialize_staging_tables();
    prepare_staging_plans(state);
    state->expected_records = total_records;
    state->expected_bytes = total_bytes;
    state->memory_grant_bytes = context->resource_grant.memory_bytes;
    state->batch_context = AllocSetContextCreate(
        CurrentMemoryContext, "Laplace persistence canonical batch",
        ALLOCSET_DEFAULT_SIZES);
    /* The receipt identifies the stable semantic plan-family manifest.  The
     * transaction-local stage writes are physical buffering operations; each
     * receipted semantic insert and verification executes once at seal. */
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_REFERENCE_PREFLIGHT);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_INSERT);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_ENTITY_VERIFY);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_INSERT);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_PHYSICALITY_VERIFY);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_ATTESTATION_INSERT);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_ATTESTATION_VERIFY);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_CONSENSUS_INSERT);
    note_plan(state, LAPLACE_PERSISTENCE_PG_PLAN_CONSENSUS_VERIFY);
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status sink_stage(
    void* opaque,
    const laplace_framework_canonical_batch* batch) {
    persistence_sink_state* state = (persistence_sink_state*)opaque;
    persistence_batch_state staged;
    MemoryContext prior_context;
    ArrayType* arrays[5];
    uint64_t counts[5];
    uint64_t record_bytes;
    uint64_t stream_bytes;
    uint64_t allocation_bytes;
    size_t offset = 0;
    size_t kind;
    if (!state->spi_connected || state->batch_context == NULL ||
        batch == NULL || batch->canonical_bytes == NULL ||
        batch->record_count == 0 || batch->byte_count == 0 ||
        batch->record_count > SIZE_MAX / sizeof(staged_record) ||
        batch->byte_count > SIZE_MAX ||
        batch->byte_count > UINT64_MAX /
            LAPLACE_PERSISTENCE_PG_STREAM_BYTE_MULTIPLIER ||
        batch->record_count > UINT64_MAX /
            (sizeof(staged_record) +
             LAPLACE_PERSISTENCE_PG_PER_RECORD_OVERHEAD_BYTES)) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    stream_bytes = batch->byte_count *
        LAPLACE_PERSISTENCE_PG_STREAM_BYTE_MULTIPLIER;
    record_bytes = batch->record_count *
        (sizeof(staged_record) +
         LAPLACE_PERSISTENCE_PG_PER_RECORD_OVERHEAD_BYTES);
    if (stream_bytes > UINT64_MAX - record_bytes) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    allocation_bytes = stream_bytes + record_bytes;
    if (allocation_bytes > state->memory_grant_bytes) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    memset(&staged, 0, sizeof(staged));
    prior_context = MemoryContextSwitchTo(state->batch_context);
    staged.records = (staged_record*)palloc0(
        (size_t)batch->record_count * sizeof(*staged.records));
    while (offset < (size_t)batch->byte_count) {
        staged_record* destination;
        size_t consumed = 0;
        laplace_persistence_status status;
        if (staged.count >= batch->record_count) {
            MemoryContextSwitchTo(prior_context);
            MemoryContextReset(state->batch_context);
            return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        }
        destination = &staged.records[staged.count];
        destination->frame = batch->canonical_bytes + offset;
        status = laplace_persistence_frame_decode(
            destination->frame, (size_t)batch->byte_count - offset,
            &destination->record, &consumed);
        if (status != LAPLACE_PERSISTENCE_OK) {
            MemoryContextSwitchTo(prior_context);
            MemoryContextReset(state->batch_context);
            return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        }
        if (destination->record.kind == LAPLACE_PERSISTENCE_RECORD_ENTITY) {
            staged.summary.entity_count += 1u;
        } else if (destination->record.kind ==
                   LAPLACE_PERSISTENCE_RECORD_PHYSICALITY) {
            staged.summary.physicality_count += 1u;
            state->next_logical_ordinal = 1u;
        } else if (destination->record.kind ==
                   LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT) {
            laplace_composition_occurrence occurrence;
            if (laplace_trajectory_composition_decode_one(
                    &destination->record.value.trajectory_segment.carrier,
                    state->next_logical_ordinal, &occurrence) !=
                LAPLACE_TRAJECTORY_OK ||
                UINT64_MAX - state->next_logical_ordinal < occurrence.run_length) {
                MemoryContextSwitchTo(prior_context);
                MemoryContextReset(state->batch_context);
                return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
            }
            destination->record.value.trajectory_segment.occurrence = occurrence;
            state->next_logical_ordinal += occurrence.run_length;
            staged.summary.trajectory_segment_count += 1u;
            if (UINT64_MAX - staged.summary.logical_occurrence_count <
                    occurrence.run_length) {
                MemoryContextSwitchTo(prior_context);
                MemoryContextReset(state->batch_context);
                return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
            }
            staged.summary.logical_occurrence_count += occurrence.run_length;
        } else if (destination->record.kind ==
                   LAPLACE_PERSISTENCE_RECORD_ATTESTATION) {
            staged.summary.attestation_count += 1u;
        } else if (destination->record.kind ==
                   LAPLACE_PERSISTENCE_RECORD_CONSENSUS) {
            staged.summary.consensus_count += 1u;
        } else {
            MemoryContextSwitchTo(prior_context);
            MemoryContextReset(state->batch_context);
            return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        }
        staged.count += 1u;
        staged.summary.frame_count += 1u;
        if (UINT64_MAX - staged.summary.byte_count < consumed) {
            MemoryContextSwitchTo(prior_context);
            MemoryContextReset(state->batch_context);
            return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        }
        staged.summary.byte_count += consumed;
        offset += consumed;
    }
    if (staged.count != batch->record_count ||
        staged.summary.byte_count != batch->byte_count) {
        MemoryContextSwitchTo(prior_context);
        MemoryContextReset(state->batch_context);
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    arrays[0] = build_entity_array(&staged);
    arrays[1] = build_physicality_array(&staged);
    arrays[2] = build_trajectory_array(&staged);
    arrays[3] = build_attestation_array(&staged);
    arrays[4] = build_consensus_array(&staged);
    counts[0] = staged.summary.entity_count;
    counts[1] = staged.summary.physicality_count;
    counts[2] = staged.summary.trajectory_segment_count;
    counts[3] = staged.summary.attestation_count;
    counts[4] = staged.summary.consensus_count;
    for (kind = 0; kind < 5; ++kind) {
        if (counts[kind] != 0u) {
            stage_record_family(state, kind, arrays[kind], counts[kind]);
        }
    }
    if (UINT64_MAX - state->staged_summary.entity_count <
            staged.summary.entity_count ||
        UINT64_MAX - state->staged_summary.physicality_count <
            staged.summary.physicality_count ||
        UINT64_MAX - state->staged_summary.trajectory_segment_count <
            staged.summary.trajectory_segment_count ||
        UINT64_MAX - state->staged_summary.attestation_count <
            staged.summary.attestation_count ||
        UINT64_MAX - state->staged_summary.consensus_count <
            staged.summary.consensus_count ||
        UINT64_MAX - state->staged_summary.logical_occurrence_count <
            staged.summary.logical_occurrence_count ||
        UINT64_MAX - state->staged_summary.frame_count <
            staged.summary.frame_count ||
        UINT64_MAX - state->staged_summary.byte_count <
            staged.summary.byte_count) {
        MemoryContextSwitchTo(prior_context);
        MemoryContextReset(state->batch_context);
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    state->staged_summary.entity_count += staged.summary.entity_count;
    state->staged_summary.physicality_count += staged.summary.physicality_count;
    state->staged_summary.trajectory_segment_count +=
        staged.summary.trajectory_segment_count;
    state->staged_summary.attestation_count += staged.summary.attestation_count;
    state->staged_summary.consensus_count += staged.summary.consensus_count;
    state->staged_summary.logical_occurrence_count +=
        staged.summary.logical_occurrence_count;
    state->staged_summary.frame_count += staged.summary.frame_count;
    state->staged_summary.byte_count += staged.summary.byte_count;
    MemoryContextSwitchTo(prior_context);
    MemoryContextReset(state->batch_context);
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status sink_seal(
    void* opaque,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    persistence_sink_state* state = (persistence_sink_state*)opaque;
    const uint64_t counts[5] = {
        state->summary.entity_count,
        state->summary.physicality_count,
        state->summary.trajectory_segment_count,
        state->summary.attestation_count,
        state->summary.consensus_count};
    size_t kind;
    if (!state->spi_connected ||
        state->staged_summary.entity_count != state->summary.entity_count ||
        state->staged_summary.physicality_count !=
            state->summary.physicality_count ||
        state->staged_summary.trajectory_segment_count !=
            state->summary.trajectory_segment_count ||
        state->staged_summary.attestation_count !=
            state->summary.attestation_count ||
        state->staged_summary.consensus_count !=
            state->summary.consensus_count ||
        state->staged_summary.logical_occurrence_count !=
            state->summary.logical_occurrence_count ||
        state->staged_summary.frame_count != state->expected_records ||
        state->staged_summary.byte_count != state->expected_bytes) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    analyze_staging_tables();
    acquire_write_partitions(state);
    execute_reference_check(state);
    for (kind = 0; kind < 5; ++kind) {
        if (kind != 2) {
            execute_record_family(state, kind, counts[kind]);
        }
    }
    if (state->inserted[1] == state->summary.physicality_count) {
        state->inserted[2] = state->summary.trajectory_segment_count;
    } else if (state->inserted[1] != 0u) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    if (state->batch_context != NULL) {
        MemoryContextDelete(state->batch_context);
        state->batch_context = NULL;
    }
    *artifact_fingerprint = *stream_fingerprint;
    return LAPLACE_FRAMEWORK_OK;
}

static void sink_abort(void* opaque) {
    persistence_sink_state* state = (persistence_sink_state*)opaque;
    if (state->batch_context != NULL) {
        MemoryContextDelete(state->batch_context);
        state->batch_context = NULL;
    }
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
#if !defined(LAPLACE_TEST_COMPOSITION_REPLAY_RECEIPT_VERIFY_BYPASS)
    static const char verify_sql_text[] =
        "SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".canonical_deposit_receipt WHERE "
        "receipt_id=$1 AND context_fingerprint=$2 AND source_fingerprint=$3 AND recipe_fingerprint=$4 AND stream_fingerprint=$5 AND sink_artifacts_fingerprint=$6 AND "
        "total_records=$7 AND total_bytes=$8 AND batch_count=$9 AND sink_count=$10 AND record_type=$11 AND effect_disposition=$12 AND status=$13 AND "
        "entity_count=$14 AND physicality_count=$15 AND trajectory_vertex_count=$16 AND occurrence_count=$17 AND logical_occurrence_count=$18 AND plan_sequence_fingerprint=$19 AND plan_count=$20";
#endif
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
    values[15] = laplace_pg_numeric_from_uint64(state->summary.trajectory_segment_count);
    values[16] = laplace_pg_numeric_from_uint64(state->summary.attestation_count);
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
#if !defined(LAPLACE_TEST_COMPOSITION_REPLAY_RECEIPT_VERIFY_BYPASS)
    laplace_pg_keep_plan(
        &deposit_receipt_verify_plan, verify_sql_text, 20, types);
    result = SPI_execute_plan(deposit_receipt_verify_plan, values, NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        laplace_pg_scalar_count("persistence receipt verification") != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace deposit receipt collides with different stored fields")));
    }
#endif
}

static int persistence_producer_never_cancel(void* state) {
    (void)state;
    return 0;
}

static void persistence_producer_observe_progress(
    void* state,
    const laplace_framework_replay_checkpoint* checkpoint) {
    (void)state;
    (void)checkpoint;
}

void LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_v1* producer,
    laplace_pg_persistence_producer_result* result) {
    laplace_framework_producer_plan plan;
    laplace_framework_canonical_batch* batches;
    laplace_digest256 cursor;
    laplace_digest256 completion;
    laplace_digest256 preflight_stream_fingerprint;
    laplace_persistence_summary summary;
    laplace_framework_sink_v1 sink;
    laplace_framework_producer_control_v1 control;
    persistence_sink_state state;
    uint32_t record_type = 0;
    uint64_t total_records = 0;
    uint64_t total_bytes = 0;
    uint64_t index;
    laplace_framework_status framework_status;
    if (context == NULL || source_fingerprint == NULL ||
        recipe_fingerprint == NULL || producer == NULL || result == NULL ||
        producer->prepare == NULL || producer->next == NULL ||
        producer->finish == NULL || producer->abort == NULL ||
        producer->abi_major != LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR ||
        producer->abi_minor > LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace persistence producer arguments are invalid")));
    }
    memset(result, 0, sizeof(*result));
    if ((context->flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("read-only Laplace execution context cannot deposit canonical state")));
    }
    memset(&plan, 0, sizeof(plan));
    if (producer->prepare(
            producer->state, context, source_fingerprint, recipe_fingerprint,
            &plan) != LAPLACE_FRAMEWORK_OK ||
        plan.batch_count == 0u || plan.batch_count > SIZE_MAX / sizeof(*batches)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persistence producer preflight plan is invalid")));
    }
    batches = (laplace_framework_canonical_batch*)palloc0(
        (size_t)plan.batch_count * sizeof(*batches));
    for (index = 0; index < plan.batch_count; ++index) {
        if (producer->next(
                producer->state, index, &batches[index], &cursor) !=
            LAPLACE_FRAMEWORK_OK) {
            producer->abort(producer->state);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace persistence producer preflight batch failed"),
                     errdetail("batch=%llu", (unsigned long long)index)));
        }
    }
    if (producer->finish(producer->state, &completion) != LAPLACE_FRAMEWORK_OK ||
        laplace_persistence_validate_stream(
            batches, (size_t)plan.batch_count, &summary) !=
            LAPLACE_PERSISTENCE_OK ||
        laplace_framework_canonical_stream_fingerprint(
            batches, (size_t)plan.batch_count,
            &preflight_stream_fingerprint, &record_type,
            &total_records, &total_bytes) != LAPLACE_FRAMEWORK_OK ||
        record_type != LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE ||
        total_records != plan.total_records || total_bytes != plan.total_bytes) {
        producer->abort(producer->state);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persistence producer failed whole-stream preflight")));
    }

    memset(&state, 0, sizeof(state));
    state.summary = summary;
    memset(&sink, 0, sizeof(sink));
    sink.state = &state;
    sink.begin = sink_begin;
    sink.stage = sink_stage;
    sink.seal = sink_seal;
    sink.abort = sink_abort;
    sink.abi_major = LAPLACE_FRAMEWORK_SINK_ABI_MAJOR;
    sink.abi_minor = LAPLACE_FRAMEWORK_SINK_ABI_MINOR;
    memset(&control, 0, sizeof(control));
    control.cancel_requested = persistence_producer_never_cancel;
    control.observe_progress = persistence_producer_observe_progress;
    control.abi_major = LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR;
    control.abi_minor = LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR;
    framework_status = laplace_framework_run_producer(
        context, source_fingerprint, recipe_fingerprint,
        producer, &control, &sink, 1u, &result->producer);
    if (framework_status != LAPLACE_FRAMEWORK_OK ||
        memcmp(
            result->producer.stream.stream_fingerprint.bytes,
            preflight_stream_fingerprint.bytes,
            sizeof(preflight_stream_fingerprint.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persistence producer execution diverged from preflight"),
                 errdetail("framework_status=%d producer_status=%d stream_status=%d failed_batch=%llu failed_sink=%llu",
                           (int)framework_status,
                           (int)result->producer.status,
                           (int)result->producer.stream.status,
                           (unsigned long long)result->producer.stream.failed_batch_index,
                           (unsigned long long)result->producer.stream.failed_sink_index)));
    }
    persist_deposit_receipt(&state, &result->producer.stream);
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("cannot close Laplace persistence SPI provider")));
    }
    state.spi_connected = 0;
    result->summary = summary;
    memcpy(result->inserted, state.inserted, sizeof(result->inserted));
    result->plan_sequence_fingerprint = state.plan_sequence_fingerprint;
    result->plan_count = state.plan_count;
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
    result_values[12] = laplace_pg_numeric_from_uint64(summary.trajectory_segment_count);
    result_values[13] = laplace_pg_numeric_from_uint64(summary.attestation_count);
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
