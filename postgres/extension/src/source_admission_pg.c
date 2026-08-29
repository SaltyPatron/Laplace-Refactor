/*
 * Product source admission must not stop at the tabular envelope. Route the
 * existing admission call through the recursive engine path using the same
 * verified Unicode source estate that builds the active Unicode product.
 */
#include <string.h>

#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"

#ifndef LAPLACE_UNICODE_SOURCE_ROOT
#error "LAPLACE_UNICODE_SOURCE_ROOT is required for recursive source admission"
#endif

static laplace_tabular_source_status
laplace_pg_tabular_source_plan_create_recursive(
    const laplace_tabular_source_input* input,
    laplace_tabular_source_plan** plan) {
    laplace_unicode_source_bundle* unicode_bundle = NULL;
    laplace_unicode_source_receipt unicode_receipt;
    laplace_tabular_source_status status;
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
    return status;
}

#define laplace_tabular_source_plan_create(input, plan) \
    laplace_pg_tabular_source_plan_create_recursive((input), (plan))
#include "source_admission_pg_legacy.inc"
#undef laplace_tabular_source_plan_create
