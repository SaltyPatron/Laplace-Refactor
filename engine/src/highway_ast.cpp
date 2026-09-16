#include "laplace/highway.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blake3.h"
#include "canonical_composition_plan.hpp"

namespace {

constexpr std::string_view RecipeDomain{
    "laplace-highway-registry-universal-ast-recipe-v1"};
constexpr std::uint32_t RecipeVersion = 1U;
constexpr std::uint64_t RoleShift = 6U;

enum class Role : std::uint64_t {
    Tag = 1U,
    Version = 2U,
    Identifier = 3U,
    Name = 4U,
    Introduced = 5U,
    Retired = 6U,
    Kind = 7U,
    Disposition = 8U,
    Alias = 9U,
};

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0U) {
            return false;
        }
    }
    return true;
}

bool HexNibble(const char value, std::uint8_t& output) {
    if (value >= '0' && value <= '9') {
        output = static_cast<std::uint8_t>(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        output = static_cast<std::uint8_t>(value - 'a' + 10);
        return true;
    }
    return false;
}

bool DecodeRegistryFingerprint(laplace_digest256& output) {
    constexpr std::string_view encoded{LAPLACE_HIGHWAY_REGISTRY_FINGERPRINT};
    static_assert(encoded.size() == sizeof(output.bytes) * 2U);
    for (std::size_t index = 0U; index < sizeof(output.bytes); ++index) {
        std::uint8_t high{};
        std::uint8_t low{};
        if (!HexNibble(encoded[index * 2U], high) ||
            !HexNibble(encoded[index * 2U + 1U], low)) {
            return false;
        }
        output.bytes[index] = static_cast<std::uint8_t>((high << 4U) | low);
    }
    return true;
}

laplace_digest256 RecipeFingerprint(
    const laplace_digest256& source_fingerprint) {
    laplace_digest256 result{};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, RecipeDomain.data(), RecipeDomain.size());
    blake3_hasher_update(
        &hasher, source_fingerprint.bytes,
        sizeof(source_fingerprint.bytes));
    const std::array<std::uint8_t, 4> version{{
        static_cast<std::uint8_t>(RecipeVersion),
        static_cast<std::uint8_t>(RecipeVersion >> 8U),
        static_cast<std::uint8_t>(RecipeVersion >> 16U),
        static_cast<std::uint8_t>(RecipeVersion >> 24U)}};
    blake3_hasher_update(&hasher, version.data(), version.size());
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}


}  // namespace

struct laplace_highway_registry_ast_plan {
    laplace_highway_registry_ast_view view{};
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::vector<std::uint64_t> kind_name_result_indexes;
    std::vector<std::uint64_t> alias_name_result_indexes;
    std::vector<std::uint64_t> disposition_name_result_indexes;
};

namespace {

class PlanBuilder final : public laplace::detail::CanonicalCompositionPlanBuilder<Role> {
public:
    PlanBuilder(
        laplace_highway_registry_ast_plan& plan,
        const laplace_digest256& geometry_epoch,
        const laplace_digest256& occurrence_context)
        : CanonicalCompositionPlanBuilder<Role>(
              plan.atom_positions, plan.operands, plan.requests,
              plan.view.recipe_fingerprint, geometry_epoch, occurrence_context,
              RecipeVersion,
#if defined(LAPLACE_TEST_HIGHWAY_AST_OMIT_OCCURRENCE)
              0U,
#else
              LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE,
#endif
              RoleShift), plan_(plan) {}

