# Plan telemetry as billing evidence and Gödel optimization input

Tracking: #140, #142, #146, #157, #165
Legacy evidence: `SaltyPatron/Laplace#1431`

## Purpose

Laplace must account for the work it actually performs rather than charging arbitrary per-button credits or adopting conventional language-model token accounting.

The same plan/execution telemetry serves two product functions:

1. **commercial accounting** — preflight quote, throttle/admission decision, reservation, actual settlement and estimator calibration;
2. **Gödel optimization/refactoring evidence** — persistent measured plan defects can become candidates for a new index, perfcache, materialization, physical operator, resource layout or other versioned implementation improvement.

These are uses of the same evidence stream. They must not become two incompatible meters.

## `EXPLAIN` is not `EXPLAIN ANALYZE`

For PostgreSQL-backed work:

- `EXPLAIN` produces the planner's plan, estimated rows and estimated PostgreSQL cost without executing the expensive statement.
- `EXPLAIN ANALYZE` executes the statement and reports actual rows/loops/timing; `BUFFERS` and other safe instrumentation add physical access evidence.
- PostgreSQL planner `cost` is not customer currency.
- estimated rows are not actual touched rows.

The old Laplace repository already used this distinction and used `EXPLAIN (ANALYZE, BUFFERS)` to prove scan/cardinality behavior rather than inferring performance from wall clock alone. Preserve that methodology while replacing the legacy credit economy.

## Typed work vector

A plan/receipt may expose dimensions including:

- canonical entity/ID lookups;
- relation/edge examinations;
- composition/constituent expansion;
- physicality/trajectory access and calculation;
- testimony/consensus/evidence records examined;
- search/candidate/frontier nodes expanded;
- index probes and index/heap tuples where available;
- partitions/tables/indexes/pages/buffers touched;
- estimated and actual SQL rows and loops;
- filtered/rechecked/sorted/hashed/materialized rows;
- perfcache/cache hits and misses;
- bytes read/written/returned;
- native typed operator/ISA invocation counts and batch widths;
- CPU/core-time;
- memory peak and/or byte-time;
- storage and network work;
- external tool/provider work;
- artifact/output quantities;
- Unicode code-point/grapheme/byte realization quantities when material.

A record touch is a useful accounting dimension but is not a universal price unit. A hot O(1) perfcache lookup, cold heap access, GIN/GiST traversal, 64-partition append scan, sort/hash spill, numerical solve, remote provider call and artifact write have different physical costs even when each can be described loosely as touching records.

## Four certainty classes

Every preflight/postflight quantity must identify its evidence class:

- `exact-preflight` — the exact quantity is knowable before expensive execution;
- `estimated-preflight` — planner/estimator prediction;
- `hard-bound` — execution is not authorized beyond this amount;
- `actual-postflight` — measured execution fact.

Laplace-native deterministic traversals may often know more than a conventional SQL planner. Canonical ID sets, Merkle-DAG structure, run spans, materialized candidate/frontier sets, index metadata, existing world state and operation shape can sometimes make candidate cardinality exact or tightly bounded before expensive work. That fact must be recorded rather than flattened into an opaque scalar.

## Quote and settlement

```text
request
 -> canonicalize/decompose input
 -> compile typed logical program
 -> inspect/materialize cheap deterministic sets when justified
 -> derive SQL/native physical plan
 -> emit typed exact/estimated/bounded work vector
 -> apply pricing/product epoch
 -> calculate membership coverage + throttle disposition
 -> quote and reserve hard ceiling
 -> execute
 -> collect actual SQL/native counters
 -> execution receipt
 -> settle actual work / release reservation
```

The quote may be exact where the work is exactly countable, or bounded/estimated where it is not. The UI/API must state which. Final allowance consumption or variable settlement derives from the actual immutable execution receipt unless the product intentionally sells a fixed-price operation.

## Unicode and language realization

Laplace's semantic work is not defined by conventional tokenizer boundaries. A query is decomposed into canonical substrate identity and typed operations; traversal works over those identities and relations. Language is then realized/rendered from canonical state through Unicode-aware realization.

Accordingly:

- semantic execution is metered by typed traversal/operator/resource evidence;
- input/output realization may separately record code points, graphemes, bytes or other declared Unicode-aware quantities when they materially consume resources;
- competitor tokens remain comparison evidence only and cannot become native Laplace execution authority.

## Gödel optimization loop

Persist plan-vs-actual evidence and expose it to Gödel/fray analysis. Useful defect signals include:

- estimated-vs-actual cardinality error;
- repeated whole-table/whole-partition scans;
- high rows-removed-by-filter ratios;
- repeated expensive primitive signatures;
- nested-loop multiplication or repeated inner execution;
- missing, unused or systematically mis-selected indexes;
- repeated database round trips;
- repeated calculations that should use canonical/perfcache reuse;
- cache/perfcache miss patterns;
- sort/hash/materialization spills;
- topology/resource-grant mismatch;
- stable hot operation shapes whose physical implementation can change without semantic change.

A persistent measured defect can become a candidate optimization such as a new/removed index, perfcache module, cached immutable set, batch operator, query rewrite, physical provider, materialization or resource-plan change.

The candidate does not activate because Gödel proposed it. It requires representative/held-out measurement, semantic/result parity, resource-receipt comparison and the normal versioned authority gates. Historical plans and receipts remain immutable.

## Estimator feedback

For every operation class, retain:

```text
logical shape
physical plan
world/cache/topology epochs
preflight estimate
hard ceiling
actual work
prediction error
result/semantic fingerprint
```

This allows the estimator to learn from actual work while simultaneously building a performance corpus for future refactoring. A newer estimator may improve future quotes but cannot rewrite an earlier quote or execution receipt.

## Product consequence

There is no defensible universal conversion such as `$0.10 = 100 credits` unless a future display-only credit is explicitly derived from a versioned basket of measured work. The authoritative model is the typed work/resource vector plus pricing and entitlement epochs.

That lets Laplace expose transparent statements such as:

```text
preflight
  entity/id probes          exact  18,420
  relation candidates       exact  71,332
  expected heap tuples      est.   12,800
  expected buffer reads     est.      944
  CPU/core-time             est.     0.19 s
  hard CPU ceiling                  0.35 s
  membership coverage               included

postflight
  entity/id probes          actual 18,420
  relation candidates       actual 71,332
  heap tuples               actual 11,962
  buffer reads              actual    901
  CPU/core-time             actual   0.17 s
  reservation released               ...
```

The exact field set and prices are contract/version dependent; the distinction between planned, bounded and actual work is not.
