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
#include "laplace/reference_mapping.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

#ifndef LAPLACE_PG_REFERENCE_MAPPING_ENTRYPOINT
#define LAPLACE_PG_REFERENCE_MAPPING_ENTRYPOINT \
    LAPLACE_PG_REFERENCE_MAPPING_RESOLVE_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_REFERENCE_MAPPING_ENTRYPOINT);

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

static void read_coordinate(
    HeapTupleHeader tuple,
    int coordinate_attribute,
    int fingerprint_attribute,
    int kind_attribute,
    int version_attribute,
    const char* prefix,
    laplace_highway_coordinate* output) {
    read_id128(
        laplace_pg_required_composite_attribute(
            tuple, coordinate_attribute, prefix),
        &output->coordinate, prefix);
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(
            tuple, fingerprint_attribute, prefix),
        &output->collision_fingerprint, prefix);
    output->kind = read_u32(tuple, kind_attribute, prefix);
    output->version = read_u64(tuple, version_attribute, prefix);
}

static laplace_reference_mapping_candidate* read_candidates(
    ArrayType* array,
    size_t* candidate_count) {
    const Oid type_oid = laplace_pg_composite_type_oid(
        "reference_mapping_candidate");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_reference_mapping_candidate* candidates;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace reference mapping input must be an exact one-dimensional reference_mapping_candidate array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace reference mapping input cannot be empty")));
    }
    candidates = (laplace_reference_mapping_candidate*)palloc0(
        sizeof(*candidates) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace reference mapping input cannot contain null candidates")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 1, "mapping boundary_id"),
            &candidates[index].boundary_id, "mapping boundary_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 2, "mapping source_profile_id"),
            &candidates[index].source_profile_id,
            "mapping source_profile_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 3, "mapping left_reference_id"),
            &candidates[index].left_reference_id,
            "mapping left_reference_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 4, "mapping right_reference_id"),
            &candidates[index].right_reference_id,
            "mapping right_reference_id");
        read_coordinate(
            tuple, 5, 6, 7, 8, "mapping left coordinate",
            &candidates[index].left_coordinate);
        read_coordinate(
            tuple, 9, 10, 11, 12, "mapping right coordinate",
            &candidates[index].right_coordinate);
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 13, "mapping relation_id"),
            &candidates[index].relation_id, "mapping relation_id");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 14, "mapping row_entity_id"),
            &candidates[index].row_entity_id, "mapping row_entity_id");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 15, "mapping left_field_entity_id"),
            &candidates[index].left_field_entity_id,
            "mapping left_field_entity_id");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 16, "mapping left_value_entity_id"),
            &candidates[index].left_value_entity_id,
            "mapping left_value_entity_id");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 17, "mapping right_field_entity_id"),
            &candidates[index].right_field_entity_id,
            "mapping right_field_entity_id");
        read_id128(
            laplace_pg_required_composite_attribute(
                tuple, 18, "mapping right_value_entity_id"),
            &candidates[index].right_value_entity_id,
            "mapping right_value_entity_id");
        candidates[index].source_ordinal = read_u64(
            tuple, 19, "mapping source_ordinal");
        candidates[index].artifact_ordinal = read_u64(
            tuple, 20, "mapping artifact_ordinal");
        candidates[index].row_ordinal = read_u64(
            tuple, 21, "mapping row_ordinal");
        candidates[index].relation_version = read_u64(
            tuple, 22, "mapping relation_version");
        candidates[index].relation_kind = read_u32(
            tuple, 23, "mapping relation_kind");
        candidates[index].flags = read_u32(
            tuple, 24, "mapping flags");
        candidates[index].left_disposition = read_u32(
            tuple, 25, "mapping left_disposition");
        candidates[index].right_disposition = read_u32(
            tuple, 26, "mapping right_disposition");
    }
    *candidate_count = (size_t)count;
    return candidates;
}

static bool query_boolean(void) {
    bool is_null = false;
    Datum value;
    if (SPI_processed != 1u || SPI_tuptable == NULL) {
        return false;
    }
    value = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    return !is_null && DatumGetBool(value);
}

