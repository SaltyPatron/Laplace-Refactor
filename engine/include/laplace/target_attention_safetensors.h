#ifndef LAPLACE_TARGET_ATTENTION_SAFETENSORS_H
#define LAPLACE_TARGET_ATTENTION_SAFETENSORS_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/target_attention_projection.h"
#include "laplace/target_compile.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_VERSION = 1
};

typedef enum laplace_target_attention_safetensors_status {
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_OK = 0,
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_ARGUMENT = 1,
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_INVALID_PROJECTION = 2,
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_CAPACITY_INSUFFICIENT = 3,
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_OVERFLOW = 4,
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_CORRUPT = 5,
    LAPLACE_TARGET_ATTENTION_SAFETENSORS_MEMORY_FAILURE = 6
} laplace_target_attention_safetensors_status;

typedef struct laplace_target_attention_safetensors_receipt {
    laplace_digest256 artifact_id;
    laplace_digest256 compile_receipt_id;
    laplace_digest256 compile_request_fingerprint;
    laplace_digest256 projection_id;
    laplace_digest256 embedding_fingerprint;
    laplace_digest256 head_set_fingerprint;
    uint64_t byte_count;
    uint64_t header_byte_count;
    uint64_t data_byte_count;
    uint64_t tensor_count;
    uint64_t head_count;
    uint64_t field_count;
    uint64_t hidden_width;
    double max_relative_residual;
    uint32_t status;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved;
} laplace_target_attention_safetensors_receipt;

/*
 * Encode the already-derived shared embedding and per-head Q/K/V/O weights into
 * one deterministic SafeTensors artifact.  This codec does not choose operators,
 * ranks, hidden width, head schedules, or semantic filler; those decisions have
 * already been made and receipted by target compilation and attention projection.
 *
 * Tensor layout:
 *   laplace.embedding                              [field_count, hidden_width]
 *   laplace.q.layer_L.head_H.expert_E              [head_rank, hidden_width]
 *   laplace.k.layer_L.head_H.expert_E              [head_rank, hidden_width]
 *   laplace.v.layer_L.head_H.expert_E              [head_rank, hidden_width]
 *   laplace.o.layer_L.head_H.expert_E              [hidden_width, head_rank]
 */
LAPLACE_API laplace_target_attention_safetensors_status
laplace_target_attention_safetensors_encode(
    const laplace_target_compile_receipt* compile_receipt,
    const laplace_target_attention_projection_result* projection,
    const laplace_target_attention_projection_receipt* projection_receipt,
    uint8_t* output,
    size_t output_capacity,
    size_t* required_bytes,
    laplace_target_attention_safetensors_receipt* receipt);

/*
 * Recreate the canonical artifact from the same compiled/projection state and
 * reject any byte, header, tensor-order, shape, offset, metadata, or payload drift.
 */
LAPLACE_API laplace_target_attention_safetensors_status
laplace_target_attention_safetensors_validate(
    const laplace_target_compile_receipt* compile_receipt,
    const laplace_target_attention_projection_result* projection,
    const laplace_target_attention_projection_receipt* projection_receipt,
    const uint8_t* artifact,
    size_t artifact_size,
    laplace_target_attention_safetensors_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
