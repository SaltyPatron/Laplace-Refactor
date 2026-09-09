#ifndef LAPLACE_POSTGRES_MATERIALIZATION_PG_H
#define LAPLACE_POSTGRES_MATERIALIZATION_PG_H

#include "postgres.h"
#include "laplace/cognition_materialization.h"
#include "laplace/framework.h"

/* Exact PostgreSQL-backed content readback for cognition realization. Unicode
 * atoms resolve through the active Tier-0 reverse/direct perfcaches; compositions
 * resolve through their canonical physicality/trajectory rows under the pinned
 * geometry epoch. Provider callbacks capture PostgreSQL errors so native C++
 * materialization unwinds before the host rethrows the original diagnostic. */
typedef struct laplace_pg_materialization_provider_state
    laplace_pg_materialization_provider_state;

typedef struct laplace_pg_materialization_provider_report {
    laplace_digest256 provider_fingerprint;
    laplace_digest256 readset_fingerprint;
    uint64_t resolved_nodes;
    uint64_t trajectory_reads;
    uint64_t trajectory_bytes;
    uint64_t database_operations;
} laplace_pg_materialization_provider_report;

void laplace_pg_materialization_provider_create(
    const laplace_framework_context* context,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider);

void laplace_pg_materialization_provider_summary(
    const laplace_pg_materialization_provider_state* owner,
    laplace_pg_materialization_provider_report* report);

ErrorData* laplace_pg_materialization_provider_take_error(
    laplace_pg_materialization_provider_state* owner);

void laplace_pg_materialization_provider_destroy(
    laplace_pg_materialization_provider_state** owner);

#endif
