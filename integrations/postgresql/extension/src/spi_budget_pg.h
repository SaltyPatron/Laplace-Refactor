#ifndef LAPLACE_POSTGRES_SPI_BUDGET_H
#define LAPLACE_POSTGRES_SPI_BUDGET_H

#include "postgres.h"

#include <stdint.h>

#include "utils/errcodes.h"

/* A view over a caller-owned counter. Charge immediately before each explicit
 * SPI_prepare or query execution, including attempts that raise ERROR. Cached
 * plans and mapped native generations consume no additional operation. NULL
 * preserves the existing unmetered interface; this does not measure backend I/O. */
typedef struct laplace_pg_spi_budget {
    uint64_t* used;
    uint64_t maximum;
} laplace_pg_spi_budget;

static inline void laplace_pg_spi_budget_charge(laplace_pg_spi_budget* budget) {
    if (budget == NULL)
        return;
    if (budget->used == NULL)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace PostgreSQL SPI operation budget has no counter")));
    if (*budget->used >= budget->maximum)
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace PostgreSQL SPI operation budget exhausted before execution or preparation")));
    *budget->used += UINT64_C(1);
}

#endif
