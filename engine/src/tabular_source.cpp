/*
 * The legacy tabular recipe remains the exact envelope/reconstruction authority.
 * Rename only its create/destroy entrypoints inside this translation unit, then
 * layer the generic recursive decomposition product path over the resulting plan.
 */
#include "laplace/tabular_source.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif
#define laplace_tabular_source_plan_create laplace_tabular_source_plan_create_legacy
#define laplace_tabular_source_plan_destroy laplace_tabular_source_plan_destroy_legacy
#include "tabular_source_legacy.inc"
#undef laplace_tabular_source_plan_destroy
#undef laplace_tabular_source_plan_create
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "laplace/tabular_source_recursive.h"
#include "laplace/decomposition_composition.h"
#include "laplace/decomposition_delimited.h"
#include "laplace/decomposition_fixed_width.h"
#include "laplace/decomposition_uax29.h"
#include "tabular_source_recursive_merge.hpp"

namespace {

void MarkSourceOccurrenceRequests(laplace_tabular_source_plan& plan) {
#if defined(LAPLACE_TEST_TABULAR_OMIT_SOURCE_OCCURRENCES)
    (void)plan;
#else
    for (auto& request : plan.requests) {
        request.flags |= LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE;
    }
#endif
}

}  // namespace

namespace recursive_admission {

constexpr std::uint64_t DelimitedKindBase = UINT64_C(0x5441424c00000000);
constexpr std::uint64_t FixedWidthKindBase = UINT64_C(0x4657445400000000);
constexpr std::size_t WitnessCacheCapacity = 8u;

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

laplace_digest256 FixedWidthProviderFingerprint(
    const laplace_tabular_source_input& input,
    const laplace_tabular_artifact& artifact,
    const std::size_t artifact_index) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    static constexpr std::string_view Domain{
        "laplace.decomposition.provider.fixed-width/v1"};
    RecursiveHashBytes(hasher, Domain.data(), Domain.size());
    RecursiveHashBytes(
        hasher,
        input.profile_declaration.syntax_authority_fingerprint.bytes,
        sizeof(input.profile_declaration.syntax_authority_fingerprint.bytes));
    RecursiveHashU64(hasher, static_cast<std::uint64_t>(artifact_index));
    RecursiveHashU32(hasher, artifact.line_terminator);
    RecursiveHashU32(hasher, artifact.expected_column_count);
    RecursiveHashU32(hasher, artifact.header_record_count);
    RecursiveHashU32(hasher, artifact.padding_byte);
    RecursiveHashU32(hasher, artifact.overflow_field_index);
    RecursiveHashU32(hasher, artifact.maximum_overflow_bytes);
    RecursiveHashU32(hasher, artifact.expected_overflow_record_count);
    for (std::uint32_t index = 0u;
         index < artifact.expected_column_count; ++index) {
        RecursiveHashU32(hasher, artifact.fixed_width_fields[index].width);
        RecursiveHashU32(hasher, artifact.fixed_width_fields[index].flags);
    }
    return RecursiveFinish(hasher);
}

struct WitnessCacheEntry {
    bool present = false;
    laplace_digest256 key{};
    laplace_digest256 trace_fingerprint{};
    std::uint64_t span_count = 0u;
};

thread_local std::array<WitnessCacheEntry, WitnessCacheCapacity> WitnessCache{};
thread_local std::size_t WitnessCacheNext = 0u;

laplace_digest256 WitnessCacheKey(
    const laplace_decomposition_content& content,
    const laplace_digest256& uax_fingerprint,
    const laplace_digest256* grammar_fingerprint) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    static constexpr std::string_view Domain{
        "laplace.tabular-source.decomposition-witness-cache/v1"};
    RecursiveHashBytes(hasher, Domain.data(), Domain.size());
    RecursiveHashBytes(
        hasher, uax_fingerprint.bytes, sizeof(uax_fingerprint.bytes));
    if (grammar_fingerprint == nullptr) {
        RecursiveHashU32(hasher, 0u);
    } else {
        RecursiveHashU32(hasher, 1u);
        RecursiveHashBytes(
            hasher, grammar_fingerprint->bytes,
            sizeof(grammar_fingerprint->bytes));
    }
    RecursiveHashBytes(
        hasher, content.media_type,
        static_cast<std::size_t>(content.media_type_byte_count));
    RecursiveHashBytes(
        hasher, content.bytes, static_cast<std::size_t>(content.byte_count));
    return RecursiveFinish(hasher);
}

