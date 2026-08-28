#include "laplace/reference_topology.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "blake3.h"

_Static_assert(sizeof(laplace_reference_candidate) == 200u,
               "reference candidate ABI must be exactly 200 bytes");
_Static_assert(sizeof(laplace_reference_record) == 336u,
               "reference record ABI must be exactly 336 bytes");
_Static_assert(sizeof(laplace_reference_topology_receipt) == 176u,
               "reference receipt ABI must be exactly 176 bytes");

enum reference_field {
    REFERENCE_FIELD_NONE = 0u,
    REFERENCE_FIELD_RECORD = 1u,
    REFERENCE_FIELD_PROFILE = 2u,
    REFERENCE_FIELD_DECLARATION = 3u,
    REFERENCE_FIELD_COORDINATE = 4u,
    REFERENCE_FIELD_OCCURRENCE = 5u
};

typedef struct reference_index {
    laplace_highway_key key;
    size_t input_index;
    uint32_t rule_flags;
} reference_index;

typedef struct coordinate_index {
    laplace_id128 coordinate;
    laplace_digest256 fingerprint;
    size_t input_index;
} coordinate_index;

typedef struct occurrence_index {
    laplace_digest256 occurrence_id;
    size_t input_index;
} occurrence_index;

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

static void hash_key(blake3_hasher* hasher, const laplace_highway_key* key) {
    hash_u32(hasher, key->kind);
    blake3_hasher_update(hasher, key->authority.bytes, 16u);
    blake3_hasher_update(hasher, key->release.bytes, 16u);
    blake3_hasher_update(hasher, key->name_space.bytes, 16u);
    blake3_hasher_update(hasher, key->local_identifier.bytes, 16u);
    hash_u64(hasher, key->version);
}

static int key_compare_value(
    const laplace_highway_key* left,
    const laplace_highway_key* right) {
    int compared;
    if (left->kind != right->kind) {
        return left->kind < right->kind ? -1 : 1;
    }
    compared = memcmp(left->authority.bytes, right->authority.bytes, 16u);
    if (compared != 0) return compared;
    compared = memcmp(left->release.bytes, right->release.bytes, 16u);
    if (compared != 0) return compared;
    compared = memcmp(left->name_space.bytes, right->name_space.bytes, 16u);
    if (compared != 0) return compared;
    compared = memcmp(
        left->local_identifier.bytes, right->local_identifier.bytes, 16u);
    if (compared != 0) return compared;
    if (left->version == right->version) return 0;
    return left->version < right->version ? -1 : 1;
}

static int reference_index_compare(const void* left, const void* right) {
    const reference_index* a = (const reference_index*)left;
    const reference_index* b = (const reference_index*)right;
    const int compared = key_compare_value(&a->key, &b->key);
    if (compared != 0) return compared;
    if (a->input_index == b->input_index) return 0;
    return a->input_index < b->input_index ? -1 : 1;
}

static int coordinate_index_compare(const void* left, const void* right) {
    const coordinate_index* a = (const coordinate_index*)left;
    const coordinate_index* b = (const coordinate_index*)right;
    int compared = memcmp(a->coordinate.bytes, b->coordinate.bytes, 16u);
    if (compared != 0) return compared;
    compared = memcmp(a->fingerprint.bytes, b->fingerprint.bytes, 32u);
    if (compared != 0) return compared;
    if (a->input_index == b->input_index) return 0;
    return a->input_index < b->input_index ? -1 : 1;
}

static int occurrence_index_compare(const void* left, const void* right) {
    const occurrence_index* a = (const occurrence_index*)left;
    const occurrence_index* b = (const occurrence_index*)right;
    const int compared = memcmp(
        a->occurrence_id.bytes, b->occurrence_id.bytes, 32u);
    if (compared != 0) return compared;
    if (a->input_index == b->input_index) return 0;
    return a->input_index < b->input_index ? -1 : 1;
}

