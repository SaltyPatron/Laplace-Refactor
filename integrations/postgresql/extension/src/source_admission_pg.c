/*
 * Product source admission crosses one source-agnostic recursive decomposition
 * boundary. The PostgreSQL host selects the currently activated grammar/codec
 * authorities and passes them as ordinary decomposition providers; the generic
 * engine does not select a Unicode/corpus/source-family path.
 *
 * The backend-local metrics below are execution evidence, not semantic state.
 * They retain the two most recent source-admission executions in one backend so
 * a replay contract can compare first publication with the immediately
 * following replay without inferring work from SQL text or row counts.
 */
#include "postgres.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/errcodes.h"
#include "utils/memutils.h"
#include "miscadmin.h"

#include <inttypes.h>
#include <string.h>

#include "blake3.h"
#include "laplace/decomposition_uax29.h"
#include "laplace/decomposition_xml.h"
#include "laplace/source_decomposition.h"
#include "laplace/source_profile.h"
#include "laplace/tree_sitter_grammar.h"
#include "laplace/uax29.h"
#include "composition_pg.h"
#include "source_structural_witness_pg.h"
#include "uax29_active_pg.h"
#include "laplace_pg_internal.h"

PG_FUNCTION_INFO_V1(laplace_source_admission_last_execution_metrics);

typedef struct laplace_pg_source_execution_metrics {
    uint64_t sequence;
    uint64_t source_stage_spi_execute_with_args_count;
    uint64_t composition_entity_candidate_count;
    uint64_t composition_physicality_candidate_count;
    uint64_t composition_entity_presence_round_count;
    uint64_t composition_physicality_presence_round_count;
    uint64_t composition_persistence_plan_count;
    uint64_t composition_receipt_persistence_call_count;
    uint8_t composition_persistence_executed;
    uint8_t valid;
    laplace_digest256 structural_execution_receipt;
} laplace_pg_source_execution_metrics;

static const laplace_tabular_source_plan* laplace_pg_active_source_plan = NULL;
static const laplace_pg_composition_execution*
    laplace_pg_active_source_execution = NULL;
static const laplace_composition_working_set_input*
    laplace_pg_active_source_composition_input = NULL;
static laplace_pg_active_uax_authority laplace_pg_source_uax_authority;
static uint8_t laplace_pg_source_uax_authority_valid = 0u;
static laplace_pg_source_execution_metrics laplace_pg_source_metrics_previous;
static laplace_pg_source_execution_metrics laplace_pg_source_metrics_active;
static uint64_t laplace_pg_source_metrics_sequence = 0u;

typedef struct laplace_pg_source_grammar_owner {
    MemoryContextCallback cleanup;
    laplace_tree_sitter_grammar* grammar;
    laplace_uax29_tables* uax_tables;
} laplace_pg_source_grammar_owner;

static HeapTupleHeader laplace_pg_source_selected_grammar = NULL;

static void laplace_pg_source_grammar_cleanup(void* pointer) {
    laplace_pg_source_grammar_owner* owner = pointer;
    laplace_tree_sitter_grammar_close(&owner->grammar);
    laplace_uax29_tables_destroy(&owner->uax_tables);
}

static void laplace_pg_source_select_grammar(
    FunctionCallInfo fcinfo, const laplace_source_profile_manifest* profile) {
    laplace_digest256 declaration;
    laplace_pg_source_selected_grammar = NULL;
    if (PG_NARGS() == 8) return;
    if (PG_NARGS() != 9 || !superuser()) {
        ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
            errmsg("Laplace grammar selection requires the authorized product administrator")));
    }
    laplace_pg_source_selected_grammar = DatumGetHeapTupleHeader(PG_GETARG_DATUM(8));
    laplace_pg_read_digest(laplace_pg_required_composite_attribute(
        laplace_pg_source_selected_grammar, 5, "declaration_fingerprint"),
        &declaration, "grammar declaration fingerprint");
    if (memcmp(declaration.bytes, profile->syntax_authority_fingerprint.bytes, 32u) != 0) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace source profile does not bind the selected grammar receipt")));
    }
}

static void laplace_pg_source_metrics_begin(void) {
    laplace_pg_source_metrics_previous = laplace_pg_source_metrics_active;
    memset(&laplace_pg_source_metrics_active, 0,
           sizeof(laplace_pg_source_metrics_active));
    ++laplace_pg_source_metrics_sequence;
    laplace_pg_source_metrics_active.sequence = laplace_pg_source_metrics_sequence;
}

