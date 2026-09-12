#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "laplace/cognition_packet.h"
#include "laplace/contract/isa.h"
#include "laplace/isa.h"
#include "cognition_isa_pg.h"
#include "laplace_pg_internal.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_execute_packet);
PG_FUNCTION_INFO_V1(laplace_pg_isa_execute_batch);

static int u64_fits_size(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return 1;
#endif
}

static uint32_t read_u32_le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static void write_u32_le(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static laplace_isa_program make_cognition_program(
    const laplace_framework_context* context,
    laplace_isa_instruction* instruction,
    laplace_isa_value_view* values) {
    laplace_isa_program program;
    memset(&program, 0, sizeof(program));
    program.instructions = instruction;
    program.values = values;
    program.context = context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    return program;
}

void laplace_pg_cognition_execute_words(
    const laplace_framework_context* context,
    const uint32_t* request_words,
    size_t request_word_count,
    uint32_t* result_words,
    size_t result_word_capacity,
    size_t* result_word_count,
    laplace_isa_receipt* receipt) {
    laplace_isa_value_view views[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_error error;
    laplace_isa_status status;
    if (context == NULL || request_words == NULL || request_word_count == 0u ||
        result_words == NULL || result_word_capacity == 0u ||
        result_word_count == NULL || receipt == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace cognition ISA execution arguments are invalid")));
    }
    *result_word_count = 0u;
    memset(views, 0, sizeof(views));
    views[0].data = (void*)request_words;
    views[0].count = (uint64_t)request_word_count;
    views[0].capacity = (uint64_t)request_word_count;
    views[0].stride_bytes = (uint32_t)sizeof(*request_words);
    views[0].type = LAPLACE_ISA_VALUE_U32_VECTOR;
    views[1].data = result_words;
    views[1].count = 0u;
    views[1].capacity = (uint64_t)result_word_capacity;
    views[1].stride_bytes = (uint32_t)sizeof(*result_words);
    views[1].type = LAPLACE_ISA_VALUE_U32_VECTOR;

    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_COGNITION_SOLVE_PACKET;
    instruction.input_value = 0u;
    instruction.output_value = 1u;
    instruction.version = LAPLACE_ISA_INSTRUCTION_VERSION_COGNITION_SOLVE_PACKET;
    program = make_cognition_program(context, &instruction, views);
    memset(receipt, 0, sizeof(*receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_isa_execute(&program, receipt, &error);
    if (status != LAPLACE_ISA_OK || views[1].count == 0u ||
        views[1].count > views[1].capacity || !u64_fits_size(views[1].count)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace cognition ISA execution failed"),
                 errdetail("isa_status=%d instruction=%llu value=%u",
                           (int)status,
                           (unsigned long long)error.instruction_index,
                           error.value_index)));
    }
    *result_word_count = (size_t)views[1].count;
    laplace_pg_persist_execution_receipt(
        receipt, (uint64_t)request_word_count, instruction.opcode);
}

static bytea* canonical_result_packet(
    const uint32_t* words,
    size_t word_count) {
    size_t bytes;
    bytea* value;
    uint8_t* destination;
    size_t index;
    if (word_count > (SIZE_MAX - VARHDRSZ) / sizeof(uint32_t)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace cognition result packet exceeds PostgreSQL allocation range")));
    }
    bytes = word_count * sizeof(uint32_t);
    value = (bytea*)palloc(VARHDRSZ + bytes);
    SET_VARSIZE(value, VARHDRSZ + bytes);
    destination = (uint8_t*)VARDATA(value);
    for (index = 0u; index < word_count; ++index) {
        write_u32_le(destination + index * 4u, words[index]);
    }
    return value;
}

Datum laplace_pg_cognition_execute_packet(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    bytea* request = PG_GETARG_BYTEA_PP(1);
    const uint8_t* request_bytes = (const uint8_t*)VARDATA_ANY(request);
    const size_t request_byte_count = (size_t)VARSIZE_ANY_EXHDR(request);
    size_t request_word_count;
    size_t result_word_capacity = 0u;
    size_t result_word_count = 0u;
    uint32_t* request_words;
    uint32_t* result_words;
    laplace_isa_receipt receipt;
    laplace_cognition_packet_status packet_status;
    Datum result_values[13];
    bool result_nulls[13] = {false};
    HeapTuple result_tuple;
    size_t index;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if (request_byte_count == 0u || (request_byte_count & 3u) != 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace cognition packet must contain a nonempty whole number of 32-bit words")));
    }
    request_word_count = request_byte_count / 4u;
    request_words = (uint32_t*)palloc(sizeof(*request_words) * request_word_count);
    for (index = 0u; index < request_word_count; ++index) {
        request_words[index] = read_u32_le(request_bytes + index * 4u);
    }
    packet_status = laplace_cognition_packet_required_result_words(
        request_words, request_word_count, &result_word_capacity);
    if (packet_status != LAPLACE_COGNITION_PACKET_OK || result_word_capacity == 0u ||
        result_word_capacity > SIZE_MAX / sizeof(*result_words)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace cognition request packet failed native preflight"),
                 errdetail("packet_status=%d", (int)packet_status)));
    }
    result_words = (uint32_t*)palloc0(sizeof(*result_words) * result_word_capacity);
    laplace_pg_cognition_execute_words(
        &context, request_words, request_word_count,
        result_words, result_word_capacity, &result_word_count, &receipt);

    result_values[0] = PointerGetDatum(canonical_result_packet(
        result_words, result_word_count));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.receipt_id.bytes, sizeof(receipt.receipt_id.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.context_fingerprint.bytes,
        sizeof(receipt.context_fingerprint.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.program_fingerprint.bytes,
        sizeof(receipt.program_fingerprint.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.input_fingerprint.bytes,
        sizeof(receipt.input_fingerprint.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.output_fingerprint.bytes,
        sizeof(receipt.output_fingerprint.bytes)));
    result_values[6] = Int64GetDatum(laplace_pg_checked_int64(
        receipt.instruction_count, "cognition instruction count"));
    result_values[7] = Int64GetDatum(laplace_pg_checked_int64(
        receipt.executed_instruction_count, "cognition executed instruction count"));
    result_values[8] = Int16GetDatum((int16)receipt.major);
    result_values[9] = Int16GetDatum((int16)receipt.minor);
    result_values[10] = Int32GetDatum((int32)receipt.receipt_detail);
    result_values[11] = Int32GetDatum((int32)receipt.status);
    result_values[12] = Int64GetDatum(laplace_pg_checked_int64(
        (uint64_t)request_word_count, "cognition packet word count"));
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 13);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}

static uint32_t transport_u32(int64 value, const char* field, int nonzero) {
    if (value < 0 || (uint64_t)value > UINT32_MAX ||
        (nonzero != 0 && value == 0)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace ISA transport field %s is outside uint32 range", field)));
    }
    return (uint32_t)value;
}

static uint16_t transport_u16(int64 value, const char* field) {
    if (value < 0 || (uint64_t)value > UINT16_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace ISA transport field %s is outside uint16 range", field)));
    }
    return (uint16_t)value;
}

static uint64_t transport_u64(int64 value, const char* field) {
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace ISA transport field %s cannot be negative", field)));
    }
    return (uint64_t)value;
}

