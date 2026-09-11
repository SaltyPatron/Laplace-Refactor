#ifndef LAPLACE_POSTGRES_SOURCE_PROFILE_PG_H
#define LAPLACE_POSTGRES_SOURCE_PROFILE_PG_H

#include <stddef.h>

#include "postgres.h"
#include "access/htup_details.h"
#include "laplace/isa.h"
#include "laplace/source_profile.h"

typedef struct laplace_pg_source_profile_execution {
    laplace_source_profile_receipt semantic_receipt;
    laplace_isa_receipt isa_receipt;
} laplace_pg_source_profile_execution;

void laplace_pg_read_source_profile(
    HeapTupleHeader tuple,
    laplace_source_profile_manifest* profile);

/* One semantic owner for source-profile validation + durable publication.
 * SQL entrypoints and native source adapters both call this function; internal
 * callers do not serialize native records into SQL just to deserialize them
 * again in the same backend. */
void laplace_pg_source_profile_execute_and_persist(
    const laplace_framework_context* context,
    const laplace_source_profile_manifest* profiles,
    size_t profile_count,
    laplace_pg_source_profile_execution* result);

#endif