    bool Build() {
        if (!DecodeRegistryFingerprint(plan_.view.source_fingerprint)) {
            return false;
        }
        plan_.view.recipe_fingerprint =
            RecipeFingerprint(plan_.view.source_fingerprint);
        const auto registry_tag = String("highway-registry");
        const auto number_tag = String(laplace::detail::CanonicalNumberTag);
        const auto kind_tag = String("kind");
        const auto alias_tag = String("alias");
        const auto disposition_tag = String("disposition");
        if (!registry_tag.has_value || !number_tag.has_value ||
            !kind_tag.has_value || !alias_tag.has_value ||
            !disposition_tag.has_value) {
            return false;
        }
        const std::uint64_t version = Number(
            LAPLACE_HIGHWAY_REGISTRY_VERSION, number_tag.index);
        if (version == InvalidIndex) {
            return false;
        }

        std::vector<std::uint64_t> kind_rows;
        std::size_t kind_count = 0U;
        const auto* kinds = laplace_highway_registry_kinds(&kind_count);
        if (kinds == nullptr || kind_count != LAPLACE_HIGHWAY_KIND_COUNT) {
            return false;
        }
        plan_.kind_name_result_indexes.reserve(kind_count);
        kind_rows.reserve(kind_count);
        for (std::size_t index = 0U; index < kind_count; ++index) {
            const auto name = String(kinds[index].name);
            const auto identifier = Number(kinds[index].id, number_tag.index);
            const auto introduced = Number(
                kinds[index].introduced, number_tag.index);
            const auto retired = Number(kinds[index].retired, number_tag.index);
            if (!name.has_value || identifier == InvalidIndex ||
                introduced == InvalidIndex || retired == InvalidIndex) {
                return false;
            }
            plan_.kind_name_result_indexes.push_back(name.index);
            kind_rows.push_back(Node({
                {kind_tag.index, Role::Tag},
                {identifier, Role::Identifier},
                {name.index, Role::Name},
                {introduced, Role::Introduced},
                {retired, Role::Retired}}));
            if (kind_rows.back() == InvalidIndex) {
                return false;
            }
        }

        std::vector<std::uint64_t> alias_rows;
        std::size_t alias_count = 0U;
        const auto* aliases = laplace_highway_registry_aliases(&alias_count);
        if (alias_count != LAPLACE_HIGHWAY_ALIAS_COUNT ||
            (alias_count != 0U && aliases == nullptr)) {
            return false;
        }
        plan_.alias_name_result_indexes.reserve(alias_count);
        alias_rows.reserve(alias_count);
        for (std::size_t index = 0U; index < alias_count; ++index) {
            const auto name = String(aliases[index].name);
            const auto kind_identifier = Number(
                aliases[index].kind_id, number_tag.index);
            const auto introduced = Number(
                aliases[index].introduced, number_tag.index);
            const auto retired = Number(
                aliases[index].retired, number_tag.index);
            if (!name.has_value || kind_identifier == InvalidIndex ||
                introduced == InvalidIndex || retired == InvalidIndex) {
                return false;
            }
            plan_.alias_name_result_indexes.push_back(name.index);
            alias_rows.push_back(Node({
                {alias_tag.index, Role::Tag},
                {kind_identifier, Role::Kind},
                {name.index, Role::Name},
                {introduced, Role::Introduced},
                {retired, Role::Retired}}));
            if (alias_rows.back() == InvalidIndex) {
                return false;
            }
        }

        std::vector<std::uint64_t> disposition_rows;
        std::size_t disposition_count = 0U;
        const auto* dispositions =
            laplace_highway_registry_dispositions(&disposition_count);
        if (dispositions == nullptr ||
            disposition_count != LAPLACE_HIGHWAY_DISPOSITION_COUNT) {
            return false;
        }
        plan_.disposition_name_result_indexes.reserve(disposition_count);
        disposition_rows.reserve(disposition_count);
        for (std::size_t index = 0U; index < disposition_count; ++index) {
            const auto name = String(dispositions[index].name);
            const auto identifier = Number(
                dispositions[index].id, number_tag.index);
            if (!name.has_value || identifier == InvalidIndex) {
                return false;
            }
            plan_.disposition_name_result_indexes.push_back(name.index);
            disposition_rows.push_back(Node({
                {disposition_tag.index, Role::Tag},
                {identifier, Role::Identifier},
                {name.index, Role::Name}}));
            if (disposition_rows.back() == InvalidIndex) {
                return false;
            }
        }

        std::vector<std::pair<std::uint64_t, Role>> root_children;
        root_children.reserve(
            2U + kind_rows.size() + alias_rows.size() +
            disposition_rows.size());
        root_children.emplace_back(registry_tag.index, Role::Tag);
        root_children.emplace_back(version, Role::Version);
        for (const auto row : kind_rows) {
            root_children.emplace_back(row, Role::Kind);
        }
        for (const auto row : alias_rows) {
            root_children.emplace_back(row, Role::Alias);
        }
        for (const auto row : disposition_rows) {
            root_children.emplace_back(row, Role::Disposition);
        }
        const auto root = Node(root_children);
        if (root == InvalidIndex) {
            return false;
        }
        plan_.view.root_result_index = root;
        return true;
    }

private:
    laplace_highway_registry_ast_plan& plan_;
};

void BindView(laplace_highway_registry_ast_plan& plan) {
    plan.view.atom_positions = plan.atom_positions.data();
    plan.view.operands = plan.operands.data();
    plan.view.requests = plan.requests.data();
    plan.view.kind_name_result_indexes =
        plan.kind_name_result_indexes.data();
    plan.view.alias_name_result_indexes =
        plan.alias_name_result_indexes.data();
    plan.view.disposition_name_result_indexes =
        plan.disposition_name_result_indexes.data();
    plan.view.atom_count = plan.atom_positions.size();
    plan.view.operand_count = plan.operands.size();
    plan.view.request_count = plan.requests.size();
    plan.view.kind_count = plan.kind_name_result_indexes.size();
    plan.view.alias_count = plan.alias_name_result_indexes.size();
    plan.view.disposition_count =
        plan.disposition_name_result_indexes.size();
    plan.view.recipe_version = RecipeVersion;
}

}  // namespace

