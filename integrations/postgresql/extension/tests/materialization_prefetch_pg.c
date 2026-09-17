/* Run the actual private prefetch and provider callback inside PostgreSQL.
 * Only this BUILD_TESTING translation unit interposes the first speculative
 * allocation. No production injection hook or alternate error wrapper is used. */
#include "postgres.h"

#include <signal.h>

#include "fmgr.h"
#include "miscadmin.h"
#include "utils/memutils.h"

static void* prefetch_probe_palloc(Size bytes);
static int prefetch_probe_geterrcode(void);

/* Give the included owner's public exports distinct names in this test unit. */
#define laplace_pg_materialization_provider_create laplace_pg_prefetch_probe_provider_create
#define laplace_pg_materialization_provider_create_for_view laplace_pg_prefetch_probe_provider_create_for_view
#define laplace_pg_materialization_provider_create_for_view_selected laplace_pg_prefetch_probe_provider_create_for_view_selected
#define laplace_pg_materialization_provider_create_selected laplace_pg_prefetch_probe_provider_create_selected
#define laplace_pg_materialization_provider_summary laplace_pg_prefetch_probe_provider_summary
#define laplace_pg_materialization_provider_take_error laplace_pg_prefetch_probe_provider_take_error
#define laplace_pg_materialization_provider_destroy laplace_pg_prefetch_probe_provider_destroy
#define palloc prefetch_probe_palloc
#define geterrcode prefetch_probe_geterrcode
#include "../src/materialization_pg.c"
#undef geterrcode
#undef palloc
#undef laplace_pg_materialization_provider_create
#undef laplace_pg_materialization_provider_create_for_view
#undef laplace_pg_materialization_provider_create_for_view_selected
#undef laplace_pg_materialization_provider_create_selected
#undef laplace_pg_materialization_provider_summary
#undef laplace_pg_materialization_provider_take_error
#undef laplace_pg_materialization_provider_destroy

static unsigned prefetch_probe_failure;
static unsigned prefetch_probe_interceptions;
static bool prefetch_probe_misclassify_cancel;
static bool prefetch_probe_saw_cancel;
static MemoryContext prefetch_probe_error_context;

/* A deliberate classification defect must fail the same SQL cancellation
 * oracle. The real signal handler and real ErrorData remain unchanged. */
static int prefetch_probe_geterrcode(void) {
    const int code = geterrcode();
    if (code == ERRCODE_QUERY_CANCELED) prefetch_probe_saw_cancel = true;
    return prefetch_probe_misclassify_cancel && code == ERRCODE_QUERY_CANCELED
        ? ERRCODE_INVALID_PARAMETER_VALUE : code;
}

static void* prefetch_probe_palloc(Size bytes) {
    const unsigned failure = prefetch_probe_failure;
    if (failure == 0u) return palloc(bytes);
    prefetch_probe_failure = 0u;
    ++prefetch_probe_interceptions;
    /* Force the production wrapper to restore its saved context on error. */
    MemoryContextSwitchTo(prefetch_probe_error_context);
    if (failure == 1u)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("controlled optional materialization prefetch allocation failure")));

    /* PostgreSQL's real SIGINT handler sets QueryCancelPending. Do not synthesize
     * a 57014 ereport: CHECK_FOR_INTERRUPTS must deliver the server's error. */
    if (kill(MyProcPid, SIGINT) != 0)
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
            errmsg("could not signal materialization prefetch probe backend")));
    for (unsigned attempt = 0u; attempt < 100u; ++attempt) {
        CHECK_FOR_INTERRUPTS();
        pg_usleep(10000L);
    }
    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
        errmsg("materialization prefetch probe SIGINT was not delivered")));
    return NULL;
}

