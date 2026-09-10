#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake3.h"
#include "composition_pg.h"
#include "content_admission_pg.h"
#include "unicode_atoms_pg.h"

static void content_hash_u32(blake3_hasher* hasher, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void content_hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void content_hash_double(blake3_hasher* hasher, double value) {
    uint64_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    content_hash_u64(hasher, bits);
}

static int content_resolve_atoms(
    void* opaque,
    const uint32_t* atom_positions,
    size_t atom_count,
    laplace_composition_known_entity* known_entities,
    laplace_digest256* provider_receipt_id) {
    static const char receipt_domain[] =
        "laplace-postgresql-content-atom-resolution-v1";
    laplace_pg_content_atom_provider_state* state =
        (laplace_pg_content_atom_provider_state*)opaque;
    laplace_pg_active_unicode_root active;
    blake3_hasher hasher;
    size_t index;

    if (state == NULL || atom_positions == NULL || atom_count == 0u ||
        known_entities == NULL || provider_receipt_id == NULL) {
        return 1;
    }

    memset(&active, 0, sizeof(active));
    laplace_pg_resolve_active_unicode_atoms(
        &state->context,
        atom_positions,
        atom_count,
        known_entities,
        &active);

    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, receipt_domain, sizeof(receipt_domain) - 1u);
    blake3_hasher_update(
        &hasher,
        state->provider_fingerprint.bytes,
        sizeof(state->provider_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, active.root_receipt.bytes, sizeof(active.root_receipt.bytes));
    blake3_hasher_update(
        &hasher,
        active.activation_epoch_id.bytes,
        sizeof(active.activation_epoch_id.bytes));
    blake3_hasher_update(
        &hasher,
        active.activation_epoch_fingerprint.bytes,
        sizeof(active.activation_epoch_fingerprint.bytes));
    content_hash_u64(&hasher, (uint64_t)atom_count);
    for (index = 0u; index < atom_count; ++index) {
        const laplace_composition_known_entity* known = &known_entities[index];
        content_hash_u32(&hasher, atom_positions[index]);
        blake3_hasher_update(
            &hasher, known->entity_id.bytes, sizeof(known->entity_id.bytes));
        blake3_hasher_update(
            &hasher,
            known->identity_witness.bytes,
            sizeof(known->identity_witness.bytes));
        blake3_hasher_update(
            &hasher,
            known->physicality_id.bytes,
            sizeof(known->physicality_id.bytes));
        content_hash_double(&hasher, known->centroid.component[0]);
        content_hash_double(&hasher, known->centroid.component[1]);
        content_hash_double(&hasher, known->centroid.component[2]);
        content_hash_double(&hasher, known->centroid.component[3]);
        content_hash_u32(&hasher, known->atom);
        content_hash_u32(&hasher, known->has_atom);
        content_hash_u32(&hasher, known->tier_floor);
    }
    blake3_hasher_finalize(
        &hasher,
        provider_receipt_id->bytes,
        sizeof(provider_receipt_id->bytes));
    return 0;
}

void laplace_pg_content_atom_provider_create(
    const laplace_framework_context* context,
    laplace_pg_content_atom_provider_state* state,
    laplace_content_atom_provider_v1* provider) {
    static const char domain[] =
        "laplace-postgresql-active-content-atom-provider-v1";
    blake3_hasher hasher;

    if (context == NULL || state == NULL || provider == NULL ||
        laplace_framework_context_validate(context) != LAPLACE_FRAMEWORK_OK ||
        (context->epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE)) == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace content atom provider requires a valid pinned Tier-0 epoch")));
    }

    memset(state, 0, sizeof(*state));
    memset(provider, 0, sizeof(*provider));
    state->context = *context;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(
        &hasher,
        context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes,
        sizeof(context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes));
    blake3_hasher_finalize(
        &hasher,
        state->provider_fingerprint.bytes,
        sizeof(state->provider_fingerprint.bytes));

    provider->state = state;
    provider->provider_fingerprint = state->provider_fingerprint;
    provider->resolve = content_resolve_atoms;
    provider->abi_major = LAPLACE_CONTENT_ATOM_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_CONTENT_ATOM_PROVIDER_ABI_MINOR;
}

void laplace_pg_content_admission_providers_create(
    const laplace_framework_context* context,
    laplace_pg_content_atom_provider_state* atom_state,
    laplace_content_atom_provider_v1* atom_provider,
    laplace_composition_presence_provider_v1* presence_provider) {
    if (presence_provider == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace content admission presence provider output is null")));
    }
    laplace_pg_content_atom_provider_create(context, atom_state, atom_provider);
    laplace_pg_composition_presence_provider(presence_provider);
}
