#include "postgres.h"

#include <limits.h>
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
#include "laplace/evidence_testimony.h"
#include "laplace/isa.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

#ifndef LAPLACE_PG_EVIDENCE_TESTIMONY_ENTRYPOINT
#define LAPLACE_PG_EVIDENCE_TESTIMONY_ENTRYPOINT \
    LAPLACE_PG_EVIDENCE_TESTIMONY_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_EVIDENCE_TESTIMONY_ENTRYPOINT);

static laplace_evidence_testimony_record* read_records(
    ArrayType* array,
    size_t* record_count) {
    const Oid type_oid = laplace_pg_composite_type_oid("evidence_testimony_record");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_evidence_testimony_record* records;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace testimony input must be a one-dimensional exact evidence_testimony_record array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace testimony input cannot be empty")));
    }
    records = (laplace_evidence_testimony_record*)palloc0(
        sizeof(*records) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        int32 source_type;
        int32 outcome_type;
        int32 disposition;
        int32 flags;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace testimony input cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 1, "testimony_id"),
            &records[index].testimony_id, "testimony_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 2, "evidence_node_id"),
            &records[index].evidence_node_id, "testimony evidence_node_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 3, "source_profile_id"),
            &records[index].source_profile_id, "testimony source_profile_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 4, "recipe_receipt_id"),
            &records[index].recipe_receipt_id, "testimony recipe_receipt_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 5, "trust_input_id"),
            &records[index].trust_input_id, "testimony trust_input_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 6, "outcome_detail_id"),
            &records[index].outcome_detail_id, "testimony outcome_detail_id");
        records[index].uncertainty_numerator = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 7, "uncertainty_numerator"),
            "testimony uncertainty_numerator");
        records[index].uncertainty_denominator = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 8, "uncertainty_denominator"),
            "testimony uncertainty_denominator");
        records[index].sample_count = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 9, "sample_count"),
            "testimony sample_count");
        source_type = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 10, "source_type"));
        outcome_type = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 11, "outcome_type"));
        disposition = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 12, "disposition"));
        flags = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 13, "flags"));
        if (source_type < 0 || outcome_type < 0 || disposition < 0 || flags < 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace testimony record contains a negative unsigned field")));
        }
        records[index].source_type = (uint32_t)source_type;
        records[index].outcome_type = (uint32_t)outcome_type;
        records[index].disposition = (uint32_t)disposition;
        records[index].flags = (uint32_t)flags;
    }
    *record_count = (size_t)count;
    return records;
}

static ArrayType* testimony_ids(
    const laplace_evidence_testimony_record* records,
    size_t record_count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * record_count);
    size_t index;
    for (index = 0u; index < record_count; ++index) {
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            records[index].testimony_id.bytes, 32u));
    }
    return construct_array(
        values, (int)record_count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static void persist_testimony(
    Oid input_array_type,
    Datum input_array,
    const laplace_evidence_testimony_receipt* testimony_receipt,
    const laplace_isa_receipt* isa_receipt) {
    static const char records_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_testimony(testimony_id,evidence_node_id,source_profile_id,recipe_receipt_id,trust_input_id,outcome_detail_id,uncertainty_numerator,uncertainty_denominator,sample_count,source_type,outcome_type,disposition,flags) SELECT testimony_id,evidence_node_id,source_profile_id,recipe_receipt_id,trust_input_id,outcome_detail_id,uncertainty_numerator,uncertainty_denominator,sample_count,source_type,outcome_type,disposition,flags FROM unnest($1::" LAPLACE_PG_SCHEMA ".evidence_testimony_record[]) ON CONFLICT DO NOTHING";
#if !defined(LAPLACE_TEST_EVIDENCE_TESTIMONY_REPLAY_VERIFY_BYPASS)
    static const char records_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".evidence_testimony_record[]) r) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony t ON t.testimony_id=i.testimony_id WHERE t.testimony_id IS NULL OR t.evidence_node_id<>i.evidence_node_id OR t.source_profile_id<>i.source_profile_id OR t.recipe_receipt_id<>i.recipe_receipt_id OR t.trust_input_id<>i.trust_input_id OR t.outcome_detail_id<>i.outcome_detail_id OR t.uncertainty_numerator<>i.uncertainty_numerator OR t.uncertainty_denominator<>i.uncertainty_denominator OR t.sample_count<>i.sample_count OR t.source_type<>i.source_type OR t.outcome_type<>i.outcome_type OR t.disposition<>i.disposition OR t.flags<>i.flags)";
#endif
    static const char receipt_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt(receipt_id,source_profile_id,input_fingerprint,output_fingerprint,isa_receipt_id,testimony_count,sample_count,uncertain_count,negative_disposition_count,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10) ON CONFLICT DO NOTHING";
    static const char receipt_verify_sql[] =
        "SELECT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt WHERE receipt_id=$1 AND source_profile_id=$2 AND input_fingerprint=$3 AND output_fingerprint=$4 AND isa_receipt_id=$5 AND testimony_count=$6 AND sample_count=$7 AND uncertain_count=$8 AND negative_disposition_count=$9 AND version=$10)";
    static const char members_write_sql[] =
        "WITH input AS (SELECT $1::bytea AS receipt_id,(r).testimony_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".evidence_testimony_record[]) WITH ORDINALITY r) INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt_member(receipt_id,testimony_id,member_ordinal) SELECT receipt_id,testimony_id,member_ordinal FROM input ON CONFLICT DO NOTHING";
    static const char members_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT $1::bytea AS receipt_id,(r).testimony_id,ordinality::numeric AS member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".evidence_testimony_record[]) WITH ORDINALITY r) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt_member m ON m.receipt_id=i.receipt_id AND m.testimony_id=i.testimony_id WHERE m.receipt_id IS NULL OR m.member_ordinal<>i.member_ordinal) AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".evidence_testimony_receipt_member m WHERE m.receipt_id=$1)=(SELECT count(*) FROM input)";
    Oid record_types[1] = {input_array_type};
    Datum record_values[1] = {input_array};
    Oid receipt_types[10] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT8OID, INT4OID};
    Datum receipt_values[10];
    Oid member_types[2] = {BYTEAOID, input_array_type};
    Datum member_values[2];
    receipt_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        testimony_receipt->receipt_id.bytes, 32u));
    receipt_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        testimony_receipt->source_profile_id.bytes, 32u));
    receipt_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        testimony_receipt->input_fingerprint.bytes, 32u));
    receipt_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        testimony_receipt->output_fingerprint.bytes, 32u));
    receipt_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt->receipt_id.bytes, 32u));
    receipt_values[5] = Int64GetDatum(laplace_pg_checked_int64(
        testimony_receipt->testimony_count, "testimony count"));
    receipt_values[6] = Int64GetDatum(laplace_pg_checked_int64(
        testimony_receipt->sample_count, "testimony sample count"));
    receipt_values[7] = Int64GetDatum(laplace_pg_checked_int64(
        testimony_receipt->uncertain_count, "testimony uncertain count"));
    receipt_values[8] = Int64GetDatum(laplace_pg_checked_int64(
        testimony_receipt->negative_disposition_count,
        "testimony negative disposition count"));
    receipt_values[9] = Int32GetDatum((int32)testimony_receipt->version);
    member_values[0] = receipt_values[0];
    member_values[1] = input_array;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace testimony persistence could not connect to SPI")));
    }
