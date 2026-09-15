#ifndef LAPLACE_CONTENT_REFERENCE_VIEW_H
#define LAPLACE_CONTENT_REFERENCE_VIEW_H

#include "laplace/physicality_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { LAPLACE_CONTENT_REFERENCE_VIEW_VERSION = 1 };
typedef struct laplace_content_reference_plan laplace_content_reference_plan;

typedef struct laplace_content_reference_source {
    const laplace_persistence_physicality_record* physicality;
    const laplace_trajectory_carrier* carriers;
    uint64_t carrier_count;
    laplace_digest256 identity_witness;
} laplace_content_reference_source;

/* The caller's pinned canonical atom provider owns placement authority. Native
 * validation binds its exact known tuple to the atomic P body, codepoint/full
 * witness and active geometry epoch. Historical source atom geometry is never
 * substituted for this separately admitted provider input. */
typedef struct laplace_content_reference_atom {
    laplace_composition_known_entity known;
    const laplace_persistence_physicality_record* physicality;
} laplace_content_reference_atom;

typedef struct laplace_content_reference_limits {
    uint64_t maximum_roots;
    uint64_t maximum_nodes;
    uint64_t maximum_physicalities;
    uint64_t maximum_carriers;
    uint64_t maximum_logical_count;
    uint64_t maximum_depth;
    uint64_t maximum_memory_bytes;
} laplace_content_reference_limits;

typedef struct laplace_content_reference_input {
    const laplace_framework_context* context;
    const laplace_id128* roots;
    uint64_t root_count;
    const laplace_content_reference_source* sources;
    uint64_t source_count;
    const laplace_content_reference_atom* atoms;
    uint64_t atom_count;
    laplace_content_reference_limits limits;
} laplace_content_reference_input;

typedef struct laplace_content_reference_root {
    laplace_id128 entity_id;
    laplace_digest256 identity_witness;
    uint64_t reference_index;
    uint32_t reference_kind;
    uint32_t reserved;
} laplace_content_reference_root;

typedef enum laplace_content_reference_status {
    LAPLACE_CONTENT_REFERENCE_OK = 0,
    LAPLACE_CONTENT_REFERENCE_INCOMPLETE = 1,
    LAPLACE_CONTENT_REFERENCE_INVALID_ARGUMENT = 2,
    LAPLACE_CONTENT_REFERENCE_CONTEXT_INVALID = 3,
    LAPLACE_CONTENT_REFERENCE_SOURCE_INVALID = 4,
    LAPLACE_CONTENT_REFERENCE_IDENTITY_INVALID = 5,
    LAPLACE_CONTENT_REFERENCE_ATOM_INVALID = 6,
    LAPLACE_CONTENT_REFERENCE_CYCLE = 7,
    LAPLACE_CONTENT_REFERENCE_LIMIT = 8,
    LAPLACE_CONTENT_REFERENCE_MEMORY_FAILURE = 9,
    LAPLACE_CONTENT_REFERENCE_RESULT_INVALID = 10
} laplace_content_reference_status;

typedef struct laplace_content_reference_plan_view {
    const laplace_framework_context* context;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 view_geometry_epoch;
    const laplace_composition_known_entity* known_entities;
    const laplace_composition_operand* operands;
    const laplace_composition_request* requests;
    const laplace_content_reference_root* roots;
    const laplace_id128* missing_entity_ids;
    uint64_t known_entity_count;
    uint64_t operand_count;
    uint64_t request_count;
    uint64_t root_count;
    uint64_t missing_entity_count;
    uint64_t authenticated_physicality_count;
    uint64_t distinct_node_count;
    uint64_t carrier_count;
    uint64_t logical_count;
    uint64_t maximum_observed_depth;
    uint64_t memory_bound_bytes;
    uint32_t version;
    uint32_t status;
} laplace_content_reference_plan_view;

/* Conservative native-owned peak including copied plan buffers, bounded graph
 * traversal and the shared physicality validator's scratch. Borrowed inputs and
 * a subsequently created ordinary composition working set are excluded. This
 * preflight checks aggregate input work before any body hashing/allocation.
 * Create enforces this bound against both limits and the context memory grant. */
LAPLACE_API laplace_content_reference_status
laplace_content_reference_plan_memory_bound(
    const laplace_content_reference_input* input, uint64_t* bytes);

/* Every supplied form is authenticated; forms of one E must agree on the exact
 * normalized ordered child-ID runs and complete witness. Roles and historical
 * geometry do not choose reference geometry. Singleton [E] views are transparent
 * and do not close E without an independent noncollapsed body or pinned atom.
 *
 * OK returns deduplicated postorder ordinary-composition requests with neutral
 * roles and no occurrence flags. The shared composer owns canonical metadata,
 * atom/tier propagation, geometry and RLE. INCOMPLETE returns an inspectable plan
 * with sorted missing E frontier but ZERO executable buffers/root mappings; a
 * caller may fetch that frontier in a batch and retry the complete input. Other
 * failures return a null plan. No persistence, reflection or SQL occurs here. */
LAPLACE_API laplace_content_reference_status laplace_content_reference_plan_create(
    const laplace_content_reference_input* input, laplace_content_reference_plan** plan);
LAPLACE_API laplace_content_reference_status laplace_content_reference_plan_view_get(
    const laplace_content_reference_plan* plan, laplace_content_reference_plan_view* view);

/* Validate every calculated result's E/full witness against the closed plan.
 * This helper cannot accept an incomplete plan or a truncated/foreign result
 * array. The caller retains ordinary working-set physicality/trajectory views. */
LAPLACE_API laplace_content_reference_status laplace_content_reference_plan_verify_results(
    const laplace_content_reference_plan* plan,
    const laplace_composition_result* results, uint64_t result_count);
LAPLACE_API void laplace_content_reference_plan_destroy(laplace_content_reference_plan** plan);

#ifdef __cplusplus
}
#endif
#endif
