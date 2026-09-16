#ifndef LAPLACE_SPI_CONTEXT_PG_H
#define LAPLACE_SPI_CONTEXT_PG_H

#include "postgres.h"
#include "executor/spi.h"
#include "utils/memutils.h"

/* SPI execution finishes in the connected procedure context, not necessarily
 * the caller's allocation context. Keep query transport and nested provider
 * calls from silently changing the lifetime of subsequent palloc results.
 * This restores allocation ownership on both ordinary return and PG ERROR;
 * the called owner still owns its SPI connection and resource cleanup. */
#define LAPLACE_PG_PRESERVE_MEMORY_CONTEXT(statement) do { \
    MemoryContext const laplace_saved_memory_context = CurrentMemoryContext; \
    PG_TRY(); \
    { statement; } \
    PG_FINALLY(); \
    { MemoryContextSwitchTo(laplace_saved_memory_context); } \
    PG_END_TRY(); \
} while (0)

static inline int laplace_pg_spi_execute_with_args(
    const char* source, int nargs, Oid* argtypes, Datum* values,
    const char* nulls, bool read_only, long count) {
    volatile int result = 0;
    LAPLACE_PG_PRESERVE_MEMORY_CONTEXT(
        result = SPI_execute_with_args(source, nargs, argtypes, values,
                                       nulls, read_only, count));
    return result;
}

#endif
