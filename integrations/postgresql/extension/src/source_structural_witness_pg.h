#ifndef LAPLACE_POSTGRES_SOURCE_STRUCTURAL_WITNESS_PG_H
#define LAPLACE_POSTGRES_SOURCE_STRUCTURAL_WITNESS_PG_H

#include "laplace/composition.h"
#include "laplace/source_profile.h"
#include "laplace/tabular_source.h"
#include "composition_pg.h"

void laplace_pg_persist_source_structural_witnesses(
    const laplace_tabular_source_plan* plan,
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* composition_input,
    const laplace_source_profile_manifest* profile);

#endif
