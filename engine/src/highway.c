#include "laplace/highway.h"

#include <limits.h>
#include <string.h>

#include "blake3.h"

_Static_assert(sizeof(laplace_highway_key) == 80u,
               "highway key ABI must be exactly 80 bytes");
_Static_assert(sizeof(laplace_highway_coordinate) == 64u,
               "highway coordinate ABI must be exactly 64 bytes");
_Static_assert(sizeof(laplace_highway_registry_receipt) == 184u,
               "highway registry receipt ABI must be exactly 184 bytes");

#define LAPLACE_HIGHWAY_KIND_ROW(identifier, name_value, introduced_value, retired_value) \
    {identifier, name_value, introduced_value, retired_value},
static const laplace_highway_kind_contract_row registry_kinds[] = {
    LAPLACE_HIGHWAY_KIND_CONTRACT_REGISTRY(LAPLACE_HIGHWAY_KIND_ROW)
};
#undef LAPLACE_HIGHWAY_KIND_ROW

#if LAPLACE_HIGHWAY_ALIAS_COUNT > 0u
#define LAPLACE_HIGHWAY_ALIAS_ROW(kind_value, name_value, introduced_value, retired_value) \
    {kind_value, name_value, introduced_value, retired_value},
static const laplace_highway_alias_contract_row registry_aliases[] = {
    LAPLACE_HIGHWAY_ALIAS_CONTRACT_REGISTRY(LAPLACE_HIGHWAY_ALIAS_ROW)
};
#undef LAPLACE_HIGHWAY_ALIAS_ROW
#endif

#define LAPLACE_HIGHWAY_DISPOSITION_ROW(identifier, name_value) \
    {identifier, name_value},
static const laplace_highway_disposition_contract_row registry_dispositions[] = {
    LAPLACE_HIGHWAY_DISPOSITION_CONTRACT_REGISTRY(
        LAPLACE_HIGHWAY_DISPOSITION_ROW)
};
#undef LAPLACE_HIGHWAY_DISPOSITION_ROW

_Static_assert(
    sizeof(registry_kinds) / sizeof(registry_kinds[0]) ==
        LAPLACE_HIGHWAY_KIND_COUNT,
    "generated highway kind registry count differs");
_Static_assert(
    sizeof(registry_dispositions) / sizeof(registry_dispositions[0]) ==
        LAPLACE_HIGHWAY_DISPOSITION_COUNT,
    "generated highway disposition registry count differs");

static int id_is_zero(const laplace_id128* value) {
    size_t index;
    uint8_t aggregate = 0u;
    for (index = 0; index < sizeof(value->bytes); ++index) {
        aggregate = (uint8_t)(aggregate | value->bytes[index]);
    }
    return aggregate == 0u;
}

static int digest_is_zero(const laplace_digest256* value) {
    size_t index;
    uint8_t aggregate = 0u;
    for (index = 0u; index < sizeof(value->bytes); ++index) {
        aggregate = (uint8_t)(aggregate | value->bytes[index]);
    }
    return aggregate == 0u;
}

static int hex_nibble(char value, uint8_t* output) {
    if (value >= '0' && value <= '9') {
        *output = (uint8_t)(value - '0');
        return 1;
    }
    if (value >= 'a' && value <= 'f') {
        *output = (uint8_t)(value - 'a' + 10);
        return 1;
    }
    return 0;
}

static int decode_registry_fingerprint(laplace_digest256* output) {
    static const char value[] = LAPLACE_HIGHWAY_REGISTRY_FINGERPRINT;
    size_t index;
    if (sizeof(value) != sizeof(output->bytes) * 2u + 1u) {
        return 0;
    }
    for (index = 0u; index < sizeof(output->bytes); ++index) {
        uint8_t high;
        uint8_t low;
        if (!hex_nibble(value[index * 2u], &high) ||
            !hex_nibble(value[index * 2u + 1u], &low)) {
            return 0;
        }
        output->bytes[index] = (uint8_t)((high << 4u) | low);
    }
    return 1;
}

int laplace_highway_kind_valid(uint32_t kind) {
    switch (kind) {
#define LAPLACE_HIGHWAY_KIND_CASE(symbol, identifier) case identifier: return 1;
        LAPLACE_HIGHWAY_KIND_REGISTRY(LAPLACE_HIGHWAY_KIND_CASE)
#undef LAPLACE_HIGHWAY_KIND_CASE
        default:
            return 0;
    }
}

static void hash_u32(blake3_hasher* hasher, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24)
    };
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void hash_string(blake3_hasher* hasher, const char* value) {
    const size_t length = strlen(value);
    hash_u64(hasher, (uint64_t)length);
    blake3_hasher_update(hasher, value, length);
}

