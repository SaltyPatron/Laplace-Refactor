#ifndef LAPLACE_TARGET_SAFETENSORS_H
#define LAPLACE_TARGET_SAFETENSORS_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/target_package.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_SAFETENSORS_VERSION = 1
};

typedef enum laplace_target_safetensors_status {
    LAPLACE_TARGET_SAFETENSORS_OK = 0,
    LAPLACE_TARGET_SAFETENSORS_INVALID_ARGUMENT = 1,
    LAPLACE_TARGET_SAFETENSORS_INVALID_PACKAGE = 2,
    LAPLACE_TARGET_SAFETENSORS_CAPACITY_INSUFFICIENT = 3,
    LAPLACE_TARGET_SAFETENSORS_OVERFLOW = 4,
    LAPLACE_TARGET_SAFETENSORS_CORRUPT = 5,
    LAPLACE_TARGET_SAFETENSORS_MEMORY_FAILURE = 6
} laplace_target_safetensors_status;

typedef struct laplace_target_safetensors_receipt {
    laplace_digest256 artifact_id;
    laplace_digest256 target_package_id;
    laplace_digest256 compile_receipt_id;
    laplace_digest256 request_fingerprint;
    uint64_t byte_count;
    uint64_t header_byte_count;
    uint64_t data_byte_count;
    uint64_t tensor_count;
    uint64_t slot_count;
    uint32_t status;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved;
} laplace_target_safetensors_receipt;

/*
 * Deterministically project one validated target-neutral package into a
 * SafeTensors F64 container.  The codec preserves the package's operator/slot
 * provenance in metadata; it does not decide architecture semantics or invent
 * missing tensors.  `required_bytes` is always reported for a valid package.
 */
LAPLACE_API laplace_target_safetensors_status laplace_target_safetensors_encode(
    const uint8_t* target_package,
    size_t target_package_size,
    uint8_t* output,
    size_t output_capacity,
    size_t* required_bytes,
    laplace_target_safetensors_receipt* receipt);

/*
 * Validate that an artifact is the exact canonical SafeTensors projection of
 * the supplied target-neutral package.  This is a codec oracle, not a claim of
 * behavioral equivalence in an external model runtime.
 */
LAPLACE_API laplace_target_safetensors_status laplace_target_safetensors_validate(
    const uint8_t* target_package,
    size_t target_package_size,
    const uint8_t* artifact,
    size_t artifact_size,
    laplace_target_safetensors_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
