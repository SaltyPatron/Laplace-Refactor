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
#include "laplace/stock_recipe.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

PG_FUNCTION_INFO_V1(LAPLACE_PG_STOCK_RECIPE_COMPILE_CATALOG_SYMBOL);

static uint32_t read_u32(
    HeapTupleHeader tuple, int attribute, const char* field) {
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
    HeapTupleHeader tuple, int attribute, const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static void read_digest(
    HeapTupleHeader tuple, int attribute,
    laplace_digest256* digest, const char* field) {
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        digest, field);
}

static void read_recipe(
    HeapTupleHeader tuple, laplace_stock_recipe* recipe) {
    read_digest(tuple, 1, &recipe->recipe_id, "stock recipe recipe_id");
    read_digest(tuple, 2, &recipe->parent_recipe_id, "stock recipe parent_recipe_id");
    read_digest(tuple, 3, &recipe->source_profile_id, "stock recipe source_profile_id");
    read_digest(tuple, 4, &recipe->source_artifact_id, "stock recipe source_artifact_id");
    read_digest(tuple, 5, &recipe->grammar_provider_id, "stock recipe grammar_provider_id");
    read_digest(tuple, 6, &recipe->codec_provider_id, "stock recipe codec_provider_id");
    read_digest(tuple, 7, &recipe->lowering_program_id, "stock recipe lowering_program_id");
    read_digest(tuple, 8, &recipe->recomposition_program_id, "stock recipe recomposition_program_id");
    read_digest(tuple, 9, &recipe->semantic_segmentation_law_id,
                "stock recipe semantic_segmentation_law_id");
    read_digest(tuple, 10, &recipe->conformance_id, "stock recipe conformance_id");
    read_digest(tuple, 11, &recipe->loss_policy_id, "stock recipe loss_policy_id");
    read_digest(tuple, 12, &recipe->correction_epoch_id, "stock recipe correction_epoch_id");
    recipe->sibling_ordinal = read_u64(tuple, 13, "stock recipe sibling_ordinal");
    recipe->scope_kind = read_u32(tuple, 14, "stock recipe scope_kind");
    recipe->modality_kind = read_u32(tuple, 15, "stock recipe modality_kind");
    recipe->version = read_u32(tuple, 16, "stock recipe version");
    recipe->flags = read_u32(tuple, 17, "stock recipe flags");
}

static void read_plane(
    HeapTupleHeader tuple, laplace_stock_perfcache_plane* plane) {
    read_digest(tuple, 1, &plane->plane_id, "stock perfcache plane_id");
    read_digest(tuple, 2, &plane->recipe_id, "stock perfcache recipe_id");
    read_digest(tuple, 3, &plane->key_kind_id, "stock perfcache key_kind_id");
    read_digest(tuple, 4, &plane->value_kind_id, "stock perfcache value_kind_id");
    read_digest(tuple, 5, &plane->dependency_epoch_id, "stock perfcache dependency_epoch_id");
    read_digest(tuple, 6, &plane->generation_program_id, "stock perfcache generation_program_id");
    read_digest(tuple, 7, &plane->semantic_verifier_id, "stock perfcache semantic_verifier_id");
    read_digest(tuple, 8, &plane->invalidation_law_id, "stock perfcache invalidation_law_id");
    read_digest(tuple, 9, &plane->rebuild_law_id, "stock perfcache rebuild_law_id");
    plane->version = read_u32(tuple, 10, "stock perfcache version");
    plane->flags = read_u32(tuple, 11, "stock perfcache flags");
}

static void read_item(
    HeapTupleHeader tuple, laplace_stock_catalog_item* item) {
    bool recipe_null = false;
    bool plane_null = false;
    Datum recipe = GetAttributeByNum(tuple, 1, &recipe_null);
    Datum plane = GetAttributeByNum(tuple, 2, &plane_null);
    item->item_kind = read_u32(tuple, 3, "stock catalog item_kind");
    item->flags = read_u32(tuple, 4, "stock catalog flags");
    if (item->item_kind == LAPLACE_STOCK_ITEM_RECIPE &&
        !recipe_null && plane_null) {
        read_recipe(DatumGetHeapTupleHeader(recipe), &item->recipe);
    } else if (item->item_kind == LAPLACE_STOCK_ITEM_PERFCACHE_PLANE &&
               recipe_null && !plane_null) {
        read_plane(DatumGetHeapTupleHeader(plane), &item->perfcache_plane);
    } else {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace stock catalog item must carry exactly its declared payload")));
    }
}

