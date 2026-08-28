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
#include "lib/stringinfo.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/evidence_lineage.h"
#include "laplace/isa.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

#ifndef LAPLACE_PG_EVIDENCE_ENTRYPOINT
#define LAPLACE_PG_EVIDENCE_ENTRYPOINT LAPLACE_PG_EVIDENCE_RECORD_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_EVIDENCE_ENTRYPOINT);

static void read_id128(Datum datum, laplace_id128* identifier, const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != sizeof(identifier->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace evidence field %s must contain exactly %zu bytes",
                        field, sizeof(identifier->bytes))));
    }
    memcpy(identifier->bytes, VARDATA_ANY(value), sizeof(identifier->bytes));
}

static laplace_evidence_lineage_record* read_records(
    ArrayType* array,
    size_t* record_count,
    size_t* node_count) {
    const Oid type_oid = laplace_pg_composite_type_oid("evidence_lineage_record");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_evidence_lineage_record* records;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace evidence input must be a one-dimensional exact evidence_lineage_record array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace evidence input cannot be empty")));
    }
    records = (laplace_evidence_lineage_record*)palloc0(
        sizeof(*records) * (size_t)count);
    *node_count = 0u;
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        int32 record_kind;
        int32 epistemic_kind;
        int32 flags;
        int32 reserved;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace evidence input cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 1, "node_id"),
            &records[index].node_id, "evidence node_id");
        read_id128(
            laplace_pg_required_composite_attribute(tuple, 2, "proposition_id"),
            &records[index].proposition_id, "proposition_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 3, "occurrence_id"),
            &records[index].occurrence_id, "evidence occurrence_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 4, "source_id"),
            &records[index].source_id, "evidence source_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 5, "context_id"),
            &records[index].context_id, "evidence context_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 6, "parent_node_id"),
            &records[index].parent_node_id, "evidence parent_node_id");
        records[index].source_ordinal = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(tuple, 7, "source_ordinal"),
            "evidence source_ordinal");
        record_kind = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 8, "record_kind"));
        epistemic_kind = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 9, "epistemic_kind"));
        flags = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 10, "flags"));
        reserved = DatumGetInt32(
            laplace_pg_required_composite_attribute(tuple, 11, "reserved"));
        if (record_kind < 0 || epistemic_kind < 0 || flags < 0 || reserved < 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace evidence record contains a negative unsigned field")));
        }
        records[index].record_kind = (uint32_t)record_kind;
        records[index].epistemic_kind = (uint32_t)epistemic_kind;
        records[index].flags = (uint32_t)flags;
        records[index].reserved = (uint32_t)reserved;
        if (records[index].record_kind == LAPLACE_EVIDENCE_RECORD_NODE) {
            ++*node_count;
        }
    }
    *record_count = (size_t)count;
    return records;
}

static char* cycle_path_text(
    const laplace_digest256* path,
    size_t path_count) {
    StringInfoData text;
    size_t node;
    initStringInfo(&text);
    for (node = 0u; node < path_count; ++node) {
        size_t byte;
        if (node != 0u) {
            appendStringInfoString(&text, " -> ");
        }
        for (byte = 0u; byte < sizeof(path[node].bytes); ++byte) {
            appendStringInfo(&text, "%02x", (unsigned int)path[node].bytes[byte]);
        }
    }
    return text.data;
}

static ArrayType* digest_array(
    const laplace_evidence_root_record* roots,
    size_t count,
    int field) {
    Datum* values = (Datum*)palloc(sizeof(*values) * count);
    size_t index;
    for (index = 0u; index < count; ++index) {
        const uint8_t* bytes = field == 0 ? roots[index].node_id.bytes :
            field == 1 ? roots[index].root_node_id.bytes :
            roots[index].proposition_id.bytes;
        const size_t length = field == 2 ? 16u : 32u;
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(bytes, length));
    }
    return construct_array(values, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static ArrayType* depth_array(
    const laplace_evidence_root_record* roots,
    size_t count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * count);
    size_t index;
    for (index = 0u; index < count; ++index) {
        values[index] = Int64GetDatum(laplace_pg_checked_int64(
            roots[index].path_depth, "evidence path depth"));
    }
    return construct_array(values, (int)count, INT8OID, sizeof(int64), true, TYPALIGN_DOUBLE);
}

