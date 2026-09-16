#ifndef LAPLACE_POSTGRES_MATERIALIZATION_PG_H
#define LAPLACE_POSTGRES_MATERIALIZATION_PG_H

#include "postgres.h"
#include "laplace/cognition_materialization.h"
#include "laplace/composition.h"
#include "laplace/framework.h"

/* Exact PostgreSQL-backed content readback. Unicode
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

/* A caller may pin physical representations from an independently verified
 * native receipt. Duplicate entity bindings must select the same physicality.
 * Unbound compositions retain the strict unique-in-geometry resolution law.
 * The independently verified selection receipt identifies the complete binding
 * authority, so selecting a subset does not change individual node receipts. */
typedef struct laplace_pg_materialization_selection {
    laplace_id128 entity_id;
    laplace_digest256 physicality_id;
} laplace_pg_materialization_selection;

void laplace_pg_materialization_provider_create(
    const laplace_framework_context* context,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider);

/* Require the exact retained view owner on every read. An absent owner or a
 * mismatched source receipt is an integrity failure, never ordinary fallback. */
void laplace_pg_materialization_provider_create_for_view(
    const laplace_framework_context* context,
    const laplace_digest256* required_view_id,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider);

/* The original source physicality is read under that retained observation's
 * historical occurrence bindings, separately from generated reference views. */
void laplace_pg_materialization_provider_create_for_view_selected(
    const laplace_framework_context* context,
    const laplace_digest256* required_view_id,
    const laplace_digest256* original_root_physicality_id,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider);

void laplace_pg_materialization_provider_create_selected(
    const laplace_framework_context* context,
    const laplace_digest256* selection_receipt,
    const laplace_pg_materialization_selection* selections,
    size_t selection_count,
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
