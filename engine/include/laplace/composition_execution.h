#ifndef LAPLACE_COMPOSITION_EXECUTION_H
#define LAPLACE_COMPOSITION_EXECUTION_H

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

#ifdef __cplusplus
}
#endif

#endif
