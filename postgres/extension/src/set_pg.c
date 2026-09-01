#include "postgres.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

#include "laplace/contract/postgresql_bindings.h"
#include "set_pg.h"

static Oid schema_oid(void) {
    return get_namespace_oid(LAPLACE_PG_SCHEMA, false);
}

Oid laplace_pg_composite_type_oid(const char* type_name) {
    const Oid type_oid = GetSysCacheOid2(
        TYPENAMENSP, Anum_pg_type_oid,
        CStringGetDatum(type_name), ObjectIdGetDatum(schema_oid()));
    if (!OidIsValid(type_oid)) {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("Laplace PostgreSQL record type %s is missing", type_name)));
    }
    return type_oid;
}

Oid laplace_pg_composite_array_oid(const char* type_name) {
    const Oid array_oid = get_array_type(laplace_pg_composite_type_oid(type_name));
    if (!OidIsValid(array_oid)) {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("Laplace PostgreSQL record array type %s[] is missing", type_name)));
    }
    return array_oid;
}

void laplace_pg_composite_binding_open(
    const char* type_name,
    const Oid* attribute_types,
    const int32* attribute_typmods,
    int attribute_count,
    laplace_pg_composite_binding* binding) {
    Oid type_oid;
    TupleDesc descriptor;
    Oid array_oid;
    int index;
    int16 type_length = 0;
    bool type_by_value = false;
    char type_alignment = 0;
    if (type_name == NULL || binding == NULL || attribute_types == NULL ||
        attribute_typmods == NULL || attribute_count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace PostgreSQL composite binding is invalid")));
    }
    memset(binding, 0, sizeof(*binding));
    type_oid = laplace_pg_composite_type_oid(type_name);
    descriptor = lookup_rowtype_tupdesc(type_oid, -1);
    if (descriptor->natts != attribute_count) {
        ReleaseTupleDesc(descriptor);
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace PostgreSQL record binding %s changed", type_name)));
    }
    for (index = 0; index < attribute_count; ++index) {
        Form_pg_attribute attribute = TupleDescAttr(descriptor, index);
        if (attribute->attisdropped ||
            attribute->atttypid != attribute_types[index] ||
            attribute->atttypmod != attribute_typmods[index]) {
            ReleaseTupleDesc(descriptor);
            ereport(ERROR,
                    (errcode(ERRCODE_DATATYPE_MISMATCH),
                     errmsg("Laplace PostgreSQL record binding %s attribute %d changed",
                            type_name, index + 1)));
        }
    }
    array_oid = get_array_type(type_oid);
    if (!OidIsValid(array_oid)) {
        ReleaseTupleDesc(descriptor);
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_OBJECT),
                 errmsg("Laplace PostgreSQL record array type %s[] is missing", type_name)));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    binding->type_oid = type_oid;
    binding->array_oid = array_oid;
    binding->descriptor = descriptor;
    binding->type_length = type_length;
    binding->type_by_value = type_by_value;
    binding->type_alignment = type_alignment;
    binding->attribute_count = attribute_count;
}

void laplace_pg_composite_binding_close(
    laplace_pg_composite_binding* binding) {
    if (binding != NULL) {
        if (binding->descriptor != NULL) {
            ReleaseTupleDesc(binding->descriptor);
        }
        memset(binding, 0, sizeof(*binding));
    }
}

Datum laplace_pg_composite_record(
    const laplace_pg_composite_binding* binding,
    Datum* values,
    bool* nulls) {
    HeapTuple tuple;
    if (binding == NULL || binding->descriptor == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace PostgreSQL composite binding is not open")));
    }
    tuple = heap_form_tuple(binding->descriptor, values, nulls);
    return HeapTupleGetDatum(tuple);
}

ArrayType* laplace_pg_composite_array(
    const laplace_pg_composite_binding* binding,
    Datum* values,
    uint64_t count) {
    if (binding == NULL || binding->descriptor == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace PostgreSQL composite binding is not open")));
    }
    if (count == 0) {
        return construct_empty_array(binding->type_oid);
    }
    if (count > (uint64_t)INT_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace PostgreSQL record batch has an unsupported cardinality")));
    }
    return construct_array(
        values, (int)count, binding->type_oid,
        binding->type_length, binding->type_by_value,
        binding->type_alignment);
}

void laplace_pg_keep_plan(
    SPIPlanPtr* target,
    const char* sql,
    int parameter_count,
    Oid* parameter_types) {
    if (*target == NULL) {
        SPIPlanPtr prepared = SPI_prepare(sql, parameter_count, parameter_types);
        if (prepared == NULL || SPI_keepplan(prepared) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("cannot prepare a Laplace set-oriented PostgreSQL plan")));
        }
        *target = prepared;
    }
}

uint64_t laplace_pg_scalar_count(const char* operation_name) {
    bool is_null = false;
    Datum value;
    if (SPI_processed != 1 || SPI_tuptable == NULL ||
        SPI_tuptable->tupdesc->natts != 1 ||
        SPI_gettypeid(SPI_tuptable->tupdesc, 1) != INT8OID) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace %s did not return one scalar", operation_name)));
    }
    value = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null || DatumGetInt64(value) < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace %s returned an invalid count", operation_name)));
    }
    return (uint64_t)DatumGetInt64(value);
}

bool laplace_pg_scalar_boolean(const char* operation_name) {
    bool is_null = false;
    Datum value;
    if (SPI_processed != 1 || SPI_tuptable == NULL ||
        SPI_tuptable->tupdesc->natts != 1 ||
        SPI_gettypeid(SPI_tuptable->tupdesc, 1) != BOOLOID) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace %s did not return one boolean", operation_name)));
    }
    value = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace %s returned a null disposition", operation_name)));
    }
    return DatumGetBool(value);
}

void laplace_pg_execute_set_write_verify(
    const char* write_sql,
    const char* verify_sql,
    int parameter_count,
    Oid* parameter_types,
    Datum* parameter_values,
    const char* operation_name) {
    int result;
    if (write_sql == NULL || verify_sql == NULL || operation_name == NULL ||
        parameter_count < 0 ||
        (parameter_count != 0 &&
         (parameter_types == NULL || parameter_values == NULL))) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace set write/verify contract is invalid")));
    }
    result = SPI_execute_with_args(
        write_sql, parameter_count, parameter_types, parameter_values,
        NULL, false, 0);
    if (result != SPI_OK_INSERT) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace %s write was not set-oriented", operation_name),
                 errdetail("spi_result=%d", result)));
    }
    result = SPI_execute_with_args(
        verify_sql, parameter_count, parameter_types, parameter_values,
        NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        !laplace_pg_scalar_boolean(operation_name)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace %s conflicts with durable state", operation_name)));
    }
}

void laplace_pg_execute_set_write_exact(
    const char* sql,
    int parameter_count,
    Oid* parameter_types,
    Datum* parameter_values,
    const char* operation_name) {
    int result;
    if (sql == NULL || operation_name == NULL || parameter_count < 0 ||
        (parameter_count != 0 &&
         (parameter_types == NULL || parameter_values == NULL))) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace exact set-write contract is invalid")));
    }
    result = SPI_execute_with_args(
        sql, parameter_count, parameter_types, parameter_values,
        NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        !laplace_pg_scalar_boolean(operation_name)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace %s conflicts with durable state", operation_name)));
    }
}
