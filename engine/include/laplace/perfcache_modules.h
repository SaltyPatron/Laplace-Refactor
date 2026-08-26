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
    laplace_perfcache_module_v1* module);

LAPLACE_API laplace_perfcache_registry_status
laplace_perfcache_unicode_tier0_module(
    laplace_perfcache_module_v1* module);

LAPLACE_API laplace_perfcache_status
laplace_perfcache_unicode_tier0_validate_view(
    void* context,
    const laplace_perfcache_view* view,
    uint64_t* invalid_record_index);

#ifdef __cplusplus
}
#endif

#endif
