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

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/isa.h"
#include "laplace/reference_topology.h"
#include "laplace_pg_internal.h"
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

static ArrayType* record_array(
    const laplace_reference_record* records,
    size_t record_count,
    Oid* array_oid) {
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
    Datum* rows = (Datum*)palloc(sizeof(*rows) * record_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "reference_topology_record", types, typmods, 20, &binding);
    for (index = 0u; index < record_count; ++index) {
        const laplace_reference_record* record = &records[index];
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
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(
        &binding, rows, (uint64_t)record_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static void persist_topology(
    ArrayType* records,
    Oid records_array_oid,
    const laplace_reference_topology_receipt* topology_receipt,
    const laplace_isa_receipt* isa_receipt) {
    static const char coordinates_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".reference_coordinate(kind,authority,release,namespace,local_identifier,version,coordinate,collision_fingerprint) SELECT DISTINCT kind,authority,release,namespace,local_identifier,version,coordinate,collision_fingerprint FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_topology_record[]) ON CONFLICT DO NOTHING";
    static const char coordinates_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT DISTINCT kind,authority,release,namespace,local_identifier,version,coordinate,collision_fingerprint FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_topology_record[])) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_coordinate c ON c.coordinate=i.coordinate WHERE c.coordinate IS NULL OR c.kind<>i.kind OR c.authority<>i.authority OR c.release<>i.release OR c.namespace<>i.namespace OR c.local_identifier<>i.local_identifier OR c.version<>i.version OR c.collision_fingerprint<>i.collision_fingerprint)";
    static const char occurrences_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".reference_occurrence(reference_id,occurrence_id,source_profile_id,coordinate,row_entity_id,field_entity_id,value_entity_id,source_ordinal,artifact_ordinal,row_ordinal,column_ordinal,rule_flags,disposition) SELECT reference_id,occurrence_id,source_profile_id,coordinate,row_entity_id,field_entity_id,value_entity_id,source_ordinal,artifact_ordinal,row_ordinal,column_ordinal,rule_flags,disposition FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_topology_record[]) ON CONFLICT DO NOTHING";
    static const char occurrences_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_topology_record[]) r) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_occurrence o ON o.reference_id=i.reference_id WHERE o.reference_id IS NULL OR o.occurrence_id<>i.occurrence_id OR o.source_profile_id<>i.source_profile_id OR o.coordinate<>i.coordinate OR o.row_entity_id<>i.row_entity_id OR o.field_entity_id<>i.field_entity_id OR o.value_entity_id<>i.value_entity_id OR o.source_ordinal<>i.source_ordinal OR o.artifact_ordinal<>i.artifact_ordinal OR o.row_ordinal<>i.row_ordinal OR o.column_ordinal<>i.column_ordinal OR o.rule_flags<>i.rule_flags OR o.disposition<>i.disposition)";
    static const char receipt_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".reference_topology_receipt(receipt_id,source_profile_id,input_fingerprint,output_fingerprint,isa_receipt_id,occurrence_count,coordinate_count,present_count,retired_count,unresolved_count,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) ON CONFLICT DO NOTHING";
    static const char receipt_verify_sql[] =
        "SELECT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".reference_topology_receipt WHERE receipt_id=$1 AND source_profile_id=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND occurrence_count=$6 AND coordinate_count=$7 AND present_count=$8 AND retired_count=$9 AND unresolved_count=$10 AND version=$11)";
    static const char members_write_sql[] =
        "WITH input AS (SELECT $1::bytea AS receipt_id,(r).occurrence_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".reference_topology_record[]) WITH ORDINALITY r) INSERT INTO " LAPLACE_PG_SCHEMA ".reference_topology_receipt_member(receipt_id,occurrence_id,member_ordinal) SELECT receipt_id,occurrence_id,member_ordinal FROM input ON CONFLICT DO NOTHING";
    static const char members_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT $1::bytea AS receipt_id,(r).occurrence_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".reference_topology_record[]) WITH ORDINALITY r) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_topology_receipt_member m ON m.receipt_id=i.receipt_id AND m.occurrence_id=i.occurrence_id WHERE m.receipt_id IS NULL OR m.member_ordinal<>i.member_ordinal) AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".reference_topology_receipt_member m WHERE m.receipt_id=$1)=(SELECT count(*) FROM input)";
    Oid record_types[1] = {records_array_oid};
    Datum record_values[1] = {PointerGetDatum(records)};
    Oid receipt_types[11] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT4OID};
    Datum receipt_values[11];
    Oid member_types[2] = {BYTEAOID, records_array_oid};
    Datum member_values[2];
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
    member_values[0] = receipt_values[0];
    member_values[1] = PointerGetDatum(records);
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace reference topology persistence could not connect to SPI")));
    }
    laplace_pg_execute_set_write_verify(
        coordinates_write_sql, coordinates_verify_sql,
        1, record_types, record_values, "reference coordinate");
    laplace_pg_execute_set_write_verify(
        occurrences_write_sql, occurrences_verify_sql,
        1, record_types, record_values, "reference occurrence");
    laplace_pg_execute_set_write_verify(
        receipt_write_sql, receipt_verify_sql,
        11, receipt_types, receipt_values, "reference topology receipt");
    laplace_pg_execute_set_write_verify(
        members_write_sql, members_verify_sql,
        2, member_types, member_values, "reference topology membership");
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

