#include "laplace/evidence_testimony.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake3.h"

static int bytes_zero(const void* value, size_t count) {
    const uint8_t* bytes = (const uint8_t*)value;
    uint8_t aggregate = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0u;
}

static int digest_equal(const laplace_digest256* left, const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void finish_digest(blake3_hasher* hasher, laplace_digest256* digest) {
    blake3_hasher_finalize(hasher, digest->bytes, sizeof(digest->bytes));
}

static uint64_t gcd_u64(uint64_t left, uint64_t right) {
    while (right != 0u) {
        const uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

static int source_type_valid(uint32_t value) {
    return value >= LAPLACE_EVIDENCE_SOURCE_STANDARD &&
        value <= LAPLACE_EVIDENCE_SOURCE_EXTERNAL_PROVIDER;
}

static int outcome_type_valid(uint32_t value) {
    return value >= LAPLACE_EVIDENCE_OUTCOME_ASSERTION &&
        value <= LAPLACE_EVIDENCE_OUTCOME_UNKNOWN_BOUNDARY;
}

static int disposition_valid(uint32_t value) {
    return value >= LAPLACE_EVIDENCE_DISPOSITION_PERSISTED &&
        value <= LAPLACE_EVIDENCE_DISPOSITION_CONTRADICTED;
}

static int uncertainty_valid(uint64_t numerator, uint64_t denominator) {
    if (denominator == 0u || numerator > denominator) {
        return 0;
    }
    if (numerator == 0u) {
        return denominator == 1u;
    }
    return gcd_u64(numerator, denominator) == 1u;
}

static void hash_record(
    blake3_hasher* hasher,
    const laplace_evidence_testimony_record* record,
    int include_identity) {
    if (include_identity) {
        blake3_hasher_update(hasher, record->testimony_id.bytes, 32u);
    }
    blake3_hasher_update(hasher, record->evidence_node_id.bytes, 32u);
    blake3_hasher_update(hasher, record->source_profile_id.bytes, 32u);
    blake3_hasher_update(hasher, record->recipe_receipt_id.bytes, 32u);
    blake3_hasher_update(hasher, record->trust_input_id.bytes, 32u);
    blake3_hasher_update(hasher, record->outcome_detail_id.bytes, 32u);
    hash_u64(hasher, record->uncertainty_numerator);
    hash_u64(hasher, record->uncertainty_denominator);
    hash_u64(hasher, record->sample_count);
    hash_u32(hasher, record->source_type);
    hash_u32(hasher, record->outcome_type);
    hash_u32(hasher, record->disposition);
    hash_u32(hasher, record->flags);
}

laplace_evidence_testimony_status laplace_evidence_testimony_identify(
    const laplace_evidence_testimony_record* testimony,
    laplace_digest256* testimony_id) {
    blake3_hasher hasher;
    if (testimony == NULL || testimony_id == NULL ||
        bytes_zero(&testimony->evidence_node_id, sizeof(testimony->evidence_node_id)) ||
        bytes_zero(&testimony->source_profile_id, sizeof(testimony->source_profile_id)) ||
        bytes_zero(&testimony->recipe_receipt_id, sizeof(testimony->recipe_receipt_id)) ||
        bytes_zero(&testimony->trust_input_id, sizeof(testimony->trust_input_id)) ||
        bytes_zero(&testimony->outcome_detail_id, sizeof(testimony->outcome_detail_id)) ||
        testimony->sample_count == 0u ||
        !source_type_valid(testimony->source_type) ||
        !outcome_type_valid(testimony->outcome_type) ||
        !disposition_valid(testimony->disposition) ||
        testimony->flags != LAPLACE_EVIDENCE_TESTIMONY_FLAGS_NONE) {
        return LAPLACE_EVIDENCE_TESTIMONY_RECORD_INVALID;
    }
    if (!uncertainty_valid(
            testimony->uncertainty_numerator,
            testimony->uncertainty_denominator)) {
        return LAPLACE_EVIDENCE_TESTIMONY_UNCERTAINTY_INVALID;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_EVIDENCE_TESTIMONY_IDENTITY_DOMAIN,
        sizeof(LAPLACE_EVIDENCE_TESTIMONY_IDENTITY_DOMAIN) - 1u);
    hash_record(&hasher, testimony, 0);
    finish_digest(&hasher, testimony_id);
    return LAPLACE_EVIDENCE_TESTIMONY_OK;
}

laplace_evidence_testimony_status laplace_evidence_record_testimony_batch(
    const laplace_evidence_testimony_record* records,
    size_t record_count,
    laplace_evidence_testimony_receipt* receipt,
    laplace_evidence_testimony_error* error) {
    blake3_hasher input_hasher;
    blake3_hasher output_hasher;
    blake3_hasher receipt_hasher;
    uint64_t samples = 0u;
    uint64_t uncertain = 0u;
    uint64_t negative = 0u;
    size_t index;
    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->version = LAPLACE_EVIDENCE_TESTIMONY_VERSION;
    }
    if (error != NULL) {
        error->record_index = UINT64_MAX;
    }
    if (records == NULL || record_count == 0u || receipt == NULL) {
        return LAPLACE_EVIDENCE_TESTIMONY_INVALID_ARGUMENT;
    }
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(
        &input_hasher, LAPLACE_EVIDENCE_TESTIMONY_INPUT_DOMAIN,
        sizeof(LAPLACE_EVIDENCE_TESTIMONY_INPUT_DOMAIN) - 1u);
    hash_u64(&input_hasher, (uint64_t)record_count);
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, LAPLACE_EVIDENCE_TESTIMONY_OUTPUT_DOMAIN,
        sizeof(LAPLACE_EVIDENCE_TESTIMONY_OUTPUT_DOMAIN) - 1u);
    hash_u64(&output_hasher, (uint64_t)record_count);
    for (index = 0u; index < record_count; ++index) {
        laplace_digest256 expected;
        const laplace_evidence_testimony_status identify_status =
            laplace_evidence_testimony_identify(&records[index], &expected);
        if (identify_status != LAPLACE_EVIDENCE_TESTIMONY_OK) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
            }
            return identify_status;
        }
        if (!digest_equal(&expected, &records[index].testimony_id)) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
            }
            return LAPLACE_EVIDENCE_TESTIMONY_IDENTITY_MISMATCH;
        }
        if (index != 0u && memcmp(
                records[index - 1u].testimony_id.bytes,
                records[index].testimony_id.bytes, 32u) >= 0) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
            }
            return LAPLACE_EVIDENCE_TESTIMONY_ORDER_INVALID;
        }
