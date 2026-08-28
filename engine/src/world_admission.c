#include "laplace/world_admission.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake3.h"
#include "laplace/contract/source_profile.h"

_Static_assert(sizeof(laplace_world_admission_record) == 472u,
               "world-admission record ABI must be exactly 472 bytes");
_Static_assert(sizeof(laplace_world_admission_receipt) == 184u,
               "world-admission receipt ABI must be exactly 184 bytes");
_Static_assert(sizeof(laplace_world_admission_error) == 16u,
               "world-admission error ABI must be exactly 16 bytes");

enum world_admission_field {
    WORLD_ADMISSION_FIELD_NONE = 0u,
    WORLD_ADMISSION_FIELD_COMPONENT = 1u,
    WORLD_ADMISSION_FIELD_OCCURRENCE = 2u,
    WORLD_ADMISSION_FIELD_CLAIM = 3u,
    WORLD_ADMISSION_FIELD_PROFILE_BINDING = 4u,
    WORLD_ADMISSION_FIELD_RECIPE_BINDING = 5u,
    WORLD_ADMISSION_FIELD_LINEAGE_BINDING = 6u,
    WORLD_ADMISSION_FIELD_CLOSURE = 7u,
    WORLD_ADMISSION_FIELD_RECONSTRUCTION = 8u,
    WORLD_ADMISSION_FIELD_IDENTITY = 9u,
    WORLD_ADMISSION_FIELD_ORDER = 10u,
    WORLD_ADMISSION_FIELD_BOUNDARY = 11u,
    WORLD_ADMISSION_FIELD_AGGREGATE = 12u
};

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

static void hash_record(
    blake3_hasher* hasher,
    const laplace_world_admission_record* record,
    int include_identity) {
    const laplace_digest256* digests[] = {
        &record->source_profile_id,
        &record->selected_boundary_fingerprint,
        &record->source_profile_receipt_id,
        &record->recipe_receipt_id,
        &record->composition_working_set_receipt_id,
        &record->composition_presence_receipt_id,
        &record->composition_producer_receipt_id,
        &record->composition_stream_receipt_id,
        &record->evidence_lineage_receipt_id,
        &record->evidence_testimony_receipt_id,
        &record->readback_fingerprint};
    size_t index;
    if (include_identity) {
        blake3_hasher_update(hasher, record->admission_id.bytes, 32u);
    }
    for (index = 0u; index < 11u; ++index) {
        blake3_hasher_update(hasher, digests[index]->bytes, 32u);
    }
    hash_u64(hasher, record->profile_occurrence_count);
    hash_u64(hasher, record->composition_occurrence_count);
    hash_u64(hasher, record->profile_claim_count);
    hash_u64(hasher, record->evidence_node_count);
    hash_u64(hasher, record->testimony_count);
    hash_u64(hasher, record->profile_bound_testimony_count);
    hash_u64(hasher, record->recipe_bound_testimony_count);
    hash_u64(hasher, record->lineage_bound_testimony_count);
    hash_u64(hasher, record->closure_subject_count);
    hash_u64(hasher, record->closed_subject_count);
    hash_u32(hasher, record->reconstruction_class);
    hash_u32(hasher, record->flags);
}

static int add_u64(uint64_t* total, uint64_t value) {
    if (UINT64_MAX - *total < value) {
        return 0;
    }
    *total += value;
    return 1;
}

