#include "laplace/stock_recipe.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "blake3.h"

static int bytes_zero(const void* value, size_t count) {
    const uint8_t* bytes = (const uint8_t*)value;
    uint8_t aggregate = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0u;
}

static int digest_equal(const laplace_digest256* left, const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, 32u) == 0;
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
    blake3_hasher_finalize(hasher, output->bytes, 32u);
}

static int recipe_shape_valid(const laplace_stock_recipe* recipe) {
    const int source = recipe != NULL &&
        recipe->scope_kind == LAPLACE_STOCK_SCOPE_SOURCE;
    return recipe != NULL &&
        (source ? bytes_zero(&recipe->parent_recipe_id, 32u) :
                  !bytes_zero(&recipe->parent_recipe_id, 32u)) &&
        !bytes_zero(&recipe->source_profile_id, 32u) &&
        !bytes_zero(&recipe->source_artifact_id, 32u) &&
        !bytes_zero(&recipe->grammar_provider_id, 32u) &&
        !bytes_zero(&recipe->codec_provider_id, 32u) &&
        !bytes_zero(&recipe->lowering_program_id, 32u) &&
        !bytes_zero(&recipe->recomposition_program_id, 32u) &&
        !bytes_zero(&recipe->semantic_segmentation_law_id, 32u) &&
        !bytes_zero(&recipe->conformance_id, 32u) &&
        !bytes_zero(&recipe->loss_policy_id, 32u) &&
        !bytes_zero(&recipe->correction_epoch_id, 32u) &&
        recipe->sibling_ordinal > 0u &&
        recipe->scope_kind >= LAPLACE_STOCK_SCOPE_SOURCE &&
        recipe->scope_kind <= LAPLACE_STOCK_SCOPE_SEMANTIC_CONSTRUCTION &&
        recipe->modality_kind > 0u && recipe->version == LAPLACE_STOCK_VERSION &&
        recipe->flags == LAPLACE_STOCK_FLAGS_NONE;
}

laplace_stock_recipe_status laplace_stock_recipe_identify(
    const laplace_stock_recipe* recipe, laplace_digest256* recipe_id) {
    blake3_hasher hasher;
    if (recipe_id == NULL || !recipe_shape_valid(recipe)) {
        return LAPLACE_STOCK_RECIPE_INVALID;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STOCK_RECIPE_DOMAIN,
                         sizeof(LAPLACE_STOCK_RECIPE_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, recipe->parent_recipe_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->source_profile_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->source_artifact_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->grammar_provider_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->codec_provider_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->lowering_program_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->recomposition_program_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->semantic_segmentation_law_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->conformance_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->loss_policy_id.bytes, 32u);
    blake3_hasher_update(&hasher, recipe->correction_epoch_id.bytes, 32u);
    hash_u64(&hasher, recipe->sibling_ordinal);
    hash_u32(&hasher, recipe->scope_kind);
    hash_u32(&hasher, recipe->modality_kind);
    hash_u32(&hasher, recipe->version);
    hash_u32(&hasher, recipe->flags);
    finish(&hasher, recipe_id);
    return LAPLACE_STOCK_RECIPE_OK;
}

static int plane_shape_valid(const laplace_stock_perfcache_plane* plane) {
    return plane != NULL && !bytes_zero(&plane->recipe_id, 32u) &&
        !bytes_zero(&plane->key_kind_id, 32u) &&
        !bytes_zero(&plane->value_kind_id, 32u) &&
        !bytes_zero(&plane->dependency_epoch_id, 32u) &&
        !bytes_zero(&plane->generation_program_id, 32u) &&
        !bytes_zero(&plane->semantic_verifier_id, 32u) &&
        !bytes_zero(&plane->invalidation_law_id, 32u) &&
        !bytes_zero(&plane->rebuild_law_id, 32u) &&
        plane->version == LAPLACE_STOCK_VERSION &&
        plane->flags == LAPLACE_STOCK_FLAGS_NONE;
}

laplace_stock_recipe_status laplace_stock_perfcache_plane_identify(
    const laplace_stock_perfcache_plane* plane, laplace_digest256* plane_id) {
    blake3_hasher hasher;
    if (plane_id == NULL || !plane_shape_valid(plane)) {
        return LAPLACE_STOCK_PERFCACHE_INVALID;
    }
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, LAPLACE_STOCK_PERFCACHE_PLANE_DOMAIN,
                         sizeof(LAPLACE_STOCK_PERFCACHE_PLANE_DOMAIN) - 1u);
    blake3_hasher_update(&hasher, plane->recipe_id.bytes, 32u);
    blake3_hasher_update(&hasher, plane->key_kind_id.bytes, 32u);
    blake3_hasher_update(&hasher, plane->value_kind_id.bytes, 32u);
    blake3_hasher_update(&hasher, plane->dependency_epoch_id.bytes, 32u);
    blake3_hasher_update(&hasher, plane->generation_program_id.bytes, 32u);
    blake3_hasher_update(&hasher, plane->semantic_verifier_id.bytes, 32u);
    blake3_hasher_update(&hasher, plane->invalidation_law_id.bytes, 32u);
    blake3_hasher_update(&hasher, plane->rebuild_law_id.bytes, 32u);
    hash_u32(&hasher, plane->version);
    hash_u32(&hasher, plane->flags);
    finish(&hasher, plane_id);
    return LAPLACE_STOCK_RECIPE_OK;
}

