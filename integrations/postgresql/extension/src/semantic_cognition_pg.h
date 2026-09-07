#ifndef LAPLACE_SEMANTIC_COGNITION_PG_H
#define LAPLACE_SEMANTIC_COGNITION_PG_H

#include "executor/spi.h"
#include "utils/memutils.h"
#include "laplace/cognition_observation_request.h"

/* Caller-owned physical state for the shared native candidate-provider boundary.
 * A connected SPI session must outlive this descriptor and every execution that
 * consumes it. Search, guidance, completion and realization stay native owners. */
typedef struct laplace_pg_semantic_provider_state {
    laplace_digest256 boundary_id;
    laplace_digest256 evidence_epoch;
    laplace_digest256 provider_fingerprint;
    uint64_t rows_examined;
    uint64_t database_operations;
    uint64_t provider_calls;
    SPIPlanPtr candidate_plan;
    MemoryContext batch_context;
    MemoryContext error_context;
    ErrorData* deferred_error;
} laplace_pg_semantic_provider_state;

void laplace_pg_semantic_provider_initialize(
    const laplace_cognition_observation_request* request,
    laplace_pg_semantic_provider_state* state,
    laplace_cognition_observation_candidate_provider_v1* provider);

void laplace_pg_semantic_provider_release(
    laplace_pg_semantic_provider_state* state);

#endif
