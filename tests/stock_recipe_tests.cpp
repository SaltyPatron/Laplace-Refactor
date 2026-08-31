#include "laplace/stock_recipe.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

namespace {

laplace_digest256 Digest(std::uint8_t seed) {
    laplace_digest256 value{};
    for (std::size_t index = 0; index < sizeof(value.bytes); ++index) {
        value.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return value;
}

laplace_stock_recipe Recipe(
    std::uint8_t seed, std::uint8_t profile_seed, std::uint32_t scope,
    std::uint64_t ordinal, const laplace_digest256& parent = {}) {
    laplace_stock_recipe value{};
    value.parent_recipe_id = parent;
    value.source_profile_id = Digest(profile_seed);
    value.source_artifact_id = Digest(static_cast<std::uint8_t>(seed + 1u));
    value.grammar_provider_id = Digest(static_cast<std::uint8_t>(seed + 2u));
    value.codec_provider_id = Digest(static_cast<std::uint8_t>(seed + 3u));
    value.lowering_program_id = Digest(static_cast<std::uint8_t>(seed + 4u));
    value.recomposition_program_id = Digest(static_cast<std::uint8_t>(seed + 5u));
    value.semantic_segmentation_law_id = Digest(0xe0u);
    value.conformance_id = Digest(static_cast<std::uint8_t>(seed + 6u));
    value.loss_policy_id = Digest(static_cast<std::uint8_t>(seed + 7u));
    value.correction_epoch_id = Digest(static_cast<std::uint8_t>(seed + 8u));
    value.sibling_ordinal = ordinal;
    value.scope_kind = scope;
    value.modality_kind = 1u;
    value.version = LAPLACE_STOCK_VERSION;
    EXPECT_EQ(laplace_stock_recipe_identify(&value, &value.recipe_id),
              LAPLACE_STOCK_RECIPE_OK);
    return value;
}

laplace_stock_perfcache_plane Plane(
    std::uint8_t seed, const laplace_digest256& recipe_id) {
    laplace_stock_perfcache_plane value{};
    value.recipe_id = recipe_id;
    value.key_kind_id = Digest(static_cast<std::uint8_t>(seed + 1u));
    value.value_kind_id = Digest(static_cast<std::uint8_t>(seed + 2u));
    value.dependency_epoch_id = Digest(static_cast<std::uint8_t>(seed + 3u));
    value.generation_program_id = Digest(static_cast<std::uint8_t>(seed + 4u));
    value.semantic_verifier_id = Digest(static_cast<std::uint8_t>(seed + 5u));
    value.invalidation_law_id = Digest(static_cast<std::uint8_t>(seed + 6u));
    value.rebuild_law_id = Digest(static_cast<std::uint8_t>(seed + 7u));
    value.version = LAPLACE_STOCK_VERSION;
    EXPECT_EQ(laplace_stock_perfcache_plane_identify(&value, &value.plane_id),
              LAPLACE_STOCK_RECIPE_OK);
    return value;
}

std::array<laplace_stock_recipe, 8> CatalogRecipes() {
    std::array<laplace_stock_recipe, 8> values{};
    values[0] = Recipe(0x10u, 0x11u, LAPLACE_STOCK_SCOPE_SOURCE, 1u);
    values[1] = Recipe(0x20u, 0x11u, LAPLACE_STOCK_SCOPE_RELEASE, 1u,
                       values[0].recipe_id);
    values[2] = Recipe(0x30u, 0x11u, LAPLACE_STOCK_SCOPE_COLLECTION, 1u,
                       values[1].recipe_id);
    values[3] = Recipe(0x40u, 0x11u, LAPLACE_STOCK_SCOPE_DIGITAL_OBJECT, 1u,
                       values[2].recipe_id);
    values[4] = Recipe(0x50u, 0x11u, LAPLACE_STOCK_SCOPE_STRUCTURAL_REGION, 1u,
                       values[3].recipe_id);
    values[5] = Recipe(0x60u, 0x11u, LAPLACE_STOCK_SCOPE_SEMANTIC_CONSTRUCTION, 1u,
                       values[4].recipe_id);
    values[6] = Recipe(0x70u, 0x71u, LAPLACE_STOCK_SCOPE_SOURCE, 1u);
    values[7] = Recipe(0x80u, 0x71u, LAPLACE_STOCK_SCOPE_DIGITAL_OBJECT, 1u,
                       values[6].recipe_id);
    return values;
}

TEST(StockRecipe, CompilesRecursiveMultiSourceCatalogWithDerivedPlanes) {
    auto recipes = CatalogRecipes();
    std::array planes{Plane(0x90u, recipes[3].recipe_id),
                      Plane(0xa0u, recipes[5].recipe_id),
                      Plane(0xb0u, recipes[7].recipe_id)};
    laplace_stock_catalog_receipt receipt{};
    laplace_stock_recipe_error error{};
    ASSERT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), planes.data(), planes.size(),
                  &receipt, &error),
              LAPLACE_STOCK_RECIPE_OK);
    EXPECT_EQ(receipt.recipe_count, recipes.size());
    EXPECT_EQ(receipt.source_count, 2u);
    EXPECT_EQ(receipt.perfcache_plane_count, planes.size());
    EXPECT_EQ(receipt.maximum_scope_kind,
              LAPLACE_STOCK_SCOPE_SEMANTIC_CONSTRUCTION);
}

