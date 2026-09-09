#ifndef LAPLACE_POSTGRES_SEMANTIC_COGNITION_PG_H
#define LAPLACE_POSTGRES_SEMANTIC_COGNITION_PG_H

#include "postgres.h"
#include "laplace/cognition_observation_request.h"

/* Reusable PostgreSQL semantic/reference-mapping candidate provider.
 *
 * This is the same provider used by the standalone semantic cognition SQL entry
 * and by firmware execution. It emits candidate records only; native cognition
 * retains search/guidance/completion authority. PostgreSQL errors are captured
 * inside the callback so C++ native owners unwind before the outer C host
 * rethrows the original database diagnostic.
 */
typedef struct laplace_pg_semantic_provider_state
    laplace_pg_semantic_provider_state;

typedef struct laplace_pg_semantic_provider_report {
    laplace_digest256 provider_fingerprint;
    uint64_t rows_examined;
    uint64_t database_operations;
    uint64_t provider_calls;
} laplace_pg_semantic_provider_report;

void laplace_pg_semantic_provider_create(
    const laplace_cognition_observation_request* request,
    uint64_t maximum_candidate_records_per_expansion,
    laplace_pg_semantic_provider_state** owner,
    laplace_cognition_observation_candidate_provider_v1* provider);

void laplace_pg_semantic_provider_summary(
    const laplace_pg_semantic_provider_state* owner,
    laplace_pg_semantic_provider_report* report);

ErrorData* laplace_pg_semantic_provider_take_error(
    laplace_pg_semantic_provider_state* owner);

void laplace_pg_semantic_provider_destroy(
    laplace_pg_semantic_provider_state** owner);

#endif
