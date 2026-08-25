#include "laplace/identity.h"

#include <limits.h>
#include <string.h>

#include "blake3.h"

_Static_assert(sizeof(laplace_id128) == LAPLACE_IDENTITY_BYTES,
               "Laplace identity ABI must be exactly 16 bytes");

enum {
    LAPLACE_RUN_BLOCK_IDENTITIES = 4096
};

static void finalize_id(blake3_hasher* hasher, laplace_id128* out_id) {
    uint8_t digest[32];
    blake3_hasher_finalize(hasher, digest, sizeof(digest));
    memcpy(out_id->bytes, digest, LAPLACE_IDENTITY_BYTES);
}

static uint8_t composite_domain(void) {
#if defined(LAPLACE_TEST_MUTATE_COMPOSITE_DOMAIN)
    return (uint8_t)(LAPLACE_COMPOSITE_DOMAIN_BYTE + 1u);
#else
    return (uint8_t)LAPLACE_COMPOSITE_DOMAIN_BYTE;
#endif
}

laplace_identity_status laplace_unicode_position_encode(
    uint32_t position,
    uint8_t out_bytes[4],
    size_t* out_length) {
    if (out_bytes == NULL || out_length == NULL) {
        return LAPLACE_IDENTITY_INVALID_ARGUMENT;
    }
    if (position > LAPLACE_UNICODE_POSITION_MAXIMUM) {
        return LAPLACE_IDENTITY_POSITION_OUT_OF_RANGE;
    }
#if defined(LAPLACE_TEST_REJECT_SURROGATE_POSITIONS)
    if (position >= UINT32_C(0xd800) && position <= UINT32_C(0xdfff)) {
        return LAPLACE_IDENTITY_POSITION_OUT_OF_RANGE;
    }
#endif

    if (position <= UINT32_C(0x7f)) {
        out_bytes[0] = (uint8_t)position;
        *out_length = 1;
    } else if (position <= UINT32_C(0x7ff)) {
        out_bytes[0] = (uint8_t)(UINT32_C(0xc0) | (position >> 6));
        out_bytes[1] = (uint8_t)(UINT32_C(0x80) | (position & UINT32_C(0x3f)));
        *out_length = 2;
    } else if (position <= UINT32_C(0xffff)) {
        out_bytes[0] = (uint8_t)(UINT32_C(0xe0) | (position >> 12));
        out_bytes[1] = (uint8_t)(UINT32_C(0x80) | ((position >> 6) & UINT32_C(0x3f)));
        out_bytes[2] = (uint8_t)(UINT32_C(0x80) | (position & UINT32_C(0x3f)));
        *out_length = 3;
    } else {
        out_bytes[0] = (uint8_t)(UINT32_C(0xf0) | (position >> 18));
        out_bytes[1] = (uint8_t)(UINT32_C(0x80) | ((position >> 12) & UINT32_C(0x3f)));
        out_bytes[2] = (uint8_t)(UINT32_C(0x80) | ((position >> 6) & UINT32_C(0x3f)));
        out_bytes[3] = (uint8_t)(UINT32_C(0x80) | (position & UINT32_C(0x3f)));
        *out_length = 4;
    }
    return LAPLACE_IDENTITY_OK;
}

laplace_identity_status laplace_identity_codepoint(
    uint32_t position,
    laplace_id128* out_id) {
    laplace_digest256 witness;
    return laplace_identity_codepoint_witness(position, out_id, &witness);
}

laplace_identity_status laplace_identity_codepoint_witness(
    uint32_t position,
    laplace_id128* out_id,
    laplace_digest256* out_witness) {
    uint8_t encoded[4];
    size_t encoded_length = 0;
    blake3_hasher hasher;
    laplace_identity_status status;

    if (out_id == NULL || out_witness == NULL) {
        return LAPLACE_IDENTITY_INVALID_ARGUMENT;
    }
    status = laplace_unicode_position_encode(position, encoded, &encoded_length);
    if (status != LAPLACE_IDENTITY_OK) {
        return status;
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, encoded, encoded_length);
    blake3_hasher_finalize(&hasher, out_witness->bytes, sizeof(out_witness->bytes));
    memcpy(out_id->bytes, out_witness->bytes, sizeof(out_id->bytes));
    return LAPLACE_IDENTITY_OK;
}

