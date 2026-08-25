#include "laplace/perfcache.h"

#include <limits.h>
#include <string.h>

#include "blake3.h"

enum {
    LAPLACE_PERFCACHE_OFFSET_VERSION = 8,
    LAPLACE_PERFCACHE_OFFSET_HEADER_BYTES = 12,
    LAPLACE_PERFCACHE_OFFSET_FLAGS = 16,
    LAPLACE_PERFCACHE_OFFSET_MODULE_ID = 24,
    LAPLACE_PERFCACHE_OFFSET_KEY_SCHEMA_ID = 40,
    LAPLACE_PERFCACHE_OFFSET_VALUE_SCHEMA_ID = 56,
    LAPLACE_PERFCACHE_OFFSET_ACTIVATION_EPOCH_ID = 72,
    LAPLACE_PERFCACHE_OFFSET_SOURCE_FINGERPRINT = 88,
    LAPLACE_PERFCACHE_OFFSET_RECIPE_FINGERPRINT = 120,
    LAPLACE_PERFCACHE_OFFSET_DEPENDENCY_FINGERPRINT = 152,
    LAPLACE_PERFCACHE_OFFSET_RECORD_COUNT = 184,
    LAPLACE_PERFCACHE_OFFSET_KEY_BYTES = 192,
    LAPLACE_PERFCACHE_OFFSET_VALUE_BYTES = 196,
    LAPLACE_PERFCACHE_OFFSET_RECORD_STRIDE = 200,
    LAPLACE_PERFCACHE_OFFSET_ACCESS_LAW = 204,
    LAPLACE_PERFCACHE_OFFSET_RECORDS = 208,
    LAPLACE_PERFCACHE_OFFSET_RECORDS_BYTES = 216,
    LAPLACE_PERFCACHE_OFFSET_METADATA = 224,
    LAPLACE_PERFCACHE_OFFSET_METADATA_BYTES = 232,
    LAPLACE_PERFCACHE_OFFSET_DIGEST = 240
};

static const uint8_t laplace_perfcache_magic[8] = {
    (uint8_t)'L', (uint8_t)'A', (uint8_t)'P', (uint8_t)'C',
    (uint8_t)'A', (uint8_t)'C', (uint8_t)'H', (uint8_t)'E'
};

static void write_u32_le(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t* destination, uint64_t value) {
    size_t index;
    for (index = 0; index < 8u; ++index) {
        destination[index] = (uint8_t)(value >> (index * 8u));
    }
}

