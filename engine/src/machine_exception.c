#include "laplace/machine_exception.h"

#include <string.h>

#define LAPLACE_MACHINE_DESCRIPTOR(name) \
    { \
        LAPLACE_MACHINE_CONDITION_##name, \
        LAPLACE_MACHINE_KIND_##name, \
        LAPLACE_MACHINE_PRIORITY_##name, \
        LAPLACE_MACHINE_CAPABILITIES_##name, \
        LAPLACE_MACHINE_RECOVERY_##name, \
        LAPLACE_MACHINE_PUBLICATION_##name \
    }

static const laplace_machine_exception_descriptor k_descriptors[] = {
    LAPLACE_MACHINE_DESCRIPTOR(NONE),
    LAPLACE_MACHINE_DESCRIPTOR(INVALID_INSTRUCTION),
    LAPLACE_MACHINE_DESCRIPTOR(INVALID_OPERAND),
    LAPLACE_MACHINE_DESCRIPTOR(IMPLEMENTATION_FAULT),
    LAPLACE_MACHINE_DESCRIPTOR(DURABILITY_FAULT),
    LAPLACE_MACHINE_DESCRIPTOR(STORAGE_FAULT),
#ifdef LAPLACE_TEST_MACHINE_EXCEPTION_COLLAPSE_HARDWARE_TO_UNKNOWN
    {
        LAPLACE_MACHINE_CONDITION_UNKNOWN,
        LAPLACE_MACHINE_KIND_HARDWARE_FAULT,
        LAPLACE_MACHINE_PRIORITY_HARDWARE_FAULT,
        LAPLACE_MACHINE_CAPABILITIES_HARDWARE_FAULT,
        LAPLACE_MACHINE_RECOVERY_HARDWARE_FAULT,
        LAPLACE_MACHINE_PUBLICATION_HARDWARE_FAULT
    },
#else
    LAPLACE_MACHINE_DESCRIPTOR(HARDWARE_FAULT),
#endif
    LAPLACE_MACHINE_DESCRIPTOR(CONSISTENCY_FAULT),
    LAPLACE_MACHINE_DESCRIPTOR(NETWORK_FAULT),
    LAPLACE_MACHINE_DESCRIPTOR(PROVIDER_UNAVAILABLE),
    LAPLACE_MACHINE_DESCRIPTOR(AUTHORITY_DENIED),
    LAPLACE_MACHINE_DESCRIPTOR(CANCELLED),
    LAPLACE_MACHINE_DESCRIPTOR(DEADLINE_EXCEEDED),
    LAPLACE_MACHINE_DESCRIPTOR(RESOURCE_EXHAUSTED),
    LAPLACE_MACHINE_DESCRIPTOR(SEMANTIC_CONTRADICTION),
    LAPLACE_MACHINE_DESCRIPTOR(INCOMPLETE_BOUNDARY),
    LAPLACE_MACHINE_DESCRIPTOR(UNSUPPORTED_OPERATION),
    LAPLACE_MACHINE_DESCRIPTOR(PARTIAL_RESULT),
    LAPLACE_MACHINE_DESCRIPTOR(KNOWN_UPPER_BOUND),
    LAPLACE_MACHINE_DESCRIPTOR(UNKNOWN)
};

#undef LAPLACE_MACHINE_DESCRIPTOR

size_t laplace_machine_exception_descriptor_count(void) {
    return sizeof(k_descriptors) / sizeof(k_descriptors[0]);
}

const laplace_machine_exception_descriptor*
laplace_machine_exception_descriptors(void) {
    return k_descriptors;
}

const laplace_machine_exception_descriptor*
laplace_machine_exception_find(uint32_t condition) {
    size_t index = 0;
    for (index = 0; index < laplace_machine_exception_descriptor_count(); ++index) {
        if (k_descriptors[index].condition == condition) {
            return &k_descriptors[index];
        }
    }
    return NULL;
}