static ArrayType* kind_array(
    const laplace_evidence_root_record* roots,
    size_t count) {
    Datum* values = (Datum*)palloc(sizeof(*values) * count);
    size_t index;
    for (index = 0u; index < count; ++index) {
        values[index] = Int32GetDatum((int32)roots[index].root_epistemic_kind);
    }
    return construct_array(values, (int)count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

static void persist_lineage(
    Oid input_array_type,
    Datum input_array,
    const laplace_evidence_root_record* roots,
    size_t root_count,
    const laplace_evidence_lineage_receipt* lineage_receipt,
    const laplace_isa_receipt* isa_receipt) {
    static const char nodes_write_sql[] =
        "WITH input AS (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".evidence_lineage_record[]) r WHERE (r).record_kind=1) INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_node(node_id,proposition_id,occurrence_id,source_id,context_id,source_ordinal,epistemic_kind,flags) SELECT node_id,proposition_id,occurrence_id,source_id,context_id,source_ordinal,epistemic_kind,flags FROM input ON CONFLICT DO NOTHING";
    static const char nodes_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".evidence_lineage_record[]) r WHERE (r).record_kind=1) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".evidence_node n ON n.node_id=i.node_id WHERE n.node_id IS NULL OR n.proposition_id<>i.proposition_id OR n.occurrence_id<>i.occurrence_id OR n.source_id<>i.source_id OR n.context_id<>i.context_id OR n.source_ordinal<>i.source_ordinal OR n.epistemic_kind<>i.epistemic_kind OR n.flags<>i.flags)";
    static const char edges_write_sql[] =
        "WITH input AS (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".evidence_lineage_record[]) r WHERE (r).record_kind=2) INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_dependence(node_id,parent_node_id) SELECT node_id,parent_node_id FROM input ON CONFLICT DO NOTHING";
    static const char edges_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT (r).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".evidence_lineage_record[]) r WHERE (r).record_kind=2) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".evidence_dependence d ON d.node_id=i.node_id AND d.parent_node_id=i.parent_node_id WHERE d.node_id IS NULL)";
    static const char roots_write_sql[] =
        "WITH input(node_id,root_node_id,proposition_id,path_depth,root_kind) AS (SELECT * FROM unnest($1::bytea[],$2::bytea[],$3::bytea[],$4::bigint[],$5::integer[])) INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_root_projection(node_id,root_node_id,proposition_id,path_depth,root_epistemic_kind,flags) SELECT node_id,root_node_id,proposition_id,path_depth,root_kind,0 FROM input ON CONFLICT DO NOTHING";
#if !defined(LAPLACE_TEST_EVIDENCE_ROOT_REPLAY_VERIFY_BYPASS)
    static const char roots_verify_sql[] =
        "WITH input(node_id,root_node_id,proposition_id,path_depth,root_kind) AS MATERIALIZED (SELECT * FROM unnest($1::bytea[],$2::bytea[],$3::bytea[],$4::bigint[],$5::integer[])) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".evidence_root_projection p ON p.node_id=i.node_id AND p.root_node_id=i.root_node_id WHERE p.node_id IS NULL OR p.proposition_id<>i.proposition_id OR p.path_depth<>i.path_depth OR p.root_epistemic_kind<>i.root_kind OR p.flags<>0)";