laplace_identity_status laplace_identity_codepoint_batch(
    const uint32_t* positions,
    size_t position_count,
    laplace_id128* out_ids) {
    size_t index;

    if ((positions == NULL && position_count != 0) ||
        (out_ids == NULL && position_count != 0)) {
        return LAPLACE_IDENTITY_INVALID_ARGUMENT;
    }
    if (position_count > SIZE_MAX / sizeof(*positions) ||
        position_count > SIZE_MAX / sizeof(*out_ids)) {
        return LAPLACE_IDENTITY_COUNT_OVERFLOW;
    }
    for (index = 0; index < position_count; ++index) {
        if (positions[index] > LAPLACE_UNICODE_POSITION_MAXIMUM) {
            return LAPLACE_IDENTITY_POSITION_OUT_OF_RANGE;
        }
    }
    for (index = 0; index < position_count; ++index) {
        const laplace_identity_status status =
            laplace_identity_codepoint(positions[index], &out_ids[index]);
        if (status != LAPLACE_IDENTITY_OK) {
            return status;
        }
    }
    return LAPLACE_IDENTITY_OK;
}

laplace_identity_status laplace_identity_composite(
    const laplace_id128* child_ids,
    size_t child_count,
    laplace_id128* out_id) {
    const uint8_t domain = composite_domain();
    blake3_hasher hasher;

    if (out_id == NULL || (child_ids == NULL && child_count != 0)) {
        return LAPLACE_IDENTITY_INVALID_ARGUMENT;
    }
    if (child_count == 0) {
        return LAPLACE_IDENTITY_EMPTY_COMPOSITION;
    }
    if (child_count == 1) {
        *out_id = child_ids[0];
        return LAPLACE_IDENTITY_OK;
    }
    if (child_count > SIZE_MAX / sizeof(*child_ids)) {
        return LAPLACE_IDENTITY_COUNT_OVERFLOW;
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, &domain, sizeof(domain));
    blake3_hasher_update(&hasher, child_ids, child_count * sizeof(*child_ids));
    finalize_id(&hasher, out_id);
    return LAPLACE_IDENTITY_OK;
}

laplace_identity_status laplace_identity_composite_runs(
    const laplace_id_run* runs,
    size_t run_count,
    uint64_t* out_logical_count,
    laplace_id128* out_id) {
    const uint8_t domain = composite_domain();
    laplace_id128 block[LAPLACE_RUN_BLOCK_IDENTITIES];
    blake3_hasher hasher;
    uint64_t logical_count = 0;
    size_t run_index;

    if (out_id == NULL || out_logical_count == NULL ||
        (runs == NULL && run_count != 0)) {
        return LAPLACE_IDENTITY_INVALID_ARGUMENT;
    }
    if (run_count == 0) {
        return LAPLACE_IDENTITY_EMPTY_COMPOSITION;
    }

    for (run_index = 0; run_index < run_count; ++run_index) {
        if (runs[run_index].count == 0) {
            return LAPLACE_IDENTITY_ZERO_RUN;
        }
        if (UINT64_MAX - logical_count < runs[run_index].count) {
            return LAPLACE_IDENTITY_COUNT_OVERFLOW;
        }
        logical_count += runs[run_index].count;
    }

    *out_logical_count = logical_count;
    if (logical_count == 1) {
        *out_id = runs[0].id;
        return LAPLACE_IDENTITY_OK;
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, &domain, sizeof(domain));
    for (run_index = 0; run_index < run_count; ++run_index) {
        uint64_t remaining = runs[run_index].count;
        size_t block_index;
        for (block_index = 0; block_index < LAPLACE_RUN_BLOCK_IDENTITIES; ++block_index) {
            block[block_index] = runs[run_index].id;
        }
        while (remaining != 0) {
            const size_t emitted = remaining > LAPLACE_RUN_BLOCK_IDENTITIES
                ? LAPLACE_RUN_BLOCK_IDENTITIES
                : (size_t)remaining;
            blake3_hasher_update(&hasher, block, emitted * sizeof(*block));
            remaining -= (uint64_t)emitted;
        }
    }
    finalize_id(&hasher, out_id);
    return LAPLACE_IDENTITY_OK;
}

int laplace_identity_equal(const laplace_id128* left, const laplace_id128* right) {
    if (left == NULL || right == NULL) {
        return 0;
    }
    return memcmp(left->bytes, right->bytes, LAPLACE_IDENTITY_BYTES) == 0;
}

int laplace_identity_compare(const laplace_id128* left, const laplace_id128* right) {
    if (left == NULL && right == NULL) {
        return 0;
    }
    if (left == NULL) {
        return -1;
    }
    if (right == NULL) {
        return 1;
    }
    return memcmp(left->bytes, right->bytes, LAPLACE_IDENTITY_BYTES);
}
