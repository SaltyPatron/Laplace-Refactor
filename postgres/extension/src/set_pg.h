#ifndef LAPLACE_POSTGRES_SET_PG_H
#define LAPLACE_POSTGRES_SET_PG_H

#include "postgres.h"

#include <stdint.h>

#include "executor/spi.h"
#include "utils/array.h"

#define LAPLACE_PG_TYPMOD_NONE (-1)
#define LAPLACE_PG_NUMERIC_TYPMOD(precision, scale) \
    (VARHDRSZ + (((precision) << 16) | (scale)))

typedef struct laplace_pg_composite_binding {
    Oid type_oid;
    Oid array_oid;
    TupleDesc descriptor;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    int attribute_count;
} laplace_pg_composite_binding;

Oid laplace_pg_composite_type_oid(const char* type_name);
Oid laplace_pg_composite_array_oid(const char* type_name);

void laplace_pg_composite_binding_open(
    const char* type_name,
    const Oid* attribute_types,
    const int32* attribute_typmods,
    int attribute_count,
    laplace_pg_composite_binding* binding);

void laplace_pg_composite_binding_close(
    laplace_pg_composite_binding* binding);

Datum laplace_pg_composite_record(
    const laplace_pg_composite_binding* binding,
    Datum* values,
    bool* nulls);

ArrayType* laplace_pg_composite_array(
    const laplace_pg_composite_binding* binding,
    Datum* values,
    uint64_t count);

void laplace_pg_keep_plan(
    SPIPlanPtr* target,
    const char* sql,
    int parameter_count,
    Oid* parameter_types);

uint64_t laplace_pg_scalar_count(const char* operation_name);

bool laplace_pg_scalar_boolean(const char* operation_name);

void laplace_pg_execute_set_write_verify(
    const char* write_sql,
    const char* verify_sql,
    int parameter_count,
    Oid* parameter_types,
    Datum* parameter_values,
    const char* operation_name);

#endif
