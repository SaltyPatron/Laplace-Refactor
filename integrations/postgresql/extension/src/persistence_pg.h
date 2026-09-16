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
    uint64_t database_operations;
} laplace_pg_persistence_producer_result;

/* Caller-owned optional output. Existing consumers request no RETURNING set and
 * incur no allocation for inserted identities. Ordering is unspecified. */
typedef struct laplace_pg_persistence_inserted_entities {
    laplace_id128* ids;
    uint64_t capacity;
    uint64_t count;
} laplace_pg_persistence_inserted_entities;

typedef struct laplace_pg_persistence_options {
    /* Query executions, utility statements and actual SPI plan preparations.
     * SPI lifecycle and tuple decoding do not execute database queries. */
    uint64_t maximum_database_operations;
    laplace_pg_persistence_inserted_entities* inserted_entities;
    /* Already reserved caller/producer buffers, excluded from the sink's
     * per-batch allocation envelope without changing the execution context. */
    uint64_t reserved_memory_bytes;
} laplace_pg_persistence_options;

#if !defined(LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL)
#define LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL \
    laplace_pg_persistence_run_producer
#endif

#define LAPLACE_PG_PERSISTENCE_JOIN_IMPL(a, b) a##b
#define LAPLACE_PG_PERSISTENCE_JOIN(a, b) LAPLACE_PG_PERSISTENCE_JOIN_IMPL(a, b)
#define LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_BOUNDED_SYMBOL \
    LAPLACE_PG_PERSISTENCE_JOIN(LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL, _bounded)
#define LAPLACE_PG_PERSISTENCE_BATCH_MEMORY_SYMBOL \
    LAPLACE_PG_PERSISTENCE_JOIN(LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL, _batch_memory_bytes)

/* Shared sink allocation estimate; zero means invalid/overflowing dimensions.
 * This accounts declared buffers, not total PostgreSQL backend RSS. */
int LAPLACE_PG_PERSISTENCE_BATCH_MEMORY_SYMBOL(
    uint64_t byte_count, uint64_t record_count, uint64_t* required_bytes);

void LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_v1* producer,
    laplace_pg_persistence_producer_result* result);

void LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_BOUNDED_SYMBOL(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_framework_producer_v1* producer,
    const laplace_pg_persistence_options* options,
    laplace_pg_persistence_producer_result* result);

#endif