static ArrayType* record_array(
    const laplace_reference_mapping_record* records,
    size_t record_count,
    Oid* array_oid) {
    static const Oid types[24] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID,
        INT4OID, INT4OID, INT4OID, INT4OID, INT4OID};
    static const int32 typmods[24] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * record_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "reference_mapping_record", types, typmods, 24, &binding);
    for (index = 0u; index < record_count; ++index) {
        const laplace_reference_mapping_record* record = &records[index];
        Datum fields[24];
        bool nulls[24] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->mapping_id.bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->proposition_id.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->occurrence_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.boundary_id.bytes, 32u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.source_profile_id.bytes, 32u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.left_reference_id.bytes, 32u));
        fields[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.right_reference_id.bytes, 32u));
        fields[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.left_coordinate.coordinate.bytes, 16u));
        fields[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.right_coordinate.coordinate.bytes, 16u));
        fields[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.relation_id.bytes, 16u));
        fields[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.row_entity_id.bytes, 16u));
        fields[11] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.left_field_entity_id.bytes, 16u));
        fields[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.left_value_entity_id.bytes, 16u));
        fields[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.right_field_entity_id.bytes, 16u));
        fields[14] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            record->candidate.right_value_entity_id.bytes, 16u));
        fields[15] = laplace_pg_numeric_from_uint64(
            record->candidate.source_ordinal);
        fields[16] = laplace_pg_numeric_from_uint64(
            record->candidate.artifact_ordinal);
        fields[17] = laplace_pg_numeric_from_uint64(
            record->candidate.row_ordinal);
        fields[18] = laplace_pg_numeric_from_uint64(
            record->candidate.relation_version);
        fields[19] = Int32GetDatum((int32)record->candidate.relation_kind);
        fields[20] = Int32GetDatum((int32)record->candidate.flags);
        fields[21] = Int32GetDatum((int32)record->candidate.left_disposition);
        fields[22] = Int32GetDatum((int32)record->candidate.right_disposition);
        fields[23] = Int32GetDatum((int32)record->disposition);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(
        &binding, rows, (uint64_t)record_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static void validate_reference_inputs(ArrayType* candidates) {
    static const char references_sql[] =
        "WITH input AS (SELECT (candidate).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_mapping_candidate[]) candidate) "
        "SELECT NOT EXISTS (SELECT FROM input i "
        "LEFT JOIN " LAPLACE_PG_SCHEMA ".source_profile p ON p.profile_id=i.source_profile_id "
        "LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_occurrence l ON l.reference_id=i.left_reference_id "
        "LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_coordinate lc ON lc.coordinate=l.coordinate "
        "LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_occurrence r ON r.reference_id=i.right_reference_id "
        "LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_coordinate rc ON rc.coordinate=r.coordinate "
        "WHERE p.profile_id IS NULL OR p.selected_boundary_fingerprint<>i.boundary_id "
        "OR l.reference_id IS NULL OR lc.coordinate IS NULL "
        "OR l.source_profile_id<>i.source_profile_id OR l.coordinate<>i.left_coordinate "
        "OR lc.collision_fingerprint<>i.left_collision_fingerprint OR lc.kind<>i.left_kind OR lc.version<>i.left_version "
        "OR l.row_entity_id<>i.row_entity_id OR l.field_entity_id<>i.left_field_entity_id OR l.value_entity_id<>i.left_value_entity_id "
        "OR l.source_ordinal<>i.source_ordinal OR l.artifact_ordinal<>i.artifact_ordinal OR l.row_ordinal<>i.row_ordinal "
        "OR l.disposition<>i.left_disposition "
        "OR r.reference_id IS NULL OR rc.coordinate IS NULL "
        "OR r.source_profile_id<>i.source_profile_id OR r.coordinate<>i.right_coordinate "
        "OR rc.collision_fingerprint<>i.right_collision_fingerprint OR rc.kind<>i.right_kind OR rc.version<>i.right_version "
        "OR r.row_entity_id<>i.row_entity_id OR r.field_entity_id<>i.right_field_entity_id OR r.value_entity_id<>i.right_value_entity_id "
        "OR r.source_ordinal<>i.source_ordinal OR r.artifact_ordinal<>i.artifact_ordinal OR r.row_ordinal<>i.row_ordinal "
        "OR r.disposition<>i.right_disposition)";
    Oid argument_types[1] = {
        get_array_type(laplace_pg_composite_type_oid(
            "reference_mapping_candidate"))};
    Datum arguments[1] = {PointerGetDatum(candidates)};
    int result;
    if (argument_types[0] == InvalidOid) {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("Laplace reference mapping candidate array type is unavailable")));
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace reference mapping validation could not connect to SPI")));
    }
    result = SPI_execute_with_args(
        references_sql, 1, argument_types, arguments, NULL, true, 1);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace reference mapping input conflicts with durable reference topology")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace reference mapping validation could not close SPI")));
    }
}