static void laplace_pg_source_metrics_capture_composition(
    const laplace_pg_composition_execution* execution) {
    if (execution == NULL) {
        return;
    }
    laplace_pg_source_metrics_active.composition_entity_candidate_count =
        execution->presence.entity_candidate_count;
    laplace_pg_source_metrics_active.composition_physicality_candidate_count =
        execution->presence.physicality_candidate_count;
    laplace_pg_source_metrics_active.composition_entity_presence_round_count =
        execution->presence.entity_round_count;
    laplace_pg_source_metrics_active.composition_physicality_presence_round_count =
        execution->presence.physicality_round_count;
    laplace_pg_source_metrics_active.composition_persistence_plan_count =
        (uint64_t)execution->persistence.plan_count;
    laplace_pg_source_metrics_active.composition_persistence_executed =
        execution->persistence_executed;
    laplace_pg_source_metrics_active.valid = 1u;
}

static void laplace_pg_source_composition_persist_receipt(
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* input) {
    LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL(execution, input);
    ++laplace_pg_source_metrics_active.composition_receipt_persistence_call_count;
}

static void laplace_pg_append_metrics_json(
    StringInfo output,
    const char* name,
    const laplace_pg_source_execution_metrics* metrics) {
    appendStringInfo(
        output,
        "\"%s\":{"
        "\"valid\":%s,"
        "\"sequence\":%" PRIu64 ","
        "\"source_stage_spi_execute_with_args_count\":%" PRIu64 ","
        "\"composition_entity_candidate_count\":%" PRIu64 ","
        "\"composition_physicality_candidate_count\":%" PRIu64 ","
        "\"composition_entity_presence_round_count\":%" PRIu64 ","
        "\"composition_physicality_presence_round_count\":%" PRIu64 ","
        "\"composition_persistence_plan_count\":%" PRIu64 ","
        "\"composition_receipt_persistence_call_count\":%" PRIu64 ","
        "\"composition_persistence_executed\":%s,",
        name,
        metrics->valid != 0u ? "true" : "false",
        metrics->sequence,
        metrics->source_stage_spi_execute_with_args_count,
        metrics->composition_entity_candidate_count,
        metrics->composition_physicality_candidate_count,
        metrics->composition_entity_presence_round_count,
        metrics->composition_physicality_presence_round_count,
        metrics->composition_persistence_plan_count,
        metrics->composition_receipt_persistence_call_count,
        metrics->composition_persistence_executed != 0u ? "true" : "false");
    appendStringInfoString(output, "\"structural_execution_receipt_id\":\"");
    {
        size_t index;
        for (index=0u; index<sizeof(metrics->structural_execution_receipt.bytes); ++index)
            appendStringInfo(output, "%02x", (unsigned int)metrics->structural_execution_receipt.bytes[index]);
    }
    appendStringInfoString(output, "\"}");
}

Datum laplace_source_admission_last_execution_metrics(PG_FUNCTION_ARGS) {
    StringInfoData output;
    (void)fcinfo;
    initStringInfo(&output);
    appendStringInfoString(
        &output,
        "{\"schema\":\"laplace.source-admission-execution-metrics/v1\",");
    laplace_pg_append_metrics_json(
        &output, "previous", &laplace_pg_source_metrics_previous);
    appendStringInfoChar(&output, ',');
    laplace_pg_append_metrics_json(
        &output, "last", &laplace_pg_source_metrics_active);
    appendStringInfoChar(&output, '}');
    PG_RETURN_TEXT_P(cstring_to_text(output.data));
}

static void laplace_pg_source_hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void laplace_pg_source_hash_bytes(
    blake3_hasher* hasher, const void* bytes, size_t count) {
    laplace_pg_source_hash_u64(hasher, (uint64_t)count);
    if (count != 0u) {
        blake3_hasher_update(hasher, bytes, count);
    }
}

