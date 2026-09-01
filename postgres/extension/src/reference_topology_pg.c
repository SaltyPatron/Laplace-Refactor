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
#include "utils/lsyscache.h"
#include "utils/memutils.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/isa.h"
#include "laplace/reference_topology.h"
#include "laplace_pg_internal.h"
#include "reference_topology_pg.h"
#include "set_pg.h"

#ifndef LAPLACE_PG_REFERENCE_TOPOLOGY_ENTRYPOINT
#define LAPLACE_PG_REFERENCE_TOPOLOGY_ENTRYPOINT \
    LAPLACE_PG_REFERENCE_TOPOLOGY_RESOLVE_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_REFERENCE_TOPOLOGY_ENTRYPOINT);

static void read_id128(Datum datum, laplace_id128* output, const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(output->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly 16 bytes", field)));
    }
    memcpy(output->bytes, VARDATA_ANY(value), sizeof(output->bytes));
}

static uint32_t read_u32(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    const int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace %s cannot be negative", field)));
    }
    return (uint32_t)value;
}

static uint64_t read_u64(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        field);
}

static laplace_reference_candidate* read_candidates(
    ArrayType* array,
    size_t* candidate_count) {
    const Oid type_oid = laplace_pg_composite_type_oid("reference_candidate");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_reference_candidate* candidates;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace reference topology input must be an exact one-dimensional reference_candidate array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace reference topology input cannot be empty")));
    }
    candidates = (laplace_reference_candidate*)palloc0(
        sizeof(*candidates) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace reference topology input cannot contain null candidates")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 1, "reference source_profile_id"),
            &candidates[index].source_profile_id, "reference source_profile_id");
        candidates[index].key.kind = read_u32(tuple, 2, "reference kind");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 3, "reference authority"),
            &candidates[index].key.authority, "reference authority");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 4, "reference release"),
            &candidates[index].key.release, "reference release");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 5, "reference namespace"),
            &candidates[index].key.name_space, "reference namespace");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 6, "reference local_identifier"),
            &candidates[index].key.local_identifier,
            "reference local_identifier");
        candidates[index].key.version = read_u64(
            tuple, 7, "reference version");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 8, "reference row_entity_id"),
            &candidates[index].row_entity_id, "reference row_entity_id");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 9, "reference field_entity_id"),
            &candidates[index].field_entity_id, "reference field_entity_id");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 10, "reference value_entity_id"),
            &candidates[index].value_entity_id, "reference value_entity_id");
        candidates[index].source_ordinal = read_u64(
            tuple, 11, "reference source_ordinal");
        candidates[index].artifact_ordinal = read_u64(
            tuple, 12, "reference artifact_ordinal");
        candidates[index].row_ordinal = read_u64(
            tuple, 13, "reference row_ordinal");
        candidates[index].column_ordinal = read_u64(
            tuple, 14, "reference column_ordinal");
        candidates[index].rule_flags = read_u32(
            tuple, 15, "reference rule_flags");
    }
    *candidate_count = (size_t)count;
    return candidates;
}

typedef struct laplace_pg_reference_record_batch {
    ArrayType* records;
    Oid array_oid;
    size_t record_count;
    uint64_t encoded_bytes;
    uint64_t minimum_encoded_record_bytes;
} laplace_pg_reference_record_batch;

