#include "laplace/reference_mapping.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "blake3.h"

_Static_assert(sizeof(laplace_reference_mapping_candidate) == 400u,
               "reference mapping candidate ABI must be exactly 400 bytes");
_Static_assert(sizeof(laplace_reference_mapping_record) == 504u,
               "reference mapping record ABI must be exactly 504 bytes");
_Static_assert(sizeof(laplace_reference_mapping_receipt) == 176u,
               "reference mapping receipt ABI must be exactly 176 bytes");

enum mapping_field {
    MAPPING_FIELD_NONE = 0u,
    MAPPING_FIELD_RECORD = 1u,
    MAPPING_FIELD_BOUNDARY = 2u,
    MAPPING_FIELD_OCCURRENCE = 3u,
    MAPPING_FIELD_COORDINATE = 4u
};

typedef struct digest_index {
    laplace_digest256 digest;
    size_t input_index;
} digest_index;

typedef struct coordinate_index {
    laplace_id128 coordinate;
    laplace_digest256 fingerprint;
    size_t input_index;
} coordinate_index;

static int bytes_zero(const void* value, size_t count) {
    const uint8_t* bytes = (const uint8_t*)value;
    uint8_t aggregate = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0u;
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u), (uint8_t)(value >> 24u)};
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

static void finish(blake3_hasher* hasher, laplace_digest256* output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

static int digest_compare(const void* left, const void* right) {
    const digest_index* a = (const digest_index*)left;
    const digest_index* b = (const digest_index*)right;
    const int compared = memcmp(a->digest.bytes, b->digest.bytes, 32u);
    if (compared != 0) return compared;
    if (a->input_index == b->input_index) return 0;
    return a->input_index < b->input_index ? -1 : 1;
}

static int coordinate_compare(const void* left, const void* right) {
    const coordinate_index* a = (const coordinate_index*)left;
    const coordinate_index* b = (const coordinate_index*)right;
    int compared = memcmp(a->coordinate.bytes, b->coordinate.bytes, 16u);
    if (compared != 0) return compared;
    compared = memcmp(a->fingerprint.bytes, b->fingerprint.bytes, 32u);
    if (compared != 0) return compared;
    if (a->input_index == b->input_index) return 0;
    return a->input_index < b->input_index ? -1 : 1;
}

static int coordinate_value_compare(
    const laplace_highway_coordinate* left,
    const laplace_highway_coordinate* right) {
    int compared = memcmp(left->coordinate.bytes, right->coordinate.bytes, 16u);
    if (compared != 0) return compared;
    return memcmp(
        left->collision_fingerprint.bytes,
        right->collision_fingerprint.bytes, 32u);
}

static void hash_coordinate(
    blake3_hasher* hasher,
    const laplace_highway_coordinate* coordinate) {
    blake3_hasher_update(hasher, coordinate->coordinate.bytes, 16u);
    blake3_hasher_update(
        hasher, coordinate->collision_fingerprint.bytes, 32u);
    hash_u32(hasher, coordinate->kind);
    hash_u64(hasher, coordinate->version);
}

static void hash_candidate(
    blake3_hasher* hasher,
    const laplace_reference_mapping_candidate* value) {
    blake3_hasher_update(hasher, value->boundary_id.bytes, 32u);
    blake3_hasher_update(hasher, value->source_profile_id.bytes, 32u);
    blake3_hasher_update(hasher, value->left_reference_id.bytes, 32u);
    blake3_hasher_update(hasher, value->right_reference_id.bytes, 32u);
    hash_coordinate(hasher, &value->left_coordinate);
    hash_coordinate(hasher, &value->right_coordinate);
    blake3_hasher_update(hasher, value->relation_id.bytes, 16u);
    blake3_hasher_update(hasher, value->row_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, value->left_field_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, value->left_value_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, value->right_field_entity_id.bytes, 16u);
    blake3_hasher_update(hasher, value->right_value_entity_id.bytes, 16u);
    hash_u64(hasher, value->source_ordinal);
    hash_u64(hasher, value->artifact_ordinal);
    hash_u64(hasher, value->row_ordinal);
    hash_u64(hasher, value->relation_version);
    hash_u32(hasher, value->relation_kind);
    hash_u32(hasher, value->flags);
    hash_u32(hasher, value->left_disposition);
    hash_u32(hasher, value->right_disposition);
}

static int disposition_valid(uint32_t disposition) {
    return disposition == LAPLACE_REFERENCE_DISPOSITION_PRESENT ||
        disposition == LAPLACE_REFERENCE_DISPOSITION_RETIRED ||
        disposition == LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED;
}

static int coordinate_valid(const laplace_highway_coordinate* coordinate) {
    return !bytes_zero(coordinate->coordinate.bytes, 16u) &&
        !bytes_zero(coordinate->collision_fingerprint.bytes, 32u) &&
        coordinate->kind != 0u && coordinate->reserved == 0u &&
        coordinate->version != 0u;
}

static int candidate_valid(const laplace_reference_mapping_candidate* value) {
    const uint32_t direction = value->flags &
        LAPLACE_REFERENCE_MAPPING_FLAG_KNOWN_MASK;
    return !bytes_zero(value->boundary_id.bytes, 32u) &&
        !bytes_zero(value->source_profile_id.bytes, 32u) &&
        !bytes_zero(value->left_reference_id.bytes, 32u) &&
        !bytes_zero(value->right_reference_id.bytes, 32u) &&
        coordinate_valid(&value->left_coordinate) &&
        coordinate_valid(&value->right_coordinate) &&
        !bytes_zero(value->relation_id.bytes, 16u) &&
        !bytes_zero(value->row_entity_id.bytes, 16u) &&
        !bytes_zero(value->left_field_entity_id.bytes, 16u) &&
        !bytes_zero(value->left_value_entity_id.bytes, 16u) &&
        !bytes_zero(value->right_field_entity_id.bytes, 16u) &&
        !bytes_zero(value->right_value_entity_id.bytes, 16u) &&
        value->source_ordinal != 0u && value->artifact_ordinal != 0u &&
        value->row_ordinal != 0u && value->relation_version != 0u &&
        value->relation_kind != 0u &&
        (value->flags & ~LAPLACE_REFERENCE_MAPPING_FLAG_KNOWN_MASK) == 0u &&
        (direction == LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED ||
         direction == LAPLACE_REFERENCE_MAPPING_FLAG_SYMMETRIC) &&
        disposition_valid(value->left_disposition) &&
        disposition_valid(value->right_disposition);
}

static uint32_t resolve_disposition(
    const laplace_reference_mapping_candidate* value) {
    const int left_unresolved =
        value->left_disposition == LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED;
    const int right_unresolved =
        value->right_disposition == LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED;
    if (value->left_disposition == LAPLACE_REFERENCE_DISPOSITION_RETIRED ||
        value->right_disposition == LAPLACE_REFERENCE_DISPOSITION_RETIRED) {
        return LAPLACE_REFERENCE_MAPPING_DISPOSITION_RETIRED_ENDPOINT;
    }
#if defined(LAPLACE_TEST_REFERENCE_MAPPING_PROMOTE_UNRESOLVED)
    return LAPLACE_REFERENCE_MAPPING_DISPOSITION_RESOLVED;
#else
    if (left_unresolved && right_unresolved) {
        return LAPLACE_REFERENCE_MAPPING_DISPOSITION_BOTH_UNRESOLVED;
    }
    if (left_unresolved) {
        return LAPLACE_REFERENCE_MAPPING_DISPOSITION_LEFT_UNRESOLVED;
    }
    if (right_unresolved) {
        return LAPLACE_REFERENCE_MAPPING_DISPOSITION_RIGHT_UNRESOLVED;
    }
    return LAPLACE_REFERENCE_MAPPING_DISPOSITION_RESOLVED;
#endif
}

static void identify_proposition(
    const laplace_reference_mapping_candidate* value,
    laplace_digest256* output) {
    const laplace_highway_coordinate* left = &value->left_coordinate;
    const laplace_highway_coordinate* right = &value->right_coordinate;
    blake3_hasher hasher;
    if (
#if defined(LAPLACE_TEST_REFERENCE_MAPPING_COLLAPSE_DIRECTION)
        1 &&
#else
        (value->flags & LAPLACE_REFERENCE_MAPPING_FLAG_SYMMETRIC) != 0u &&
#endif
        coordinate_value_compare(left, right) > 0) {
        const laplace_highway_coordinate* temporary = left;
        left = right;
        right = temporary;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_REFERENCE_MAPPING_PROPOSITION_DOMAIN,
        sizeof(LAPLACE_REFERENCE_MAPPING_PROPOSITION_DOMAIN) - 1u);
    hash_u32(&hasher, value->relation_kind);
    hash_u64(&hasher, value->relation_version);
    blake3_hasher_update(&hasher, value->relation_id.bytes, 16u);
    hash_u32(&hasher, value->flags);
#if defined(LAPLACE_TEST_REFERENCE_MAPPING_WITNESS_IN_PROPOSITION)
    blake3_hasher_update(&hasher, value->source_profile_id.bytes, 32u);
#endif
    hash_coordinate(&hasher, left);
    hash_coordinate(&hasher, right);
    finish(&hasher, output);
}

static void identify_occurrence(
    const laplace_reference_mapping_candidate* value,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_REFERENCE_MAPPING_OCCURRENCE_DOMAIN,
        sizeof(LAPLACE_REFERENCE_MAPPING_OCCURRENCE_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, value->source_profile_id.bytes, 32u);
    blake3_hasher_update(&hasher, value->row_entity_id.bytes, 16u);
    blake3_hasher_update(&hasher, value->left_field_entity_id.bytes, 16u);
    blake3_hasher_update(&hasher, value->left_value_entity_id.bytes, 16u);
    blake3_hasher_update(&hasher, value->right_field_entity_id.bytes, 16u);
    blake3_hasher_update(&hasher, value->right_value_entity_id.bytes, 16u);
    hash_u64(&hasher, value->source_ordinal);
    hash_u64(&hasher, value->artifact_ordinal);
    hash_u64(&hasher, value->row_ordinal);
    finish(&hasher, output);
}

static void identify_record(laplace_reference_mapping_record* record) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_REFERENCE_MAPPING_RECORD_DOMAIN,
        sizeof(LAPLACE_REFERENCE_MAPPING_RECORD_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, record->proposition_id.bytes, 32u);
    blake3_hasher_update(&hasher, record->occurrence_id.bytes, 32u);
    blake3_hasher_update(
        &hasher, record->candidate.left_reference_id.bytes, 32u);
    blake3_hasher_update(
        &hasher, record->candidate.right_reference_id.bytes, 32u);
    hash_u32(&hasher, record->disposition);
    finish(&hasher, &record->mapping_id);
}

