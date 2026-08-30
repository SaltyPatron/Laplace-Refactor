/*
 * Product source admission must not stop at the tabular envelope. Route the
 * existing admission call through the recursive engine path using the same
 * verified Unicode source estate that builds the active Unicode product.
 */
#include <string.h>

#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"
#include "composition_pg.h"
#include "source_structural_witness_pg.h"

#ifndef LAPLACE_UNICODE_SOURCE_ROOT
#error "LAPLACE_UNICODE_SOURCE_ROOT is required for recursive source admission"
#endif

static const laplace_tabular_source_plan* laplace_pg_active_source_plan = NULL;
static const laplace_pg_composition_execution*
    laplace_pg_active_source_execution = NULL;
static const laplace_composition_working_set_input*
    laplace_pg_active_source_composition_input = NULL;

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
    laplace_pg_composition_execute(input, execution);
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
#define laplace_tabular_source_profile_finalize(plan, summary, profile) \
    laplace_pg_source_profile_finalize_with_witnesses((plan), (summary), (profile))
#define laplace_tabular_source_plan_create(input, plan) \
    laplace_pg_tabular_source_plan_create_recursive((input), (plan))
#include "source_admission_pg_legacy.inc"
#undef laplace_tabular_source_plan_create
#undef laplace_tabular_source_profile_finalize
#undef LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL
