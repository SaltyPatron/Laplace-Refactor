#include "postgres.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/numeric.h"
#include "utils/syscache.h"

#include "laplace/isa.h"
#include "laplace/framework.h"
#include "laplace/trajectory.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace_pg_internal.h"
#include "perfcache_pg.h"

PG_MODULE_MAGIC;

void _PG_init(void);

void _PG_init(void) {
    laplace_pg_perfcache_initialize();
}

PG_FUNCTION_INFO_V1(LAPLACE_PG_IDENTITY_CALCULATE_SYMBOL);
PG_FUNCTION_INFO_V1(LAPLACE_PG_IDENTITY_EXECUTE_SYMBOL);
PG_FUNCTION_INFO_V1(LAPLACE_PG_TRAJECTORY_CALCULATE_SYMBOL);
PG_FUNCTION_INFO_V1(LAPLACE_PG_TRAJECTORY_EXECUTE_SYMBOL);

static SPIPlanPtr receipt_insert_plan = NULL;
static SPIPlanPtr receipt_select_plan = NULL;

static Datum required_composite_attribute(
    HeapTupleHeader tuple,
    int attribute_number,
    const char* attribute_name) {
    bool is_null = false;
    Datum value = GetAttributeByNum(tuple, attribute_number, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                 errmsg("Laplace execution context field %s cannot be null",
                        attribute_name)));
    }
    return value;
}

static void read_digest(Datum datum, laplace_digest256* digest, const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(digest->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace execution context field %s must contain exactly %zu bytes",
                        field, sizeof(digest->bytes))));
    }
    memcpy(digest->bytes, VARDATA_ANY(value), sizeof(digest->bytes));
}

void laplace_pg_read_execution_context(
    Datum datum,
    laplace_framework_context* context) {
    HeapTupleHeader tuple = DatumGetHeapTupleHeader(datum);
    ArrayType* epochs;
    Datum* epoch_datums = NULL;
    bool* epoch_nulls = NULL;
    int epoch_count = 0;
    int64 memory_bytes;
    int64 epoch_mask;
    int32 cpu_slots;
    int32 io_slots;
    int32 flags;
    int16 major;
    int16 minor;
    int index;

    memset(context, 0, sizeof(*context));
    epochs = DatumGetArrayTypeP(required_composite_attribute(tuple, 1, "epochs"));
    if (ARR_NDIM(epochs) != 1 || ARR_ELEMTYPE(epochs) != BYTEAOID ||
        ArrayGetNItems(ARR_NDIM(epochs), ARR_DIMS(epochs)) !=
            LAPLACE_FRAMEWORK_EPOCH_COUNT) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace execution context must carry exactly %u epoch digests",
                        (unsigned int)LAPLACE_FRAMEWORK_EPOCH_COUNT)));
    }
    deconstruct_array(epochs, BYTEAOID, -1, false, TYPALIGN_INT,
                      &epoch_datums, &epoch_nulls, &epoch_count);
    for (index = 0; index < epoch_count; ++index) {
        if (epoch_nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace execution context epoch %d cannot be null", index)));
        }
        read_digest(epoch_datums[index], &context->epochs[index], "epochs");
    }
    read_digest(
        required_composite_attribute(tuple, 2, "authority_fingerprint"),
        &context->authority_fingerprint,
        "authority_fingerprint");

    memory_bytes = DatumGetInt64(
        required_composite_attribute(tuple, 3, "memory_bytes"));
    cpu_slots = DatumGetInt32(
        required_composite_attribute(tuple, 4, "cpu_slots"));
    io_slots = DatumGetInt32(
        required_composite_attribute(tuple, 5, "io_slots"));
    epoch_mask = DatumGetInt64(
        required_composite_attribute(tuple, 6, "epoch_mask"));
    major = DatumGetInt16(
        required_composite_attribute(tuple, 7, "framework_major"));
    minor = DatumGetInt16(
        required_composite_attribute(tuple, 8, "framework_minor"));
    flags = DatumGetInt32(required_composite_attribute(tuple, 9, "flags"));
    if (memory_bytes <= 0 || cpu_slots <= 0 || io_slots < 0 || epoch_mask <= 0 ||
        major < 0 || minor < 0 || flags < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace execution context contains an out-of-range scalar")));
    }
    context->resource_grant.memory_bytes = (uint64_t)memory_bytes;
    context->resource_grant.cpu_slots = (uint32_t)cpu_slots;
    context->resource_grant.io_slots = (uint32_t)io_slots;
    context->epoch_mask = (uint64_t)epoch_mask;
    context->major = (uint16_t)major;
    context->minor = (uint16_t)minor;
    context->flags = (uint32_t)flags;
    if (laplace_framework_context_validate(context) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace execution context violates the native framework contract")));
    }
}

