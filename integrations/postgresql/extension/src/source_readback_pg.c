#include "postgres.h"

#include <stdint.h>
#include <string.h>
#include "executor/spi.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/tuplestore.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "blake3.h"
#include "laplace/cognition_materialization.h"
#include "laplace/framework.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"
#include "source_structural_witness_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_source_readback_utf8);
PG_FUNCTION_INFO_V1(laplace_pg_source_readback_utf8_batch);

static uint64_t read_limit(HeapTupleHeader tuple, int field, const char* name) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple,field,name),name);
}

static void binding_identity(const laplace_framework_context* context,
    const laplace_digest256* profile, const laplace_digest256* structural,
    uint64_t artifact, const laplace_pg_source_readback_binding* binding,
    laplace_digest256* result) {
    static const char domain[]="laplace.source-canonical-content-readback/v1";
    blake3_hasher hasher;
    uint8_t coordinates[16];
    unsigned index;
    for(index=0;index<8;++index) {
        coordinates[index]=(uint8_t)(artifact>>(index*8));
        coordinates[index+8]=(uint8_t)(binding->byte_count>>(index*8));
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher,domain,sizeof(domain)-1u);
    blake3_hasher_update(&hasher,profile->bytes,32u);
    blake3_hasher_update(&hasher,structural->bytes,32u);
    blake3_hasher_update(&hasher,binding->root_content_id.bytes,16u);
    blake3_hasher_update(&hasher,coordinates,sizeof(coordinates));
    blake3_hasher_update(&hasher,context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes,32u);
    blake3_hasher_update(&hasher,context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes,32u);
    blake3_hasher_finalize(&hasher,result->bytes,32u);
}

static HeapTuple readback_tuple(FunctionCallInfo fcinfo,
    const laplace_digest256* profile, const laplace_digest256* structural,
    uint64_t artifact, const laplace_pg_source_readback_binding* binding,
    const laplace_content_materialization_receipt* receipt,
    const uint8_t* output, size_t output_bytes, uint64_t database_operations) {
    Datum values[19] = {0};
    bool nulls[19] = {false};
    HeapTuple result;
    int index;
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(output, output_bytes));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->root_content_id.bytes, 16u));
    values[2] = laplace_pg_numeric_from_uint64(artifact);
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile->bytes, 32u));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(structural->bytes, 32u));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->source_receipt_id.bytes, 32u));
    values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->source_recipe_id.bytes, 32u));
    values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->materialization_id.bytes, 32u));
    values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->provider_fingerprint.bytes, 32u));
    values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->readset_fingerprint.bytes, 32u));
    values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->output_fingerprint.bytes, 32u));
    values[11] = laplace_pg_numeric_from_uint64(receipt->output_bytes);
    values[12] = laplace_pg_numeric_from_uint64(receipt->resolved_node_count);
    values[13] = laplace_pg_numeric_from_uint64(receipt->trajectory_carrier_count);
    values[14] = laplace_pg_numeric_from_uint64(receipt->codepoint_count);
    values[15] = Int32GetDatum((int32)receipt->maximum_depth_observed);
    values[16] = laplace_pg_numeric_from_uint64(binding->witness_count);
    values[17] = laplace_pg_numeric_from_uint64(database_operations);
    values[18] = Int32GetDatum((int32)receipt->version);
    result = laplace_pg_form_result_tuple(fcinfo, values, nulls, 19);
    /* Heap formation copied every by-reference field. Retain only the tuple. */
    for (index = 0; index < 19; ++index)
        if (index != 15 && index != 18) pfree(DatumGetPointer(values[index]));
    return result;
}

