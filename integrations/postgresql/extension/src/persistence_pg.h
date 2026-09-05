#ifndef LAPLACE_POSTGRES_PERSISTENCE_PG_H
#define LAPLACE_POSTGRES_PERSISTENCE_PG_H

#include <stdint.h>

#include "laplace/framework.h"
#include "laplace/persistence.h"

typedef struct laplace_pg_persistence_producer_result {
    laplace_framework_producer_receipt producer;
    laplace_persistence_summary summary;
    uint64_t inserted[4];
    laplace_digest256 plan_sequence_fingerprint;
    uint32_t plan_count;
    uint32_t reserved;
} laplace_pg_persistence_producer_result;

#if !defined(LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL)
#define LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL \
    laplace_pg_persistence_run_producer
#endif

void LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_v1* producer,
    laplace_pg_persistence_producer_result* result);

#endif
