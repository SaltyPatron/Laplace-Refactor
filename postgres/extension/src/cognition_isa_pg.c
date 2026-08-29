#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"

#include "laplace/cognition_packet.h"
#include "laplace/contract/isa.h"
#include "laplace/isa.h"
#include "laplace_pg_internal.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_execute_packet);

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
    uint32_t* request_words;
    uint32_t* result_words;
    laplace_isa_value_view views[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt receipt;
    laplace_isa_error error;
    laplace_isa_status status;
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

    memset(views, 0, sizeof(views));
    views[0].data = request_words;
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
    program = make_cognition_program(&context, &instruction, views);
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_isa_execute(&program, &receipt, &error);
    if (status != LAPLACE_ISA_OK || views[1].count == 0u ||
        views[1].count > views[1].capacity) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace cognition ISA execution failed"),
                 errdetail("isa_status=%d instruction=%llu value=%u",
                           (int)status,
                           (unsigned long long)error.instruction_index,
                           error.value_index)));
    }
    laplace_pg_persist_execution_receipt(
        &receipt, (uint64_t)request_word_count, instruction.opcode);

    result_values[0] = PointerGetDatum(canonical_result_packet(
        result_words, (size_t)views[1].count));
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