static void hash_key_payload(blake3_hasher* hasher, const laplace_highway_key* key) {
    hash_u32(hasher, key->kind);
    blake3_hasher_update(hasher, key->authority.bytes, sizeof(key->authority.bytes));
#if !defined(LAPLACE_TEST_HIGHWAY_OMIT_RELEASE_SCOPE)
    blake3_hasher_update(hasher, key->release.bytes, sizeof(key->release.bytes));
#endif
    blake3_hasher_update(hasher, key->name_space.bytes, sizeof(key->name_space.bytes));
    blake3_hasher_update(
        hasher, key->local_identifier.bytes, sizeof(key->local_identifier.bytes));
    hash_u64(hasher, key->version);
}

static laplace_highway_status validate_key(const laplace_highway_key* key) {
    if (key == NULL || key->reserved != 0u) {
        return LAPLACE_HIGHWAY_INVALID_ARGUMENT;
    }
    if (!laplace_highway_kind_valid(key->kind)) {
        return LAPLACE_HIGHWAY_UNKNOWN_KIND;
    }
    if (id_is_zero(&key->authority) || id_is_zero(&key->release) ||
        id_is_zero(&key->name_space) || id_is_zero(&key->local_identifier)) {
        return LAPLACE_HIGHWAY_ZERO_SCOPE;
    }
    if (key->version == 0u) {
        return LAPLACE_HIGHWAY_ZERO_VERSION;
    }
    return LAPLACE_HIGHWAY_OK;
}

laplace_highway_status laplace_highway_coordinate_calculate(
    const laplace_highway_key* key,
    laplace_highway_coordinate* output) {
    static const uint8_t coordinate_domain[] = LAPLACE_HIGHWAY_COORDINATE_DOMAIN;
    static const uint8_t fingerprint_domain[] = LAPLACE_HIGHWAY_FINGERPRINT_DOMAIN;
    laplace_digest256 coordinate_digest;
    blake3_hasher hasher;
    laplace_highway_status status;

    if (output == NULL) {
        return LAPLACE_HIGHWAY_INVALID_ARGUMENT;
    }
    status = validate_key(key);
    if (status != LAPLACE_HIGHWAY_OK) {
        return status;
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, coordinate_domain, sizeof(coordinate_domain) - 1u);
    hash_key_payload(&hasher, key);
    blake3_hasher_finalize(
        &hasher, coordinate_digest.bytes, sizeof(coordinate_digest.bytes));

    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, fingerprint_domain, sizeof(fingerprint_domain) - 1u);
    hash_key_payload(&hasher, key);
    blake3_hasher_finalize(
        &hasher, output->collision_fingerprint.bytes,
        sizeof(output->collision_fingerprint.bytes));

    memcpy(output->coordinate.bytes, coordinate_digest.bytes,
           sizeof(output->coordinate.bytes));
    if (id_is_zero(&output->coordinate)) {
        memset(output, 0, sizeof(*output));
        return LAPLACE_HIGHWAY_ZERO_COORDINATE;
    }
    output->kind = key->kind;
    output->reserved = 0u;
    output->version = key->version;
    return LAPLACE_HIGHWAY_OK;
}

laplace_highway_status laplace_highway_coordinate_calculate_batch(
    const laplace_highway_key* keys,
    size_t key_count,
    laplace_highway_coordinate* outputs) {
    size_t index;
    if ((keys == NULL && key_count != 0u) ||
        (outputs == NULL && key_count != 0u)) {
        return LAPLACE_HIGHWAY_INVALID_ARGUMENT;
    }
    if (key_count > SIZE_MAX / sizeof(*keys) ||
        key_count > SIZE_MAX / sizeof(*outputs)) {
        return LAPLACE_HIGHWAY_COUNT_OVERFLOW;
    }
    for (index = 0; index < key_count; ++index) {
        const laplace_highway_status status = validate_key(&keys[index]);
        if (status != LAPLACE_HIGHWAY_OK) {
            return status;
        }
    }
    for (index = 0; index < key_count; ++index) {
        const laplace_highway_status status =
            laplace_highway_coordinate_calculate(&keys[index], &outputs[index]);
        if (status != LAPLACE_HIGHWAY_OK) {
            return status;
        }
    }
    return LAPLACE_HIGHWAY_OK;
}

const laplace_highway_kind_contract_row* laplace_highway_registry_kinds(
    size_t* count) {
    if (count == NULL) {
        return NULL;
    }
    *count = sizeof(registry_kinds) / sizeof(registry_kinds[0]);
    return registry_kinds;
}

const laplace_highway_alias_contract_row* laplace_highway_registry_aliases(
    size_t* count) {
    if (count == NULL) {
        return NULL;
    }
    *count = LAPLACE_HIGHWAY_ALIAS_COUNT;
#if LAPLACE_HIGHWAY_ALIAS_COUNT > 0u
    return registry_aliases;
#else
    return NULL;
#endif
}

const laplace_highway_disposition_contract_row*
laplace_highway_registry_dispositions(size_t* count) {
    if (count == NULL) {
        return NULL;
    }
    *count = sizeof(registry_dispositions) / sizeof(registry_dispositions[0]);
    return registry_dispositions;
}

