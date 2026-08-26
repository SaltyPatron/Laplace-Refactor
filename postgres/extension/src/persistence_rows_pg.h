#ifndef LAPLACE_POSTGRES_PERSISTENCE_ROWS_PG_H
#define LAPLACE_POSTGRES_PERSISTENCE_ROWS_PG_H

#include "postgres.h"

#include "laplace/persistence.h"
#include "set_pg.h"

void laplace_pg_entity_binding_open(
    laplace_pg_composite_binding* binding);

Datum laplace_pg_entity_record(
    const laplace_pg_composite_binding* binding,
    const laplace_persistence_entity_record* entity);

const char* laplace_pg_entity_insert_sql(void);
const char* laplace_pg_entity_verify_sql(void);

void laplace_pg_physicality_binding_open(
    laplace_pg_composite_binding* binding);

Datum laplace_pg_physicality_record(
    const laplace_pg_composite_binding* binding,
    const laplace_persistence_physicality_record* physicality);

const char* laplace_pg_physicality_insert_sql(void);
const char* laplace_pg_physicality_verify_sql(void);

#endif
