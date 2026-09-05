#ifndef LAPLACE_POSTGRES_COMPOSITION_PG_H
#define LAPLACE_POSTGRES_COMPOSITION_PG_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "persistence_pg.h"

typedef struct laplace_pg_composition_execution {
    laplace_composition_working_set* working_set;
    laplace_composition_presence_receipt presence;
    laplace_pg_persistence_producer_result persistence;
    laplace_composition_working_set_summary summary;
    const laplace_composition_result* results;
    const uint8_t* entity_dispositions;
    const uint8_t* physicality_dispositions;
    size_t result_count;
    size_t entity_disposition_count;
    size_t physicality_disposition_count;
    uint32_t effect_disposition;
    uint8_t persistence_executed;
} laplace_pg_composition_execution;

#if !defined(LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL)
#define LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL laplace_pg_composition_execute
#endif

#if !defined(LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL)
#define LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL \
    laplace_pg_composition_execution_destroy
#endif

#if !defined(LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL)
#define LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL \
    laplace_pg_persist_composition_execution_receipt
#endif

void LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution);

void LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(
    laplace_pg_composition_execution* execution);

void LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL(
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* input);

#endif
