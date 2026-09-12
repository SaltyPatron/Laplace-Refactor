#ifndef LAPLACE_COGNITION_REALIZATION_H
#define LAPLACE_COGNITION_REALIZATION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_semantic_act.h"
#include "laplace/composition.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT = UINT32_C(1),
    LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT = UINT32_C(2),
    LAPLACE_COGNITION_REALIZATION_KNOWN_REQUEST_FLAGS =
        LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT |
        LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT,

    LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE = UINT32_C(1),
    LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_COMPOSED = UINT32_C(2),
    LAPLACE_COGNITION_REALIZATION_CANDIDATE_STRUCTURAL_FALLBACK = UINT32_C(3),

    LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED = UINT32_C(1),
    LAPLACE_COGNITION_REALIZATION_CANDIDATE_NEW_COMPOSITION = UINT32_C(2),
    LAPLACE_COGNITION_REALIZATION_CANDIDATE_KNOWN_FLAGS =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED |
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_NEW_COMPOSITION,

    LAPLACE_COGNITION_REALIZATION_VERSION = 1,
    LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR = 0,
    LAPLACE_COGNITION_REALIZATION_RECEIPT_VERSION = 1,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_VERSION = 1
};

typedef enum laplace_cognition_realization_status {
    LAPLACE_COGNITION_REALIZATION_OK = 0,
    LAPLACE_COGNITION_REALIZATION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_REALIZATION_INVALID_FLAGS = 2,
    LAPLACE_COGNITION_REALIZATION_INVALID_SEMANTIC_ACT = 3,
    LAPLACE_COGNITION_REALIZATION_INVALID_PROVIDER = 4,
    LAPLACE_COGNITION_REALIZATION_PROVIDER_FAILURE = 5,
    LAPLACE_COGNITION_REALIZATION_UNSUPPORTED = 6,
    LAPLACE_COGNITION_REALIZATION_INCOMPLETE = 7,
    LAPLACE_COGNITION_REALIZATION_AMBIGUOUS = 8,
    LAPLACE_COGNITION_REALIZATION_RANGE = 9
} laplace_cognition_realization_status;

typedef enum laplace_cognition_realization_disposition {
    LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE = 0,
    LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED = 1,
    LAPLACE_COGNITION_REALIZATION_DISPOSITION_INCOMPLETE = 2,
    LAPLACE_COGNITION_REALIZATION_DISPOSITION_AMBIGUOUS = 3
} laplace_cognition_realization_disposition;