laplace_reference_mapping_status laplace_reference_mapping_resolve_batch(
    const laplace_reference_mapping_candidate* candidates,
    size_t candidate_count,
    laplace_reference_mapping_record* records,
    laplace_reference_mapping_receipt* receipt,
    laplace_reference_mapping_error* error) {
    digest_index* occurrences = NULL;
    digest_index* propositions = NULL;
    coordinate_index* coordinates = NULL;
    blake3_hasher input_hasher;
    blake3_hasher output_hasher;
    blake3_hasher receipt_hasher;
    size_t index;
    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->version = LAPLACE_REFERENCE_MAPPING_VERSION;
    }
    if (error != NULL) {
        error->record_index = UINT64_MAX;
        error->field = MAPPING_FIELD_NONE;
        error->reserved = 0u;
    }
    if (candidates == NULL || records == NULL || receipt == NULL ||
        candidate_count == 0u || candidate_count > SIZE_MAX / 2u ||
        candidate_count > SIZE_MAX / sizeof(*occurrences)) {
        return LAPLACE_REFERENCE_MAPPING_INVALID_ARGUMENT;
    }
    occurrences = (digest_index*)calloc(candidate_count, sizeof(*occurrences));
    propositions = (digest_index*)calloc(candidate_count, sizeof(*propositions));
    coordinates = (coordinate_index*)calloc(
        candidate_count * 2u, sizeof(*coordinates));
    if (occurrences == NULL || propositions == NULL || coordinates == NULL) {
        free(occurrences); free(propositions); free(coordinates);
        return LAPLACE_REFERENCE_MAPPING_MEMORY_FAILURE;
    }
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(
        &input_hasher, LAPLACE_REFERENCE_MAPPING_INPUT_DOMAIN,
        sizeof(LAPLACE_REFERENCE_MAPPING_INPUT_DOMAIN) - 1u);
    hash_u64(&input_hasher, (uint64_t)candidate_count);
    for (index = 0u; index < candidate_count; ++index) {
        const laplace_reference_mapping_candidate* value = &candidates[index];
        if (!candidate_valid(value)) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
                error->field = MAPPING_FIELD_RECORD;
            }
            free(occurrences); free(propositions); free(coordinates);
            return LAPLACE_REFERENCE_MAPPING_RECORD_INVALID;
        }
        if (index != 0u && memcmp(
                candidates[0].boundary_id.bytes,
                value->boundary_id.bytes, 32u) != 0) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
                error->field = MAPPING_FIELD_BOUNDARY;
            }
            free(occurrences); free(propositions); free(coordinates);
            return LAPLACE_REFERENCE_MAPPING_BOUNDARY_MISMATCH;
        }
        records[index].candidate = *value;
        records[index].disposition = resolve_disposition(value);
        records[index].reserved = 0u;
        identify_proposition(value, &records[index].proposition_id);
        identify_occurrence(value, &records[index].occurrence_id);
        identify_record(&records[index]);
        occurrences[index].digest = records[index].occurrence_id;
        occurrences[index].input_index = index;
        propositions[index].digest = records[index].proposition_id;
        propositions[index].input_index = index;
        coordinates[index * 2u].coordinate = value->left_coordinate.coordinate;
        coordinates[index * 2u].fingerprint =
            value->left_coordinate.collision_fingerprint;
        coordinates[index * 2u].input_index = index;
        coordinates[index * 2u + 1u].coordinate =
            value->right_coordinate.coordinate;
        coordinates[index * 2u + 1u].fingerprint =
            value->right_coordinate.collision_fingerprint;
        coordinates[index * 2u + 1u].input_index = index;
        hash_candidate(&input_hasher, value);
    }
    qsort(occurrences, candidate_count, sizeof(*occurrences), digest_compare);
    for (index = 1u; index < candidate_count; ++index) {
        if (memcmp(occurrences[index - 1u].digest.bytes,
                   occurrences[index].digest.bytes, 32u) == 0) {
            if (error != NULL) {
                error->record_index = (uint64_t)occurrences[index].input_index;
                error->field = MAPPING_FIELD_OCCURRENCE;
            }
            free(occurrences); free(propositions); free(coordinates);
            return LAPLACE_REFERENCE_MAPPING_DUPLICATE_OCCURRENCE;
        }
    }
    qsort(coordinates, candidate_count * 2u, sizeof(*coordinates),
          coordinate_compare);
    for (index = 1u; index < candidate_count * 2u; ++index) {
        if (memcmp(coordinates[index - 1u].coordinate.bytes,
                   coordinates[index].coordinate.bytes, 16u) == 0 &&
            memcmp(coordinates[index - 1u].fingerprint.bytes,
                   coordinates[index].fingerprint.bytes, 32u) != 0) {
            if (error != NULL) {
                error->record_index = (uint64_t)coordinates[index].input_index;
                error->field = MAPPING_FIELD_COORDINATE;
            }
            free(occurrences); free(propositions); free(coordinates);
            return LAPLACE_REFERENCE_MAPPING_COORDINATE_COLLISION;
        }
    }
    qsort(propositions, candidate_count, sizeof(*propositions), digest_compare);
    receipt->proposition_count = 1u;
    for (index = 1u; index < candidate_count; ++index) {
        if (memcmp(propositions[index - 1u].digest.bytes,
                   propositions[index].digest.bytes, 32u) != 0) {
            receipt->proposition_count += 1u;
        }
    }
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, LAPLACE_REFERENCE_MAPPING_OUTPUT_DOMAIN,
        sizeof(LAPLACE_REFERENCE_MAPPING_OUTPUT_DOMAIN) - 1u);
    hash_u64(&output_hasher, (uint64_t)candidate_count);
    for (index = 0u; index < candidate_count; ++index) {
        blake3_hasher_update(
            &output_hasher, records[index].mapping_id.bytes, 32u);
        if (records[index].disposition ==
            LAPLACE_REFERENCE_MAPPING_DISPOSITION_RESOLVED) {
            receipt->resolved_count += 1u;
        } else if (records[index].disposition ==
                   LAPLACE_REFERENCE_MAPPING_DISPOSITION_RETIRED_ENDPOINT) {
            receipt->retired_count += 1u;
        } else {
            receipt->unresolved_count += 1u;
        }
    }
    receipt->boundary_id = candidates[0].boundary_id;
    receipt->occurrence_count = (uint64_t)candidate_count;
    receipt->status = LAPLACE_REFERENCE_MAPPING_OK;
    finish(&input_hasher, &receipt->input_fingerprint);
    finish(&output_hasher, &receipt->output_fingerprint);
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_REFERENCE_MAPPING_RECEIPT_DOMAIN,
        sizeof(LAPLACE_REFERENCE_MAPPING_RECEIPT_DOMAIN) - 1u);
    blake3_hasher_update(&receipt_hasher, receipt->boundary_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(&receipt_hasher, receipt->occurrence_count);
    hash_u64(&receipt_hasher, receipt->proposition_count);
    hash_u64(&receipt_hasher, receipt->resolved_count);
    hash_u64(&receipt_hasher, receipt->unresolved_count);
    hash_u64(&receipt_hasher, receipt->retired_count);
    hash_u32(&receipt_hasher, receipt->version);
    hash_u32(&receipt_hasher, receipt->status);
    finish(&receipt_hasher, &receipt->receipt_id);
    free(occurrences); free(propositions); free(coordinates);
    return LAPLACE_REFERENCE_MAPPING_OK;
}
