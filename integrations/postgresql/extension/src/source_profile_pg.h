#ifndef LAPLACE_POSTGRES_SOURCE_PROFILE_PG_H
#define LAPLACE_POSTGRES_SOURCE_PROFILE_PG_H

#include "postgres.h"

#include "access/htup_details.h"
#include "laplace/source_profile.h"

void laplace_pg_read_source_profile(
    HeapTupleHeader tuple,
    laplace_source_profile_manifest* profile);

#endif