extern "C" laplace_highway_status laplace_highway_registry_ast_plan_create(
    const laplace_digest256* geometry_epoch,
    const laplace_digest256* occurrence_context_fingerprint,
    laplace_highway_registry_ast_plan** plan) {
    if (geometry_epoch == nullptr ||
        occurrence_context_fingerprint == nullptr || plan == nullptr ||
        *plan != nullptr || DigestZero(*geometry_epoch) ||
        DigestZero(*occurrence_context_fingerprint)) {
        return LAPLACE_HIGHWAY_INVALID_ARGUMENT;
    }
    try {
        auto* created = new laplace_highway_registry_ast_plan{};
        created->view.geometry_epoch = *geometry_epoch;
        created->view.occurrence_context_fingerprint =
            *occurrence_context_fingerprint;
        PlanBuilder builder{
            *created, *geometry_epoch, *occurrence_context_fingerprint};
        if (!builder.Build()) {
            delete created;
            return LAPLACE_HIGHWAY_REGISTRY_INVALID;
        }
        BindView(*created);
        *plan = created;
        return LAPLACE_HIGHWAY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_HIGHWAY_MEMORY_FAILURE;
    }
}

extern "C" laplace_highway_status laplace_highway_registry_ast_plan_view(
    const laplace_highway_registry_ast_plan* plan,
    laplace_highway_registry_ast_view* view) {
    if (plan == nullptr || view == nullptr || plan->view.atom_positions == nullptr ||
        plan->view.operands == nullptr || plan->view.requests == nullptr ||
        plan->view.kind_name_result_indexes == nullptr ||
        (plan->view.alias_count != 0U &&
         plan->view.alias_name_result_indexes == nullptr) ||
        plan->view.disposition_name_result_indexes == nullptr ||
        plan->view.atom_count == 0U || plan->view.operand_count == 0U ||
        plan->view.request_count == 0U ||
        plan->view.root_result_index + 1U != plan->view.request_count) {
        return LAPLACE_HIGHWAY_REGISTRY_INVALID;
    }
    *view = plan->view;
    return LAPLACE_HIGHWAY_OK;
}

extern "C" void laplace_highway_registry_ast_plan_destroy(
    laplace_highway_registry_ast_plan** plan) {
    if (plan == nullptr) {
        return;
    }
    delete *plan;
    *plan = nullptr;
}