static Size transport_byte_count(
    uint64_t count,
    uint32_t stride,
    const char* field) {
    uint64_t bytes;
    if (stride == 0u || count > UINT64_MAX / stride) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace ISA transport %s byte count overflows", field)));
    }
    bytes = count * stride;
    if (bytes > (uint64_t)MaxAllocSize) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace ISA transport %s exceeds PostgreSQL allocation range", field)));
    }
    return (Size)bytes;
}

Datum laplace_pg_isa_execute_batch(PG_FUNCTION_ARGS) {
    bytea* context_bytes = PG_GETARG_BYTEA_PP(0);
    uint32_t opcode = transport_u32(PG_GETARG_INT64(1), "opcode", 0);
    uint32_t input_type = transport_u32(PG_GETARG_INT64(2), "input_type", 0);
    uint32_t output_type = transport_u32(PG_GETARG_INT64(3), "output_type", 0);
    uint16_t instruction_version =
        transport_u16(PG_GETARG_INT64(4), "instruction_version");
    uint32_t input_stride = transport_u32(PG_GETARG_INT64(5), "input_stride", 1);
    uint32_t output_stride = transport_u32(PG_GETARG_INT64(6), "output_stride", 1);
    uint64_t input_count = transport_u64(PG_GETARG_INT64(7), "input_count");
    uint64_t output_capacity = transport_u64(
        PG_GETARG_INT64(8), "output_capacity");
    bytea* input_payload = PG_GETARG_BYTEA_PP(9);
    uint16_t isa_major = transport_u16(PG_GETARG_INT64(10), "isa_major");
    uint16_t isa_minor = transport_u16(PG_GETARG_INT64(11), "isa_minor");
    bool publish_receipt = PG_GETARG_BOOL(12);
    Size input_byte_count = transport_byte_count(
        input_count, input_stride, "input");
    Size output_capacity_bytes = transport_byte_count(
        output_capacity, output_stride, "output");
    laplace_framework_context context;
    void* input_storage;
    void* output_storage;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt receipt;
    laplace_isa_error error;
    laplace_isa_status status;
    Size output_byte_count;
    Datum result_values[16];
    bool result_nulls[16] = {false};
    HeapTuple result_tuple;

    if ((size_t)VARSIZE_ANY_EXHDR(context_bytes) != sizeof(context)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace ISA transport execution context must contain exactly %zu native bytes",
                        sizeof(context))));
    }
    if ((Size)VARSIZE_ANY_EXHDR(input_payload) != input_byte_count) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace ISA transport input payload byte count differs from count and stride")));
    }

    memcpy(&context, VARDATA_ANY(context_bytes), sizeof(context));
    input_storage = palloc(input_byte_count == 0u ? 1u : input_byte_count);
    output_storage = palloc0(
        output_capacity_bytes == 0u ? 1u : output_capacity_bytes);
    if (input_byte_count != 0u) {
        memcpy(input_storage, VARDATA_ANY(input_payload), input_byte_count);
    }

    memset(values, 0, sizeof(values));
    values[0].data = input_storage;
    values[0].count = input_count;
    values[0].capacity = input_count;
    values[0].stride_bytes = input_stride;
    values[0].type = input_type;
    values[0].flags = LAPLACE_ISA_KNOWN_VALUE_FLAGS;
    values[1].data = output_storage;
    values[1].count = 0u;
    values[1].capacity = output_capacity;
    values[1].stride_bytes = output_stride;
    values[1].type = output_type;
    values[1].flags = LAPLACE_ISA_KNOWN_VALUE_FLAGS;

    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = opcode;
    instruction.input_value = 0u;
    instruction.output_value = 1u;
    instruction.version = instruction_version;
    instruction.flags = LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS;

    memset(&program, 0, sizeof(program));
    program.instructions = &instruction;
    program.values = values;
    program.context = &context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = isa_major;
    program.minor = isa_minor;
    program.flags = LAPLACE_ISA_KNOWN_PROGRAM_FLAGS;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;

    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_isa_execute(&program, &receipt, &error);
    if (values[1].count > values[1].capacity) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace ISA transport native output count exceeds capacity")));
    }
    output_byte_count = transport_byte_count(
        values[1].count, output_stride, "published output");

    if (status == LAPLACE_ISA_OK && publish_receipt) {
        if (input_count == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace ISA transport cannot persist a zero-item successful receipt")));
        }
        laplace_pg_persist_execution_receipt(&receipt, input_count, opcode);
    }

    result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        (const uint8_t*)output_storage, output_byte_count));
    result_values[1] = Int64GetDatum(laplace_pg_checked_int64(
        values[1].count, "ISA transport output count"));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.receipt_id.bytes,
        sizeof(receipt.receipt_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.context_fingerprint.bytes,
        sizeof(receipt.context_fingerprint.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.program_fingerprint.bytes,
        sizeof(receipt.program_fingerprint.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.input_fingerprint.bytes,
        sizeof(receipt.input_fingerprint.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.output_fingerprint.bytes,
        sizeof(receipt.output_fingerprint.bytes)));
    result_values[7] = Int64GetDatum(laplace_pg_checked_int64(
        receipt.instruction_count, "ISA transport instruction count"));
    result_values[8] = Int64GetDatum(laplace_pg_checked_int64(
        receipt.executed_instruction_count,
        "ISA transport executed instruction count"));
    result_values[9] = Int32GetDatum((int32)receipt.major);
    result_values[10] = Int32GetDatum((int32)receipt.minor);
    result_values[11] = Int64GetDatum((int64)receipt.receipt_detail);
    result_values[12] = Int32GetDatum((int32)status);
    result_values[13] = Int32GetDatum((int32)error.status);
    result_values[14] = laplace_pg_numeric_from_uint64(error.instruction_index);
    result_values[15] = Int64GetDatum((int64)error.value_index);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 16);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}

#include "execution_context_pg.inc"
#include "target_attention_pg.inc"
#include "target_attention_export_pg.inc"
#include "target_export_pg.inc"
