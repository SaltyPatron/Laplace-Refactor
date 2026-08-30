/*
 * The legacy tabular recipe remains the exact envelope/reconstruction authority.
 * Rename only its create entrypoint inside this translation unit, then layer the
 * generic recursive decomposition product path over the resulting plan.
 */
#define laplace_tabular_source_plan_create laplace_tabular_source_plan_create_legacy
#include "tabular_source_legacy.inc"
#undef laplace_tabular_source_plan_create

#include "laplace/tabular_source_recursive.h"
#include "laplace/decomposition_composition.h"
#include "laplace/decomposition_delimited.h"
#include "laplace/decomposition_uax29.h"

namespace recursive_admission {

constexpr std::uint64_t SupplementalRoleShift = 6u;
constexpr std::uint64_t LegacyRootRole = UINT64_C(32) << SupplementalRoleShift;
constexpr std::uint64_t DecompositionTraceRole =
    UINT64_C(33) << SupplementalRoleShift;
constexpr std::uint64_t DelimitedKindBase = UINT64_C(0x5441424c00000000);
constexpr std::uint32_t RecursiveRecipeVersion = 1u;

bool RecursiveAdd(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) return false;
    output = left + right;
    return true;
}

void RecursiveHashU32(blake3_hasher& hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void RecursiveHashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void RecursiveHashBytes(
    blake3_hasher& hasher,
    const void* bytes,
    const std::size_t count) {
    RecursiveHashU64(hasher, static_cast<std::uint64_t>(count));
    if (count != 0u) blake3_hasher_update(&hasher, bytes, count);
}

laplace_digest256 RecursiveFinish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

laplace_digest256 UaxProviderFingerprint(
    const laplace_unicode_source_receipt& receipt) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    static constexpr std::string_view Domain{
        "laplace.decomposition.provider.uax29/v1"};
    RecursiveHashBytes(hasher, Domain.data(), Domain.size());
    RecursiveHashBytes(
        hasher, receipt.source_fingerprint.bytes,
        sizeof(receipt.source_fingerprint.bytes));
    RecursiveHashBytes(
        hasher, receipt.recipe_fingerprint.bytes,
        sizeof(receipt.recipe_fingerprint.bytes));
    return RecursiveFinish(hasher);
}

laplace_digest256 DelimitedProviderFingerprint(
    const laplace_tabular_source_input& input,
    const laplace_tabular_artifact& artifact,
    const std::size_t artifact_index) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    static constexpr std::string_view Domain{
        "laplace.decomposition.provider.delimited/v1"};
    RecursiveHashBytes(hasher, Domain.data(), Domain.size());
    RecursiveHashBytes(
        hasher,
        input.profile_declaration.syntax_authority_fingerprint.bytes,
        sizeof(input.profile_declaration.syntax_authority_fingerprint.bytes));
    RecursiveHashU64(hasher, static_cast<std::uint64_t>(artifact_index));
    RecursiveHashU32(hasher, artifact.delimiter);
    RecursiveHashU32(hasher, artifact.line_terminator);
    RecursiveHashU32(hasher, artifact.expected_column_count);
    RecursiveHashU32(hasher, artifact.header_record_count);
    return RecursiveFinish(hasher);
}

