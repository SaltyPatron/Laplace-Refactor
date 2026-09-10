#ifndef LAPLACE_UCDXML_ESTATE_H
#define LAPLACE_UCDXML_ESTATE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/ucdxml_evidence.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_UCDXML_ESTATE_VERSION = 1
};

typedef int (*laplace_ucdxml_estate_batch_sink_fn)(
    void* sink_state,
    const laplace_persistence_attestation_record* occurrences,
    size_t occurrence_count,
    const laplace_evidence_lineage_record* lineage_records,
    size_t lineage_count,
    const laplace_evidence_root_record* root_relations,
    size_t root_count,
    const laplace_evidence_testimony_record* testimonies,
    size_t testimony_count,
    const laplace_ucdxml_evidence_batch_receipt* batch_receipt);

typedef struct laplace_ucdxml_estate_input {
    const laplace_ucdxml_projection* projection;
    const laplace_decomposition_content* source_content;
    laplace_text_identity source_root_identity;
    laplace_digest256 source_fingerprint;
    laplace_digest256 source_profile_id;
    laplace_digest256 evidence_recipe_receipt_id;
    laplace_digest256 trust_input_id;
    laplace_digest256 context_fingerprint;
    uint64_t source_root_ordinal;
    uint64_t lineage_memory_limit_bytes;
    size_t property_batch_capacity;
    laplace_ucdxml_estate_batch_sink_fn sink;
    void* sink_state;
    uint32_t source_type;
    uint32_t flags;
} laplace_ucdxml_estate_input;

typedef struct laplace_ucdxml_estate_receipt {
    laplace_digest256 receipt_id;
    laplace_digest256 source_profile_id;
    laplace_digest256 source_root_node_id;
    laplace_digest256 lineage_batches_fingerprint;
    laplace_digest256 testimony_batches_fingerprint;
    uint64_t covered_position_count;
    uint64_t property_count;
    uint64_t batch_count;
    uint64_t occurrence_count;
    uint64_t lineage_record_count;
    uint64_t root_relation_count;
    uint64_t testimony_count;
    uint32_t version;
    uint32_t status;
} laplace_ucdxml_estate_receipt;

typedef enum laplace_ucdxml_estate_status {
    LAPLACE_UCDXML_ESTATE_OK = 0,
    LAPLACE_UCDXML_ESTATE_INVALID_ARGUMENT = 1,
    LAPLACE_UCDXML_ESTATE_PROJECTION_FAILURE = 2,
    LAPLACE_UCDXML_ESTATE_EVIDENCE_FAILURE = 3,
    LAPLACE_UCDXML_ESTATE_ROOT_DRIFT = 4,
    LAPLACE_UCDXML_ESTATE_SINK_FAILURE = 5,
    LAPLACE_UCDXML_ESTATE_OVERFLOW = 6,
    LAPLACE_UCDXML_ESTATE_MEMORY_FAILURE = 7,
    LAPLACE_UCDXML_ESTATE_EMPTY = 8
} laplace_ucdxml_estate_status;

/*
 * Walk the complete Unicode position universe through one already-compiled UAX
 * #42 projection and close effective properties in bounded batches.  Each batch
 * is delegated to laplace_ucdxml_evidence_emit_batch, so all batches share the
 * same canonical source/release dependence root while never materializing the
 * full evidence estate in memory.
 *
 * A sink is optional. When present it receives each fully validated canonical
 * batch in deterministic position/property order and may persist it. The sink is
 * responsible for transaction-wide rollback if later batches fail; all native
 * batch outputs remain individually replay-safe and receipted.
 */
LAPLACE_API laplace_ucdxml_estate_status laplace_ucdxml_estate_execute(
    const laplace_ucdxml_estate_input* input,
    laplace_ucdxml_estate_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
