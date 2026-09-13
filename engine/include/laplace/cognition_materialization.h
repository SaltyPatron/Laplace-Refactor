#ifndef LAPLACE_COGNITION_MATERIALIZATION_H
#define LAPLACE_COGNITION_MATERIALIZATION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_realization.h"
#include "laplace/export.h"
#include "laplace/trajectory.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM = UINT32_C(1),
    LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION = UINT32_C(2),
    LAPLACE_COGNITION_MATERIALIZATION_VERSION = 1,
    LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR = 1,
    LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR = 0,
    LAPLACE_COGNITION_MATERIALIZATION_RECEIPT_VERSION = 1
};

typedef enum laplace_cognition_materialization_status {
    LAPLACE_COGNITION_MATERIALIZATION_OK = 0,
    LAPLACE_COGNITION_MATERIALIZATION_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_MATERIALIZATION_INVALID_REALIZATION = 2,
    LAPLACE_COGNITION_MATERIALIZATION_INVALID_PROVIDER = 3,
    LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_FAILURE = 4,
    LAPLACE_COGNITION_MATERIALIZATION_NODE_INVALID = 5,
    LAPLACE_COGNITION_MATERIALIZATION_IDENTITY_MISMATCH = 6,
    LAPLACE_COGNITION_MATERIALIZATION_TRAJECTORY_INVALID = 7,
    LAPLACE_COGNITION_MATERIALIZATION_CYCLE = 8,
    LAPLACE_COGNITION_MATERIALIZATION_LIMIT = 9,
    LAPLACE_COGNITION_MATERIALIZATION_CAPACITY = 10,
    LAPLACE_COGNITION_MATERIALIZATION_MEMORY_FAILURE = 11,
    LAPLACE_COGNITION_MATERIALIZATION_ENCODING_INVALID = 12,
    LAPLACE_COGNITION_MATERIALIZATION_ENCODING_RANGE = 13
} laplace_cognition_materialization_status;

/*
 * Exact immutable content descriptor supplied by a persistence provider. Atom
 * nodes carry one Unicode codepoint position. Composition nodes carry the exact
 * ordered trajectory identity needed for recursive readback. The native reader
 * validates the full content witness before any bytes are published.
 */
typedef struct laplace_cognition_materialization_node {
    laplace_id128 entity_id;
    laplace_digest256 identity_witness;
    laplace_digest256 physicality_id;
    laplace_digest256 trajectory_fingerprint;
    laplace_digest256 node_receipt_id;
    uint64_t logical_count;
    uint64_t carrier_count;
    uint32_t atom;
    uint32_t kind;
    uint8_t tier_floor;
    uint8_t reserved8[7];
} laplace_cognition_materialization_node;

typedef struct laplace_cognition_materialization_request {
    uint64_t maximum_nodes;
    uint64_t maximum_trajectory_carriers;
    uint64_t maximum_output_bytes;
    uint32_t maximum_depth;
    uint32_t version;
} laplace_cognition_materialization_request;

typedef int (*laplace_cognition_materialization_resolve_node_fn)(
    void* provider_state,
    const laplace_id128* entity_id,
    laplace_cognition_materialization_node* node);

typedef int (*laplace_cognition_materialization_read_trajectory_fn)(
    void* provider_state,
    const laplace_cognition_materialization_node* node,
    laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_digest256* read_receipt_id);

typedef struct laplace_cognition_materialization_provider_v1 {
    void* state;
    laplace_digest256 provider_fingerprint;
    laplace_cognition_materialization_resolve_node_fn resolve_node;
    laplace_cognition_materialization_read_trajectory_fn read_trajectory;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved;
} laplace_cognition_materialization_provider_v1;

typedef struct laplace_cognition_materialization_receipt {
    laplace_digest256 materialization_id;
    laplace_digest256 source_candidate_receipt_id;
    laplace_digest256 source_recipe_id;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 readset_fingerprint;
    laplace_digest256 output_fingerprint;
    laplace_id128 root_content_id;
    uint64_t resolved_node_count;
    uint64_t trajectory_carrier_count;
    uint64_t codepoint_count;
    uint64_t output_bytes;
    uint32_t maximum_depth_observed;
    uint32_t version;
} laplace_cognition_materialization_receipt;

/*
 * Reconstructs one completed realization to exact ordered Unicode-position UTF-8
 * bytes. Composite identities and full witnesses are recalculated from provider
 * trajectories before descent; atom identities are recalculated from codepoint
 * positions. No token vocabulary, English pivot, punctuation rule, or generated
 * surface text participates in this readback.
 * Within one immutable provider execution, a fully verified canonical subtree
 * is read once and subsequent occurrences reuse its exact output slice. Node
 * and carrier limits/counts measure distinct provider reads; output and depth
 * limits still apply to every occurrence, including reused subtrees. Reuse does
 * not persist across calls or replace occurrence tier validation.
 *
 * The caller buffer is written only after the complete root has validated. A
 * corrupt trajectory, identity mismatch, cycle, provider defect, or finite-limit
 * exhaustion therefore publishes zero output bytes.
 */
LAPLACE_API laplace_cognition_materialization_status
laplace_cognition_realization_materialize_utf8(
    const laplace_cognition_realization_result* realization,
    const laplace_cognition_materialization_request* request,
    const laplace_cognition_materialization_provider_v1* provider,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes,
    laplace_cognition_materialization_receipt* receipt);

/* Explicit output serialization chosen by the admitted realization recipe.
 * OCTETS requires byte-valued Unicode positions (0..255); it neither invents a
 * byte identity law nor selects a modality from prompt text. Unsupported values
 * are rejected rather than truncated or silently re-encoded as UTF-8.
 *
 * Both encodings use the same identity, trajectory, provider and atomic-publication
 * implementation. UTF8 retains its existing output and receipt fingerprints.
 * OCTETS uses a distinct output-fingerprint domain, so even identical ASCII
 * outputs cannot conceal a change of serialization selection in the receipt.
 */
enum {
    LAPLACE_COGNITION_OUTPUT_UTF8 = UINT32_C(0),
    LAPLACE_COGNITION_OUTPUT_OCTETS = UINT32_C(1)
};

LAPLACE_API laplace_cognition_materialization_status
laplace_cognition_realization_materialize_encoded(
    const laplace_cognition_realization_result* realization,
    const laplace_cognition_materialization_request* request,
    const laplace_cognition_materialization_provider_v1* provider,
    uint32_t output_encoding,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_bytes,
    laplace_cognition_materialization_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