PG_FUNCTION_INFO_V1(laplace_pg_test_materialization_prefetch);
PGDLLEXPORT Datum laplace_pg_test_materialization_prefetch(PG_FUNCTION_ARGS);
Datum laplace_pg_test_materialization_prefetch(PG_FUNCTION_ARGS) {
    const int32 mode = PG_GETARG_INT32(0);
    const laplace_digest256 zero_digest = {{0}};
    const laplace_id128 root_id = {{0x11}};
    const laplace_id128 child_id = {{0x33}};
    const laplace_digest256 root_physicality = {{0x22}};
    const MemoryContext caller = CurrentMemoryContext;
    MemoryContext fixture;
    laplace_pg_materialization_provider_state* state;
    materialization_cache_entry* parent;
    materialization_cache_entry* child;
    laplace_trajectory_carrier output;
    unsigned char untouched_output[sizeof(output)];
    laplace_digest256 receipt;
    ErrorData* error;
    bool found = false;
    bool consistent;
    bool cancelled;
    bool fallback;
    int status;

    if (mode < 0 || mode > 2)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("materialization prefetch probe mode must be 0, 1, or 2")));
    fixture = AllocSetContextCreate(caller, "Laplace prefetch cancellation fixture",
        ALLOCSET_SMALL_SIZES);
    state = MemoryContextAllocZero(fixture, sizeof(*state));
    state->caller_context = caller;
    state->scratch_context = AllocSetContextCreate(fixture,
        "Laplace prefetch fixture scratch", ALLOCSET_SMALL_SIZES);
    state->cache_context = AllocSetContextCreate(fixture,
        "Laplace prefetch fixture cache", ALLOCSET_SMALL_SIZES);
    state->binding_context = AllocSetContextCreate(fixture,
        "Laplace prefetch fixture bindings", ALLOCSET_SMALL_SIZES);
    state->scope_context = AllocSetContextCreate(fixture,
        "Laplace prefetch fixture scope", ALLOCSET_SMALL_SIZES);
    state->context.resource_grant.memory_bytes = UINT64_C(1048576);
    blake3_hasher_init(&state->readset);
    materialization_clear_cache(state);

    parent = materialization_cache_enter_selected(state, &root_id,
        &root_physicality, &found);
    parent->ready = 1u;
    parent->node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    parent->node.entity_id = root_id;
    parent->node.physicality_id = root_physicality;
    parent->node.carrier_count = 1u;
    parent->carrier_count = 1u;
    parent->trajectory = MemoryContextAllocZero(state->cache_context,
        sizeof(*parent->trajectory));
    child = materialization_cache_enter(state, &child_id, &found);
    child->resolving = 1u;
    child->node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    child->carrier_count = 1u;
    child->trajectory = MemoryContextAllocZero(state->cache_context,
        sizeof(*child->trajectory));
    memset(untouched_output, 0xa5, sizeof(untouched_output));
    memcpy(&output, untouched_output, sizeof(output));
    memset(&receipt, 0xa5, sizeof(receipt));

    prefetch_probe_interceptions = 0u;
    prefetch_probe_saw_cancel = false;
    prefetch_probe_failure = mode == 0 ? 1u : 2u;
    prefetch_probe_misclassify_cancel = mode == 2;
    prefetch_probe_error_context = state->binding_context;
    status = materialization_read_trajectory(
        state, &parent->node, &output, 1u, &receipt);
    consistent = prefetch_probe_interceptions == 1u &&
        prefetch_probe_failure == 0u && CurrentMemoryContext == caller &&
        parent->children_prefetched == 1u && child->resolving == 0u &&
        child->trajectory == NULL && child->carrier_count == 0u &&
        child->node.kind == 0u && child->node.carrier_count == 0u &&
        prefetch_probe_saw_cancel == (mode != 0);
    prefetch_probe_failure = 0u;
    prefetch_probe_misclassify_cancel = false;
    prefetch_probe_saw_cancel = false;
    prefetch_probe_error_context = NULL;

    error = laplace_pg_prefetch_probe_provider_take_error(state);
    cancelled = status != 0 && error != NULL &&
        error->sqlerrcode == ERRCODE_QUERY_CANCELED &&
        memcmp(&receipt, &zero_digest, sizeof(receipt)) == 0 &&
        state->trajectory_reads == 0u && state->trajectory_bytes == 0u &&
        memcmp(&output, untouched_output, sizeof(output)) == 0;
    fallback = status == 0 && error == NULL &&
        memcmp(&receipt, &zero_digest, sizeof(receipt)) != 0 &&
        state->trajectory_reads == 1u &&
        state->trajectory_bytes == sizeof(output) &&
        memcmp(&output, parent->trajectory, sizeof(output)) == 0;
    MemoryContextDelete(fixture);
    if (!consistent || (!cancelled && !fallback)) {
        if (error != NULL) FreeErrorData(error);
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
            errmsg("materialization prefetch probe violated callback cleanup or receipt semantics")));
    }
    if (error != NULL) ReThrowError(error);
    PG_RETURN_BOOL(true);
}
