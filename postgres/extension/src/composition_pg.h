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
} laplace_pg_composition_execution;

void laplace_pg_composition_execute(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution);

void laplace_pg_composition_execution_destroy(
    laplace_pg_composition_execution* execution);

#endif