#endif
    static const char receipt_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt(receipt_id,input_fingerprint,output_fingerprint,isa_receipt_id,input_record_count,node_count,edge_count,root_relation_count,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9) ON CONFLICT DO NOTHING";
    static const char receipt_verify_sql[] =
        "SELECT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt WHERE receipt_id=$1 AND input_fingerprint=$2 AND output_fingerprint=$3 AND isa_receipt_id=$4 AND input_record_count=$5 AND node_count=$6 AND edge_count=$7 AND root_relation_count=$8 AND version=$9)";
    static const char members_write_sql[] =
        "WITH raw AS (SELECT (r).node_id,ordinality FROM unnest($2::" LAPLACE_PG_SCHEMA ".evidence_lineage_record[]) WITH ORDINALITY r WHERE (r).record_kind=1),input AS (SELECT $1::bytea AS receipt_id,node_id,row_number() OVER (ORDER BY ordinality)::numeric AS member_ordinal FROM raw) INSERT INTO " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt_member(receipt_id,node_id,member_ordinal) SELECT receipt_id,node_id,member_ordinal FROM input ON CONFLICT DO NOTHING";
    static const char members_verify_sql[] =
        "WITH raw AS (SELECT (r).node_id,ordinality FROM unnest($2::" LAPLACE_PG_SCHEMA ".evidence_lineage_record[]) WITH ORDINALITY r WHERE (r).record_kind=1),input AS MATERIALIZED (SELECT $1::bytea AS receipt_id,node_id,row_number() OVER (ORDER BY ordinality)::numeric AS member_ordinal FROM raw) SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt_member m ON m.receipt_id=i.receipt_id AND m.node_id=i.node_id WHERE m.receipt_id IS NULL OR m.member_ordinal<>i.member_ordinal) AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".evidence_lineage_receipt_member m WHERE m.receipt_id=$1)=(SELECT count(*) FROM input)";
    Oid one_type[1] = {input_array_type};
    Datum one_value[1] = {input_array};
    Oid root_types[5] = {BYTEAARRAYOID, BYTEAARRAYOID, BYTEAARRAYOID, INT8ARRAYOID, INT4ARRAYOID};
    Datum root_values[5];
    Oid receipt_types[9] = {BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, INT8OID, INT8OID, INT8OID, INT8OID, INT4OID};
    Datum receipt_values[9];
    Oid member_types[2] = {BYTEAOID, input_array_type};
    Datum member_values[2];
    root_values[0] = PointerGetDatum(digest_array(roots, root_count, 0));
    root_values[1] = PointerGetDatum(digest_array(roots, root_count, 1));
    root_values[2] = PointerGetDatum(digest_array(roots, root_count, 2));
    root_values[3] = PointerGetDatum(depth_array(roots, root_count));
    root_values[4] = PointerGetDatum(kind_array(roots, root_count));
    receipt_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(lineage_receipt->receipt_id.bytes, 32u));
    receipt_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(lineage_receipt->input_fingerprint.bytes, 32u));
    receipt_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(lineage_receipt->output_fingerprint.bytes, 32u));
    receipt_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(isa_receipt->receipt_id.bytes, 32u));
    receipt_values[4] = Int64GetDatum(laplace_pg_checked_int64(lineage_receipt->input_record_count, "input record count"));
    receipt_values[5] = Int64GetDatum(laplace_pg_checked_int64(lineage_receipt->node_count, "node count"));
    receipt_values[6] = Int64GetDatum(laplace_pg_checked_int64(lineage_receipt->edge_count, "edge count"));
    receipt_values[7] = Int64GetDatum(laplace_pg_checked_int64(lineage_receipt->root_relation_count, "root relation count"));
    receipt_values[8] = Int32GetDatum((int32)lineage_receipt->version);
    member_values[0] = receipt_values[0];
    member_values[1] = input_array;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE), errmsg("Laplace evidence persistence could not connect to SPI")));
    }
    laplace_pg_execute_set_write_verify(
        nodes_write_sql, nodes_verify_sql, 1, one_type, one_value,
        "evidence node replay");
    laplace_pg_execute_set_write_verify(
        edges_write_sql, edges_verify_sql, 1, one_type, one_value,
        "evidence dependence replay");
#if defined(LAPLACE_TEST_EVIDENCE_ROOT_REPLAY_VERIFY_BYPASS)
    const int result = SPI_execute_with_args(
        roots_write_sql, 5, root_types, root_values, NULL, false, 0);
    if (result != SPI_OK_INSERT) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg("Laplace evidence root projection write was not set-oriented")));
    }
#else
    laplace_pg_execute_set_write_verify(
        roots_write_sql, roots_verify_sql, 5, root_types, root_values,
        "evidence root projection replay");
#endif
    laplace_pg_execute_set_write_verify(
        receipt_write_sql, receipt_verify_sql,
        9, receipt_types, receipt_values, "evidence lineage receipt");
    laplace_pg_execute_set_write_verify(
        members_write_sql, members_verify_sql,
        2, member_types, member_values, "evidence lineage receipt membership");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg("Laplace evidence persistence could not close SPI")));
    }
}