static laplace_digest256 laplace_pg_source_uax_provider_fingerprint(
    const laplace_pg_active_uax_authority* authority) {
    static const char domain[] = "laplace.decomposition.provider.uax29/v1";
    laplace_digest256 result;
    blake3_hasher hasher;
    memset(&result, 0, sizeof(result));
    blake3_hasher_init(&hasher);
    laplace_pg_source_hash_bytes(&hasher, domain, sizeof(domain) - 1u);
    laplace_pg_source_hash_bytes(
        &hasher, authority->source_fingerprint.bytes,
        sizeof(authority->source_fingerprint.bytes));
    laplace_pg_source_hash_bytes(
        &hasher, authority->recipe_fingerprint.bytes,
        sizeof(authority->recipe_fingerprint.bytes));
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

static laplace_digest256 laplace_pg_source_xml_provider_fingerprint(void) {
    static const char domain[] = "laplace-decomposition-xml-v1";
    laplace_digest256 result;
    blake3_hasher hasher;
    memset(&result, 0, sizeof(result));
    blake3_hasher_init(&hasher);
    /* Match the existing generic XML provider identity used by the native
     * XML/universal-AST route: length-prefixed domain plus empty payload. */
    laplace_pg_source_hash_bytes(&hasher, domain, sizeof(domain) - 1u);
    laplace_pg_source_hash_bytes(&hasher, NULL, 0u);
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

static int laplace_pg_source_digest_zero(const laplace_digest256* value) {
    size_t index;
    if (value == NULL) return 1;
    for (index = 0u; index < sizeof(value->bytes); ++index) {
        if (value->bytes[index] != 0u) return 0;
    }
    return 1;
}

static laplace_tabular_source_status laplace_pg_source_runtime_artifact_graph(
    laplace_tabular_source_input* input) {
    laplace_digest256 graph;
    laplace_tabular_source_status status;
    if (input == NULL) return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    if (!laplace_pg_source_digest_zero(
            &input->profile_declaration.artifact_graph_fingerprint)) {
        return LAPLACE_TABULAR_SOURCE_OK;
    }
    memset(&graph, 0, sizeof(graph));
    status = laplace_tabular_source_graph_identify(
        input->artifacts, (size_t)input->artifact_count,
        input->reference_rules, (size_t)input->reference_rule_count,
        input->mapping_rules, (size_t)input->mapping_rule_count,
        &graph);
    if (status != LAPLACE_TABULAR_SOURCE_OK) return status;
    input->profile_declaration.artifact_graph_fingerprint = graph;
    return LAPLACE_TABULAR_SOURCE_OK;
}

static laplace_tabular_source_status
laplace_pg_source_decomposition_plan_create(
    const laplace_tabular_source_input* input,
    laplace_tabular_source_plan** plan) {
    laplace_uax29_tables* uax_tables = NULL;
    laplace_decomposition_uax29_provider uax_provider;
    laplace_decomposition_xml_provider xml_provider;
    laplace_decomposition_provider_v1 providers[3];
    laplace_pg_source_grammar_owner* grammar_owner = NULL;
    /* Generic product admission must not invent a shallower syntax ceiling than
     * the engine it is invoking. 4096 is the engine's current explicit machine
     * window; a selected grammar receipt may impose a smaller declared bound. */
    uint32_t maximum_depth = 4096u;
    uint64_t provider_count = 2u;
    laplace_pg_active_uax_authority uax_authority;
    laplace_digest256 uax_fingerprint;
    laplace_digest256 xml_fingerprint;
    laplace_tabular_source_input runtime_input;
    laplace_tabular_source_status status;

    laplace_pg_active_source_plan = NULL;
    laplace_pg_active_source_execution = NULL;
    laplace_pg_active_source_composition_input = NULL;
    laplace_pg_source_uax_authority_valid = 0u;
    memset(&laplace_pg_source_uax_authority, 0,
           sizeof(laplace_pg_source_uax_authority));
    laplace_pg_source_metrics_begin();
    memset(&uax_provider, 0, sizeof(uax_provider));
    memset(&xml_provider, 0, sizeof(xml_provider));
    memset(providers, 0, sizeof(providers));
    memset(&uax_authority, 0, sizeof(uax_authority));
    if (input == NULL || plan == NULL) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    runtime_input = *input;
    status = laplace_pg_source_runtime_artifact_graph(&runtime_input);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        return status;
    }

    /* Product UAX authority is derived from the active canonical Unicode atom
     * stream. No Unicode source directory is consulted on this execution path.
     * The provider fingerprint remains bound to the canonical root's retained
     * source+recipe identity, so replacing the physical provider does not change
     * the logical UAX authority. */
    grammar_owner = palloc0(sizeof(*grammar_owner));
    grammar_owner->cleanup.func = laplace_pg_source_grammar_cleanup;
    grammar_owner->cleanup.arg = grammar_owner;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &grammar_owner->cleanup);
    laplace_pg_uax29_tables_from_active_unicode(&grammar_owner->uax_tables, &uax_authority);
    uax_tables = grammar_owner->uax_tables;
    if (uax_tables == NULL) {
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }

    uax_fingerprint =
        laplace_pg_source_uax_provider_fingerprint(&uax_authority);
    if (laplace_decomposition_uax29_provider_init(
            &uax_provider, uax_tables, &uax_fingerprint) !=
        LAPLACE_DECOMPOSITION_OK) {
        laplace_pg_source_grammar_cleanup(grammar_owner);
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }

    /* The generic XML provider is always present in the common provider set.
     * Its own applicability contract restricts it to application/xml, text/xml,
     * and +xml grammar-input spans, so non-XML source profiles are unchanged. */
    xml_fingerprint = laplace_pg_source_xml_provider_fingerprint();
    if (laplace_decomposition_xml_provider_init(
            &xml_provider, UINT64_C(0x584d4c0000000000), &xml_fingerprint) !=
        LAPLACE_DECOMPOSITION_OK) {
        laplace_pg_source_grammar_cleanup(grammar_owner);
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
    providers[0] = uax_provider.provider;
    providers[1] = xml_provider.provider;
    if (laplace_pg_source_selected_grammar != NULL) {
        HeapTupleHeader selected = laplace_pg_source_selected_grammar;
        const char* path = TextDatumGetCString(laplace_pg_required_composite_attribute(selected, 1, "library_path"));
        const char* symbol = TextDatumGetCString(laplace_pg_required_composite_attribute(selected, 2, "language_symbol"));
        const char* media = TextDatumGetCString(laplace_pg_required_composite_attribute(selected, 3, "media_type"));
        const uint64_t kind_base = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(selected, 4, "kind_base"), "grammar kind base");
        laplace_digest256 declaration;
        laplace_digest256 library_sha;
        const int64 library_bytes = DatumGetInt64(laplace_pg_required_composite_attribute(selected, 7, "library_bytes"));
        const int32 depth = DatumGetInt32(laplace_pg_required_composite_attribute(selected, 8, "maximum_depth"));
        laplace_tree_sitter_grammar_status grammar_status;
        uint64_t artifact;
        laplace_pg_read_digest(laplace_pg_required_composite_attribute(selected, 5, "declaration_fingerprint"),
            &declaration, "grammar declaration fingerprint");
        laplace_pg_read_digest(laplace_pg_required_composite_attribute(selected, 6, "library_sha256"),
            &library_sha, "grammar shared object SHA-256");
        if (library_bytes <= 0 || depth <= 0 || depth > 4096) {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("Laplace grammar resource bounds are invalid")));
        }
        grammar_status = laplace_tree_sitter_grammar_open_verified(path, symbol, media,
            (uint64_t)strlen(media), kind_base, &declaration, library_sha.bytes,
            (uint64_t)library_bytes, &grammar_owner->grammar);
        if (grammar_status != LAPLACE_TREE_SITTER_GRAMMAR_OK) {
            laplace_pg_source_grammar_cleanup(grammar_owner);
            ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION),
                errmsg("Laplace selected grammar provider is unavailable or failed exact verification"),
                errdetail("status=%u", (unsigned int)grammar_status)));
        }
        providers[2] = *laplace_tree_sitter_grammar_provider(grammar_owner->grammar);
        provider_count = 3u;
        maximum_depth = (uint32_t)depth;
        for (artifact = 0u; artifact < runtime_input.artifact_count; ++artifact) {
            const laplace_tabular_artifact* item = &runtime_input.artifacts[artifact];
            if (item->media_type == NULL ||
                !((item->media_type_byte_count == (uint64_t)strlen(media) &&
                   memcmp(item->media_type, media, (size_t)item->media_type_byte_count) == 0) ||
                  (item->media_type_byte_count == 10u && memcmp(item->media_type, "text/plain", 10u) == 0))) {
                ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("Laplace artifact has no selected grammar or exact-text observation provider")));
            }
        }
    }
    status = laplace_source_decomposition_plan_create_bounded(
        &runtime_input, providers, provider_count, maximum_depth, plan);
    laplace_pg_source_grammar_cleanup(grammar_owner);
    if (status == LAPLACE_TABULAR_SOURCE_OK && plan != NULL && *plan != NULL) {
        laplace_pg_active_source_plan = *plan;
        laplace_pg_source_uax_authority = uax_authority;
        laplace_pg_source_uax_authority_valid = 1u;
    }
    return status;
}

