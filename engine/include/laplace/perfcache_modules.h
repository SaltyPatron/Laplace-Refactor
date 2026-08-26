#ifndef LAPLACE_PERFCACHE_MODULES_H
#define LAPLACE_PERFCACHE_MODULES_H

#include "laplace/export.h"
#include "laplace/perfcache_registry.h"

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

#ifdef __cplusplus
}
#endif

#endif