TEST(StockRecipe, CatalogIdentityIsInvariantToCallerOrder) {
    auto recipes = CatalogRecipes();
    std::array planes{Plane(0x90u, recipes[3].recipe_id),
                      Plane(0xa0u, recipes[5].recipe_id)};
    laplace_stock_catalog_receipt first{};
    laplace_stock_catalog_receipt second{};
    ASSERT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), planes.data(), planes.size(),
                  &first, nullptr), LAPLACE_STOCK_RECIPE_OK);
    std::reverse(recipes.begin(), recipes.end());
    std::reverse(planes.begin(), planes.end());
    ASSERT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), planes.data(), planes.size(),
                  &second, nullptr), LAPLACE_STOCK_RECIPE_OK);
    EXPECT_EQ(0, std::memcmp(&first, &second, sizeof(first)));
}

TEST(StockRecipe, RejectsOrphanAndCrossSourceHierarchy) {
    auto recipes = CatalogRecipes();
    laplace_stock_catalog_receipt receipt{};
    recipes[4].parent_recipe_id = Digest(0xf0u);
    ASSERT_EQ(laplace_stock_recipe_identify(&recipes[4], &recipes[4].recipe_id),
              LAPLACE_STOCK_RECIPE_OK);
    EXPECT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), nullptr, 0u, &receipt, nullptr),
              LAPLACE_STOCK_RECIPE_PARENT_MISSING);
    recipes = CatalogRecipes();
    recipes[4].parent_recipe_id = recipes[6].recipe_id;
    ASSERT_EQ(laplace_stock_recipe_identify(&recipes[4], &recipes[4].recipe_id),
              LAPLACE_STOCK_RECIPE_OK);
    recipes[5].parent_recipe_id = recipes[4].recipe_id;
    ASSERT_EQ(laplace_stock_recipe_identify(&recipes[5], &recipes[5].recipe_id),
              LAPLACE_STOCK_RECIPE_OK);
    EXPECT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), nullptr, 0u, &receipt, nullptr),
              LAPLACE_STOCK_RECIPE_HIERARCHY_INVALID);
}

TEST(StockRecipe, RejectsUnownedOrSemanticallyDriftedPerfcachePlane) {
    auto recipes = CatalogRecipes();
    auto plane = Plane(0x90u, Digest(0xfeu));
    laplace_stock_catalog_receipt receipt{};
    EXPECT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), &plane, 1u, &receipt, nullptr),
              LAPLACE_STOCK_PERFCACHE_RECIPE_MISSING);
    plane = Plane(0x90u, recipes[3].recipe_id);
    plane.semantic_verifier_id.bytes[0] ^= 1u;
    EXPECT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), &plane, 1u, &receipt, nullptr),
              LAPLACE_STOCK_PERFCACHE_IDENTITY_MISMATCH);
}

TEST(StockRecipe, RequiresOneSharedModalityAgnosticSegmentationLaw) {
    auto recipes = CatalogRecipes();
    recipes[7].semantic_segmentation_law_id = Digest(0xd0u);
    ASSERT_EQ(laplace_stock_recipe_identify(&recipes[7], &recipes[7].recipe_id),
              LAPLACE_STOCK_RECIPE_OK);
    laplace_stock_catalog_receipt receipt{};
    EXPECT_EQ(laplace_stock_recipe_compile_catalog(
                  recipes.data(), recipes.size(), nullptr, 0u, &receipt, nullptr),
              LAPLACE_STOCK_RECIPE_SEGMENTATION_LAW_MISMATCH);
}

}  // namespace
