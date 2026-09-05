#ifndef LAPLACE_POSTGRES_UNICODE_ROOT_PG_H
#define LAPLACE_POSTGRES_UNICODE_ROOT_PG_H

#include "postgres.h"

#include "laplace/framework.h"
#include "laplace/unicode_root.h"

typedef struct laplace_pg_unicode_sink laplace_pg_unicode_sink;

typedef struct laplace_pg_unicode_sink_configuration {
    laplace_unicode_root_stream_expectation expectation;
    laplace_unicode_root_stream_summary expected_summary;
} laplace_pg_unicode_sink_configuration;

typedef struct laplace_pg_unicode_sink_result {
    laplace_unicode_root_stream_summary root_summary;
    laplace_digest256 artifact_fingerprint;
    laplace_digest256 plan_manifest_fingerprint;
    laplace_digest256 plan_sequence_fingerprint;
    uint64 batch_count;
    uint64 plan_count;
    uint64 family_batch_counts[4];
    uint64 persisted_counts[6];
    uint32 sealed;
    uint32 reserved;
} laplace_pg_unicode_sink_result;

void laplace_pg_unicode_sink_create(
    const laplace_pg_unicode_sink_configuration* configuration,
    laplace_pg_unicode_sink** state,
    laplace_framework_sink_v1* sink);

void laplace_pg_unicode_sink_result_get(
    const laplace_pg_unicode_sink* state,
    laplace_pg_unicode_sink_result* result);

void laplace_pg_unicode_sink_destroy(
    laplace_pg_unicode_sink** state);

#endif
