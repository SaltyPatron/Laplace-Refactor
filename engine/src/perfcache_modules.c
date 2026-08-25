#include "laplace/perfcache_modules.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "laplace/contract/perfcache.h"
#include "laplace/unicode_root.h"

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

laplace_perfcache_registry_status laplace_perfcache_framework_probe_module(
    laplace_perfcache_module_v1* module) {
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

laplace_perfcache_registry_status laplace_perfcache_unicode_tier0_module(
    laplace_perfcache_module_v1* module) {
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
    module->access_law = LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED;
    module->key_bytes = LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_BYTES;
    module->value_bytes = LAPLACE_PERFCACHE_UNICODE_TIER0_VALUE_BYTES;
    module->flags = LAPLACE_PERFCACHE_MODULE_REQUIRED;
    module->abi_major = LAPLACE_PERFCACHE_MODULE_ABI_MAJOR;
    module->abi_minor = LAPLACE_PERFCACHE_MODULE_ABI_MINOR;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}
