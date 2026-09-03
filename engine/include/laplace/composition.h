#ifndef LAPLACE_COMPOSITION_H
#define LAPLACE_COMPOSITION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/composition.h"
#include "laplace/export.h"
#include "laplace/framework.h"
#include "laplace/geometry.h"
#include "laplace/persistence.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_composition_working_set laplace_composition_working_set;

typedef struct laplace_composition_known_entity {
    laplace_id128 entity_id;
    laplace_digest256 identity_witness;
    laplace_digest256 physicality_id;
    laplace_point4d centroid;
    uint32_t atom;
    uint8_t tier_floor;
    uint8_t has_atom;
    uint16_t reserved;
} laplace_composition_known_entity;

typedef struct laplace_composition_operand {
    uint64_t reference_index;
    uint64_t multiplicity;
    uint64_t relationship_metadata;
    uint32_t reference_kind;
    uint32_t flags;
} laplace_composition_operand;

typedef struct laplace_composition_request {
    uint64_t first_operand;
    uint64_t operand_count;
    uint64_t source_ordinal;
    uint32_t recipe_version;
    uint32_t flags;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
} laplace_composition_request;

typedef struct laplace_composition_result {
    laplace_id128 entity_id;
    laplace_digest256 identity_witness;
    laplace_digest256 physicality_id;
    laplace_point4d centroid;
    double radius;
    uint64_t logical_count;
    uint64_t trajectory_vertex_count;
    uint8_t tier_floor;
    uint8_t collapsed;
    uint16_t reserved16;
    uint32_t reserved32;
} laplace_composition_result;

typedef struct laplace_composition_entity_candidate {
    laplace_persistence_entity_record entity;
    uint8_t tier_floor;
    uint8_t reserved8[7];
} laplace_composition_entity_candidate;

typedef struct laplace_composition_working_set_input {
    const laplace_framework_context* context;
    const laplace_digest256* source_fingerprint;
    const laplace_digest256* calculation_recipe_fingerprint;
    const laplace_composition_known_entity* known_entities;
    uint64_t known_entity_count;
    const laplace_composition_operand* operands;
    uint64_t operand_count;
    const laplace_composition_request* requests;
    uint64_t request_count;
    uint64_t preferred_batch_bytes;
    uint64_t reserved;
} laplace_composition_working_set_input;

typedef struct laplace_composition_working_set_summary {
    laplace_digest256 receipt_id;
    laplace_digest256 presence_receipt_id;
    laplace_digest256 context_fingerprint;
    laplace_digest256 source_fingerprint;
    laplace_digest256 calculation_recipe_fingerprint;
    laplace_digest256 input_fingerprint;
    laplace_digest256 stream_fingerprint;
    uint64_t known_entity_count;
    uint64_t request_count;
    uint64_t operand_count;
    uint64_t unique_entity_count;
    uint64_t unique_physicality_count;
    uint64_t novel_entity_count;
    uint64_t novel_physicality_count;
    uint64_t novel_trajectory_vertex_count;
    uint64_t trajectory_vertex_count;
    uint64_t occurrence_count;
    uint64_t logical_occurrence_count;
    uint64_t collapsed_request_count;
    uint64_t deduplicated_entity_count;
    uint64_t batch_count;
    uint64_t stream_record_count;
    uint64_t stream_byte_count;
    uint64_t estimated_peak_working_bytes;
    uint32_t maximum_tier_floor;
    uint32_t presence_applied;
    uint32_t status;
} laplace_composition_working_set_summary;

typedef struct laplace_composition_presence_provider_result {
    laplace_digest256 provider_fingerprint;
    laplace_digest256 provider_receipt_id;
    uint64_t returned_entity_count;
    uint64_t returned_physicality_count;
    uint64_t entity_round_count;
    uint64_t physicality_round_count;
    uint32_t flags;
    uint32_t reserved;
} laplace_composition_presence_provider_result;

typedef struct laplace_composition_presence_receipt {
    laplace_digest256 semantic_receipt_id;
    laplace_digest256 execution_receipt_id;
    laplace_digest256 working_set_input_fingerprint;
    laplace_digest256 candidate_fingerprint;
    laplace_digest256 disposition_fingerprint;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 provider_receipt_id;
    uint64_t entity_candidate_count;
    uint64_t physicality_candidate_count;
    uint64_t participating_tier_count;
    uint64_t entity_round_count;
    uint64_t physicality_round_count;
    uint32_t status;
    uint32_t reserved;
} laplace_composition_presence_receipt;

