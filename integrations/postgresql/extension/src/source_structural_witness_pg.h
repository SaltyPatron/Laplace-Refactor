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
    const laplace_source_profile_manifest* profile,
    laplace_digest256* current_execution_receipt);

/* Rehash a v3 historical witness or a v4 current execution, including its exact
 * canonical hash and authenticated v3 baseline, in one bounded streaming read.
 * No rows are changed. The selected Unicode root is returned only on success. */
typedef struct laplace_pg_source_readback_binding {
    laplace_id128 root_content_id;
    laplace_digest256 root_physicality_id;
    laplace_digest256 recipe_id;
    laplace_digest256 composition_receipt_id;
    uint64_t byte_count;
    uint64_t witness_count;
    uint64_t database_operations;
} laplace_pg_source_readback_binding;

void laplace_pg_verify_source_structural_root(
    const laplace_digest256* profile_id, const laplace_digest256* receipt_id,
    uint64_t artifact_index, uint64_t maximum_witnesses,
    laplace_pg_source_readback_binding* binding);

void laplace_pg_verify_source_structural_roots(
    const laplace_digest256* profile_id, const laplace_digest256* receipt_id,
    const uint64_t* artifact_indexes, size_t artifact_count, uint64_t maximum_witnesses,
    laplace_pg_source_readback_binding* bindings);

#endif