static void persist_mappings(
    ArrayType* records,
    Oid records_array_oid,
    const laplace_reference_mapping_receipt* mapping_receipt,
    const laplace_isa_receipt* isa_receipt) {
    static const char propositions_write_sql[] =
        "WITH raw AS (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_mapping_record[]) r),input AS (SELECT DISTINCT proposition_id,CASE WHEN flags=2 AND left_coordinate>right_coordinate THEN right_coordinate ELSE left_coordinate END AS left_coordinate,CASE WHEN flags=2 AND left_coordinate>right_coordinate THEN left_coordinate ELSE right_coordinate END AS right_coordinate,relation_id,relation_kind,relation_version,flags FROM raw) INSERT INTO " LAPLACE_PG_SCHEMA ".reference_mapping_proposition(proposition_id,left_coordinate,right_coordinate,relation_id,relation_kind,relation_version,flags) SELECT proposition_id,left_coordinate,right_coordinate,relation_id,relation_kind,relation_version,flags FROM input ON CONFLICT DO NOTHING";
    static const char propositions_verify_sql[] =
        "WITH raw AS (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_mapping_record[]) r),input AS MATERIALIZED (SELECT DISTINCT proposition_id,CASE WHEN flags=2 AND left_coordinate>right_coordinate THEN right_coordinate ELSE left_coordinate END AS left_coordinate,CASE WHEN flags=2 AND left_coordinate>right_coordinate THEN left_coordinate ELSE right_coordinate END AS right_coordinate,relation_id,relation_kind,relation_version,flags FROM raw) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_mapping_proposition p ON p.proposition_id=i.proposition_id WHERE p.proposition_id IS NULL OR p.left_coordinate<>i.left_coordinate OR p.right_coordinate<>i.right_coordinate OR p.relation_id<>i.relation_id OR p.relation_kind<>i.relation_kind OR p.relation_version<>i.relation_version OR p.flags<>i.flags)";
    static const char occurrences_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".reference_mapping_occurrence(mapping_id,occurrence_id,proposition_id,boundary_id,source_profile_id,left_reference_id,right_reference_id,row_entity_id,left_field_entity_id,left_value_entity_id,right_field_entity_id,right_value_entity_id,source_ordinal,artifact_ordinal,row_ordinal,left_disposition,right_disposition,disposition) SELECT mapping_id,occurrence_id,proposition_id,boundary_id,source_profile_id,left_reference_id,right_reference_id,row_entity_id,left_field_entity_id,left_value_entity_id,right_field_entity_id,right_value_entity_id,source_ordinal,artifact_ordinal,row_ordinal,left_disposition,right_disposition,disposition FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_mapping_record[]) ON CONFLICT DO NOTHING";
    static const char occurrences_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".reference_mapping_record[]) r) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_mapping_occurrence o ON o.mapping_id=i.mapping_id WHERE o.mapping_id IS NULL OR o.occurrence_id<>i.occurrence_id OR o.proposition_id<>i.proposition_id OR o.boundary_id<>i.boundary_id OR o.source_profile_id<>i.source_profile_id OR o.left_reference_id<>i.left_reference_id OR o.right_reference_id<>i.right_reference_id OR o.row_entity_id<>i.row_entity_id OR o.left_field_entity_id<>i.left_field_entity_id OR o.left_value_entity_id<>i.left_value_entity_id OR o.right_field_entity_id<>i.right_field_entity_id OR o.right_value_entity_id<>i.right_value_entity_id OR o.source_ordinal<>i.source_ordinal OR o.artifact_ordinal<>i.artifact_ordinal OR o.row_ordinal<>i.row_ordinal OR o.left_disposition<>i.left_disposition OR o.right_disposition<>i.right_disposition OR o.disposition<>i.disposition)";
    static const char receipt_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".reference_mapping_receipt(receipt_id,boundary_id,input_fingerprint,output_fingerprint,isa_receipt_id,occurrence_count,proposition_count,resolved_count,unresolved_count,retired_count,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) ON CONFLICT DO NOTHING";
    static const char receipt_verify_sql[] =
        "SELECT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".reference_mapping_receipt WHERE receipt_id=$1 AND boundary_id=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND occurrence_count=$6 AND proposition_count=$7 AND resolved_count=$8 AND unresolved_count=$9 AND retired_count=$10 AND version=$11)";
    static const char members_write_sql[] =
        "WITH input AS (SELECT $1::bytea AS receipt_id,(r).occurrence_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".reference_mapping_record[]) WITH ORDINALITY r) INSERT INTO " LAPLACE_PG_SCHEMA ".reference_mapping_receipt_member(receipt_id,occurrence_id,member_ordinal) SELECT receipt_id,occurrence_id,member_ordinal FROM input ON CONFLICT DO NOTHING";
    static const char members_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT $1::bytea AS receipt_id,(r).occurrence_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".reference_mapping_record[]) WITH ORDINALITY r) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".reference_mapping_receipt_member m ON m.receipt_id=i.receipt_id AND m.occurrence_id=i.occurrence_id WHERE m.receipt_id IS NULL OR m.member_ordinal<>i.member_ordinal) AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".reference_mapping_receipt_member m WHERE m.receipt_id=$1)=(SELECT count(*) FROM input)";
    Oid record_types[1] = {records_array_oid};
    Datum record_values[1] = {PointerGetDatum(records)};
    Oid receipt_types[11] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT4OID};
    Datum receipt_values[11];
    Oid member_types[2] = {BYTEAOID, records_array_oid};
    Datum member_values[2];
    receipt_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        mapping_receipt->receipt_id.bytes, 32u));
    receipt_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        mapping_receipt->boundary_id.bytes, 32u));
    receipt_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        mapping_receipt->input_fingerprint.bytes, 32u));
    receipt_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        mapping_receipt->output_fingerprint.bytes, 32u));
    receipt_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt->receipt_id.bytes, 32u));
    receipt_values[5] = Int64GetDatum(laplace_pg_checked_int64(
        mapping_receipt->occurrence_count, "mapping occurrence count"));
    receipt_values[6] = Int64GetDatum(laplace_pg_checked_int64(
        mapping_receipt->proposition_count, "mapping proposition count"));
    receipt_values[7] = Int64GetDatum(laplace_pg_checked_int64(
        mapping_receipt->resolved_count, "mapping resolved count"));
    receipt_values[8] = Int64GetDatum(laplace_pg_checked_int64(
        mapping_receipt->unresolved_count, "mapping unresolved count"));
    receipt_values[9] = Int64GetDatum(laplace_pg_checked_int64(
        mapping_receipt->retired_count, "mapping retired count"));
    receipt_values[10] = Int32GetDatum((int32)mapping_receipt->version);
    member_values[0] = receipt_values[0];
    member_values[1] = PointerGetDatum(records);
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace reference mapping persistence could not connect to SPI")));
    }
    laplace_pg_execute_set_write_verify(
        propositions_write_sql, propositions_verify_sql,
        1, record_types, record_values, "reference mapping proposition");
    laplace_pg_execute_set_write_verify(
        occurrences_write_sql, occurrences_verify_sql,
        1, record_types, record_values, "reference mapping occurrence");
    laplace_pg_execute_set_write_verify(
        receipt_write_sql, receipt_verify_sql,
        11, receipt_types, receipt_values, "reference mapping receipt");
    laplace_pg_execute_set_write_verify(
        members_write_sql, members_verify_sql,
        2, member_types, member_values, "reference mapping membership");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace reference mapping persistence could not close SPI")));
    }
}