static uint32_t read_u32_le(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static uint64_t read_u64_le(const uint8_t* source) {
    uint64_t value = 0;
    size_t index;
    for (index = 0; index < 8u; ++index) {
        value |= (uint64_t)source[index] << (index * 8u);
    }
    return value;
}

static int bytes_nonzero(const uint8_t* bytes, size_t count) {
    size_t index;
    uint8_t combined = 0;
    for (index = 0; index < count; ++index) {
        combined |= bytes[index];
    }
    return combined != 0u;
}

static int bytes_zero(const uint8_t* bytes, size_t count) {
    return !bytes_nonzero(bytes, count);
}

static int contract_valid(const laplace_perfcache_contract* contract) {
    const uint64_t stride =
        (uint64_t)contract->key_bytes + (uint64_t)contract->value_bytes;
#if defined(LAPLACE_TEST_REJECT_ZERO_PERFCACHE_VALUES)
    if (!bytes_nonzero(contract->module_id.bytes, sizeof(contract->module_id.bytes)) ||
        !bytes_nonzero(contract->key_schema_id.bytes, sizeof(contract->key_schema_id.bytes)) ||
        !bytes_nonzero(contract->value_schema_id.bytes, sizeof(contract->value_schema_id.bytes)) ||
        !bytes_nonzero(contract->activation_epoch_id.bytes,
                       sizeof(contract->activation_epoch_id.bytes)) ||
        !bytes_nonzero(contract->source_fingerprint.bytes,
                       sizeof(contract->source_fingerprint.bytes)) ||
        !bytes_nonzero(contract->recipe_fingerprint.bytes,
                       sizeof(contract->recipe_fingerprint.bytes)) ||
        !bytes_nonzero(contract->dependency_fingerprint.bytes,
                       sizeof(contract->dependency_fingerprint.bytes))) {
        return 0;
    }
#endif
    if (contract->value_bytes == 0u || stride == 0u || stride > UINT32_MAX ||
        contract->flags != 0u) {
        return 0;
    }
    if ((contract->access_law == LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED &&
         contract->key_bytes == 0u) ||
        (contract->access_law == LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED &&
         contract->key_bytes != sizeof(uint32_t)) ||
        (contract->access_law != LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED &&
         contract->access_law != LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED &&
         contract->access_law != LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED)) {
        return 0;
    }
    return 1;
}

static int contract_equal(
    const laplace_perfcache_contract* left,
    const laplace_perfcache_contract* right) {
    return memcmp(left->module_id.bytes,
                  right->module_id.bytes,
                  sizeof(left->module_id.bytes)) == 0 &&
        memcmp(left->key_schema_id.bytes,
               right->key_schema_id.bytes,
               sizeof(left->key_schema_id.bytes)) == 0 &&
        memcmp(left->value_schema_id.bytes,
               right->value_schema_id.bytes,
               sizeof(left->value_schema_id.bytes)) == 0 &&
        memcmp(left->activation_epoch_id.bytes,
               right->activation_epoch_id.bytes,
               sizeof(left->activation_epoch_id.bytes)) == 0 &&
        memcmp(left->source_fingerprint.bytes,
               right->source_fingerprint.bytes,
               sizeof(left->source_fingerprint.bytes)) == 0 &&
        memcmp(left->recipe_fingerprint.bytes,
               right->recipe_fingerprint.bytes,
               sizeof(left->recipe_fingerprint.bytes)) == 0 &&
        memcmp(left->dependency_fingerprint.bytes,
               right->dependency_fingerprint.bytes,
               sizeof(left->dependency_fingerprint.bytes)) == 0 &&
        left->key_bytes == right->key_bytes &&
        left->value_bytes == right->value_bytes &&
        left->access_law == right->access_law &&
        left->flags == right->flags;
}

static int records_sorted_unique(
    const uint8_t* records,
    uint64_t record_count,
    uint32_t key_bytes,
    uint32_t record_stride);

static int dense_u32_zero_based_keys(
    const uint8_t* records,
    uint64_t record_count,
    uint32_t record_stride) {
    uint64_t index;
    if (record_count > (uint64_t)UINT32_MAX + 1u) {
        return 0;
    }
    for (index = 0; index < record_count; ++index) {
        if (read_u32_le(records + (size_t)index * (size_t)record_stride) !=
            (uint32_t)index) {
            return 0;
        }
    }
    return 1;
}

static laplace_perfcache_status records_access_law_valid(
    const uint8_t* records,
    uint64_t record_count,
    const laplace_perfcache_contract* contract,
    uint32_t record_stride) {
    if (record_count == 0u) {
        return LAPLACE_PERFCACHE_OK;
    }
    switch (contract->access_law) {
        case LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED:
            return records_sorted_unique(
                       records, record_count, contract->key_bytes, record_stride)
                ? LAPLACE_PERFCACHE_OK
                : LAPLACE_PERFCACHE_KEYS_NOT_SORTED_UNIQUE;
        case LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED:
            return dense_u32_zero_based_keys(records, record_count, record_stride)
                ? LAPLACE_PERFCACHE_OK
                : LAPLACE_PERFCACHE_DENSE_KEY_MISMATCH;
        case LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED:
            return LAPLACE_PERFCACHE_OK;
        default:
            return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
}

static int records_sorted_unique(
    const uint8_t* records,
    uint64_t record_count,
    uint32_t key_bytes,
    uint32_t record_stride) {
#if defined(LAPLACE_TEST_SKIP_SORTED_UNIQUE_VALIDATION)
    (void)records;
    (void)record_count;
    (void)key_bytes;
    (void)record_stride;
    return 1;
#else
    uint64_t index;
    for (index = 1; index < record_count; ++index) {
        const uint8_t* previous = records +
            (size_t)(index - 1u) * (size_t)record_stride;
        const uint8_t* current = records +
            (size_t)index * (size_t)record_stride;
        if (memcmp(previous, current, key_bytes) >= 0) {
            return 0;
        }
    }
    return 1;
#endif
}

static laplace_perfcache_status measure_internal(
    const laplace_perfcache_spec* spec,
    uint64_t* record_stride,
    uint64_t* records_bytes,
    uint64_t* artifact_bytes) {
    uint64_t size;

    if (spec == NULL || record_stride == NULL || records_bytes == NULL ||
        artifact_bytes == NULL || !contract_valid(&spec->contract)) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    if ((spec->record_count != 0u && spec->records == NULL) ||
        (spec->metadata_bytes != 0u && spec->metadata == NULL)) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *record_stride =
        (uint64_t)spec->contract.key_bytes + (uint64_t)spec->contract.value_bytes;
    if (spec->record_count > UINT64_MAX / *record_stride) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    *records_bytes = spec->record_count * *record_stride;
    size = LAPLACE_PERFCACHE_HEADER_BYTES;
    if (UINT64_MAX - size < *records_bytes) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    size += *records_bytes;
    if (UINT64_MAX - size < spec->metadata_bytes) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    size += spec->metadata_bytes;
    if (UINT64_MAX - size < LAPLACE_PERFCACHE_DIGEST_BYTES) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    size += LAPLACE_PERFCACHE_DIGEST_BYTES;
    if (size > SIZE_MAX) {
        return LAPLACE_PERFCACHE_SIZE_OVERFLOW;
    }
    *artifact_bytes = size;
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status laplace_perfcache_measure(
    const laplace_perfcache_spec* spec,
    size_t* artifact_bytes) {
    uint64_t stride = 0;
    uint64_t records_bytes = 0;
    uint64_t measured = 0;
    laplace_perfcache_status status;

    if (artifact_bytes == NULL) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    status = measure_internal(spec, &stride, &records_bytes, &measured);
    if (status == LAPLACE_PERFCACHE_OK) {
        *artifact_bytes = (size_t)measured;
    }
    return status;
}

laplace_perfcache_status laplace_perfcache_write(
    const laplace_perfcache_spec* spec,
    uint8_t* artifact,
    size_t artifact_capacity,
    size_t* artifact_bytes) {
    uint64_t stride = 0;
    uint64_t records_bytes = 0;
    uint64_t measured = 0;
    uint64_t metadata_offset;
    uint64_t digest_offset;
    blake3_hasher hasher;
    laplace_perfcache_status status;

    if (artifact_bytes == NULL) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    status = measure_internal(spec, &stride, &records_bytes, &measured);
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }
    *artifact_bytes = (size_t)measured;
    if (artifact == NULL || artifact_capacity < (size_t)measured) {
        return LAPLACE_PERFCACHE_BUFFER_TOO_SMALL;
    }
    status = records_access_law_valid(
        spec->records, spec->record_count, &spec->contract, (uint32_t)stride);
    if (status != LAPLACE_PERFCACHE_OK) {
        return status;
    }

    metadata_offset = LAPLACE_PERFCACHE_HEADER_BYTES + records_bytes;
    digest_offset = metadata_offset + spec->metadata_bytes;
    memset(artifact, 0, (size_t)measured);
    memcpy(artifact, laplace_perfcache_magic, sizeof(laplace_perfcache_magic));
    write_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_VERSION,
                 LAPLACE_PERFCACHE_FORMAT_VERSION);
    write_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_HEADER_BYTES,
                 LAPLACE_PERFCACHE_HEADER_BYTES);
    write_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_FLAGS, spec->contract.flags);
    memcpy(artifact + LAPLACE_PERFCACHE_OFFSET_MODULE_ID,
           spec->contract.module_id.bytes,
           sizeof(spec->contract.module_id.bytes));
    memcpy(artifact + LAPLACE_PERFCACHE_OFFSET_KEY_SCHEMA_ID,
           spec->contract.key_schema_id.bytes,
           sizeof(spec->contract.key_schema_id.bytes));
    memcpy(artifact + LAPLACE_PERFCACHE_OFFSET_VALUE_SCHEMA_ID,
           spec->contract.value_schema_id.bytes,
           sizeof(spec->contract.value_schema_id.bytes));
    memcpy(artifact + LAPLACE_PERFCACHE_OFFSET_ACTIVATION_EPOCH_ID,
           spec->contract.activation_epoch_id.bytes,
           sizeof(spec->contract.activation_epoch_id.bytes));
    memcpy(artifact + LAPLACE_PERFCACHE_OFFSET_SOURCE_FINGERPRINT,
           spec->contract.source_fingerprint.bytes,
           sizeof(spec->contract.source_fingerprint.bytes));
    memcpy(artifact + LAPLACE_PERFCACHE_OFFSET_RECIPE_FINGERPRINT,
           spec->contract.recipe_fingerprint.bytes,
           sizeof(spec->contract.recipe_fingerprint.bytes));
    memcpy(artifact + LAPLACE_PERFCACHE_OFFSET_DEPENDENCY_FINGERPRINT,
           spec->contract.dependency_fingerprint.bytes,
           sizeof(spec->contract.dependency_fingerprint.bytes));
    write_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORD_COUNT, spec->record_count);
    write_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_KEY_BYTES,
                 spec->contract.key_bytes);
    write_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_VALUE_BYTES,
                 spec->contract.value_bytes);
    write_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORD_STRIDE, (uint32_t)stride);
    write_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_ACCESS_LAW,
                 spec->contract.access_law);
    write_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORDS,
                 LAPLACE_PERFCACHE_HEADER_BYTES);
    write_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORDS_BYTES, records_bytes);
    write_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_METADATA, metadata_offset);
    write_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_METADATA_BYTES,
                 spec->metadata_bytes);
    write_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_DIGEST, digest_offset);
    if (records_bytes != 0u) {
        memcpy(artifact + LAPLACE_PERFCACHE_HEADER_BYTES,
               spec->records,
               (size_t)records_bytes);
    }
    if (spec->metadata_bytes != 0u) {
        memcpy(artifact + (size_t)metadata_offset,
               spec->metadata,
               (size_t)spec->metadata_bytes);
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, artifact, (size_t)digest_offset);
    blake3_hasher_finalize(
        &hasher, artifact + (size_t)digest_offset, LAPLACE_PERFCACHE_DIGEST_BYTES);
    return LAPLACE_PERFCACHE_OK;
}

