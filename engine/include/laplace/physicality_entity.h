#ifndef LAPLACE_PHYSICALITY_ENTITY_H
#define LAPLACE_PHYSICALITY_ENTITY_H

#include "laplace/composition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_physicality_entity_plan laplace_physicality_entity_plan;

typedef struct laplace_physicality_entity_validation {
    laplace_id128 realized_entity_id;
    laplace_digest256 realized_identity_witness;
    uint8_t witness_available;
    uint8_t tier_floor;
    uint8_t tier_available;
    uint8_t reserved[5];
} laplace_physicality_entity_validation;

typedef struct laplace_physicality_entity_input {
    const laplace_persistence_physicality_record* physicality;
    const laplace_trajectory_carrier* carriers;
    uint64_t carrier_count;
    /* Geometry belongs to this derived view, not to descriptor content identity. */
    laplace_digest256 view_geometry_epoch;
    uint64_t maximum_requests;
    uint64_t maximum_operands;
    uint64_t maximum_carriers;
    /* Identity validation hashes the complete logical sequence, including runs. */
    uint64_t maximum_logical_count;
} laplace_physicality_entity_input;

/* Known-entity order is external_entity_ids followed by atom_positions. Resolve
 * those through the existing canonical owner, then execute these requests with
 * the ordinary composition working set. No native identity/hash is substituted.
 * Every generated request is a finite derived view: occurrence flags are zero.
 * The caller must not recursively deposit/reflect the view's physicalities.
 */
typedef struct laplace_physicality_entity_plan_view {
    laplace_digest256 physicality_record_id;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 view_geometry_epoch;
    laplace_id128 realized_entity_id;
    const laplace_id128* external_entity_ids;
    const uint32_t* atom_positions;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    uint64_t external_entity_count;
    uint64_t atom_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t root_result_index;
    uint32_t version;
    uint32_t reserved;
    laplace_physicality_entity_validation source_validation;
} laplace_physicality_entity_plan_view;

typedef enum laplace_physicality_entity_status {
    LAPLACE_PHYSICALITY_ENTITY_OK = 0,
    LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT = 1,
    LAPLACE_PHYSICALITY_ENTITY_RECORD_INVALID = 2,
    LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID = 3,
    LAPLACE_PHYSICALITY_ENTITY_LIMIT = 4,
    LAPLACE_PHYSICALITY_ENTITY_MEMORY_FAILURE = 5
} laplace_physicality_entity_status;

/* Conservative native-owned peak workspace bounds, excluding borrowed input
 * carrier bytes. Include vector spare capacity, index maps and validation
 * scratch. Callers admit these before entering native allocation or hashing.
 */
LAPLACE_API laplace_physicality_entity_status laplace_physicality_entity_plan_memory_bound(
    const laplace_physicality_entity_input* input, uint64_t* bytes);
LAPLACE_API laplace_physicality_entity_status laplace_physicality_entity_validation_memory_bound(
    uint64_t carrier_count, uint64_t* bytes);

/* Read-only validation shared by reflection and external selection admission.
 * Atomic points accept zero carriers; no content tier is inferred from geometry.
 * Both the physical carrier envelope and expanded identity work are bounded.
 */
LAPLACE_API laplace_physicality_entity_status laplace_physicality_entity_record_validate(
    const laplace_persistence_physicality_record* physicality,
    const laplace_trajectory_carrier* carriers,
    uint64_t carrier_count,
    uint64_t maximum_carriers,
    uint64_t maximum_logical_count,
    laplace_physicality_entity_validation* validation);

LAPLACE_API laplace_physicality_entity_status laplace_physicality_entity_plan_create(
    const laplace_physicality_entity_input* input,
    laplace_physicality_entity_plan** plan);

LAPLACE_API laplace_physicality_entity_status laplace_physicality_entity_plan_view_get(
    const laplace_physicality_entity_plan* plan,
    laplace_physicality_entity_plan_view* view);

LAPLACE_API void laplace_physicality_entity_plan_destroy(
    laplace_physicality_entity_plan** plan);

#ifdef __cplusplus
}
#endif
#endif