#ifndef LAPLACE_TEST_TESTIMONY_PROFILE_HOMOGENEITY_BYPASS
        if (index != 0u && !digest_equal(
                &records[0].source_profile_id,
                &records[index].source_profile_id)) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
            }
            return LAPLACE_EVIDENCE_TESTIMONY_PROFILE_MISMATCH;
        }
#endif
        if (UINT64_MAX - samples < records[index].sample_count) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
            }
            return LAPLACE_EVIDENCE_TESTIMONY_OVERFLOW;
        }
        samples += records[index].sample_count;
        uncertain += records[index].uncertainty_numerator != 0u ? 1u : 0u;
        switch (records[index].disposition) {
            case LAPLACE_EVIDENCE_DISPOSITION_UNSUPPORTED:
            case LAPLACE_EVIDENCE_DISPOSITION_MALFORMED:
            case LAPLACE_EVIDENCE_DISPOSITION_UNRESOLVED:
            case LAPLACE_EVIDENCE_DISPOSITION_REJECTED:
            case LAPLACE_EVIDENCE_DISPOSITION_CONTRADICTED:
                negative += 1u;
                break;
            default:
                break;
        }
        hash_record(&input_hasher, &records[index], 1);
        blake3_hasher_update(
            &output_hasher, records[index].testimony_id.bytes, 32u);
    }
    receipt->source_profile_id = records[0].source_profile_id;
    finish_digest(&input_hasher, &receipt->input_fingerprint);
    finish_digest(&output_hasher, &receipt->output_fingerprint);
    receipt->testimony_count = (uint64_t)record_count;
    receipt->sample_count = samples;
    receipt->uncertain_count = uncertain;
    receipt->negative_disposition_count = negative;
    receipt->status = LAPLACE_EVIDENCE_TESTIMONY_OK;
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_EVIDENCE_TESTIMONY_RECEIPT_DOMAIN,
        sizeof(LAPLACE_EVIDENCE_TESTIMONY_RECEIPT_DOMAIN) - 1u);
    blake3_hasher_update(&receipt_hasher, receipt->source_profile_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(&receipt_hasher, receipt->testimony_count);
    hash_u64(&receipt_hasher, receipt->sample_count);
    hash_u64(&receipt_hasher, receipt->uncertain_count);
    hash_u64(&receipt_hasher, receipt->negative_disposition_count);
    hash_u32(&receipt_hasher, receipt->version);
    hash_u32(&receipt_hasher, receipt->status);
    finish_digest(&receipt_hasher, &receipt->receipt_id);
    return LAPLACE_EVIDENCE_TESTIMONY_OK;
}
