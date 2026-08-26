#ifndef LAPLACE_PERFCACHE_MODULES_H
#define LAPLACE_PERFCACHE_MODULES_H

#include "laplace/export.h"
#include "laplace/perfcache_registry.h"
#include "laplace/unicode_root.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Generated identities come from contracts/perfcache.json. */
LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_framework_probe_module(
    laplace_perfcache_module_v2* module);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_unicode_tier0_module(
    laplace_perfcache_module_v2* module);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_unicode_identity_reverse_module(
    laplace_perfcache_module_v2* module);

/*
 * Resolve an exact persisted contract to the native module implementation
 * shipped by this engine.  Callers must not select a validator from a path,
 * filename, or product-specific guess: the complete module contract selects
 * the implementation.
 */
LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_builtin_module_resolve(
    const laplace_perfcache_contract* contract,
    laplace_perfcache_module_v2* module);

LAPLACE_API laplace_perfcache_status
laplace_perfcache_unicode_tier0_validate_view(
    void* context,
    const laplace_perfcache_view* view,
    uint64_t* invalid_record_index);

LAPLACE_API laplace_perfcache_status
laplace_perfcache_unicode_identity_reverse_validate_view(
    void* context,
    const laplace_perfcache_view* view,
    uint64_t* invalid_record_index);

typedef struct laplace_unicode_identity_key {
    laplace_id128 content_id;
    laplace_digest256 identity_preimage_fingerprint;
} laplace_unicode_identity_key;

/*
 * Typed hot accessors retain cache layout semantics in the native engine.
 * Returned atom views remain valid only while the supplied generation pin is
 * held. Reverse results are copied into caller-owned output storage.
 */
LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_unicode_tier0_resolve_batch(
    const laplace_perfcache_pin* pin,
    const uint32_t* codepoint_positions,
    size_t item_count,
    laplace_unicode_atom_record_view* atoms,
    uint8_t* found);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_unicode_identity_reverse_resolve_batch(
    const laplace_perfcache_pin* pin,
    const laplace_unicode_identity_key* identities,
    size_t item_count,
    uint32_t* codepoint_positions,
    uint8_t* found);

#ifdef __cplusplus
}
#endif

#endif