laplace_tabular_source_status DecompositionStatus(
    const laplace_decomposition_status status) {
    switch (status) {
        case LAPLACE_DECOMPOSITION_OK:
            return LAPLACE_TABULAR_SOURCE_OK;
        case LAPLACE_DECOMPOSITION_MEMORY_FAILURE:
            return LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
        case LAPLACE_DECOMPOSITION_LIMIT_EXCEEDED:
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        case LAPLACE_DECOMPOSITION_RANGE_INVALID:
        case LAPLACE_DECOMPOSITION_PROVIDER_FAILURE:
        case LAPLACE_DECOMPOSITION_PROVIDER_INVALID:
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        default:
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
}

laplace_tabular_source_status CompositionStatus(
    const laplace_decomposition_composition_status status) {
    switch (status) {
        case LAPLACE_DECOMPOSITION_COMPOSITION_OK:
            return LAPLACE_TABULAR_SOURCE_OK;
        case LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID:
            return LAPLACE_TABULAR_SOURCE_UTF8_INVALID;
        case LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE:
            return LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
        case LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW:
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        case LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID:
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        default:
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
}

std::uint64_t MergeAtom(
    laplace_tabular_source_plan& destination,
    std::map<std::uint32_t, std::uint64_t>& destination_atoms,
    const std::uint32_t position) {
    const auto found = destination_atoms.find(position);
    if (found != destination_atoms.end()) return found->second;
    const std::uint64_t index =
        static_cast<std::uint64_t>(destination.atom_positions.size());
    destination.atom_positions.push_back(position);
    destination_atoms.emplace(position, index);
    return index;
}

laplace_tabular_source_status MergeDecompositionPlan(
    laplace_tabular_source_plan& destination,
    const laplace_decomposition_composition_plan_view& source,
    std::map<std::uint32_t, std::uint64_t>& destination_atoms,
    std::uint64_t& merged_root) {
    if (source.atom_positions == nullptr || source.operands == nullptr ||
        source.requests == nullptr || source.atom_count == 0u ||
        source.operand_count == 0u || source.request_count == 0u ||
        source.root_result_index >= source.request_count) {
        return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
    }
    const std::uint64_t request_base =
        static_cast<std::uint64_t>(destination.requests.size());
    const std::uint64_t operand_base =
        static_cast<std::uint64_t>(destination.operands.size());

    for (std::uint64_t operand_index = 0u;
         operand_index < source.operand_count; ++operand_index) {
        laplace_composition_operand operand =
            source.operands[static_cast<std::size_t>(operand_index)];
        if (operand.reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY) {
            if (operand.reference_index >= source.atom_count) {
                return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            }
            const std::uint32_t position =
                source.atom_positions[
                    static_cast<std::size_t>(operand.reference_index)];
            operand.reference_index =
                MergeAtom(destination, destination_atoms, position);
        } else if (
            operand.reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT) {
            if (operand.reference_index >= source.request_count ||
                operand.reference_index >
                    std::numeric_limits<std::uint64_t>::max() - request_base) {
                return LAPLACE_TABULAR_SOURCE_OVERFLOW;
            }
            operand.reference_index += request_base;
        } else {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
        destination.operands.push_back(operand);
    }

    for (std::uint64_t request_index = 0u;
         request_index < source.request_count; ++request_index) {
        laplace_composition_request request =
            source.requests[static_cast<std::size_t>(request_index)];
        if (request.first_operand > source.operand_count ||
            request.operand_count >
                source.operand_count - request.first_operand ||
            request.first_operand >
                std::numeric_limits<std::uint64_t>::max() - operand_base) {
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        }
        request.first_operand += operand_base;
        const std::uint64_t destination_index =
            static_cast<std::uint64_t>(destination.requests.size());
        if (destination_index == std::numeric_limits<std::uint64_t>::max()) {
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        }
        request.source_ordinal = destination_index + 1u;
        destination.requests.push_back(request);
    }

    if (source.root_result_index >
        std::numeric_limits<std::uint64_t>::max() - request_base) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }
    merged_root = request_base + source.root_result_index;
    return LAPLACE_TABULAR_SOURCE_OK;
}

laplace_tabular_source_status AppendRecursiveRoot(
    laplace_tabular_source_plan& plan,
    const laplace_tabular_source_input& input,
    const std::uint64_t legacy_root,
    const std::vector<std::uint64_t>& trace_roots) {
    if (trace_roots.empty() || legacy_root >= plan.requests.size()) {
        return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
    }
    const std::uint64_t first =
        static_cast<std::uint64_t>(plan.operands.size());
    plan.operands.push_back(laplace_composition_operand{
        legacy_root,
        1u,
        LegacyRootRole,
        LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT,
        0u});
    for (const std::uint64_t trace_root : trace_roots) {
        if (trace_root >= plan.requests.size()) {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
        plan.operands.push_back(laplace_composition_operand{
            trace_root,
            1u,
            DecompositionTraceRole,
            LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT,
            0u});
    }
    const std::uint64_t index =
        static_cast<std::uint64_t>(plan.requests.size());
    if (index == std::numeric_limits<std::uint64_t>::max()) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }
    plan.requests.push_back(laplace_composition_request{
        first,
        static_cast<std::uint64_t>(trace_roots.size() + 1u),
        index + 1u,
        RecursiveRecipeVersion,
        0u,
        input.profile_declaration.recipe_program_fingerprint,
        input.geometry_epoch,
        input.occurrence_context_fingerprint});
    plan.view.root_result_index = index;
    return LAPLACE_TABULAR_SOURCE_OK;
}

laplace_tabular_source_status UpdateProfileCounts(
    laplace_tabular_source_plan& plan,
    const std::uint64_t legacy_output_count,
    const std::uint64_t recursive_span_count) {
    laplace_source_profile_manifest& profile = plan.view.profile;
    const std::uint64_t new_output_count =
        static_cast<std::uint64_t>(plan.requests.size());
    if (new_output_count < legacy_output_count) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }
    const std::uint64_t output_delta =
        new_output_count - legacy_output_count;
    const std::uint64_t new_edge_count =
        static_cast<std::uint64_t>(plan.operands.size());
    std::uint64_t syntax_node_count = 0u;
    std::uint64_t span_count = 0u;
    std::uint64_t closure_subject_count = 0u;
    if (!RecursiveAdd(
            new_output_count, new_edge_count, syntax_node_count) ||
        !RecursiveAdd(
            profile.span_count, recursive_span_count, span_count) ||
        !RecursiveAdd(
            profile.closure_subject_count,
            output_delta,
            closure_subject_count)) {
        return LAPLACE_TABULAR_SOURCE_OVERFLOW;
    }
    profile.edge_count = new_edge_count;
    profile.output_count = new_output_count;
    profile.syntax_node_count = syntax_node_count;
    profile.span_count = span_count;
    profile.closure_subject_count = closure_subject_count;

    std::uint64_t disposition = 0u;
    if (profile.reconstruction_class ==
        LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT) {
        if (!RecursiveAdd(profile.persisted_count, output_delta, disposition)) {
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        }
        profile.persisted_count = disposition;
    } else if (profile.reconstruction_class ==
               LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC) {
        if (!RecursiveAdd(
                profile.transformation_count, output_delta, disposition)) {
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        }
        profile.transformation_count = disposition;
        if (!RecursiveAdd(profile.transformed_count, output_delta, disposition)) {
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        }
        profile.transformed_count = disposition;
    } else {
        if (!RecursiveAdd(profile.lossy_count, output_delta, disposition)) {
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        }
        profile.lossy_count = disposition;
    }
    return LAPLACE_TABULAR_SOURCE_OK;
}

laplace_tabular_source_status BuildRecursive(
    const laplace_tabular_source_input* input,
    const laplace_unicode_source_bundle* unicode_bundle,
    laplace_tabular_source_plan** plan) {
    if (input == nullptr || unicode_bundle == nullptr || plan == nullptr ||
        *plan != nullptr) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }

    laplace_tabular_source_plan* created = nullptr;
    laplace_tabular_source_status status =
        laplace_tabular_source_plan_create_legacy(input, &created);
    if (status != LAPLACE_TABULAR_SOURCE_OK || created == nullptr) {
        return status;
    }

    laplace_unicode_source_receipt unicode_receipt{};
    laplace_uax29_tables* uax_tables = nullptr;
    if (laplace_unicode_source_bundle_receipt(
            unicode_bundle, &unicode_receipt) != LAPLACE_UNICODE_OK ||
        laplace_uax29_tables_create(unicode_bundle, &uax_tables) !=
            LAPLACE_UAX29_OK ||
        uax_tables == nullptr) {
        laplace_uax29_tables_destroy(&uax_tables);
        laplace_tabular_source_plan_destroy(&created);
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }

    const laplace_digest256 uax_fingerprint =
        UaxProviderFingerprint(unicode_receipt);
    laplace_decomposition_uax29_provider uax_provider{};
    if (laplace_decomposition_uax29_provider_init(
            &uax_provider, uax_tables, &uax_fingerprint) !=
        LAPLACE_DECOMPOSITION_OK) {
        laplace_uax29_tables_destroy(&uax_tables);
        laplace_tabular_source_plan_destroy(&created);
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }

    const std::uint64_t legacy_output_count =
        created->view.profile.output_count;
    std::uint64_t recursive_span_count = 0u;
    std::vector<std::uint64_t> trace_roots;
    std::vector<laplace_digest256> trace_fingerprints;
    trace_roots.reserve(static_cast<std::size_t>(input->artifact_count));
    trace_fingerprints.reserve(static_cast<std::size_t>(input->artifact_count));

    std::map<std::uint32_t, std::uint64_t> destination_atoms;
    for (std::size_t atom_index = 0u;
         atom_index < created->atom_positions.size(); ++atom_index) {
        destination_atoms.emplace(
            created->atom_positions[atom_index],
            static_cast<std::uint64_t>(atom_index));
    }

    for (std::size_t artifact_index = 0u;
         artifact_index < static_cast<std::size_t>(input->artifact_count);
         ++artifact_index) {
        const laplace_tabular_artifact& artifact =
            input->artifacts[artifact_index];
        laplace_decomposition_delimited_provider delimited_provider{};
        std::array<laplace_decomposition_provider_v1, 2> providers{};
        providers[0] = uax_provider.provider;
        std::uint64_t provider_count = 1u;

        if (artifact.mode == LAPLACE_TABULAR_ARTIFACT_DELIMITED) {
            const laplace_digest256 grammar_fingerprint =
                DelimitedProviderFingerprint(*input, artifact, artifact_index);
            const std::uint32_t terminator =
                artifact.line_terminator == LAPLACE_TABULAR_TERMINATOR_CRLF
                    ? LAPLACE_DECOMPOSITION_DELIMITED_CRLF
                    : LAPLACE_DECOMPOSITION_DELIMITED_LF;
            const auto provider_status =
                laplace_decomposition_delimited_provider_init(
                    &delimited_provider,
                    artifact.delimiter,
                    terminator,
                    artifact.expected_column_count,
                    artifact.header_record_count,
                    DelimitedKindBase,
                    &grammar_fingerprint);
            if (provider_status != LAPLACE_DECOMPOSITION_OK) {
                status = DecompositionStatus(provider_status);
                goto recursive_failure;
            }
            providers[1] = delimited_provider.provider;
            provider_count = 2u;
        }

        static constexpr char FallbackDelimitedMediaType[] =
            "text/tab-separated-values";
        laplace_decomposition_content content{};
        content.bytes = artifact.bytes;
        content.byte_count = artifact.byte_count;
        content.name = artifact.name;
        content.name_byte_count = artifact.name_byte_count;
        if (artifact.media_type != nullptr) {
            content.media_type = artifact.media_type;
            content.media_type_byte_count = artifact.media_type_byte_count;
        } else if (artifact.mode == LAPLACE_TABULAR_ARTIFACT_DELIMITED) {
            content.media_type = FallbackDelimitedMediaType;
            content.media_type_byte_count =
                sizeof(FallbackDelimitedMediaType) - 1u;
        }

        if (artifact.byte_count >
            (std::numeric_limits<std::uint64_t>::max() - 1024u) / 24u) {
            status = LAPLACE_TABULAR_SOURCE_OVERFLOW;
            goto recursive_failure;
        }
        const std::uint64_t maximum_spans = artifact.byte_count * 24u + 1024u;
        laplace_decomposition_input decomposition_input{};
        decomposition_input.content = content;
        decomposition_input.providers = providers.data();
        decomposition_input.provider_count = provider_count;
        decomposition_input.maximum_spans = maximum_spans;
        decomposition_input.maximum_depth = 8u;

        laplace_decomposition_result* decomposition = nullptr;
        const auto decomposition_status =
            laplace_decomposition_run(&decomposition_input, &decomposition);
        if (decomposition_status != LAPLACE_DECOMPOSITION_OK ||
            decomposition == nullptr) {
            status = DecompositionStatus(decomposition_status);
            laplace_decomposition_result_destroy(&decomposition);
            goto recursive_failure;
        }

        laplace_decomposition_composition_input composition_input{};
        composition_input.content = &content;
        composition_input.decomposition = decomposition;
        composition_input.recipe_fingerprint =
            input->profile_declaration.recipe_program_fingerprint;
        composition_input.geometry_epoch = input->geometry_epoch;
        composition_input.occurrence_context_fingerprint =
            input->occurrence_context_fingerprint;
        composition_input.source_ordinal_base =
            static_cast<std::uint64_t>(created->requests.size());

        laplace_decomposition_composition_plan* composition_plan = nullptr;
        const auto composition_status =
            laplace_decomposition_composition_plan_create(
                &composition_input, &composition_plan);
        if (composition_status != LAPLACE_DECOMPOSITION_COMPOSITION_OK ||
            composition_plan == nullptr) {
            status = CompositionStatus(composition_status);
            laplace_decomposition_result_destroy(&decomposition);
            laplace_decomposition_composition_plan_destroy(&composition_plan);
            goto recursive_failure;
        }

        laplace_decomposition_composition_plan_view composition_view{};
        if (laplace_decomposition_composition_plan_view_get(
                composition_plan, &composition_view) !=
            LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
            status = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            laplace_decomposition_result_destroy(&decomposition);
            laplace_decomposition_composition_plan_destroy(&composition_plan);
            goto recursive_failure;
        }

        std::uint64_t merged_root = 0u;
        status = MergeDecompositionPlan(
            *created, composition_view, destination_atoms, merged_root);
        if (status != LAPLACE_TABULAR_SOURCE_OK) {
            laplace_decomposition_result_destroy(&decomposition);
            laplace_decomposition_composition_plan_destroy(&composition_plan);
            goto recursive_failure;
        }
        if (!RecursiveAdd(
                recursive_span_count,
                composition_view.span_count,
                recursive_span_count)) {
            status = LAPLACE_TABULAR_SOURCE_OVERFLOW;
            laplace_decomposition_result_destroy(&decomposition);
            laplace_decomposition_composition_plan_destroy(&composition_plan);
            goto recursive_failure;
        }
        trace_roots.push_back(merged_root);
        trace_fingerprints.push_back(composition_view.trace_fingerprint);
        laplace_decomposition_result_destroy(&decomposition);
        laplace_decomposition_composition_plan_destroy(&composition_plan);
    }

    status = AppendRecursiveRoot(
        *created, *input, created->view.root_result_index, trace_roots);
    if (status != LAPLACE_TABULAR_SOURCE_OK) goto recursive_failure;

    status = UpdateProfileCounts(
        *created, legacy_output_count, recursive_span_count);
    if (status != LAPLACE_TABULAR_SOURCE_OK) goto recursive_failure;

    {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        static constexpr std::string_view Domain{
            "laplace.tabular-source.recursive-decomposition/v1"};
        RecursiveHashBytes(hasher, Domain.data(), Domain.size());
        RecursiveHashBytes(
            hasher,
            created->view.source_fingerprint.bytes,
            sizeof(created->view.source_fingerprint.bytes));
        RecursiveHashU64(
            hasher, static_cast<std::uint64_t>(trace_fingerprints.size()));
        for (const laplace_digest256& fingerprint : trace_fingerprints) {
            RecursiveHashBytes(
                hasher, fingerprint.bytes, sizeof(fingerprint.bytes));
        }
        created->view.source_fingerprint = RecursiveFinish(hasher);
    }

    BindView(*created);
    laplace_uax29_tables_destroy(&uax_tables);
    *plan = created;
    return LAPLACE_TABULAR_SOURCE_OK;

recursive_failure:
    laplace_uax29_tables_destroy(&uax_tables);
    laplace_tabular_source_plan_destroy(&created);
    return status;
}

}  // namespace recursive_admission

extern "C" laplace_tabular_source_status laplace_tabular_source_plan_create(
    const laplace_tabular_source_input* input,
    laplace_tabular_source_plan** plan) {
    return laplace_tabular_source_plan_create_legacy(input, plan);
}

extern "C" laplace_tabular_source_status
laplace_tabular_source_plan_create_recursive(
    const laplace_tabular_source_input* input,
    const laplace_unicode_source_bundle* unicode_bundle,
    laplace_tabular_source_plan** plan) {
    try {
        return recursive_admission::BuildRecursive(input, unicode_bundle, plan);
    } catch (const std::bad_alloc&) {
        return LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
    }
}