static int candidate_valid(const laplace_reference_candidate* candidate) {
    const uint32_t declarations = candidate->rule_flags &
        (LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION |
         LAPLACE_REFERENCE_RULE_RETIRED_DECLARATION);
    return !bytes_zero(&candidate->source_profile_id, 32u) &&
        !bytes_zero(&candidate->row_entity_id, 16u) &&
        !bytes_zero(&candidate->field_entity_id, 16u) &&
        !bytes_zero(&candidate->value_entity_id, 16u) &&
#if defined(LAPLACE_TEST_REFERENCE_USE_FIELD_AS_LOCAL_IDENTIFIER)
        memcmp(candidate->field_entity_id.bytes,
               candidate->key.local_identifier.bytes, 16u) == 0 &&
#else
        memcmp(candidate->value_entity_id.bytes,
               candidate->key.local_identifier.bytes, 16u) == 0 &&
#endif
        candidate->source_ordinal != 0u &&
        candidate->row_ordinal != 0u &&
        candidate->column_ordinal != 0u &&
        candidate->reserved == 0u &&
        (candidate->rule_flags & LAPLACE_REFERENCE_RULE_ENDPOINT) != 0u &&
        (candidate->rule_flags & ~LAPLACE_REFERENCE_RULE_KNOWN_MASK) == 0u &&
        declarations != (LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION |
                         LAPLACE_REFERENCE_RULE_RETIRED_DECLARATION);
}

static void identify_occurrence(
    const laplace_reference_candidate* candidate,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_REFERENCE_TOPOLOGY_OCCURRENCE_DOMAIN,
        sizeof(LAPLACE_REFERENCE_TOPOLOGY_OCCURRENCE_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, candidate->source_profile_id.bytes, 32u);
    blake3_hasher_update(&hasher, candidate->row_entity_id.bytes, 16u);
    blake3_hasher_update(&hasher, candidate->field_entity_id.bytes, 16u);
    blake3_hasher_update(&hasher, candidate->value_entity_id.bytes, 16u);
    hash_u64(&hasher, candidate->source_ordinal);
    hash_u64(&hasher, candidate->artifact_ordinal);
    hash_u64(&hasher, candidate->row_ordinal);
    hash_u64(&hasher, candidate->column_ordinal);
    finish(&hasher, output);
}

static void identify_record(laplace_reference_record* record) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_REFERENCE_TOPOLOGY_IDENTITY_DOMAIN,
        sizeof(LAPLACE_REFERENCE_TOPOLOGY_IDENTITY_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, record->occurrence_id.bytes, 32u);
    hash_key(&hasher, &record->candidate.key);
    blake3_hasher_update(&hasher, record->coordinate.coordinate.bytes, 16u);
    blake3_hasher_update(
        &hasher, record->coordinate.collision_fingerprint.bytes, 32u);
    hash_u32(&hasher, record->disposition);
    hash_u32(&hasher, record->candidate.rule_flags);
    finish(&hasher, &record->reference_id);
}