bytea* laplace_pg_bytes_to_bytea(const uint8_t* bytes, size_t length) {
    bytea* value = (bytea*)palloc(VARHDRSZ + length);
    SET_VARSIZE(value, VARHDRSZ + length);
    memcpy(VARDATA(value), bytes, length);
    return value;
}

int64 laplace_pg_checked_int64(uint64_t value, const char* field) {
    if (value > (uint64_t)PG_INT64_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("%s exceeds PostgreSQL bigint", field)));
    }
    return (int64)value;
}

Datum laplace_pg_numeric_from_uint64(uint64_t value) {
    char buffer[32];
    int written = snprintf(
        buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    if (written <= 0 || (size_t)written >= sizeof(buffer)) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("cannot encode unsigned 64-bit value")));
    }
    return DirectFunctionCall3(numeric_in,
                               CStringGetDatum(buffer),
                               ObjectIdGetDatum(InvalidOid),
                               Int32GetDatum(-1));
}

static void ensure_receipt_plans(void) {
    static const char insert_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".execution_receipt ("
        "receipt_id, context_fingerprint, program_fingerprint, input_fingerprint, output_fingerprint, "
        "instruction_count, executed_instruction_count, isa_major, isa_minor, "
        "receipt_detail, status, item_count, opcode) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
        "ON CONFLICT (receipt_id) DO NOTHING";
    static const char select_sql[] =
        "SELECT receipt_id, context_fingerprint, program_fingerprint, input_fingerprint, "
        "output_fingerprint, instruction_count, executed_instruction_count, "
        "isa_major, isa_minor, receipt_detail, status, item_count, opcode "
        "FROM " LAPLACE_PG_SCHEMA ".execution_receipt WHERE receipt_id = $1";
    static Oid insert_types[13] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT2OID, INT2OID,
        INT4OID, INT4OID, INT8OID, INT4OID};
    static Oid select_types[1] = {BYTEAOID};

    if (receipt_insert_plan == NULL) {
        receipt_insert_plan = SPI_prepare(insert_sql, 13, insert_types);
        if (receipt_insert_plan == NULL || SPI_keepplan(receipt_insert_plan) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("cannot prepare the Laplace receipt insert plan")));
        }
    }
    if (receipt_select_plan == NULL) {
        receipt_select_plan = SPI_prepare(select_sql, 1, select_types);
        if (receipt_select_plan == NULL || SPI_keepplan(receipt_select_plan) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("cannot prepare the Laplace receipt verification plan")));
        }
    }
}

static int bytea_datum_matches(Datum datum, const uint8_t* bytes, size_t length) {
    bytea* value = DatumGetByteaPP(datum);
    return (size_t)VARSIZE_ANY_EXHDR(value) == length &&
           memcmp(VARDATA_ANY(value), bytes, length) == 0;
}

static Datum tuple_value(HeapTuple tuple, TupleDesc descriptor, int column) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("stored Laplace receipt contains a null field")));
    }
    return value;
}

