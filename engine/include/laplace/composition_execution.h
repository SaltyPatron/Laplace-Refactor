#ifndef LAPLACE_COMPOSITION_EXECUTION_H
#define LAPLACE_COMPOSITION_EXECUTION_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "laplace/execution.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COMPOSITION_FRONTIER_EXECUTION_PLAN_VERSION = 1,
    LAPLACE_COMPOSITION_FRONTIER_EXECUTION_POSTFLIGHT_VERSION = 1
};

/*
 * Aggregate physical scheduling plan for one already-admitted composition request
 * DAG.  This surface is intentionally a cheap preflight: it inspects request
 * dependencies and the conserved execution grant but does not calculate identities,
 * geometry, trajectories, occurrences, or any other semantic result.
 *
 * Memory required by the semantic working set remains owned by the composition
 * working-set validator.  These fields therefore receipt dependency-frontier and
 * CPU scheduling shape only; they must not be presented as a semantic-work memory
 * or throughput measurement.
 */
typedef struct laplace_composition_frontier_execution_plan {
    laplace_digest256 plan_fingerprint;
    uint64_t request_count;
    uint64_t dependency_edge_count;
    uint64_t frontier_count;
    uint64_t dependency_depth;
    uint64_t minimum_frontier_width;
    uint64_t maximum_frontier_width;
    uint64_t total_planned_chunks;
    uint64_t grant_memory_bytes;
    uint32_t minimum_outer_workers;
    uint32_t maximum_outer_workers;
    uint32_t grant_cpu_slots;
    uint32_t grant_io_slots;
    uint32_t version;
    uint32_t status;
} laplace_composition_frontier_execution_plan;

/*
 * Aggregate postflight for the physical execution of the dependency frontiers.
 * None of these measurements participate in canonical composition identity,
 * physicality, occurrence, stream, presence, or semantic receipt calculation.
 *
 * wall_time_ns and core_time_ns cover only calls through the common execution
 * runtime for ready frontiers. worker_capacity_ns is wall time multiplied by the
 * workers admitted for each frontier. idle_wait_capacity_ns is the unconsumed
 * portion of that finite worker capacity after clamping measured process CPU time
 * to the same capacity. parallel_efficiency_ppm is the corresponding ratio in
 * parts per million, avoiding floating-point ABI fields.
 *
 * total_frontier_width / frontier_count is the mean ready width. dependency_depth
 * is the zero-based critical-path depth from the same dependency plan used by
 * preflight. completed_items/chunks are actual postflight execution counts.
 */
typedef struct laplace_composition_frontier_execution_postflight {
    laplace_digest256 plan_fingerprint;
    uint64_t request_count;
    uint64_t frontier_count;
    uint64_t dependency_depth;
    uint64_t total_frontier_width;
    uint64_t minimum_frontier_width;
    uint64_t maximum_frontier_width;
    uint64_t completed_items;
    uint64_t completed_chunks;
    uint64_t wall_time_ns;
    uint64_t core_time_ns;
    uint64_t worker_capacity_ns;
    uint64_t idle_wait_capacity_ns;
    uint32_t minimum_outer_workers;
    uint32_t maximum_outer_workers;
    uint32_t grant_cpu_slots;
    uint32_t parallel_efficiency_ppm;
    uint32_t version;
    uint32_t status;
} laplace_composition_frontier_execution_postflight;

/*
 * Derive the dependency-frontier scheduling plan without executing a composition
 * request.  A source/file/record boundary is not an input to this calculation.
 * Physical CPU width can change with the grant; dependency frontiers cannot.
 */
LAPLACE_API laplace_composition_status
laplace_composition_frontier_execution_plan_build(
    const laplace_composition_working_set_input* input,
    laplace_composition_frontier_execution_plan* plan);

/*
 * Execute a composition working set through an explicitly selected common runtime
 * provider.  The provider controls only physical execution of ready dependency
 * frontiers; it is not an input to canonical identity, geometry, occurrence,
 * persistence ordering, or the semantic working-set receipt.
 *
 * The provider is borrowed only for the duration of this synchronous call.  Invalid
 * provider state fails closed through the common execution boundary rather than
 * silently falling back to another provider.
 */
LAPLACE_API laplace_composition_status
laplace_composition_working_set_create_with_provider(
    const laplace_composition_working_set_input* input,
    const laplace_execution_runtime_provider_v1* provider,
    laplace_composition_working_set** working_set);

/*
 * Return the actual common-execution receipts produced while calculating each
 * dependency frontier of a completed working set.  Receipt order is canonical
 * frontier order.  Every request belongs to exactly one receipt, and the sum of
 * completed_items therefore equals the working-set semantic calculation count on
 * successful execution.
 *
 * These are postflight execution receipts, not estimates.  The returned storage is
 * owned by the working set and remains valid until that working set is destroyed.
 */
LAPLACE_API const laplace_execution_work_receipt*
laplace_composition_working_set_frontier_execution_receipts(
    const laplace_composition_working_set* working_set,
    size_t* receipt_count);

/*
 * Return the aggregate measured physical postflight retained for one completed
 * working set.  The postflight is wrapper-owned physical metadata and is erased
 * with the working set.  It is never hashed into semantic product identity.
 */
LAPLACE_API laplace_composition_status
laplace_composition_working_set_frontier_execution_postflight_get(
    const laplace_composition_working_set* working_set,
    laplace_composition_frontier_execution_postflight* postflight);

#ifdef __cplusplus
}
#endif

#endif
