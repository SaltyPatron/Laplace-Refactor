#ifndef LAPLACE_COGNITION_PACKET_H
#define LAPLACE_COGNITION_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum laplace_cognition_packet_status {
    LAPLACE_COGNITION_PACKET_OK = 0,
    LAPLACE_COGNITION_PACKET_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_PACKET_INVALID_REQUEST = 2,
    LAPLACE_COGNITION_PACKET_RESULT_CAPACITY = 3,
    LAPLACE_COGNITION_PACKET_MEMORY_FAILURE = 4,
    LAPLACE_COGNITION_PACKET_RUNTIME_FAILURE = 5,
    LAPLACE_COGNITION_PACKET_RANGE = 6
} laplace_cognition_packet_status;

LAPLACE_API laplace_cognition_packet_status
laplace_cognition_packet_required_result_bytes(
    const uint8_t* request_bytes,
    size_t request_byte_count,
    size_t* required_result_bytes);

LAPLACE_API laplace_cognition_packet_status
laplace_cognition_packet_execute(
    const uint8_t* request_bytes,
    size_t request_byte_count,
    uint8_t* result_bytes,
    size_t result_capacity,
    size_t* result_byte_count);

#ifdef __cplusplus
}
#endif

#endif
