#ifndef LAPLACE_POSTGRES_INTERNAL_H
#define LAPLACE_POSTGRES_INTERNAL_H

#include "postgres.h"

#include <stddef.h>
#include <stdint.h>

#include "access/htup_details.h"
#include "fmgr.h"

#include "laplace/framework.h"
#include "laplace/isa.h"

Datum laplace_pg_required_composite_attribute(
    HeapTupleHeader tuple,
    int attribute_number,
    const char* attribute_name);

void laplace_pg_read_digest(
    Datum datum,
    laplace_digest256* digest,
    const char* field);

uint64_t laplace_pg_uint64_from_numeric(Datum datum, const char* field);

void laplace_pg_read_execution_context(
    Datum datum,
    laplace_framework_context* context);

bytea* laplace_pg_bytes_to_bytea(const uint8_t* bytes, size_t length);

int64 laplace_pg_checked_int64(uint64_t value, const char* field);

Datum laplace_pg_numeric_from_uint64(uint64_t value);

HeapTuple laplace_pg_form_result_tuple(
    FunctionCallInfo fcinfo,
    Datum* values,
    bool* nulls,
    int expected_attributes);

void laplace_pg_persist_execution_receipt(
    const laplace_isa_receipt* receipt,
    uint64_t item_count,
    uint32_t opcode);

#endif
