#ifndef LAPLACE_COGNITION_PROMPT_ADMISSION_H
#define LAPLACE_COGNITION_PROMPT_ADMISSION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_observation_request.h"
#include "laplace/cognition_turn.h"
#include "laplace/composition.h"
#include "laplace/decomposition.h"
#include "laplace/decomposition_composition.h"
#include "laplace/export.h"
#include "laplace/framework.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION = 1,
    LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MINOR = 0
};

typedef struct laplace_cognition_prompt_admission
    laplace_cognition_prompt_admission;

typedef enum laplace_cognition_prompt_admission_status {
    LAPLACE_COGNITION_PROMPT_ADMISSION_OK = 0,
    LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_PROMPT_ADMISSION_INVALID_VERSION = 2,
    LAPLACE_COGNITION_PROMPT_ADMISSION_DECOMPOSITION_FAILURE = 3,
    LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_PLAN_FAILURE = 4,
    LAPLACE_COGNITION_PROMPT_ADMISSION_ATOM_PROVIDER_FAILURE = 5,
    LAPLACE_COGNITION_PROMPT_ADMISSION_ATOM_PROVIDER_INVALID = 6,
    LAPLACE_COGNITION_PROMPT_ADMISSION_COMPOSITION_FAILURE = 7,
    LAPLACE_COGNITION_PROMPT_ADMISSION_PRESENCE_FAILURE = 8,
    LAPLACE_COGNITION_PROMPT_ADMISSION_ROOT_INVALID = 9,
    LAPLACE_COGNITION_PROMPT_ADMISSION_MEMORY_FAILURE = 10,
    LAPLACE_COGNITION_PROMPT_ADMISSION_NO_PUBLICATION_REQUIRED = 11,
    LAPLACE_COGNITION_PROMPT_ADMISSION_STRUCTURAL_PROVIDER_FAILURE = 12
} laplace_cognition_prompt_admission_status;

/*
 * Resolves the Unicode-floor atom positions emitted by the canonical
 * decomposition->composition bridge to exact already-admitted Tier-0 entities.
 * A live PostgreSQL host may back this with its Unicode root/perfcache; a native
 * embedded host may use another exact provider. The prompt admission owner never
 * invents a second atom mapping.
 */
typedef int (*laplace_cognition_prompt_atom_resolve_fn)(
    void* provider_state,
    const uint32_t* atom_positions,
    size_t atom_count,
    laplace_composition_known_entity* known_entities,
    laplace_digest256* provider_receipt_id);

