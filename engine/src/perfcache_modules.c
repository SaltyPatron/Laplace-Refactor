#include "laplace/perfcache_modules.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "laplace/contract/perfcache.h"
#include "laplace/unicode_root.h"

_Static_assert(
    sizeof(laplace_unicode_identity_key) ==
        LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES,
    "Unicode reverse keys must be one exact content identity plus witness");

static int hex_nibble(char value, uint8_t* nibble) {
    if (value >= '0' && value <= '9') {
        *nibble = (uint8_t)(value - '0');
        return 1;
    }
    if (value >= 'a' && value <= 'f') {
        *nibble = (uint8_t)(value - 'a' + 10);
        return 1;
    }
    return 0;
}

static int decode_hex(const char* source, uint8_t* output, size_t bytes) {
    size_t index;
    for (index = 0u; index < bytes; ++index) {
        uint8_t high;
        uint8_t low;
        if (!hex_nibble(source[index * 2u], &high) ||
            !hex_nibble(source[index * 2u + 1u], &low)) {
            return 0;
        }
        output[index] = (uint8_t)((high << 4u) | low);
    }
    return source[bytes * 2u] == '\0';
}

static laplace_perfcache_status validate_framework_probe_record(
    void* context,
    uint64_t record_index,
    const uint8_t* record,
    uint32_t record_stride) {
    uint32_t key;
    (void)context;
    if (record == NULL ||
        record_stride != LAPLACE_PERFCACHE_FRAMEWORK_PROBE_KEY_BYTES +
            LAPLACE_PERFCACHE_FRAMEWORK_PROBE_VALUE_BYTES) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    key = (uint32_t)record[0] |
        ((uint32_t)record[1] << 8u) |
        ((uint32_t)record[2] << 16u) |
        ((uint32_t)record[3] << 24u);
    return key == record_index
        ? LAPLACE_PERFCACHE_OK
        : LAPLACE_PERFCACHE_DENSE_KEY_MISMATCH;
}