laplace_reference_topology_status laplace_reference_topology_resolve_batch(
    const laplace_reference_candidate* candidates,
    size_t candidate_count,
    laplace_reference_record* records,
    laplace_reference_topology_receipt* receipt,
    laplace_reference_topology_error* error) {
    reference_index* ordered = NULL;
    coordinate_index* coordinates = NULL;
    occurrence_index* occurrences = NULL;
    blake3_hasher input_hasher;
    blake3_hasher output_hasher;
    blake3_hasher receipt_hasher;
    size_t index;
    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->version = LAPLACE_REFERENCE_TOPOLOGY_VERSION;
    }
    if (error != NULL) {
        error->record_index = UINT64_MAX;
        error->field = REFERENCE_FIELD_NONE;
        error->reserved = 0u;
    }
    if (candidates == NULL || records == NULL || receipt == NULL ||
        candidate_count == 0u || candidate_count > SIZE_MAX / sizeof(*ordered)) {
        return LAPLACE_REFERENCE_TOPOLOGY_INVALID_ARGUMENT;
    }
    ordered = (reference_index*)calloc(candidate_count, sizeof(*ordered));
    coordinates = (coordinate_index*)calloc(candidate_count, sizeof(*coordinates));
    occurrences = (occurrence_index*)calloc(candidate_count, sizeof(*occurrences));
    if (ordered == NULL || coordinates == NULL || occurrences == NULL) {
        free(ordered);
        free(coordinates);
        free(occurrences);
        return LAPLACE_REFERENCE_TOPOLOGY_MEMORY_FAILURE;
    }
    blake3_hasher_init(&input_hasher);
    blake3_hasher_update(
        &input_hasher, LAPLACE_REFERENCE_TOPOLOGY_INPUT_DOMAIN,
        sizeof(LAPLACE_REFERENCE_TOPOLOGY_INPUT_DOMAIN) - 1u);
    hash_u64(&input_hasher, (uint64_t)candidate_count);
    for (index = 0u; index < candidate_count; ++index) {
        if (!candidate_valid(&candidates[index]) ||
            laplace_highway_coordinate_calculate(
                &candidates[index].key, &records[index].coordinate) !=
                LAPLACE_HIGHWAY_OK) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
                error->field = REFERENCE_FIELD_RECORD;
            }
            free(ordered); free(coordinates); free(occurrences);
            return LAPLACE_REFERENCE_TOPOLOGY_RECORD_INVALID;
        }
        if (index != 0u && memcmp(
                candidates[0].source_profile_id.bytes,
                candidates[index].source_profile_id.bytes, 32u) != 0) {
            if (error != NULL) {
                error->record_index = (uint64_t)index;
                error->field = REFERENCE_FIELD_PROFILE;
            }
            free(ordered); free(coordinates); free(occurrences);
            return LAPLACE_REFERENCE_TOPOLOGY_PROFILE_MISMATCH;
        }
        records[index].candidate = candidates[index];
        records[index].reserved = 0u;
        identify_occurrence(&candidates[index], &records[index].occurrence_id);
        ordered[index].key = candidates[index].key;
        ordered[index].input_index = index;
        ordered[index].rule_flags = candidates[index].rule_flags;
        coordinates[index].coordinate = records[index].coordinate.coordinate;
        coordinates[index].fingerprint =
            records[index].coordinate.collision_fingerprint;
        coordinates[index].input_index = index;
        occurrences[index].occurrence_id = records[index].occurrence_id;
        occurrences[index].input_index = index;
        blake3_hasher_update(
            &input_hasher, candidates[index].source_profile_id.bytes, 32u);
        hash_key(&input_hasher, &candidates[index].key);
        blake3_hasher_update(&input_hasher, candidates[index].row_entity_id.bytes, 16u);
        blake3_hasher_update(&input_hasher, candidates[index].field_entity_id.bytes, 16u);
        blake3_hasher_update(&input_hasher, candidates[index].value_entity_id.bytes, 16u);
        hash_u64(&input_hasher, candidates[index].source_ordinal);
        hash_u64(&input_hasher, candidates[index].artifact_ordinal);
        hash_u64(&input_hasher, candidates[index].row_ordinal);
        hash_u64(&input_hasher, candidates[index].column_ordinal);
        hash_u32(&input_hasher, candidates[index].rule_flags);
    }
    qsort(ordered, candidate_count, sizeof(*ordered), reference_index_compare);
    index = 0u;
    while (index < candidate_count) {
        size_t end = index + 1u;
        int present = 0;
        int retired = 0;
        uint32_t disposition;
        while (end < candidate_count &&
               key_compare_value(&ordered[index].key, &ordered[end].key) == 0) {
            ++end;
        }
        for (size_t item = index; item < end; ++item) {
            present |= (ordered[item].rule_flags &
                        LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION) != 0u;
            retired |= (ordered[item].rule_flags &
                        LAPLACE_REFERENCE_RULE_RETIRED_DECLARATION) != 0u;
        }
#if defined(LAPLACE_TEST_REFERENCE_IGNORE_DECLARATION_CONFLICT)
        retired = 0;
#endif
        if (present && retired) {
            if (error != NULL) {
                error->record_index = (uint64_t)ordered[index].input_index;
                error->field = REFERENCE_FIELD_DECLARATION;
            }
            free(ordered); free(coordinates); free(occurrences);
            return LAPLACE_REFERENCE_TOPOLOGY_DECLARATION_CONFLICT;
        }
#if defined(LAPLACE_TEST_REFERENCE_PROMOTE_UNRESOLVED)
        disposition = LAPLACE_REFERENCE_DISPOSITION_PRESENT;
#else
        disposition = present ? LAPLACE_REFERENCE_DISPOSITION_PRESENT :
            (retired ? LAPLACE_REFERENCE_DISPOSITION_RETIRED :
             LAPLACE_REFERENCE_DISPOSITION_UNRESOLVED);
#endif
        for (size_t item = index; item < end; ++item) {
            records[ordered[item].input_index].disposition = disposition;
        }
        receipt->coordinate_count += 1u;
        index = end;
    }
    qsort(coordinates, candidate_count, sizeof(*coordinates),
          coordinate_index_compare);
    for (index = 1u; index < candidate_count; ++index) {
        if (memcmp(coordinates[index - 1u].coordinate.bytes,
                   coordinates[index].coordinate.bytes, 16u) == 0 &&
            memcmp(coordinates[index - 1u].fingerprint.bytes,
                   coordinates[index].fingerprint.bytes, 32u) != 0) {
            if (error != NULL) {
                error->record_index = (uint64_t)coordinates[index].input_index;
                error->field = REFERENCE_FIELD_COORDINATE;
            }
            free(ordered); free(coordinates); free(occurrences);
            return LAPLACE_REFERENCE_TOPOLOGY_COORDINATE_COLLISION;
        }
    }
    qsort(occurrences, candidate_count, sizeof(*occurrences),
          occurrence_index_compare);
    for (index = 1u; index < candidate_count; ++index) {
        if (memcmp(occurrences[index - 1u].occurrence_id.bytes,
                   occurrences[index].occurrence_id.bytes, 32u) == 0) {
            if (error != NULL) {
                error->record_index = (uint64_t)occurrences[index].input_index;
                error->field = REFERENCE_FIELD_OCCURRENCE;
            }
            free(ordered); free(coordinates); free(occurrences);
            return LAPLACE_REFERENCE_TOPOLOGY_DUPLICATE_OCCURRENCE;
        }
    }
    blake3_hasher_init(&output_hasher);
    blake3_hasher_update(
        &output_hasher, LAPLACE_REFERENCE_TOPOLOGY_OUTPUT_DOMAIN,
        sizeof(LAPLACE_REFERENCE_TOPOLOGY_OUTPUT_DOMAIN) - 1u);
    hash_u64(&output_hasher, (uint64_t)candidate_count);
    for (index = 0u; index < candidate_count; ++index) {
        identify_record(&records[index]);
        blake3_hasher_update(&output_hasher, records[index].reference_id.bytes, 32u);
        switch (records[index].disposition) {
            case LAPLACE_REFERENCE_DISPOSITION_PRESENT:
                receipt->present_count += 1u;
                break;
            case LAPLACE_REFERENCE_DISPOSITION_RETIRED:
                receipt->retired_count += 1u;
                break;
            default:
                receipt->unresolved_count += 1u;
                break;
        }
    }
    receipt->source_profile_id = candidates[0].source_profile_id;
    receipt->occurrence_count = (uint64_t)candidate_count;
    receipt->status = LAPLACE_REFERENCE_TOPOLOGY_OK;
    finish(&input_hasher, &receipt->input_fingerprint);
    finish(&output_hasher, &receipt->output_fingerprint);
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_REFERENCE_TOPOLOGY_RECEIPT_DOMAIN,
        sizeof(LAPLACE_REFERENCE_TOPOLOGY_RECEIPT_DOMAIN) - 1u);
    blake3_hasher_update(&receipt_hasher, receipt->source_profile_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->input_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, receipt->output_fingerprint.bytes, 32u);
    hash_u64(&receipt_hasher, receipt->occurrence_count);
    hash_u64(&receipt_hasher, receipt->coordinate_count);
    hash_u64(&receipt_hasher, receipt->present_count);
    hash_u64(&receipt_hasher, receipt->retired_count);
    hash_u64(&receipt_hasher, receipt->unresolved_count);
    hash_u32(&receipt_hasher, receipt->version);
    hash_u32(&receipt_hasher, receipt->status);
    finish(&receipt_hasher, &receipt->receipt_id);
    free(ordered); free(coordinates); free(occurrences);
    return LAPLACE_REFERENCE_TOPOLOGY_OK;
}