static int stored_receipt_matches(
    const laplace_isa_receipt* receipt,
    uint64_t item_count,
    uint32_t opcode) {
    HeapTuple tuple = SPI_tuptable->vals[0];
    TupleDesc descriptor = SPI_tuptable->tupdesc;
    const size_t digest_bytes = sizeof(receipt->receipt_id.bytes);

    return bytea_datum_matches(tuple_value(tuple, descriptor, 1),
                               receipt->receipt_id.bytes, digest_bytes) &&
           bytea_datum_matches(tuple_value(tuple, descriptor, 2),
                               receipt->context_fingerprint.bytes, digest_bytes) &&
           bytea_datum_matches(tuple_value(tuple, descriptor, 3),
                               receipt->program_fingerprint.bytes, digest_bytes) &&
           bytea_datum_matches(tuple_value(tuple, descriptor, 4),
                               receipt->input_fingerprint.bytes, digest_bytes) &&
           bytea_datum_matches(tuple_value(tuple, descriptor, 5),
                               receipt->output_fingerprint.bytes, digest_bytes) &&
           DatumGetInt64(tuple_value(tuple, descriptor, 6)) ==
               laplace_pg_checked_int64(receipt->instruction_count, "instruction count") &&
           DatumGetInt64(tuple_value(tuple, descriptor, 7)) ==
               laplace_pg_checked_int64(receipt->executed_instruction_count,
                             "executed instruction count") &&
           DatumGetInt16(tuple_value(tuple, descriptor, 8)) == (int16)receipt->major &&
           DatumGetInt16(tuple_value(tuple, descriptor, 9)) == (int16)receipt->minor &&
           DatumGetInt32(tuple_value(tuple, descriptor, 10)) ==
               (int32)receipt->receipt_detail &&
           DatumGetInt32(tuple_value(tuple, descriptor, 11)) == (int32)receipt->status &&
           DatumGetInt64(tuple_value(tuple, descriptor, 12)) ==
               laplace_pg_checked_int64(item_count, "item count") &&
           DatumGetInt32(tuple_value(tuple, descriptor, 13)) == (int32)opcode;
}

static void persist_receipt(
    const laplace_isa_receipt* receipt,
    uint64_t item_count,
    uint32_t opcode) {
    Datum values[13];
    Datum select_values[1];
    int result;
    size_t digest_bytes = sizeof(receipt->receipt_id.bytes);

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->receipt_id.bytes, digest_bytes));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->context_fingerprint.bytes, digest_bytes));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->program_fingerprint.bytes, digest_bytes));
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->input_fingerprint.bytes, digest_bytes));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->output_fingerprint.bytes, digest_bytes));
    values[5] = Int64GetDatum(laplace_pg_checked_int64(
        receipt->instruction_count, "instruction count"));
    values[6] = Int64GetDatum(laplace_pg_checked_int64(
        receipt->executed_instruction_count, "executed instruction count"));
    values[7] = Int16GetDatum((int16)receipt->major);
    values[8] = Int16GetDatum((int16)receipt->minor);
    values[9] = Int32GetDatum((int32)receipt->receipt_detail);
    values[10] = Int32GetDatum((int32)receipt->status);
    values[11] = Int64GetDatum(laplace_pg_checked_int64(item_count, "item count"));
    values[12] = Int32GetDatum((int32)opcode);

    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("cannot connect Laplace receipt persistence to SPI")));
    }
    ensure_receipt_plans();
    result = SPI_execute_plan(receipt_insert_plan, values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > 1) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace receipt insert did not produce a bounded result")));
    }
    if (SPI_processed == 0) {
        select_values[0] = values[0];
        result = SPI_execute_plan(receipt_select_plan, select_values, NULL, true, 1);
        if (result != SPI_OK_SELECT || SPI_processed != 1 ||
            !stored_receipt_matches(receipt, item_count, opcode)) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace receipt identity collides with different stored fields")));
        }
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("cannot close Laplace receipt persistence")));
    }
}

