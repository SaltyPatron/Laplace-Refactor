#ifndef LAPLACE_POSTGRES_COGNITION_PROVIDER_H
#define LAPLACE_POSTGRES_COGNITION_PROVIDER_H

#include "postgres.h"
#include "laplace/cognition_observation_request.h"

/* PostgreSQL's physical provider, shared by standalone search and firmware.
 * It selects indexed persisted physicalities; the native engine alone decodes,
 * validates and derives structural candidates. It does not supply semantics,
 * select a goal, execute a second search or manufacture testimony.
 *
 * Create/destroy/take_error are outer C-host operations. The candidate callback
 * catches PostgreSQL ERROR internally and returns through native C++ normally.
 * Call take_error only after native execution has unwound; rethrow after releasing
 * all native results. Captured failure prevents subsequent callback work.
 *
 * The host supplies a separately reserved memory grant, keeps a PostgreSQL SPI
 * connection and snapshot active during enumeration, and pins the same evidence
 * boundary/epoch for every firmware step. Query limits are conserved across the
 * owner's lifetime. Request storage is copied, so earlier stack requests may die.
 */
typedef struct laplace_pg_cognition_provider laplace_pg_cognition_provider;
typedef struct laplace_pg_cognition_provider_report {
    laplace_digest256 provider_fingerprint;
    laplace_digest256 readset_fingerprint;
    uint64_t rows_fetched;
    uint64_t carriers_decoded;
    uint64_t logical_occurrences;
    uint64_t indexed_entities;
    uint64_t trajectory_bytes;
    uint64_t database_operations;
    uint64_t batch_count;
} laplace_pg_cognition_provider_report;

void laplace_pg_cognition_provider_create(
    const laplace_cognition_observation_request* request,
    uint64_t provider_memory_bytes,
    laplace_pg_cognition_provider** owner,
    laplace_cognition_observation_candidate_provider_v1* provider);
void laplace_pg_cognition_provider_summary(
    const laplace_pg_cognition_provider* owner,
    laplace_pg_cognition_provider_report* report);
ErrorData* laplace_pg_cognition_provider_take_error(laplace_pg_cognition_provider* owner);
void laplace_pg_cognition_provider_destroy(laplace_pg_cognition_provider** owner);

#endif
