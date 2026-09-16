#include "postgres.h"

#include <stdint.h>
#include <string.h>

#include "executor/spi.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "laplace/cognition_materialization.h"
#include "laplace/framework.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_content_materialize_utf8);

/* This binding supplies source provenance to the common native traversal. It
 * neither constructs a cognition realization nor interprets descriptor text. */
Datum laplace_pg_content_materialize_utf8(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_framework_context provider_context;
    laplace_id128 root;
    laplace_digest256 source_receipt;
    laplace_digest256 source_recipe;
    laplace_cognition_materialization_request request = {0};
    laplace_cognition_materialization_provider_v1 provider = {0};
    laplace_content_materialization_receipt receipt = {0};
    laplace_pg_materialization_provider_state* owner = NULL;
    laplace_cognition_materialization_status status;
    HeapTupleHeader request_tuple;
    bytea* root_value;
    int32 maximum_depth;
    int32 version;
    uint8_t* output;
    bytea* output_value;
    Size output_allocation;
    size_t output_bytes = 0u;
    ErrorData* provider_error;
    Datum fields[15] = {0};
    bool nulls[15] = {false};
    volatile bool spi_connected = false;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    root_value = PG_GETARG_BYTEA_PP(1);
    if ((size_t)VARSIZE_ANY_EXHDR(root_value) != sizeof(root.bytes))
        ereport(ERROR, (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
            errmsg("Laplace content root must contain exactly 16 bytes")));
    memcpy(root.bytes, VARDATA_ANY(root_value), sizeof(root.bytes));
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &source_receipt, "content source_receipt_id");
    laplace_pg_read_digest(PG_GETARG_DATUM(3), &source_recipe, "content source_recipe_id");
    request_tuple = DatumGetHeapTupleHeader(PG_GETARG_DATUM(4));
    request.maximum_nodes = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(request_tuple, 1, "maximum_nodes"), "maximum_nodes");
    request.maximum_trajectory_carriers = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(request_tuple, 2, "maximum_trajectory_carriers"), "maximum_trajectory_carriers");
    request.maximum_output_bytes = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(request_tuple, 3, "maximum_output_bytes"), "maximum_output_bytes");
    maximum_depth = DatumGetInt32(laplace_pg_required_composite_attribute(request_tuple, 4, "maximum_depth"));
    version = DatumGetInt32(laplace_pg_required_composite_attribute(request_tuple, 5, "version"));
    if (maximum_depth < 0 || version < 0)
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
            errmsg("Laplace content materialization depth and version cannot be negative")));
    request.maximum_depth = (uint32_t)maximum_depth;
    request.version = (uint32_t)version;
    if (laplace_framework_context_validate(&context) != LAPLACE_FRAMEWORK_OK)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace content materialization requires a valid execution context")));
    if (request.maximum_output_bytes > (uint64_t)(MaxAllocSize - VARHDRSZ))
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace content materialization output exceeds PostgreSQL bytea addressability")));
    output_allocation = (Size)(request.maximum_output_bytes == 0u ? 1u : request.maximum_output_bytes) + VARHDRSZ;
    if ((uint64_t)output_allocation >= context.resource_grant.memory_bytes)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace content output leaves no memory grant for its provider")));
    /* Reserve the one caller-owned output allocation before creating the
     * provider. Reuse it as the returned bytea after atomic native validation. */
    provider_context = context;
    provider_context.resource_grant.memory_bytes -= (uint64_t)output_allocation;
    output_value = palloc(output_allocation);
    output = (uint8_t*)VARDATA(output_value);
    if (PG_NARGS() == 6) {
        laplace_digest256 original_physicality;
        laplace_pg_read_digest(PG_GETARG_DATUM(5), &original_physicality, "content original root_physicality_id");
        laplace_pg_materialization_provider_create_for_view_selected(&provider_context, &source_receipt,
            &original_physicality, &owner, &provider);
    } else laplace_pg_materialization_provider_create_for_view(&provider_context, &source_receipt, &owner, &provider);

    PG_TRY();
    {
        if (SPI_connect() != SPI_OK_CONNECT)
            ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                errmsg("Laplace content materialization could not connect to PostgreSQL")));
        spi_connected = true;
        status = laplace_content_materialize_encoded(&root, &source_receipt, &source_recipe,
            &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8, output,
            (size_t)request.maximum_output_bytes, &output_bytes, &receipt);
        if (SPI_finish() != SPI_OK_FINISH)
            ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                errmsg("Laplace content materialization PostgreSQL cleanup failed")));
        spi_connected = false;
    }
    PG_CATCH();
    {
        if (spi_connected) (void)SPI_finish();
        laplace_pg_materialization_provider_destroy(&owner);
        PG_RE_THROW();
    }
    PG_END_TRY();
    provider_error = laplace_pg_materialization_provider_take_error(owner);
    laplace_pg_materialization_provider_destroy(&owner);
    if (provider_error != NULL) ReThrowError(provider_error);

    if (status == LAPLACE_COGNITION_MATERIALIZATION_OK) {
        SET_VARSIZE(output_value, output_bytes + VARHDRSZ);
        fields[0] = PointerGetDatum(output_value);
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.materialization_id.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.source_receipt_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.source_recipe_id.bytes, 32u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.provider_fingerprint.bytes, 32u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.readset_fingerprint.bytes, 32u));
        fields[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.output_fingerprint.bytes, 32u));
        fields[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.root_content_id.bytes, 16u));
        fields[8] = laplace_pg_numeric_from_uint64(receipt.resolved_node_count);
        fields[9] = laplace_pg_numeric_from_uint64(receipt.trajectory_carrier_count);
        fields[10] = laplace_pg_numeric_from_uint64(receipt.codepoint_count);
        fields[11] = laplace_pg_numeric_from_uint64(receipt.output_bytes);
        fields[12] = Int32GetDatum((int32)receipt.maximum_depth_observed);
        fields[14] = Int32GetDatum((int32)receipt.version);
    } else {
        for (int index = 0; index <= 12; ++index) nulls[index] = true;
        fields[14] = Int32GetDatum(LAPLACE_COGNITION_MATERIALIZATION_RECEIPT_VERSION);
    }
    fields[13] = Int32GetDatum((int32)status);
    PG_RETURN_DATUM(HeapTupleGetDatum(laplace_pg_form_result_tuple(fcinfo, fields, nulls, 15)));
}