HeapTuple laplace_pg_form_result_tuple(
    FunctionCallInfo fcinfo,
    Datum* values,
    bool* nulls,
    int expected_attributes) {
    TupleDesc descriptor = NULL;
    if (get_call_result_type(fcinfo, NULL, &descriptor) != TYPEFUNC_COMPOSITE ||
        descriptor == NULL || descriptor->natts != expected_attributes) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace PostgreSQL result type does not match its binding")));
    }
    return heap_form_tuple(BlessTupleDesc(descriptor), values, nulls);
}

static void receipt_result_values(
    Datum* values,
    int offset,
    const laplace_isa_receipt* receipt,
    uint64_t item_count) {
    const size_t digest_bytes = sizeof(receipt->receipt_id.bytes);
    values[offset] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->receipt_id.bytes, digest_bytes));
    values[offset + 1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->context_fingerprint.bytes, digest_bytes));
    values[offset + 2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->program_fingerprint.bytes, digest_bytes));
    values[offset + 3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->input_fingerprint.bytes, digest_bytes));
    values[offset + 4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt->output_fingerprint.bytes, digest_bytes));
    values[offset + 5] = Int64GetDatum(laplace_pg_checked_int64(
        receipt->instruction_count, "instruction count"));
    values[offset + 6] = Int64GetDatum(laplace_pg_checked_int64(
        receipt->executed_instruction_count, "executed instruction count"));
    values[offset + 7] = Int16GetDatum((int16)receipt->major);
    values[offset + 8] = Int16GetDatum((int16)receipt->minor);
    values[offset + 9] = Int32GetDatum((int32)receipt->receipt_detail);
    values[offset + 10] = Int32GetDatum((int32)receipt->status);
    values[offset + 11] = Int64GetDatum(laplace_pg_checked_int64(item_count, "item count"));
}

static laplace_isa_program make_program(
    const laplace_framework_context* context,
    laplace_isa_instruction* instruction,
    laplace_isa_value_view* values) {
    laplace_isa_program program;
    memset(&program, 0, sizeof(program));
    program.instructions = instruction;
    program.values = values;
    program.context = context;
    program.instruction_count = 1;
    program.value_count = 2;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    return program;
}

static Datum identity_codepoint_batch(
    FunctionCallInfo fcinfo,
    bool publish_receipt) {
    laplace_framework_context context;
    ArrayType* input = PG_GETARG_ARRAYTYPE_P(1);
    Datum* input_datums = NULL;
    bool* input_nulls = NULL;
    int input_count = 0;
    uint32_t* positions;
    laplace_id128* identities;
    laplace_isa_value_view views[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt receipt;
    laplace_isa_error error;
    laplace_isa_status status;
    Datum* entity_datums;
    ArrayType* entity_array;
    Datum result_values[13];
    bool result_nulls[13] = {false};
    HeapTuple result_tuple;
    int index;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    deconstruct_array(input, INT4OID, 4, true, TYPALIGN_INT,
                      &input_datums, &input_nulls, &input_count);
    if (input_count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("identity batch must contain at least one Unicode position")));
    }
    positions = (uint32_t*)palloc(sizeof(*positions) * (size_t)input_count);
    identities = (laplace_id128*)palloc0(sizeof(*identities) * (size_t)input_count);
    for (index = 0; index < input_count; ++index) {
        if (input_nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("identity batch cannot contain null positions")));
        }
        positions[index] = (uint32_t)DatumGetInt32(input_datums[index]);
    }

    memset(views, 0, sizeof(views));
    views[0].data = positions;
    views[0].count = (uint64_t)input_count;
    views[0].capacity = (uint64_t)input_count;
    views[0].stride_bytes = (uint32_t)sizeof(*positions);
    views[0].type = LAPLACE_ISA_VALUE_U32_VECTOR;
    views[1].data = identities;
    views[1].capacity = (uint64_t)input_count;
    views[1].stride_bytes = (uint32_t)sizeof(*identities);
    views[1].type = LAPLACE_ISA_VALUE_ID128_VECTOR;

    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH;
    instruction.input_value = 0;
    instruction.output_value = 1;
    instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH;
    program = make_program(&context, &instruction, views);
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_isa_execute(&program, &receipt, &error);
    if (status != LAPLACE_ISA_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("identity ISA batch rejected status %d at instruction %llu",
                        (int)status,
                        (unsigned long long)error.instruction_index)));
    }
    if (publish_receipt) {
        persist_receipt(&receipt, (uint64_t)input_count, instruction.opcode);
    }

    entity_datums = (Datum*)palloc(sizeof(*entity_datums) * (size_t)input_count);
    for (index = 0; index < input_count; ++index) {
        entity_datums[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            identities[index].bytes, sizeof(identities[index].bytes)));
    }
    entity_array = construct_array(
        entity_datums, input_count, BYTEAOID, -1, false, TYPALIGN_INT);
    result_values[0] = PointerGetDatum(entity_array);
    receipt_result_values(result_values, 1, &receipt, (uint64_t)input_count);
    result_tuple = laplace_pg_form_result_tuple(fcinfo, result_values, result_nulls, 13);
    return HeapTupleGetDatum(result_tuple);
}

