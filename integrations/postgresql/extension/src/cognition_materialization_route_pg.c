#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "executor/spi.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "laplace/cognition_materialization.h"
#include "laplace/cognition_realization.h"
#include "laplace/framework.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_realization_materialize_utf8);

static void materialization_route_read_id(
    Datum datum,
    laplace_id128* id,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != sizeof(id->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly %zu bytes",
                        field, sizeof(id->bytes))));
    }
    memcpy(id->bytes, VARDATA_ANY(value), sizeof(id->bytes));
}

static uint32_t materialization_route_read_u32(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace %s cannot be negative", field)));
    }
    return (uint32_t)value;
}

static uint64_t materialization_route_read_u64(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static void materialization_route_read_realization(
    HeapTupleHeader tuple,
    laplace_cognition_realization_result* result) {
    memset(result, 0, sizeof(*result));
    materialization_route_read_id(
        laplace_pg_required_composite_attribute(tuple, 1, "realization content_id"),
        &result->content_id,
        "realization content_id");
    materialization_route_read_id(
        laplace_pg_required_composite_attribute(tuple, 2, "realization language_id"),
        &result->language_id,
        "realization language_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 3, "realization candidate_receipt_id"),
        &result->candidate_receipt_id,
        "realization candidate_receipt_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 4, "realization realization_recipe_id"),
        &result->realization_recipe_id,
        "realization realization_recipe_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 5, "realization missing_obligation_fingerprint"),
        &result->missing_obligation_fingerprint,
        "realization missing_obligation_fingerprint");
    result->preference_rank = materialization_route_read_u64(
        tuple, 6, "realization preference_rank");
    result->reused_subtree_count = materialization_route_read_u64(
        tuple, 7, "realization reused_subtree_count");
    result->generated_composition_count = materialization_route_read_u64(
        tuple, 8, "realization generated_composition_count");
    result->structural_tier = materialization_route_read_u32(
        tuple, 9, "realization structural_tier");
    result->match_class = materialization_route_read_u32(
        tuple, 10, "realization match_class");
    result->missing_obligation_count = materialization_route_read_u32(
        tuple, 11, "realization missing_obligation_count");
    result->disposition = materialization_route_read_u32(
        tuple, 12, "realization disposition");
    result->flags = materialization_route_read_u32(
        tuple, 13, "realization flags");
    result->version = materialization_route_read_u32(
        tuple, 14, "realization version");
}

static void materialization_route_read_request(
    HeapTupleHeader tuple,
    laplace_cognition_materialization_request* request) {
    memset(request, 0, sizeof(*request));
    request->maximum_nodes = materialization_route_read_u64(
        tuple, 1, "materialization maximum_nodes");
    request->maximum_trajectory_carriers = materialization_route_read_u64(
        tuple, 2, "materialization maximum_trajectory_carriers");
    request->maximum_output_bytes = materialization_route_read_u64(
        tuple, 3, "materialization maximum_output_bytes");
    request->maximum_depth = materialization_route_read_u32(
        tuple, 4, "materialization maximum_depth");
    request->version = materialization_route_read_u32(
        tuple, 5, "materialization version");
}

Datum laplace_pg_cognition_realization_materialize_utf8(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_cognition_realization_result realization;
    laplace_cognition_materialization_request request;
    laplace_cognition_materialization_provider_v1 provider;
    laplace_cognition_materialization_receipt receipt;
    laplace_pg_materialization_provider_state* owner = NULL;
    laplace_cognition_materialization_status native_status =
        LAPLACE_COGNITION_MATERIALIZATION_INVALID_ARGUMENT;
    ErrorData* provider_error;
    uint8_t* output;
    size_t output_bytes = 0u;
    Size allocation_bytes;
    Datum values[15] = {0};
    bool nulls[15] = {false};
    HeapTuple tuple;
    volatile bool spi_connected = false;

    memset(&provider, 0, sizeof(provider));
    memset(&receipt, 0, sizeof(receipt));
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    materialization_route_read_realization(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &realization);
    materialization_route_read_request(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(2)), &request);

    if (request.maximum_output_bytes > context.resource_grant.memory_bytes ||
        request.maximum_output_bytes > (uint64_t)MaxAllocSize) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace materialization output exceeds the execution-context memory grant"),
                 errdetail("requested_bytes=%llu granted_bytes=%llu",
                           (unsigned long long)request.maximum_output_bytes,
                           (unsigned long long)context.resource_grant.memory_bytes)));
    }

    allocation_bytes = (Size)(request.maximum_output_bytes == 0u
        ? 1u
        : request.maximum_output_bytes);
    output = (uint8_t*)palloc(allocation_bytes);
    laplace_pg_materialization_provider_create(&context, &owner, &provider);

    PG_TRY();
    {
        if (SPI_connect() != SPI_OK_CONNECT) {
            ereport(ERROR,
                    (errcode(ERRCODE_CONNECTION_FAILURE),
                     errmsg("Laplace materialization could not connect to PostgreSQL")));
        }
        spi_connected = true;
        native_status = laplace_cognition_realization_materialize_utf8(
            &realization,
            &request,
            &provider,
            output,
            (size_t)request.maximum_output_bytes,
            &output_bytes,
            &receipt);
        if (SPI_finish() != SPI_OK_FINISH) {
            ereport(ERROR,
                    (errcode(ERRCODE_CONNECTION_FAILURE),
                     errmsg("Laplace materialization PostgreSQL cleanup failed")));
        }
        spi_connected = false;
    }
    PG_CATCH();
    {
        if (spi_connected) {
            (void)SPI_finish();
        }
        laplace_pg_materialization_provider_destroy(&owner);
        PG_RE_THROW();
    }
    PG_END_TRY();

    provider_error = laplace_pg_materialization_provider_take_error(owner);
    laplace_pg_materialization_provider_destroy(&owner);
    if (provider_error != NULL) {
        ReThrowError(provider_error);
    }

    if (native_status == LAPLACE_COGNITION_MATERIALIZATION_OK) {
        values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(output, output_bytes));
        values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.materialization_id.bytes, sizeof(receipt.materialization_id.bytes)));
        values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.source_candidate_receipt_id.bytes,
            sizeof(receipt.source_candidate_receipt_id.bytes)));
        values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.source_recipe_id.bytes, sizeof(receipt.source_recipe_id.bytes)));
        values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.provider_fingerprint.bytes, sizeof(receipt.provider_fingerprint.bytes)));
        values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.readset_fingerprint.bytes, sizeof(receipt.readset_fingerprint.bytes)));
        values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.output_fingerprint.bytes, sizeof(receipt.output_fingerprint.bytes)));
        values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.root_content_id.bytes, sizeof(receipt.root_content_id.bytes)));
        values[8] = laplace_pg_numeric_from_uint64(receipt.resolved_node_count);
        values[9] = laplace_pg_numeric_from_uint64(receipt.trajectory_carrier_count);
        values[10] = laplace_pg_numeric_from_uint64(receipt.codepoint_count);
        values[11] = laplace_pg_numeric_from_uint64(receipt.output_bytes);
        values[12] = Int32GetDatum((int32)receipt.maximum_depth_observed);
        values[14] = Int32GetDatum((int32)receipt.version);
    } else {
        int index;
        for (index = 0; index <= 12; ++index) {
            nulls[index] = true;
        }
        values[14] = Int32GetDatum(
            (int32)LAPLACE_COGNITION_MATERIALIZATION_RECEIPT_VERSION);
    }
    values[13] = Int32GetDatum((int32)native_status);

    tuple = laplace_pg_form_result_tuple(fcinfo, values, nulls, 15);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}
