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
    LAPLACE_COMPOSITION_FRONTIER_EXECUTION_PLAN_VERSION = 1
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
 * Derive the dependency-frontier scheduling plan without executing a composition
 * request.  A source/file/record boundary is not an input to this calculation.
 * Physical CPU width can change with the grant; dependency frontiers cannot.
 */
LAPLACE_API laplace_composition_status
laplace_composition_frontier_execution_plan_build(
    const laplace_composition_working_set_input* input,
    laplace_composition_frontier_execution_plan* plan);

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
 * Identify the canonical semantic state produced by a completed composition
 * working set while deliberately excluding physical execution choices.  The
 * identity covers canonical results, entities, physicalities/trajectories,
 * explicit occurrence witnesses, source identity and calculation recipe.  It
 * excludes execution-context/resource grants, worker/chunk/frontier receipts,
 * presence probe identity, persistence batch boundaries and serialized stream
 * partitioning.
 *
 * This is the comparison surface for physical-plan invariance: legal changes in
 * workers, scheduling, presence/deposit batch sizes or another equivalent physical
 * provider may change execution receipts but must not change this fingerprint.
 */
LAPLACE_API laplace_composition_status
laplace_composition_working_set_semantic_fingerprint(
    const laplace_composition_working_set* working_set,
    laplace_digest256* semantic_fingerprint);

#ifdef __cplusplus
}
#endif

#endif
