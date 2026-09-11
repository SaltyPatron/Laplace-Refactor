#ifndef LAPLACE_CONTENT_ADMISSION_H
#define LAPLACE_CONTENT_ADMISSION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "laplace/decomposition.h"
#include "laplace/decomposition_composition.h"
#include "laplace/export.h"
#include "laplace/framework.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_CONTENT_ADMISSION_VERSION = 1,
    LAPLACE_CONTENT_ATOM_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_CONTENT_ATOM_PROVIDER_ABI_MINOR = 0
};

typedef struct laplace_content_admission laplace_content_admission;

typedef enum laplace_content_admission_status {
    LAPLACE_CONTENT_ADMISSION_OK = 0,
    LAPLACE_CONTENT_ADMISSION_INVALID_ARGUMENT = 1,
    LAPLACE_CONTENT_ADMISSION_INVALID_VERSION = 2,
    LAPLACE_CONTENT_ADMISSION_DECOMPOSITION_FAILURE = 3,
    LAPLACE_CONTENT_ADMISSION_COMPOSITION_PLAN_FAILURE = 4,
    LAPLACE_CONTENT_ADMISSION_ATOM_PROVIDER_FAILURE = 5,
    LAPLACE_CONTENT_ADMISSION_ATOM_PROVIDER_INVALID = 6,
    LAPLACE_CONTENT_ADMISSION_COMPOSITION_FAILURE = 7,
    LAPLACE_CONTENT_ADMISSION_PRESENCE_FAILURE = 8,
    LAPLACE_CONTENT_ADMISSION_ROOT_INVALID = 9,
    LAPLACE_CONTENT_ADMISSION_MEMORY_FAILURE = 10,
    LAPLACE_CONTENT_ADMISSION_NO_PUBLICATION_REQUIRED = 11
} laplace_content_admission_status;

/* Resolve canonical Unicode-floor positions emitted by decomposition into the
 * already-admitted Tier-0 entities owned by the selected substrate.  The
 * admission engine is deliberately independent of PostgreSQL, Unicode source
 * files, perfcache implementation, and any one source family. */
typedef int (*laplace_content_atom_resolve_fn)(
    void* provider_state,
    const uint32_t* atom_positions,
    size_t atom_count,
    laplace_composition_known_entity* known_entities,
    laplace_digest256* provider_receipt_id);

typedef struct laplace_content_atom_provider_v1 {
    void* state;
    laplace_digest256 provider_fingerprint;
    laplace_content_atom_resolve_fn resolve;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_content_atom_provider_v1;

typedef struct laplace_content_admission_input {
    /* Exact source bytes and a recipe-selected provider set.  No source family
     * is implicit in this boundary. */
    laplace_decomposition_input decomposition;
    laplace_decomposition_media_type_resolver_fn media_resolver;
    void* media_resolver_state;

    const laplace_framework_context* framework_context;
    laplace_digest256 source_fingerprint;
    laplace_digest256 content_recipe_fingerprint;
    laplace_digest256 calculation_recipe_fingerprint;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context_fingerprint;
    uint64_t source_ordinal_base;
    uint64_t preferred_batch_bytes;
    uint32_t version;
    uint32_t reserved;
} laplace_content_admission_input;

typedef struct laplace_content_admission_view {
    laplace_digest256 admission_receipt_id;
    laplace_digest256 exact_bytes_fingerprint;
    laplace_digest256 decomposition_trace_fingerprint;
    laplace_digest256 atom_provider_fingerprint;
    laplace_digest256 atom_provider_receipt_id;
    laplace_digest256 presence_receipt_id;
    laplace_id128 root_entity_id;
    laplace_digest256 root_identity_witness;
    laplace_digest256 root_physicality_id;
    laplace_decomposition_summary decomposition_summary;
    laplace_composition_working_set_summary composition_summary;
    uint64_t atom_count;
    uint64_t request_count;
    uint32_t version;
    uint32_t status;
} laplace_content_admission_view;

/* Common exact-content admission primitive used by source, prompt, document,
 * media and future recipe adapters.  It owns no source semantics: providers
 * decompose exact bytes, the canonical composition bridge determines content
 * identity, the supplied atom provider resolves the substrate floor, and the
 * supplied presence provider resolves durable canonical state. */
LAPLACE_API laplace_content_admission_status laplace_content_admission_create(
    const laplace_content_admission_input* input,
    const laplace_content_atom_provider_v1* atom_provider,
    const laplace_composition_presence_provider_v1* presence_provider,
    laplace_content_admission** admission);

LAPLACE_API laplace_content_admission_status laplace_content_admission_view_get(
    const laplace_content_admission* admission,
    laplace_content_admission_view* view);

/* Expose the exact canonical stream for the host persistence layer.  A single
 * already-admitted Tier-0 atom legitimately requires no publication. */
LAPLACE_API laplace_content_admission_status laplace_content_admission_producer(
    laplace_content_admission* admission,
    laplace_framework_producer_v1* producer);

/* Read-only recipe/evidence projection surfaces.  These expose the exact
 * decomposition and canonical plan already executed by the admission owner;
 * source adapters must not reparse or remint a parallel syntax/content graph. */
LAPLACE_API const laplace_decomposition_result*
laplace_content_admission_decomposition(const laplace_content_admission* admission);

LAPLACE_API const laplace_decomposition_composition_plan*
laplace_content_admission_composition_plan(const laplace_content_admission* admission);

LAPLACE_API const laplace_composition_working_set*
laplace_content_admission_working_set(const laplace_content_admission* admission);

LAPLACE_API const laplace_composition_known_entity*
laplace_content_admission_known_entities(
    const laplace_content_admission* admission,
    size_t* known_entity_count);

LAPLACE_API void laplace_content_admission_destroy(
    laplace_content_admission** admission);

#ifdef __cplusplus
}
#endif

#endif