static int compare_recipe(const void* left, const void* right) {
    return memcmp(((const laplace_stock_recipe*)left)->recipe_id.bytes,
                  ((const laplace_stock_recipe*)right)->recipe_id.bytes, 32u);
}

static int compare_plane(const void* left, const void* right) {
    return memcmp(((const laplace_stock_perfcache_plane*)left)->plane_id.bytes,
                  ((const laplace_stock_perfcache_plane*)right)->plane_id.bytes, 32u);
}

static const laplace_stock_recipe* find_recipe(
    const laplace_stock_recipe* recipes, size_t count,
    const laplace_digest256* recipe_id) {
    laplace_stock_recipe key;
    memset(&key, 0, sizeof(key));
    key.recipe_id = *recipe_id;
    return (const laplace_stock_recipe*)bsearch(
        &key, recipes, count, sizeof(*recipes), compare_recipe);
}

static laplace_stock_recipe_status fail(
    laplace_stock_recipe_error* error, laplace_stock_recipe_status status,
    size_t recipe_index, size_t plane_index) {
    if (error != NULL) {
        error->recipe_index = (uint64_t)recipe_index;
        error->perfcache_index = (uint64_t)plane_index;
        error->status = (uint32_t)status;
    }
    return status;
}

laplace_stock_recipe_status laplace_stock_recipe_compile_catalog(
    const laplace_stock_recipe* recipes, size_t recipe_count,
    const laplace_stock_perfcache_plane* planes, size_t plane_count,
    laplace_stock_catalog_receipt* receipt, laplace_stock_recipe_error* error) {
    laplace_stock_recipe* ordered_recipes;
    laplace_stock_perfcache_plane* ordered_planes = NULL;
    blake3_hasher recipe_hasher;
    blake3_hasher plane_hasher;
    blake3_hasher catalog_hasher;
    size_t index;
    uint64_t source_count = 0u;
    uint32_t maximum_scope = 0u;
    if (recipes == NULL || recipe_count == 0u || receipt == NULL ||
        (plane_count != 0u && planes == NULL)) {
        return fail(error, LAPLACE_STOCK_RECIPE_INVALID_ARGUMENT, 0u, 0u);
    }
    if (recipe_count > SIZE_MAX / sizeof(*ordered_recipes) ||
        plane_count > SIZE_MAX / sizeof(*ordered_planes)) {
        return fail(error, LAPLACE_STOCK_RECIPE_OVERFLOW, 0u, 0u);
    }
    ordered_recipes = (laplace_stock_recipe*)malloc(
        recipe_count * sizeof(*ordered_recipes));
    if (ordered_recipes == NULL) {
        return fail(error, LAPLACE_STOCK_RECIPE_RESOURCE_INSUFFICIENT, 0u, 0u);
    }
    memcpy(ordered_recipes, recipes, recipe_count * sizeof(*ordered_recipes));
    for (index = 0u; index < recipe_count; ++index) {
        laplace_digest256 identity;
        const laplace_stock_recipe_status status =
            laplace_stock_recipe_identify(&ordered_recipes[index], &identity);
        if (status != LAPLACE_STOCK_RECIPE_OK ||
            !digest_equal(&identity, &ordered_recipes[index].recipe_id)) {
            free(ordered_recipes);
            return fail(error, status == LAPLACE_STOCK_RECIPE_OK ?
                LAPLACE_STOCK_RECIPE_IDENTITY_MISMATCH : status, index, 0u);
        }
    }
    qsort(ordered_recipes, recipe_count, sizeof(*ordered_recipes), compare_recipe);
    blake3_hasher_init(&recipe_hasher);
    for (index = 0u; index < recipe_count; ++index) {
        const laplace_stock_recipe* recipe = &ordered_recipes[index];
        if (index != 0u && digest_equal(
                &recipe->recipe_id, &ordered_recipes[index - 1u].recipe_id)) {
            free(ordered_recipes);
            return fail(error, LAPLACE_STOCK_RECIPE_DUPLICATE, index, 0u);
        }
        if (!digest_equal(&recipe->semantic_segmentation_law_id,
                          &ordered_recipes[0].semantic_segmentation_law_id)) {
            free(ordered_recipes);
            return fail(error,
                        LAPLACE_STOCK_RECIPE_SEGMENTATION_LAW_MISMATCH,
                        index, 0u);
        }
        if (recipe->scope_kind == LAPLACE_STOCK_SCOPE_SOURCE) {
            ++source_count;
        } else {
#if !defined(LAPLACE_TEST_STOCK_RECIPE_PARENT_BYPASS)
            const laplace_stock_recipe* parent = find_recipe(
                ordered_recipes, recipe_count, &recipe->parent_recipe_id);
            if (parent == NULL) {
                free(ordered_recipes);
                return fail(error, LAPLACE_STOCK_RECIPE_PARENT_MISSING, index, 0u);
            }
            if (!digest_equal(&parent->source_profile_id, &recipe->source_profile_id) ||
                parent->scope_kind >= recipe->scope_kind) {
                free(ordered_recipes);
                return fail(error, LAPLACE_STOCK_RECIPE_HIERARCHY_INVALID, index, 0u);
            }
#endif
        }
        if (recipe->scope_kind > maximum_scope) {
            maximum_scope = recipe->scope_kind;
        }
        blake3_hasher_update(&recipe_hasher, recipe->recipe_id.bytes, 32u);
    }
    for (index = 0u; index < recipe_count; ++index) {
        size_t other;
        for (other = index + 1u; other < recipe_count; ++other) {
            if (ordered_recipes[index].sibling_ordinal ==
                    ordered_recipes[other].sibling_ordinal &&
                digest_equal(&ordered_recipes[index].parent_recipe_id,
                             &ordered_recipes[other].parent_recipe_id) &&
                digest_equal(&ordered_recipes[index].source_profile_id,
                             &ordered_recipes[other].source_profile_id)) {
                free(ordered_recipes);
                return fail(error, LAPLACE_STOCK_RECIPE_SIBLING_DUPLICATE,
                            other, 0u);
            }
        }
    }
    if (source_count == 0u) {
        free(ordered_recipes);
        return fail(error, LAPLACE_STOCK_RECIPE_HIERARCHY_INVALID, 0u, 0u);
    }
    blake3_hasher_init(&plane_hasher);
    if (plane_count != 0u) {
        ordered_planes = (laplace_stock_perfcache_plane*)malloc(
            plane_count * sizeof(*ordered_planes));
        if (ordered_planes == NULL) {
            free(ordered_recipes);
            return fail(error, LAPLACE_STOCK_RECIPE_RESOURCE_INSUFFICIENT, 0u, 0u);
        }
        memcpy(ordered_planes, planes, plane_count * sizeof(*ordered_planes));
        for (index = 0u; index < plane_count; ++index) {
            laplace_digest256 identity;
            laplace_stock_recipe_status status =
                laplace_stock_perfcache_plane_identify(&ordered_planes[index], &identity);
            if (status != LAPLACE_STOCK_RECIPE_OK ||
                !digest_equal(&identity, &ordered_planes[index].plane_id)) {
                free(ordered_planes);
                free(ordered_recipes);
                return fail(error, status == LAPLACE_STOCK_RECIPE_OK ?
                    LAPLACE_STOCK_PERFCACHE_IDENTITY_MISMATCH : status, 0u, index);
            }
        }
        qsort(ordered_planes, plane_count, sizeof(*ordered_planes), compare_plane);
        for (index = 0u; index < plane_count; ++index) {
            if ((index != 0u && digest_equal(&ordered_planes[index].plane_id,
                                             &ordered_planes[index - 1u].plane_id)) ||
                find_recipe(ordered_recipes, recipe_count,
                            &ordered_planes[index].recipe_id) == NULL) {
                const laplace_stock_recipe_status status = index != 0u &&
                    digest_equal(&ordered_planes[index].plane_id,
                                 &ordered_planes[index - 1u].plane_id) ?
                    LAPLACE_STOCK_PERFCACHE_DUPLICATE :
                    LAPLACE_STOCK_PERFCACHE_RECIPE_MISSING;
                free(ordered_planes);
                free(ordered_recipes);
                return fail(error, status, 0u, index);
            }
            blake3_hasher_update(&plane_hasher, ordered_planes[index].plane_id.bytes, 32u);
        }
    }
    memset(receipt, 0, sizeof(*receipt));
    finish(&recipe_hasher, &receipt->recipe_set_fingerprint);
    finish(&plane_hasher, &receipt->perfcache_set_fingerprint);
    blake3_hasher_init(&catalog_hasher);
    blake3_hasher_update(&catalog_hasher, LAPLACE_STOCK_CATALOG_DOMAIN,
                         sizeof(LAPLACE_STOCK_CATALOG_DOMAIN) - 1u);
    blake3_hasher_update(&catalog_hasher, receipt->recipe_set_fingerprint.bytes, 32u);
    blake3_hasher_update(&catalog_hasher, receipt->perfcache_set_fingerprint.bytes, 32u);
    hash_u64(&catalog_hasher, (uint64_t)recipe_count);
    hash_u64(&catalog_hasher, source_count);
    hash_u64(&catalog_hasher, (uint64_t)plane_count);
    hash_u32(&catalog_hasher, maximum_scope);
    hash_u32(&catalog_hasher, LAPLACE_STOCK_VERSION);
    finish(&catalog_hasher, &receipt->catalog_id);
    receipt->recipe_count = (uint64_t)recipe_count;
    receipt->source_count = source_count;
    receipt->perfcache_plane_count = (uint64_t)plane_count;
    receipt->maximum_scope_kind = maximum_scope;
    receipt->version = LAPLACE_STOCK_VERSION;
    receipt->status = LAPLACE_STOCK_RECIPE_OK;
    receipt->flags = LAPLACE_STOCK_FLAGS_NONE;
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }
    free(ordered_planes);
    free(ordered_recipes);
    return LAPLACE_STOCK_RECIPE_OK;
}