Datum LAPLACE_PG_IDENTITY_CALCULATE_SYMBOL(PG_FUNCTION_ARGS) {
    return identity_codepoint_batch(fcinfo, false);
}

Datum LAPLACE_PG_IDENTITY_EXECUTE_SYMBOL(PG_FUNCTION_ARGS) {
    return identity_codepoint_batch(fcinfo, true);
}

static uint64_t read_little_endian_u64(const uint8_t* bytes) {
    uint64_t value = 0;
    size_t index;
    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void decode_sql_carrier(Datum datum, laplace_trajectory_carrier* carrier) {
    bytea* value = DatumGetByteaPP(datum);
    const uint8_t* bytes = (const uint8_t*)VARDATA_ANY(value);
    size_t slot;
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(*carrier)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("trajectory carrier must contain exactly 32 canonical bytes")));
    }
    for (slot = 0; slot < LAPLACE_TRAJECTORY_SLOT_COUNT; ++slot) {
        uint64_t bits = read_little_endian_u64(bytes + slot * 8u);
        memcpy(&carrier->slots[slot], &bits, sizeof(bits));
    }
}

static Oid occurrence_type_oid(void) {
    Oid namespace_id = get_namespace_oid(LAPLACE_PG_SCHEMA, false);
    Oid type_id = GetSysCacheOid2(
        TYPENAMENSP, Anum_pg_type_oid,
        CStringGetDatum("composition_occurrence"), ObjectIdGetDatum(namespace_id));
    if (!OidIsValid(type_id)) {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("Laplace composition occurrence type is not installed")));
    }
    return type_id;
}

static ArrayType* form_occurrence_array(
    const laplace_composition_occurrence* occurrences,
    int occurrence_count) {
    Oid type_id = occurrence_type_oid();
    TupleDesc descriptor = lookup_rowtype_tupdesc(type_id, -1);
    Datum* occurrence_datums =
        (Datum*)palloc(sizeof(*occurrence_datums) * (size_t)occurrence_count);
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    int index;

    for (index = 0; index < occurrence_count; ++index) {
        Datum values[8];
        bool nulls[8] = {false};
        HeapTuple tuple;
        values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            occurrences[index].entity_id.bytes,
            sizeof(occurrences[index].entity_id.bytes)));
        values[1] = laplace_pg_numeric_from_uint64(occurrences[index].logical_ordinal);
        values[2] = Int64GetDatum(laplace_pg_checked_int64(occurrences[index].metadata, "metadata"));
        values[3] = Int64GetDatum((int64)occurrences[index].atom);
        values[4] = Int32GetDatum((int32)occurrences[index].packed_ordinal);
        values[5] = Int32GetDatum((int32)occurrences[index].run_length);
        values[6] = Int16GetDatum((int16)occurrences[index].tier);
        values[7] = BoolGetDatum(occurrences[index].has_atom != 0);
        tuple = heap_form_tuple(descriptor, values, nulls);
        occurrence_datums[index] = HeapTupleGetDatum(tuple);
    }
    ReleaseTupleDesc(descriptor);
    get_typlenbyvalalign(type_id, &type_length, &type_by_value, &type_alignment);
    return construct_array(occurrence_datums, occurrence_count, type_id,
                           type_length, type_by_value, type_alignment);
}