typedef struct laplace_cognition_prompt_atom_provider_v1 {
    void* state;
    laplace_digest256 provider_fingerprint;
    laplace_cognition_prompt_atom_resolve_fn resolve;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_prompt_atom_provider_v1;

/*
 * Occurrence scope is deliberately separate from content identity. Changing any
 * of these fields may mint a different occurrence receipt but must never change
 * the canonical prompt trunk entity.
 */
typedef struct laplace_cognition_prompt_occurrence_scope {
    laplace_digest256 principal_fingerprint;
    laplace_digest256 session_fingerprint;
    laplace_digest256 discourse_id;
    laplace_digest256 world_id;
    laplace_digest256 time_fingerprint;
    laplace_digest256 context_fingerprint;
    uint64_t turn_ordinal;
    uint32_t turn_flags;
    uint32_t reserved;
} laplace_cognition_prompt_occurrence_scope;

typedef struct laplace_cognition_prompt_admission_input {
    /* Exact source bytes plus the selected Unicode/grammar providers. */
    laplace_decomposition_input decomposition;
    laplace_decomposition_media_type_resolver_fn media_resolver;
    void* media_resolver_state;

    /* Canonical composition execution authority. */
    const laplace_framework_context* framework_context;
    laplace_digest256 source_fingerprint;
    laplace_digest256 content_recipe_fingerprint;
    laplace_digest256 calculation_recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    uint64_t source_ordinal_base;
    uint64_t preferred_batch_bytes;

    laplace_cognition_prompt_occurrence_scope occurrence;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_prompt_admission_input;

typedef struct laplace_cognition_prompt_admission_view {
    laplace_digest256 admission_receipt_id;
    laplace_digest256 exact_bytes_fingerprint;
    laplace_digest256 decomposition_trace_fingerprint;
    laplace_digest256 atom_provider_fingerprint;
    laplace_digest256 atom_provider_receipt_id;
    laplace_digest256 presence_receipt_id;
    laplace_digest256 occurrence_id;
    laplace_id128 trunk_entity_id;
    laplace_digest256 trunk_identity_witness;
    laplace_digest256 trunk_physicality_id;
    laplace_cognition_turn_input turn;
    laplace_decomposition_summary decomposition_summary;
    laplace_composition_working_set_summary composition_summary;
    uint64_t atom_count;
    uint64_t request_count;
    uint64_t semantic_attestation_count;
    uint32_t version;
    uint32_t status;
} laplace_cognition_prompt_admission_view;

/*
 * Admit one prompt as ordinary exact Laplace content.
 *
 * The selected decomposition is lowered through the existing canonical
 * decomposition->composition bridge, Unicode atoms are resolved by the supplied
 * exact atom provider, and composite presence is resolved by the normal
 * composition presence provider. The resulting root is the complete prompt
 * trunk. No token/topic is selected and no semantic attestation is created.
 *
 * The independently scoped occurrence is bound only after the trunk is known;
 * `view.turn.observation_entity_id` is therefore always the whole canonical
 * trunk while `observation_occurrence_id` is the separate occurrence receipt.
 */
LAPLACE_API laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_create(
    const laplace_cognition_prompt_admission_input* input,
    const laplace_cognition_prompt_atom_provider_v1* atom_provider,
    const laplace_composition_presence_provider_v1* presence_provider,
    laplace_cognition_prompt_admission** admission);

LAPLACE_API laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_view_get(
    const laplace_cognition_prompt_admission* admission,
    laplace_cognition_prompt_admission_view* view);

/*
 * Expose the already-owned canonical composition producer for persistence. The
 * stream contains exact entity/physicality/trajectory state and intentionally
 * contains zero prompt semantic attestations. A one-codepoint prompt is already
 * an admitted Unicode-floor entity and therefore has no new publication stream.
 */
LAPLACE_API laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_producer(
    laplace_cognition_prompt_admission* admission,
    laplace_framework_producer_v1* producer);

/*
 * Expose the admitted prompt's exact composition/decomposition graph through the
 * same candidate-only boundary used by persistent structural, semantic and
 * evidence providers. Cognition therefore starts from the whole trunk and may
 * descend through canonical constituents or rise through exact containers before
 * any semantic provider is consulted. The provider emits only structural
 * CONSTITUENT/CONTAINER, PREDECESSOR/SUCCESSOR and COOCCUR crossings on the
 * PHYSICALITY plane through the common packed-trajectory candidate generator.
 * Exact parser-span constituents remain a separately witnessed structural view.
 * Neither view synthesizes semantic relations, guidance, completion or testimony.
 *
 * One immutable index is retained by the admission, independent of publication
 * presence. Repeated descriptor requests and concurrent read batches reuse it;
 * the admission must outlive all its readers. No composition or complete-edge
 * rebuilding occurs on expansion. Canonical candidates retain packed logical
 * ordinals and run multiplicities; parser candidates retain sibling ordinals in
 * their exact parent/child span witness, never search depths.
 *
 * `maximum_candidate_records_per_expansion` is a conservative finite bound on
 * examined/output records per frontier source across all five families, not a
 * promise to allocate that many records. Transient projection is bounded by three
 * candidate-capacity-sized arrays, plus the caller-owned output. Capacity failure
 * publishes no candidate prefix and returns an explicit UNKNOWN boundary. Binding
 * fingerprints are validated and an empty source batch performs no indexed work.
 */
LAPLACE_API laplace_cognition_prompt_admission_status
laplace_cognition_prompt_admission_structural_provider(
    laplace_cognition_prompt_admission* admission,
    laplace_cognition_observation_candidate_provider_v1* provider);

LAPLACE_API void laplace_cognition_prompt_admission_destroy(
    laplace_cognition_prompt_admission** admission);

#ifdef __cplusplus
}
#endif

#endif
