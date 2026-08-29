#include "laplace/tabular_decomposition.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "blake3.h"
#include "laplace/decomposition.h"
#include "laplace/decomposition_delimited.h"
#include "laplace/decomposition_uax29.h"

namespace {

constexpr std::uint32_t Utf8DelimitedMode = 2u;
constexpr std::uint64_t DelimitedKindBase = UINT64_C(0x5441424c45000000);
constexpr std::string_view DelimitedProviderDomain{
    "laplace-tabular-recursive-delimited-provider-v1"};
constexpr std::string_view DecompositionRecipeDomain{
    "laplace-tabular-recursive-decomposition-recipe-v1"};
constexpr std::string_view InternalTabularMediaType{
    "application/x-laplace-tabular-field-container"};

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) return false;
    }
    return true;
}

void HashU32(blake3_hasher& hasher, const std::uint32_t value) {
    std::uint8_t bytes[4]{};
    for (std::size_t index = 0u; index < 4u; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes, sizeof(bytes));
}

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::uint8_t bytes[8]{};
    for (std::size_t index = 0u; index < 8u; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes, sizeof(bytes));
}

void HashBlob(blake3_hasher& hasher, const void* bytes, const std::size_t count) {
    HashU64(hasher, static_cast<std::uint64_t>(count));
    if (count != 0u) blake3_hasher_update(&hasher, bytes, count);
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

laplace_digest256 DelimitedProviderFingerprint(
    const laplace_tabular_source_plan_view& source,
    const laplace_tabular_artifact& artifact) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashBlob(hasher, DelimitedProviderDomain.data(), DelimitedProviderDomain.size());
    HashBlob(
        hasher,
        source.profile.recipe_program_fingerprint.bytes,
        sizeof(source.profile.recipe_program_fingerprint.bytes));
    HashU32(hasher, artifact.delimiter);
    HashU32(hasher, artifact.line_terminator);
    HashU32(hasher, artifact.expected_column_count);
    HashU32(hasher, artifact.header_record_count);
    for (std::uint32_t column = 0u; column < artifact.expected_column_count; ++column) {
        const laplace_tabular_column& value = artifact.columns[column];
        HashBlob(
            hasher,
            value.bytes,
            static_cast<std::size_t>(value.byte_count));
    }
    return Finish(hasher);
}

laplace_digest256 DecompositionRecipeFingerprint(
    const laplace_tabular_source_plan_view& source,
    const laplace_digest256& uax29_provider) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashBlob(hasher, DecompositionRecipeDomain.data(), DecompositionRecipeDomain.size());
    HashBlob(
        hasher,
        source.profile.recipe_program_fingerprint.bytes,
        sizeof(source.profile.recipe_program_fingerprint.bytes));
    HashBlob(hasher, uax29_provider.bytes, sizeof(uax29_provider.bytes));
    return Finish(hasher);
}

}  // namespace

struct laplace_tabular_decomposition_plan {
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::vector<laplace_tabular_decomposition_occurrence> occurrences;
    std::uint64_t artifact_count{};
};