typedef enum laplace_composition_status {
    LAPLACE_COMPOSITION_OK = 0,
    LAPLACE_COMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_COMPOSITION_CONTEXT_INVALID = 2,
    LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT = 3,
    LAPLACE_COMPOSITION_REFERENCE_INVALID = 4,
    LAPLACE_COMPOSITION_IDENTITY_INVALID = 5,
    LAPLACE_COMPOSITION_IDENTITY_COLLISION = 6,
    LAPLACE_COMPOSITION_METADATA_INVALID = 7,
    LAPLACE_COMPOSITION_TIER_OVERFLOW = 8,
    LAPLACE_COMPOSITION_COUNT_OVERFLOW = 9,
    LAPLACE_COMPOSITION_GEOMETRY_INVALID = 10,
    LAPLACE_COMPOSITION_TRAJECTORY_INVALID = 11,
    LAPLACE_COMPOSITION_PERSISTENCE_INVALID = 12,
    LAPLACE_COMPOSITION_MEMORY_FAILURE = 13,
    LAPLACE_COMPOSITION_PRESENCE_INVALID = 14,
    LAPLACE_COMPOSITION_PRESENCE_ALREADY_APPLIED = 15,
    LAPLACE_COMPOSITION_PRESENCE_REQUIRED = 16
} laplace_composition_status;

typedef enum laplace_composition_presence_disposition {
    LAPLACE_COMPOSITION_NOVEL = 0,
    LAPLACE_COMPOSITION_EXACT_PRESENT = 1
} laplace_composition_presence_disposition;

typedef laplace_composition_status (*laplace_composition_presence_resolve_fn)(
    void* state,
    const laplace_composition_entity_candidate* entity_candidates,
    size_t entity_candidate_count,
    const laplace_persistence_physicality_record* physicality_candidates,
    size_t physicality_candidate_count,
    uint8_t* entity_dispositions,
    uint8_t* physicality_dispositions,
    laplace_composition_presence_provider_result* result);

typedef struct laplace_composition_presence_provider_v1 {
    void* state;
    laplace_composition_presence_resolve_fn resolve;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_composition_presence_provider_v1;

LAPLACE_API laplace_composition_status laplace_composition_working_set_create(
    const laplace_composition_working_set_input* input,
    laplace_composition_working_set** working_set);

LAPLACE_API laplace_composition_status laplace_composition_working_set_summary_get(
    const laplace_composition_working_set* working_set,
    laplace_composition_working_set_summary* summary);

LAPLACE_API laplace_composition_status
laplace_composition_working_set_effect_disposition_get(
    const laplace_composition_working_set* working_set,
    uint32_t* effect_disposition);

LAPLACE_API const laplace_composition_result* laplace_composition_working_set_results(
    const laplace_composition_working_set* working_set,
    size_t* result_count);

LAPLACE_API const laplace_composition_entity_candidate*
laplace_composition_working_set_entity_candidates(
    const laplace_composition_working_set* working_set,
    size_t* candidate_count);

LAPLACE_API const uint8_t*
laplace_composition_working_set_entity_dispositions(
    const laplace_composition_working_set* working_set,
    size_t* disposition_count);

LAPLACE_API const uint8_t*
laplace_composition_working_set_physicality_dispositions(
    const laplace_composition_working_set* working_set,
    size_t* disposition_count);

LAPLACE_API laplace_composition_status
laplace_composition_working_set_physicality_candidate_get(
    const laplace_composition_working_set* working_set,
    size_t candidate_index,
    laplace_persistence_physicality_record* candidate);

LAPLACE_API laplace_composition_status
laplace_composition_working_set_resolve_presence(
    laplace_composition_working_set* working_set,
    const laplace_composition_presence_provider_v1* provider,
    laplace_composition_presence_receipt* receipt);

LAPLACE_API laplace_composition_status laplace_composition_working_set_producer(
    laplace_composition_working_set* working_set,
    laplace_framework_producer_v1* producer);

/*
 * Releases presence-candidate and trajectory construction storage after the
 * canonical stream has been sealed. Results, dispositions, summaries,
 * receipts, and producer output remain available. Physical providers use this
 * boundary before copying the canonical stream into external persistence.
 */
LAPLACE_API laplace_composition_status
laplace_composition_working_set_compact_publication_input(
    laplace_composition_working_set* working_set);

LAPLACE_API void laplace_composition_working_set_destroy(
    laplace_composition_working_set** working_set);

#ifdef __cplusplus
}
#endif

#endif