static void read_contract(
    const uint8_t* artifact,
    laplace_perfcache_contract* contract) {
    memset(contract, 0, sizeof(*contract));
    memcpy(contract->module_id.bytes,
           artifact + LAPLACE_PERFCACHE_OFFSET_MODULE_ID,
           sizeof(contract->module_id.bytes));
    memcpy(contract->key_schema_id.bytes,
           artifact + LAPLACE_PERFCACHE_OFFSET_KEY_SCHEMA_ID,
           sizeof(contract->key_schema_id.bytes));
    memcpy(contract->value_schema_id.bytes,
           artifact + LAPLACE_PERFCACHE_OFFSET_VALUE_SCHEMA_ID,
           sizeof(contract->value_schema_id.bytes));
    memcpy(contract->activation_epoch_id.bytes,
           artifact + LAPLACE_PERFCACHE_OFFSET_ACTIVATION_EPOCH_ID,
           sizeof(contract->activation_epoch_id.bytes));
    memcpy(contract->source_fingerprint.bytes,
           artifact + LAPLACE_PERFCACHE_OFFSET_SOURCE_FINGERPRINT,
           sizeof(contract->source_fingerprint.bytes));
    memcpy(contract->recipe_fingerprint.bytes,
           artifact + LAPLACE_PERFCACHE_OFFSET_RECIPE_FINGERPRINT,
           sizeof(contract->recipe_fingerprint.bytes));
    memcpy(contract->dependency_fingerprint.bytes,
           artifact + LAPLACE_PERFCACHE_OFFSET_DEPENDENCY_FINGERPRINT,
           sizeof(contract->dependency_fingerprint.bytes));
    contract->key_bytes = read_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_KEY_BYTES);
    contract->value_bytes = read_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_VALUE_BYTES);
    contract->access_law = read_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_ACCESS_LAW);
    contract->flags = read_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_FLAGS);
}