extern "C" laplace_tabular_decomposition_status
laplace_tabular_decomposition_plan_create(
    const laplace_tabular_decomposition_input* input,
    laplace_tabular_decomposition_plan** output) {
    if (input == nullptr || output == nullptr) {
        return LAPLACE_TABULAR_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *output = nullptr;
    if (input->source_plan == nullptr || input->artifacts == nullptr ||
        input->artifact_count == 0u ||
        input->artifact_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        input->uax29 == nullptr || DigestZero(input->uax29_provider_fingerprint) ||
        DigestZero(input->geometry_epoch) ||
        DigestZero(input->occurrence_context_fingerprint) ||
        input->first_source_ordinal == 0u || input->flags != 0u ||
        input->reserved != 0u) {
        return LAPLACE_TABULAR_DECOMPOSITION_INVALID_ARGUMENT;
    }

    laplace_tabular_source_plan_view source_view{};
    if (laplace_tabular_source_plan_view_get(
            input->source_plan, &source_view) != LAPLACE_TABULAR_SOURCE_OK ||
        source_view.artifact_count != input->artifact_count ||
        source_view.recipe_version == 0u ||
        DigestZero(source_view.profile.recipe_program_fingerprint)) {
        return LAPLACE_TABULAR_DECOMPOSITION_SOURCE_PLAN_INVALID;
    }

    auto* plan = new (std::nothrow) laplace_tabular_decomposition_plan{};
    if (plan == nullptr) return LAPLACE_TABULAR_DECOMPOSITION_MEMORY_FAILURE;

    try {
        std::unordered_map<std::uint32_t, std::uint64_t> atom_indexes;
        atom_indexes.reserve(4096u);
        const laplace_digest256 decomposition_recipe =
            DecompositionRecipeFingerprint(source_view, input->uax29_provider_fingerprint);
        std::uint64_t next_source_ordinal = input->first_source_ordinal;

        for (std::uint64_t artifact_index = 0u;
             artifact_index < input->artifact_count; ++artifact_index) {
            const laplace_tabular_artifact& artifact =
                input->artifacts[static_cast<std::size_t>(artifact_index)];
            if (artifact.mode != Utf8DelimitedMode) continue;
            if (artifact.bytes == nullptr || artifact.byte_count == 0u ||
                artifact.byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
                artifact.columns == nullptr || artifact.expected_column_count == 0u ||
                artifact.name == nullptr || artifact.name_byte_count == 0u ||
                artifact.name_byte_count > static_cast<std::uint64_t>(SIZE_MAX)) {
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_SOURCE_PLAN_INVALID;
            }

            laplace_decomposition_uax29_provider uax_provider{};
            if (laplace_decomposition_uax29_provider_init(
                    &uax_provider,
                    input->uax29,
                    &input->uax29_provider_fingerprint) != LAPLACE_DECOMPOSITION_OK) {
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_PROVIDER_FAILURE;
            }

            laplace_decomposition_delimited_provider delimited_provider{};
            const laplace_digest256 delimited_fingerprint =
                DelimitedProviderFingerprint(source_view, artifact);
            if (laplace_decomposition_delimited_provider_init(
                    &delimited_provider,
                    artifact.delimiter,
                    artifact.line_terminator,
                    artifact.expected_column_count,
                    artifact.header_record_count,
                    DelimitedKindBase,
                    &delimited_fingerprint) != LAPLACE_DECOMPOSITION_OK) {
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_PROVIDER_FAILURE;
            }

            const laplace_decomposition_provider_v1 providers[] = {
                delimited_provider.provider,
                uax_provider.provider};
            laplace_decomposition_input decomposition_input{};
            decomposition_input.content.bytes = artifact.bytes;
            decomposition_input.content.byte_count = artifact.byte_count;
            decomposition_input.content.media_type = InternalTabularMediaType.data();
            decomposition_input.content.media_type_byte_count =
                static_cast<std::uint64_t>(InternalTabularMediaType.size());
            decomposition_input.content.name = artifact.name;
            decomposition_input.content.name_byte_count = artifact.name_byte_count;
            decomposition_input.providers = providers;
            decomposition_input.provider_count = 2u;
            if (artifact.byte_count >
                std::numeric_limits<std::uint64_t>::max() / UINT64_C(24)) {
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_OVERFLOW;
            }
            decomposition_input.maximum_spans =
                artifact.byte_count * UINT64_C(24) + UINT64_C(4096);
            decomposition_input.maximum_depth = 8u;

            laplace_decomposition_result* decomposition = nullptr;
            const laplace_decomposition_status decomposition_status =
                laplace_decomposition_run(&decomposition_input, &decomposition);
            if (decomposition_status != LAPLACE_DECOMPOSITION_OK || decomposition == nullptr) {
                laplace_decomposition_result_destroy(&decomposition);
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_PROVIDER_FAILURE;
            }

            laplace_decomposition_composition_input lowering_input{};
            lowering_input.decomposition = decomposition;
            lowering_input.bytes = artifact.bytes;
            lowering_input.byte_count = artifact.byte_count;
            lowering_input.recipe_fingerprint = decomposition_recipe;
            lowering_input.geometry_epoch = input->geometry_epoch;
            lowering_input.occurrence_context_fingerprint =
                input->occurrence_context_fingerprint;
            lowering_input.first_source_ordinal = next_source_ordinal;
            lowering_input.recipe_version = source_view.recipe_version;

            laplace_decomposition_composition_plan* lowered = nullptr;
            const laplace_decomposition_composition_status lowering_status =
                laplace_decomposition_composition_plan_create(
                    &lowering_input, &lowered);
            if (lowering_status != LAPLACE_DECOMPOSITION_COMPOSITION_OK ||
                lowered == nullptr) {
                laplace_decomposition_composition_plan_destroy(&lowered);
                laplace_decomposition_result_destroy(&decomposition);
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_COMPOSITION_FAILURE;
            }

            laplace_decomposition_composition_plan_view lowered_view{};
            if (laplace_decomposition_composition_plan_view_get(
                    lowered, &lowered_view) !=
                    LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
                laplace_decomposition_composition_plan_destroy(&lowered);
                laplace_decomposition_result_destroy(&decomposition);
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_COMPOSITION_FAILURE;
            }

            std::vector<std::uint64_t> local_to_global;
            local_to_global.resize(static_cast<std::size_t>(lowered_view.atom_count));
            for (std::uint64_t local_index = 0u;
                 local_index < lowered_view.atom_count; ++local_index) {
                const std::uint32_t codepoint =
                    lowered_view.atom_positions[static_cast<std::size_t>(local_index)];
                const auto found = atom_indexes.find(codepoint);
                if (found == atom_indexes.end()) {
                    const std::uint64_t global_index =
                        static_cast<std::uint64_t>(plan->atom_positions.size());
                    plan->atom_positions.push_back(codepoint);
                    atom_indexes.emplace(codepoint, global_index);
                    local_to_global[static_cast<std::size_t>(local_index)] = global_index;
                } else {
                    local_to_global[static_cast<std::size_t>(local_index)] = found->second;
                }
            }

            const std::uint64_t operand_base =
                static_cast<std::uint64_t>(plan->operands.size());
            const std::uint64_t request_base =
                static_cast<std::uint64_t>(plan->requests.size());
            plan->operands.reserve(
                plan->operands.size() + static_cast<std::size_t>(lowered_view.operand_count));
            for (std::uint64_t index = 0u; index < lowered_view.operand_count; ++index) {
                laplace_composition_operand operand =
                    lowered_view.operands[static_cast<std::size_t>(index)];
                if (operand.reference_kind != LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY ||
                    operand.known_entity_index >= lowered_view.atom_count) {
                    laplace_decomposition_composition_plan_destroy(&lowered);
                    laplace_decomposition_result_destroy(&decomposition);
                    delete plan;
                    return LAPLACE_TABULAR_DECOMPOSITION_COMPOSITION_FAILURE;
                }
                operand.known_entity_index = local_to_global[
                    static_cast<std::size_t>(operand.known_entity_index)];
                plan->operands.push_back(operand);
            }

            plan->requests.reserve(
                plan->requests.size() + static_cast<std::size_t>(lowered_view.request_count));
            for (std::uint64_t index = 0u; index < lowered_view.request_count; ++index) {
                laplace_composition_request request =
                    lowered_view.requests[static_cast<std::size_t>(index)];
                if (request.first_operand_index >
                    std::numeric_limits<std::uint64_t>::max() - operand_base) {
                    laplace_decomposition_composition_plan_destroy(&lowered);
                    laplace_decomposition_result_destroy(&decomposition);
                    delete plan;
                    return LAPLACE_TABULAR_DECOMPOSITION_OVERFLOW;
                }
                request.first_operand_index += operand_base;
                plan->requests.push_back(request);
            }

            plan->occurrences.reserve(
                plan->occurrences.size() +
                static_cast<std::size_t>(lowered_view.occurrence_count));
            for (std::uint64_t index = 0u; index < lowered_view.occurrence_count; ++index) {
                laplace_tabular_decomposition_occurrence occurrence{};
                occurrence.occurrence =
                    lowered_view.occurrences[static_cast<std::size_t>(index)];
                if (occurrence.occurrence.composition_result_index >
                    std::numeric_limits<std::uint64_t>::max() - request_base) {
                    laplace_decomposition_composition_plan_destroy(&lowered);
                    laplace_decomposition_result_destroy(&decomposition);
                    delete plan;
                    return LAPLACE_TABULAR_DECOMPOSITION_OVERFLOW;
                }
                occurrence.occurrence.composition_result_index += request_base;
                occurrence.artifact_index = artifact_index;
                plan->occurrences.push_back(occurrence);
            }

            if (lowered_view.request_count >
                std::numeric_limits<std::uint64_t>::max() - next_source_ordinal) {
                laplace_decomposition_composition_plan_destroy(&lowered);
                laplace_decomposition_result_destroy(&decomposition);
                delete plan;
                return LAPLACE_TABULAR_DECOMPOSITION_OVERFLOW;
            }
            next_source_ordinal += lowered_view.request_count;
            ++plan->artifact_count;
            laplace_decomposition_composition_plan_destroy(&lowered);
            laplace_decomposition_result_destroy(&decomposition);
        }
    } catch (...) {
        delete plan;
        return LAPLACE_TABULAR_DECOMPOSITION_MEMORY_FAILURE;
    }

    if (plan->requests.empty()) {
        delete plan;
        return LAPLACE_TABULAR_DECOMPOSITION_SOURCE_PLAN_INVALID;
    }
    *output = plan;
    return LAPLACE_TABULAR_DECOMPOSITION_OK;
}

extern "C" laplace_tabular_decomposition_status
laplace_tabular_decomposition_plan_view_get(
    const laplace_tabular_decomposition_plan* plan,
    laplace_tabular_decomposition_plan_view* view) {
    if (plan == nullptr || view == nullptr) {
        return LAPLACE_TABULAR_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *view = laplace_tabular_decomposition_plan_view{};
    view->atom_positions = plan->atom_positions.data();
    view->operands = plan->operands.data();
    view->requests = plan->requests.data();
    view->occurrences = plan->occurrences.data();
    view->atom_count = static_cast<std::uint64_t>(plan->atom_positions.size());
    view->operand_count = static_cast<std::uint64_t>(plan->operands.size());
    view->request_count = static_cast<std::uint64_t>(plan->requests.size());
    view->occurrence_count = static_cast<std::uint64_t>(plan->occurrences.size());
    view->artifact_count = plan->artifact_count;
    return LAPLACE_TABULAR_DECOMPOSITION_OK;
}

extern "C" void laplace_tabular_decomposition_plan_destroy(
    laplace_tabular_decomposition_plan** plan) {
    if (plan == nullptr) return;
    delete *plan;
    *plan = nullptr;
}
