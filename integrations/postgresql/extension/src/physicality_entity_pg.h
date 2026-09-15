#ifndef LAPLACE_POSTGRES_PHYSICALITY_ENTITY_H
#define LAPLACE_POSTGRES_PHYSICALITY_ENTITY_H

#include "postgres.h"
#include "laplace/framework.h"
#include "laplace/persistence.h"
#include "laplace/composition.h"
#include "laplace/physicality_occurrence_binding.h"

/* Physical provider policy, not a statement about bytes per native operation.
 * One current grant byte admits at most one logical source-validation step.
 * All native allocations still use the independent checked memory estimator. */
#define LAPLACE_PG_PHYSICALITY_ENTITY_READ_MAX_OPERATIONS UINT64_C(13)
#define LAPLACE_PG_PHYSICALITY_ENTITY_READ_LOGICAL_STEPS_PER_GRANT_BYTE UINT64_C(1)

/* Finite, verified derived views. These physicalities are calculated from an
 * admitted immutable original record; reading them never deposits or reflects
 * the resulting view. Arrays and carriers belong to the caller memory context. */
typedef struct laplace_pg_physicality_entity_view {
    laplace_persistence_physicality_record physicality;
    const laplace_trajectory_carrier* carriers;
    size_t carrier_count;
    laplace_digest256 identity_witness;
    laplace_digest256 derivation_receipt;
    const laplace_composition_known_entity* external_known;
    size_t external_count;
    laplace_digest256 view_id;
    laplace_digest256 owner_recipe_fingerprint;
    laplace_composition_known_entity original_source;
    laplace_digest256 binding_set_id;
    const laplace_physicality_occurrence_binding* occurrence_bindings;
    size_t occurrence_binding_count;
    const laplace_physicality_occurrence_binding* generated_occurrence_bindings;
    size_t generated_occurrence_binding_count;
    laplace_digest256 generated_binding_receipt;
    const laplace_composition_known_entity* canonical_known;
    size_t canonical_known_count;
    const laplace_composition_known_entity* all_known;
    size_t all_known_count;
} laplace_pg_physicality_entity_view;

/* Indexed candidate generation only. Every selected view is reconstructed by
 * the native plan and checked against its retained exact owner before return.
 * All limits are admitted before SPI or native work; exhaustion is an error,
 * never a truncated successful frontier. No relation mask means no work. */
void laplace_pg_physicality_entity_candidates(
    const laplace_framework_context* context,
    const laplace_id128* source_ids,
    size_t source_count,
    uint32_t relation_mask,
    uint64_t maximum_rows,
    uint64_t maximum_memory_bytes,
    uint64_t maximum_database_operations,
    laplace_pg_physicality_entity_view** views,
    size_t* view_count,
    uint64_t* database_operations);

/* Exact selectors are parallel to entity_ids. NULL or an all-zero item means
 * no selected representation for that entity; filtering occurs before limits. */
void laplace_pg_physicality_entity_resolve(
    const laplace_framework_context* context,
    const laplace_id128* entity_ids,
    const laplace_digest256* selected_physicality_ids,
    size_t entity_count,
    uint64_t maximum_rows,
    uint64_t maximum_memory_bytes,
    uint64_t maximum_database_operations,
    laplace_pg_physicality_entity_view** views,
    size_t* view_count,
    uint64_t* database_operations);

void laplace_pg_physicality_entity_resolve_scoped(
    const laplace_framework_context* context,
    const laplace_digest256* required_view_id,
    const laplace_id128* entity_ids,
    const laplace_digest256* selected_physicality_ids,
    size_t entity_count,
    uint64_t maximum_rows,
    uint64_t maximum_memory_bytes,
    uint64_t maximum_database_operations,
    laplace_pg_physicality_entity_view** views,
    size_t* view_count,
    uint64_t* database_operations);

/* Reconstruct only the descriptor root belonging to this exact retained owner.
 * The caller must authenticate original_source E/P before historical traversal;
 * this function never chooses an entity's historical physicality. */
void laplace_pg_physicality_entity_resolve_owner(
    const laplace_framework_context* context,
    const laplace_digest256* exact_view_id,
    uint64_t maximum_memory_bytes,
    uint64_t maximum_database_operations,
    laplace_pg_physicality_entity_view** views,
    size_t* view_count,
    uint64_t* database_operations);

#endif