static Datum source_readback(FunctionCallInfo fcinfo, bool batch) {
    laplace_framework_context context;
    MemoryContext caller_context = CurrentMemoryContext;
    laplace_digest256 profile, structural;
    laplace_pg_source_readback_binding* bindings;
    laplace_cognition_materialization_request request;
    laplace_cognition_materialization_provider_v1 provider;
    laplace_pg_materialization_provider_state* owner = NULL;
    HeapTupleHeader selected = DatumGetHeapTupleHeader(PG_GETARG_DATUM(4));
    uint64_t maximum_witnesses = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(5), "maximum witnesses");
    uint64_t* artifacts;
    size_t artifact_count = 1u;
    size_t index;
    uint64_t maximum_total_output, total_output = 0u, prior_operations = 0u;
    int32 depth, version;
    volatile bool connected = false;
    HeapTuple single_result = NULL;
    ReturnSetInfo* set = NULL;

    memset(&request, 0, sizeof(request));
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    laplace_pg_read_digest(PG_GETARG_DATUM(1), &profile, "source profile id");
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &structural, "structural receipt id");
    request.maximum_nodes = read_limit(selected, 1, "maximum nodes");
    request.maximum_trajectory_carriers = read_limit(selected, 2, "maximum trajectory carriers");
    request.maximum_output_bytes = read_limit(selected, 3, "maximum output bytes");
    depth = DatumGetInt32(laplace_pg_required_composite_attribute(selected, 4, "maximum depth"));
    version = DatumGetInt32(laplace_pg_required_composite_attribute(selected, 5, "version"));
    maximum_total_output = batch ? laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(6), "maximum total output bytes") : request.maximum_output_bytes;
    /* Reserve space for overlapping output buffers/tuples. Node/trajectory bounds
     * remain separate; this reservation is not a measurement of total RSS. */
    if (depth < 0 || version != LAPLACE_COGNITION_MATERIALIZATION_VERSION ||
        request.maximum_nodes == 0u || request.maximum_output_bytes == 0u ||
        request.maximum_output_bytes > (uint64_t)(MaxAllocSize - VARHDRSZ) ||
        maximum_total_output == 0u || maximum_witnesses == 0u ||
        maximum_total_output > context.resource_grant.memory_bytes / 4u)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace source readback requires finite bounds and a fourfold aggregate output reservation")));
    request.maximum_depth = (uint32_t)depth;
    request.version = (uint32_t)version;
    if (batch) {
        Datum* items;
        bool* missing;
        int count;
        ArrayType* array = PG_GETARG_ARRAYTYPE_P(3);
        if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != NUMERICOID ||
            ArrayGetNItems(ARR_NDIM(array), ARR_DIMS(array)) > 4096)
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("Laplace source readback requires one bounded artifact-index vector")));
        deconstruct_array(array, NUMERICOID, -1, false, TYPALIGN_INT, &items, &missing, &count);
        if (count <= 0) ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace source readback artifact selection is empty")));
        artifact_count = (size_t)count;
        artifacts = (uint64_t*)palloc(artifact_count * sizeof(*artifacts));
        for (index = 0u; index < artifact_count; ++index) {
            if (missing[index]) ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                errmsg("Laplace source readback artifact index cannot be null")));
            artifacts[index] = laplace_pg_uint64_from_numeric(items[index], "artifact index");
        }
        pfree(items);
        pfree(missing);
        InitMaterializedSRF(fcinfo, MAT_SRF_USE_EXPECTED_DESC);
        set = (ReturnSetInfo*)fcinfo->resultinfo;
    } else {
        artifacts = (uint64_t*)palloc(sizeof(*artifacts));
        artifacts[0] = laplace_pg_uint64_from_numeric(PG_GETARG_DATUM(3), "artifact index");
    }
    bindings = (laplace_pg_source_readback_binding*)palloc0(artifact_count * sizeof(*bindings));
    laplace_pg_materialization_provider_create(&context, &owner, &provider);
    PG_TRY();
    {
        if (SPI_connect() != SPI_OK_CONNECT)
            ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                errmsg("Laplace source readback SPI connection failed")));
        connected = true;
        laplace_pg_verify_source_structural_roots(&profile, &structural, artifacts,
            artifact_count, maximum_witnesses, bindings);
        for (index = 0u; index < artifact_count; ++index) {
            if (bindings[index].byte_count > request.maximum_output_bytes ||
                bindings[index].byte_count > maximum_total_output - total_output)
                ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                    errmsg("Laplace source artifacts exceed their per-file or aggregate output bound")));
            total_output += bindings[index].byte_count;
        }
        for (index = 0u; index < artifact_count; ++index) {
            laplace_digest256 binding_id;
            laplace_content_materialization_receipt receipt;
            laplace_pg_materialization_provider_report report;
            laplace_cognition_materialization_status status;
            ErrorData* provider_error;
            size_t output_bytes = 0u;
            uint64_t operations;
            uint8_t* output = (uint8_t*)palloc((Size)bindings[index].byte_count);
            HeapTuple tuple;
            binding_identity(&context, &profile, &structural, artifacts[index],
                &bindings[index], &binding_id);
            status = laplace_content_materialize_encoded(&bindings[index].root_content_id,
                &binding_id, &bindings[index].recipe_id, &request, &provider,
                LAPLACE_COGNITION_OUTPUT_UTF8, output, (size_t)bindings[index].byte_count,
                &output_bytes, &receipt);
            laplace_pg_materialization_provider_summary(owner, &report);
            provider_error = laplace_pg_materialization_provider_take_error(owner);
            if (provider_error != NULL) ReThrowError(provider_error);
            if (status != LAPLACE_COGNITION_MATERIALIZATION_OK ||
                output_bytes != bindings[index].byte_count)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace canonical source readback failed exact native materialization"),
                    errdetail("artifact=%llu native_status=%u expected_bytes=%llu output_bytes=%zu",
                        (unsigned long long)artifacts[index], (unsigned)status,
                        (unsigned long long)bindings[index].byte_count, output_bytes)));
            if (report.database_operations < prior_operations ||
                report.database_operations - prior_operations > UINT64_MAX - bindings[index].database_operations)
                ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                    errmsg("Laplace readback database operation accounting overflowed")));
            operations = report.database_operations - prior_operations + bindings[index].database_operations;
            prior_operations = report.database_operations;
            tuple = readback_tuple(fcinfo, &profile, &structural, artifacts[index],
                &bindings[index], &receipt, output, output_bytes, operations);
            if (batch) tuplestore_puttuple(set->setResult, tuple);
            else {
                /* SPI owns tuple storage; copy the single result to the caller. */
                MemoryContext previous = MemoryContextSwitchTo(caller_context);
                single_result = heap_copytuple(tuple);
                MemoryContextSwitchTo(previous);
            }
            heap_freetuple(tuple);
            pfree(output);
        }
        if (SPI_finish() != SPI_OK_FINISH)
            ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                errmsg("Laplace source readback SPI cleanup failed")));
        connected = false;
    }
    PG_CATCH();
    {
        if (connected) (void)SPI_finish();
        laplace_pg_materialization_provider_destroy(&owner);
        PG_RE_THROW();
    }
    PG_END_TRY();
    laplace_pg_materialization_provider_destroy(&owner);
    pfree(artifacts);
    pfree(bindings);
    if (batch) return (Datum)0;
    return HeapTupleGetDatum(single_result);
}

Datum laplace_pg_source_readback_utf8(PG_FUNCTION_ARGS) {
    return source_readback(fcinfo, false);
}

Datum laplace_pg_source_readback_utf8_batch(PG_FUNCTION_ARGS) {
    return source_readback(fcinfo, true);
}