static Datum record_datum(
    const laplace_pg_composite_binding* binding,
    const laplace_reference_record* record) {
    Datum fields[20];
    bool nulls[20] = {false};
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->reference_id.bytes, 32u));
    fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->occurrence_id.bytes, 32u));
    fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.source_profile_id.bytes, 32u));
    fields[3] = Int32GetDatum((int32)record->candidate.key.kind);
    fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.key.authority.bytes, 16u));
    fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.key.release.bytes, 16u));
    fields[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.key.name_space.bytes, 16u));
    fields[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.key.local_identifier.bytes, 16u));
    fields[8] = laplace_pg_numeric_from_uint64(
        record->candidate.key.version);
    fields[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->coordinate.coordinate.bytes, 16u));
    fields[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->coordinate.collision_fingerprint.bytes, 32u));
    fields[11] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.row_entity_id.bytes, 16u));
    fields[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.field_entity_id.bytes, 16u));
    fields[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        record->candidate.value_entity_id.bytes, 16u));
    fields[14] = laplace_pg_numeric_from_uint64(
        record->candidate.source_ordinal);
    fields[15] = laplace_pg_numeric_from_uint64(
        record->candidate.artifact_ordinal);
    fields[16] = laplace_pg_numeric_from_uint64(
        record->candidate.row_ordinal);
    fields[17] = laplace_pg_numeric_from_uint64(
        record->candidate.column_ordinal);
    fields[18] = Int32GetDatum((int32)record->candidate.rule_flags);
    fields[19] = Int32GetDatum((int32)record->disposition);
    return laplace_pg_composite_record(binding, fields, nulls);
}

