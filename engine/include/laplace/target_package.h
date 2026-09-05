#ifndef LAPLACE_TARGET_PACKAGE_H
#define LAPLACE_TARGET_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"
#include "laplace/target_compile.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_TARGET_PACKAGE_VERSION = 1
};

typedef enum laplace_target_package_status {
    LAPLACE_TARGET_PACKAGE_OK = 0,
    LAPLACE_TARGET_PACKAGE_INVALID_ARGUMENT = 1,
    LAPLACE_TARGET_PACKAGE_INVALID_COMPILE_RESULT = 2,
    LAPLACE_TARGET_PACKAGE_CAPACITY_INSUFFICIENT = 3,
    LAPLACE_TARGET_PACKAGE_OVERFLOW = 4,
    LAPLACE_TARGET_PACKAGE_INVALID_FORMAT = 5,
    LAPLACE_TARGET_PACKAGE_CORRUPT = 6,
    LAPLACE_TARGET_PACKAGE_MEMORY_FAILURE = 7
} laplace_target_package_status;

typedef struct laplace_target_package_receipt {
    laplace_digest256 package_id;
    laplace_digest256 compile_receipt_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 evidence_boundary;
    laplace_digest256 evidence_epoch;
    laplace_digest256 recipe_fingerprint;
    laplace_digest256 target_contract_fingerprint;
    uint64_t byte_count;
    uint64_t slot_count;
    uint64_t distinct_operator_count;
    uint64_t matrix_value_count;
    uint64_t rhs_value_count;
    uint32_t flags;
    uint32_t status;
    uint32_t version;
    uint32_t reserved;
} laplace_target_package_receipt;

typedef struct laplace_target_package laplace_target_package;

/*
 * Measure the exact byte length of the deterministic target-neutral package.
 * This validates the compile result/receipt but performs no serialization and
 * no target-runtime conversion.
 */
LAPLACE_API laplace_target_package_status laplace_target_package_measure(
    const laplace_target_compile_result* result,
    const laplace_target_compile_receipt* compile_receipt,
    size_t* required_bytes);

/*
 * Serialize one validated target-neutral compilation exactly.  The package is
 * operator-first state: per-slot provenance plus exact dense matrix/RHS values.
 * It is not GGUF, SafeTensors, or another consumer codec and does not invent
 * architecture semantics absent from the compile result.
 */
LAPLACE_API laplace_target_package_status laplace_target_package_encode(
    const laplace_target_compile_result* result,
    const laplace_target_compile_receipt* compile_receipt,
    uint8_t* output,
    size_t output_capacity,
    laplace_target_package_receipt* receipt);

/*
 * Decode and independently validate package framing, package integrity,
 * compile-receipt aggregates, slot identities, and matrix fingerprints.
 */
LAPLACE_API laplace_target_package_status laplace_target_package_decode(
    const uint8_t* input,
    size_t input_size,
    laplace_target_package** package,
    laplace_target_package_receipt* receipt);

LAPLACE_API void laplace_target_package_destroy(
    laplace_target_package** package);

LAPLACE_API laplace_target_package_status laplace_target_package_compile_receipt(
    const laplace_target_package* package,
    laplace_target_compile_receipt* receipt);

LAPLACE_API size_t laplace_target_package_slot_count(
    const laplace_target_package* package);

LAPLACE_API laplace_target_package_status laplace_target_package_slot_receipt(
    const laplace_target_package* package,
    size_t slot_index,
    laplace_target_compile_slot_receipt* receipt);

LAPLACE_API laplace_target_package_status laplace_target_package_matrix(
    const laplace_target_package* package,
    size_t slot_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

LAPLACE_API laplace_target_package_status laplace_target_package_rhs(
    const laplace_target_package* package,
    size_t slot_index,
    double* output,
    size_t output_capacity,
    size_t* required_count);

#ifdef __cplusplus
}
#endif

#endif
