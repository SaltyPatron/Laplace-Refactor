#ifndef LAPLACE_POSTGRES_COMPOSITION_PG_H
#define LAPLACE_POSTGRES_COMPOSITION_PG_H

#include <stddef.h>
#include <stdint.h>
#include "utils/array.h"

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

#define LAPLACE_PG_COMPOSITION_OBSERVED_NAME_INNER(name) name##_observed
#define LAPLACE_PG_COMPOSITION_OBSERVED_NAME(name) \
    LAPLACE_PG_COMPOSITION_OBSERVED_NAME_INNER(name)
#define LAPLACE_PG_COMPOSITION_EXECUTE_OBSERVED_SYMBOL \
    LAPLACE_PG_COMPOSITION_OBSERVED_NAME(LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL)

#if !defined(LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL)
#define LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL \
    laplace_pg_composition_execution_destroy
#endif

#if !defined(LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL)
#define LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL \
    laplace_pg_persist_composition_execution_receipt
#endif

/* Shared durable presence provider used by composition deposit and raw prompt
 * admission. It performs the same set-oriented canonical entity/physicality
 * conflict check; prompt admission does not own a second SQL presence engine. */
void laplace_pg_composition_presence_provider(
    laplace_composition_presence_provider_v1* provider);

/* Shared exact SQL transport decoder; callers must independently authenticate
 * the selected durable physicality inputs before deriving another view. */
laplace_composition_known_entity* laplace_pg_composition_read_known_entities(
    ArrayType* array, uint64_t* count);

void LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution);

/* Observe the exact resolved candidates before their construction storage is
 * released or the sealed stream is deposited. Borrowed candidate pointers are
 * valid only during this callback; the observer must not mutate the working set.
 * A PostgreSQL error follows the same execution cleanup as a producer failure.
 * The ordinary execute entrypoint calls this owner with no observer. */
typedef void (*laplace_pg_composition_observer)(
    const laplace_pg_composition_execution* execution, void* state);

void LAPLACE_PG_COMPOSITION_EXECUTE_OBSERVED_SYMBOL(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution,
    laplace_pg_composition_observer observer,
    void* observer_state);

void LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(
    laplace_pg_composition_execution* execution);

/* Explicit administrative integrity proof after canonical metadata presence has
 * succeeded. Reads only the selected reconstructed trajectories in bounded sets.
 * The caller holds a stable snapshot/lock covering the physicality rows. */
void laplace_pg_composition_verify_stored_trajectories(
    const laplace_pg_composition_execution* execution,
    uint64_t maximum_batch_bytes);

void LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL(
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* input);

#endif
