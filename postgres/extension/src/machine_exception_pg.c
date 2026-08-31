#include "postgres.h"

#include <stdint.h>

#include "access/htup_details.h"
#include "fmgr.h"
#include "funcapi.h"

#include "laplace/machine_exception.h"

PG_FUNCTION_INFO_V1(laplace_pg_machine_exception_registry);

static void laplace_pg_machine_exception_descriptor_values(
    const laplace_machine_exception_descriptor* descriptor,
    Datum values[6],
    bool nulls[6]) {
    size_t index = 0;

    if (laplace_machine_exception_descriptor_validate(descriptor) !=
        LAPLACE_MACHINE_EXCEPTION_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("PostgreSQL machine-exception descriptor diverges from the native registry")));
    }
    for (index = 0; index < 6u; ++index) {
        nulls[index] = false;
    }
    values[0] = Int64GetDatum((int64)descriptor->condition);
    values[1] = Int64GetDatum((int64)descriptor->kind);
    values[2] = Int64GetDatum((int64)descriptor->priority);
    values[3] = Int64GetDatum((int64)descriptor->capability_flags);
    values[4] = Int64GetDatum((int64)descriptor->recovery_disposition);
    values[5] = Int64GetDatum((int64)descriptor->publication_disposition);
}

Datum laplace_pg_machine_exception_registry(PG_FUNCTION_ARGS) {
    FuncCallContext* function_context = NULL;

    if (SRF_IS_FIRSTCALL()) {
        MemoryContext previous_context;
        TupleDesc tuple_descriptor = NULL;

        if (laplace_machine_exception_registry_validate() !=
            LAPLACE_MACHINE_EXCEPTION_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("native machine-exception registry is invalid")));
        }
        function_context = SRF_FIRSTCALL_INIT();
        previous_context = MemoryContextSwitchTo(
            function_context->multi_call_memory_ctx);
        if (get_call_result_type(fcinfo, NULL, &tuple_descriptor) !=
            TYPEFUNC_COMPOSITE) {
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("machine_exception_registry requires a composite return type")));
        }
        function_context->tuple_desc = BlessTupleDesc(tuple_descriptor);
        function_context->max_calls =
            (uint64)laplace_machine_exception_descriptor_count();
        MemoryContextSwitchTo(previous_context);
    }

    function_context = SRF_PERCALL_SETUP();
    if (function_context->call_cntr < function_context->max_calls) {
        const laplace_machine_exception_descriptor* descriptors =
            laplace_machine_exception_descriptors();
        const laplace_machine_exception_descriptor* descriptor =
            &descriptors[function_context->call_cntr];
        Datum values[6];
        bool nulls[6];
        HeapTuple tuple;

        laplace_pg_machine_exception_descriptor_values(
            descriptor, values, nulls);
        tuple = heap_form_tuple(function_context->tuple_desc, values, nulls);
        SRF_RETURN_NEXT(function_context, HeapTupleGetDatum(tuple));
    }
    SRF_RETURN_DONE(function_context);
}