laplace_highway_status laplace_highway_registry_materialize(
    const laplace_framework_context* context,
    laplace_highway_registry_receipt* receipt) {
    static const uint8_t epoch_domain[] =
        "laplace-highway-registry-epoch-v1";
    static const uint8_t receipt_domain[] =
        "laplace-highway-registry-receipt-v1";
    laplace_digest256 epoch_fingerprint;
    blake3_hasher hasher;
    size_t index;

    if (context == NULL || receipt == NULL) {
        return LAPLACE_HIGHWAY_INVALID_ARGUMENT;
    }
    memset(receipt, 0, sizeof(*receipt));
    if (laplace_framework_context_fingerprint(
            context, &receipt->context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        return LAPLACE_HIGHWAY_CONTEXT_INVALID;
    }
    if (!decode_registry_fingerprint(&receipt->registry_fingerprint)) {
        memset(receipt, 0, sizeof(*receipt));
        return LAPLACE_HIGHWAY_REGISTRY_INVALID;
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, epoch_domain, sizeof(epoch_domain) - 1u);
    blake3_hasher_update(
        &hasher, receipt->registry_fingerprint.bytes,
        sizeof(receipt->registry_fingerprint.bytes));
    hash_u64(&hasher, LAPLACE_HIGHWAY_REGISTRY_VERSION);
    hash_u64(&hasher, LAPLACE_HIGHWAY_KIND_COUNT);
    for (index = 0u; index < LAPLACE_HIGHWAY_KIND_COUNT; ++index) {
        hash_u32(&hasher, registry_kinds[index].id);
        hash_string(&hasher, registry_kinds[index].name);
        hash_u64(&hasher, registry_kinds[index].introduced);
        hash_u64(&hasher, registry_kinds[index].retired);
    }
    hash_u64(&hasher, LAPLACE_HIGHWAY_ALIAS_COUNT);
#if LAPLACE_HIGHWAY_ALIAS_COUNT > 0u
    for (index = 0u; index < LAPLACE_HIGHWAY_ALIAS_COUNT; ++index) {
        hash_u32(&hasher, registry_aliases[index].kind_id);
        hash_string(&hasher, registry_aliases[index].name);
        hash_u64(&hasher, registry_aliases[index].introduced);
        hash_u64(&hasher, registry_aliases[index].retired);
    }
#endif
    hash_u64(&hasher, LAPLACE_HIGHWAY_DISPOSITION_COUNT);
    for (index = 0u; index < LAPLACE_HIGHWAY_DISPOSITION_COUNT; ++index) {
        hash_u32(&hasher, registry_dispositions[index].id);
        hash_string(&hasher, registry_dispositions[index].name);
    }
    blake3_hasher_finalize(
        &hasher, epoch_fingerprint.bytes, sizeof(epoch_fingerprint.bytes));
    memcpy(receipt->activation_epoch_id.bytes, epoch_fingerprint.bytes,
           sizeof(receipt->activation_epoch_id.bytes));
    receipt->activation_epoch_fingerprint = epoch_fingerprint;
    if (id_is_zero(&receipt->activation_epoch_id) ||
        digest_is_zero(&receipt->activation_epoch_fingerprint)) {
        memset(receipt, 0, sizeof(*receipt));
        return LAPLACE_HIGHWAY_REGISTRY_INVALID;
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, receipt_domain, sizeof(receipt_domain) - 1u);
    blake3_hasher_update(
        &hasher, receipt->context_fingerprint.bytes,
        sizeof(receipt->context_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->registry_fingerprint.bytes,
        sizeof(receipt->registry_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, receipt->activation_epoch_fingerprint.bytes,
        sizeof(receipt->activation_epoch_fingerprint.bytes));
    hash_u64(&hasher, LAPLACE_HIGHWAY_REGISTRY_VERSION);
    hash_u64(&hasher, LAPLACE_HIGHWAY_KIND_COUNT);
    hash_u64(&hasher, LAPLACE_HIGHWAY_ALIAS_COUNT);
    hash_u64(&hasher, LAPLACE_HIGHWAY_DISPOSITION_COUNT);
    blake3_hasher_finalize(
        &hasher, receipt->receipt_id.bytes, sizeof(receipt->receipt_id.bytes));
    if (digest_is_zero(&receipt->receipt_id)) {
        memset(receipt, 0, sizeof(*receipt));
        return LAPLACE_HIGHWAY_REGISTRY_INVALID;
    }
    receipt->registry_version = LAPLACE_HIGHWAY_REGISTRY_VERSION;
    receipt->kind_count = LAPLACE_HIGHWAY_KIND_COUNT;
    receipt->alias_count = LAPLACE_HIGHWAY_ALIAS_COUNT;
    receipt->disposition_count = LAPLACE_HIGHWAY_DISPOSITION_COUNT;
    receipt->status = LAPLACE_HIGHWAY_OK;
    return LAPLACE_HIGHWAY_OK;
}
