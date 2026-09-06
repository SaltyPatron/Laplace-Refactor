#ifndef LAPLACE_TEST_CONTEXT_FIXTURE_H
#define LAPLACE_TEST_CONTEXT_FIXTURE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "laplace/framework.h"

#if defined(LAPLACE_TEST_STANDING_ISOLATED_MUTANT)
#include "laplace/universal_ast.h"

/*
 * The standing-order ISA mutation probe links a private isa.c. It exercises only
 * standing dispatch and intentionally does not link the production universal-AST
 * packet runtime, because doing so would also risk resolving back through the
 * production ISA path. Keep the unrelated packet opcode inert in this isolated
 * mutant exactly as isa_tests.cpp already does for cognition packet dispatch.
 */
extern "C" laplace_universal_ast_status
laplace_universal_ast_packet_validate_words(
    const uint32_t*, size_t, laplace_universal_ast_packet_receipt*) {
    return LAPLACE_UNIVERSAL_AST_INVALID_ARGUMENT;
}
#endif

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
