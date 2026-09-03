#ifndef LAPLACE_STOCK_RECIPE_H
#define LAPLACE_STOCK_RECIPE_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/contract/stock_recipe.h"
#include "laplace/export.h"
#include "laplace/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct laplace_stock_recipe {
    laplace_digest256 recipe_id;
    laplace_digest256 parent_recipe_id;
    laplace_digest256 source_profile_id;
    laplace_digest256 source_artifact_id;
    laplace_digest256 grammar_provider_id;
    laplace_digest256 codec_provider_id;
    laplace_digest256 lowering_program_id;
    laplace_digest256 recomposition_program_id;
    laplace_digest256 semantic_segmentation_law_id;
    laplace_digest256 conformance_id;
    laplace_digest256 loss_policy_id;
    laplace_digest256 correction_epoch_id;
    uint64_t sibling_ordinal;
    uint32_t scope_kind;
    uint32_t modality_kind;
    uint32_t version;
    uint32_t flags;
} laplace_stock_recipe;

typedef struct laplace_stock_perfcache_plane {
    laplace_digest256 plane_id;
    laplace_digest256 recipe_id;
    laplace_digest256 key_kind_id;
    laplace_digest256 value_kind_id;
    laplace_digest256 dependency_epoch_id;
    laplace_digest256 generation_program_id;
    laplace_digest256 semantic_verifier_id;
    laplace_digest256 invalidation_law_id;
    laplace_digest256 rebuild_law_id;
    uint32_t version;
    uint32_t flags;
} laplace_stock_perfcache_plane;

typedef struct laplace_stock_catalog_item {
    laplace_stock_recipe recipe;
    laplace_stock_perfcache_plane perfcache_plane;
    uint32_t item_kind;
    uint32_t flags;
} laplace_stock_catalog_item;

typedef struct laplace_stock_catalog_receipt {
    laplace_digest256 catalog_id;
    laplace_digest256 recipe_set_fingerprint;
    laplace_digest256 perfcache_set_fingerprint;
    uint64_t recipe_count;
    uint64_t source_count;
    uint64_t perfcache_plane_count;
    uint32_t maximum_scope_kind;
    uint32_t version;
    uint32_t status;
    uint32_t flags;
} laplace_stock_catalog_receipt;

typedef struct laplace_stock_recipe_error {
    uint64_t recipe_index;
    uint64_t perfcache_index;
    uint32_t status;
    uint32_t reserved;
} laplace_stock_recipe_error;

typedef enum laplace_stock_recipe_status {
    LAPLACE_STOCK_RECIPE_OK = 0,
    LAPLACE_STOCK_RECIPE_INVALID_ARGUMENT = 1,
    LAPLACE_STOCK_RECIPE_INVALID = 2,
    LAPLACE_STOCK_RECIPE_IDENTITY_MISMATCH = 3,
    LAPLACE_STOCK_RECIPE_DUPLICATE = 4,
    LAPLACE_STOCK_RECIPE_PARENT_MISSING = 5,
    LAPLACE_STOCK_RECIPE_HIERARCHY_INVALID = 6,
    LAPLACE_STOCK_RECIPE_SIBLING_DUPLICATE = 7,
    LAPLACE_STOCK_PERFCACHE_INVALID = 8,
    LAPLACE_STOCK_PERFCACHE_IDENTITY_MISMATCH = 9,
    LAPLACE_STOCK_PERFCACHE_DUPLICATE = 10,
    LAPLACE_STOCK_PERFCACHE_RECIPE_MISSING = 11,
    LAPLACE_STOCK_RECIPE_RESOURCE_INSUFFICIENT = 12,
    LAPLACE_STOCK_RECIPE_OVERFLOW = 13,
    LAPLACE_STOCK_RECIPE_SEGMENTATION_LAW_MISMATCH = 14
} laplace_stock_recipe_status;

LAPLACE_API laplace_stock_recipe_status laplace_stock_recipe_identify(
    const laplace_stock_recipe* recipe, laplace_digest256* recipe_id);
LAPLACE_API laplace_stock_recipe_status laplace_stock_perfcache_plane_identify(
    const laplace_stock_perfcache_plane* plane, laplace_digest256* plane_id);
LAPLACE_API laplace_stock_recipe_status laplace_stock_recipe_compile_catalog(
    const laplace_stock_recipe* recipes, size_t recipe_count,
    const laplace_stock_perfcache_plane* planes, size_t plane_count,
    laplace_stock_catalog_receipt* receipt, laplace_stock_recipe_error* error);
LAPLACE_API laplace_stock_recipe_status laplace_stock_recipe_compile_catalog_items(
    const laplace_stock_catalog_item* items, size_t item_count,
    laplace_stock_catalog_receipt* receipt, laplace_stock_recipe_error* error);

#ifdef __cplusplus
}
#endif

#endif