static ArrayType* bytea_field_array(
    const laplace_reference_mapping_record* records,
    size_t record_count,
    int field) {
    Datum* values = (Datum*)palloc(sizeof(*values) * record_count);
    size_t index;
    for (index = 0u; index < record_count; ++index) {
        const uint8_t* bytes = field == 0
            ? records[index].mapping_id.bytes
            : (field == 1 ? records[index].proposition_id.bytes
                          : records[index].occurrence_id.bytes);
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(bytes, 32u));
    }
    return construct_array(
        values, (int)record_count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static ArrayType* disposition_array(
    const laplace_reference_mapping_record* records,
    size_t record_count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * record_count);
    size_t index;
    for (index = 0u; index < record_count; ++index) {
        values[index] = Int32GetDatum((int32)records[index].disposition);
    }
    return construct_array(
        values, (int)record_count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

Datum LAPLACE_PG_REFERENCE_MAPPING_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    laplace_reference_mapping_candidate* candidates;
    laplace_reference_mapping_record* records;
    laplace_reference_mapping_record* reconstructed_records;
    size_t candidate_count = 0u;
    laplace_reference_mapping_receipt semantic_receipt;
    laplace_reference_mapping_error semantic_error;
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
                 errmsg("Laplace reference mapping persistence requires a writable execution context")));
    }
    validate_reference_inputs(input_array);
    candidates = read_candidates(input_array, &candidate_count);
    records = (laplace_reference_mapping_record*)palloc0(
        sizeof(*records) * candidate_count);
    reconstructed_records = (laplace_reference_mapping_record*)palloc0(
        sizeof(*reconstructed_records) * candidate_count);
    memset(values, 0, sizeof(values));
    values[0].data = candidates;
    values[0].count = (uint64_t)candidate_count;
    values[0].capacity = (uint64_t)candidate_count;
    values[0].stride_bytes = sizeof(*candidates);
    values[0].type = LAPLACE_ISA_VALUE_REFERENCE_MAPPING_CANDIDATE_VECTOR;
    values[1].data = records;
    values[1].capacity = (uint64_t)candidate_count;
    values[1].stride_bytes = sizeof(*records);
    values[1].type = LAPLACE_ISA_VALUE_REFERENCE_MAPPING_RECORD_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_REFERENCE_MAPPING_RESOLVE_BATCH;
    instruction.output_value = 1u;
    instruction.version =
        LAPLACE_ISA_INSTRUCTION_VERSION_REFERENCE_MAPPING_RESOLVE_BATCH;
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
                 errmsg("Laplace reference mapping ISA execution failed"),
                 errdetail("status=%d instruction=%llu",
                           (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }
    memset(&semantic_receipt, 0, sizeof(semantic_receipt));
    memset(&semantic_error, 0, sizeof(semantic_error));
    if (laplace_reference_mapping_resolve_batch(
            candidates, candidate_count, reconstructed_records,
            &semantic_receipt, &semantic_error) !=
            LAPLACE_REFERENCE_MAPPING_OK ||
        memcmp(records, reconstructed_records,
               sizeof(*records) * candidate_count) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace reference mapping semantic receipt reconstruction failed")));
    }
    laplace_pg_persist_execution_receipt(
        &isa_receipt, candidate_count, instruction.opcode);
    persisted_records = record_array(
        records, candidate_count, &persisted_records_oid);
    persist_mappings(
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
        semantic_receipt.boundary_id.bytes, 32u));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.input_fingerprint.bytes, 32u));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.output_fingerprint.bytes, 32u));
    result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt.receipt_id.bytes, 32u));
    result_values[9] = laplace_pg_numeric_from_uint64(
        semantic_receipt.occurrence_count);
    result_values[10] = laplace_pg_numeric_from_uint64(
        semantic_receipt.proposition_count);
    result_values[11] = laplace_pg_numeric_from_uint64(
        semantic_receipt.resolved_count);
    result_values[12] = laplace_pg_numeric_from_uint64(
        semantic_receipt.unresolved_count);
    result_values[13] = laplace_pg_numeric_from_uint64(
        semantic_receipt.retired_count);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 14);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