static laplace_perfcache_status validate_framework_probe_view(
    void* context,
    const laplace_perfcache_view* view,
    uint64_t* invalid_record_index) {
    uint64_t record_index;
    (void)context;
    if (view == NULL || invalid_record_index == NULL ||
        view->records == NULL || view->record_count == 0u) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    *invalid_record_index = UINT64_MAX;
    for (record_index = 0u; record_index < view->record_count; ++record_index) {
        const uint8_t* record = view->records +
            (size_t)record_index * view->record_stride;
        if (validate_framework_probe_record(
                NULL, record_index, record, view->record_stride) !=
            LAPLACE_PERFCACHE_OK) {
            *invalid_record_index = record_index;
            return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
        }
    }
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_registry_status laplace_perfcache_framework_probe_module(
    laplace_perfcache_module_v2* module) {
    if (module == NULL) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    memset(module, 0, sizeof(*module));
    if (!decode_hex(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_MODULE_ID_HEX,
                    module->module_id.bytes,
                    sizeof(module->module_id.bytes)) ||
        !decode_hex(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_KEY_SCHEMA_ID_HEX,
                    module->key_schema_id.bytes,
                    sizeof(module->key_schema_id.bytes)) ||
        !decode_hex(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_VALUE_SCHEMA_ID_HEX,
                    module->value_schema_id.bytes,
                    sizeof(module->value_schema_id.bytes)) ||
        !decode_hex(
            LAPLACE_PERFCACHE_FRAMEWORK_PROBE_CONTRACT_FINGERPRINT_HEX,
            module->module_contract_fingerprint.bytes,
            sizeof(module->module_contract_fingerprint.bytes))) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
    module->validate_record = validate_framework_probe_record;
    module->validate_view = validate_framework_probe_view;
    module->access_law = LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED;
    module->key_bytes = LAPLACE_PERFCACHE_FRAMEWORK_PROBE_KEY_BYTES;
    module->value_bytes = LAPLACE_PERFCACHE_FRAMEWORK_PROBE_VALUE_BYTES;
    module->flags = LAPLACE_PERFCACHE_MODULE_REQUIRED;
    module->abi_major = LAPLACE_PERFCACHE_MODULE_ABI_MAJOR;
    module->abi_minor = LAPLACE_PERFCACHE_MODULE_ABI_MINOR;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

static uint32_t read_u32le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static uint64_t read_u64le(const uint8_t* bytes) {
    uint64_t value = 0u;
    size_t index;
    for (index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

static laplace_perfcache_status validate_unicode_tier0_record(
    void* context,
    uint64_t record_index,
    const uint8_t* record,
    uint32_t record_stride) {
    const uint8_t* value;
    uint8_t expected_lup[4] = {0u, 0u, 0u, 0u};
    size_t expected_lup_length = 0u;
    laplace_id128 expected_id;
    laplace_digest256 expected_witness;
    uint32_t axes[4];
    uint8_t expected_hilbert[LAPLACE_UNICODE_HILBERT_KEY_BYTES];
    size_t axis;
    uint32_t key;
    (void)context;
    if (record == NULL ||
        record_stride != LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_BYTES +
            LAPLACE_PERFCACHE_UNICODE_TIER0_VALUE_BYTES ||
        record_index >= LAPLACE_PERFCACHE_UNICODE_TIER0_POPULATION) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    key = read_u32le(record);
    value = record + LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_BYTES;
    if (key != record_index || read_u32le(value + 8u) < LAPLACE_UNICODE_ATOM_HEADER_BYTES ||
        read_u32le(value + 12u) >= LAPLACE_UNICODE_ROOT_POPULATION ||
        value[16] > LAPLACE_UNICODE_SURROGATE_LUP_ADDRESS ||
        value[17] == 0u || value[17] > 4u ||
        value[18] != 0u || value[19] != 0u ||
        laplace_unicode_position_encode(key, expected_lup, &expected_lup_length) !=
            LAPLACE_IDENTITY_OK ||
        value[17] != expected_lup_length ||
        memcmp(value + 20u, expected_lup, 4u) != 0 ||
        laplace_identity_codepoint_witness(key, &expected_id, &expected_witness) !=
            LAPLACE_IDENTITY_OK ||
        memcmp(value + 24u, expected_id.bytes, 16u) != 0 ||
        memcmp(value + 40u, expected_witness.bytes, 32u) != 0) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    for (axis = 0u; axis < 4u; ++axis) {
        const uint64_t bits = read_u64le(value + 72u + axis * 8u);
        double component;
        memcpy(&component, &bits, sizeof(component));
        if (laplace_unicode_quantize_component_u32(component, &axes[axis]) !=
            LAPLACE_UNICODE_OK) {
            return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
        }
    }
    if (laplace_unicode_hilbert4_encode(axes, expected_hilbert) !=
            LAPLACE_UNICODE_OK ||
        memcmp(value + 104u, expected_hilbert, sizeof(expected_hilbert)) != 0) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    return LAPLACE_PERFCACHE_OK;
}

static int module_contract_matches(
    const laplace_perfcache_contract* contract,
    const laplace_perfcache_module_v2* module) {
    return memcmp(
               contract->module_id.bytes, module->module_id.bytes,
               sizeof(contract->module_id.bytes)) == 0 &&
        memcmp(
               contract->key_schema_id.bytes, module->key_schema_id.bytes,
               sizeof(contract->key_schema_id.bytes)) == 0 &&
        memcmp(
               contract->value_schema_id.bytes, module->value_schema_id.bytes,
               sizeof(contract->value_schema_id.bytes)) == 0 &&
        memcmp(
               contract->module_contract_fingerprint.bytes,
               module->module_contract_fingerprint.bytes,
               sizeof(contract->module_contract_fingerprint.bytes)) == 0 &&
        contract->key_bytes == module->key_bytes &&
        contract->value_bytes == module->value_bytes &&
        contract->access_law == module->access_law;
}

laplace_perfcache_status laplace_perfcache_unicode_tier0_validate_view(
    void* context,
    const laplace_perfcache_view* view,
    uint64_t* invalid_record_index) {
    laplace_perfcache_module_v2 module;
    uint64_t expected_metadata_offset = 0u;
    uint64_t record_index;
    (void)context;
    if (view == NULL || invalid_record_index == NULL ||
        view->records == NULL || view->metadata == NULL ||
#if !defined(LAPLACE_TEST_ALLOW_UNICODE_TIER0_PARTIAL_VIEW)
        view->record_count != LAPLACE_PERFCACHE_UNICODE_TIER0_POPULATION ||
#else
        view->record_count == 0u ||
#endif
        laplace_perfcache_unicode_tier0_module(&module) !=
            LAPLACE_PERFCACHE_REGISTRY_OK ||
        !module_contract_matches(&view->contract, &module) ||
        view->record_stride != module.key_bytes + module.value_bytes) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    *invalid_record_index = UINT64_MAX;
    for (record_index = 0u; record_index < view->record_count;
         ++record_index) {
        const uint8_t* record = view->records +
            (size_t)record_index * view->record_stride;
        const uint8_t* value = record + module.key_bytes;
        const uint64_t metadata_offset = read_u64le(value);
        const uint32_t metadata_bytes = read_u32le(value + 8u);
        laplace_unicode_atom_record_view atom;
        size_t consumed = 0u;
        size_t axis;
        if (validate_unicode_tier0_record(
                NULL, record_index, record, view->record_stride) !=
                LAPLACE_PERFCACHE_OK ||
            metadata_offset != expected_metadata_offset ||
            metadata_offset > view->metadata_bytes ||
            (uint64_t)metadata_bytes >
                view->metadata_bytes - metadata_offset ||
            laplace_unicode_atom_record_open(
                view->metadata + metadata_offset, metadata_bytes,
                &atom, &consumed) != LAPLACE_UNICODE_OK ||
            consumed != metadata_bytes ||
            atom.value.codepoint_position != record_index ||
            atom.value.placement_rank != read_u32le(value + 12u) ||
            atom.value.position_class != value[16] ||
            atom.value.lup_v1_length != value[17] ||
            memcmp(atom.value.lup_v1_bytes, value + 20u, 4u) != 0 ||
            memcmp(atom.value.content_id.bytes, value + 24u, 16u) != 0 ||
            memcmp(
                atom.value.identity_preimage_fingerprint.bytes,
                value + 40u, 32u) != 0 ||
            memcmp(atom.value.hilbert_key, value + 104u, 16u) != 0 ||
            memcmp(atom.value.physicality_id.bytes, value + 120u, 32u) != 0) {
            *invalid_record_index = record_index;
            return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
        }
        for (axis = 0u; axis < 4u; ++axis) {
            uint64_t atom_bits = 0u;
            memcpy(
                &atom_bits, &atom.value.coordinate.component[axis],
                sizeof(atom_bits));
            if (atom_bits != read_u64le(value + 72u + axis * 8u)) {
                *invalid_record_index = record_index;
                return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
            }
        }
        expected_metadata_offset += metadata_bytes;
    }
    if (expected_metadata_offset != view->metadata_bytes) {
        *invalid_record_index = view->record_count;
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_registry_status laplace_perfcache_unicode_tier0_module(
    laplace_perfcache_module_v2* module) {
    if (module == NULL) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    memset(module, 0, sizeof(*module));
    if (!decode_hex(LAPLACE_PERFCACHE_UNICODE_TIER0_MODULE_ID_HEX,
                    module->module_id.bytes, sizeof(module->module_id.bytes)) ||
        !decode_hex(LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_SCHEMA_ID_HEX,
                    module->key_schema_id.bytes,
                    sizeof(module->key_schema_id.bytes)) ||
        !decode_hex(LAPLACE_PERFCACHE_UNICODE_TIER0_VALUE_SCHEMA_ID_HEX,
                    module->value_schema_id.bytes,
                    sizeof(module->value_schema_id.bytes)) ||
        !decode_hex(LAPLACE_PERFCACHE_UNICODE_TIER0_CONTRACT_FINGERPRINT_HEX,
                    module->module_contract_fingerprint.bytes,
                    sizeof(module->module_contract_fingerprint.bytes))) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
    module->validate_record = validate_unicode_tier0_record;
    module->validate_view = laplace_perfcache_unicode_tier0_validate_view;
    module->access_law = LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED;
    module->key_bytes = LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_BYTES;
    module->value_bytes = LAPLACE_PERFCACHE_UNICODE_TIER0_VALUE_BYTES;
    module->flags = LAPLACE_PERFCACHE_MODULE_REQUIRED;
    module->abi_major = LAPLACE_PERFCACHE_MODULE_ABI_MAJOR;
    module->abi_minor = LAPLACE_PERFCACHE_MODULE_ABI_MINOR;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

static laplace_perfcache_status validate_unicode_identity_reverse_record(
    void* context,
    uint64_t record_index,
    const uint8_t* record,
    uint32_t record_stride) {
    laplace_id128 expected_id;
    laplace_digest256 expected_witness;
    const uint8_t* value;
    uint32_t position;
    uint8_t occupied;
    size_t byte_index;
    (void)context;
    if (record == NULL ||
        record_stride != LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES +
            LAPLACE_PERFCACHE_UNICODE_REVERSE_VALUE_BYTES ||
        record_index >= LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    value = record + LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES;
    position = read_u32le(value);
    occupied = value[4];
    if (occupied == 0u) {
        for (byte_index = 0u; byte_index < record_stride; ++byte_index) {
            if (record[byte_index] != 0u) {
                return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
            }
        }
        return LAPLACE_PERFCACHE_OK;
    }
    if (occupied != 1u || value[5] != 0u || value[6] != 0u ||
        value[7] != 0u) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    if (position >= LAPLACE_PERFCACHE_UNICODE_REVERSE_POPULATION ||
        laplace_identity_codepoint_witness(
            position, &expected_id, &expected_witness) != LAPLACE_IDENTITY_OK ||
        memcmp(record, expected_id.bytes, sizeof(expected_id.bytes)) != 0 ||
        memcmp(record + sizeof(expected_id.bytes), expected_witness.bytes,
               sizeof(expected_witness.bytes)) != 0) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    return LAPLACE_PERFCACHE_OK;
}

static uint64_t unicode_identity_reverse_home(const uint8_t* key) {
    return read_u64le(key + sizeof(laplace_id128)) &
        (LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY - 1u);
}

static laplace_perfcache_status unicode_identity_reverse_find(
    const laplace_perfcache_view* view,
    const uint8_t* key,
    uint64_t* record_index,
    uint8_t* found) {
    uint64_t slot;
    uint64_t probes;
    if (view == NULL || key == NULL || record_index == NULL || found == NULL ||
        view->record_count != LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY ||
        view->record_stride != LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES +
            LAPLACE_PERFCACHE_UNICODE_REVERSE_VALUE_BYTES) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    slot = unicode_identity_reverse_home(key);
    *found = 0u;
    *record_index = UINT64_MAX;
    for (probes = 0u;
         probes < LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY; ++probes) {
        const uint8_t* record = view->records +
            (size_t)slot * view->record_stride;
        const uint8_t occupied =
            record[LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES + 4u];
        if (occupied == 0u) {
            return LAPLACE_PERFCACHE_OK;
        }
        if (occupied != 1u) {
            return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
        }
        if (memcmp(
                record, key,
                LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES) == 0) {
            *found = 1u;
            *record_index = slot;
            return LAPLACE_PERFCACHE_OK;
        }
        slot = (slot + 1u) &
            (LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY - 1u);
    }
    return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
}

static laplace_perfcache_status unicode_identity_reverse_lookup(
    void* context,
    const laplace_perfcache_view* view,
    const uint8_t* keys,
    size_t key_count,
    uint64_t* record_indexes,
    uint8_t* found) {
    size_t key_index;
    (void)context;
    if (view == NULL ||
        (key_count != 0u &&
         (keys == NULL || record_indexes == NULL || found == NULL)) ||
        view->record_count != LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY ||
        view->record_stride != LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES +
            LAPLACE_PERFCACHE_UNICODE_REVERSE_VALUE_BYTES) {
        return LAPLACE_PERFCACHE_INVALID_ARGUMENT;
    }
    for (key_index = 0u; key_index < key_count; ++key_index) {
        const uint8_t* key = keys +
            key_index * LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES;
        const laplace_perfcache_status status =
            unicode_identity_reverse_find(
                view, key, &record_indexes[key_index], &found[key_index]);
        if (status != LAPLACE_PERFCACHE_OK) {
            return status;
        }
    }
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_status
laplace_perfcache_unicode_identity_reverse_validate_view(
    void* context,
    const laplace_perfcache_view* view,
    uint64_t* invalid_record_index) {
    laplace_perfcache_module_v2 module;
    uint64_t record_index;
    (void)context;
    if (view == NULL || invalid_record_index == NULL ||
        view->records == NULL || view->metadata_bytes != 0u ||
        view->record_count != LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY ||
        laplace_perfcache_unicode_identity_reverse_module(&module) !=
            LAPLACE_PERFCACHE_REGISTRY_OK ||
        !module_contract_matches(&view->contract, &module) ||
        view->record_stride != module.key_bytes + module.value_bytes) {
        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
    }
    *invalid_record_index = UINT64_MAX;
    {
        uint64_t occupied_count = 0u;
        for (record_index = 0u; record_index < view->record_count;
             ++record_index) {
            const uint8_t* record = view->records +
                (size_t)record_index * view->record_stride;
            const uint8_t occupied = record[module.key_bytes + 4u];
            if (validate_unicode_identity_reverse_record(
                    NULL, record_index, record, view->record_stride) !=
                    LAPLACE_PERFCACHE_OK) {
                *invalid_record_index = record_index;
                return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
            }
            if (occupied != 0u) {
                uint64_t slot = unicode_identity_reverse_home(record);
                uint64_t probes;
                ++occupied_count;
                for (probes = 0u;
                     probes < LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY;
                     ++probes) {
                    const uint8_t* candidate = view->records +
                        (size_t)slot * view->record_stride;
                    if (candidate[module.key_bytes + 4u] == 0u ||
                        (slot != record_index &&
                         memcmp(candidate, record, module.key_bytes) == 0)) {
                        *invalid_record_index = record_index;
                        return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
                    }
                    if (slot == record_index) {
                        break;
                    }
                    slot = (slot + 1u) &
                        (LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY - 1u);
                }
                if (probes == LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY) {
                    *invalid_record_index = record_index;
                    return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
                }
            }
        }
        if (occupied_count != LAPLACE_PERFCACHE_UNICODE_REVERSE_POPULATION) {
            *invalid_record_index = view->record_count;
            return LAPLACE_PERFCACHE_SEMANTIC_MISMATCH;
        }
    }
    return LAPLACE_PERFCACHE_OK;
}

laplace_perfcache_registry_status
laplace_perfcache_unicode_identity_reverse_module(
    laplace_perfcache_module_v2* module) {
    if (module == NULL) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    memset(module, 0, sizeof(*module));
    if (!decode_hex(LAPLACE_PERFCACHE_UNICODE_REVERSE_MODULE_ID_HEX,
                    module->module_id.bytes, sizeof(module->module_id.bytes)) ||
        !decode_hex(LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_SCHEMA_ID_HEX,
                    module->key_schema_id.bytes,
                    sizeof(module->key_schema_id.bytes)) ||
        !decode_hex(LAPLACE_PERFCACHE_UNICODE_REVERSE_VALUE_SCHEMA_ID_HEX,
                    module->value_schema_id.bytes,
                    sizeof(module->value_schema_id.bytes)) ||
        !decode_hex(
            LAPLACE_PERFCACHE_UNICODE_REVERSE_CONTRACT_FINGERPRINT_HEX,
            module->module_contract_fingerprint.bytes,
            sizeof(module->module_contract_fingerprint.bytes))) {
        return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
    }
    module->validate_record = validate_unicode_identity_reverse_record;
    module->validate_view =
        laplace_perfcache_unicode_identity_reverse_validate_view;
    module->lookup_batch = unicode_identity_reverse_lookup;
#if defined(LAPLACE_TEST_UNICODE_REVERSE_SORTED_SUBSTITUTION)
    module->access_law = LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED;
#else
    module->access_law = LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED;
#endif
    module->key_bytes = LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES;
    module->value_bytes = LAPLACE_PERFCACHE_UNICODE_REVERSE_VALUE_BYTES;
    module->flags = LAPLACE_PERFCACHE_MODULE_REQUIRED;
    module->abi_major = LAPLACE_PERFCACHE_MODULE_ABI_MAJOR;
    module->abi_minor = LAPLACE_PERFCACHE_MODULE_ABI_MINOR;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

laplace_perfcache_registry_status
laplace_perfcache_unicode_tier0_resolve_batch(
    const laplace_perfcache_pin* pin,
    const uint32_t* codepoint_positions,
    size_t item_count,
    laplace_unicode_atom_record_view* atoms,
    uint8_t* found) {
    laplace_perfcache_module_v2 module;
    const laplace_perfcache_view* view = NULL;
    size_t item_index;
    if (pin == NULL ||
        (item_count != 0u &&
         (codepoint_positions == NULL || atoms == NULL || found == NULL)) ||
        laplace_perfcache_unicode_tier0_module(&module) !=
            LAPLACE_PERFCACHE_REGISTRY_OK ||
        laplace_perfcache_pin_view(pin, &module.module_id, &view) !=
            LAPLACE_PERFCACHE_REGISTRY_OK || view == NULL ||
        !module_contract_matches(&view->contract, &module)) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    for (item_index = 0u; item_index < item_count; ++item_index) {
        const uint32_t position = codepoint_positions[item_index];
        memset(&atoms[item_index], 0, sizeof(atoms[item_index]));
        found[item_index] = 0u;
        if (position < LAPLACE_PERFCACHE_UNICODE_TIER0_POPULATION) {
            const uint8_t* record = view->records +
                (size_t)position * view->record_stride;
            const uint8_t* value = record + module.key_bytes;
            const uint64_t metadata_offset = read_u64le(value);
            const uint32_t metadata_bytes = read_u32le(value + 8u);
            size_t consumed = 0u;
            if (metadata_offset > view->metadata_bytes ||
                (uint64_t)metadata_bytes >
                    view->metadata_bytes - metadata_offset ||
                laplace_unicode_atom_record_open(
                    view->metadata + metadata_offset, metadata_bytes,
                    &atoms[item_index], &consumed) != LAPLACE_UNICODE_OK ||
                consumed != metadata_bytes ||
                atoms[item_index].value.codepoint_position != position) {
                return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
            }
            found[item_index] = 1u;
        }
    }
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

laplace_perfcache_registry_status
laplace_perfcache_unicode_identity_reverse_resolve_batch(
    const laplace_perfcache_pin* pin,
    const laplace_unicode_identity_key* identities,
    size_t item_count,
    uint32_t* codepoint_positions,
    uint8_t* found) {
    laplace_perfcache_module_v2 module;
    const laplace_perfcache_view* view = NULL;
    size_t item_index;
    if (pin == NULL || sizeof(laplace_unicode_identity_key) !=
            LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES ||
        (item_count != 0u &&
         (identities == NULL || codepoint_positions == NULL || found == NULL)) ||
        laplace_perfcache_unicode_identity_reverse_module(&module) !=
            LAPLACE_PERFCACHE_REGISTRY_OK ||
        laplace_perfcache_pin_view(pin, &module.module_id, &view) !=
            LAPLACE_PERFCACHE_REGISTRY_OK || view == NULL ||
        !module_contract_matches(&view->contract, &module)) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    for (item_index = 0u; item_index < item_count; ++item_index) {
        uint64_t record_index = UINT64_MAX;
        const laplace_perfcache_status status =
            unicode_identity_reverse_find(
                view, (const uint8_t*)&identities[item_index],
                &record_index, &found[item_index]);
        codepoint_positions[item_index] = UINT32_MAX;
        if (status != LAPLACE_PERFCACHE_OK) {
            return LAPLACE_PERFCACHE_REGISTRY_INTERNAL_ERROR;
        }
        if (found[item_index] != 0u) {
            const uint8_t* record = view->records +
                (size_t)record_index * view->record_stride;
            codepoint_positions[item_index] = read_u32le(
                record + module.key_bytes);
        }
    }
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}

laplace_perfcache_registry_status laplace_perfcache_builtin_module_resolve(
    const laplace_perfcache_contract* contract,
    laplace_perfcache_module_v2* module) {
    laplace_perfcache_module_v2 candidate;
    laplace_perfcache_registry_status status;
    if (contract == NULL || module == NULL) {
        return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
    }
    status = laplace_perfcache_framework_probe_module(&candidate);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return status;
    }
    if (memcmp(
            contract->module_id.bytes, candidate.module_id.bytes,
            sizeof(contract->module_id.bytes)) == 0) {
        if (!module_contract_matches(contract, &candidate)) {
            return LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH;
        }
        *module = candidate;
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    }
    status = laplace_perfcache_unicode_identity_reverse_module(&candidate);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return status;
    }
    if (memcmp(
            contract->module_id.bytes, candidate.module_id.bytes,
            sizeof(contract->module_id.bytes)) == 0) {
        if (!module_contract_matches(contract, &candidate)) {
            return LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH;
        }
        *module = candidate;
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    }
    status = laplace_perfcache_unicode_tier0_module(&candidate);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return status;
    }
    if (memcmp(
            contract->module_id.bytes, candidate.module_id.bytes,
            sizeof(contract->module_id.bytes)) == 0) {
        if (!module_contract_matches(contract, &candidate)) {
            return LAPLACE_PERFCACHE_REGISTRY_MODULE_SET_MISMATCH;
        }
        *module = candidate;
        return LAPLACE_PERFCACHE_REGISTRY_OK;
    }
    memset(module, 0, sizeof(*module));
    return LAPLACE_PERFCACHE_REGISTRY_MODULE_NOT_FOUND;
}
