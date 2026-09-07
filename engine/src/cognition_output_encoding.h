#ifndef LAPLACE_COGNITION_OUTPUT_ENCODING_INTERNAL_H
#define LAPLACE_COGNITION_OUTPUT_ENCODING_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* Serialization only: canonical identity and the admitted recipe stay unchanged.
 * The caller selects the encoding before the read starts. Content never selects
 * a mode, and positions outside the octet range are never truncated. */
static inline int laplace_cognition_encode_octet_position(
    uint32_t position, uint8_t output[4], size_t* output_bytes) {
    if (output_bytes != NULL) *output_bytes = 0;
    if (output == NULL || output_bytes == NULL) {
        return 0;
    }
#if !defined(LAPLACE_TEST_OUTPUT_TRUNCATE)
    if (position > UINT32_C(255)) return 0;
#endif
    output[0] = (uint8_t)position;
    *output_bytes = 1;
    return 1;
}

#endif
