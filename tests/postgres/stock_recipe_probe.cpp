#include "laplace/isa.h"
#include "laplace/stock_recipe.h"
#include "../context_fixture.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void Fill(laplace_digest256* digest, std::uint8_t value) {
    std::memset(digest->bytes, value, sizeof(digest->bytes));
}

void Print(const char* key, const laplace_digest256& digest) {
    std::printf("%s=", key);
    for (const auto byte : digest.bytes) {
        std::printf("%02x", static_cast<unsigned int>(byte));
    }
    std::putchar('\n');
}

laplace_stock_recipe Recipe(
    std::uint8_t seed,
    std::uint8_t profile_seed,
    std::uint32_t scope,
    const laplace_digest256& parent = {}) {
    laplace_stock_recipe recipe{};
    recipe.parent_recipe_id = parent;
    Fill(&recipe.source_profile_id, profile_seed);
    Fill(&recipe.source_artifact_id, static_cast<std::uint8_t>(seed + 1u));
    Fill(&recipe.grammar_provider_id, static_cast<std::uint8_t>(seed + 2u));
    Fill(&recipe.codec_provider_id, static_cast<std::uint8_t>(seed + 3u));
    Fill(&recipe.lowering_program_id, static_cast<std::uint8_t>(seed + 4u));
    Fill(&recipe.recomposition_program_id, static_cast<std::uint8_t>(seed + 5u));
    Fill(&recipe.semantic_segmentation_law_id, 0xe0u);
    Fill(&recipe.conformance_id, static_cast<std::uint8_t>(seed + 6u));
    Fill(&recipe.loss_policy_id, static_cast<std::uint8_t>(seed + 7u));
    Fill(&recipe.correction_epoch_id, static_cast<std::uint8_t>(seed + 8u));
    recipe.sibling_ordinal = 1u;
    recipe.scope_kind = scope;
    recipe.modality_kind = 1u;
    recipe.version = LAPLACE_STOCK_VERSION;
    if (laplace_stock_recipe_identify(&recipe, &recipe.recipe_id) !=
        LAPLACE_STOCK_RECIPE_OK) {
        std::exit(2);
    }
    return recipe;
}

laplace_stock_perfcache_plane Plane(
    std::uint8_t seed,
    const laplace_digest256& recipe_id) {
    laplace_stock_perfcache_plane plane{};
    plane.recipe_id = recipe_id;
    Fill(&plane.key_kind_id, static_cast<std::uint8_t>(seed + 1u));
    Fill(&plane.value_kind_id, static_cast<std::uint8_t>(seed + 2u));
    Fill(&plane.dependency_epoch_id, static_cast<std::uint8_t>(seed + 3u));
    Fill(&plane.generation_program_id, static_cast<std::uint8_t>(seed + 4u));
    Fill(&plane.semantic_verifier_id, static_cast<std::uint8_t>(seed + 5u));
    Fill(&plane.invalidation_law_id, static_cast<std::uint8_t>(seed + 6u));
    Fill(&plane.rebuild_law_id, static_cast<std::uint8_t>(seed + 7u));
    plane.version = LAPLACE_STOCK_VERSION;
    if (laplace_stock_perfcache_plane_identify(&plane, &plane.plane_id) !=
        LAPLACE_STOCK_RECIPE_OK) {
        std::exit(3);
    }
    return plane;
}

}  // namespace

int main() {
    std::array<laplace_stock_catalog_item, 3> items{};
    items[0].recipe = Recipe(0x10u, 0x11u, LAPLACE_STOCK_SCOPE_SOURCE);
    items[0].item_kind = LAPLACE_STOCK_ITEM_RECIPE;
    items[1].recipe = Recipe(
        0x20u, 0x11u, LAPLACE_STOCK_SCOPE_DIGITAL_OBJECT,
        items[0].recipe.recipe_id);
    items[1].item_kind = LAPLACE_STOCK_ITEM_RECIPE;
    items[2].perfcache_plane = Plane(0x90u, items[1].recipe.recipe_id);
    items[2].item_kind = LAPLACE_STOCK_ITEM_PERFCACHE_PLANE;
    laplace_stock_catalog_receipt catalog{};
    laplace_stock_recipe_error catalog_error{};
    if (laplace_stock_recipe_compile_catalog_items(
            items.data(), items.size(), &catalog, &catalog_error) !=
        LAPLACE_STOCK_RECIPE_OK) {
        return 4;
    }
    laplace_stock_catalog_receipt output{};
    std::array<laplace_isa_value_view, 2> values{{
        {items.data(), items.size(), items.size(), sizeof(items[0]),
         LAPLACE_ISA_VALUE_STOCK_CATALOG_ITEM_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u},
        {&output, 0u, 1u, sizeof(output),
         LAPLACE_ISA_VALUE_STOCK_CATALOG_RECEIPT_VECTOR,
         LAPLACE_ISA_KNOWN_VALUE_FLAGS, 0u}}};
    laplace_isa_instruction instruction{
        LAPLACE_ISA_OPCODE_STOCK_RECIPE_COMPILE_CATALOG_BATCH,
        0u, 1u,
        LAPLACE_ISA_INSTRUCTION_VERSION_STOCK_RECIPE_COMPILE_CATALOG_BATCH,
        LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS};
    const auto context = laplace_test_context(0u);
    laplace_isa_program program{
        &instruction, values.data(), &context, 1u, values.size(),
        LAPLACE_ISA_MAJOR, LAPLACE_ISA_MINOR,
        LAPLACE_ISA_KNOWN_PROGRAM_FLAGS,
        LAPLACE_ISA_RECEIPT_DETAIL_FULL, 0u};
    laplace_isa_receipt isa_receipt{};
    laplace_isa_error isa_error{};
    if (laplace_isa_execute(&program, &isa_receipt, &isa_error) !=
            LAPLACE_ISA_OK ||
        std::memcmp(&catalog, &output, sizeof(catalog)) != 0) {
        return 5;
    }
    Print("STOCK_ROOT_RECIPE", items[0].recipe.recipe_id);
    Print("STOCK_CHILD_RECIPE", items[1].recipe.recipe_id);
    Print("STOCK_PLANE", items[2].perfcache_plane.plane_id);
    Print("STOCK_CATALOG", catalog.catalog_id);
    Print("STOCK_RECIPE_SET", catalog.recipe_set_fingerprint);
    Print("STOCK_PERFCACHE_SET", catalog.perfcache_set_fingerprint);
    Print("STOCK_ISA_RECEIPT", isa_receipt.receipt_id);
    return 0;
}
