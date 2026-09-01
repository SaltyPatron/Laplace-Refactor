/*
 * Product source admission must not stop at the tabular envelope. Route the
 * existing admission call through the recursive engine path using the same
 * verified Unicode source estate that builds the active Unicode product.
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

#include <inttypes.h>
#include <string.h>

#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"
#include "composition_pg.h"
#include "source_structural_witness_pg.h"

#ifndef LAPLACE_UNICODE_SOURCE_ROOT
#error "LAPLACE_UNICODE_SOURCE_ROOT is required for recursive source admission"
#endif

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
} laplace_pg_source_execution_metrics;

static const laplace_tabular_source_plan* laplace_pg_active_source_plan = NULL;
static const laplace_pg_composition_execution*
    laplace_pg_active_source_execution = NULL;
static const laplace_composition_working_set_input*
    laplace_pg_active_source_composition_input = NULL;
static laplace_pg_source_execution_metrics laplace_pg_source_metrics_previous;
static laplace_pg_source_execution_metrics laplace_pg_source_metrics_active;
static uint64_t laplace_pg_source_metrics_sequence = 0u;

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
        "\"composition_persistence_executed\":%s}",
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
}

Datum laplace_source_admission_last_execution_metrics(PG_FUNCTION_ARGS) {
    StringInfoData output;
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

static laplace_tabular_source_status
laplace_pg_tabular_source_plan_create_recursive(
    const laplace_tabular_source_input* input,
    laplace_tabular_source_plan** plan) {
    laplace_unicode_source_bundle* unicode_bundle = NULL;
    laplace_unicode_source_receipt unicode_receipt;
    laplace_tabular_source_status status;
    laplace_pg_active_source_plan = NULL;
    laplace_pg_active_source_execution = NULL;
    laplace_pg_active_source_composition_input = NULL;
    laplace_pg_source_metrics_begin();
    memset(&unicode_receipt, 0, sizeof(unicode_receipt));
    if (laplace_unicode_source_bundle_open(
            LAPLACE_UNICODE_SOURCE_ROOT,
            &unicode_bundle,
            &unicode_receipt) != LAPLACE_UNICODE_OK ||
        unicode_bundle == NULL) {
        laplace_unicode_source_bundle_close(&unicode_bundle);
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
    status = laplace_tabular_source_plan_create_recursive(
        input, unicode_bundle, plan);
    laplace_unicode_source_bundle_close(&unicode_bundle);
    if (status == LAPLACE_TABULAR_SOURCE_OK && plan != NULL && *plan != NULL) {
        laplace_pg_active_source_plan = *plan;
    }
    return status;
}

static void laplace_pg_source_composition_execute(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution) {
    LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(input, execution);
    laplace_pg_source_metrics_capture_composition(execution);
    laplace_pg_active_source_execution = execution;
    laplace_pg_active_source_composition_input = input;
}

static laplace_tabular_source_status
laplace_pg_source_profile_finalize_with_witnesses(
    const laplace_tabular_source_plan* plan,
    const laplace_composition_working_set_summary* summary,
    laplace_source_profile_manifest* profile) {
    const laplace_tabular_source_status status =
        laplace_tabular_source_profile_finalize(plan, summary, profile);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        return status;
    }
    if (plan != laplace_pg_active_source_plan ||
        laplace_pg_active_source_execution == NULL ||
        laplace_pg_active_source_composition_input == NULL ||
        summary != &laplace_pg_active_source_execution->summary) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace structural witness deposition lost its source execution binding")));
    }
    laplace_pg_persist_source_structural_witnesses(
        plan,
        laplace_pg_active_source_execution,
        laplace_pg_active_source_composition_input,
        profile);
    return status;
}

#undef LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL
#define LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL laplace_pg_source_composition_execute
#undef LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL
#define LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL \
    laplace_pg_source_composition_persist_receipt
#define laplace_tabular_source_profile_finalize(plan, summary, profile) \
    laplace_pg_source_profile_finalize_with_witnesses((plan), (summary), (profile))
#define laplace_tabular_source_plan_create(input, plan) \
    laplace_pg_tabular_source_plan_create_recursive((input), (plan))
#define SPI_execute_with_args(...) \
    (++laplace_pg_source_metrics_active.source_stage_spi_execute_with_args_count, \
     SPI_execute_with_args(__VA_ARGS__))
#include "source_admission_pg_legacy.inc"
#undef SPI_execute_with_args
#undef laplace_tabular_source_plan_create
#undef laplace_tabular_source_profile_finalize
#undef LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL
#undef LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL
