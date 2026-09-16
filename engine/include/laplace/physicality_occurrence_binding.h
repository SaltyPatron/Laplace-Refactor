#ifndef LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_H
#define LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_H

#include "laplace/physicality_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION = 1 };

/* Exact physical representation selected for one parent-relative interval.
 * This is view provenance, never a constituent's canonical content identity.
 * Ordinals are one-based. A binding may split a stored RLE carrier. */
typedef struct laplace_physicality_occurrence_binding {
    laplace_digest256 parent_physicality_id;
    laplace_id128 entity_id;
    laplace_digest256 selected_physicality_id;
    uint64_t first_logical_ordinal;
    uint64_t logical_count;
    uint64_t metadata;
    uint32_t version;
    uint32_t reserved;
} laplace_physicality_occurrence_binding;

typedef struct laplace_physicality_occurrence_parent {
    const laplace_persistence_physicality_record* physicality;
    const laplace_trajectory_carrier* carriers;
    uint64_t carrier_count;
} laplace_physicality_occurrence_parent;

typedef enum laplace_physicality_occurrence_status {
    LAPLACE_PHYSICALITY_OCCURRENCE_OK = 0,
    LAPLACE_PHYSICALITY_OCCURRENCE_INVALID_ARGUMENT = 1,
    LAPLACE_PHYSICALITY_OCCURRENCE_PARENT_INVALID = 2,
    LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID = 3,
    LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT = 4,
    LAPLACE_PHYSICALITY_OCCURRENCE_MEMORY_FAILURE = 5
} laplace_physicality_occurrence_status;

/* The interval pass allocates no heap. Peak temporary native memory is the
 * shared parent validator's bound for the largest individual carrier set.
 * Borrowed parent/carrier/binding arrays and output slots are excluded. */
LAPLACE_API laplace_physicality_occurrence_status
laplace_physicality_occurrence_bindings_memory_bound(
    uint64_t maximum_parent_carrier_count, uint64_t* bytes);

/* Primary bounded batch law. Parents must be strictly ordered by their full
 * native physicality ID; bindings must be grouped in that same order and then
 * by first ordinal. Every parent is a composition with exact interval coverage
 * from 1 through its logical_count, without gaps, overlap or overflow.
 *
 * Aggregate parent, carrier, binding and expanded logical work limits are
 * checked before native parent hashing. Every parent record, content identity,
 * carrier fingerprint and decoded carrier law is verified by the shared owner.
 * All intervals are validated before any successful receipt is published.
 * Empty batches have an ordinary typed empty-set receipt.
 *
 * Optional parent_receipts has at least parent_count slots. Outputs must not
 * alias borrowed inputs. The batch receipt and admitted parent output slots are
 * zero on failure; at most maximum_parents slots are ever touched, including
 * when refusing an oversized parent_count or output capacity. The receipt
 * binds every selected physicality ID; authentication of those selected child
 * bodies, witnesses and geometry belongs to the caller's existing native/PG
 * physicality owner. This API does not infer or select child geometry. */
LAPLACE_API laplace_physicality_occurrence_status
laplace_physicality_occurrence_bindings_validate_batch(
    const laplace_physicality_occurrence_parent* parents, uint64_t parent_count,
    const laplace_physicality_occurrence_binding* bindings, uint64_t binding_count,
    uint64_t maximum_parents, uint64_t maximum_carriers,
    uint64_t maximum_bindings, uint64_t maximum_logical_count,
    laplace_digest256* batch_receipt,
    laplace_digest256* parent_receipts, uint64_t parent_receipt_capacity);

/* Exact one-parent wrapper over the primary batch receipt law. */
LAPLACE_API laplace_physicality_occurrence_status
laplace_physicality_occurrence_bindings_validate(
    const laplace_persistence_physicality_record* parent,
    const laplace_trajectory_carrier* carriers, uint64_t carrier_count,
    const laplace_physicality_occurrence_binding* bindings, uint64_t binding_count,
    uint64_t maximum_carriers, uint64_t maximum_bindings,
    uint64_t maximum_logical_count, laplace_digest256* receipt);

#ifdef __cplusplus
}
#endif
#endif
