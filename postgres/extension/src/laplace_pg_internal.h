#ifndef LAPLACE_POSTGRES_INTERNAL_H
#define LAPLACE_POSTGRES_INTERNAL_H

#include "postgres.h"

#include <stddef.h>
#include <stdint.h>

#include "access/htup_details.h"
#include "fmgr.h"

#include "laplace/framework.h"

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

#endif