static Datum trajectory_composition_decode_batch(
    FunctionCallInfo fcinfo,
    bool publish_receipt) {
    laplace_framework_context context;
    ArrayType* input = PG_GETARG_ARRAYTYPE_P(1);
    Datum* input_datums = NULL;
    bool* input_nulls = NULL;
    int input_count = 0;
    laplace_trajectory_carrier* carriers;
    laplace_composition_occurrence* occurrences;
    laplace_isa_value_view views[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt receipt;
    laplace_isa_error error;
    laplace_isa_status status;
    uint64_t logical_count = 0;
    Datum result_values[14];
    bool result_nulls[14] = {false};
    HeapTuple result_tuple;
    int index;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    deconstruct_array(input, BYTEAOID, -1, false, TYPALIGN_INT,
                      &input_datums, &input_nulls, &input_count);
    if (input_count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("trajectory batch must contain at least one carrier")));
    }
    carriers = (laplace_trajectory_carrier*)palloc0(
        sizeof(*carriers) * (size_t)input_count);
    occurrences = (laplace_composition_occurrence*)palloc0(
        sizeof(*occurrences) * (size_t)input_count);
    for (index = 0; index < input_count; ++index) {
        if (input_nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("trajectory batch cannot contain null carriers")));
        }
        decode_sql_carrier(input_datums[index], &carriers[index]);
    }

    memset(views, 0, sizeof(views));
    views[0].data = carriers;
    views[0].count = (uint64_t)input_count;
    views[0].capacity = (uint64_t)input_count;
    views[0].stride_bytes = (uint32_t)sizeof(*carriers);
    views[0].type = LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR;
    views[1].data = occurrences;
    views[1].capacity = (uint64_t)input_count;
    views[1].stride_bytes = (uint32_t)sizeof(*occurrences);
    views[1].type = LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR;

    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH;
    instruction.input_value = 0;
    instruction.output_value = 1;
    instruction.version =
        LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH;
    program = make_program(&context, &instruction, views);
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_isa_execute(&program, &receipt, &error);
    if (status != LAPLACE_ISA_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("trajectory ISA batch rejected status %d at instruction %llu",
                        (int)status,
                        (unsigned long long)error.instruction_index)));
    }
    for (index = 0; index < input_count; ++index) {
        if (UINT64_MAX - logical_count < occurrences[index].run_length) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("trajectory logical count overflow")));
        }
        logical_count += occurrences[index].run_length;
    }
    if (publish_receipt) {
        persist_receipt(&receipt, (uint64_t)input_count, instruction.opcode);
    }

    result_values[0] = PointerGetDatum(form_occurrence_array(occurrences, input_count));
    result_values[1] = laplace_pg_numeric_from_uint64(logical_count);
    receipt_result_values(result_values, 2, &receipt, (uint64_t)input_count);
    result_tuple = laplace_pg_form_result_tuple(fcinfo, result_values, result_nulls, 14);
    return HeapTupleGetDatum(result_tuple);
}

Datum LAPLACE_PG_TRAJECTORY_CALCULATE_SYMBOL(PG_FUNCTION_ARGS) {
    return trajectory_composition_decode_batch(fcinfo, false);
}

Datum LAPLACE_PG_TRAJECTORY_EXECUTE_SYMBOL(PG_FUNCTION_ARGS) {
    return trajectory_composition_decode_batch(fcinfo, true);
}