Datum LAPLACE_PG_EVIDENCE_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    const Oid input_array_type = ARR_ELEMTYPE(input_array) == InvalidOid
        ? InvalidOid : get_array_type(ARR_ELEMTYPE(input_array));
    laplace_evidence_lineage_record* records;
    size_t record_count = 0u;
    size_t node_count = 0u;
    const uint64_t requested_capacity = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(2), "evidence root capacity");
    laplace_evidence_root_record* roots;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt isa_receipt;
    laplace_isa_error isa_error;
    laplace_evidence_lineage_receipt lineage_receipt;
    laplace_evidence_lineage_error lineage_error;
    size_t root_count = 0u;
    Datum result_values[13];
    bool result_nulls[13] = {false};
    HeapTuple result_tuple;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    records = read_records(input_array, &record_count, &node_count);
    if (requested_capacity == 0u || requested_capacity > (uint64_t)INT_MAX ||
        requested_capacity > SIZE_MAX / sizeof(*roots)) {
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED), errmsg("Laplace evidence root capacity is outside the executable boundary")));
    }
    roots = (laplace_evidence_root_record*)palloc0(
        sizeof(*roots) * (size_t)requested_capacity);
    memset(values, 0, sizeof(values));
    values[0].data = records;
    values[0].count = (uint64_t)record_count;
    values[0].capacity = (uint64_t)record_count;
    values[0].stride_bytes = sizeof(*records);
    values[0].type = LAPLACE_ISA_VALUE_EVIDENCE_LINEAGE_RECORD_VECTOR;
    values[1].data = roots;
    values[1].capacity = requested_capacity;
    values[1].stride_bytes = sizeof(*roots);
    values[1].type = LAPLACE_ISA_VALUE_EVIDENCE_ROOT_RECORD_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_LINEAGE_BATCH;
    instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_LINEAGE_BATCH;
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
        if (isa_error.status == LAPLACE_ISA_DEPENDENCE_CYCLE) {
            laplace_digest256* cycle = (laplace_digest256*)palloc0(
                sizeof(*cycle) * (node_count + 1u));
            char* path;
            memset(&lineage_receipt, 0, sizeof(lineage_receipt));
            memset(&lineage_error, 0, sizeof(lineage_error));
            lineage_error.cycle_path = cycle;
            lineage_error.cycle_path_capacity = node_count + 1u;
            (void)laplace_evidence_record_lineage_batch(
                records, record_count, context.resource_grant.memory_bytes,
                roots, (size_t)requested_capacity, &root_count,
                &lineage_receipt, &lineage_error);
            path = cycle_path_text(cycle, (size_t)lineage_error.cycle_path_count);
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_RECURSION),
                     errmsg("Laplace evidence dependence cycle rejected"),
                     errdetail("offending path: %s", path)));
        }
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace evidence ISA execution failed"),
                 errdetail("status=%d instruction=%llu", (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }
    root_count = (size_t)values[1].count;
    memset(&lineage_receipt, 0, sizeof(lineage_receipt));
    memset(&lineage_error, 0, sizeof(lineage_error));
    if (laplace_evidence_record_lineage_batch(
            records, record_count, context.resource_grant.memory_bytes,
            roots, (size_t)requested_capacity, &root_count,
            &lineage_receipt, &lineage_error) != LAPLACE_EVIDENCE_LINEAGE_OK) {
        ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION), errmsg("Laplace evidence semantic receipt reconstruction failed")));
    }
    laplace_pg_persist_execution_receipt(&isa_receipt, record_count, instruction.opcode);
    persist_lineage(input_array_type, PointerGetDatum(input_array), roots, root_count,
                    &lineage_receipt, &isa_receipt);
    result_values[0] = PointerGetDatum(digest_array(roots, root_count, 0));
    result_values[1] = PointerGetDatum(digest_array(roots, root_count, 1));
    result_values[2] = PointerGetDatum(digest_array(roots, root_count, 2));
    result_values[3] = PointerGetDatum(depth_array(roots, root_count));
    result_values[4] = PointerGetDatum(kind_array(roots, root_count));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(lineage_receipt.receipt_id.bytes, 32u));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(lineage_receipt.input_fingerprint.bytes, 32u));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(lineage_receipt.output_fingerprint.bytes, 32u));
    result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(isa_receipt.receipt_id.bytes, 32u));
    result_values[9] = laplace_pg_numeric_from_uint64(lineage_receipt.input_record_count);
    result_values[10] = laplace_pg_numeric_from_uint64(lineage_receipt.node_count);
    result_values[11] = laplace_pg_numeric_from_uint64(lineage_receipt.edge_count);
    result_values[12] = laplace_pg_numeric_from_uint64(lineage_receipt.root_relation_count);
    result_tuple = laplace_pg_form_result_tuple(fcinfo, result_values, result_nulls, 13);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