laplace_perfcache_status laplace_perfcache_validate(
    const uint8_t* artifact,
    size_t artifact_bytes,
    const laplace_perfcache_contract* expected_contract,
    laplace_perfcache_view* view) {
    laplace_perfcache_contract actual_contract;
    uint64_t record_count;
    uint32_t record_stride;
    uint64_t records_offset;
    uint64_t records_bytes;
    uint64_t metadata_offset;
    uint64_t metadata_bytes;
    uint64_t digest_offset;
    uint64_t expected_records_bytes;
    uint64_t expected_metadata_offset;
    uint64_t expected_digest_offset;
    uint8_t digest[LAPLACE_PERFCACHE_DIGEST_BYTES];
    blake3_hasher hasher;

    if (artifact == NULL || expected_contract == NULL || view == NULL ||
        !contract_valid(expected_contract)) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    if (artifact_bytes <
        (size_t)LAPLACE_PERFCACHE_HEADER_BYTES + LAPLACE_PERFCACHE_DIGEST_BYTES) {
        return LAPLACE_PERFCACHE_SECTION_INVALID;
    }
    if (memcmp(artifact, laplace_perfcache_magic, sizeof(laplace_perfcache_magic)) != 0) {
        return LAPLACE_PERFCACHE_BAD_MAGIC;
    }
    if (read_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_VERSION) !=
        LAPLACE_PERFCACHE_FORMAT_VERSION) {
        return LAPLACE_PERFCACHE_UNSUPPORTED_VERSION;
    }
    if (read_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_HEADER_BYTES) !=
            LAPLACE_PERFCACHE_HEADER_BYTES ||
        !bytes_zero(artifact + 248u, 8u)) {
        return LAPLACE_PERFCACHE_HEADER_INVALID;
    }
    read_contract(artifact, &actual_contract);
    if (!contract_valid(&actual_contract)) {
        return LAPLACE_PERFCACHE_HEADER_INVALID;
    }
    if (!contract_equal(&actual_contract, expected_contract)) {
        return LAPLACE_PERFCACHE_CONTRACT_MISMATCH;
    }

    record_count = read_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORD_COUNT);
    record_stride = read_u32_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORD_STRIDE);
    records_offset = read_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORDS);
    records_bytes = read_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_RECORDS_BYTES);
    metadata_offset = read_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_METADATA);
    metadata_bytes = read_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_METADATA_BYTES);
    digest_offset = read_u64_le(artifact + LAPLACE_PERFCACHE_OFFSET_DIGEST);

    if (record_stride != actual_contract.key_bytes + actual_contract.value_bytes ||
        record_count > UINT64_MAX / record_stride) {
        return LAPLACE_PERFCACHE_SECTION_INVALID;
    }
    expected_records_bytes = record_count * record_stride;
    if (UINT64_MAX - LAPLACE_PERFCACHE_HEADER_BYTES < expected_records_bytes) {
        return LAPLACE_PERFCACHE_SECTION_INVALID;
    }
    expected_metadata_offset = LAPLACE_PERFCACHE_HEADER_BYTES + expected_records_bytes;
    if (UINT64_MAX - expected_metadata_offset < metadata_bytes) {
        return LAPLACE_PERFCACHE_SECTION_INVALID;
    }
    expected_digest_offset = expected_metadata_offset + metadata_bytes;
    if (UINT64_MAX - expected_digest_offset < LAPLACE_PERFCACHE_DIGEST_BYTES ||
        expected_digest_offset + LAPLACE_PERFCACHE_DIGEST_BYTES != artifact_bytes ||
        records_offset != LAPLACE_PERFCACHE_HEADER_BYTES ||
        records_bytes != expected_records_bytes ||
        metadata_offset != expected_metadata_offset ||
        digest_offset != expected_digest_offset) {
        return LAPLACE_PERFCACHE_SECTION_INVALID;
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, artifact, (size_t)digest_offset);
    blake3_hasher_finalize(&hasher, digest, sizeof(digest));
    if (memcmp(digest,
               artifact + (size_t)digest_offset,
               LAPLACE_PERFCACHE_DIGEST_BYTES) != 0) {
        return LAPLACE_PERFCACHE_DIGEST_MISMATCH;
    }
    {
        const laplace_perfcache_status access_status = records_access_law_valid(
            artifact + (size_t)records_offset,
            record_count,
            &actual_contract,
            record_stride);
        if (access_status != LAPLACE_PERFCACHE_OK) {
            return access_status;
        }
    }

    memset(view, 0, sizeof(*view));
    view->contract = actual_contract;
    view->artifact = artifact;
    view->artifact_bytes = artifact_bytes;
    view->records = artifact + (size_t)records_offset;
    view->record_count = record_count;
    view->record_stride = record_stride;
    view->metadata = artifact + (size_t)metadata_offset;
    view->metadata_bytes = metadata_bytes;
    memcpy(view->artifact_digest.bytes,
           artifact + (size_t)digest_offset,
           sizeof(view->artifact_digest.bytes));
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status laplace_perfcache_lookup_batch(
    const laplace_perfcache_view* view,
    const uint8_t* keys,
    size_t key_count,
    uint64_t* record_indexes,
    uint8_t* found) {
    size_t key_index;

    if (view == NULL || (key_count != 0u &&
        (keys == NULL || record_indexes == NULL || found == NULL))) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    if (!contract_valid(&view->contract) ||
        view->record_stride != view->contract.key_bytes + view->contract.value_bytes) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    if (view->contract.access_law == LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED) {
        return LAPLACE_PERFCACHE_LOOKUP_UNSUPPORTED;
    }
    if (key_count != 0u &&
        key_count > SIZE_MAX / view->contract.key_bytes) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    for (key_index = 0; key_index < key_count; ++key_index) {
        const uint8_t* key = keys + key_index * view->contract.key_bytes;
        if (view->contract.access_law ==
            LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED) {
            const uint64_t index = read_u32_le(key);
            if (index < view->record_count) {
                found[key_index] = 1u;
                record_indexes[key_index] = index;
            } else {
                found[key_index] = 0u;
                record_indexes[key_index] = UINT64_MAX;
            }
            continue;
        }
        if (view->contract.access_law !=
            LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED) {
            return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
        }
        uint64_t low = 0;
        uint64_t high = view->record_count;
        while (low < high) {
            const uint64_t middle = low + (high - low) / 2u;
            const uint8_t* record = view->records +
                (size_t)middle * view->record_stride;
            const int comparison = memcmp(key, record, view->contract.key_bytes);
            if (comparison > 0) {
                low = middle + 1u;
            } else {
                high = middle;
            }
        }
        if (low < view->record_count &&
            memcmp(key,
                   view->records + (size_t)low * view->record_stride,
                   view->contract.key_bytes) == 0) {
            found[key_index] = 1u;
            record_indexes[key_index] = low;
        } else {
            found[key_index] = 0u;
            record_indexes[key_index] = UINT64_MAX;
        }
    }
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status laplace_perfcache_validate_records(
    const laplace_perfcache_view* view,
    laplace_perfcache_record_validator validator,
    void* context,
    uint64_t* invalid_record_index) {
    uint64_t record_index;

    if (view == NULL || validator == NULL || invalid_record_index == NULL ||
        !contract_valid(&view->contract) ||
        view->record_stride != view->contract.key_bytes + view->contract.value_bytes) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    *invalid_record_index = UINT64_MAX;
    for (record_index = 0; record_index < view->record_count; ++record_index) {
        const uint8_t* record = view->records +
            (size_t)record_index * view->record_stride;
        const laplace_perfcache_status status = validator(
            context, record_index, record, view->record_stride);
        if (status != LAPLACE_PERFCACHE_OK) {
            *invalid_record_index = record_index;
            return status;
        }
    }
    return LAPLACE_PERFCACHE_OK;
}