static laplace_stock_catalog_item* read_items(
    ArrayType* array, size_t* item_count) {
    const Oid type_oid = laplace_pg_composite_type_oid("stock_catalog_item");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_stock_catalog_item* items;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace stock catalog input must be a one-dimensional exact stock_catalog_item array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace stock catalog input cannot be empty")));
    }
    items = (laplace_stock_catalog_item*)palloc0(sizeof(*items) * (size_t)count);
    for (index = 0; index < count; ++index) {
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace stock catalog input cannot contain null items")));
        }
        read_item(DatumGetHeapTupleHeader(values[index]), &items[index]);
    }
    *item_count = (size_t)count;
    return items;
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

static void persist_catalog(
    Oid input_array_type,
    Datum input_array,
    const laplace_stock_catalog_receipt* catalog,
    const laplace_isa_receipt* isa_receipt) {
    static const char recipes_sql[] =
        "WITH items AS (SELECT i FROM unnest($1::" LAPLACE_PG_SCHEMA ".stock_catalog_item[]) i),"
        "input AS (SELECT ((i).recipe).* FROM items WHERE (i).item_kind=1),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".stock_recipe SELECT * FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT NOT EXISTS (SELECT FROM input i WHERE NOT EXISTS (SELECT FROM written w WHERE w.recipe_id=i.recipe_id AND ROW(w.*) IS NOT DISTINCT FROM ROW(i.*)) AND NOT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".stock_recipe d WHERE d.recipe_id=i.recipe_id AND ROW(d.*) IS NOT DISTINCT FROM ROW(i.*)))";
    static const char planes_sql[] =
        "WITH items AS (SELECT i FROM unnest($1::" LAPLACE_PG_SCHEMA ".stock_catalog_item[]) i),"
        "input AS (SELECT ((i).perfcache_plane).* FROM items WHERE (i).item_kind=2),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".stock_perfcache_plane SELECT * FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT NOT EXISTS (SELECT FROM input i WHERE NOT EXISTS (SELECT FROM written w WHERE w.plane_id=i.plane_id AND ROW(w.*) IS NOT DISTINCT FROM ROW(i.*)) AND NOT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".stock_perfcache_plane d WHERE d.plane_id=i.plane_id AND ROW(d.*) IS NOT DISTINCT FROM ROW(i.*)))";
    static const char receipt_sql[] =
        "WITH written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".stock_catalog_receipt(catalog_id,recipe_set_fingerprint,perfcache_set_fingerprint,isa_receipt_id,recipe_count,source_count,perfcache_plane_count,maximum_scope_kind,version) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9) ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT EXISTS (SELECT FROM written WHERE catalog_id=$1 AND recipe_set_fingerprint=$2 AND perfcache_set_fingerprint=$3 AND isa_receipt_id=$4 AND recipe_count=$5 AND source_count=$6 AND perfcache_plane_count=$7 AND maximum_scope_kind=$8 AND version=$9) OR EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".stock_catalog_receipt WHERE catalog_id=$1 AND recipe_set_fingerprint=$2 AND perfcache_set_fingerprint=$3 AND isa_receipt_id=$4 AND recipe_count=$5 AND source_count=$6 AND perfcache_plane_count=$7 AND maximum_scope_kind=$8 AND version=$9)";
    static const char members_sql[] =
        "WITH items AS (SELECT i FROM unnest($2::" LAPLACE_PG_SCHEMA ".stock_catalog_item[]) i),"
        "input AS (SELECT $1::bytea AS catalog_id,(i).item_kind,CASE WHEN (i).item_kind=1 THEN ((i).recipe).recipe_id ELSE ((i).perfcache_plane).plane_id END AS item_id FROM items),"
        "written AS (INSERT INTO " LAPLACE_PG_SCHEMA ".stock_catalog_receipt_member SELECT * FROM input ON CONFLICT DO NOTHING RETURNING *) "
        "SELECT (SELECT count(*) FROM written)+(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".stock_catalog_receipt_member m JOIN input i ON i.catalog_id=m.catalog_id AND i.item_kind=m.item_kind AND i.item_id=m.item_id WHERE m.catalog_id=$1)=(SELECT count(*) FROM input) AND (SELECT count(*) FROM written)+(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".stock_catalog_receipt_member m WHERE m.catalog_id=$1)=(SELECT count(*) FROM input)";
    Oid item_types[1] = {input_array_type};
    Datum item_values[1] = {input_array};
    Oid receipt_types[9] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT4OID, INT4OID};
    Datum receipt_values[9];
    Oid member_types[2] = {BYTEAOID, input_array_type};
    Datum member_values[2];
    int result;
    receipt_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        catalog->catalog_id.bytes, 32u));
    receipt_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        catalog->recipe_set_fingerprint.bytes, 32u));
    receipt_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        catalog->perfcache_set_fingerprint.bytes, 32u));
    receipt_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt->receipt_id.bytes, 32u));
    receipt_values[4] = Int64GetDatum(laplace_pg_checked_int64(
        catalog->recipe_count, "stock catalog recipe count"));
    receipt_values[5] = Int64GetDatum(laplace_pg_checked_int64(
        catalog->source_count, "stock catalog source count"));
    receipt_values[6] = Int64GetDatum(laplace_pg_checked_int64(
        catalog->perfcache_plane_count, "stock catalog perfcache count"));
    receipt_values[7] = Int32GetDatum((int32)catalog->maximum_scope_kind);
    receipt_values[8] = Int32GetDatum((int32)catalog->version);
    member_values[0] = receipt_values[0];
    member_values[1] = input_array;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace stock catalog persistence could not connect to SPI")));
    }
    result = SPI_execute_with_args(
        recipes_sql, 1, item_types, item_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace stock recipe replay conflicts with durable state")));
    }
    result = SPI_execute_with_args(
        planes_sql, 1, item_types, item_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace stock perfcache replay conflicts with durable state")));
    }
    result = SPI_execute_with_args(
        receipt_sql, 9, receipt_types, receipt_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace stock catalog receipt collides with durable state")));
    }
    result = SPI_execute_with_args(
        members_sql, 2, member_types, member_values, NULL, false, 0);
    if (result != SPI_OK_SELECT || !query_boolean()) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace stock catalog membership conflicts with durable state")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace stock catalog persistence could not close SPI")));
    }
}