typedef struct laplace_cognition_realization_request {
    laplace_id128 modality_id;
    laplace_id128 language_id;
    laplace_id128 register_id;
    laplace_digest256 evidence_epoch;
    laplace_digest256 realization_recipe_epoch;
    laplace_digest256 context_fingerprint;
    uint32_t maximum_candidates;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_realization_request;

typedef struct laplace_cognition_realization_candidate {
    laplace_id128 content_id;
    laplace_id128 language_id;
    laplace_digest256 candidate_receipt_id;
    laplace_digest256 realization_recipe_id;
    laplace_digest256 obligation_fingerprint;
    uint64_t preference_rank;
    uint64_t reused_subtree_count;
    uint64_t generated_composition_count;
    uint32_t structural_tier;
    uint32_t match_class;
    uint32_t missing_obligation_count;
    uint32_t flags;
} laplace_cognition_realization_candidate;

typedef struct laplace_cognition_realization_usage {
    laplace_digest256 provider_receipt_id;
    laplace_digest256 missing_obligation_fingerprint;
    uint64_t rows_examined;
    uint64_t exact_whole_examined;
    uint64_t exact_composed_examined;
    uint64_t structural_fallback_examined;
    uint32_t missing_obligation_count;
    uint32_t disposition;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_realization_usage;

typedef int (*laplace_cognition_realization_enumerate_fn)(
    void* provider_state,
    const laplace_cognition_semantic_act* semantic_act,
    const laplace_cognition_realization_request* request,
    laplace_cognition_realization_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_realization_usage* usage);

typedef struct laplace_cognition_realization_provider_v1 {
    void* state;
    laplace_digest256 provider_fingerprint;
    uint64_t maximum_candidate_records;
    laplace_cognition_realization_enumerate_fn enumerate;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_realization_provider_v1;

typedef struct laplace_cognition_realization_result {
    laplace_id128 content_id;
    laplace_id128 language_id;
    laplace_digest256 candidate_receipt_id;
    laplace_digest256 realization_recipe_id;
    laplace_digest256 missing_obligation_fingerprint;
    uint64_t preference_rank;
    uint64_t reused_subtree_count;
    uint64_t generated_composition_count;
    uint32_t structural_tier;
    uint32_t match_class;
    uint32_t missing_obligation_count;
    uint32_t disposition;
    uint32_t flags;
    uint32_t version;
} laplace_cognition_realization_result;

typedef struct laplace_cognition_realization_receipt {
    laplace_digest256 realization_id;
    laplace_digest256 semantic_act_id;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 provider_receipt_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 result_fingerprint;
    uint64_t candidate_count;
    uint64_t eligible_candidate_count;
    uint32_t disposition;
    uint32_t version;
} laplace_cognition_realization_receipt;

/*
 * Exact recomposition is the native output-construction boundary. A realization
 * recipe selects already-canonical constituents and their typed relationship
 * metadata; this primitive composes them through the same universal composition
 * engine used by admission. It does not select a semantic act, infer language
 * from text, concatenate private strings, or create testimony.
 */
typedef struct laplace_cognition_realization_composition_input {
    const laplace_framework_context* framework_context;
    const laplace_digest256* source_fingerprint;
    const laplace_digest256* calculation_recipe_fingerprint;
    const laplace_composition_known_entity* constituents;
    const uint64_t* relationship_metadata;
    uint64_t constituent_count;
    laplace_digest256 geometry_epoch;
    uint64_t source_ordinal;
    uint64_t preferred_batch_bytes;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_realization_composition_input;

typedef struct laplace_cognition_realization_composition_receipt {
    laplace_digest256 composition_id;
    laplace_digest256 semantic_act_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 plan_fingerprint;
    laplace_digest256 working_set_receipt_id;
    laplace_digest256 presence_receipt_id;
    laplace_digest256 stream_fingerprint;
    laplace_id128 content_id;
    uint64_t constituent_count;
    uint64_t reused_subtree_count;
    uint64_t generated_composition_count;
    uint32_t structural_tier;
    uint32_t flags;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_realization_composition_receipt;

typedef enum laplace_cognition_realization_composition_status {
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_OK = 0,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_INVALID_SEMANTIC_ACT = 2,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_INVALID_REQUEST = 3,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_COMPOSITION_FAILURE = 4,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_PRESENCE_FAILURE = 5,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_RESULT_INVALID = 6,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_PRODUCER_FAILURE = 7,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_MEMORY_FAILURE = 8,
    LAPLACE_COGNITION_REALIZATION_COMPOSITION_RANGE = 9
} laplace_cognition_realization_composition_status;

typedef struct laplace_cognition_realization_composition
    laplace_cognition_realization_composition;

LAPLACE_API laplace_cognition_realization_status
laplace_cognition_semantic_act_realize(
    const laplace_cognition_semantic_act* semantic_act,
    const laplace_cognition_realization_request* request,
    const laplace_cognition_realization_provider_v1* provider,
    laplace_cognition_realization_result* result,
    laplace_cognition_realization_receipt* receipt);

LAPLACE_API laplace_cognition_realization_composition_status
laplace_cognition_realization_composition_create(
    const laplace_cognition_semantic_act* semantic_act,
    const laplace_cognition_realization_request* request,
    const laplace_cognition_realization_composition_input* input,
    const laplace_composition_presence_provider_v1* presence_provider,
    laplace_cognition_realization_composition** composition,
    laplace_cognition_realization_candidate* candidate,
    laplace_cognition_realization_composition_receipt* receipt);

LAPLACE_API laplace_cognition_realization_composition_status
laplace_cognition_realization_composition_producer(
    laplace_cognition_realization_composition* composition,
    laplace_framework_producer_v1* producer);

LAPLACE_API void laplace_cognition_realization_composition_destroy(
    laplace_cognition_realization_composition** composition);

#ifdef __cplusplus
}
#endif

#endif