static laplace_pg_reference_record_batch record_array(
    const laplace_reference_record* records,
    size_t record_count,
    uint64_t preferred_batch_bytes) {
    static const Oid types[20] = {
        BYTEAOID, BYTEAOID, BYTEAOID, INT4OID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, NUMERICOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID,
        INT4OID, INT4OID};
    static const int32 typmods[20] = {
        -1, -1, -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows;
    size_t capacity;
    size_t index;
    const uint64_t batch_byte_limit =
        preferred_batch_bytes > (uint64_t)MaxAllocSize ?
            (uint64_t)MaxAllocSize : preferred_batch_bytes;
    uint64_t encoded_data_bytes = 0u;
    laplace_pg_reference_record_batch result;
    memset(&result, 0, sizeof(result));
    if (record_count == 0u || preferred_batch_bytes == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace reference topology batch requires records and a positive byte limit")));
    }
    capacity = record_count;
    if (capacity > (size_t)(batch_byte_limit / sizeof(*rows))) {
        capacity = (size_t)(batch_byte_limit / sizeof(*rows));
    }
    if (capacity > (size_t)INT_MAX) {
        capacity = (size_t)INT_MAX;
    }
    if (capacity > (size_t)(MaxAllocSize / sizeof(*rows))) {
        capacity = (size_t)(MaxAllocSize / sizeof(*rows));
    }
    if (capacity == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace reference topology batch-byte authority cannot hold one encoded record"),
                 errdetail("preferred_batch_bytes=%llu",
                           (unsigned long long)preferred_batch_bytes)));
    }
    rows = (Datum*)palloc(sizeof(*rows) * capacity);
    laplace_pg_composite_binding_open(
        "reference_topology_record", types, typmods, 20, &binding);
    for (index = 0u; index < capacity; ++index) {
        Datum row = record_datum(&binding, &records[index]);
        const uint64_t row_bytes = (uint64_t)MAXALIGN(
            VARSIZE_ANY(DatumGetPointer(row)));
        const uint64_t projected_bytes =
            (uint64_t)ARR_OVERHEAD_NONULLS(1) +
            encoded_data_bytes + row_bytes;
        if (projected_bytes > batch_byte_limit) {
            if (index == 0u) {
                laplace_pg_composite_binding_close(&binding);
                ereport(ERROR,
                        (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                         errmsg("Laplace reference topology encoded record exceeds its batch-byte authority"),
                         errdetail("record_bytes=%llu preferred_batch_bytes=%llu",
                                   (unsigned long long)projected_bytes,
                                   (unsigned long long)preferred_batch_bytes)));
            }
            break;
        }
        rows[index] = row;
        encoded_data_bytes += row_bytes;
        if (result.minimum_encoded_record_bytes == 0u ||
            row_bytes < result.minimum_encoded_record_bytes) {
            result.minimum_encoded_record_bytes = row_bytes;
        }
    }
    result.records = laplace_pg_composite_array(
        &binding, rows, (uint64_t)index);
    result.array_oid = binding.array_oid;
    result.record_count = index;
    result.encoded_bytes = (uint64_t)VARSIZE_ANY(result.records);
    if (result.encoded_bytes > batch_byte_limit) {
        laplace_pg_composite_binding_close(&binding);
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace reference topology encoded batch exceeded its byte authority"),
                 errdetail("encoded_bytes=%llu preferred_batch_bytes=%llu",
                           (unsigned long long)result.encoded_bytes,
                           (unsigned long long)preferred_batch_bytes)));
    }
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static void persist_topology(
    const laplace_reference_record* records,
    size_t record_count,
    uint64_t preferred_batch_bytes,
    const laplace_reference_topology_receipt* topology_receipt,
    const laplace_isa_receipt* isa_receipt,
    laplace_pg_reference_persistence_measurement* measurement) {
    static const char coordinates_write_sql[] =
        "WITH input AS MATERIALIZED (SELECT DISTINCT kind,authority,release,namespace,local_identifier,version,coordinate,collision_fingerprint FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_topology_record[])),inserted AS (INSERT INTO " LAPLACE_PG_SCHEMA ".reference_coordinate(kind,authority,release,namespace,local_identifier,version,coordinate,collision_fingerprint) SELECT kind,authority,release,namespace,local_identifier,version,coordinate,collision_fingerprint FROM input ON CONFLICT DO NOTHING RETURNING 1) SELECT (SELECT count(*) FROM inserted)+(SELECT count(*) FROM input i JOIN " LAPLACE_PG_SCHEMA ".reference_coordinate c USING(coordinate) WHERE c.kind=i.kind AND c.authority=i.authority AND c.release=i.release AND c.namespace=i.namespace AND c.local_identifier=i.local_identifier AND c.version=i.version AND c.collision_fingerprint=i.collision_fingerprint)=(SELECT count(*) FROM input)";
    static const char occurrences_write_sql[] =
        "WITH input AS MATERIALIZED (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_topology_record[]) r),inserted AS (INSERT INTO " LAPLACE_PG_SCHEMA ".reference_occurrence(reference_id,occurrence_id,source_profile_id,coordinate,row_entity_id,field_entity_id,value_entity_id,source_ordinal,artifact_ordinal,row_ordinal,column_ordinal,rule_flags,disposition) SELECT reference_id,occurrence_id,source_profile_id,coordinate,row_entity_id,field_entity_id,value_entity_id,source_ordinal,artifact_ordinal,row_ordinal,column_ordinal,rule_flags,disposition FROM input ON CONFLICT DO NOTHING RETURNING 1) SELECT (SELECT count(*) FROM inserted)+(SELECT count(*) FROM input i JOIN " LAPLACE_PG_SCHEMA ".reference_occurrence o USING(reference_id) WHERE o.occurrence_id=i.occurrence_id AND o.source_profile_id=i.source_profile_id AND o.coordinate=i.coordinate AND o.row_entity_id=i.row_entity_id AND o.field_entity_id=i.field_entity_id AND o.value_entity_id=i.value_entity_id AND o.source_ordinal=i.source_ordinal AND o.artifact_ordinal=i.artifact_ordinal AND o.row_ordinal=i.row_ordinal AND o.column_ordinal=i.column_ordinal AND o.rule_flags=i.rule_flags AND o.disposition=i.disposition)=(SELECT count(*) FROM input)";
    static const char receipt_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".reference_topology_receipt(receipt_id,source_profile_id,input_fingerprint,output_fingerprint,isa_receipt_id,occurrence_count,coordinate_count,present_count,retired_count,unresolved_count,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) ON CONFLICT DO NOTHING";
    static const char receipt_verify_sql[] =
        "SELECT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".reference_topology_receipt WHERE receipt_id=$1 AND source_profile_id=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND occurrence_count=$6 AND coordinate_count=$7 AND present_count=$8 AND retired_count=$9 AND unresolved_count=$10 AND version=$11)";
    static const char members_write_sql[] =
        "WITH input AS MATERIALIZED (SELECT $1::bytea AS receipt_id,(r).occurrence_id,($3::bigint+ordinality)::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".reference_topology_record[]) WITH ORDINALITY r),inserted AS (INSERT INTO " LAPLACE_PG_SCHEMA ".reference_topology_receipt_member(receipt_id,occurrence_id,member_ordinal) SELECT receipt_id,occurrence_id,member_ordinal FROM input ON CONFLICT DO NOTHING RETURNING 1) SELECT (SELECT count(*) FROM inserted)+(SELECT count(*) FROM input i JOIN " LAPLACE_PG_SCHEMA ".reference_topology_receipt_member m USING(receipt_id,occurrence_id) WHERE m.member_ordinal=i.member_ordinal)=(SELECT count(*) FROM input)";
    static const char members_count_sql[] =
        "SELECT count(*)=$2::bigint FROM " LAPLACE_PG_SCHEMA ".reference_topology_receipt_member WHERE receipt_id=$1";
    Oid receipt_types[11] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT4OID};
    Datum receipt_values[11];
    MemoryContext batch_context;
    MemoryContext prior_context;
    size_t offset;
    int result;
    memset(measurement, 0, sizeof(*measurement));
    receipt_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        topology_receipt->receipt_id.bytes, 32u));
    receipt_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        topology_receipt->source_profile_id.bytes, 32u));
    receipt_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        topology_receipt->input_fingerprint.bytes, 32u));
    receipt_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        topology_receipt->output_fingerprint.bytes, 32u));
    receipt_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt->receipt_id.bytes, 32u));
    receipt_values[5] = Int64GetDatum(laplace_pg_checked_int64(
        topology_receipt->occurrence_count, "reference occurrence count"));
    receipt_values[6] = Int64GetDatum(laplace_pg_checked_int64(
        topology_receipt->coordinate_count, "reference coordinate count"));
    receipt_values[7] = Int64GetDatum(laplace_pg_checked_int64(
        topology_receipt->present_count, "reference present count"));
    receipt_values[8] = Int64GetDatum(laplace_pg_checked_int64(
        topology_receipt->retired_count, "reference retired count"));
    receipt_values[9] = Int64GetDatum(laplace_pg_checked_int64(
        topology_receipt->unresolved_count, "reference unresolved count"));
    receipt_values[10] = Int32GetDatum((int32)topology_receipt->version);
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace reference topology persistence could not connect to SPI")));
    }
    laplace_pg_execute_set_write_verify(
        receipt_write_sql, receipt_verify_sql,
        11, receipt_types, receipt_values, "reference topology receipt");
    batch_context = AllocSetContextCreate(
        CurrentMemoryContext,
        "Laplace reference topology persistence batch",
        ALLOCSET_DEFAULT_SIZES);
    prior_context = MemoryContextSwitchTo(batch_context);
    for (offset = 0u; offset < record_count;) {
        const size_t remaining = record_count - offset;
        laplace_pg_reference_record_batch batch = record_array(
            records + offset, remaining, preferred_batch_bytes);
        Oid record_types[1] = {batch.array_oid};
        Datum record_values[1] = {PointerGetDatum(batch.records)};
        Oid member_types[3] = {BYTEAOID, batch.array_oid, INT8OID};
        Datum member_values[3] = {
            receipt_values[0],
            PointerGetDatum(batch.records),
            Int64GetDatum(laplace_pg_checked_int64(
                (uint64_t)offset, "reference topology member offset"))};
        laplace_pg_execute_set_write_exact(
            coordinates_write_sql,
            1, record_types, record_values, "reference coordinate batch");
        laplace_pg_execute_set_write_exact(
            occurrences_write_sql,
            1, record_types, record_values, "reference occurrence batch");
        laplace_pg_execute_set_write_exact(
            members_write_sql,
            3, member_types, member_values, "reference topology membership batch");
        ++measurement->batch_count;
        if ((uint64_t)batch.record_count > measurement->maximum_batch_records) {
            measurement->maximum_batch_records = (uint64_t)batch.record_count;
        }
        if (batch.encoded_bytes > measurement->maximum_encoded_batch_bytes) {
            measurement->maximum_encoded_batch_bytes = batch.encoded_bytes;
        }
        if (measurement->minimum_encoded_record_bytes == 0u ||
            batch.minimum_encoded_record_bytes <
                measurement->minimum_encoded_record_bytes) {
            measurement->minimum_encoded_record_bytes =
                batch.minimum_encoded_record_bytes;
        }
        offset += batch.record_count;
        MemoryContextSwitchTo(prior_context);
        MemoryContextReset(batch_context);
        prior_context = MemoryContextSwitchTo(batch_context);
    }
    MemoryContextSwitchTo(prior_context);
    MemoryContextDelete(batch_context);
    {
        Oid count_types[2] = {BYTEAOID, INT8OID};
        Datum count_values[2] = {
            receipt_values[0],
            Int64GetDatum(laplace_pg_checked_int64(
                (uint64_t)record_count,
                "reference topology membership count"))};
        result = SPI_execute_with_args(
            members_count_sql, 2, count_types, count_values,
            NULL, false, 1);
        if (result != SPI_OK_SELECT ||
            !laplace_pg_scalar_boolean("reference topology membership count")) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace reference topology membership cardinality conflicts with durable state")));
        }
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace reference topology persistence could not close SPI")));
    }
}