bool WitnessCacheLookup(
    const laplace_digest256& key,
    laplace_digest256& trace_fingerprint,
    std::uint64_t& span_count) {
    for (const WitnessCacheEntry& entry : WitnessCache) {
        if (entry.present &&
            memcmp(entry.key.bytes, key.bytes, sizeof(key.bytes)) == 0) {
            trace_fingerprint = entry.trace_fingerprint;
            span_count = entry.span_count;
            return true;
        }
    }
    return false;
}

void WitnessCacheStore(
    const laplace_digest256& key,
    const laplace_digest256& trace_fingerprint,
    const std::uint64_t span_count) {
    WitnessCacheEntry& entry = WitnessCache[WitnessCacheNext];
    entry.present = true;
    entry.key = key;
    entry.trace_fingerprint = trace_fingerprint;
    entry.span_count = span_count;
    WitnessCacheNext = (WitnessCacheNext + 1u) % WitnessCacheCapacity;
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

laplace_tabular_source_status DecompositionCompositionStatus(
    const laplace_decomposition_composition_status status) {
    switch (status) {
        case LAPLACE_DECOMPOSITION_COMPOSITION_OK:
            return LAPLACE_TABULAR_SOURCE_OK;
        case LAPLACE_DECOMPOSITION_COMPOSITION_MEMORY_FAILURE:
            return LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
        case LAPLACE_DECOMPOSITION_COMPOSITION_OVERFLOW:
            return LAPLACE_TABULAR_SOURCE_OVERFLOW;
        case LAPLACE_DECOMPOSITION_COMPOSITION_DECOMPOSITION_INVALID:
        case LAPLACE_DECOMPOSITION_COMPOSITION_UTF8_INVALID:
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        default:
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
}

laplace_tabular_source_status DecompositionTrace(
    const laplace_decomposition_content& content,
    const laplace_decomposition_result* decomposition,
    laplace_digest256& fingerprint,
    std::uint64_t& span_count) {
    std::size_t count = 0u;
    const laplace_decomposition_span* spans =
        laplace_decomposition_spans(decomposition, &count);
    laplace_decomposition_summary summary{};
    if (spans == nullptr || count == 0u ||
        laplace_decomposition_summary_get(decomposition, &summary) !=
            LAPLACE_DECOMPOSITION_OK ||
        summary.span_count != static_cast<std::uint64_t>(count)) {
        return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
    }

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    static constexpr std::string_view Domain{
        "laplace.decomposition.witness-trace/v1"};
    RecursiveHashBytes(hasher, Domain.data(), Domain.size());
    RecursiveHashBytes(
        hasher, content.bytes, static_cast<std::size_t>(content.byte_count));
    RecursiveHashU64(hasher, static_cast<std::uint64_t>(count));

    for (std::size_t index = 0u; index < count; ++index) {
        const laplace_decomposition_span& span = spans[index];
        if (span.byte_start >= span.byte_end ||
            span.byte_end > content.byte_count ||
            (index == 0u &&
             span.parent_span_index !=
                 std::numeric_limits<std::uint64_t>::max()) ||
            (index != 0u &&
             span.parent_span_index >= static_cast<std::uint64_t>(index))) {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
        std::size_t media_type_bytes = 0u;
        const char* media_type = laplace_decomposition_span_media_type(
            decomposition, index, &media_type_bytes);
        if ((media_type == nullptr) != (media_type_bytes == 0u)) {
            return LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
        }
        RecursiveHashU64(hasher, static_cast<std::uint64_t>(index));
        RecursiveHashU64(hasher, span.byte_start);
        RecursiveHashU64(hasher, span.byte_end);
        RecursiveHashU64(hasher, span.parent_span_index);
        RecursiveHashBytes(
            hasher, span.provider_fingerprint.bytes,
            sizeof(span.provider_fingerprint.bytes));
        RecursiveHashU64(hasher, span.kind);
        RecursiveHashU32(hasher, span.depth);
        RecursiveHashU32(hasher, span.flags);
        RecursiveHashBytes(hasher, media_type, media_type_bytes);
    }
    fingerprint = RecursiveFinish(hasher);
    span_count = static_cast<std::uint64_t>(count);
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
    MarkSourceOccurrenceRequests(*created);

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
    std::vector<laplace_digest256> trace_fingerprints;
    std::vector<laplace_tabular_decomposition_witness> decomposition_witnesses;
    std::vector<std::uint8_t> decomposition_witness_media_types;
    trace_fingerprints.reserve(static_cast<std::size_t>(input->artifact_count));

    for (std::size_t artifact_index = 0u;
         artifact_index < static_cast<std::size_t>(input->artifact_count);
         ++artifact_index) {
        const laplace_tabular_artifact& artifact =
            input->artifacts[artifact_index];
        laplace_decomposition_delimited_provider delimited_provider{};
        laplace_decomposition_fixed_width_provider fixed_width_provider{};
        std::vector<laplace_decomposition_fixed_width_field> fixed_width_fields;
        laplace_digest256 grammar_fingerprint{};
        bool has_grammar_fingerprint = false;
        std::array<laplace_decomposition_provider_v1, 2> providers{};
        providers[0] = uax_provider.provider;
        std::uint64_t provider_count = 1u;

        if (artifact.mode == LAPLACE_TABULAR_ARTIFACT_DELIMITED) {
            grammar_fingerprint =
                DelimitedProviderFingerprint(*input, artifact, artifact_index);
            has_grammar_fingerprint = true;
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
        } else if (artifact.mode == LAPLACE_TABULAR_ARTIFACT_FIXED_WIDTH) {
            grammar_fingerprint =
                FixedWidthProviderFingerprint(*input, artifact, artifact_index);
            has_grammar_fingerprint = true;
            fixed_width_fields.reserve(artifact.expected_column_count);
            for (std::uint32_t index = 0u;
                 index < artifact.expected_column_count; ++index) {
                fixed_width_fields.push_back(
                    laplace_decomposition_fixed_width_field{
                        artifact.fixed_width_fields[index].width,
                        artifact.fixed_width_fields[index].flags});
            }
            const std::uint32_t terminator =
                artifact.line_terminator == LAPLACE_TABULAR_TERMINATOR_CRLF
                    ? LAPLACE_DECOMPOSITION_FIXED_WIDTH_CRLF
                    : LAPLACE_DECOMPOSITION_FIXED_WIDTH_LF;
            const auto provider_status =
                laplace_decomposition_fixed_width_provider_init(
                    &fixed_width_provider,
                    fixed_width_fields.data(),
                    artifact.expected_column_count,
                    artifact.header_record_count,
                    terminator,
                    artifact.padding_byte,
                    artifact.overflow_field_index,
                    artifact.maximum_overflow_bytes,
                    FixedWidthKindBase,
                    &grammar_fingerprint);
            if (provider_status != LAPLACE_DECOMPOSITION_OK) {
                status = DecompositionStatus(provider_status);
                goto recursive_failure;
            }
            providers[1] = fixed_width_provider.provider;
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
        } else if (artifact.mode != LAPLACE_TABULAR_ARTIFACT_RAW) {
            content.media_type = FallbackDelimitedMediaType;
            content.media_type_byte_count =
                sizeof(FallbackDelimitedMediaType) - 1u;
        }

        const laplace_digest256 cache_key = WitnessCacheKey(
            content,
            uax_fingerprint,
            has_grammar_fingerprint ? &grammar_fingerprint : nullptr);
        laplace_digest256 trace_fingerprint{};
        std::uint64_t artifact_span_count = 0u;
        const bool witness_cached = WitnessCacheLookup(
            cache_key, trace_fingerprint, artifact_span_count);

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

        laplace_decomposition_summary decomposition_summary{};
        if (laplace_decomposition_summary_get(
                decomposition, &decomposition_summary) !=
                LAPLACE_DECOMPOSITION_OK ||
            decomposition_summary.span_count == 0u) {
            status = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            laplace_decomposition_result_destroy(&decomposition);
            goto recursive_failure;
        }

        if (!witness_cached) {
            status = DecompositionTrace(
                content, decomposition, trace_fingerprint, artifact_span_count);
            if (status != LAPLACE_TABULAR_SOURCE_OK) {
                laplace_decomposition_result_destroy(&decomposition);
                goto recursive_failure;
            }
            WitnessCacheStore(
                cache_key, trace_fingerprint, artifact_span_count);
        } else if (artifact_span_count != decomposition_summary.span_count) {
            status = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            laplace_decomposition_result_destroy(&decomposition);
            goto recursive_failure;
        }

        if (decomposition_summary.span_count > 1u) {
            const std::uint64_t insertion_index =
                created->view.root_result_index;
            laplace_decomposition_composition_input composition_input{};
            composition_input.content = &content;
            composition_input.decomposition = decomposition;
            composition_input.recipe_fingerprint =
                input->profile_declaration.recipe_program_fingerprint;
            composition_input.geometry_epoch = input->geometry_epoch;
            composition_input.occurrence_context_fingerprint =
                input->occurrence_context_fingerprint;
            composition_input.source_ordinal_base = insertion_index;

            laplace_decomposition_composition_plan* composition_plan = nullptr;
            const auto composition_status =
                laplace_decomposition_composition_plan_create(
                    &composition_input, &composition_plan);
            if (composition_status != LAPLACE_DECOMPOSITION_COMPOSITION_OK ||
                composition_plan == nullptr) {
                status = DecompositionCompositionStatus(composition_status);
                laplace_decomposition_composition_plan_destroy(&composition_plan);
                laplace_decomposition_result_destroy(&decomposition);
                goto recursive_failure;
            }

            laplace_decomposition_composition_plan_view composition_view{};
            const auto view_status =
                laplace_decomposition_composition_plan_view_get(
                    composition_plan, &composition_view);
            if (view_status != LAPLACE_DECOMPOSITION_COMPOSITION_OK) {
                status = DecompositionCompositionStatus(view_status);
                laplace_decomposition_composition_plan_destroy(&composition_plan);
                laplace_decomposition_result_destroy(&decomposition);
                goto recursive_failure;
            }

            std::size_t span_count = 0u;
            const laplace_decomposition_span* spans =
                laplace_decomposition_spans(decomposition, &span_count);
            if (spans == nullptr ||
                span_count != static_cast<std::size_t>(composition_view.span_count)) {
                status = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
                laplace_decomposition_composition_plan_destroy(&composition_plan);
                laplace_decomposition_result_destroy(&decomposition);
                goto recursive_failure;
            }
            std::vector<laplace::internal::RecursiveDecompositionWitnessInput>
                witness_inputs;
            witness_inputs.reserve(span_count);
            for (std::size_t span_index = 0u;
                 span_index < span_count;
                 ++span_index) {
                std::size_t media_type_bytes = 0u;
                const char* media_type = laplace_decomposition_span_media_type(
                    decomposition, span_index, &media_type_bytes);
                if ((media_type == nullptr) != (media_type_bytes == 0u)) {
                    status = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
                    laplace_decomposition_composition_plan_destroy(&composition_plan);
                    laplace_decomposition_result_destroy(&decomposition);
                    goto recursive_failure;
                }
                const laplace_decomposition_span& span = spans[span_index];
                laplace::internal::RecursiveDecompositionWitnessInput witness{};
                witness.provider_fingerprint = span.provider_fingerprint;
                witness.media_type = media_type;
                witness.byte_start = span.byte_start;
                witness.byte_end = span.byte_end;
                witness.parent_span_index = span.parent_span_index;
                witness.kind = span.kind;
                witness.media_type_byte_count =
                    static_cast<std::uint64_t>(media_type_bytes);
                witness.depth = span.depth;
                witness.flags = span.flags;
                witness_inputs.push_back(witness);
            }

            status = laplace::internal::MergeRecursiveCanonicalComposition(
                created->atom_positions,
                created->operands,
                created->requests,
                insertion_index,
                composition_view);
            if (status == LAPLACE_TABULAR_SOURCE_OK) {
                status = laplace::internal::AppendRecursiveDecompositionWitnesses(
                    created->atom_positions,
                    insertion_index,
                    composition_view,
                    static_cast<std::uint64_t>(artifact_index),
                    trace_fingerprint,
                    witness_inputs.data(),
                    static_cast<std::uint64_t>(witness_inputs.size()),
                    decomposition_witnesses,
                    decomposition_witness_media_types);
            }
            if (status == LAPLACE_TABULAR_SOURCE_OK) {
                created->view.root_result_index =
                    insertion_index + composition_view.request_count;
            }
            laplace_decomposition_composition_plan_destroy(&composition_plan);
            if (status != LAPLACE_TABULAR_SOURCE_OK) {
                laplace_decomposition_result_destroy(&decomposition);
                goto recursive_failure;
            }
            if (!RecursiveAdd(
                    recursive_span_count,
                    artifact_span_count,
                    recursive_span_count)) {
                status = LAPLACE_TABULAR_SOURCE_OVERFLOW;
                laplace_decomposition_result_destroy(&decomposition);
                goto recursive_failure;
            }
        }
        laplace_decomposition_result_destroy(&decomposition);
        trace_fingerprints.push_back(trace_fingerprint);
    }

    status = UpdateProfileCounts(
        *created, legacy_output_count, recursive_span_count);
    if (status != LAPLACE_TABULAR_SOURCE_OK) goto recursive_failure;

    {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        static constexpr std::string_view Domain{
            "laplace.tabular-source.recursive-decomposition/v2"};
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

    if (!decomposition_witnesses.empty()) {
        auto* witness_storage = new (std::nothrow)
            laplace_tabular_decomposition_witness[decomposition_witnesses.size()];
        std::uint8_t* media_storage = nullptr;
        if (!decomposition_witness_media_types.empty()) {
            media_storage = new (std::nothrow)
                std::uint8_t[decomposition_witness_media_types.size()];
        }
        if (witness_storage == nullptr ||
            (!decomposition_witness_media_types.empty() && media_storage == nullptr)) {
            delete[] witness_storage;
            delete[] media_storage;
            status = LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
            goto recursive_failure;
        }
        std::copy(
            decomposition_witnesses.begin(),
            decomposition_witnesses.end(), witness_storage);
        if (media_storage != nullptr) {
            std::copy(
                decomposition_witness_media_types.begin(),
                decomposition_witness_media_types.end(), media_storage);
        }
        created->view.decomposition_witnesses = witness_storage;
        created->view.decomposition_witness_media_types = media_storage;
        created->view.decomposition_witness_count =
            static_cast<std::uint64_t>(decomposition_witnesses.size());
        created->view.decomposition_witness_media_type_byte_count =
            static_cast<std::uint64_t>(decomposition_witness_media_types.size());
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
    const laplace_tabular_source_status status =
        laplace_tabular_source_plan_create_legacy(input, plan);
    if (status == LAPLACE_TABULAR_SOURCE_OK && plan != nullptr && *plan != nullptr) {
        MarkSourceOccurrenceRequests(**plan);
    }
    return status;
}

extern "C" void laplace_tabular_source_plan_destroy(
    laplace_tabular_source_plan** plan) {
    if (plan != nullptr && *plan != nullptr) {
        delete[] const_cast<laplace_tabular_decomposition_witness*>(
            (*plan)->view.decomposition_witnesses);
        delete[] const_cast<std::uint8_t*>(
            (*plan)->view.decomposition_witness_media_types);
        (*plan)->view.decomposition_witnesses = nullptr;
        (*plan)->view.decomposition_witness_media_types = nullptr;
        (*plan)->view.decomposition_witness_count = 0u;
        (*plan)->view.decomposition_witness_media_type_byte_count = 0u;
    }
    laplace_tabular_source_plan_destroy_legacy(plan);
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