static laplace_world_admission_status validate_record(
    const laplace_world_admission_record* record,
    uint32_t* field) {
    const laplace_digest256* digests[] = {
        &record->source_profile_id,
        &record->selected_boundary_fingerprint,
        &record->source_profile_receipt_id,
        &record->recipe_receipt_id,
        &record->composition_working_set_receipt_id,
        &record->composition_presence_receipt_id,
        &record->composition_producer_receipt_id,
        &record->composition_stream_receipt_id,
        &record->evidence_lineage_receipt_id,
        &record->evidence_testimony_receipt_id,
        &record->readback_fingerprint};
    size_t index;
    for (index = 0u; index < 11u; ++index) {
        if (bytes_zero(digests[index], sizeof(*digests[index]))) {
            *field = WORLD_ADMISSION_FIELD_COMPONENT;
            return LAPLACE_WORLD_ADMISSION_COMPONENT_MISSING;
        }
    }
    if (record->profile_occurrence_count == 0u ||
        record->profile_occurrence_count != record->composition_occurrence_count) {
        *field = WORLD_ADMISSION_FIELD_OCCURRENCE;
        return LAPLACE_WORLD_ADMISSION_OCCURRENCE_MISMATCH;
    }
    if (record->profile_claim_count != record->testimony_count) {
        *field = WORLD_ADMISSION_FIELD_CLAIM;
        return LAPLACE_WORLD_ADMISSION_CLAIM_MISMATCH;
    }
    if (record->profile_bound_testimony_count != record->testimony_count) {
        *field = WORLD_ADMISSION_FIELD_PROFILE_BINDING;
        return LAPLACE_WORLD_ADMISSION_PROFILE_BINDING_MISMATCH;
    }
    if (record->recipe_bound_testimony_count != record->testimony_count) {
        *field = WORLD_ADMISSION_FIELD_RECIPE_BINDING;
        return LAPLACE_WORLD_ADMISSION_RECIPE_BINDING_MISMATCH;
    }
#if !defined(LAPLACE_TEST_WORLD_ADMISSION_LINEAGE_BINDING_BYPASS)
    if (record->lineage_bound_testimony_count != record->testimony_count ||
        (record->testimony_count != 0u && record->evidence_node_count == 0u)) {
        *field = WORLD_ADMISSION_FIELD_LINEAGE_BINDING;
        return LAPLACE_WORLD_ADMISSION_LINEAGE_BINDING_MISMATCH;
    }
#endif
    if (record->closure_subject_count == 0u ||
        record->closed_subject_count != record->closure_subject_count) {
        *field = WORLD_ADMISSION_FIELD_CLOSURE;
        return LAPLACE_WORLD_ADMISSION_CLOSURE_MISMATCH;
    }
    if (record->reconstruction_class < LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT ||
        record->reconstruction_class > LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_NONE) {
        *field = WORLD_ADMISSION_FIELD_RECONSTRUCTION;
        return LAPLACE_WORLD_ADMISSION_RECONSTRUCTION_INVALID;
    }
    if (record->flags != LAPLACE_WORLD_ADMISSION_FLAGS_NONE) {
        return LAPLACE_WORLD_ADMISSION_INVALID_ARGUMENT;
    }
    return LAPLACE_WORLD_ADMISSION_OK;
}

laplace_world_admission_status laplace_world_admission_identify(
    const laplace_world_admission_record* admission,
    laplace_digest256* admission_id) {
    blake3_hasher hasher;
    uint32_t field = WORLD_ADMISSION_FIELD_NONE;
    laplace_world_admission_status status;
    if (admission == NULL || admission_id == NULL) {
        return LAPLACE_WORLD_ADMISSION_INVALID_ARGUMENT;
    }
    status = validate_record(admission, &field);
    if (status != LAPLACE_WORLD_ADMISSION_OK) {
        return status;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_WORLD_ADMISSION_ADMISSION_DOMAIN,
        sizeof(LAPLACE_WORLD_ADMISSION_ADMISSION_DOMAIN) - 1u);
    hash_record(&hasher, admission, 0);
    finish_digest(&hasher, admission_id);
    return LAPLACE_WORLD_ADMISSION_OK;
}