laplace_machine_exception_status laplace_machine_exception_registry_validate(void) {
    const size_t count = laplace_machine_exception_descriptor_count();
    size_t outer = 0;
    size_t inner = 0;

    if (count != (size_t)LAPLACE_MACHINE_CONDITION_COUNT) {
        return LAPLACE_MACHINE_EXCEPTION_REGISTRY_INVALID;
    }
    if (k_descriptors[0].condition != LAPLACE_MACHINE_CONDITION_NONE) {
        return LAPLACE_MACHINE_EXCEPTION_REGISTRY_INVALID;
    }
    for (outer = 0; outer < count; ++outer) {
        const laplace_machine_exception_descriptor* descriptor = &k_descriptors[outer];
        if (descriptor->condition >= 64u ||
            descriptor->kind > LAPLACE_MACHINE_KIND_TERMINAL_DISPOSITION ||
            (descriptor->capability_flags & ~LAPLACE_MACHINE_CAPABILITY_KNOWN_MASK) != 0u ||
            descriptor->recovery_disposition > LAPLACE_MACHINE_RECOVERY_TYPED_LIMIT ||
            descriptor->publication_disposition > LAPLACE_MACHINE_PUBLICATION_UPPER_BOUND) {
            return LAPLACE_MACHINE_EXCEPTION_REGISTRY_INVALID;
        }
        for (inner = outer + 1u; inner < count; ++inner) {
            if (descriptor->condition == k_descriptors[inner].condition) {
                return LAPLACE_MACHINE_EXCEPTION_REGISTRY_INVALID;
            }
            if (descriptor->condition != LAPLACE_MACHINE_CONDITION_NONE &&
                descriptor->priority == k_descriptors[inner].priority) {
                return LAPLACE_MACHINE_EXCEPTION_REGISTRY_INVALID;
            }
        }
    }
    return LAPLACE_MACHINE_EXCEPTION_OK;
}

laplace_machine_exception_status laplace_machine_exception_classify(
    const uint32_t* observed_conditions,
    size_t observed_condition_count,
    const laplace_machine_exception_binding* binding,
    laplace_machine_exception_receipt* receipt) {
    const laplace_machine_exception_descriptor* selected = NULL;
    size_t index = 0;
    uint64_t observed_mask = 0;

    if (observed_conditions == NULL || observed_condition_count == 0u ||
        binding == NULL || receipt == NULL ||
        (binding->presence_mask & ~LAPLACE_MACHINE_EXCEPTION_BIND_KNOWN_MASK) != 0u) {
        return LAPLACE_MACHINE_EXCEPTION_INVALID_ARGUMENT;
    }
    if (laplace_machine_exception_registry_validate() != LAPLACE_MACHINE_EXCEPTION_OK) {
        return LAPLACE_MACHINE_EXCEPTION_REGISTRY_INVALID;
    }

    for (index = 0; index < observed_condition_count; ++index) {
        const uint32_t condition = observed_conditions[index];
        const laplace_machine_exception_descriptor* descriptor = NULL;
        if (condition == LAPLACE_MACHINE_CONDITION_NONE || condition >= 64u) {
            return LAPLACE_MACHINE_EXCEPTION_UNKNOWN_CONDITION;
        }
        descriptor = laplace_machine_exception_find(condition);
        if (descriptor == NULL) {
            return LAPLACE_MACHINE_EXCEPTION_UNKNOWN_CONDITION;
        }
        observed_mask |= UINT64_C(1) << condition;
        if (selected == NULL || descriptor->priority < selected->priority) {
            selected = descriptor;
        }
    }

    if (selected == NULL) {
        return LAPLACE_MACHINE_EXCEPTION_UNKNOWN_CONDITION;
    }
    memset(receipt, 0, sizeof(*receipt));
    receipt->selected = *selected;
    receipt->binding = *binding;
    receipt->observed_condition_mask = observed_mask;
    return LAPLACE_MACHINE_EXCEPTION_OK;
}

laplace_machine_exception_status laplace_machine_exception_why_not(
    const laplace_machine_exception_receipt* receipt,
    uint64_t completed_work_units,
    uint64_t open_obligation_count,
    const laplace_digest256* open_obligations_fingerprint,
    const laplace_digest256* continuation_condition_fingerprint,
    laplace_machine_why_not* why_not) {
    const laplace_machine_exception_descriptor* descriptor = NULL;

    if (receipt == NULL || open_obligations_fingerprint == NULL ||
        continuation_condition_fingerprint == NULL || why_not == NULL ||
        receipt->selected.condition == LAPLACE_MACHINE_CONDITION_NONE) {
        return LAPLACE_MACHINE_EXCEPTION_INVALID_ARGUMENT;
    }
    descriptor = laplace_machine_exception_find(receipt->selected.condition);
    if (descriptor == NULL ||
        descriptor->kind != receipt->selected.kind ||
        descriptor->priority != receipt->selected.priority ||
        descriptor->capability_flags != receipt->selected.capability_flags ||
        descriptor->recovery_disposition != receipt->selected.recovery_disposition ||
        descriptor->publication_disposition != receipt->selected.publication_disposition) {
        return LAPLACE_MACHINE_EXCEPTION_INVALID_ARGUMENT;
    }

    memset(why_not, 0, sizeof(*why_not));
    why_not->open_obligations_fingerprint = *open_obligations_fingerprint;
    why_not->continuation_condition_fingerprint = *continuation_condition_fingerprint;
    why_not->completed_work_units = completed_work_units;
    why_not->open_obligation_count = open_obligation_count;
    why_not->limiting_condition = descriptor->condition;
    why_not->recovery_disposition = descriptor->recovery_disposition;
    return LAPLACE_MACHINE_EXCEPTION_OK;
}
