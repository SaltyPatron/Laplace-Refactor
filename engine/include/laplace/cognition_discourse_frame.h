#ifndef LAPLACE_COGNITION_DISCOURSE_FRAME_H
#define LAPLACE_COGNITION_DISCOURSE_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/cognition_discourse.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_DISCOURSE_FRAME_VERSION = 1,
    LAPLACE_COGNITION_DISCOURSE_FRAME_HEADER_BYTES = 44,
    LAPLACE_COGNITION_DISCOURSE_FRAME_PAYLOAD_BYTES = 608,
    LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES =
        LAPLACE_COGNITION_DISCOURSE_FRAME_HEADER_BYTES +
        LAPLACE_COGNITION_DISCOURSE_FRAME_PAYLOAD_BYTES
};

typedef enum laplace_cognition_discourse_frame_status {
    LAPLACE_COGNITION_DISCOURSE_FRAME_OK = 0,
    LAPLACE_COGNITION_DISCOURSE_FRAME_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_DISCOURSE_FRAME_CAPACITY = 2,
    LAPLACE_COGNITION_DISCOURSE_FRAME_HEADER_INVALID = 3,
    LAPLACE_COGNITION_DISCOURSE_FRAME_CHECKSUM_MISMATCH = 4,
    LAPLACE_COGNITION_DISCOURSE_FRAME_STATE_INVALID = 5,
    LAPLACE_COGNITION_DISCOURSE_FRAME_IDENTITY_MISMATCH = 6
} laplace_cognition_discourse_frame_status;

typedef struct laplace_cognition_discourse_frame_receipt {
    laplace_digest256 frame_fingerprint;
    laplace_digest256 state_id;
    uint64_t frame_bytes;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_discourse_frame_receipt;

/*
 * Canonical persistence/readback frame for an immutable discourse snapshot.
 * The frame contains no transport/session identifiers beyond the native state
 * itself. PostgreSQL, files, replicas and future storage providers can persist
 * these bytes without acquiring discourse semantics.
 */
LAPLACE_API laplace_cognition_discourse_frame_status
laplace_cognition_discourse_frame_encode(
    const laplace_cognition_discourse_state* state,
    uint8_t* bytes,
    size_t capacity,
    size_t* written,
    laplace_cognition_discourse_frame_receipt* receipt);

LAPLACE_API laplace_cognition_discourse_frame_status
laplace_cognition_discourse_frame_decode(
    const uint8_t* bytes,
    size_t byte_count,
    laplace_cognition_discourse_state* state,
    laplace_cognition_discourse_frame_receipt* receipt);

#ifdef __cplusplus
}
#endif

#endif