Datum LAPLACE_PG_STOCK_RECIPE_COMPILE_CATALOG_SYMBOL(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    const Oid input_array_type = get_array_type(ARR_ELEMTYPE(input_array));
    laplace_stock_catalog_item* items;
    size_t item_count = 0u;
    laplace_stock_catalog_receipt semantic_receipt;
    laplace_stock_catalog_receipt output_receipt;
    laplace_stock_recipe_error semantic_error;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt isa_receipt;
    laplace_isa_error isa_error;
    Datum result_values[9];
    bool result_nulls[9] = {false};
    HeapTuple result_tuple;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    items = read_items(input_array, &item_count);
    memset(&output_receipt, 0, sizeof(output_receipt));
    memset(values, 0, sizeof(values));
    values[0].data = items;
    values[0].count = (uint64_t)item_count;
    values[0].capacity = (uint64_t)item_count;
    values[0].stride_bytes = sizeof(*items);
    values[0].type = LAPLACE_ISA_VALUE_STOCK_CATALOG_ITEM_VECTOR;
    values[1].data = &output_receipt;
    values[1].capacity = 1u;
    values[1].stride_bytes = sizeof(output_receipt);
    values[1].type = LAPLACE_ISA_VALUE_STOCK_CATALOG_RECEIPT_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_STOCK_RECIPE_COMPILE_CATALOG_BATCH;
    instruction.version =
        LAPLACE_ISA_INSTRUCTION_VERSION_STOCK_RECIPE_COMPILE_CATALOG_BATCH;
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
                 errmsg("Laplace stock catalog ISA execution failed"),
                 errdetail("status=%d instruction=%llu",
                           (int)isa_error.status,
                           (unsigned long long)isa_error.instruction_index)));
    }
    memset(&semantic_receipt, 0, sizeof(semantic_receipt));
    memset(&semantic_error, 0, sizeof(semantic_error));
    if (laplace_stock_recipe_compile_catalog_items(
            items, item_count, &semantic_receipt, &semantic_error) !=
            LAPLACE_STOCK_RECIPE_OK ||
        memcmp(&semantic_receipt, &output_receipt, sizeof(semantic_receipt)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace stock catalog semantic receipt reconstruction failed")));
    }
    laplace_pg_persist_execution_receipt(
        &isa_receipt, item_count, instruction.opcode);
    persist_catalog(
        input_array_type, PointerGetDatum(input_array),
        &semantic_receipt, &isa_receipt);
    result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.catalog_id.bytes, 32u));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.recipe_set_fingerprint.bytes, 32u));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        semantic_receipt.perfcache_set_fingerprint.bytes, 32u));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt.receipt_id.bytes, 32u));
    result_values[4] = laplace_pg_numeric_from_uint64(semantic_receipt.recipe_count);
    result_values[5] = laplace_pg_numeric_from_uint64(semantic_receipt.source_count);
    result_values[6] = laplace_pg_numeric_from_uint64(
        semantic_receipt.perfcache_plane_count);
    result_values[7] = Int32GetDatum((int32)semantic_receipt.maximum_scope_kind);
    result_values[8] = Int32GetDatum((int32)semantic_receipt.version);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 9);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
