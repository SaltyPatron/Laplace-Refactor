#ifndef LAPLACE_POSTGRES_PERSISTENCE_ENTITIES_PG_H
#define LAPLACE_POSTGRES_PERSISTENCE_ENTITIES_PG_H

#include "persistence_pg.h"

/* Peak declared adapter buffers plus the common sink's largest batch estimate.
 * Zero means invalid/overflowing dimensions. Caller-owned input/output buffers
 * are separate; this is not a bound on total PostgreSQL backend RSS. */
uint64_t laplace_pg_persistence_entities_memory_bytes(
    uint64_t entity_count, uint64_t maximum_batch_bytes);

/* Ordered unique native entity rows only. This producer cannot emit a
 * physicality, attestation, consensus row or a fabricated presence result.
 * options is required and retains the caller's finite remaining operation and
 * memory bounds. Its reserved_memory_bytes excludes the adapter allocation
 * returned by the sizing function above. The real common deposit receipt and
 * actual INSERT RETURNING identities are returned through result/options. */
void laplace_pg_persistence_deposit_entities(
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    const laplace_persistence_entity_record* entities,
    uint64_t entity_count,
    uint64_t maximum_batch_bytes,
    const laplace_pg_persistence_options* options,
    laplace_pg_persistence_producer_result* result);

#endif