Datum LAPLACE_PG_REFERENCE_TOPOLOGY_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    laplace_reference_candidate* candidates;
    laplace_reference_record* records;
    laplace_reference_record* reconstructed_records;
    size_t candidate_count = 0u;
    laplace_reference_topology_receipt semantic_receipt;
    laplace_reference_topology_error semantic_error;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt isa_receipt;
    laplace_isa_error isa_error;
    ArrayType* persisted_records;
    Oid persisted_records_oid;
    Datum result_values[14];
    bool result_nulls[14] = {false};
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
    reconstructed_records = (laplace_reference_record*)palloc0(
        sizeof(*reconstructed_records) * candidate_count);
    memset(values, 0, sizeof(values));
    values[0].data = candidates;
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
    program.context = &context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    memset(&isa_receipt, 0, sizeof(isa_receipt));
    memset(&isa_error, 0, sizeof(isa_error));
    if (laplace_isa_execute(&program, &isa_receipt, &isa_error) !=
        LAPLACE_ISA_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace reference topology ISA execution failed"),
                 errdetail("status=%d instruction=%llu",
                           (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }
    memset(&semantic_receipt, 0, sizeof(semantic_receipt));
    memset(&semantic_error, 0, sizeof(semantic_error));
    if (laplace_reference_topology_resolve_batch(
            candidates, candidate_count, reconstructed_records,
            &semantic_receipt,
            &semantic_error) != LAPLACE_REFERENCE_TOPOLOGY_OK ||
        memcmp(records, reconstructed_records,
               sizeof(*records) * candidate_count) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace reference topology semantic receipt reconstruction failed")));
    }
    laplace_pg_persist_execution_receipt(
        &isa_receipt, candidate_count, instruction.opcode);
    persisted_records = record_array(
        records, candidate_count, &persisted_records_oid);
    persist_topology(
        persisted_records, persisted_records_oid,
        &semantic_receipt, &isa_receipt);
    result_values[0] = PointerGetDatum(bytea_field_array(
        records, candidate_count, 0));
    result_values[1] = PointerGetDatum(bytea_field_array(
        records, candidate_count, 1));
    result_values[2] = PointerGetDatum(bytea_field_array(
        records, candidate_count, 2));
    result_values[3] = PointerGetDatum(disposition_array(
        records, candidate_count));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.receipt_id.bytes, 32u));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.source_profile_id.bytes, 32u));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.input_fingerprint.bytes, 32u));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.output_fingerprint.bytes, 32u));
    result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt.receipt_id.bytes, 32u));
    result_values[9] = laplace_pg_numeric_from_uint64(
        semantic_receipt.occurrence_count);
    result_values[10] = laplace_pg_numeric_from_uint64(
        semantic_receipt.coordinate_count);
    result_values[11] = laplace_pg_numeric_from_uint64(
        semantic_receipt.present_count);
    result_values[12] = laplace_pg_numeric_from_uint64(
        semantic_receipt.retired_count);
    result_values[13] = laplace_pg_numeric_from_uint64(
        semantic_receipt.unresolved_count);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 14);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
