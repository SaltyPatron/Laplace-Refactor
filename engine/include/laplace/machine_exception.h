#ifndef LAPLACE_MACHINE_EXCEPTION_H
#define LAPLACE_MACHINE_EXCEPTION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/framework.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum laplace_machine_exception_status {
    LAPLACE_MACHINE_EXCEPTION_OK = 0,
    LAPLACE_MACHINE_EXCEPTION_INVALID_ARGUMENT = 1,
    LAPLACE_MACHINE_EXCEPTION_UNKNOWN_CONDITION = 2,
    LAPLACE_MACHINE_EXCEPTION_REGISTRY_INVALID = 3
} laplace_machine_exception_status;

typedef struct laplace_machine_exception_descriptor {
    uint32_t condition;
    uint32_t kind;
    uint32_t priority;
    uint32_t capability_flags;
    uint32_t recovery_disposition;
    uint32_t publication_disposition;
} laplace_machine_exception_descriptor;

enum {
    LAPLACE_MACHINE_EXCEPTION_BIND_PROGRAM = UINT64_C(1) << 0,
    LAPLACE_MACHINE_EXCEPTION_BIND_PHYSICAL_PLAN = UINT64_C(1) << 1,
    LAPLACE_MACHINE_EXCEPTION_BIND_PROVIDER = UINT64_C(1) << 2,
    LAPLACE_MACHINE_EXCEPTION_BIND_NODE = UINT64_C(1) << 3,
    LAPLACE_MACHINE_EXCEPTION_BIND_WORLD_SCOPE = UINT64_C(1) << 4,
    LAPLACE_MACHINE_EXCEPTION_BIND_TRANSACTION = UINT64_C(1) << 5,
    LAPLACE_MACHINE_EXCEPTION_BIND_LAST_VALID_RECEIPT = UINT64_C(1) << 6,
    LAPLACE_MACHINE_EXCEPTION_BIND_DURABLE_BOUNDARY = UINT64_C(1) << 7,
    LAPLACE_MACHINE_EXCEPTION_BIND_REPLAY_ORIGIN = UINT64_C(1) << 8,
    LAPLACE_MACHINE_EXCEPTION_BIND_INVALIDATED_OUTPUT = UINT64_C(1) << 9
};

#define LAPLACE_MACHINE_EXCEPTION_BIND_KNOWN_MASK \
    (LAPLACE_MACHINE_EXCEPTION_BIND_PROGRAM | \
     LAPLACE_MACHINE_EXCEPTION_BIND_PHYSICAL_PLAN | \
     LAPLACE_MACHINE_EXCEPTION_BIND_PROVIDER | \
     LAPLACE_MACHINE_EXCEPTION_BIND_NODE | \
     LAPLACE_MACHINE_EXCEPTION_BIND_WORLD_SCOPE | \
     LAPLACE_MACHINE_EXCEPTION_BIND_TRANSACTION | \
     LAPLACE_MACHINE_EXCEPTION_BIND_LAST_VALID_RECEIPT | \
     LAPLACE_MACHINE_EXCEPTION_BIND_DURABLE_BOUNDARY | \
     LAPLACE_MACHINE_EXCEPTION_BIND_REPLAY_ORIGIN | \
     LAPLACE_MACHINE_EXCEPTION_BIND_INVALIDATED_OUTPUT)

typedef struct laplace_machine_exception_binding {
    laplace_digest256 program_fingerprint;
    laplace_digest256 physical_plan_fingerprint;
    laplace_digest256 provider_fingerprint;
    laplace_digest256 node_fingerprint;
    laplace_digest256 world_scope_fingerprint;
    laplace_digest256 transaction_fingerprint;
    laplace_digest256 last_valid_receipt_fingerprint;
    laplace_digest256 durable_boundary_fingerprint;
    laplace_digest256 replay_origin_fingerprint;
    laplace_digest256 invalidated_output_fingerprint;
    uint64_t presence_mask;
    uint64_t affected_instruction_index;
    uint64_t invalidated_output_count;
    uint64_t reserved;
} laplace_machine_exception_binding;

typedef struct laplace_machine_exception_receipt {
    laplace_machine_exception_descriptor selected;
    laplace_machine_exception_binding binding;
    uint64_t observed_condition_mask;
    uint64_t reserved;
} laplace_machine_exception_receipt;

typedef struct laplace_machine_why_not {
    laplace_digest256 open_obligations_fingerprint;
    laplace_digest256 continuation_condition_fingerprint;
    uint64_t completed_work_units;
    uint64_t open_obligation_count;
    uint32_t limiting_condition;
    uint32_t recovery_disposition;
} laplace_machine_why_not;

LAPLACE_API size_t laplace_machine_exception_descriptor_count(void);

LAPLACE_API const laplace_machine_exception_descriptor*
laplace_machine_exception_descriptors(void);

LAPLACE_API const laplace_machine_exception_descriptor*
laplace_machine_exception_find(uint32_t condition);

LAPLACE_API laplace_machine_exception_status
laplace_machine_exception_registry_validate(void);

LAPLACE_API laplace_machine_exception_status laplace_machine_exception_classify(
    const uint32_t* observed_conditions,
    size_t observed_condition_count,
    const laplace_machine_exception_binding* binding,
    laplace_machine_exception_receipt* receipt);

LAPLACE_API laplace_machine_exception_status laplace_machine_exception_why_not(
    const laplace_machine_exception_receipt* receipt,
    uint64_t completed_work_units,
    uint64_t open_obligation_count,
    const laplace_digest256* open_obligations_fingerprint,
    const laplace_digest256* continuation_condition_fingerprint,
    laplace_machine_why_not* why_not);

#ifdef __cplusplus
}
#endif

#endif
