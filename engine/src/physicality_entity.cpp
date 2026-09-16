#include "laplace/physicality_entity.h"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <limits>
#include <string_view>
#include <vector>

#include "blake3.h"
#include "canonical_composition_plan.hpp"

namespace {

constexpr std::uint32_t Version = 1U;
constexpr std::string_view RecipeLaw{
    "laplace-physicality-entity-view-v1;ordinary-ordered-composition;"
    "PhysicalityRecord,version,entity,type,vertex-class,recipe-version,form,"
    "dimensions,flags,recipe,geometry,trajectory-fingerprint,centroid,radius,"
    "logical-count,vertex-count,trajectory;"
    "number=shared-canonical-unsigned-decimal;Octets=ordered-unsigned;Binary64=exact-uint64;"
    "Occurrence=entity,ordinal,run-length,metadata;"
    "external-entity-reference;role-shift=6;tag=1,identifier=3,value=2;no-occurrence"};
enum class Role : std::uint64_t { Tag = 1U, Identifier = 3U, Value = 2U };

bool Equal(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool Zero(const laplace_digest256& digest) {
    for (const auto byte : digest.bytes) if (byte != 0U) return false;
    return true;
}

using EntityKey = std::array<std::uint8_t, LAPLACE_IDENTITY_BYTES>;
EntityKey Key(const laplace_id128& id) {
    EntityKey key{};
    std::memcpy(key.data(), id.bytes, key.size());
    return key;
}

std::uint64_t Binary64(const double& value) {
    static_assert(sizeof(value) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    std::uint64_t result{};
    std::memcpy(&result, &value, sizeof(result));
#if defined(LAPLACE_TEST_PHYSICALITY_ENTITY_NORMALIZE_SIGNED_ZERO)
    if (value == 0.0) result = 0U;
#endif
    return result;
}

laplace_physicality_entity_status Validate(
    const laplace_physicality_entity_input& input,
    std::vector<laplace_composition_occurrence>& occurrences,
    laplace_physicality_entity_validation& validation) {
    const auto& record = *input.physicality;
    laplace_digest256 actual{};
    if (laplace_persistence_physicality_identify(&record, &actual) !=
            LAPLACE_PERSISTENCE_OK || !Equal(actual, record.physicality_id))
        return LAPLACE_PHYSICALITY_ENTITY_RECORD_INVALID;
    if (record.logical_count > input.maximum_logical_count)
        return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    if (record.vertex_count != input.carrier_count)
        return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
    validation.realized_entity_id = record.entity_id;
    if (record.physicality_type == LAPLACE_PERSISTENCE_PHYSICALITY_ATOMIC_POINT)
        return LAPLACE_PHYSICALITY_ENTITY_OK;
    if (laplace_persistence_trajectory_fingerprint(input.carriers,
            static_cast<std::size_t>(input.carrier_count), &actual) !=
            LAPLACE_PERSISTENCE_OK || !Equal(actual, record.trajectory_fingerprint))
        return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
    std::vector<laplace_id_run> runs;
    runs.reserve(static_cast<std::size_t>(input.carrier_count));
    occurrences.reserve(static_cast<std::size_t>(input.carrier_count));
    std::uint64_t total = 0U;
    std::uint8_t maximum_child_tier = 0U;
    for (std::uint64_t index = 0U; index < input.carrier_count; ++index) {
        laplace_composition_occurrence occurrence{};
        if (total == UINT64_MAX ||
            laplace_trajectory_composition_decode_one(&input.carriers[index],
                total + 1U, &occurrence) != LAPLACE_TRAJECTORY_OK ||
            UINT64_MAX - total < occurrence.run_length)
            return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
        if (occurrence.has_atom != 0U) {
            laplace_id128 atom{};
            if (occurrence.tier != 0U || laplace_identity_codepoint(
                    occurrence.atom, &atom) != LAPLACE_IDENTITY_OK ||
                !laplace_identity_equal(&atom, &occurrence.entity_id))
                return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
        }
        total += occurrence.run_length;
        if (occurrence.tier > maximum_child_tier) maximum_child_tier = occurrence.tier;
        occurrences.push_back(occurrence);
        runs.push_back({occurrence.entity_id, occurrence.run_length});
    }
    laplace_id128 realized{};
    std::uint64_t identity_count{};
    if (total != record.logical_count)
        return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
    const auto identity_status = total >= 2U
        ? laplace_identity_composite_runs_witness(runs.data(), runs.size(), nullptr,
            &identity_count, &realized, &validation.realized_identity_witness)
        : laplace_identity_composite_runs(runs.data(), runs.size(), &identity_count, &realized);
    if (identity_status != LAPLACE_IDENTITY_OK || identity_count != total ||
        !laplace_identity_equal(&realized, &record.entity_id))
        return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
    if (total >= 2U) {
        if (maximum_child_tier >= LAPLACE_COMPOSITION_TIER_MAXIMUM)
            return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
        validation.witness_available = 1U;
        validation.tier_available = 1U;
        validation.tier_floor = static_cast<std::uint8_t>(maximum_child_tier + 1U);
    } else if (occurrences[0].has_atom != 0U) {
        if (laplace_identity_codepoint_witness(occurrences[0].atom, &realized,
                &validation.realized_identity_witness) != LAPLACE_IDENTITY_OK)
            return LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID;
        validation.witness_available = 1U;
        validation.tier_available = 1U;
        validation.tier_floor = 0U;
    }
    return LAPLACE_PHYSICALITY_ENTITY_OK;
}
}  // namespace

laplace_physicality_entity_status laplace_physicality_entity_validation_memory_bound(
    std::uint64_t carrier_count, std::uint64_t* bytes) {
    if (bytes == nullptr) return LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT;
    *bytes = 0U;
    // Reserved occurrence and identity-run vectors hold fewer than 128 bytes
    // per carrier. The fixed envelope covers hashing, allocator and call state.
    if (carrier_count > (UINT64_MAX - UINT64_C(65536)) / UINT64_C(128))
        return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    *bytes = UINT64_C(65536) + carrier_count * UINT64_C(128);
    return LAPLACE_PHYSICALITY_ENTITY_OK;
}

laplace_physicality_entity_status laplace_physicality_entity_plan_memory_bound(
    const laplace_physicality_entity_input* input, std::uint64_t* bytes) {
    if (bytes == nullptr) return LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT;
    *bytes = 0U;
    if (input == nullptr) return LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT;
    const auto carriers = input->carrier_count;
    if (carriers > (UINT64_MAX - UINT64_C(16384)) / UINT64_C(256))
        return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    // The fixed record has fewer than 1024 requests/16384 operands; one
    // occurrence adds at most 16 requests and 256 operands, including unsigned
    // 20-digit spellings. Shared maps can only reduce those bounds. Per-request
    // and per-operand allowances include doubling vector capacity and tree-map
    // keys; per-carrier allowance includes external indexes and scratch vectors.
    const auto requests = std::min(input->maximum_requests,
        UINT64_C(1024) + carriers * UINT64_C(16));
    const auto operands = std::min(input->maximum_operands,
        UINT64_C(16384) + carriers * UINT64_C(256));
    std::uint64_t result = UINT64_C(65536);
    const auto add = [&result](std::uint64_t count, std::uint64_t width) {
        if (count > (UINT64_MAX - result) / width) return false;
        result += count * width;
        return true;
    };
    if (!add(requests, UINT64_C(512)) || !add(operands, UINT64_C(128)) ||
        !add(carriers, UINT64_C(512))) return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    *bytes = result;
    return LAPLACE_PHYSICALITY_ENTITY_OK;
}

laplace_physicality_entity_status laplace_physicality_entity_record_validate(
    const laplace_persistence_physicality_record* physicality,
    const laplace_trajectory_carrier* carriers, std::uint64_t carrier_count,
    std::uint64_t maximum_carriers, std::uint64_t maximum_logical_count,
    laplace_physicality_entity_validation* validation) {
    if (validation != nullptr) *validation = {};
    if (physicality == nullptr || (carrier_count != 0U && carriers == nullptr))
        return LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT;
    if (carrier_count > maximum_carriers ||
        carrier_count > SIZE_MAX / sizeof(laplace_composition_occurrence))
        return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    try {
        laplace_physicality_entity_input input{};
        input.physicality = physicality;
        input.carriers = carriers;
        input.carrier_count = carrier_count;
        input.maximum_logical_count = maximum_logical_count;
        std::vector<laplace_composition_occurrence> occurrences;
        laplace_physicality_entity_validation result{};
        const auto status = Validate(input, occurrences, result);
        if (status == LAPLACE_PHYSICALITY_ENTITY_OK && validation != nullptr)
            *validation = result;
        return status;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PHYSICALITY_ENTITY_MEMORY_FAILURE;
    } catch (...) {
        return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    }
}

struct laplace_physicality_entity_plan {
    laplace_physicality_entity_plan_view view{};
    laplace_digest256 no_occurrence_context{};
    std::vector<laplace_id128> external_entities;
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
};

namespace {
class Builder final : public laplace::detail::CanonicalCompositionPlanBuilder<Role> {
public:
    Builder(laplace_physicality_entity_plan& plan,
            const laplace_physicality_entity_input& input,
            const std::map<EntityKey, std::uint64_t>& external_indexes)
        : CanonicalCompositionPlanBuilder(plan.atom_positions, plan.operands,
              plan.requests, plan.view.recipe_fingerprint,
              plan.view.view_geometry_epoch, plan.no_occurrence_context, Version,
              0U, 6U, plan.external_entities.size(), input.maximum_requests,
              input.maximum_operands), plan_(plan), external_indexes_(external_indexes) {}

    bool Build(const laplace_persistence_physicality_record& record,
               const std::vector<laplace_composition_occurrence>& occurrences) {
#if defined(LAPLACE_TEST_PHYSICALITY_ENTITY_PRIVATE_NUMBER_TAG)
        number_tag_ = Text("Number");
#else
        number_tag_ = Text(laplace::detail::CanonicalNumberTag);
#endif
        const auto root_tag = Text("PhysicalityRecord");
        std::vector<std::pair<std::uint64_t, Role>> fields{
            {root_tag, Role::Tag},
            {Field("version", Numeric(Version)), Role::Value},
            {Reference("entity", record.entity_id), Role::Value},
            {Field("type", Numeric(record.physicality_type)), Role::Value},
            {Field("vertex-class", Numeric(record.vertex_class)), Role::Value},
            {Field("recipe-version", Numeric(record.recipe_version)), Role::Value},
            {Field("form", Numeric(record.structural_form)), Role::Value},
            {Field("dimensions", Numeric(record.dimension_count)), Role::Value},
            {Field("flags", Numeric(record.flags)), Role::Value},
            {Field("recipe", Octets(record.recipe_fingerprint)), Role::Value},
            {Field("geometry", Octets(record.geometry_epoch)), Role::Value},
            {Field("trajectory-fingerprint", Octets(record.trajectory_fingerprint)), Role::Value}};
        std::vector<std::pair<std::uint64_t, Role>> centroid{
            {Text("Binary64Vector"), Role::Tag}};
        for (const double& coordinate : record.centroid.component)
            centroid.push_back({Floating(coordinate), Role::Value});
        fields.push_back({Field("centroid", Node(centroid)), Role::Value});
        fields.push_back({Field("radius", Floating(record.radius)), Role::Value});
        fields.push_back({Field("logical-count", Numeric(record.logical_count)), Role::Value});
        fields.push_back({Field("vertex-count", Numeric(record.vertex_count)), Role::Value});
        std::vector<std::pair<std::uint64_t, Role>> trajectory{
            {Text("Trajectory"), Role::Tag},
            {Field("count", Numeric(occurrences.size())), Role::Value}};
        for (const auto& occurrence : occurrences) {
            trajectory.push_back({Node({
                {Text("Occurrence"), Role::Tag},
                {Reference("entity", occurrence.entity_id), Role::Value},
                {Field("ordinal", Numeric(occurrence.logical_ordinal)), Role::Value},
                {Field("run-length", Numeric(occurrence.run_length)), Role::Value},
                {Field("metadata", Numeric(occurrence.metadata)), Role::Value}}), Role::Value});
        }
        fields.push_back({Field("trajectory", Node(trajectory)), Role::Value});
        plan_.view.root_result_index = Node(fields);
        return plan_.view.root_result_index != InvalidIndex;
    }

private:
    std::uint64_t Text(const std::string_view value) {
        const auto result = String(value);
        return result.has_value ? result.index : InvalidIndex;
    }
    std::uint64_t Numeric(const std::uint64_t value) { return Number(value, number_tag_); }
    std::uint64_t Field(const std::string_view name, const std::uint64_t value) {
        return Node({{Text(name), Role::Tag}, {value, Role::Value}});
    }
    std::uint64_t Floating(const double& value) {
        return Node({{Text("Binary64"), Role::Tag}, {Numeric(Binary64(value)), Role::Value}});
    }
    std::uint64_t Octets(const laplace_digest256& digest) {
        std::vector<std::pair<std::uint64_t, Role>> bytes{{Text("Octets"), Role::Tag}};
        for (const auto byte : digest.bytes) bytes.push_back({Numeric(byte), Role::Value});
        return Node(bytes);
    }
    std::uint64_t Reference(const std::string_view name, const laplace_id128& entity) {
        const auto found = external_indexes_.find(Key(entity));
        if (found == external_indexes_.end()) return InvalidIndex;
        return NodeReferences({
            {Text(name), 1U, static_cast<std::uint64_t>(Role::Tag) << 6U,
             LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT, 0U},
            {found->second, 1U, static_cast<std::uint64_t>(Role::Value) << 6U,
             LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U}});
    }
    laplace_physicality_entity_plan& plan_;
    const std::map<EntityKey, std::uint64_t>& external_indexes_;
    std::uint64_t number_tag_{InvalidIndex};
};
}  // namespace

laplace_physicality_entity_status laplace_physicality_entity_plan_create(
    const laplace_physicality_entity_input* input,
    laplace_physicality_entity_plan** plan) {
    if (plan == nullptr) return LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT;
    *plan = nullptr;
    if (input == nullptr || input->physicality == nullptr ||
        (input->carrier_count != 0U && input->carriers == nullptr) ||
        Zero(input->view_geometry_epoch) || input->maximum_requests == 0U ||
        input->maximum_operands == 0U)
        return LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT;
    if (input->carrier_count > input->maximum_carriers ||
        input->carrier_count > SIZE_MAX / sizeof(laplace_composition_occurrence))
        return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    try {
        std::vector<laplace_composition_occurrence> occurrences;
        laplace_physicality_entity_validation validation{};
        const auto status = Validate(*input, occurrences, validation);
        if (status != LAPLACE_PHYSICALITY_ENTITY_OK) return status;
        auto created = std::make_unique<laplace_physicality_entity_plan>();
        created->view.physicality_record_id = input->physicality->physicality_id;
        created->view.realized_entity_id = input->physicality->entity_id;
        created->view.view_geometry_epoch = input->view_geometry_epoch;
        created->view.version = Version;
        created->view.source_validation = validation;
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, RecipeLaw.data(), RecipeLaw.size());
        blake3_hasher_finalize(&hasher, created->view.recipe_fingerprint.bytes,
            sizeof(created->view.recipe_fingerprint.bytes));
        std::map<EntityKey, std::uint64_t> external_indexes;
        const auto add = [&](const laplace_id128& entity) {
            const auto key = Key(entity);
            if (!external_indexes.contains(key)) {
                external_indexes.emplace(key, created->external_entities.size());
                created->external_entities.push_back(entity);
            }
        };
        add(input->physicality->entity_id);
        for (const auto& occurrence : occurrences) add(occurrence.entity_id);
        Builder builder(*created, *input, external_indexes);
        if (!builder.Build(*input->physicality, occurrences))
            return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
        created->view.external_entity_ids = created->external_entities.data();
        created->view.atom_positions = created->atom_positions.data();
        created->view.operands = created->operands.data();
        created->view.requests = created->requests.data();
        created->view.external_entity_count = created->external_entities.size();
        created->view.atom_count = created->atom_positions.size();
        created->view.operand_count = created->operands.size();
        created->view.request_count = created->requests.size();
        *plan = created.release();
        return LAPLACE_PHYSICALITY_ENTITY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_PHYSICALITY_ENTITY_MEMORY_FAILURE;
    } catch (...) {
        return LAPLACE_PHYSICALITY_ENTITY_LIMIT;
    }
}

laplace_physicality_entity_status laplace_physicality_entity_plan_view_get(
    const laplace_physicality_entity_plan* plan,
    laplace_physicality_entity_plan_view* view) {
    if (plan == nullptr || view == nullptr)
        return LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT;
    *view = plan->view;
    return LAPLACE_PHYSICALITY_ENTITY_OK;
}

void laplace_physicality_entity_plan_destroy(laplace_physicality_entity_plan** plan) {
    if (plan != nullptr) { delete *plan; *plan = nullptr; }
}