#ifdef LAPLACE_TEST_EVIDENCE_TESTIMONY_REPLAY_VERIFY_BYPASS
    const int result = SPI_execute_with_args(
        records_write_sql, 1, record_types, record_values, NULL, false, 0);
    if (result != SPI_OK_INSERT) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace testimony write was not set-oriented")));
    }
#else
    laplace_pg_execute_set_write_verify(
        records_write_sql, records_verify_sql,
        1, record_types, record_values, "testimony replay");
#endif
    laplace_pg_execute_set_write_verify(
        receipt_write_sql, receipt_verify_sql,
        10, receipt_types, receipt_values, "testimony receipt");
    laplace_pg_execute_set_write_verify(
        members_write_sql, members_verify_sql,
        2, member_types, member_values, "testimony receipt membership");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace testimony persistence could not close SPI")));
    }
}

Datum LAPLACE_PG_EVIDENCE_TESTIMONY_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    const Oid input_array_type = ARR_ELEMTYPE(input_array) == InvalidOid
        ? InvalidOid : get_array_type(ARR_ELEMTYPE(input_array));
    laplace_evidence_testimony_record* records;
    size_t record_count = 0u;
    laplace_evidence_testimony_receipt semantic_receipt;
    laplace_evidence_testimony_receipt output_receipt;
    laplace_evidence_testimony_error semantic_error;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt isa_receipt;
    laplace_isa_error isa_error;
    Datum result_values[10];
    bool result_nulls[10] = {false};
    HeapTuple result_tuple;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    records = read_records(input_array, &record_count);
    memset(&output_receipt, 0, sizeof(output_receipt));
    memset(values, 0, sizeof(values));
    values[0].data = records;
    values[0].count = (uint64_t)record_count;
    values[0].capacity = (uint64_t)record_count;
    values[0].stride_bytes = sizeof(*records);
    values[0].type = LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECORD_VECTOR;
    values[1].data = &output_receipt;
    values[1].capacity = 1u;
    values[1].stride_bytes = sizeof(output_receipt);
    values[1].type = LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECEIPT_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_TESTIMONY_BATCH;
    instruction.version =
        LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_TESTIMONY_BATCH;
    instruction.output_value = 1u;
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
    if (laplace_isa_execute(&program, &isa_receipt, &isa_error) != LAPLACE_ISA_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace testimony ISA execution failed"),
                 errdetail("status=%d instruction=%llu",
                           (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }
    memset(&semantic_receipt, 0, sizeof(semantic_receipt));
    memset(&semantic_error, 0, sizeof(semantic_error));
    if (laplace_evidence_record_testimony_batch(
            records, record_count, &semantic_receipt, &semantic_error) !=
            LAPLACE_EVIDENCE_TESTIMONY_OK ||
        memcmp(&semantic_receipt, &output_receipt, sizeof(semantic_receipt)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace testimony semantic receipt reconstruction failed")));
    }
    laplace_pg_persist_execution_receipt(
        &isa_receipt, record_count, instruction.opcode);
    persist_testimony(
        input_array_type, PointerGetDatum(input_array),
        &semantic_receipt, &isa_receipt);
    result_values[0] = PointerGetDatum(testimony_ids(records, record_count));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.receipt_id.bytes, 32u));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.source_profile_id.bytes, 32u));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.input_fingerprint.bytes, 32u));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.output_fingerprint.bytes, 32u));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt.receipt_id.bytes, 32u));
    result_values[6] = laplace_pg_numeric_from_uint64(
        semantic_receipt.testimony_count);
    result_values[7] = laplace_pg_numeric_from_uint64(
        semantic_receipt.sample_count);
    result_values[8] = laplace_pg_numeric_from_uint64(
        semantic_receipt.uncertain_count);
    result_values[9] = laplace_pg_numeric_from_uint64(
        semantic_receipt.negative_disposition_count);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 10);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