laplace_world_admission_status laplace_world_admission_close_batch(
    const laplace_world_admission_record* admissions,
    size_t admission_count,
    laplace_world_admission_receipt* receipt,
    laplace_world_admission_error* error) {
    blake3_hasher input_hasher;
    blake3_hasher output_hasher;
    blake3_hasher receipt_hasher;
    size_t index;
    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->version = LAPLACE_WORLD_ADMISSION_VERSION;
    }
    if (error != NULL) {
        error->admission_index = UINT64_MAX;
        error->field = WORLD_ADMISSION_FIELD_NONE;
        error->reserved = 0u;
    }
    if (admissions == NULL || admission_count == 0u || receipt == NULL) {
        return LAPLACE_WORLD_ADMISSION_INVALID_ARGUMENT;
    }
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(
        &input_hasher, LAPLACE_WORLD_ADMISSION_INPUT_DOMAIN,
        sizeof(LAPLACE_WORLD_ADMISSION_INPUT_DOMAIN) - 1u);
    hash_u64(&input_hasher, (uint64_t)admission_count);
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, LAPLACE_WORLD_ADMISSION_OUTPUT_DOMAIN,
        sizeof(LAPLACE_WORLD_ADMISSION_OUTPUT_DOMAIN) - 1u);
    hash_u64(&output_hasher, (uint64_t)admission_count);
    for (index = 0u; index < admission_count; ++index) {
        laplace_digest256 expected;
        uint32_t field = WORLD_ADMISSION_FIELD_NONE;
        const laplace_world_admission_status status =
            validate_record(&admissions[index], &field);
        if (status != LAPLACE_WORLD_ADMISSION_OK) {
            if (error != NULL) {
                error->admission_index = (uint64_t)index;
                error->field = field;
            }
            return status;
        }
        if (laplace_world_admission_identify(&admissions[index], &expected) !=
                LAPLACE_WORLD_ADMISSION_OK ||
            !digest_equal(&expected, &admissions[index].admission_id)) {
            if (error != NULL) {
                error->admission_index = (uint64_t)index;
                error->field = WORLD_ADMISSION_FIELD_IDENTITY;
            }
            return LAPLACE_WORLD_ADMISSION_IDENTITY_MISMATCH;
        }
        if (index != 0u && memcmp(
                admissions[index - 1u].admission_id.bytes,
                admissions[index].admission_id.bytes, 32u) >= 0) {
            if (error != NULL) {
                error->admission_index = (uint64_t)index;
                error->field = WORLD_ADMISSION_FIELD_ORDER;
            }
            return LAPLACE_WORLD_ADMISSION_ORDER_INVALID;
        }
        if (index != 0u && !digest_equal(
                &admissions[0].selected_boundary_fingerprint,
                &admissions[index].selected_boundary_fingerprint)) {
            if (error != NULL) {
                error->admission_index = (uint64_t)index;
                error->field = WORLD_ADMISSION_FIELD_BOUNDARY;
            }
            return LAPLACE_WORLD_ADMISSION_BOUNDARY_MISMATCH;
        }
        if (!add_u64(&receipt->occurrence_count, admissions[index].profile_occurrence_count) ||
            !add_u64(&receipt->claim_count, admissions[index].profile_claim_count) ||
            !add_u64(&receipt->evidence_node_count, admissions[index].evidence_node_count) ||
            !add_u64(&receipt->testimony_count, admissions[index].testimony_count) ||
            !add_u64(&receipt->closure_subject_count, admissions[index].closure_subject_count)) {
            if (error != NULL) {
                error->admission_index = (uint64_t)index;
                error->field = WORLD_ADMISSION_FIELD_AGGREGATE;
            }
            return LAPLACE_WORLD_ADMISSION_OVERFLOW;
        }
        hash_record(&input_hasher, &admissions[index], 1);
        blake3_hasher_update(&output_hasher, admissions[index].admission_id.bytes, 32u);
        blake3_hasher_update(&output_hasher, admissions[index].readback_fingerprint.bytes, 32u);
    }
    receipt->selected_boundary_fingerprint = admissions[0].selected_boundary_fingerprint;
    receipt->admission_count = (uint64_t)admission_count;
    receipt->status = LAPLACE_WORLD_ADMISSION_OK;
    finish_digest(&input_hasher, &receipt->input_fingerprint);
    finish_digest(&output_hasher, &receipt->output_fingerprint);
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_WORLD_ADMISSION_RECEIPT_DOMAIN,
        sizeof(LAPLACE_WORLD_ADMISSION_RECEIPT_DOMAIN) - 1u);
    blake3_hasher_update(&receipt_hasher, receipt->selected_boundary_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(&receipt_hasher, receipt->admission_count);
    hash_u64(&receipt_hasher, receipt->occurrence_count);
    hash_u64(&receipt_hasher, receipt->claim_count);
    hash_u64(&receipt_hasher, receipt->evidence_node_count);
    hash_u64(&receipt_hasher, receipt->testimony_count);
    hash_u64(&receipt_hasher, receipt->closure_subject_count);
    hash_u32(&receipt_hasher, receipt->version);
    hash_u32(&receipt_hasher, receipt->status);
    finish_digest(&receipt_hasher, &receipt->receipt_id);
    return LAPLACE_WORLD_ADMISSION_OK;
}
