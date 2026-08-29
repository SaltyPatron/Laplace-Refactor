#ifndef LAPLACE_COGNITION_PACKET_COMPILE_H
#define LAPLACE_COGNITION_PACKET_COMPILE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_packet.h"
#include "laplace/cognition_runtime.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compile one typed logical cognition request into the canonical little-endian
 * u32 word transport consumed by the cognition ISA opcode.  This is the shared
 * compiler boundary for PostgreSQL, managed, and future transports; callers do
 * not own or reproduce the packet grammar. */
LAPLACE_API laplace_cognition_packet_status
laplace_cognition_packet_request_required_words(
    const laplace_cognition_runtime_request* request,
    size_t* required_words);

LAPLACE_API laplace_cognition_packet_status
laplace_cognition_packet_encode_request_words(
    const laplace_cognition_runtime_request* request,
    uint32_t* words,
    size_t word_capacity,
    size_t* word_count);

/* Decode the canonical ISA result transport into the typed runtime result. The
 * caller supplies result->solution and result->solution_capacity; receipts and
 * solution_count are populated only after the complete packet validates. */
LAPLACE_API laplace_cognition_packet_status
laplace_cognition_packet_decode_result_words(
    const uint32_t* words,
    size_t word_count,
    laplace_cognition_runtime_result* result);

#ifdef __cplusplus
}
#endif

#endif