static void laplace_pg_source_require_uax_epoch(
    const laplace_composition_working_set_input* input) {
    if (laplace_pg_source_uax_authority_valid == 0u || input == NULL ||
        input->context == NULL ||
        (input->context->epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE)) == 0u ||
        memcmp(
            input->context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes,
            laplace_pg_source_uax_authority.activation_epoch_fingerprint.bytes,
            sizeof(laplace_pg_source_uax_authority.activation_epoch_fingerprint.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace source execution changed Unicode authority between decomposition and composition")));
    }
}

static void laplace_pg_source_composition_execute(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution) {
    laplace_pg_source_require_uax_epoch(input);
    LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(input, execution);
    laplace_pg_source_metrics_capture_composition(execution);
    laplace_pg_active_source_execution = execution;
    laplace_pg_active_source_composition_input = input;
}

static laplace_tabular_source_status
laplace_pg_source_profile_finalize_with_logical_denominator(
    const laplace_tabular_source_plan* plan,
    const laplace_composition_working_set_summary* summary,
    laplace_source_profile_manifest* profile) {
    laplace_source_profile_receipt receipt;
    laplace_source_profile_error error;
    const laplace_tabular_source_status status =
        laplace_tabular_source_profile_finalize(plan, summary, profile);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        return status;
    }
    if (summary->logical_occurrence_count == 0u) {
        return LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH;
    }

    /*
     * The source-profile occurrence denominator describes the canonical
     * logical composition represented by this admission. Explicit source
     * occurrence attestations are intentionally a separate execution fact:
     * recursive canonical subtrees are not allowed to manufacture source
     * sightings merely because they were lowered into the Merkle DAG.
     *
     * The generic tabular finalizer predates that separation and still closes
     * the profile on summary.occurrence_count (the explicitly emitted
     * attestation count). Product source admission uses the contract-owned
     * logical denominator consumed by world_admission_close_batch while leaving
     * summary.occurrence_count untouched in the composition execution receipt.
     */
    profile->occurrence_count = summary->logical_occurrence_count;
    memset(&profile->profile_id, 0, sizeof(profile->profile_id));
    memset(&receipt, 0, sizeof(receipt));
    memset(&error, 0, sizeof(error));
    if (laplace_source_profile_identify(profile, &profile->profile_id) !=
            LAPLACE_SOURCE_PROFILE_OK ||
        laplace_source_profile_validate_batch(profile, 1u, &receipt, &error) !=
            LAPLACE_SOURCE_PROFILE_OK) {
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }

    if (plan != laplace_pg_active_source_plan ||
        laplace_pg_active_source_execution == NULL ||
        laplace_pg_active_source_composition_input == NULL ||
        summary != &laplace_pg_active_source_execution->summary) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace structural witness deposition lost its source execution binding")));
    }
    return status;
}

#undef LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL
#define LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL laplace_pg_source_composition_execute
#undef LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL
#define LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL \
    laplace_pg_source_composition_persist_receipt
#define laplace_tabular_source_profile_finalize(plan, summary, profile) \
    laplace_pg_source_profile_finalize_with_logical_denominator((plan), (summary), (profile))
#define laplace_tabular_source_plan_create(input, plan) \
    laplace_pg_source_decomposition_plan_create((input), (plan))
#define SPI_execute_with_args(...) \
    (++laplace_pg_source_metrics_active.source_stage_spi_execute_with_args_count, \
     SPI_execute_with_args(__VA_ARGS__))
#include "source_admission_pg_legacy.inc"
#undef SPI_execute_with_args
#undef laplace_tabular_source_plan_create
#undef laplace_tabular_source_profile_finalize
#undef LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL
#undef LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL
