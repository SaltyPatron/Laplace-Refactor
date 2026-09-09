#ifndef LAPLACE_POSTGRES_REALIZATION_PG_H
#define LAPLACE_POSTGRES_REALIZATION_PG_H

#include "postgres.h"

#include "laplace/cognition_realization.h"
#include "laplace/framework.h"

/*
 * Exact-witness realization over canonical PostgreSQL content.
 *
 * This provider does not generate language, infer a modality, or manufacture a
 * surface form. It can only reuse the exact canonical content selected by a
 * completed semantic act when that content is actually persisted. Requests that
 * require language- or register-specific realization remain typed unsupported
 * until the selected language/grammar evidence supplies that stronger provider.
 */
typedef struct laplace_pg_exact_realization_provider_state
    laplace_pg_exact_realization_provider_state;

typedef struct laplace_pg_exact_realization_provider_report {
    laplace_digest256 provider_fingerprint;
    uint64_t rows_examined;
    uint64_t database_operations;
    uint64_t provider_calls;
} laplace_pg_exact_realization_provider_report;

void laplace_pg_exact_realization_provider_create(
    const laplace_framework_context* context,
    laplace_pg_exact_realization_provider_state** owner,
    laplace_cognition_realization_provider_v1* provider);

void laplace_pg_exact_realization_provider_summary(
    const laplace_pg_exact_realization_provider_state* owner,
    laplace_pg_exact_realization_provider_report* report);

ErrorData* laplace_pg_exact_realization_provider_take_error(
    laplace_pg_exact_realization_provider_state* owner);

void laplace_pg_exact_realization_provider_destroy(
    laplace_pg_exact_realization_provider_state** owner);

#endif