static ArrayType* bytea_field_array(
    const laplace_reference_record* records,
    size_t record_count,
    int field) {
    Datum* values = (Datum*)palloc(sizeof(*values) * record_count);
    size_t index;
    for (index = 0u; index < record_count; ++index) {
        const uint8_t* bytes;
        size_t width;
        if (field == 0) {
            bytes = records[index].reference_id.bytes;
            width = 32u;
        } else if (field == 1) {
            bytes = records[index].coordinate.coordinate.bytes;
            width = 16u;
        } else {
            bytes = records[index].coordinate.collision_fingerprint.bytes;
            width = 32u;
        }
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(bytes, width));
    }
    return construct_array(
        values, (int)record_count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static ArrayType* disposition_array(
    const laplace_reference_record* records,
    size_t record_count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * record_count);
    size_t index;
    for (index = 0u; index < record_count; ++index) {
        values[index] = Int32GetDatum((int32)records[index].disposition);
    }
    return construct_array(
        values, (int)record_count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

void laplace_pg_reference_topology_execute_and_persist(
    const laplace_framework_context* context,
    const laplace_reference_candidate* candidates,
    size_t candidate_count,
    uint64_t preferred_batch_bytes,
    laplace_reference_record* records,
    laplace_pg_reference_topology_execution* execution) {
    laplace_reference_record* reconstructed_records;
    laplace_reference_topology_error semantic_error;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_error isa_error;
    if (context == NULL || candidates == NULL || candidate_count == 0u ||
        preferred_batch_bytes == 0u || records == NULL || execution == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace reference topology execution requires exact nonempty native inputs")));
    }
    memset(execution, 0, sizeof(*execution));
    reconstructed_records = (laplace_reference_record*)palloc0(
        sizeof(*reconstructed_records) * candidate_count);
    memset(values, 0, sizeof(values));
    values[0].data = (void*)candidates;
    values[0].count = (uint64_t)candidate_count;
    values[0].capacity = (uint64_t)candidate_count;
    values[0].stride_bytes = sizeof(*candidates);
    values[0].type = LAPLACE_ISA_VALUE_REFERENCE_CANDIDATE_VECTOR;
    values[1].data = records;
    values[1].capacity = (uint64_t)candidate_count;
    values[1].stride_bytes = sizeof(*records);
    values[1].type = LAPLACE_ISA_VALUE_REFERENCE_RECORD_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_REFERENCE_TOPOLOGY_RESOLVE_BATCH;
    instruction.output_value = 1u;
    instruction.version =
        LAPLACE_ISA_INSTRUCTION_VERSION_REFERENCE_TOPOLOGY_RESOLVE_BATCH;
    memset(&program, 0, sizeof(program));
    program.instructions = &instruction;
    program.values = values;
    program.context = context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    memset(&isa_error, 0, sizeof(isa_error));
    if (laplace_isa_execute(
            &program, &execution->isa_receipt, &isa_error) != LAPLACE_ISA_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace reference topology ISA execution failed"),
                 errdetail("status=%d instruction=%llu",
                           (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }
    memset(&semantic_error, 0, sizeof(semantic_error));
    if (laplace_reference_topology_resolve_batch(
            candidates, candidate_count, reconstructed_records,
            &execution->semantic_receipt, &semantic_error) !=
            LAPLACE_REFERENCE_TOPOLOGY_OK ||
        memcmp(records, reconstructed_records,
               sizeof(*records) * candidate_count) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace reference topology semantic receipt reconstruction failed")));
    }
    pfree(reconstructed_records);
    laplace_pg_persist_execution_receipt(
        &execution->isa_receipt, candidate_count, instruction.opcode);
    persist_topology(
        records, candidate_count, preferred_batch_bytes,
        &execution->semantic_receipt, &execution->isa_receipt,
        &execution->persistence);
}

Datum LAPLACE_PG_REFERENCE_TOPOLOGY_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    const uint64_t preferred_batch_bytes = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(2), "reference topology preferred_batch_bytes");
    laplace_reference_candidate* candidates;
    laplace_reference_record* records;
    size_t candidate_count = 0u;
    laplace_pg_reference_topology_execution execution;
    Datum result_values[18];
    bool result_nulls[18] = {false};
    HeapTuple result_tuple;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("Laplace reference topology persistence requires a writable execution context")));
    }
    candidates = read_candidates(input_array, &candidate_count);
    records = (laplace_reference_record*)palloc0(
        sizeof(*records) * candidate_count);
    laplace_pg_reference_topology_execute_and_persist(
        &context, candidates, candidate_count, preferred_batch_bytes,
        records, &execution);
    result_values[0] = PointerGetDatum(bytea_field_array(
        records, candidate_count, 0));
    result_values[1] = PointerGetDatum(bytea_field_array(
        records, candidate_count, 1));
    result_values[2] = PointerGetDatum(bytea_field_array(
        records, candidate_count, 2));
    result_values[3] = PointerGetDatum(disposition_array(
        records, candidate_count));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.receipt_id.bytes, 32u));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.source_profile_id.bytes, 32u));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.input_fingerprint.bytes, 32u));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.semantic_receipt.output_fingerprint.bytes, 32u));
    result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        execution.isa_receipt.receipt_id.bytes, 32u));
    result_values[9] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.occurrence_count);
    result_values[10] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.coordinate_count);
    result_values[11] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.present_count);
    result_values[12] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.retired_count);
    result_values[13] = laplace_pg_numeric_from_uint64(
        execution.semantic_receipt.unresolved_count);
    result_values[14] = laplace_pg_numeric_from_uint64(
        execution.persistence.batch_count);
    result_values[15] = laplace_pg_numeric_from_uint64(
        execution.persistence.maximum_batch_records);
    result_values[16] = laplace_pg_numeric_from_uint64(
        execution.persistence.maximum_encoded_batch_bytes);
    result_values[17] = laplace_pg_numeric_from_uint64(
        execution.persistence.minimum_encoded_record_bytes);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 18);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
