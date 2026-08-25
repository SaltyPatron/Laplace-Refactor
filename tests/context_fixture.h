#ifndef LAPLACE_TEST_CONTEXT_FIXTURE_H
#define LAPLACE_TEST_CONTEXT_FIXTURE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "laplace/framework.h"

static inline laplace_framework_context laplace_test_context(uint8_t variant) {
    laplace_framework_context context;
    size_t epoch;
    size_t byte;

    memset(&context, 0, sizeof(context));
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.flags = LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY;
    context.epoch_mask =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_COUNT) - UINT64_C(1);
    for (epoch = 0; epoch < LAPLACE_FRAMEWORK_EPOCH_COUNT; ++epoch) {
        for (byte = 0; byte < sizeof(context.epochs[epoch].bytes); ++byte) {
            context.epochs[epoch].bytes[byte] =
                (uint8_t)(UINT8_C(1) + variant + (uint8_t)epoch);
        }
    }
    for (byte = 0; byte < sizeof(context.authority_fingerprint.bytes); ++byte) {
        context.authority_fingerprint.bytes[byte] =
            (uint8_t)(UINT8_C(0xa0) + variant);
    }
    context.resource_grant.memory_bytes = UINT64_C(1048576) + variant;
    context.resource_grant.cpu_slots = UINT32_C(4);
    context.resource_grant.io_slots = UINT32_C(1);
    return context;
}

#endif
